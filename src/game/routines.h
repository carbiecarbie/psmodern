// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/dungeon.h"
#include "game/encounters.h"
#include "game/experience.h"
#include "game/money.h"
#include "game/options.h"
#include "machine/routines.h"
#include <string_view>

namespace psmodern::game {
inline constexpr RoutineEntry controller_poll_entry{0,0x0339};
inline constexpr RoutineEntry random_entry{0,0x05B1};

// Only identified game images get replacements. Authored runner fixtures keep
// an empty registry. The caller must have verified this digest against its ROM.
// Options that are not allowed values throw std::invalid_argument (see validate).
bool register_routines(RoutineRegistry& registry, std::string_view rom_sha1, const Options& options = {});

// Each game system registers its own segments (random.cpp, encounters.cpp,
// experience.cpp); register_routines calls them for an identified image.
void register_random(RoutineRegistry& registry);
// The milestone 7 dungeon routines (entries in game/dungeon.h): the cell
// readers (dungeon_cells.cpp), the handler with its pitfall, steps and turns
// (dungeon_movement.cpp), and the door, exit lookup and object check
// (dungeon_objects.cpp).
void register_dungeon_cells(RoutineRegistry& registry);
void register_dungeon_movement(RoutineRegistry& registry);
void register_dungeon_objects(RoutineRegistry& registry);
}
