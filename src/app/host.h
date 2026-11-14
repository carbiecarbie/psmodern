// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/machine.h"
#include "app/wide_picture.h"
#include <optional>
#include <span>

namespace psmodern::app {
// Packed SDL_PIXELFORMAT_ARGB8888, independent of the host byte order.
constexpr std::uint32_t rgb(std::uint8_t cram) {
    return 0xff000000u | ((cram & 3u)*85u << 16) |
           (((cram >> 2)&3u)*85u << 8) | ((cram >> 4)&3u)*85u;
}
// The original picture (Vdp::Picture) and the canvas the host shows it on
// (milestone 10): 344 x 192 with the wide canvas, the original 256 x 192
// otherwise. The 256-column centre sits at canvas_centre_x of the canvas.
inline constexpr int picture_width=256, picture_height=192;
constexpr int canvas_width(bool wide) { return wide ? wide_width : picture_width; }
constexpr int canvas_centre_x(bool wide) { return wide ? wide_centre_x : 0; }
struct Viewport { int x,y,width,height,scale; bool operator==(const Viewport&) const = default; };
// A canvas_width x 192 canvas in a width x height output (both in output
// pixels): nearest-neighbour integer scale, at least 1, centred with black
// bars.
Viewport viewport(int width,int height,int canvas_width);
// The 256 x 192 centre in packed colours, before any overlay: the wide
// picture's x 44-299 when one is given (its x 0-7 drawn from the map), the
// VDP picture otherwise.
void centre_pixels(std::span<std::uint32_t> centre,const Vdp::Picture& picture,const WidePicture* wide);
// The side bands (x 0-43 and 300-343) of a 344 x 192 canvas: the wide
// picture's when one is given, black (a pillarbox) otherwise. The centre
// columns are left alone.
void band_pixels(std::span<std::uint32_t> canvas,const WidePicture* wide);
// Window geometry in window units (SDL's window coordinates; equal to output
// pixels unless the display scales, HiDPI).
struct WindowRect { int x=0,y=0,width=0,height=0; bool operator==(const WindowRect&) const = default; };
struct WindowBorders { int top=0,left=0,bottom=0,right=0; };
struct CanvasWindow { WindowRect rect; int scale=1; bool operator==(const CanvasWindow&) const = default; };
// The normal window that shows a canvas_width x 192 canvas at `scale`,
// measured in output pixels and converted to window units by the window's
// output/window ratio (rounded up, so the output is never a pixel short of
// the scale). The scale is lowered until the window and its borders fit the
// usable display bounds (1 at least, whether or not it fits); without bounds
// it is kept. The new window keeps the old one's centre, moved inside the
// usable bounds when it would cross them.
CanvasWindow canvas_window(const WindowRect& window,int output_width,int output_height,int canvas_width,int scale,
                           const std::optional<WindowRect>& usable,const WindowBorders& borders={});
struct Buttons {
    Pad pad{};
    bool pause=false, reset=false;
};
using Keys=std::array<bool,8>; // Up, Down, Left, Right, Z, X, Enter, Backspace.
Buttons keyboard(const Keys& keys);
constexpr int stick_dead_zone=8000; // SDL's signed 16-bit axis range; about 24.4%.
Buttons controller(Buttons digital,int x,int y);
FrameInput combine(const Buttons& keyboard,const Buttons& controller);

class EnterKey {
public:
    bool event(bool down,bool alt,bool repeat) {
        if (!down) {consumed_=false;return false;}
        if (!repeat && alt) {consumed_=true;return true;}
        return false;
    }
    bool pause(bool down) const {return down && !consumed_;}
private:
    bool consumed_=false;
};

class Resampler {
public:
    explicit Resampler(int device_hz);
    // Input periods are integrated, including fractional endpoints. State
    // persists across frames; rate changes begin at the next output period.
    std::vector<std::int16_t> process(std::span<const std::int16_t> input,double adjustment=1,unsigned speed=1);
    static double bounded_adjustment(double adjustment);
private:
    double nominal_, width_, remaining_, sum_=0;
    bool started_=false;
};
double rate_adjustment(double queued_seconds);
inline constexpr double frame_seconds=59736.0/3579545.0;
inline constexpr double audio_target_seconds=0.075;

// A release must be observed in the focused context before a source arms.
class SpeedShortcut {
public:
    bool update(bool focused,bool keyboard,bool shoulder,bool allowed);
    bool keyboard_event(bool down,bool focused,bool allowed);
    bool shoulder_event(bool down,bool focused,bool allowed);
    void controller_changed() { shoulder_armed_=false; shoulder_previous_=false; }
    void focus_lost() { keyboard_armed_=shoulder_armed_=keyboard_previous_=shoulder_previous_=false; }
private:
    bool keyboard_armed_=false,shoulder_armed_=false;
    bool keyboard_previous_=false,shoulder_previous_=false;
};

// All time is supplied by the caller, so tests need no wall clock or SDL.
class FrameClock {
public:
    explicit FrameClock(double now):deadline_(now) {}
    unsigned due(double now);
    void speed(unsigned value,double now);
    unsigned speed() const { return speed_; }
    double deadline() const { return deadline_; }
    double discarded_seconds() const { return discarded_; }
private:
    double deadline_, discarded_=0;
    unsigned speed_=1;
};
}
