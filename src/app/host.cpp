// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/host.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace psmodern::app {
Buttons keyboard(const Keys& k) { return {{k[0],k[1],k[2],k[3],k[4],k[5]},k[6],k[7]}; }
Viewport viewport(int w,int h) {
    const int scale=std::max(1,std::min(w/256,h/192));
    return {(w-256*scale)/2,(h-192*scale)/2,256*scale,192*scale,scale};
}
Buttons controller(Buttons b,int x,int y) {
    b.pad.left |= x < -stick_dead_zone; b.pad.right |= x > stick_dead_zone;
    b.pad.up |= y < -stick_dead_zone; b.pad.down |= y > stick_dead_zone;
    return b;
}
FrameInput combine(const Buttons& k,const Buttons& c) {
    FrameInput f;
    f.pads[0]={k.pad.up||c.pad.up,k.pad.down||c.pad.down,
               k.pad.left||c.pad.left,k.pad.right||c.pad.right,
               k.pad.one||c.pad.one,k.pad.two||c.pad.two};
    f.pause=k.pause||c.pause; f.reset=k.reset||c.reset;
    return f;
}
Resampler::Resampler(int hz) {
    if (hz<8000 || hz>192000) throw std::invalid_argument("Unsupported audio sample rate.");
    nominal_=static_cast<double>(Psg::cpu_hz)/Psg::sample_cycles/hz;
    width_=remaining_=nominal_;
}
double Resampler::bounded_adjustment(double a) {
    if (!std::isfinite(a)) throw std::invalid_argument("Invalid audio rate adjustment.");
    return std::clamp(a,0.995,1.005);
}
std::vector<std::int16_t> Resampler::process(std::span<const std::int16_t> input,double a,unsigned speed) {
    if (speed!=1 && speed!=2 && speed!=4) throw std::invalid_argument("Invalid speed.");
    const auto next_width=nominal_*bounded_adjustment(a)*speed;
    if (!started_ && !input.empty()) { if (speed!=1) width_=remaining_=next_width; started_=true; }
    std::vector<std::int16_t> output;
    output.reserve(static_cast<std::size_t>(input.size()/next_width)+1);
    for (auto sample:input) {
        double available=1;
        while (available>0) {
            const auto used=std::min(available,remaining_);
            sum_+=sample*used; available-=used; remaining_-=used;
            if (remaining_<1e-10) {
                output.push_back(static_cast<std::int16_t>(std::clamp(std::lround(sum_/width_),-32768l,32767l)));
                width_=remaining_=next_width; sum_=0;
            }
        }
    }
    return output;
}
double rate_adjustment(double queued) {
    // Queue is measured immediately before adding the current frame.
    return Resampler::bounded_adjustment(1+(queued-(audio_target_seconds-frame_seconds/2))*0.1);
}
bool SpeedShortcut::keyboard_event(bool down,bool focused,bool allowed) {
    if(!focused || !allowed) { keyboard_armed_=keyboard_previous_=false; return false; }
    const bool pressed=down && keyboard_armed_ && !keyboard_previous_;
    if(!down)keyboard_armed_=true;
    keyboard_previous_=down;
    return pressed;
}
bool SpeedShortcut::shoulder_event(bool down,bool focused,bool allowed) {
    if(!focused || !allowed) { shoulder_armed_=shoulder_previous_=false; return false; }
    const bool pressed=down && shoulder_armed_ && !shoulder_previous_;
    if(!down)shoulder_armed_=true;
    shoulder_previous_=down;
    return pressed;
}
bool SpeedShortcut::update(bool focused,bool keyboard,bool shoulder,bool allowed) {
    if (!focused || !allowed) {
        keyboard_armed_=shoulder_armed_=false;
        keyboard_previous_=shoulder_previous_=false;
        return false;
    }
    const bool pressed=(keyboard_armed_ && keyboard && !keyboard_previous_) ||
                       (shoulder_armed_ && shoulder && !shoulder_previous_);
    if (!keyboard) keyboard_armed_=true;
    if (!shoulder) shoulder_armed_=true;
    keyboard_previous_=keyboard; shoulder_previous_=shoulder;
    return pressed;
}
void FrameClock::speed(unsigned value,double now) {
    if (value!=1 && value!=2 && value!=4) throw std::invalid_argument("Invalid speed.");
    if (value!=speed_) { speed_=value; deadline_=now+frame_seconds/speed_; }
}
unsigned FrameClock::due(double now) {
    unsigned count=0;
    while (now>=deadline_ && count<3*speed_) { ++count; deadline_+=frame_seconds/speed_; }
    if (now>=deadline_) {
        discarded_+=now+frame_seconds/speed_-deadline_;
        deadline_=now+frame_seconds/speed_;
    }
    return count;
}
}
