// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/bindings.h"
#include "app/host.h"
#include "app/options_menu.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace psmodern::app {
void sdl_check(bool success,const char* operation);
void message(const std::string& text,SDL_Window* window=nullptr);
class Controller {
public:
    ~Controller();
    void refresh();
    bool owns(SDL_JoystickID instance) const;
    // The active bindings travel with the controller, which every sampler
    // already receives. Defaults until set.
    void set_bindings(const Bindings& bindings) { bindings_=bindings; }
    const Bindings& bindings() const { return bindings_; }
    // The window's Alt+Enter state travels the same way, so that
    // sample_menu_input sees a Return that Alt+Enter consumed. The owner feeds
    // it the Return events and passes it to the other samplers.
    EnterKey& enter() { return enter_; }
    const EnterKey& enter() const { return enter_; }
    // Held pad inputs (the triggers as latched below), the left stick and
    // whether a controller is open; nothing held when none is attached. The
    // keyboard part of the snapshot is left alone.
    void snapshot(InputSnapshot& into) const;
    bool held(PadInput input) const;
    // One pad input's change carried by this event, from the owned controller
    // only: a button down or up, or a trigger press or release. Each trigger
    // has one latch, shared with held(): pressed above trigger_threshold,
    // released only below trigger_release_threshold, so a trigger reports one
    // press however it trembles. Call once per event. Opening a controller
    // seeds the latch from its current axes, so a trigger already held when
    // it opens (start-up, hot-plug, the game after the start screen) is held
    // but reports no press until it is released and pressed again.
    struct PadChange { PadInput input; bool down; };
    std::optional<PadChange> translate(const SDL_Event& event);
    bool connected() const;
private:
    SDL_GameController* controller_=nullptr;
    Bindings bindings_=Bindings::defaults();
    EnterKey enter_;
    std::array<bool,2> trigger_down_{};
};
// The key's non-repeat press (true) or release (false) in this window, if
// this event is one.
std::optional<bool> key_change(const SDL_Event& event,SDL_Window* window,Key key);
// Raw snapshot for the Controls screen's fixed navigation and capture: every
// scancode while the window has keyboard focus, Return cleared while an Alt
// key is held or Enter is consumed by Alt+Enter, then the controller.
InputSnapshot sample_controls_input(SDL_Window* window,const Controller& controller,const EnterKey& enter);
// Console input through the controller's bindings (frame_input). Return,
// whatever action it is bound to, counts as held only while no Alt key is
// held and Alt+Enter has not consumed it (0.2.0's rule for Pause).
FrameInput sample_input(SDL_Window* window,const Controller& controller,const EnterKey& enter);
// Host menu input through the bindings (menu_input); by default arrows /
// d-pad, Z / A accept, X / B cancel, Esc / Y toggle (Options), M / X map.
// Return counts as in sample_input, through controller.enter().
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
// Start screen input (start_input), fixed whatever the bindings: arrows,
// d-pad and left stick move; Z, Enter without Alt, controller A or Start
// accept; Esc is cancel (quit). Return is filtered as in sample_input.
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
