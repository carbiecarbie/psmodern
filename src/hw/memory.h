// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace psmodern {

class Memory {
public:
    enum class Kind { rom, cartridge_ram, ram, unmapped };
    struct Mapping { Kind kind; unsigned bank; bool operator==(const Mapping&) const = default; };
    using Rom = std::vector<std::uint8_t>;
    using CartridgeRam = std::array<std::uint8_t, 8192>;

    explicit Memory(std::shared_ptr<const Rom> rom, CartridgeRam cartridge_ram = {});

    std::uint8_t read(std::uint16_t address) const;
    Mapping mapping(std::uint16_t address) const;
    void write(std::uint16_t address, std::uint8_t value);
    void set_memory_control(std::uint8_t value) { memory_control_ = value; }
    std::uint8_t memory_control() const { return memory_control_; }
    bool io_enabled() const { return !(memory_control_ & 0x04); }
    const CartridgeRam& cartridge_ram() const { return cartridge_ram_; }
    bool cartridge_ram_dirty() const { return cartridge_ram_dirty_; }
    void clear_cartridge_ram_dirty() { cartridge_ram_dirty_ = false; }
    bool operator==(const Memory&) const = default;

private:
    std::shared_ptr<const Rom> rom_;
    std::array<std::uint8_t, 8192> ram_{};
    CartridgeRam cartridge_ram_{};
    std::array<std::uint8_t, 3> banks_{0, 1, 2};
    std::uint8_t mapper_control_ = 0;
    std::uint8_t memory_control_ = 0xab;
    bool cartridge_ram_dirty_ = false;
};
}
