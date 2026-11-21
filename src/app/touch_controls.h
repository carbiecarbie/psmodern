// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/bindings.h"
#include <map>

namespace psmodern::app {
struct TouchLayout {
    WindowRect game{}, pad{};        // pad: the drawn cross, three cells a side
    PerAction<WindowRect> buttons{}; // drawn buttons; empty for actions absent on this screen
    // Where a touch counts: each drawn control grown toward the screen edge
    // and into the free space around it. Never drawn.
    WindowRect pad_hit{};
    PerAction<WindowRect> hit{};
    bool overlay=false;
};
// Dimensions, cutout insets and finger coordinates are all in output pixels.
TouchLayout touch_layout(int width,int height,WindowBorders insets,bool wide,bool gameplay,bool automap);

class TouchControls {
public:
    void set_layout(const TouchLayout& layout);
    void down(std::int64_t finger,int x,int y);
    void move(std::int64_t finger,int x,int y);
    void up(std::int64_t finger) { fingers_.erase(finger); }
    void clear() { fingers_.clear(); }
    void controller_used() { clear(); visible_=false; }
    bool visible() const { return visible_; }
    PerAction<bool> held() const;
private:
    struct Finger { bool pad; Action button,held; };
    std::map<std::int64_t,Finger> fingers_;
    TouchLayout layout_;
    bool visible_=true;
};

// The 72x72 cross: four cartridge windows joined at a black centre, each with
// the cartridge's menu cursor as its arrow (mirrored and turned; milestone 16
// decision 4). The arrow of `held` is yellow; Action::count lights none.
void draw_touch_cross(const RomFont& font,std::span<std::uint32_t> pixels,Action held);
// A tightly packed tile-aligned button texture. The host scales the texture to
// its hitbox and applies landscape overlay alpha when compositing.
void draw_touch_button(const RomFont& font,std::span<std::uint32_t> pixels,int width,Action action,
                       Language language,unsigned speed,bool pressed);
}
