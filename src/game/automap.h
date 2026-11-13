// SPDX-License-Identifier: GPL-3.0-or-later
// The automap model (milestone 7, part M1): which cells of each floor the
// party has seen or uncovered, and the map view built from them. No SDL and no
// Machine: the host feeds samples read at the dungeon wait (dungeon::sample).
//
// Seen: at every sample, the cells the original standing view (01:6AED with
// frame 0) reads from the party's cell and facing, plus the cell stood on.
// Found: the cells whose disguise is lifted (a fake wall walked through, a
// pitfall stood on). A seen chest shows at once (Maintainer decision,
// 2026-11-11), so chests need no found state. Exploration is keyed by floor ID.
#pragma once
#include "game/dungeon.h"
#include "game/dungeon_tables.h"
#include <array>
#include <bitset>
#include <cstdint>
#include <optional>
#include <span>

namespace psmodern::game {
using DungeonLayout = std::array<std::uint8_t,dungeon::cells_per_floor>;
using CellSet = std::bitset<dungeon::cells_per_floor>;

struct FloorExploration {
    CellSet seen, found;
    bool operator==(const FloorExploration&) const = default;
};
struct Exploration {
    std::array<FloorExploration,dungeon::floor_count> floors{};
    bool empty() const;
    bool operator==(const Exploration&) const = default;
};
// Read with Machine::peek at 00:0F48 in mode $0B.
struct DungeonSample {
    std::uint8_t floor, cell, facing;   // facing already & 3 (only the low two bits are used)
    DungeonLayout layout;               // $CB00-$CBFF as the game has it
};

// The cells the original standing view reads, plus the cell stood on: the
// cell ahead (index 1); if it is walkable, indexes 2-4; if 4 is walkable,
// 5-7; if 7 is walkable, 8-10 (01:6C46-6D10; 01:6E38 when the cell ahead is
// not walkable). The readers' offsets (01:6BCA) add with 8-bit wrap.
CellSet view_cells(const DungeonTables& tables, const DungeonLayout& layout, std::uint8_t cell, std::uint8_t facing);

class ExplorationTracker {
public:
    explicit ExplorationTracker(const DungeonTables& tables);
    // One sample at the dungeon wait: adds the seen cells and the found ones.
    // Ignored when the floor is 61 or more.
    void observe(const DungeonSample& sample);
    void reset();                                // power-on, power cycle, new game
    void replace(const Exploration& saved);      // Continue
    // The party left the dungeon (the host saw a mode other than 0Ah or 0Bh
    // after a pass): the next sample starts a new walk, so leaving and coming
    // back can never pass for a walk through a fake wall.
    // Returns whether a previous sample was forgotten.
    bool leave();
    const Exploration& exploration() const;
private:
    struct Position { std::uint8_t floor, cell, facing; };
    const DungeonTables& tables_;
    Exploration exploration_{};
    std::optional<Position> previous_;   // the last distinct sample, for the fake-wall rule
};

enum class MapKind : std::uint8_t { unknown, floor, wall, stairs_up, stairs_down, door, locked_door,
                                    magic_door, chest, opened_chest, pitfall };
struct MapCell {
    MapKind kind=MapKind::unknown;
    bool exit=false;   // stairs or door leading to the world map
    bool operator==(const MapCell&) const = default;
};
struct MapView {
    std::array<MapCell,dungeon::cells_per_floor> cells;
    std::uint8_t party_cell;
    std::int8_t party_ahead;   // offset(facing, 1): -16 up, +1 right, +16 down, -1 left
    std::int8_t floor_level=0; // DungeonTables::floor_level of the floor: 1, 2... from FLOOR 1 up, -1, -2... below it; 0 is none
    bool operator==(const MapView&) const = default;
};
// The view of the current floor; done flags are read from the game block
// ($C300-$C6FF). A cell neither seen nor found is unknown and its layout byte
// is never read.
MapView build_view(const DungeonTables& tables, const FloorExploration& floor_state, std::uint8_t floor,
                   const DungeonLayout& layout, std::uint8_t cell, std::uint8_t facing,
                   std::span<const std::uint8_t,dungeon::game_block_bytes> game_block);
}
