// SPDX-License-Identifier: GPL-3.0-or-later
#include "rom/rom.h"
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include <string_view>
int main(int argc, char** argv) {
    if (argc != 3) return 1;
    if (*argv[1] == '\0' || !std::filesystem::exists(argv[1])) {
        std::cout << "ROM path missing; skipping.\n"; return 77;
    }
    try {
        auto expected = std::string_view(argv[2]) == "us" ? psmodern::RomVersion::us_rev_a : psmodern::RomVersion::brazilian;
        if (psmodern::identify_rom(argv[1]) != expected) throw std::runtime_error("Wrong ROM version.");
        std::cout << "ROM identified.\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
