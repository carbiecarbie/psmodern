// SPDX-License-Identifier: GPL-3.0-or-later
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include "rom/rom.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) { std::cerr << "Usage: psmodern <rom-path>\n"; return 1; }
    try {
        // Reference SDL without creating a window, so the runtime link is exercised.
        SDL_version version{};
        SDL_GetVersion(&version);
        const auto rom = psmodern::identify_rom(argv[1]);
        std::cout << "Phantasy Star: " << psmodern::rom_name(rom) << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
