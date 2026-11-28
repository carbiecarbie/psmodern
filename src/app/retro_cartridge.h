// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace psmodern::app::retro {
// A read-only view of the selected cartridge and one captured work-RAM image.
// Slot 2 reads name the desired bank explicitly and never touch the live mapper.
class Cartridge {
public:
    Cartridge(std::span<const std::uint8_t> rom, std::span<const std::uint8_t,8192> ram)
        : rom_(rom), ram_(ram) {}
    unsigned byte(unsigned address, unsigned bank=0) const;
    unsigned word(unsigned address, unsigned bank=0) const;
    std::string name(unsigned address, unsigned width, unsigned bank=0) const;
    std::string member_name(unsigned member) const;
    std::string item_name(unsigned item) const;
    std::string enemy_name() const;
    // Reads a row of pre-tiled letters in the ROM window layout.
    std::string tile_row(unsigned address, unsigned columns, unsigned bank=27) const;
    unsigned spell_id(unsigned group, unsigned index) const;
    std::string spell_name(unsigned group, unsigned index) const;
    unsigned spell_cost(unsigned spell) const;
    struct ShopItem { unsigned id, price; };
    ShopItem shop_item(unsigned row) const;
    unsigned shop_count() const;
    struct Comparison {
        unsigned slot, before, after;
        bool equipment, eligible, equipped;
    };
    Comparison compare(unsigned member, unsigned item) const;
    std::optional<unsigned> next_experience(unsigned member) const;
private:
    unsigned level_row(unsigned member, unsigned level) const;
    unsigned shop_table() const;
    std::span<const std::uint8_t> rom_;
    std::span<const std::uint8_t,8192> ram_;
};
char cartridge_character(unsigned code);
}
