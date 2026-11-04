// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/memory.h"
#include <stdexcept>
#include <iostream>
#include <memory>

void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void run() {
    auto rom = std::make_shared<psmodern::Memory::Rom>(512 * 1024);
    for (unsigned bank = 0; bank < 32; ++bank)
        for (unsigned offset = 0; offset < 0x4000; ++offset)
            (*rom)[bank * 0x4000 + offset] = static_cast<std::uint8_t>(bank);
    psmodern::Memory memory(rom);
    check(memory.read(0xc000) == 0xab && memory.read(0xe000) == 0xab, "memory mapping");
    check(memory.read(0x0400) == 0 && memory.read(0x4000) == 1 && memory.read(0x8000) == 2, "memory mapping");
    memory.write(0xfffd, 0xff);
    check(memory.read(0xfffd) == 0xff && memory.read(0x03ff) == 0 && memory.read(0x0400) == 31, "memory mapping");
    memory.write(0xfffe, 0x22);
    memory.write(0xffff, 0x23);
    check(memory.read(0x4000) == 2 && memory.read(0x8000) == 3, "memory mapping");
    memory.write(0xc123, 0x42);
    check(memory.read(0xe123) == 0x42, "memory mapping");
    memory.write(0xfffc, 8);
    memory.write(0x8001, 0x5a);
    check(memory.cartridge_ram_dirty() && memory.read(0xa001) == 0x5a, "memory mapping");
    memory.clear_cartridge_ram_dirty();
    memory.write(0xfffc, 12);
    check(memory.read(0x8001) == 0x5a && !memory.cartridge_ram_dirty(), "memory mapping");
    memory.write(0x8001, 0x5a);
    check(!memory.cartridge_ram_dirty(), "memory mapping");
    memory.write(0xfffc, 0);
    check(memory.read(0x8000) == 3, "memory mapping");
    for (unsigned reg=0xfffc; reg<=0xffff; ++reg) {
        memory.write(static_cast<std::uint16_t>(reg),0x9b);
        check(memory.read(static_cast<std::uint16_t>(reg))==0x9b, "mapper write-through");
        check(memory.read(static_cast<std::uint16_t>(reg-0x2000))==0x9b, "mapper RAM mirror");
    }
    psmodern::Memory::CartridgeRam saved{}; saved[0x1fff]=0x72;
    psmodern::Memory saved_memory(rom,saved);
    saved_memory.write(0xfffc,8);
    check(saved_memory.read(0x9fff)==0x72 && saved_memory.read(0xbfff)==0x72,"cartridge image and mirrors");
    check(!saved_memory.cartridge_ram_dirty(),"loaded cartridge RAM is clean");
    saved_memory.write(0xbfff,0x71);
    check(saved_memory.cartridge_ram()[0x1fff]==0x71 && saved_memory.cartridge_ram_dirty(),"cartridge image output");
    saved_memory.set_memory_control(0x50);
    check(saved_memory.read(0xc000)==0xff && saved_memory.read(0x8000)==0xff,"disabled RAM and cartridge");
    saved_memory.set_memory_control(0);
    check(saved_memory.read(0xc000)==0xab && saved_memory.read(0x9fff)==0x71,"re-enable memory");
}

int main() { try { run(); std::cout << "Memory tests passed.\n"; return 0; } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; } }
