// SPDX-License-Identifier: GPL-3.0-or-later
// The dungeon tables, read once from the player's ROM image: object records
// (chests, fixed fights, events), exit records, the view offsets and the
// floor layouts. Nothing is stored in the repository; only an identified
// image should be passed in.
#pragma once
#include "game/dungeon.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace psmodern::game {
struct ObjectRecord {
    std::uint8_t floor, cell;
    std::uint16_t flag;        // done-flag address inside the game block
    std::uint8_t type;         // 0 item chest, 1 money chest, 2 fixed fight, 3 event
    bool operator==(const ObjectRecord&) const = default;
};
struct ExitRecord {
    std::uint8_t floor, cell, kind;   // kind below 80h: to the world map
    bool operator==(const ExitRecord&) const = default;
};

class DungeonTables {
public:
    // A 512 KiB image. Throws std::invalid_argument on a smaller image, a table
    // without its terminator inside its bank, a floor of 61 or more, or a done
    // flag outside the game block.
    explicit DungeonTables(std::span<const std::uint8_t> rom);
    // The first record of this floor and cell, as the original scans find it
    // (01:6963 for objects, 01:68BC for exits).
    std::optional<ObjectRecord> object_at(std::uint8_t floor, std::uint8_t cell) const;
    std::optional<ExitRecord> exit_at(std::uint8_t floor, std::uint8_t cell) const;
    // The signed cell offset of the readers' table (01:6BCA, 01:6BE9) for
    // facing & 3 and an index below 16 (std::out_of_range otherwise).
    std::int8_t offset(unsigned facing, unsigned index) const;
    // A floor's 256 cell values decoded from its packed layout in bank 0Fh of
    // the player's ROM (std::out_of_range for a floor of 61 or more).
    std::array<std::uint8_t,dungeon::cells_per_floor> layout(unsigned floor) const;
    // A floor's label relative to its stack (docs/how-it-works/dungeons.md): a
    // stack is the run of floors joined by stairs without the exit bit. The
    // lowest floor of the stack with an exit to the world map is 1, floors
    // above it 2, 3..., floors below it -1, -2... (shown B1, B2...). A stack
    // without such an exit starts at its lowest floor. A PSModern convention,
    // not something the original shows. 0 is returned for a floor of 61 or more.
    int floor_level(unsigned floor) const { return floor<levels_.size() ? levels_[floor] : 0; }
    std::span<const ObjectRecord> objects() const { return objects_; }
    std::span<const ExitRecord> exits() const { return exits_; }
    bool operator==(const DungeonTables&) const = default;
private:
    std::vector<ObjectRecord> objects_;
    std::vector<ExitRecord> exits_;
    std::array<std::int8_t,4*dungeon::offsets_per_facing> offsets_{};
    std::array<std::int8_t,dungeon::floor_count> levels_{};
    std::vector<std::uint8_t> layouts_;   // floor_count x layout_bytes, packed as in the image
};
}
