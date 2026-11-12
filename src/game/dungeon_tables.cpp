// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/dungeon_tables.h"
#include <stdexcept>
#include <string>

namespace psmodern::game {
namespace {
constexpr std::size_t image_bytes=512*1024;

// The records of one table, from its start to the floor byte FFh, which must
// lie inside the table's bank.
template<class Record, class Decode>
std::vector<Record> records(std::span<const std::uint8_t> rom, RoutineEntry table, unsigned size, const char* name, Decode decode) {
    const auto start=dungeon::image_offset(table);
    const auto bank_end=(start/0x4000+1)*0x4000;
    std::vector<Record> out;
    for (auto at=start;;at+=size) {
        if (at>=bank_end) throw std::invalid_argument(std::string("The dungeon ")+name+" table has no end inside its bank.");
        if (rom[at]==dungeon::table_end) return out;
        if (at+size>bank_end) throw std::invalid_argument(std::string("The dungeon ")+name+" table has no end inside its bank.");
        if (rom[at]>=dungeon::floor_count) throw std::invalid_argument(std::string("A dungeon ")+name+" record names a floor above 60.");
        out.push_back(decode(rom.subspan(at,size)));
    }
}
}

DungeonTables::DungeonTables(std::span<const std::uint8_t> rom) {
    if (rom.size()<image_bytes) throw std::invalid_argument("The dungeon tables need a 512 KiB image.");
    objects_=records<ObjectRecord>(rom,dungeon::objects_table,dungeon::object_record_bytes,"object",[](auto r) {
        const auto flag=static_cast<std::uint16_t>(r[2]|(r[3]<<8));
        if (flag<dungeon::game_block || flag>=dungeon::game_block+dungeon::game_block_bytes)
            throw std::invalid_argument("A dungeon object record's done flag lies outside the game block.");
        return ObjectRecord{r[0],r[1],flag,r[4]};
    });
    exits_=records<ExitRecord>(rom,dungeon::exits_table,dungeon::exit_record_bytes,"exit",[](auto r) {
        return ExitRecord{r[0],r[1],r[2]};
    });
    const auto offsets=dungeon::image_offset(dungeon::view_offsets_table);
    for (std::size_t i=0;i<offsets_.size();++i) offsets_[i]=static_cast<std::int8_t>(rom[offsets+i]);
    const auto layouts=dungeon::image_offset(dungeon::layouts_table);
    layouts_.assign(rom.begin()+static_cast<std::ptrdiff_t>(layouts),
                    rom.begin()+static_cast<std::ptrdiff_t>(layouts+dungeon::floor_count*dungeon::layout_bytes));
}

std::optional<ObjectRecord> DungeonTables::object_at(std::uint8_t floor, std::uint8_t cell) const {
    for (const auto& record:objects_) if (record.floor==floor && record.cell==cell) return record;
    return std::nullopt;
}
std::optional<ExitRecord> DungeonTables::exit_at(std::uint8_t floor, std::uint8_t cell) const {
    for (const auto& record:exits_) if (record.floor==floor && record.cell==cell) return record;
    return std::nullopt;
}
std::int8_t DungeonTables::offset(unsigned facing, unsigned index) const {
    if (index>=dungeon::offsets_per_facing) throw std::out_of_range("A view offset index must be below 16.");
    return offsets_[(facing&3)*dungeon::offsets_per_facing+index];
}
std::array<std::uint8_t,dungeon::cells_per_floor> DungeonTables::layout(unsigned floor) const {
    if (floor>=dungeon::floor_count) throw std::out_of_range("A dungeon floor must be below 61.");
    std::array<std::uint8_t,dungeon::cells_per_floor> cells{};
    for (unsigned i=0;i<dungeon::layout_bytes;++i) {
        const auto packed=layouts_[floor*dungeon::layout_bytes+i];
        cells[2*i]=static_cast<std::uint8_t>(packed>>4);
        cells[2*i+1]=static_cast<std::uint8_t>(packed&0x0F);
    }
    return cells;
}
}
