// SPDX-License-Identifier: GPL-3.0-or-later
// The map window drawn into the 256x192 overlay over the picture captured
// when the hold began, as the Options window is (milestone 7, part M2; layout
// in the design notes, section 4): the ROM-font window, the title, a 16x16
// grid of 8x8 cells and a legend of ten rows in ROM-font text.
#pragma once
#include "app/options_menu.h"
#include "game/automap.h"
#include <array>
#include <cstdint>
#include <span>

namespace psmodern::app {
namespace automap_layout {
inline constexpr int window_x=8, window_y=8, window_width=240, window_height=176;
inline constexpr int title_x=24, title_y=16;
inline constexpr int grid_x=24, grid_y=32, cell_size=8;
inline constexpr int legend_icon_x=168, legend_label_x=180, legend_y=32, legend_step=14;
}
// Colours as packed ARGB, Master System palette values (from the approved
// mockup): unknown black, floor 000055, wall 00AAAA.
namespace automap_colors {
inline constexpr std::uint32_t black=0xff000000, white=0xffffffff, floor=0xff000055, wall=0xff00aaaa,
    yellow=0xffffff00, orange=0xffffaa00, brown=0xffaa5500, grey=0xffaaaaaa, green=0xff55ff55, magic=0xffaa00aa;
}
enum class MapIcon : std::uint8_t { party, chest, opened_chest, stairs_up, stairs_down, exit, door, pitfall };
// One 8x8 drawing: a colour per pixel, 0 transparent.
using IconPixels=std::array<std::array<std::uint32_t,8>,8>;
// The icon as drawn, the party pointing up; a door in its plain colours.
IconPixels automap_icon(MapIcon icon);
// The cell's background colour (door kinds on the wall colour, unknown black).
std::uint32_t automap_background(game::MapKind kind);
void render_automap(std::span<std::uint32_t> pixels, const RomFont& font, const game::MapView& view);

// The map screen's own input: arms once every button is released (as the
// Options window does), then a fresh map, toggle (Esc or the controller's Y)
// or cancel press closes it; nothing else does anything.
class MapScreen {
public:
    void open() { visible_=true; armed_=false; previous_={}; }
    void close() { visible_=false; }
    bool visible() const { return visible_; }
    MenuAction update(const MenuInput& input);
private:
    MenuInput previous_;
    bool visible_=false, armed_=false;
};
}
