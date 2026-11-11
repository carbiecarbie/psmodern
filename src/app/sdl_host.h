// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/host.h"
#include "app/options_menu.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <memory>
#include <string>

namespace psmodern::app {
void sdl_check(bool success,const char* operation);
void message(const std::string& text,SDL_Window* window=nullptr);
class Controller {
public:
    ~Controller();
    void refresh();
    Buttons sample() const;
    bool options() const;
    bool fast_forward() const;
    bool owns(SDL_JoystickID instance) const;
private:
    SDL_GameController* controller_=nullptr;
};
FrameInput sample_input(SDL_Window* window,const Controller& controller,const EnterKey& enter);
MenuInput sample_menu_input(SDL_Window* window,const Controller& controller);
// The game window and what draws the 256x192 picture into it.
struct Display {
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window{nullptr,SDL_DestroyWindow};
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer{nullptr,SDL_DestroyRenderer};
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture{nullptr,SDL_DestroyTexture};
};
// A resizable 768x576 window with a vsync renderer when available.
Display open_display(const std::string& title);
// Shows the uploaded texture: nearest-neighbour integer scaling, centred
// with black bars. The upload stays with the caller, where the session tests
// intercept it.
void present(const Display& display);
// Alt+Enter: borderless desktop fullscreen on and off.
void toggle_fullscreen(SDL_Window* window);
// Start screen input: arrows, d-pad and left stick move; Z, Enter without
// Alt, controller A or Start accept; Esc is cancel (quit).
MenuInput sample_start_input(SDL_Window* window,const Controller& controller,const EnterKey& enter);
class Audio {
public:
    Audio();
    ~Audio();
    void removed(SDL_AudioDeviceID device);
    void submit(std::span<const std::int16_t> samples,double now);
    void reset();
    void pause(bool paused);
    void report() const;
private:
    void open(double now);
    void close();
    SDL_AudioDeviceID device_=0;
    int hz_=48000;
    std::unique_ptr<Resampler> resampler_;
    double retry_at_=0;
    bool warned_=false;
    bool paused_=false;
    unsigned underruns_=0, resets_=0;
    std::uint64_t submitted_=0, nonzero_=0;
    double queue_min_=1, queue_max_=0;
};
}
