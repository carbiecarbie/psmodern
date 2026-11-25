// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/recording.h"
#include "replay/checkpoint.h"
#include "rom/sha1.h"
#include "rom/sha256.h"
#include "game/routines.h"
#include "replay/census.h"
#include "replay/verification.h"
#include "oracle/lockstep.h"
#include <cstdlib>
#include <optional>
#include <string_view>
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
// link: a playthrough link (milestone 19, label playthrough). It never skips:
// a missing ROM, manifest or start image fails, and no call may be refused.
int compare(const char* rom_path, const char* recording_path, const char* manifest_path,const char* census_path,bool link=false) {
    // A census left by an earlier run must never satisfy the census tests that
    // read it, so remove it first and write it again only after a clean match.
    if(census_path) {std::error_code ignored; std::filesystem::remove(census_path,ignored);}
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
        if (link) throw std::runtime_error(std::string("Playthrough links never skip: ")+
            (!have_rom?"ROM missing":!have_manifest?"reference manifest missing":"start image missing"));
        std::cout<<"Skipping: "<<(!have_rom?"ROM missing":!have_manifest?"reference manifest missing":"initial cartridge RAM image missing")<<".\n";
        return 77;
    }

    Machine machine(rom, cart);
    RoutineRegistry routines;
    // A recognized ROM gets game routines; authored fixtures (any other digest) get none.
    const bool recognized=game::register_routines(routines,recording.rom_sha1);
    // An authored fixture (no segment registered) runs on the oracle's core.
    std::optional<oracle::Scaffold> scaffold;
    if(!recognized) scaffold.emplace(oracle::Scaffold::originals);
    // The oracle runs the whole recording beside the engine (milestone 14):
    // compared at every instruction boundary (strict, the default since Part
    // 0b, review B2), or only at top-level segment boundaries with
    // PSMODERN_LOCKSTEP_STRICT=0, and as whole machines at every frame
    // observer. With PSMODERN_COVERAGE_DIR the instruction addresses reached
    // are written there (the run-time half of the rendered-address census).
    const char* strict_variable=std::getenv("PSMODERN_LOCKSTEP_STRICT");
    const bool strict=!(strict_variable && std::string_view(strict_variable)=="0");
    const char* coverage_dir=std::getenv("PSMODERN_COVERAGE_DIR");
    if (coverage_dir && *coverage_dir) oracle::coverage().enabled=true;
    const auto checks_before=oracle::opcode_checks();
    const auto label=std::filesystem::path(recording_path).filename().string();
    oracle::Lockstep lockstep(machine,label,strict ? oracle::Mode::instructions : oracle::Mode::segments);
    FrameInput input{};
    std::size_t change = 0;
    constexpr std::array names{"picture", "ram", "vram", "cram", "cart-ram", "cpu", "psg"};
    std::array<std::uint64_t, 7> failures{};
    const bool check_defeat=std::filesystem::path(recording_path).stem()=="party-defeat";
    bool defeated=false,returned_to_title=false;
    replay::FrameWrites frame_writes;
    std::uint64_t max_overshoot=0;
    for (std::uint64_t frame = 1; frame <= recording.frames; ++frame) {
        if (change < recording.changes.size() && recording.changes[change].frame == frame - 1)
            input = recording.changes[change++].input;
        // The seven components are taken at the frame observer: the first
        // instruction boundary at or after the frame end, at any depth.
        std::optional<replay::Manifest> actual;
        const auto overshoot=lockstep.run_frame(machine,input,&routines,[&](const Machine& m,oracle::FrameSample& sample) {
            actual=replay::manifest(m,frame_writes.take(frame,sample.writes));
            if (check_defeat) {
                const auto mode=m.peek(game::dungeon::game_mode);
                if (mode==game::dungeon::dungeon_mode && m.peek(0xC2E6)==0x49 && m.peek(0xC29D) &&
                    !m.peek(0xC401) && !m.peek(0xC411) && !m.peek(0xC421)) defeated=true;
                if (defeated && mode==3) returned_to_title=true;
                if (mode==18 || mode==19) throw std::runtime_error("Party-defeat recording entered the credits.");
            }
        });
        if (!actual) throw std::logic_error("The frame observer did not run.");
        // Overshoot invariant (review R6): every top-level step, nested
        // handlers included, ends before the next frame target.
        if (overshoot>=Machine::frame_cycles) throw std::runtime_error("A top-level step passed the next frame target.");
        max_overshoot=std::max(max_overshoot,overshoot);
        const auto& expected=reference[frame-1];
        for (std::size_t i = 0; i < actual->size(); ++i) {
            if ((*actual)[i] == expected[i]) continue;
            if (failures[i]++ == 0)
                std::cerr << names[i] << ": first difference at frame " << frame << '\n';
        }
    }
    const auto& counts=routines.census();
    std::cout<<"Lockstep "<<(strict ? "(every instruction boundary)" : "(every top-level segment boundary)")
             <<": "<<lockstep.comparisons()<<" comparisons, "<<lockstep.body_comparisons()<<" inside bodies, "
             <<lockstep.frame_comparisons()<<" whole-machine frames\n"
             <<"Interrupts delivered inside bodies: "<<counts.body_irq<<" IRQ, "<<counts.body_nmi<<" NMI; at top level "
             <<counts.top_irq<<" IRQ, "<<counts.top_nmi<<" NMI; in nested handlers "<<counts.nested_irq<<" IRQ, "
             <<counts.nested_nmi<<" NMI\n"
             <<"Maximum top-level step "<<counts.max_step<<" cycles, maximum overshoot "<<max_overshoot<<" cycles\n"
             <<"Opcode checks of rendered instructions: "<<oracle::opcode_checks()-checks_before<<'\n';
    if (coverage_dir && *coverage_dir) {
        const auto& c=oracle::coverage();
        const std::filesystem::path recording_file(recording_path);
        const auto out_path=std::filesystem::path(coverage_dir)/
            (recording_file.parent_path().filename().string()+"-"+recording_file.stem().string()+".coverage.tsv");
        std::filesystem::create_directories(out_path.parent_path());
        std::ofstream out(out_path,std::ios::binary);
        out<<"location\tboundary\n";
        for (std::size_t i=0;i<c.native.size();++i) {
            if (!c.native[i] && !c.rendered[i]) continue;
            const auto bank=static_cast<unsigned>(i/0x4000);
            const auto base=bank==0 ? 0u : bank==1 ? 0x4000u : 0x8000u;
            out<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(2)<<bank<<':'<<std::setw(4)<<(base|(i%0x4000))
               <<std::dec<<std::setfill(' ')<<'\t'<<(c.rendered[i] ? (c.native[i] ? "both" : "in_body") : "top")<<'\n';
        }
    }
    for (std::size_t i = 0; i < names.size(); ++i)
        if (failures[i]) std::cout << names[i] << ": " << failures[i] << " differing frames\n";
    if (check_defeat && !returned_to_title) throw std::runtime_error("Darkfalz defeat and return to title were not observed.");
    // The controller (00:0339) must be reached and keeps 90% everywhere.
    // With a recognized ROM the registry must hold routines and the controller must be reached.
    if(recognized && routines.empty()) throw std::runtime_error("Recognized ROM registered no routines.");
    const std::array required{RoutineEntry{0,0x0339}};
    for(const auto& [entry,routine]:routines.entries()) {
        const auto cpp=routine.counters.cpp_calls, fallback=routine.counters.fallback_calls;
        std::cout<<"Routine "<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(2)<<entry.bank<<':'<<std::setw(4)<<entry.address
                 <<std::dec<<std::setfill(' ')<<" C++ calls: "<<cpp<<", fallbacks: "<<fallback
                 <<(cpp+fallback<replay::min_gated_calls?" (not gated)":"")<<'\n';
    }
    if(const auto problems=replay::checkpoint_problems(routines,recognized ? std::span<const RoutineEntry>(required) : std::span<const RoutineEntry>());!problems.empty()) {
        std::string text="Checkpoint census rule failed:";
        for(const auto& problem:problems) text+="\n  "+problem;
        throw std::runtime_error(text);
    }
    // A link's run makes no refused call at all (a refused call stops the program).
    if(link) for(const auto& [entry,routine]:routines.entries())
        if(routine.counters.fallback_calls) throw std::runtime_error("Refused calls in a playthrough link.");
    const bool matched=std::none_of(failures.begin(), failures.end(), [](auto n) { return n != 0; });
    if(census_path && matched) {
        const std::filesystem::path path(census_path);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream census(path,std::ios::binary);
        replay::write_census(census,routines);
        census.close();if(!census)throw std::runtime_error("Cannot write checkpoint census.");
        auto transition_path=path; transition_path.replace_extension(".transition.tsv");
        std::ofstream transition(transition_path,std::ios::binary);
        replay::write_transition_census(transition,routines);
        transition.close();if(!transition)throw std::runtime_error("Cannot write checkpoint transition census.");
    }
    return matched ? 0 : 1;
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 6 && std::string_view(argv[1]) == "--link") return compare(argv[2], argv[3], argv[4], argv[5], true);
        if (argc != 4 && argc != 5) throw std::runtime_error("Expected [--link] ROM, recording, manifest and (required with --link) census path.");
        return compare(argv[1], argv[2], argv[3],argc==5 ? argv[4] : nullptr);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
