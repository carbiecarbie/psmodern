// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 10: the widescreen view of the world map, the towns and the
// space flight. A read-only presentation path beside the VDP picture: after
// each frame the frame driver copies the VDP state and a few work RAM bytes
// (the state line 0 of the next picture is drawn with) and pairs that copy
// with the next picture. Over a map it holds a camera and draws the 44
// columns on each side and the blanked left 8 of the centre from the map in
// the player's ROM, with the cartridge's tiles and palette; in space it draws
// them from the name table wrapped. The machine is never written.
#pragma once
#include "app/wide_picture.h"
#include "game/map_screens.h"
#include "hw/memory.h"
#include "hw/vdp.h"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace psmodern { class Machine; }

namespace psmodern::app {
// The VDP state one picture is drawn with.
struct VdpState {
    std::array<std::uint8_t,0x4000> vram{};
    std::array<std::uint8_t,32> cram{};
    std::array<std::uint8_t,11> registers{};
};

// An own background-only mode-4 decoder, kept apart from Vdp so the verified
// VDP stays untouched; tests prove it bit-equal to Vdp for pictures whose
// state does not change while they are drawn and that have no sprites.
namespace mode4 {
// The CRAM indices (0-31) of one row (0-7) of a background tile, left to
// right, for a name word: flips, palette select and the pattern address
// masks of registers 3 and 4 as Vdp applies them.
std::array<std::uint8_t,8> tile_row(const VdpState& state,std::uint16_t name,unsigned row) noexcept;
// The whole 256 x 192 background picture: scrolls and their locks, the
// name table base and row mask of register 2, the left column blank, the
// display enable and the backdrop.
void decode_background(const VdpState& state,Vdp::Picture& out) noexcept;
// The name table drawn past both screen edges as the VDP would draw wider
// columns (the 32 name columns wrap): wide x shows screen column x - 44,
// with the scrolls and their locks of decode_background. Fills wide columns
// [x0, x1) of every line. Its x 52-299 equals decode_background's x 8-255.
void render_wrapped(const VdpState& state,int x0,int x1,WidePicture& out) noexcept;
}

// What the driver copies after each frame (about 17 KiB).
struct WideSnapshot {
    VdpState vdp;
    std::uint8_t mode=0;                  // game mode (C202)
    std::uint8_t h_shadow=0,v_shadow=0;   // scroll shadows (C300, C304)
    std::uint8_t camera_x=0,page_x=0;     // map camera X: pixel and page (C301, C302)
    std::uint8_t camera_y=0,page_y=0;     // map camera Y: pixel and page (C305, C306)
    game::MapArea area;                   // C260, C262, C263
    std::array<std::uint8_t,game::map::window_bytes> window{};   // the screen window (CC00)
};
void copy_snapshot(const Machine& machine,WideSnapshot& out) noexcept;

// What the drawn columns show: the map (world map, towns, the ship's take-off
// and descent) or, in space flight, the name table wrapped horizontally.
enum class WideSource { none, map, space };

// The world pixel shown at x 0, line 0 of the 256 original columns, and the
// scroll registers of the picture it was last moved with.
struct HeldCamera {
    unsigned x=0,y=0;
    std::uint8_t r8=0,r9=0;
    bool operator==(const HeldCamera&) const = default;
};

class WideView {
public:
    explicit WideView(std::shared_ptr<const Memory::Rom> rom);
    // Forgets the copies and the anchor (power cycle, option switched on).
    void reset() noexcept;
    // A language swap: the map reader reads the new image; the anchor is dropped.
    void replace_rom(std::shared_ptr<const Memory::Rom> rom);
    // After each run_frame: the previous copy is paired with the picture just
    // completed, the camera follows it, and the next copy is taken.
    void capture(const Machine& machine) noexcept;
    // The same with a prepared copy (tests).
    void push(const WideSnapshot& next) noexcept;
    // The widened picture for the VDP picture just completed, or nullptr
    // (pillarbox). Rendered on first request after a frame; valid until the
    // next capture, push, reset or replace_rom.
    const WidePicture* picture(const Vdp::Picture& centre) const noexcept;
    bool widened() const { return widened_; }
    WideSource source() const { return source_; }
    const std::optional<HeldCamera>& camera() const { return camera_; }
    // The copy paired with the picture just completed, if any.
    const WideSnapshot* shown() const { return has_shown_ ? shown_.get() : nullptr; }
    // The screen window of the shown copy overrides the ROM screens it holds
    // (map and road frames only).
    bool window_used() const { return window_used_; }
    // Consistent map or road frames whose work RAM camera differed from the
    // held camera, which was then corrected (since construction).
    std::uint64_t corrections() const { return corrections_; }
    const game::MapScreens& screens() const { return screens_; }
    // Draws wide columns [x0, x1) of every line from the map, for the shown
    // copy and the held camera (widened() must be true). Tests also draw the
    // centre with it.
    void render(int x0,int x1,WidePicture& out) const noexcept;
    // Decision 6 of the plan: the game modes whose frames may widen, and the
    // two (map and road) where the camera may be anchored.
    static bool family_mode(std::uint8_t mode) noexcept;
    // The space flight scene: load 00:0A5C (4) and flight 00:086F (5).
    static bool ship_mode(std::uint8_t mode) noexcept { return mode==0x04 || mode==0x05; }
    static bool anchor_mode(std::uint8_t mode) noexcept { return mode==0x09 || mode==0x0F; }
    // Display on and register 0 = 26h (left column blank, mode 4).
    static bool map_display(const VdpState& state) noexcept { return state.registers[0]==0x26 && (state.registers[1]&0x40); }
    // Work RAM is the displayed camera: both scroll registers equal their
    // shadows and the horizontal one is minus the camera's low byte.
    static bool consistent(const WideSnapshot& s) noexcept;
    // The camera a ship frame shows over a map, from work RAM: X from the
    // camera (the scroll register is minus its low byte), Y from the camera
    // plus the signed distance the vertical scroll has run ahead of its shadow.
    static HeldCamera ship_camera(const WideSnapshot& s) noexcept;
private:
    void update() noexcept;
    void ship(const WideSnapshot& s) noexcept;
    // Every fully visible name-table cell right of x 8 holds the map's word
    // for that camera.
    bool names_match(const WideSnapshot& s,const HeldCamera& camera) const noexcept;
    game::MapScreens screens_;
    std::unique_ptr<WideSnapshot> shown_,next_;
    bool has_shown_=false,has_next_=false;
    std::optional<HeldCamera> camera_;
    std::optional<std::array<std::uint8_t,2>> previous_page_;
    bool widened_=false,window_used_=false;
    // A ship scene has shown a map or space since it began: only then may a
    // frame with register 8 at 0 show space (the departure fade before the
    // take-off pillarboxes whatever its name table holds).
    bool scene_started_=false;
    WideSource source_=WideSource::none;
    std::uint64_t corrections_=0;
    mutable std::unique_ptr<WidePicture> picture_;
    mutable bool rendered_=false;
};
}
