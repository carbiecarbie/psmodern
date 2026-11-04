// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/memory.h"
#include <stdexcept>

namespace psmodern {
Memory::Memory(std::shared_ptr<const Rom> rom, CartridgeRam cartridge_ram)
    : rom_(std::move(rom)), cartridge_ram_(cartridge_ram) {
    if (!rom_ || rom_->size() != 512 * 1024) throw std::invalid_argument("Expected a 512 KiB ROM image");
}

Memory::Mapping Memory::mapping(std::uint16_t address) const {
    if (address >= 0xc000) return {(memory_control_ & 0x10) ? Kind::unmapped : Kind::ram, 0};
    if (memory_control_ & 0x40) return {Kind::unmapped, 0};
    if (address >= 0x8000 && (mapper_control_ & 0x08)) return {Kind::cartridge_ram, 0};
    const unsigned slot = address >> 14;
    const unsigned bank = address < 0x0400 ? 0 : banks_[slot];
    return {Kind::rom, bank};
}

std::uint8_t Memory::read(std::uint16_t address) const {
    const auto m = mapping(address);
    switch (m.kind) {
    case Kind::ram: return ram_[address & 0x1fff];
    case Kind::cartridge_ram: return cartridge_ram_[address & 0x1fff];
    case Kind::rom: return (*rom_)[m.bank * 0x4000 + (address & 0x3fff)];
    case Kind::unmapped: return 0xff;
    }
    return 0xff;
}

void Memory::write(std::uint16_t address, std::uint8_t value) {
    if (address >= 0xc000) {
        if (!(memory_control_ & 0x10)) ram_[address & 0x1fff] = value;
        if (!(memory_control_ & 0x40)) {
            // Only bit 3 maps slot 2 to RAM. Bit 2 selects a 16 KiB bank,
            // absent on the 8 KiB chip; the remaining features are outside
            // this cartridge's observed use. All bits still write through.
            if (address == 0xfffc) mapper_control_ = value;
            else if (address >= 0xfffd) banks_[address - 0xfffd] = value & 31;
        }
    } else if (address >= 0x8000 && !(memory_control_ & 0x40) && (mapper_control_ & 0x08)) {
        auto& byte = cartridge_ram_[address & 0x1fff];
        if (byte != value) { byte = value; cartridge_ram_dirty_ = true; }
    }
}
}
