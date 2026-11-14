// SPDX-License-Identifier: GPL-3.0-or-later
// The world map and town screens, read from the player's ROM image for the
// widescreen bands (milestone 10). Nothing is stored in the repository.
//
// An area is named by the three values the map load 00:0B6A keeps in work
// RAM: the screen table pointer, the bank holding the table and its screens,
// and the metatile bank. The table has 9 x 9 screen pointers (row-major, two
// bytes each); the ninth row and column repeat the first, so the world is a
// torus of 8 x 8 screens. Each screen is 16 x 12 metatile ids, run-length
// coded as 01:723D decodes them; a metatile is four name words (top left,
// top right, bottom left, bottom right) at 8000h + 8 x id of the metatile
// bank, as 01:744B draws them (ROM static).
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace psmodern::game {
namespace map {
inline constexpr unsigned screen_columns=8, screen_rows=8;     // the torus
inline constexpr unsigned table_side=9;                         // pointers per table row and column
inline constexpr unsigned cells_x=16, cells_y=12, cells_per_screen=cells_x*cells_y;
inline constexpr unsigned screen_width=256, screen_height=192;
inline constexpr unsigned world_width=screen_columns*screen_width;     // 2048
inline constexpr unsigned world_height=screen_rows*screen_height;      // 1536
inline constexpr unsigned metatiles=256;
// The screen window of work RAM: four slots of 256 bytes, each holding one
// screen as rows of 16 ids, for the 2 x 2 screens from the camera's page.
inline constexpr unsigned window_slot=256, window_bytes=4*window_slot;
}
// The work RAM bytes that name an area (catalogued: C260 screen table
// pointer, C262 its bank, C263 the metatile bank).
struct MapArea {
    std::uint16_t table=0;
    std::uint8_t table_bank=0, metatile_bank=0;
    bool operator==(const MapArea&) const = default;
};
using MapScreen=std::array<std::uint8_t,map::cells_per_screen>;

class MapScreens {
public:
    // Any image; every read is bounds checked. A missing image reads nothing.
    explicit MapScreens(std::shared_ptr<const std::vector<std::uint8_t>> rom);
    // One screen of the 9 x 9 table (column and row below 9), decoded as
    // 01:723D does, or nothing when the table or the screen lies outside its
    // bank in slot 2, a literal run is empty, or the codes give fewer than
    // 192 or more than 256 ids (more than one window slot). count, when
    // given, receives the number of ids the codes gave.
    std::optional<MapScreen> decode(const MapArea& area,unsigned column,unsigned row,unsigned* count=nullptr) const;
    // The four name words of a metatile, or nothing for a bank beyond the image.
    std::optional<std::array<std::uint16_t,4>> metatile(std::uint8_t bank,std::uint8_t id) const;
    // Makes an area current: its 8 x 8 screens and the 256 metatiles are
    // decoded once and kept until another area is selected. False (and
    // nothing current) when any of them cannot be read. Never throws after
    // construction and never allocates.
    bool select(const MapArea& area) noexcept;
    bool ready() const { return ready_; }
    const MapArea& area() const { return area_; }
    // After a successful select. Coordinates wrap around the torus.
    const MapScreen& screen(unsigned column,unsigned row) const {
        return screens_[(row%map::screen_rows)*map::screen_columns+column%map::screen_columns];
    }
    std::uint16_t word(std::uint8_t id,unsigned quarter) const { return words_[id*4u+(quarter&3)]; }
private:
    std::shared_ptr<const std::vector<std::uint8_t>> rom_;
    MapArea area_{};
    bool tried_=false, ready_=false;
    std::array<MapScreen,map::screen_columns*map::screen_rows> screens_{};
    std::array<std::uint16_t,map::metatiles*4> words_{};
};
}
