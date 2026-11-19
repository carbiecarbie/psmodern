// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/menu.h"
#include "game/text.h"
#include <array>
namespace psmodern::game {
void register_menu_remainder(RoutineRegistry& registry);
// The stretches between the segments of a menu file, each a segment of its own
// (milestone 14), registered by register_menu_remainder; each file lists its
// entries below.
void register_menu_engine_joins(RoutineRegistry& registry);
void register_menu_windows_joins(RoutineRegistry& registry);
void register_menu_field_joins(RoutineRegistry& registry);
void register_menu_places_joins(RoutineRegistry& registry);
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
    at(0x39C2,menu::save_slots_unit),at(0x3A89,menu::draw_window_at_once_unit),
    // The stretches that follow an interrupt switch (milestone 14).
    at(0x3030,menu::number_unit),at(0x3035,menu::number_unit),
    at(0x30A1,menu::long_number_unit),at(0x30A6,menu::long_number_unit),at(0x319D,menu::enemy_name_line_unit),
    at(0x332B,text_number_unit),at(0x33C3,text_alternate_unit),at(0x33C7,text_alternate_unit),
    at(0x3411,text_output_unit),at(0x35B6,menu::item_name_line_unit),at(0x35BB,menu::item_name_line_unit),
    at(0x3633,menu::money_unit),at(0x3638,menu::money_unit),at(0x3A7A,menu::row_drawer_unit),
    at(0x3AA1,menu::draw_window_at_once_unit),at(0x3AC3,menu::save_under_unit),
    // menu_engine.cpp
    at(0x3161,menu::enemy_name_line_unit),at(0x3432,menu::dialogue_scroll_two_unit),at(0x3457,menu::dialogue_scroll_unit),
    at(0x3A6B,menu::row_drawer_unit),at(0x3A6E,menu::row_drawer_unit),at(0x3A73,menu::row_drawer_unit),
    at(0x3A82,menu::row_drawer_unit),at(0x3A8F,menu::draw_window_at_once_unit),at(0x3AAD,menu::save_under_unit),
    // menu_windows.cpp
    at(0x2F1B,menu::member_box_unit),at(0x2FA1,menu::member_box_refresh_unit),at(0x2FA4,menu::member_box_refresh_unit),at(0x2FAF,menu::member_box_refresh_unit),
    at(0x2FBD,menu::member_box_refresh_unit),at(0x3539,menu::item_list_unit),at(0x356A,menu::item_list_unit),
    at(0x3676,menu::member_choice_unit),at(0x3693,menu::battle_member_choice_unit),at(0x3859,menu::status_window_unit),
    at(0x3A04,menu::money_window_unit),
    // menu_field.cpp
    at(0x1BEB,menu::field_menu_unit),
    at(0x1C0D,menu::field_menu_unit),
    at(0x1C10,menu::field_menu_unit),
    at(0x1C26,menu::field_menu_unit),
    at(0x1CA1,menu::stats_unit),
    at(0x1CB1,menu::stats_unit),
    at(0x1CB4,menu::stats_unit),
    at(0x1CD0,menu::stats_unit),
    at(0x1CD3,menu::stats_unit),
    at(0x1CD6,menu::stats_unit),
    at(0x1CD9,menu::stats_unit),
    at(0x1CDC,menu::stats_unit),
    at(0x1CE5,menu::save_unit),
    at(0x1CEE,menu::save_unit),
    at(0x1D3B,menu::save_unit),
    at(0x1D3E,menu::save_unit),
    at(0x1D4D,menu::magic_unit),
    at(0x1DB7,menu::magic_unit),
    at(0x1DBA,menu::magic_unit),
    at(0x1DBD,menu::magic_unit),
    at(0x1DCB,menu::magic_unit),
    at(0x1DCE,menu::magic_unit),
    at(0x1DD7,menu::magic_unit),
    at(0x21BB,menu::item_unit),
    at(0x21BE,menu::item_unit),
    at(0x21F2,menu::item_unit),
    at(0x21F5,menu::item_exit_unit),
    at(0x21F8,menu::item_unit),
    at(0x26E8,menu::equip_unit),
    at(0x2733,menu::equip_unit),
    at(0x2736,menu::equip_unit),
    at(0x2739,menu::equip_unit),
    at(0x2741,menu::equip_unit),
    at(0x2744,menu::equip_unit),
    at(0x274D,menu::equip_unit),
    at(0x276D,menu::drop_unit),
    at(0x2776,menu::drop_unit),
    at(0x2779,menu::drop_unit),
    at(0x27C2,menu::add_item_unit),
    at(0x27CD,menu::add_item_unit),
    at(0x27E2,menu::add_item_unit),
    at(0x27E5,menu::add_item_unit),
    at(0x2849,menu::search_unit),
    at(0x284C,menu::search_unit),
    at(0x28D8,menu::search_unit),
    at(0x28E1,menu::chest_unit),
    at(0x28E8,menu::chest_unit),
    at(0x2945,menu::chest_unit),
    // menu_places.cpp
    at(0x2993,menu::hospital_unit),
    at(0x29A3,menu::hospital_unit),
    at(0x29B9,menu::hospital_unit),
    at(0x29FE,menu::hospital_unit),
    at(0x2A01,menu::hospital_unit),
    at(0x2A34,menu::hospital_unit),
    at(0x2A43,menu::hospital_unit),
    at(0x2A48,menu::hospital_unit),
    at(0x2A5B,menu::hospital_unit),
    at(0x2A61,menu::hospital_unit),
    at(0x2A6A,menu::hospital_unit),
    at(0x2A91,menu::church_unit),
    at(0x2AA5,menu::church_unit),
    at(0x2ACE,menu::church_unit),
    at(0x2AD1,menu::church_unit),
    at(0x2B12,menu::church_unit),
    at(0x2B1B,menu::church_unit),
    at(0x2B23,menu::church_unit),
    at(0x2B2C,menu::church_unit),
    at(0x2B52,menu::church_unit),
    at(0x2B55,menu::church_unit),
    at(0x2BC6,menu::shop_unit),
    at(0x2BD1,menu::shop_unit),
    at(0x2BD8,menu::shop_unit),
    at(0x2C28,menu::shop_unit),
    at(0x2C33,menu::shop_unit),
    at(0x2C36,menu::shop_unit),
    at(0x2C39,menu::shop_unit),
    at(0x2C9E,menu::buy_sell_shop_unit),
    at(0x2CB7,menu::sell_unit),
    at(0x2CE7,menu::sell_unit),
    at(0x2CF0,menu::sell_unit),
    at(0x2CF9,menu::sell_unit),
    at(0x2D10,menu::sell_unit)
};
}
}
