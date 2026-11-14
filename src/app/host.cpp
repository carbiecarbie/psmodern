// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/host.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace psmodern::app {
Buttons keyboard(const Keys& k) { return {{k[0],k[1],k[2],k[3],k[4],k[5]},k[6],k[7]}; }
Viewport viewport(int w,int h,int canvas) {
    if (canvas!=picture_width && canvas!=wide_width) throw std::invalid_argument("Unsupported canvas width.");
    const int scale=std::max(1,std::min(w/canvas,h/picture_height));
    return {(w-canvas*scale)/2,(h-picture_height*scale)/2,canvas*scale,picture_height*scale,scale};
}
void centre_pixels(std::span<std::uint32_t> centre,const Vdp::Picture& picture,const WidePicture* wide) {
    if (centre.size()!=std::size_t(picture_width*picture_height)) throw std::invalid_argument("Expected a 256 by 192 centre.");
    if (!wide) { std::transform(picture.begin(),picture.end(),centre.begin(),rgb); return; }
    for (int y=0;y<picture_height;++y) {
        const auto* row=wide->data()+y*wide_width+wide_centre_x;
        std::transform(row,row+picture_width,centre.begin()+y*picture_width,rgb);
    }
}
void band_pixels(std::span<std::uint32_t> canvas,const WidePicture* wide) {
    if (canvas.size()!=std::size_t(wide_width*wide_height)) throw std::invalid_argument("Expected a 344 by 192 canvas.");
    constexpr int right=wide_centre_x+picture_width;
    for (int y=0;y<wide_height;++y) for (int x:{0,right}) {
        const int width=x ? wide_width-right : wide_centre_x;
        auto* out=canvas.data()+y*wide_width+x;
        if (wide) std::transform(wide->data()+y*wide_width+x,wide->data()+y*wide_width+x+width,out,rgb);
        else std::fill(out,out+width,0xff000000u);
    }
}
CanvasWindow canvas_window(const WindowRect& window,int ow,int oh,int canvas,int scale,
                           const std::optional<WindowRect>& usable,const WindowBorders& borders) {
    if (canvas!=picture_width && canvas!=wide_width) throw std::invalid_argument("Unsupported canvas width.");
    // Output pixels to window units, rounded up; 1:1 when either size is unknown.
    auto units=[](long long pixels,int window_size,int output_size) {
        if (window_size<=0 || output_size<=0) return int(pixels);
        return int((pixels*window_size+output_size-1)/output_size);
    };
    CanvasWindow fitted;
    for (fitted.scale=std::max(1,scale);;--fitted.scale) {
        fitted.rect.width=units(1LL*canvas*fitted.scale,window.width,ow);
        fitted.rect.height=units(1LL*picture_height*fitted.scale,window.height,oh);
        if (fitted.scale==1 || !usable ||
            (fitted.rect.width+borders.left+borders.right<=usable->width &&
             fitted.rect.height+borders.top+borders.bottom<=usable->height)) break;
    }
    fitted.rect.x=window.x+(window.width-fitted.rect.width)/2;
    fitted.rect.y=window.y+(window.height-fitted.rect.height)/2;
    if (usable) {
        auto inside=[](int position,int low,int high) { return high<low ? low : std::clamp(position,low,high); };
        fitted.rect.x=inside(fitted.rect.x,usable->x+borders.left,usable->x+usable->width-borders.right-fitted.rect.width);
        fitted.rect.y=inside(fitted.rect.y,usable->y+borders.top,usable->y+usable->height-borders.bottom-fitted.rect.height);
    }
    return fitted;
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
