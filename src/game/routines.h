// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/dungeon.h"
#include "game/encounters.h"
#include "game/experience.h"
#include "game/map.h"
#include "game/menu.h"
#include "game/money.h"
#include "game/options.h"
#include "game/text.h"
#include "machine/routines.h"
#include <string_view>

namespace psmodern::game {
inline constexpr RoutineEntry controller_poll_entry{0,0x0339};
inline constexpr RoutineEntry random_entry{0,0x05B1};
inline constexpr std::array<RoutineEntry,3> dispatch_entries{{{0,0x00E6},{0,0x0131},{1,0x4769}}};
void register_dispatch(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,4> block_transfer_entries{{{0,0x0CB1},{0,0x1D1D},{1,0x7926},{1,0x6E68}}};
void register_block_transfers(RoutineRegistry& registry);
void register_block_transfer(RoutineRegistry& registry,RoutineEntry entry);
void register_block_transfer(RoutineRegistry& registry,RoutineEntry entry,bool native_frame_tail);
inline constexpr std::array<RoutineEntry,7> save_menu_entries{{
    {0,0x1D0E},{0,0x1D15},{0,0x1D1F},{0,0x1D24},{0,0x1D2B},{0,0x1D2D},{0,0x1D32}
}};
void register_save_menu(RoutineRegistry& registry);
void register_observed_menu_cancel(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,4> window_row_entries{{{0,0x3451},{0,0x3454},{0,0x3A97},{0,0x3A9C}}};
void register_window_rows(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,3> dungeon_layout_entries{{{1,0x6D56},{1,0x6D6E},{1,0x6D7F}}};
void register_dungeon_layout(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,10> dungeon_tile_entries{{
    {1,0x6B62},{1,0x6B6A},{1,0x6B6E},{1,0x6B70},{1,0x6B74},{1,0x6B76},{1,0x6B7D},
    {1,0x6B81},{1,0x6B85},{1,0x6B8B}
}};
inline constexpr std::array<RoutineEntry,8> dungeon_pattern_entries{{
    {1,0x6B8E},{1,0x6B90},{1,0x6B96},{1,0x6B98},{1,0x6BAB},{1,0x6BB1},
    {1,0x6BB5},{1,0x6BC7}
}};
void register_dungeon_streams(RoutineRegistry& registry);
inline constexpr std::array<std::uint16_t,3> pitfall_video_copies{0x6625,0x665A,0x66B1};
void register_pitfall_video(RoutineRegistry& registry);
inline constexpr std::array<std::uint16_t,11> map_display_entries{
    0x0B6A,0x0B6D,0x0B6E,0x0B71,0x0BB4,0x0BB5,0x0BB8,0x0C8E,0x0C91,0x0C92,0x0C93
};
inline constexpr std::array<std::uint16_t,7> ice_display_entries{
    0x75CC,0x75CD,0x75D0,0x75D2,0x75D4,0x75DA,0x75E3
};
void register_map_display(RoutineRegistry& registry);
inline constexpr std::array<std::uint16_t,4> word_product_entries{0x044C,0x047C,0x04A8,0x04D4};
inline constexpr std::array<std::uint16_t,4> division_entries{0x04FE,0x052D,0x0559,0x0585};
void register_arithmetic(RoutineRegistry& registry);

// Only identified game images get replacements. Authored runner fixtures keep
// an empty registry. The caller must have verified this digest against its ROM.
// Options that are not allowed values throw std::invalid_argument (see validate).
bool register_routines(RoutineRegistry& registry, std::string_view rom_sha1, const Options& options = {});

// Each game system registers its own segments (random.cpp, encounters.cpp,
// experience.cpp); register_routines calls them for an identified image.
void register_random(RoutineRegistry& registry);
void register_sound(RoutineRegistry& registry);
// The milestone 7 dungeon routines (entries in game/dungeon.h): the cell
// readers (dungeon_cells.cpp), the handler with its pitfall, steps and turns
// (dungeon_movement.cpp), and the door, exit lookup and object check
// (dungeon_objects.cpp).
void register_dungeon_cells(RoutineRegistry& registry);
void register_dungeon_movement(RoutineRegistry& registry);
void register_dungeon_objects(RoutineRegistry& registry);
// The milestone 11 map routines register through the six functions declared
// in game/map.h (map_video.cpp, map_scroll.cpp, map_objects.cpp,
// map_terrain.cpp, map_entrances.cpp, map_modes.cpp).
// The milestone 12 menu routines register through the six functions declared
// in game/menu.h (menu_engine.cpp, menu_windows.cpp, menu_select.cpp,
// menu_field.cpp, menu_places.cpp, menu_fades.cpp).
}
