// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/recording.h"
#include "replay/checkpoint.h"
#include "rom/sha1.h"
#include "rom/sha256.h"
#include "game/routines.h"
#include "replay/census.h"
#include "replay/verification.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
using namespace psmodern;
std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open checkpoint input.");
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
    if (in.bad()) throw std::runtime_error("Cannot read checkpoint input.");
    return bytes;
}
int compare(const char* rom_path, const char* recording_path, const char* manifest_path,const char* census_path) {
    // Validate every available input before reporting missing dependencies.
    const bool have_recording=std::filesystem::exists(recording_path);
    const bool have_rom=std::filesystem::exists(rom_path);
    const bool have_manifest=std::filesystem::exists(manifest_path);
    replay::Recording recording;
    if (have_recording) {
        recording=replay::read(recording_path);
        if (!recording.frames) throw std::runtime_error("Checkpoint requires at least one frame.");
    }
    auto rom=std::make_shared<Memory::Rom>();
    if (have_rom) {
        *rom=read_bytes(rom_path);
        if (rom->size()!=512*1024 || (have_recording && sha1(*rom)!=recording.rom_sha1))
            throw std::runtime_error("Incorrect ROM digest or size.");
    }
    Memory::CartridgeRam cart{};
    // The image lives beside the manifest; without a manifest path there is
    // no such place, and the working directory must not stand in for it.
    const auto image_path = std::filesystem::path(manifest_path).parent_path() / "cart-ram.bin";
    const bool have_image=*manifest_path && std::filesystem::exists(image_path);
    if (have_image) {
        std::ifstream image(image_path, std::ios::binary);
        if (!image || !image.read(reinterpret_cast<char*>(cart.data()), cart.size()) ||
            image.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("Cartridge RAM image must be exactly 8192 bytes.");
        if (have_recording && sha1(cart)!=recording.cart_ram_sha1)
            throw std::runtime_error("Incorrect ROM or initial cartridge RAM digest.");
    }
    std::ifstream manifest(manifest_path);
    std::string line;
    std::vector<std::array<std::string,7>> reference;
    if (have_manifest) {
        if (!std::getline(manifest,line) || line!="frame picture ram vram cram cart-ram cpu psg")
            throw std::runtime_error("Invalid manifest header.");
        if (!std::getline(manifest,line) || line!="reference mesence 1")
            throw std::runtime_error("Missing or invalid reference provenance (runner manifests are not references).");
        auto field=[&](const std::string& key) {
            if (!std::getline(manifest,line) || !line.starts_with(key+" "))
                throw std::runtime_error("Missing reference provenance: "+key);
            return line.substr(key.size()+1);
        };
        auto digest=[](const std::string& value) {
            if (value.size()!=64 || value.find_first_not_of("0123456789abcdef")!=value.npos)
                throw std::runtime_error("Invalid reference provenance digest.");
        };
        const auto build=field("build-sha256");digest(build);
        if (build!=PSMODERN_REFERENCE_SHA256)
            throw std::runtime_error("Reference build SHA-256 mismatch.");
        const auto encoded_label=field("build-label");
        std::string label,extra;
        std::istringstream label_stream(encoded_label);
        if (encoded_label.empty() || encoded_label.front()!='"' || encoded_label.back()!='"' ||
            !(label_stream>>std::quoted(label)) || (label_stream>>extra) ||
            label.empty() || label.find_first_not_of(" \t\r")==label.npos)
            throw std::runtime_error("Empty reference build label.");
        const auto recorded=field("recording-sha256"); digest(recorded);
        if (have_recording && recorded!=sha256(read_bytes(recording_path)))
            throw std::runtime_error("Reference recording digest mismatch.");
        while (std::getline(manifest,line)) {
            std::istringstream row(line);
            std::string number;
            row>>number;
            if (number!=std::to_string(reference.size()+1)) throw std::runtime_error("Invalid frame number.");
            std::array<std::string,7> expected;
            for (auto& hash:expected)
                if (!(row>>hash) || hash.size()!=40 || hash.find_first_not_of("0123456789abcdef")!=hash.npos)
                    throw std::runtime_error("Invalid reference hash.");
            if (row>>number) throw std::runtime_error("Extra manifest column.");
            reference.push_back(std::move(expected));
        }
        if (!manifest.eof()) throw std::runtime_error("Unreadable manifest data.");
        if (reference.empty() || (have_recording && reference.size()!=recording.frames))
            throw std::runtime_error("Missing or extra reference frame.");
    }
    // Recordings belong to the repository: with the ROM present, a missing
    // one is a defect, not an absent external dependency.
    if (have_rom && !have_recording)
        throw std::runtime_error("Checkpoint recording is missing: "+std::string(recording_path));
    if (!have_rom || !have_manifest || (!have_image && sha1(cart)!=recording.cart_ram_sha1)) {
        std::cout<<"Skipping: "<<(!have_rom?"ROM missing":!have_manifest?"reference manifest missing":"initial cartridge RAM image missing")<<".\n";
        return 77;
    }

    Machine machine(rom, cart);
    RoutineRegistry routines;
    game::register_routines(routines,recording.rom_sha1);
    replay::CallVerifier verifier(std::filesystem::path(recording_path).filename().string());
    routines.verifier=&verifier;
    Machine original(rom,cart);
    RoutineRegistry disabled;
    game::register_routines(disabled,recording.rom_sha1);
    disabled.enabled=false;
    FrameInput input{};
    std::size_t change = 0;
    constexpr std::array names{"picture", "ram", "vram", "cram", "cart-ram", "cpu", "psg"};
    std::array<std::uint64_t, 7> failures{};
    replay::FrameWrites frame_writes;
    replay::FrameWrites original_writes;
    for (std::uint64_t frame = 1; frame <= recording.frames; ++frame) {
        if (change < recording.changes.size() && recording.changes[change].frame == frame - 1)
            input = recording.changes[change++].input;
        machine.run_frame(input,routines);
        auto psg=frame_writes.take(frame,machine.take_psg_writes());
        machine.take_samples();
        original.run_frame(input,disabled);
        auto native_psg=original_writes.take(frame,original.take_psg_writes());
        original.take_samples();
        // One set of digests serves both comparisons: routines on/off and the reference.
        const auto actual=replay::manifest(machine,psg);
        replay::compare_manifests(std::filesystem::path(recording_path).filename().string(),frame,
            replay::manifest(original,native_psg),actual);
        const auto& expected=reference[frame-1];

        for (std::size_t i = 0; i < actual.size(); ++i) {
            if (actual[i] == expected[i]) continue;
            if (failures[i]++ == 0)
                std::cerr << names[i] << ": first difference at frame " << frame << '\n';
        }
    }
    for (std::size_t i = 0; i < names.size(); ++i)
        if (failures[i]) std::cout << names[i] << ": " << failures[i] << " differing frames\n";
    for(const auto& [entry,routine]:routines.entries()) {
        const auto cpp=routine.counters.cpp_calls, fallback=routine.counters.fallback_calls;
        if(cpp==0 || static_cast<double>(cpp)/(cpp+fallback)<0.9)
            throw std::runtime_error("Checkpoint did not reach 90% C++ calls.");
        std::cout<<"Routine C++ calls: "<<cpp<<", fallbacks: "<<fallback<<'\n';
    }
    if(census_path) {
        const std::filesystem::path path(census_path);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream census(path,std::ios::binary);
        replay::write_census(census,routines);
        census.close();if(!census)throw std::runtime_error("Cannot write checkpoint census.");
    }
    return std::any_of(failures.begin(), failures.end(), [](auto n) { return n != 0; });
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4 && argc != 5) throw std::runtime_error("Expected ROM, recording, manifest and optional census path.");
        return compare(argv[1], argv[2], argv[3],argc==5 ? argv[4] : nullptr);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
