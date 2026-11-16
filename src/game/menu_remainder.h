// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/menu.h"
#include "game/text.h"
#include <array>
namespace psmodern::game {
void register_menu_remainder(RoutineRegistry& registry);
namespace menu_remainder {
struct Segment { RoutineEntry entry, unit; };
constexpr Segment at(std::uint16_t address,RoutineEntry unit) { return {{0,address},unit}; }
inline constexpr std::array segments{
    at(0x2FD8,menu::number_wide_label_unit),at(0x2FE1,menu::number_unit),
    at(0x3036,menu::long_number_unit),at(0x315C,menu::enemy_name_line_unit),
    at(0x3304,text_number_unit),at(0x33C2,text_alternate_unit),
    at(0x332A,text_number_unit),at(0x33C6,text_alternate_unit),
    at(0x33D6,text_output_unit),at(0x356B,menu::item_name_line_unit),
    at(0x35BC,menu::money_wide_label_unit),at(0x35C4,menu::money_unit),
    at(0x393A,menu::shop_line_unit),at(0x3A69,menu::row_drawer_unit),
    at(0x3A8C,menu::draw_window_at_once_unit),at(0x3AA6,menu::save_under_unit),
    at(0x302F,menu::number_unit),at(0x30A0,menu::long_number_unit),
    at(0x319C,menu::enemy_name_line_unit),at(0x3410,text_output_unit),
    at(0x35B5,menu::item_name_line_unit),at(0x3632,menu::money_unit),
    at(0x3A79,menu::row_drawer_unit),at(0x3AA0,menu::draw_window_at_once_unit),
    at(0x3AC2,menu::save_under_unit),
    at(0x319A,menu::enemy_name_line_unit),at(0x340F,text_output_unit),
    at(0x35B3,menu::item_name_line_unit),at(0x3AC0,menu::save_under_unit),
    at(0x3A68,menu::row_drawer_unit),at(0x39B1,menu::save_slots_unit),
    at(0x39C2,menu::save_slots_unit),at(0x3A89,menu::draw_window_at_once_unit)
};
// These exact loads are the instruction protected by the preceding EI.
// They execute on the core to retain its interrupt-delay bookkeeping.
inline constexpr std::array<RoutineEntry,7> ei_delay_loads{{
    {0,0x3030},{0,0x30A1},{0,0x332B},{0,0x3411},{0,0x35B6},{0,0x3633},{0,0x3A7A}}};
// The same native boundary returns to a caller, or restores the saved bank.
inline constexpr std::array<RoutineEntry,4> ei_delay_epilogues{{
    {0,0x319D},{0,0x33C7},{0,0x3AA1},{0,0x3AC3}}};
}
}
