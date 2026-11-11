// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/options.h"
#include "machine/routines.h"
#include <array>

namespace psmodern::game {
// Segment entries of the encounter checks, as planned in milestone 4 (map
// check unit 01:5F63, shared monster-group choice unit 01:5FD8, dungeon
// game-mode handler unit 00:0F3C). Registered by register_encounters.
inline constexpr RoutineEntry map_check_unit{1,0x5F63};
inline constexpr RoutineEntry map_compare_entry{1,0x5FB0};
inline constexpr RoutineEntry map_after_compare_entry{1,0x5FB1};
inline constexpr RoutineEntry group_choice_unit{1,0x5FD8};
inline constexpr RoutineEntry group_choice_after_draw_entry{1,0x5FEC};
inline constexpr RoutineEntry dungeon_check_unit{0,0x0F3C};
inline constexpr RoutineEntry dungeon_compare_entry{0,0x0F5B};
inline constexpr std::array<RoutineEntry,13> encounter_entries{
    map_check_unit, map_compare_entry, map_after_compare_entry,
    group_choice_unit, group_choice_after_draw_entry,
    dungeon_check_unit, RoutineEntry{0,0x0F43}, RoutineEntry{0,0x0F4B}, dungeon_compare_entry,
    RoutineEntry{0,0x0F5C}, RoutineEntry{0,0x0F62}, RoutineEntry{0,0x0F6D}, RoutineEntry{0,0x0F75}};

void register_encounters(RoutineRegistry& registry, const Options& options);
}
