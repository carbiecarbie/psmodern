// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/automap.h"

namespace psmodern::game {
namespace {
std::uint8_t at(const DungeonTables& tables, std::uint8_t cell, std::uint8_t facing, unsigned index) {
    return static_cast<std::uint8_t>(cell+tables.offset(facing,index));
}
// A chest the game announces: the first record of the cell is an item or
// money chest (01:6963 stops at the first match; a dead record is never on a
// value-8 cell, so the caller's value test excludes it).
bool chest_record(const std::optional<ObjectRecord>& record) {
    return record && (record->type==dungeon::item_chest || record->type==dungeon::money_chest);
}
}

bool Exploration::empty() const {
    for (const auto& floor:floors) if (floor.seen.any() || floor.found.any()) return false;
    return true;
}

CellSet view_cells(const DungeonTables& tables, const DungeonLayout& layout, std::uint8_t cell, std::uint8_t facing) {
    CellSet cells;
    cells.set(cell);
    auto add=[&](unsigned first, unsigned last) {
        for (unsigned k=first;k<=last;++k) cells.set(at(tables,cell,facing,k));
    };
    auto open=[&](unsigned index) { return dungeon::walkable(layout[at(tables,cell,facing,index)]); };
    // 01:6AED reads the cell ahead to choose the frame; a blocked cell ahead
    // draws the close wall and its overlay only (01:6E38).
    add(1,1);
    if (!open(1)) return cells;
    add(2,4);            // 01:6C67-6C82: two ahead, then the sides of the cell ahead
    if (!open(4)) return cells;
    add(5,7);            // 01:6C85-6CBC
    if (!open(7)) return cells;
    add(8,10);           // 01:6CBF-6D10
    return cells;
}

ExplorationTracker::ExplorationTracker(const DungeonTables& tables) : tables_(tables) {}

void ExplorationTracker::observe(const DungeonSample& sample) {
    if (sample.floor>=dungeon::floor_count) return;
    const auto facing=static_cast<std::uint8_t>(sample.facing&3);
    const auto cell=sample.cell;
    auto& floor=exploration_.floors[sample.floor];
    floor.seen|=view_cells(tables_,sample.layout,cell,facing);

    // Fake wall: two consecutive distinct samples (no departure from the
    // dungeon between them) on one floor with one facing, two cells apart
    // along it, with a fake wall between. The middle cell never reaches the
    // wait (the two forward steps of 01:6755).
    const auto ahead=tables_.offset(facing,dungeon::ahead);
    if (previous_ && previous_->floor==sample.floor && previous_->facing==facing &&
        cell==static_cast<std::uint8_t>(previous_->cell+2*ahead)) {
        const auto middle=static_cast<std::uint8_t>(previous_->cell+ahead);
        if (dungeon::kind(sample.layout[middle])==dungeon::CellKind::fake_wall) floor.found.set(middle);
    }
    // Pitfall: standing on a value-8 cell without a record (01:65EE starts
    // the fall in the next handler call).
    if (dungeon::object_cell(sample.layout[cell]) && !tables_.object_at(sample.floor,cell)) floor.found.set(cell);
    // Chests need no discovery: a seen chest cell shows as a chest
    // (Maintainer decision, 2026-11-11). Fixed fights and events are never marked.

    // A repeated sample (the party waiting) never replaces the previous one.
    if (!previous_ || previous_->floor!=sample.floor || previous_->cell!=cell || previous_->facing!=facing)
        previous_=Position{sample.floor,cell,facing};
}
void ExplorationTracker::reset() { exploration_={}; previous_.reset(); }
void ExplorationTracker::replace(const Exploration& saved) { exploration_=saved; previous_.reset(); }
bool ExplorationTracker::leave() { const bool had=previous_.has_value(); previous_.reset(); return had; }
const Exploration& ExplorationTracker::exploration() const { return exploration_; }

MapView build_view(const DungeonTables& tables, const FloorExploration& floor_state, std::uint8_t floor,
                   const DungeonLayout& layout, std::uint8_t cell, std::uint8_t facing,
                   std::span<const std::uint8_t,dungeon::game_block_bytes> game_block) {
    MapView view{};
    view.party_cell=cell;
    view.floor_level=static_cast<std::int8_t>(tables.floor_level(floor));
    view.party_ahead=tables.offset(facing,dungeon::ahead);
    for (unsigned c=0;c<dungeon::cells_per_floor;++c) {
        const bool seen=floor_state.seen.test(c), found=floor_state.found.test(c);
        if (!seen && !found) continue;   // unknown; its layout byte is not read
        const auto id=static_cast<std::uint8_t>(c);
        const auto value=static_cast<std::uint8_t>(layout[c]&~dungeon::opened_door_bit);
        auto& out=view.cells[c];
        if (dungeon::object_cell(value)) {
            const auto record=tables.object_at(floor,id);
            if (chest_record(record)) {   // any shown cell: seen (or found, in a stored exploration)
                const auto flag=static_cast<std::size_t>(record->flag-dungeon::game_block);
                out.kind=flag<game_block.size() && game_block[flag]==0xFF ? MapKind::opened_chest : MapKind::chest;
            } else if (found && !record) out.kind=MapKind::pitfall;
            else out.kind=MapKind::floor;
            continue;
        }
        switch (dungeon::kind(value)) {
        case dungeon::CellKind::floor: out.kind=MapKind::floor; break;
        case dungeon::CellKind::wall: out.kind=MapKind::wall; break;
        case dungeon::CellKind::stairs_up: out.kind=MapKind::stairs_up; break;
        case dungeon::CellKind::stairs_down: out.kind=MapKind::stairs_down; break;
        case dungeon::CellKind::door: out.kind=MapKind::door; break;
        case dungeon::CellKind::locked_door: out.kind=MapKind::locked_door; break;
        case dungeon::CellKind::magic_door: out.kind=MapKind::magic_door; break;
        case dungeon::CellKind::fake_wall: out.kind=found ? MapKind::floor : MapKind::wall; break;
        }
        // The exit lookup (01:68BC) leaves for the world map when the cell has
        // no record (01:68E9) or its first record's kind is below 80h
        // (01:68F0-68F4); kinds of 80h or more start a scene, an interaction or
        // an event instead and show as plain stairs or doors.
        const auto kind=dungeon::kind(value);
        if (seen && dungeon::exit_bit(value) && kind>=dungeon::CellKind::stairs_up && kind<=dungeon::CellKind::magic_door) {
            const auto record=tables.exit_at(floor,id);
            out.exit=!record || record->kind<dungeon::world_map_kinds_end;
        }
    }
    return view;
}
}
