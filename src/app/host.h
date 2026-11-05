// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/machine.h"
#include <span>

namespace psmodern::app {
// Packed SDL_PIXELFORMAT_ARGB8888, independent of the host byte order.
constexpr std::uint32_t rgb(std::uint8_t cram) {
    return 0xff000000u | ((cram & 3u)*85u << 16) |
           (((cram >> 2)&3u)*85u << 8) | ((cram >> 4)&3u)*85u;
}
struct Viewport { int x,y,width,height,scale; bool operator==(const Viewport&) const = default; };
Viewport viewport(int width,int height);
struct Buttons {
    Pad pad{};
    bool pause=false, reset=false;
};
using Keys=std::array<bool,8>; // Up, Down, Left, Right, Z, X, Enter, Backspace.
Buttons keyboard(const Keys& keys);
constexpr int stick_dead_zone=8000; // SDL's signed 16-bit axis range; about 24.4%.
Buttons controller(Buttons digital,int x,int y);
FrameInput combine(const Buttons& keyboard,const Buttons& controller);

class Resampler {
public:
    explicit Resampler(int device_hz);
    // Input periods are integrated, including fractional endpoints. State
    // persists across frames; rate changes begin at the next output period.
    std::vector<std::int16_t> process(std::span<const std::int16_t> input,double adjustment=1);
    static double bounded_adjustment(double adjustment);
private:
    double nominal_, width_, remaining_, sum_=0;
};
double rate_adjustment(double queued_seconds);
inline constexpr double frame_seconds=59736.0/3579545.0;
inline constexpr double audio_target_seconds=0.075;

// All time is supplied by the caller, so tests need no wall clock or SDL.
class FrameClock {
public:
    explicit FrameClock(double now):deadline_(now) {}
    unsigned due(double now);
    double deadline() const { return deadline_; }
    double discarded_seconds() const { return discarded_; }
private:
    double deadline_, discarded_=0;
};
}
