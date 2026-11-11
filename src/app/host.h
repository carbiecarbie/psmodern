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
