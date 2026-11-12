// SPDX-License-Identifier: GPL-3.0-or-later
// The automap file <save>.automap beside the .sav (milestone 7, part M1): one
// exploration record per game-block digest (SHA-256 of $C300-$C6FF, 64
// lowercase hex digits).
//
//   psmodern-automap 1
//   record <digest> <n>                     n floor lines follow, decimal 0-61
//   floor <id> <seen> <found>               id two hex digits, 00-3c, ascending
//
// seen and found are 32 bytes each, two lowercase hex digits per byte, byte 0
// first; byte k holds cells 8k-8k+7 with cell 8k in bit 0. Only floors with a
// bit are written; records are in ascending digest order; every line ends in
// one LF. The text is canonical: the reader accepts exactly what the writer
// produces, and refuses anything else whole (never partially applied).
//
// Limits. A record line has at most 75 bytes and a floor line exactly 139, so
// a record holds at most 75 + 61 x 139 = 8,554 bytes. Pruning keeps only the
// digests of used slots in the cartridge image on disk and in the image being
// written: at most 5 + 5 = 10 records, 19 + 10 x 8,554 = 85,559 bytes. The
// reader accepts at most 10 records and 98,304 bytes; the writer checks its
// own output against the reader before returning it.
#pragma once
#include "game/automap.h"
#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace psmodern::app {
inline constexpr std::size_t automap_max_records=10;
inline constexpr std::size_t automap_max_bytes=98304;
inline constexpr std::string_view automap_header="psmodern-automap 1\n";
inline constexpr std::size_t automap_record_line_max=75, automap_floor_line_bytes=139;
inline constexpr std::size_t automap_record_max=automap_record_line_max+game::dungeon::floor_count*automap_floor_line_bytes;
inline constexpr std::size_t automap_file_max=automap_header.size()+automap_max_records*automap_record_max;
static_assert(automap_file_max==85559 && automap_file_max<=automap_max_bytes,
              "the largest file the writer can produce is one the reader accepts");

class AutomapImage {
public:
    // The record of this digest, or nothing (an empty map).
    const game::Exploration* find(std::string_view digest) const;
    // Adds or replaces a record. Throws std::invalid_argument unless the
    // digest is 64 lowercase hex digits.
    void put(std::string digest, const game::Exploration& exploration);
    // Keeps only the records whose digest is in `keep` (the used slot blocks of
    // the cartridge image on disk and of the one being written). Throws
    // std::invalid_argument, changing nothing, when `keep` holds more than
    // automap_max_records digests, so a pruned image can always be written.
    void prune(const std::set<std::string>& keep);
    std::size_t size() const { return records_.size(); }
    bool operator==(const AutomapImage&) const = default;
private:
    friend std::string write_automap(const AutomapImage& image);
    std::map<std::string,game::Exploration,std::less<>> records_;
};
// Throws std::runtime_error when any part of the text is unusable (unknown
// header or version, malformed or non-canonical line, duplicate digest, floor
// above 60, more than 10 records, more than 98,304 bytes); nothing is applied
// partially. The caller keeps the file and shows an empty map.
AutomapImage parse_automap(std::string_view text);
// Throws std::length_error when the image holds more than 10 records (prune
// first), and std::logic_error if its own output would not read back equal.
std::string write_automap(const AutomapImage& image);

// The record key of a game block ($C300-$C6FF): its SHA-256, lowercase hex.
std::string automap_digest(std::span<const std::uint8_t,game::dungeon::game_block_bytes> game_block);
// The digests of the used save slots of a cartridge RAM image (slot k's block
// at k x 400h, used when the byte at 200h + k is nonzero, as 00:063E tests).
// Throws std::invalid_argument for an image smaller than the last slot's end.
std::set<std::string> used_slot_digests(std::span<const std::uint8_t> cartridge_ram);
}
