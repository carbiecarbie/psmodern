// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/recording.h"
#include "rom/sha1.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace psmodern;
int compare(const char* rom_path, const char* recording_path, const char* manifest_path) {
    if (!std::filesystem::exists(rom_path) || !std::filesystem::exists(manifest_path)) {
        std::cout << "Skipping: ROM or reference manifest missing.\n";
        return 77;
    }
    const auto recording = replay::read(recording_path);
    std::ifstream file(rom_path, std::ios::binary);
    auto rom = std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),
                                             std::istreambuf_iterator<char>());
    Memory::CartridgeRam cart{};
    const auto image_path = std::filesystem::path(manifest_path).parent_path() / "cart-ram.bin";
    if (std::filesystem::exists(image_path)) {
        std::ifstream image(image_path, std::ios::binary);
        if (!image || !image.read(reinterpret_cast<char*>(cart.data()), cart.size()) ||
            image.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("Cartridge RAM image must be exactly 8192 bytes.");
    } else if (sha1(cart) != recording.cart_ram_sha1) {
        std::cout << "Skipping: initial cartridge RAM image missing.\n";
        return 77;
    }
    if (file.bad() || sha1(*rom) != recording.rom_sha1 || sha1(cart) != recording.cart_ram_sha1)
        throw std::runtime_error("Incorrect ROM or initial cartridge RAM digest.");
    std::ifstream manifest(manifest_path);
    std::string line;
    if (!std::getline(manifest, line) || line != "frame picture ram vram cram cart-ram")
        throw std::runtime_error("Invalid manifest header.");

    Machine machine(rom, cart);
    FrameInput input{};
    std::size_t change = 0;
    constexpr std::array names{"picture", "ram", "vram", "cram", "cart-ram"};
    std::array<std::uint64_t, 5> failures{};
    for (std::uint64_t frame = 1; frame <= recording.frames; ++frame) {
        if (change < recording.changes.size() && recording.changes[change].frame == frame - 1)
            input = recording.changes[change++].input;
        machine.run_frame(input);
        machine.take_psg_writes();
        machine.take_samples();
        const auto& vram = machine.vdp().vram();
        std::array actual{sha1(machine.picture()), sha1(machine.memory().ram()), sha1(vram),
                          sha1(machine.vdp().cram()), sha1(machine.memory().cartridge_ram())};
        if (!std::getline(manifest, line)) throw std::runtime_error("Missing reference frame.");
        std::istringstream row(line);
        std::string number;
        row >> number;
        if (number != std::to_string(frame)) throw std::runtime_error("Invalid frame number.");
        std::array<std::string, 5> expected;
        for (auto& hash : expected)
            if (!(row >> hash) || hash.size() != 40 || hash.find_first_not_of("0123456789abcdef") != hash.npos)
                throw std::runtime_error("Invalid reference hash.");
        if (row >> number) throw std::runtime_error("Extra manifest column.");

        for (std::size_t i = 0; i < actual.size(); ++i) {
            if (actual[i] == expected[i]) continue;
            if (failures[i]++ == 0)
                std::cerr << names[i] << ": first difference at frame " << frame << '\n';
        }
    }
    if (std::getline(manifest, line) || !manifest.eof()) throw std::runtime_error("Extra or unreadable manifest data.");
    for (std::size_t i = 0; i < names.size(); ++i)
        if (failures[i]) std::cout << names[i] << ": " << failures[i] << " differing frames\n";
    return std::any_of(failures.begin(), failures.end(), [](auto n) { return n != 0; });
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 4) throw std::runtime_error("Expected ROM, recording and manifest.");
        return compare(argv[1], argv[2], argv[3]);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
