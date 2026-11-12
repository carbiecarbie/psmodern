// SPDX-License-Identifier: GPL-3.0-or-later
// Dungeon names shared by the milestone 7 routines, the dungeon tables and
// the automap: work RAM addresses, where the tables live in the player's ROM,
// record sizes, how a layout cell is decoded, and the instructions the host
// watches. Named constants only; every table byte is read from the player's
// ROM at run time (dungeon_tables.h).
#pragma once
#include "machine/routines.h"
#include <array>
#include <cstdint>

namespace psmodern::game::dungeon {
// ---- Work RAM ---------------------------------------------------------------
inline constexpr std::uint16_t game_mode=0xC202;        // the main dispatcher's mode byte
inline constexpr std::uint8_t dungeon_mode=0x0B;        // its value while walking a dungeon
inline constexpr std::uint8_t dungeon_load_mode=0x0A;   // loading or redrawing a dungeon floor (stairs, scenes, entry)
inline constexpr std::uint16_t facing=0xC30A;           // low two bits: 0 up, 1 right, 2 down, 3 left
inline constexpr std::uint16_t position=0xC30C;         // row x 16 + column
inline constexpr std::uint16_t floor=0xC30D;            // layout ID, 0-60
inline constexpr std::uint16_t light=0xC315;
inline constexpr std::uint16_t saved_mode=0xC316;       // game mode stored with a save
inline constexpr std::uint16_t layout=0xCB00;           // 256 cells, one byte each
inline constexpr std::uint16_t object_flags_first=0xC50A, object_flags_last=0xC6D8; // done flags of the object records
inline constexpr std::uint16_t game_block=0xC300;       // the block a save copies
inline constexpr unsigned game_block_bytes=1024;

// ---- Tables in the player's ROM (bank:address) ------------------------------
inline constexpr RoutineEntry layouts_table{0x0F,0x9F6E};   // 128 bytes per floor, two cells per byte, high nibble first
inline constexpr RoutineEntry objects_table{0x03,0xAF5C};   // 7-byte records, a floor byte FFh ends the table
inline constexpr RoutineEntry exits_table{0x03,0xB473};     // 5-byte records, a floor byte FFh ends the table
inline constexpr RoutineEntry view_offsets_table{0x01,0x6C06}; // 16 signed cell offsets per facing
inline constexpr unsigned floor_count=61;
inline constexpr unsigned layout_bytes=128;
inline constexpr unsigned cells_per_floor=256;
inline constexpr unsigned object_record_bytes=7;   // floor, cell, done-flag address (low, high), type, two data bytes
inline constexpr unsigned exit_record_bytes=5;     // floor, cell, kind, two data bytes
inline constexpr unsigned offsets_per_facing=16;
inline constexpr std::uint8_t table_end=0xFF;
// Offset of a bank:address in the image (a 16 KiB bank mapped at its slot).
constexpr std::size_t image_offset(RoutineEntry location) {
    return static_cast<std::size_t>(location.bank)*0x4000u+(location.address&0x3FFFu);
}

// ---- Cells ------------------------------------------------------------------
// The low three bits give the kind; bit 3 marks an exit on stairs and doors
// (values 10-14) and, on value 8 alone, an object or a pitfall; bit 7 marks an
// opened door and exists only in RAM.
enum class CellKind : std::uint8_t { floor, wall, stairs_up, stairs_down, door, locked_door, magic_door, fake_wall };
constexpr CellKind kind(std::uint8_t value) { return static_cast<CellKind>(value&7); }
constexpr bool exit_bit(std::uint8_t value) { return (value&8)!=0; }
constexpr bool object_cell(std::uint8_t value) { return value==8; }
constexpr bool walkable(std::uint8_t value) { return (value&7)==0; }
inline constexpr std::uint8_t opened_door_bit=0x80;
// Object record types.
inline constexpr std::uint8_t item_chest=0, money_chest=1, fixed_fight=2, event=3;
// Exit kinds below 80h lead to the world map.
inline constexpr std::uint8_t world_map_kinds_end=0x80;

// Offset indexes into a facing's row, as the callers pass them in B.
inline constexpr unsigned ahead=1, behind=0x0B;

// ---- Instructions the host observes (bank 0) --------------------------------
inline constexpr RoutineEntry sample{0,0x0F48};          // the dungeon loop's wait: one sample per pass
inline constexpr RoutineEntry after_save{0,0x1D2D};      // first instruction after a save's used flag is set
inline constexpr RoutineEntry after_load{0,0x06B4};      // first instruction after Continue copied a slot
inline constexpr RoutineEntry after_new_game{0,0x05F4};  // first instruction after a new game cleared the block
// The main loop after every mode pass (00:00AF). A battle, the field menu and
// every step run nested inside the dungeon pass, so the mode is still 0Bh
// here between consecutive walking passes, or 0Ah when the floor is loaded or
// redrawn; any other mode means the party has left the dungeon.
inline constexpr RoutineEntry pass_end{0,0x00AF};

// ---- Cartridge RAM ----------------------------------------------------------
// Save slot k (1-5) holds a copy of the game block at offset k x 400h; its used
// flag is the byte at 200h + k.
inline constexpr unsigned save_slots=5;
constexpr unsigned slot_offset(unsigned slot) { return slot*0x400u; }
constexpr unsigned slot_used_flag(unsigned slot) { return 0x200u+slot; }

// ---- Rewritten entries ------------------------------------------------------
// Every milestone 7 segment entry with the unit it belongs to (design notes,
// section 3). Part B registers the two cell readers; the movement and object
// parts register theirs from these lists, so no later part edits a header.
struct Segment { RoutineEntry entry, unit; };

inline constexpr RoutineEntry cell_type_unit{1,0x6BCA};   // kind of the cell at offset index B
inline constexpr RoutineEntry cell_value_unit{1,0x6BE9};  // value of that cell without bit 7
inline constexpr std::array<Segment,2> cell_segments{{{cell_type_unit,cell_type_unit},{cell_value_unit,cell_value_unit}}};

inline constexpr RoutineEntry handler_unit{1,0x65EE};
inline constexpr RoutineEntry pitfall_unit{1,0x661C};
inline constexpr RoutineEntry forward_step_unit{1,0x6758};
inline constexpr RoutineEntry back_step_unit{1,0x6792};
inline constexpr RoutineEntry turn_left_unit{1,0x67B7};
inline constexpr RoutineEntry turn_right_unit{1,0x67E2};
inline constexpr std::array<Segment,26> movement_segments{{
    {handler_unit,handler_unit},{{1,0x6619},handler_unit},{{1,0x6715},handler_unit},{{1,0x6734},handler_unit},
    {{1,0x6750},handler_unit},{{1,0x6788},handler_unit},{{1,0x678D},handler_unit},{{1,0x67B2},handler_unit},
    {{1,0x67DD},handler_unit},
    {pitfall_unit,pitfall_unit},{{1,0x662C},pitfall_unit},{{1,0x6643},pitfall_unit},{{1,0x6660},pitfall_unit},
    {{1,0x6670},pitfall_unit},{{1,0x6674},pitfall_unit},{{1,0x6684},pitfall_unit},{{1,0x6699},pitfall_unit},
    {{1,0x66B7},pitfall_unit},{{1,0x66CC},pitfall_unit},{{1,0x66DC},pitfall_unit},
    {forward_step_unit,forward_step_unit},{{1,0x675D},forward_step_unit},{{1,0x6775},forward_step_unit},
    {back_step_unit,back_step_unit},{turn_left_unit,turn_left_unit},{turn_right_unit,turn_right_unit}}};

inline constexpr RoutineEntry door_unit{1,0x681B};
inline constexpr RoutineEntry exit_lookup_unit{1,0x68BC};
inline constexpr RoutineEntry object_check_unit{1,0x6963};
inline constexpr std::array<Segment,25> object_segments{{
    {door_unit,door_unit},{{1,0x6835},door_unit},{{1,0x683A},door_unit},
    {exit_lookup_unit,exit_lookup_unit},{{1,0x68E2},exit_lookup_unit},{{1,0x68F7},exit_lookup_unit},
    {{1,0x6900},exit_lookup_unit},{{1,0x690E},exit_lookup_unit},{{1,0x6914},exit_lookup_unit},
    {{1,0x6925},exit_lookup_unit},{{1,0x6930},exit_lookup_unit},{{1,0x6939},exit_lookup_unit},
    {{1,0x694C},exit_lookup_unit},{{1,0x6960},exit_lookup_unit},
    {object_check_unit,object_check_unit},{{1,0x6989},object_check_unit},{{1,0x69D1},object_check_unit},
    {{1,0x69D5},object_check_unit},{{1,0x69D9},object_check_unit},{{1,0x69F1},object_check_unit},
    {{1,0x6A07},object_check_unit},{{1,0x6A12},object_check_unit},{{1,0x6A29},object_check_unit},
    {{1,0x6A2F},object_check_unit},{{1,0x6A3F},object_check_unit}}};
}
