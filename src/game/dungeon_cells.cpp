// SPDX-License-Identifier: GPL-3.0-or-later
// The two dungeon cell readers, rewritten as one segment each (milestone 7,
// part B): 01:6BCA, the kind of the cell at a view offset index, and 01:6BE9,
// that cell's value without the opened-door bit. Neither branches, so each has
// one path and its bound is that path's fixed cost, final return included.
// The renderings live in dungeon_kit.h, where the other dungeon segments
// render the same readers inline after a bare call.
#include "game/dungeon_kit.h"
#include "game/routines.h"

namespace psmodern::game {
void register_dungeon_cells(RoutineRegistry& registry) {
    // The pushed HL is the only stack use: two scratch bytes below SP.
    kit::add_segment(registry,dungeon::cell_type_unit,
        kit::segment(dungeon::cell_type_unit,{kit::returned_at(kit::cell_type_return)},{.scratch_stack=2}),
        [](const Machine&) -> std::uint64_t { return kit::cell_type_cycles; },
        [](auto& z) { kit::cell_type(z); return kit::returns(kit::cell_type_return,kit::return_cycles); });
    kit::add_segment(registry,dungeon::cell_value_unit,
        kit::segment(dungeon::cell_value_unit,{kit::returned_at(kit::cell_value_return)}),
        [](const Machine&) -> std::uint64_t { return kit::cell_value_cycles; },
        [](auto& z) { kit::cell_value(z); return kit::returns(kit::cell_value_return,kit::return_cycles); });
}
}
