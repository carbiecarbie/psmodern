// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/bindings.h"
#include <map>
#include <optional>
#include <set>

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

// After the last controller input, how long a touch cannot bring the hidden
// controls back (issue 6: a stray touch woke them while playing with a pad).
constexpr std::uint32_t touch_wake_quiet_ms=2000;
// Whether new touch controls start shown: as the previous ones ended (the start
// screen into the game, one game into the next), else hidden when a controller
// is already connected.
inline bool touch_start_visible(std::optional<bool> previous,bool controller_connected) {
    return previous ? *previous : !controller_connected;
}

class TouchControls {
public:
    explicit TouchControls(bool visible=true):visible_(visible) {}
    void set_layout(const TouchLayout& layout);
    // `ms` is the touch's time on the same clock as controller_input's.
    void down(std::int64_t finger,int x,int y,std::uint32_t ms);
    void move(std::int64_t finger,int x,int y);
    void up(std::int64_t finger) { fingers_.erase(finger); }
    void clear() { fingers_.clear(); }
    // One controller button or axis (`source`, any key unique to it) at time
    // `ms`: `active` is a button down or an axis beyond the dead zone, else a
    // button up. Active input hides the controls. Hidden controls wake only
    // while nothing is active and touch_wake_quiet_ms after the last input.
    void controller_input(std::int64_t source,bool active,std::uint32_t ms);
    // An axis back inside the dead zone: no longer active, but no input either
    // (a resting stick trembles there without being used).
    void controller_settled(std::int64_t source) { active_.erase(source); }
    // Forget what is held (a controller removed, the app in the background):
    // a lost release must not block waking for ever.
    void controller_reset() { active_.clear(); }
    bool visible() const { return visible_; }
    PerAction<bool> held() const;
private:
    struct Finger { bool pad; Action button,held; };
    std::map<std::int64_t,Finger> fingers_;
    std::set<std::int64_t> active_;
    std::optional<std::uint32_t> last_input_;
    TouchLayout layout_;
    bool visible_;
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
