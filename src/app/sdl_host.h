// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/host.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <memory>

namespace psmodern::app {
void sdl_check(bool success,const char* operation);
void message(const std::string& text,SDL_Window* window=nullptr);
class Controller {
public:
    ~Controller();
    void refresh();
    Buttons sample() const;
private:
    SDL_GameController* controller_=nullptr;
};
FrameInput sample_input(SDL_Window* window,const Controller& controller);
class Audio {
public:
    Audio();
    ~Audio();
    void removed(SDL_AudioDeviceID device);
    void submit(std::span<const std::int16_t> samples,double now);
    void reset();
    void report() const;
private:
    void open(double now);
    void close();
    SDL_AudioDeviceID device_=0;
    int hz_=48000;
    std::unique_ptr<Resampler> resampler_;
    double retry_at_=0;
    bool warned_=false;
    unsigned underruns_=0, resets_=0;
    std::uint64_t submitted_=0, nonzero_=0;
    double queue_min_=1, queue_max_=0;
};
}
