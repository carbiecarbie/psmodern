// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace psmodern::app::retro {
class Cartridge;

// The pictured name table is also the record of what each native window has
// drawn. A restored row no longer matches its cartridge layout.
struct FieldMotion {
    bool active=false;
    std::array<unsigned,4> cards{};
    std::array<unsigned,4> party_hp{}, party_mp{};
    unsigned actor_rows=0, command_rows=0, enemy_rows=0, enemy_stat_rows=0;
    std::optional<unsigned> actor_member, command_arrow;
    std::array<unsigned,8> enemy_hp{};
    FieldMotion()=default;
    FieldMotion(const Cartridge& data,std::span<const std::uint8_t,64*24> name_table,bool in_battle=false);
    bool cards_visible() const;
    bool visible() const;
};
}
