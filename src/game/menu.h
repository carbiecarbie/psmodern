// SPDX-License-Identifier: GPL-3.0-or-later
// Menus and windows (milestone 12): the rewritten units, their planned
// segment entries per part, the code each part may register in, and the
// instructions that stay native. Named constants only; every table byte and
// operand is read from the player's ROM at run time.
//
// The six parts each own one source file and register only there:
//   engine    menu_engine.cpp    drawing and saving windows, the printers with interrupts off
//   windows   menu_windows.cpp   every window the game opens, field and battle
//   select    menu_select.cpp    the cursor loop, yes or no, the waits, the cursor blink and the menu frame handlers
//   field     menu_field.cpp     the field menu: status, save, magic, items, search and chests
//   places    menu_places.cpp    the hospital, the church and the shops
//   fades     menu_fades.cpp     palette fades, flashes and palette effects
// The planned lists below are the segment design (the exits, stack offsets and
// permissions of each entry are in the part briefs). A part may register an
// entry that is not listed only inside its own code ranges, with a unit of
// its own, and must report it (tests/menu_base_tests.cpp and
// tests/game_routine_tests.cpp accept exactly that).
#pragma once
#include "machine/routines.h"
#include <array>
#include <cstdint>
#include <span>

namespace psmodern::game::menu {
// ---- Work RAM the menus share (ROM static, milestone 12 study) -------------------------
inline constexpr std::uint16_t game_mode=0xC202;          // the main dispatcher's mode byte
inline constexpr std::uint16_t pressed_buttons=0xC205;    // buttons newly pressed this frame (controller poll)
inline constexpr std::uint16_t sound_request=0xC004;      // sound request byte (ordinary stores)
inline constexpr std::uint16_t palette=0xC220, palette_target=0xC240;   // palette buffer (32 bytes) and the fade-in target
inline constexpr std::uint16_t selection_active=0xC268;   // non-zero while a cursor loop runs (the blink draws)
inline constexpr std::uint16_t cursor_address=0xC269;     // VRAM address of the first option's cursor cell (word)
inline constexpr std::uint16_t choice=0xC26B;             // the option under the cursor; C26Ch the one last drawn
inline constexpr std::uint16_t blink_phase=0xC26D;        // cursor blink counter
inline constexpr std::uint16_t option_count=0xC26E;       // options of the running selection minus one
inline constexpr std::uint16_t item_page=0xC299;          // first item shown and the page count (word)
inline constexpr std::uint16_t flash_timer=0xC2BE, flash_range=0xC2C0;
inline constexpr std::uint16_t current_character=0xC2C2;
inline constexpr std::uint16_t selection_result=0xC2C5;   // also the dialogue number word
inline constexpr std::uint16_t dialogue_open=0xC2D3;      // the dialogue window is open
inline constexpr std::uint16_t money=0xC4E0, inventory_count=0xC4E2;
inline constexpr std::uint16_t window_buffers_first=0xD700, window_buffers_last=0xDEAF;   // what lies under each window kind

// ---- Parts, units and segments ---------------------------------------------------
enum class Part : unsigned { engine, windows, select, field, places, fades, count };
inline constexpr std::array<const char*,static_cast<unsigned>(Part::count)> part_names{
    "engine","windows","select","field","places","fades"};
struct Segment { RoutineEntry entry, unit; };
struct Range { unsigned bank; std::uint16_t first, last; };   // inclusive CPU addresses

constexpr RoutineEntry at0(std::uint16_t address) { return {0,address}; }
constexpr RoutineEntry at1(std::uint16_t address) { return {1,address}; }

// -- engine
inline constexpr RoutineEntry number_wide_label_unit=at0(0x2FD8);
inline constexpr RoutineEntry number_unit=at0(0x2FE1);
inline constexpr RoutineEntry long_number_unit=at0(0x3036);
inline constexpr RoutineEntry enemy_name_line_unit=at0(0x315C);
inline constexpr RoutineEntry dialogue_scroll_two_unit=at0(0x342C);
inline constexpr RoutineEntry dialogue_scroll_unit=at0(0x3439);
inline constexpr RoutineEntry item_name_line_unit=at0(0x356B);
inline constexpr RoutineEntry money_wide_label_unit=at0(0x35BC);
inline constexpr RoutineEntry money_unit=at0(0x35C4);
inline constexpr RoutineEntry digit_unit=at0(0x3645);
inline constexpr RoutineEntry shop_line_unit=at0(0x393A);
inline constexpr RoutineEntry draw_window_unit=at0(0x3A57);
inline constexpr RoutineEntry row_drawer_unit=at0(0x3A68);
inline constexpr RoutineEntry draw_window_at_once_unit=at0(0x3A83);
inline constexpr RoutineEntry save_under_unit=at0(0x3AA6);
inline constexpr std::array<Segment,29> engine_segments{{
    {at0(0x2FD9),number_wide_label_unit}, {at0(0x2FE2),number_unit}, {at0(0x3037),long_number_unit},
    {at0(0x315D),enemy_name_line_unit}, {at0(0x316F),enemy_name_line_unit}, {at0(0x3188),enemy_name_line_unit},
    {at0(0x342C),dialogue_scroll_two_unit}, {at0(0x3435),dialogue_scroll_two_unit},
    {at0(0x3439),dialogue_scroll_unit}, {at0(0x3445),dialogue_scroll_unit}, {at0(0x345A),dialogue_scroll_unit},
    {at0(0x3461),dialogue_scroll_unit}, {at0(0x356C),item_name_line_unit}, {at0(0x35BD),money_wide_label_unit},
    {at0(0x35C5),money_unit}, {at0(0x3645),digit_unit}, {at0(0x393B),shop_line_unit}, {at0(0x3A57),draw_window_unit},
    {at0(0x3A63),draw_window_unit}, {at0(0x3A6A),row_drawer_unit}, {at0(0x3A7F),row_drawer_unit},
    {at0(0x3A83),draw_window_at_once_unit}, {at0(0x3A8D),draw_window_at_once_unit},
    {at0(0x3A92),draw_window_at_once_unit},
    {at0(0x3AA2),draw_window_at_once_unit}, {at0(0x3AA7),save_under_unit}, {at0(0x3AAB),save_under_unit},
    {at0(0x3AB0),save_under_unit}, {at0(0x3AB7),save_under_unit}
}};

// -- windows
inline constexpr RoutineEntry battle_name_box_unit=at0(0x2EAC);
inline constexpr RoutineEntry battle_name_box_close_unit=at0(0x2ECD);
inline constexpr RoutineEntry party_window_unit=at0(0x2ED9);
inline constexpr RoutineEntry member_box_unit=at0(0x2F16);
inline constexpr RoutineEntry party_window_redraw_unit=at0(0x2F3C);
inline constexpr RoutineEntry member_box_redraw_unit=at0(0x2F6D);
inline constexpr RoutineEntry member_box_refresh_unit=at0(0x2F93);
inline constexpr RoutineEntry party_window_close_unit=at0(0x30B7);
inline constexpr RoutineEntry battle_commands_unit=at0(0x30C3);
inline constexpr RoutineEntry battle_commands_redraw_unit=at0(0x30D5);
inline constexpr RoutineEntry battle_commands_close_unit=at0(0x30E1);
inline constexpr RoutineEntry enemy_windows_unit=at0(0x30ED);
inline constexpr RoutineEntry enemy_list_unit=at0(0x3105);
inline constexpr RoutineEntry enemy_windows_close_unit=at0(0x319E);
inline constexpr RoutineEntry dialogue_close_unit=at0(0x3464);
inline constexpr RoutineEntry magic_list_unit=at0(0x3478);
inline constexpr RoutineEntry magic_list_close_unit=at0(0x34C9);
inline constexpr RoutineEntry item_choice_unit=at0(0x34D5);
inline constexpr RoutineEntry item_list_open_unit=at0(0x3521);
inline constexpr RoutineEntry item_list_unit=at0(0x352D);
inline constexpr RoutineEntry item_list_close_unit=at0(0x3656);
inline constexpr RoutineEntry member_choice_unit=at0(0x3665);
inline constexpr RoutineEntry battle_member_choice_unit=at0(0x3682);
inline constexpr RoutineEntry member_names_unit=at0(0x369F);
inline constexpr RoutineEntry member_choice_close_unit=at0(0x36BB);
inline constexpr RoutineEntry battle_member_choice_close_unit=at0(0x36CC);
inline constexpr RoutineEntry field_menu_window_unit=at0(0x36DD);
inline constexpr RoutineEntry field_menu_window_redraw_unit=at0(0x36EF);
inline constexpr RoutineEntry field_menu_window_close_unit=at0(0x36FB);
inline constexpr RoutineEntry equipment_window_unit=at0(0x3707);
inline constexpr RoutineEntry equipment_window_close_unit=at0(0x374D);
inline constexpr RoutineEntry item_actions_unit=at0(0x3759);
inline constexpr RoutineEntry item_actions_close_unit=at0(0x376B);
inline constexpr RoutineEntry buy_or_sell_unit=at0(0x3777);
inline constexpr RoutineEntry buy_or_sell_close_unit=at0(0x3797);
inline constexpr RoutineEntry yes_no_window_unit=at0(0x37A3);
inline constexpr RoutineEntry yes_no_window_close_unit=at0(0x37C3);
inline constexpr RoutineEntry status_window_unit=at0(0x37CF);
inline constexpr RoutineEntry status_window_close_unit=at0(0x38C1);
inline constexpr RoutineEntry shop_list_open_unit=at0(0x38CD);
inline constexpr RoutineEntry shop_list_unit=at0(0x38D9);
inline constexpr RoutineEntry shop_list_close_unit=at0(0x3999);
inline constexpr RoutineEntry save_slots_open_unit=at0(0x39A5);
inline constexpr RoutineEntry save_slots_unit=at0(0x39B1);
inline constexpr RoutineEntry save_slots_close_unit=at0(0x39DD);
inline constexpr RoutineEntry money_window_open_unit=at0(0x39E9);
inline constexpr RoutineEntry money_window_unit=at0(0x39F7);
inline constexpr RoutineEntry money_window_close_unit=at0(0x3A12);
inline constexpr RoutineEntry two_choice_unit=at0(0x3A21);
inline constexpr std::array<Segment,125> windows_segments{{
    {at0(0x2EAC),battle_name_box_unit}, {at0(0x2EB8),battle_name_box_unit}, {at0(0x2ECD),battle_name_box_close_unit},
    {at0(0x2ED9),party_window_unit}, {at0(0x2EE5),party_window_unit}, {at0(0x2EF2),party_window_unit},
    {at0(0x2EFF),party_window_unit}, {at0(0x2F0C),party_window_unit}, {at0(0x2F16),member_box_unit},
    {at0(0x2F21),member_box_unit}, {at0(0x2F2A),member_box_unit}, {at0(0x2F33),member_box_unit},
    {at0(0x2F3C),party_window_redraw_unit}, {at0(0x2F49),party_window_redraw_unit},
    {at0(0x2F56),party_window_redraw_unit}, {at0(0x2F63),party_window_redraw_unit},
    {at0(0x2F6D),member_box_redraw_unit}, {at0(0x2F78),member_box_redraw_unit}, {at0(0x2F81),member_box_redraw_unit},
    {at0(0x2F8A),member_box_redraw_unit}, {at0(0x2F93),member_box_refresh_unit},
    {at0(0x2FA0),member_box_refresh_unit}, {at0(0x30B7),party_window_close_unit}, {at0(0x30C3),battle_commands_unit},
    {at0(0x30CF),battle_commands_unit}, {at0(0x30D5),battle_commands_redraw_unit},
    {at0(0x30E1),battle_commands_close_unit}, {at0(0x30ED),enemy_windows_unit}, {at0(0x30F9),enemy_windows_unit},
    {at0(0x3105),enemy_list_unit}, {at0(0x3111),enemy_list_unit}, {at0(0x3119),enemy_list_unit},
    {at0(0x311E),enemy_list_unit}, {at0(0x3127),enemy_list_unit}, {at0(0x3139),enemy_list_unit},
    {at0(0x314B),enemy_list_unit}, {at0(0x319E),enemy_windows_close_unit}, {at0(0x31AF),enemy_windows_close_unit},
    {at0(0x3464),dialogue_close_unit}, {at0(0x3478),magic_list_unit}, {at0(0x3486),magic_list_unit},
    {at0(0x34AA),magic_list_unit}, {at0(0x34C9),magic_list_close_unit}, {at0(0x34D5),item_choice_unit},
    {at0(0x34E4),item_choice_unit}, {at0(0x34EB),item_choice_unit}, {at0(0x350F),item_choice_unit},
    {at0(0x3521),item_list_open_unit}, {at0(0x352D),item_list_unit}, {at0(0x353C),item_list_unit},
    {at0(0x354A),item_list_unit}, {at0(0x3559),item_list_unit}, {at0(0x355E),item_list_unit},
    {at0(0x3656),item_list_close_unit}, {at0(0x3663),item_list_close_unit}, {at0(0x3665),member_choice_unit},
    {at0(0x3679),member_choice_unit}, {at0(0x3682),battle_member_choice_unit},
    {at0(0x3696),battle_member_choice_unit}, {at0(0x369F),member_names_unit}, {at0(0x36B2),member_names_unit},
    {at0(0x36BB),member_choice_close_unit}, {at0(0x36CC),battle_member_choice_close_unit},
    {at0(0x36DD),field_menu_window_unit}, {at0(0x36E9),field_menu_window_unit},
    {at0(0x36EF),field_menu_window_redraw_unit}, {at0(0x36FB),field_menu_window_close_unit},
    {at0(0x3707),equipment_window_unit}, {at0(0x3714),equipment_window_unit}, {at0(0x3720),equipment_window_unit},
    {at0(0x373A),equipment_window_unit}, {at0(0x373F),equipment_window_unit}, {at0(0x374B),equipment_window_unit},
    {at0(0x374D),equipment_window_close_unit}, {at0(0x3759),item_actions_unit}, {at0(0x3765),item_actions_unit},
    {at0(0x376B),item_actions_close_unit}, {at0(0x3777),buy_or_sell_unit}, {at0(0x3783),buy_or_sell_unit},
    {at0(0x3789),buy_or_sell_unit}, {at0(0x3797),buy_or_sell_close_unit}, {at0(0x37A3),yes_no_window_unit},
    {at0(0x37AF),yes_no_window_unit}, {at0(0x37B5),yes_no_window_unit}, {at0(0x37C3),yes_no_window_close_unit},
    {at0(0x37CF),status_window_unit}, {at0(0x37EA),status_window_unit}, {at0(0x37F3),status_window_unit},
    {at0(0x37FC),status_window_unit}, {at0(0x3808),status_window_unit}, {at0(0x3811),status_window_unit},
    {at0(0x381A),status_window_unit}, {at0(0x3823),status_window_unit}, {at0(0x382C),status_window_unit},
    {at0(0x3835),status_window_unit}, {at0(0x383E),status_window_unit}, {at0(0x3847),status_window_unit},
    {at0(0x3850),status_window_unit}, {at0(0x385C),status_window_unit}, {at0(0x38C1),status_window_close_unit},
    {at0(0x38CD),shop_list_open_unit}, {at0(0x38D9),shop_list_unit}, {at0(0x38E5),shop_list_unit},
    {at0(0x390B),shop_list_unit}, {at0(0x3912),shop_list_unit}, {at0(0x3922),shop_list_unit},
    {at0(0x392B),shop_list_unit}, {at0(0x3999),shop_list_close_unit}, {at0(0x39A5),save_slots_open_unit},
    {at0(0x39B6),save_slots_unit}, {at0(0x39C7),save_slots_unit}, {at0(0x39D5),save_slots_unit},
    {at0(0x39DD),save_slots_close_unit}, {at0(0x39E9),money_window_open_unit}, {at0(0x39F6),money_window_open_unit},
    {at0(0x39F7),money_window_unit}, {at0(0x3A07),money_window_unit}, {at0(0x3A10),money_window_unit},
    {at0(0x3A12),money_window_close_unit}, {at0(0x3A1F),money_window_close_unit}, {at0(0x3A21),two_choice_unit},
    {at0(0x3A2D),two_choice_unit}, {at0(0x3A38),two_choice_unit}, {at0(0x3A46),two_choice_unit},
    {at0(0x3A54),two_choice_unit}
}};

// -- select
inline constexpr RoutineEntry fade_frame_handler_unit=at0(0x015F);
inline constexpr RoutineEntry menu_frame_handler_unit=at0(0x01AD);
inline constexpr RoutineEntry text_frame_handler_unit=at0(0x01D1);
inline constexpr RoutineEntry yes_no_unit=at0(0x2D19);
inline constexpr RoutineEntry wait_button_unit=at0(0x2D25);
inline constexpr RoutineEntry wait_short_unit=at0(0x2D33);
inline constexpr RoutineEntry wait_long_unit=at0(0x2D37);
inline constexpr RoutineEntry wait_fixed_unit=at0(0x2D47);
inline constexpr RoutineEntry wait_frames_unit=at0(0x2D49);
inline constexpr RoutineEntry selection_unit=at0(0x2D51);
inline constexpr RoutineEntry item_selection_unit=at0(0x2DA5);
inline constexpr RoutineEntry cursor_blink_unit=at0(0x2E62);
inline constexpr std::array<Segment,25> select_segments{{
    {at0(0x015F),fade_frame_handler_unit}, {at0(0x017A),fade_frame_handler_unit},
    {at0(0x01AD),menu_frame_handler_unit}, {at0(0x01D1),text_frame_handler_unit}, {at0(0x2D19),yes_no_unit},
    {at0(0x2D1D),yes_no_unit}, {at0(0x2D21),yes_no_unit}, {at0(0x2D25),wait_button_unit},
    {at0(0x2D2A),wait_button_unit}, {at0(0x2D33),wait_short_unit}, {at0(0x2D37),wait_long_unit},
    {at0(0x2D3E),wait_long_unit}, {at0(0x2D47),wait_fixed_unit}, {at0(0x2D49),wait_frames_unit},
    {at0(0x2D4E),wait_frames_unit}, {at0(0x2D51),selection_unit}, {at0(0x2D60),selection_unit},
    {at0(0x2D65),selection_unit}, {at0(0x2D94),selection_unit}, {at0(0x2D9D),selection_unit},
    {at0(0x2DA5),item_selection_unit}, {at0(0x2DAF),item_selection_unit}, {at0(0x2E0C),item_selection_unit},
    {at0(0x2E37),item_selection_unit}, {at0(0x2E62),cursor_blink_unit}
}};

// -- field
inline constexpr RoutineEntry field_menu_unit=at0(0x1BE1);
inline constexpr RoutineEntry stats_unit=at0(0x1CA1);
inline constexpr RoutineEntry save_unit=at0(0x1CDF);
inline constexpr RoutineEntry magic_unit=at0(0x1D4D);
inline constexpr RoutineEntry item_unit=at0(0x2168);
inline constexpr RoutineEntry item_exit_unit=at0(0x21F5);
inline constexpr RoutineEntry item_use_unit=at0(0x2201);
inline constexpr RoutineEntry equip_unit=at0(0x26C8);
inline constexpr RoutineEntry drop_unit=at0(0x2752);
inline constexpr RoutineEntry remove_selected_item_unit=at0(0x277C);
inline constexpr RoutineEntry remove_item_unit=at0(0x277F);
inline constexpr RoutineEntry add_item_unit=at0(0x279F);
inline constexpr RoutineEntry find_item_unit=at0(0x282E);
inline constexpr RoutineEntry search_unit=at0(0x2839);
inline constexpr RoutineEntry chest_unit=at0(0x28DB);
inline constexpr RoutineEntry chest_trap_unit=at0(0x2950);
inline constexpr RoutineEntry chest_trap_member_unit=at0(0x2966);
inline constexpr std::array<Segment,63> field_segments{{
    {at0(0x1BE1),field_menu_unit}, {at0(0x1BEE),field_menu_unit}, {at0(0x1C02),field_menu_unit},
    {at0(0x1C29),field_menu_unit}, {at0(0x1C51),field_menu_unit}, {at0(0x1C5E),field_menu_unit},
    {at0(0x1CA4),stats_unit}, {at0(0x1CAB),stats_unit}, {at0(0x1CB7),stats_unit}, {at0(0x1CDF),save_unit},
    {at0(0x1CE8),save_unit}, {at0(0x1CF1),save_unit}, {at0(0x1CFF),save_unit}, {at0(0x1D33),save_unit},
    {at0(0x1D50),magic_unit}, {at0(0x1D58),magic_unit}, {at0(0x1D84),magic_unit}, {at0(0x1D93),magic_unit},
    {at0(0x1DAD),magic_unit}, {at0(0x1DDA),magic_unit}, {at0(0x2168),item_unit}, {at0(0x2178),item_unit},
    {at0(0x21A0),item_unit}, {at0(0x21A7),item_unit}, {at0(0x21CB),item_unit}, {at0(0x21D9),item_unit},
    {at0(0x21E7),item_unit}, {at0(0x2201),item_use_unit}, {at0(0x26C8),equip_unit}, {at0(0x26F1),equip_unit},
    {at0(0x26FB),equip_unit}, {at0(0x272D),equip_unit}, {at0(0x273C),equip_unit}, {at0(0x2750),equip_unit},
    {at0(0x2752),drop_unit}, {at0(0x277C),remove_selected_item_unit}, {at0(0x277F),remove_item_unit},
    {at0(0x279F),add_item_unit}, {at0(0x27C5),add_item_unit}, {at0(0x27D0),add_item_unit},
    {at0(0x27E8),add_item_unit}, {at0(0x2807),add_item_unit}, {at0(0x2814),add_item_unit},
    {at0(0x282E),find_item_unit}, {at0(0x2839),search_unit}, {at0(0x2843),search_unit}, {at0(0x28B7),search_unit},
    {at0(0x28CD),search_unit}, {at0(0x28D2),search_unit}, {at0(0x28DB),chest_unit}, {at0(0x28E4),chest_unit},
    {at0(0x28EB),chest_unit}, {at0(0x2901),chest_unit}, {at0(0x290A),chest_unit}, {at0(0x291D),chest_unit},
    {at0(0x292E),chest_unit}, {at0(0x2936),chest_unit}, {at0(0x2948),chest_unit}, {at0(0x2950),chest_trap_unit},
    {at0(0x295E),chest_trap_unit}, {at0(0x2964),chest_trap_unit}, {at0(0x2969),chest_trap_member_unit},
    {at0(0x2974),chest_trap_member_unit}
}};

// -- places
inline constexpr RoutineEntry hospital_unit=at0(0x298D);
inline constexpr RoutineEntry restore_all_unit=at0(0x2A6D);
inline constexpr RoutineEntry church_unit=at0(0x2A85);
inline constexpr RoutineEntry next_level_report_unit=at0(0x2B58);
inline constexpr RoutineEntry next_level_member_unit=at0(0x2B84);
inline constexpr RoutineEntry shop_unit=at0(0x2BC0);
inline constexpr RoutineEntry second_shop_unit=at0(0x2C8F);
inline constexpr RoutineEntry buy_sell_shop_unit=at0(0x2C98);
inline constexpr RoutineEntry sell_unit=at0(0x2CB1);
inline constexpr std::array<Segment,58> places_segments{{
    {at0(0x298D),hospital_unit}, {at0(0x2996),hospital_unit}, {at0(0x29A6),hospital_unit},
    {at0(0x29B1),hospital_unit}, {at0(0x29D5),hospital_unit}, {at0(0x2A04),hospital_unit},
    {at0(0x2A08),hospital_unit}, {at0(0x2A1D),hospital_unit}, {at0(0x2A37),hospital_unit},
    {at0(0x2A46),hospital_unit}, {at0(0x2A4B),hospital_unit}, {at0(0x2A55),hospital_unit},
    {at0(0x2A64),hospital_unit}, {at0(0x2A6D),restore_all_unit}, {at0(0x2A75),restore_all_unit},
    {at0(0x2A85),church_unit}, {at0(0x2A94),church_unit}, {at0(0x2A98),church_unit}, {at0(0x2AA8),church_unit},
    {at0(0x2AB0),church_unit}, {at0(0x2AD4),church_unit}, {at0(0x2AD8),church_unit}, {at0(0x2AE1),church_unit},
    {at0(0x2AF4),church_unit}, {at0(0x2B0A),church_unit}, {at0(0x2B15),church_unit}, {at0(0x2B1E),church_unit},
    {at0(0x2B2F),church_unit}, {at0(0x2B3A),church_unit}, {at0(0x2B46),church_unit}, {at0(0x2B4C),church_unit},
    {at0(0x2B58),next_level_report_unit}, {at0(0x2B63),next_level_report_unit}, {at0(0x2B6F),next_level_report_unit},
    {at0(0x2B7B),next_level_report_unit}, {at0(0x2B84),next_level_member_unit}, {at0(0x2BC0),shop_unit},
    {at0(0x2BC9),shop_unit}, {at0(0x2BDB),shop_unit}, {at0(0x2BE2),shop_unit}, {at0(0x2BE6),shop_unit},
    {at0(0x2C0F),shop_unit}, {at0(0x2C1D),shop_unit}, {at0(0x2C22),shop_unit}, {at0(0x2C2B),shop_unit},
    {at0(0x2C74),shop_unit}, {at0(0x2C80),shop_unit}, {at0(0x2C8F),second_shop_unit},
    {at0(0x2C98),buy_sell_shop_unit}, {at0(0x2CA1),buy_sell_shop_unit}, {at0(0x2CA6),buy_sell_shop_unit},
    {at0(0x2CB1),sell_unit}, {at0(0x2CBA),sell_unit}, {at0(0x2CC0),sell_unit}, {at0(0x2CFC),sell_unit},
    {at0(0x2D04),sell_unit}, {at0(0x2D0A),sell_unit}, {at0(0x2D13),sell_unit}
}};

// -- fades
inline constexpr RoutineEntry fade_out_short_unit=at1(0x7AFD);
inline constexpr RoutineEntry fade_out_unit=at1(0x7B05);
inline constexpr RoutineEntry fade_in_short_unit=at1(0x7B18);
inline constexpr RoutineEntry fade_in_unit=at1(0x7B20);
inline constexpr RoutineEntry fade_step_unit=at1(0x7B40);
inline constexpr RoutineEntry fade_out_colour_unit=at1(0x7B5F);
inline constexpr RoutineEntry fade_in_colour_unit=at1(0x7B92);
inline constexpr RoutineEntry battle_flash_unit=at1(0x7BAC);
inline constexpr RoutineEntry magic_flash_unit=at1(0x7BC4);
inline constexpr RoutineEntry flash_step_unit=at1(0x7BEE);
inline constexpr RoutineEntry palette_cycle_unit=at1(0x7C85);
inline constexpr RoutineEntry palette_load_unit=at1(0x7CA6);
inline constexpr RoutineEntry palette_sequence_unit=at1(0x7CBB);
inline constexpr RoutineEntry scene_palette_sequence_unit=at1(0x7CE4);
inline constexpr RoutineEntry palette_sequence_step_unit=at1(0x7CF1);
inline constexpr std::array<Segment,23> fades_segments{{
    {at1(0x7AFD),fade_out_short_unit}, {at1(0x7B05),fade_out_unit}, {at1(0x7B10),fade_out_unit},
    {at1(0x7B18),fade_in_short_unit}, {at1(0x7B20),fade_in_unit}, {at1(0x7B38),fade_in_unit},
    {at1(0x7B40),fade_step_unit}, {at1(0x7B5F),fade_out_colour_unit}, {at1(0x7B92),fade_in_colour_unit},
    {at1(0x7BAC),battle_flash_unit}, {at1(0x7BBC),battle_flash_unit}, {at1(0x7BC4),magic_flash_unit},
    {at1(0x7BE6),magic_flash_unit}, {at1(0x7BEE),flash_step_unit}, {at1(0x7C85),palette_cycle_unit},
    {at1(0x7CA1),palette_cycle_unit}, {at1(0x7CA6),palette_load_unit}, {at1(0x7CBB),palette_sequence_unit},
    {at1(0x7CC9),palette_sequence_unit}, {at1(0x7CDE),palette_sequence_unit},
    {at1(0x7CE4),scene_palette_sequence_unit}, {at1(0x7CF1),palette_sequence_step_unit},
    {at1(0x7D11),palette_sequence_step_unit}
}};

inline constexpr std::span<const Segment> planned_segments(Part part) {
    switch (part) {
        case Part::engine: return engine_segments;
        case Part::windows: return windows_segments;
        case Part::select: return select_segments;
        case Part::field: return field_segments;
        case Part::places: return places_segments;
        case Part::fades: return fades_segments;
        default: return {};
    }
}
inline constexpr std::size_t planned_total=323;

// The code each part may register entries in (the static extent of its units;
// shared printer bodies belong to the engine).
inline constexpr std::array<Range,7> engine_ranges{{
    {0,0x2FD8,0x30A6},{0,0x315C,0x319D},{0,0x342C,0x3463},{0,0x356B,0x3638},
    {0,0x3645,0x3655},{0,0x393A,0x3998},{0,0x3A57,0x3AC3}}};
inline constexpr std::array<Range,7> windows_ranges{{
    {0,0x2EAC,0x2FD7},{0,0x30B7,0x315B},{0,0x319E,0x31BA},{0,0x3464,0x356A},
    {0,0x3656,0x3864},{0,0x38C1,0x3939},{0,0x3999,0x3A56}}};
inline constexpr std::array<Range,3> select_ranges{{
    {0,0x015F,0x018E},{0,0x01AD,0x01EE},{0,0x2D19,0x2EAB}}};
inline constexpr std::array<Range,5> field_ranges{{
    {0,0x1BE1,0x1C7B},{0,0x1CA1,0x1DDB},{0,0x2168,0x21FA},{0,0x2201,0x2209},
    {0,0x26C8,0x2979}}};
inline constexpr std::array<Range,1> places_ranges{{
    {0,0x298D,0x2D18}}};
inline constexpr std::array<Range,2> fades_ranges{{
    {1,0x7AFD,0x7C17},{1,0x7C85,0x7D16}}};
inline constexpr std::span<const Range> part_ranges(Part part) {
    switch (part) {
        case Part::engine: return engine_ranges;
        case Part::windows: return windows_ranges;
        case Part::select: return select_ranges;
        case Part::field: return field_ranges;
        case Part::places: return places_ranges;
        case Part::fades: return fades_ranges;
        default: return {};
    }
}

// Units whose first instruction stays native (interrupts off, a bare call or a
// cartridge-RAM switch first), so they have no entry at their own address.
inline constexpr std::array<RoutineEntry,15> native_first_units{{
    number_wide_label_unit,number_unit,long_number_unit,enemy_name_line_unit,item_name_line_unit,money_wide_label_unit,
    money_unit,shop_line_unit,row_drawer_unit,save_under_unit,save_slots_unit,
    stats_unit,magic_unit,item_exit_unit,chest_trap_member_unit}};

// ---- Instructions that stay native ------------------------------------------------
// Every DI and EI, the instruction after each EI (it always falls back with
// ei_delay), the instructions right before a DI or EI that cannot end a
// segment (pushes, pops, stores), the cartridge-RAM switches, the host-observed
// instructions, the save copy stretch, and the two loads before the dialogue
// scroll's last window call (00:3451, 00:3454) and the end of a window row
// (00:3A97, 52 cycles): the frame boundary falls on them on most scrolls (ROM
// run), so a segment there would mostly fall back.
// No segment may start at one, declare one as an exit, or render one.
inline constexpr std::array<RoutineEntry,61> native_instructions{{
    at0(0x1D0E), at0(0x1D0F), at0(0x1D10), at0(0x1D12), at0(0x1D15), at0(0x1D17), at0(0x1D1A), at0(0x1D1D),
    at0(0x1D1F), at0(0x1D21), at0(0x1D24), at0(0x1D25), at0(0x1D26), at0(0x1D28), at0(0x1D2B), at0(0x1D2D),
    at0(0x1D2F), at0(0x1D32), at0(0x2D93), at0(0x2FD8), at0(0x2FE1), at0(0x302F), at0(0x3030), at0(0x3036),
    at0(0x30A0), at0(0x30A1), at0(0x315C), at0(0x319A), at0(0x319B), at0(0x319C), at0(0x319D), at0(0x3451),
    at0(0x3454), at0(0x356B), at0(0x35B3), at0(0x35B4), at0(0x35B5), at0(0x35B6), at0(0x35BC), at0(0x35C4),
    at0(0x3632), at0(0x3633), at0(0x393A), at0(0x39B1), at0(0x39B3), at0(0x39C2), at0(0x39C4), at0(0x3A68),
    at0(0x3A69), at0(0x3A79), at0(0x3A7A), at0(0x3A89), at0(0x3A8C), at0(0x3A97), at0(0x3AA0), at0(0x3AA1), at0(0x3AA6),
    at0(0x3AC0), at0(0x3AC1), at0(0x3AC2), at0(0x3AC3)
}};
// The save copy: cartridge RAM switched in, the 1,024-byte copy, the
// automap's observation point 00:1D2D, cartridge RAM switched out.
inline constexpr Range save_copy_stretch{0,0x1D0E,0x1D32};
// The host's hold of the field menu (src/app/frame_driver.cpp): it redirects
// the frame wait to the menu's first instruction (run by the core), watches
// the cancel point and, in the save, the observation point. 00:0B14 and
// 00:0F48 (the waits) belong to milestone 11 code and stay untouched.
inline constexpr RoutineEntry host_menu_entry=at0(0x1BE1);
inline constexpr RoutineEntry menu_cancel_point=at0(0x2D93);
inline constexpr RoutineEntry save_observation_point=at0(0x1D2D);

// The `ld bc,nn` instructions that give the engine a window's rows (B) and
// columns (C) at its call sites; tests/menu_probe.h reads the sizes from the ROM.
inline constexpr std::array<RoutineEntry,73> window_size_sites{{
    at0(0x2EB2), at0(0x2ED3), at0(0x2EDF), at0(0x2F1B), at0(0x2F36), at0(0x2F72), at0(0x2F8D), at0(0x2F9A),
    at0(0x30BD), at0(0x30C9), at0(0x30DB), at0(0x30E7), at0(0x30F3), at0(0x30FF), at0(0x310B), at0(0x3121),
    at0(0x3133), at0(0x3156), at0(0x31B5), at0(0x31E7), at0(0x3226), at0(0x343F), at0(0x344B), at0(0x3454),
    at0(0x3472), at0(0x3480), at0(0x34C3), at0(0x34CF), at0(0x3527), at0(0x3533), at0(0x3564), at0(0x365D),
    at0(0x3670), at0(0x368D), at0(0x36B5), at0(0x36C6), at0(0x36D7), at0(0x36E3), at0(0x36F5), at0(0x3701),
    at0(0x370E), at0(0x371A), at0(0x3745), at0(0x3753), at0(0x375F), at0(0x3771), at0(0x377D), at0(0x379D),
    at0(0x37A9), at0(0x37C9), at0(0x37E4), at0(0x37ED), at0(0x380B), at0(0x381D), at0(0x382F), at0(0x3841),
    at0(0x3853), at0(0x385F), at0(0x38C7), at0(0x38D3), at0(0x38DF), at0(0x391C), at0(0x399F), at0(0x39AB),
    at0(0x39BC), at0(0x39E3), at0(0x39F0), at0(0x39FE), at0(0x3A0A), at0(0x3A19), at0(0x3A27), at0(0x3A4E),
    at1(0x4377)
}};
}

namespace psmodern::game {
// One registration function per part; register_routines calls all six for an
// identified image. Each lives in its part's file and registers only there.
void register_menu_engine(RoutineRegistry& registry);
void register_menu_windows(RoutineRegistry& registry);
void register_menu_select(RoutineRegistry& registry);
void register_menu_field(RoutineRegistry& registry);
void register_menu_places(RoutineRegistry& registry);
void register_menu_fades(RoutineRegistry& registry);
}
