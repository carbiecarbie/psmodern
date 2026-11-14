// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace psmodern::app {
void sdl_check(bool ok,const char* what) {
    if (!ok) throw std::runtime_error(std::string(what)+": "+SDL_GetError());
}
void message(const std::string& text,SDL_Window* window) {
    std::cerr<<text<<'\n';
    if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"PSModern",text.c_str(),window)!=0)
        std::cerr<<"Cannot show error dialog: "<<SDL_GetError()<<'\n';
}
// The pure bindings model (bindings.h) names SDL's values without SDL.
static_assert(key_slots==SDL_NUM_SCANCODES);
static_assert(int(PadInput::a)==SDL_CONTROLLER_BUTTON_A && int(PadInput::b)==SDL_CONTROLLER_BUTTON_B &&
    int(PadInput::x)==SDL_CONTROLLER_BUTTON_X && int(PadInput::y)==SDL_CONTROLLER_BUTTON_Y &&
    int(PadInput::back)==SDL_CONTROLLER_BUTTON_BACK && int(PadInput::guide)==SDL_CONTROLLER_BUTTON_GUIDE &&
    int(PadInput::start)==SDL_CONTROLLER_BUTTON_START && int(PadInput::left_stick)==SDL_CONTROLLER_BUTTON_LEFTSTICK &&
    int(PadInput::right_stick)==SDL_CONTROLLER_BUTTON_RIGHTSTICK && int(PadInput::left_shoulder)==SDL_CONTROLLER_BUTTON_LEFTSHOULDER &&
    int(PadInput::right_shoulder)==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER && int(PadInput::dpad_up)==SDL_CONTROLLER_BUTTON_DPAD_UP &&
    int(PadInput::dpad_down)==SDL_CONTROLLER_BUTTON_DPAD_DOWN && int(PadInput::dpad_left)==SDL_CONTROLLER_BUTTON_DPAD_LEFT &&
    int(PadInput::dpad_right)==SDL_CONTROLLER_BUTTON_DPAD_RIGHT && int(PadInput::misc1)==SDL_CONTROLLER_BUTTON_MISC1 &&
    int(PadInput::paddle1)==SDL_CONTROLLER_BUTTON_PADDLE1 && int(PadInput::paddle2)==SDL_CONTROLLER_BUTTON_PADDLE2 &&
    int(PadInput::paddle3)==SDL_CONTROLLER_BUTTON_PADDLE3 && int(PadInput::paddle4)==SDL_CONTROLLER_BUTTON_PADDLE4 &&
    int(PadInput::touchpad)==SDL_CONTROLLER_BUTTON_TOUCHPAD && pad_button_count==SDL_CONTROLLER_BUTTON_MAX);
namespace {
constexpr SDL_Scancode sdl_key_table[]={
    SDL_SCANCODE_A,SDL_SCANCODE_B,SDL_SCANCODE_C,SDL_SCANCODE_D,SDL_SCANCODE_E,SDL_SCANCODE_F,SDL_SCANCODE_G,SDL_SCANCODE_H,
    SDL_SCANCODE_I,SDL_SCANCODE_J,SDL_SCANCODE_K,SDL_SCANCODE_L,SDL_SCANCODE_M,SDL_SCANCODE_N,SDL_SCANCODE_O,SDL_SCANCODE_P,
    SDL_SCANCODE_Q,SDL_SCANCODE_R,SDL_SCANCODE_S,SDL_SCANCODE_T,SDL_SCANCODE_U,SDL_SCANCODE_V,SDL_SCANCODE_W,SDL_SCANCODE_X,
    SDL_SCANCODE_Y,SDL_SCANCODE_Z,SDL_SCANCODE_1,SDL_SCANCODE_2,SDL_SCANCODE_3,SDL_SCANCODE_4,SDL_SCANCODE_5,SDL_SCANCODE_6,
    SDL_SCANCODE_7,SDL_SCANCODE_8,SDL_SCANCODE_9,SDL_SCANCODE_0,SDL_SCANCODE_RETURN,SDL_SCANCODE_ESCAPE,SDL_SCANCODE_BACKSPACE,
    SDL_SCANCODE_TAB,SDL_SCANCODE_SPACE,SDL_SCANCODE_MINUS,SDL_SCANCODE_EQUALS,SDL_SCANCODE_LEFTBRACKET,SDL_SCANCODE_RIGHTBRACKET,
    SDL_SCANCODE_BACKSLASH,SDL_SCANCODE_SEMICOLON,SDL_SCANCODE_APOSTROPHE,SDL_SCANCODE_GRAVE,SDL_SCANCODE_COMMA,SDL_SCANCODE_PERIOD,
    SDL_SCANCODE_SLASH,SDL_SCANCODE_CAPSLOCK,SDL_SCANCODE_F1,SDL_SCANCODE_F2,SDL_SCANCODE_F3,SDL_SCANCODE_F4,SDL_SCANCODE_F5,
    SDL_SCANCODE_F6,SDL_SCANCODE_F7,SDL_SCANCODE_F8,SDL_SCANCODE_F9,SDL_SCANCODE_F10,SDL_SCANCODE_F11,SDL_SCANCODE_F12,
    SDL_SCANCODE_INSERT,SDL_SCANCODE_HOME,SDL_SCANCODE_PAGEUP,SDL_SCANCODE_DELETE,SDL_SCANCODE_END,SDL_SCANCODE_PAGEDOWN,
    SDL_SCANCODE_RIGHT,SDL_SCANCODE_LEFT,SDL_SCANCODE_DOWN,SDL_SCANCODE_UP,SDL_SCANCODE_KP_DIVIDE,SDL_SCANCODE_KP_MULTIPLY,
    SDL_SCANCODE_KP_MINUS,SDL_SCANCODE_KP_PLUS,SDL_SCANCODE_KP_ENTER,SDL_SCANCODE_KP_1,SDL_SCANCODE_KP_2,SDL_SCANCODE_KP_3,
    SDL_SCANCODE_KP_4,SDL_SCANCODE_KP_5,SDL_SCANCODE_KP_6,SDL_SCANCODE_KP_7,SDL_SCANCODE_KP_8,SDL_SCANCODE_KP_9,SDL_SCANCODE_KP_0,
    SDL_SCANCODE_KP_PERIOD,SDL_SCANCODE_NONUSBACKSLASH,SDL_SCANCODE_INTERNATIONAL1,SDL_SCANCODE_LCTRL,SDL_SCANCODE_LSHIFT,
    SDL_SCANCODE_RCTRL,SDL_SCANCODE_RSHIFT};
constexpr bool key_table_matches_sdl() {
    if (std::size(sdl_key_table)!=key_table_size) return false;
    for (std::size_t i=0;i<key_table_size;++i) if (key_table[i].key!=sdl_key_table[i]) return false;
    return true;
}
static_assert(key_table_matches_sdl(),"every key-table entry is its SDL scancode");
constexpr SDL_GameControllerAxis trigger_axis[2]={SDL_CONTROLLER_AXIS_TRIGGERLEFT,SDL_CONTROLLER_AXIS_TRIGGERRIGHT};
}
Controller::~Controller() { if (controller_) SDL_GameControllerClose(controller_); }
void Controller::refresh() {
    if (controller_ && !SDL_GameControllerGetAttached(controller_)) {
        SDL_GameControllerClose(controller_); controller_=nullptr; trigger_down_={};
    }
    if (!controller_) {
        for (int i=0;i<SDL_NumJoysticks();++i) {
            if (SDL_IsGameController(i)) {
                controller_=SDL_GameControllerOpen(i); trigger_down_={};
                if (controller_) {
                    // SDL 2 rests the triggers of a freshly opened controller
                    // until its driver reports again: read the device once so
                    // the seed sees a trigger already held.
                    SDL_GameControllerUpdate();
                    for (std::size_t t=0;t<2;++t) trigger_down_[t]=SDL_GameControllerGetAxis(controller_,trigger_axis[t])>trigger_threshold;
                    break;
                }
                std::cerr<<"Cannot open controller: "<<SDL_GetError()<<'\n';
            }
        }
    }
}
bool Controller::owns(SDL_JoystickID instance) const {
    return controller_ && SDL_GameControllerGetAttached(controller_) &&
        SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller_))==instance;
}
bool Controller::connected() const { return controller_ && SDL_GameControllerGetAttached(controller_); }
bool Controller::held(PadInput input) const {
    if (!connected() || input>=PadInput::count) return false;
    if (input<PadInput::left_trigger) return SDL_GameControllerGetButton(controller_,SDL_GameControllerButton(input))!=0;
    return trigger_down_[std::size_t(input)-pad_button_count];
}
void Controller::snapshot(InputSnapshot& into) const {
    into.pad.reset(); into.stick_x=into.stick_y=0; into.controller=connected();
    if (!into.controller) return;
    for (std::size_t i=0;i<pad_input_count;++i) into.pad[i]=held(PadInput(i));
    into.stick_x=SDL_GameControllerGetAxis(controller_,SDL_CONTROLLER_AXIS_LEFTX);
    into.stick_y=SDL_GameControllerGetAxis(controller_,SDL_CONTROLLER_AXIS_LEFTY);
}
std::optional<Controller::PadChange> Controller::translate(const SDL_Event& event) {
    if ((event.type==SDL_CONTROLLERBUTTONDOWN || event.type==SDL_CONTROLLERBUTTONUP) && owns(event.cbutton.which) &&
        event.cbutton.button<pad_button_count)
        return PadChange{PadInput(event.cbutton.button),event.type==SDL_CONTROLLERBUTTONDOWN};
    if (event.type==SDL_CONTROLLERAXISMOTION && owns(event.caxis.which)) {
        for (std::size_t i=0;i<2;++i) {
            if (event.caxis.axis!=trigger_axis[i]) continue;
            const bool down=trigger_down_[i] ? event.caxis.value>=trigger_release_threshold : event.caxis.value>trigger_threshold;
            if (down==trigger_down_[i]) return std::nullopt;
            trigger_down_[i]=down;
            return PadChange{PadInput(pad_button_count+i),down};
        }
    }
    return std::nullopt;
}
std::optional<bool> key_change(const SDL_Event& event,SDL_Window* window,Key key) {
    if ((event.type!=SDL_KEYDOWN && event.type!=SDL_KEYUP) || event.key.repeat || event.key.keysym.scancode!=key ||
        event.key.windowID!=SDL_GetWindowID(window)) return std::nullopt;
    return event.type==SDL_KEYDOWN;
}
namespace {
// One keyboard read, only while the window has keyboard focus: every key,
// then the Return rule (0.2.0's rule for Pause, now for whatever action
// Return is bound to): never while an Alt key is held and, given the
// EnterKey, never while Alt+Enter consumes it. Then the controller.
InputSnapshot held_input(SDL_Window* window,const Controller& controller,const EnterKey* enter) {
    InputSnapshot input;
    input.focused=SDL_GetKeyboardFocus()==window;
    if (input.focused) {
        // SDL's keyboard state always holds SDL_NUM_SCANCODES entries.
        const auto* keys=SDL_GetKeyboardState(nullptr);
        for (std::size_t i=0;i<key_slots;++i) input.keys[i]=keys[i]!=0;
        if ((enter && !enter->pause(input.keys[SDL_SCANCODE_RETURN])) || input.keys[SDL_SCANCODE_LALT] || input.keys[SDL_SCANCODE_RALT])
            input.keys[SDL_SCANCODE_RETURN]=false;
    }
    controller.snapshot(input);
    return input;
}
}
InputSnapshot sample_controls_input(SDL_Window* window,const Controller& controller,const EnterKey& enter) {
    return held_input(window,controller,&enter);
}
MenuInput sample_menu_input(SDL_Window* window,const Controller& controller) {
    return menu_input(controller.bindings(),held_input(window,controller,&controller.enter()));
}
MenuInput sample_start_input(SDL_Window* window,const Controller& controller,const EnterKey& enter) {
    return start_input(held_input(window,controller,&enter));
}
Display open_display(const std::string& title) {
    Display display;
    display.window.reset(SDL_CreateWindow(title.c_str(),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,768,576,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI));
    sdl_check(bool(display.window),"Creating window");
    SDL_SetWindowMinimumSize(display.window.get(),256,192);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
    auto& renderer=display.renderer;
    renderer.reset(SDL_CreateRenderer(display.window.get(),-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC));
    if (!renderer) renderer.reset(SDL_CreateRenderer(display.window.get(),-1,SDL_RENDERER_ACCELERATED));
    if (!renderer) renderer.reset(SDL_CreateRenderer(display.window.get(),-1,SDL_RENDERER_SOFTWARE));
    sdl_check(bool(renderer),"Creating renderer");
    SDL_RendererInfo renderer_info{};
    sdl_check(SDL_GetRendererInfo(renderer.get(),&renderer_info)==0,"Reading renderer information");
    SDL_DisplayMode mode{};
    const int display_index=SDL_GetWindowDisplayIndex(display.window.get());
    const bool have_display=display_index>=0 && SDL_GetCurrentDisplayMode(display_index,&mode)==0;
    std::cerr<<"Video: "<<renderer_info.name<<", vsync="
             <<bool(renderer_info.flags&SDL_RENDERER_PRESENTVSYNC)
             <<", display_hz="<<(have_display ? mode.refresh_rate : 0)<<'\n';
    display.texture.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,wide_width,wide_height));
    sdl_check(bool(display.texture),"Creating picture texture");
    sdl_check(SDL_SetTextureScaleMode(display.texture.get(),SDL_ScaleModeNearest)==0,"Setting nearest-neighbour scaling");
    return display;
}
SDL_Rect centre_rect(const Display& display) {
    return {canvas_centre_x(display.wide_canvas),0,picture_width,picture_height};
}
void upload_bands(Display& display,const WidePicture* wide) {
    if (!display.wide_canvas) return;
    display.bands.resize(wide_width*wide_height);
    band_pixels(display.bands,wide);
    constexpr int right=wide_centre_x+picture_width;
    for (const SDL_Rect band:{SDL_Rect{0,0,wide_centre_x,wide_height},SDL_Rect{right,0,wide_width-right,wide_height}})
        sdl_check(SDL_UpdateTexture(display.texture.get(),&band,display.bands.data()+band.x,wide_width*sizeof(std::uint32_t))==0,
                  "Uploading the side bands");
}
void apply_canvas(Display& display,bool wide) {
    if (display.wide_canvas==wide) return;
    auto* window=display.window.get();
    int ow,oh; sdl_check(SDL_GetRendererOutputSize(display.renderer.get(),&ow,&oh)==0,"Reading output size");
    const int scale=viewport(ow,oh,canvas_width(display.wide_canvas)).scale;
    display.wide_canvas=wide;
    SDL_SetWindowMinimumSize(window,canvas_width(wide),picture_height);
    if (SDL_GetWindowFlags(window)&(SDL_WINDOW_FULLSCREEN|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_MINIMIZED)) return;
    WindowRect current;
    SDL_GetWindowPosition(window,&current.x,&current.y);
    SDL_GetWindowSize(window,&current.width,&current.height);
    std::optional<WindowRect> usable;
    SDL_Rect bounds{};
    const int index=SDL_GetWindowDisplayIndex(window);
    if (index>=0 && SDL_GetDisplayUsableBounds(index,&bounds)==0) usable=WindowRect{bounds.x,bounds.y,bounds.w,bounds.h};
    WindowBorders borders;   // unknown borders (an error) count as none
    if (SDL_GetWindowBordersSize(window,&borders.top,&borders.left,&borders.bottom,&borders.right)!=0) borders={};
    const auto fitted=canvas_window(current,ow,oh,canvas_width(wide),scale,usable,borders);
    SDL_SetWindowSize(window,fitted.rect.width,fitted.rect.height);
    SDL_SetWindowPosition(window,fitted.rect.x,fitted.rect.y);
}
void present(const Display& display) {
    auto* renderer=display.renderer.get();
    int w,h; sdl_check(SDL_GetRendererOutputSize(renderer,&w,&h)==0,"Reading output size");
    const int canvas=canvas_width(display.wide_canvas);
    const auto v=viewport(w,h,canvas); const SDL_Rect source{0,0,canvas,picture_height},rect{v.x,v.y,v.width,v.height};
    sdl_check(SDL_SetRenderDrawColor(renderer,0,0,0,255)==0,"Setting black bars");
    sdl_check(SDL_RenderClear(renderer)==0,"Clearing picture");
    sdl_check(SDL_RenderCopy(renderer,display.texture.get(),&source,&rect)==0,"Drawing picture");
    SDL_RenderPresent(renderer);
}
void toggle_fullscreen(SDL_Window* window) {
    const bool fullscreen=SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN_DESKTOP;
    sdl_check(SDL_SetWindowFullscreen(window,fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP)==0,"Toggling fullscreen");
}
FrameInput sample_input(SDL_Window* window,const Controller& c,const EnterKey& enter) {
    SDL_PumpEvents(); // Exactly one snapshot for this emulated frame.
    return frame_input(c.bindings(),held_input(window,c,&enter));
}
Audio::Audio(std::function<void(const std::string&)> warning):warning_(std::move(warning)) {
    if(!warning_)warning_=[](const std::string& text){message(text);};
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) {
        warning_(std::string("Audio unavailable; continuing silently: ")+SDL_GetError()); warned_=true;
    }
}
Audio::~Audio() { close(); }
void Audio::close() { if (device_) SDL_CloseAudioDevice(device_); device_=0; resampler_.reset(); }
void Audio::removed(SDL_AudioDeviceID device) {
    if (device_==device) { close(); std::cerr<<"Audio device removed; retrying default device.\n"; }
}
void Audio::open(double now) {
    if (now<retry_at_) return;
    retry_at_=now+1;
    SDL_AudioSpec wanted{},got{};
    wanted.freq=48000; wanted.format=AUDIO_S16SYS; wanted.channels=1; wanted.samples=512;
    if (!(SDL_WasInit(SDL_INIT_AUDIO)&SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
    device_=SDL_OpenAudioDevice(nullptr,0,&wanted,&got,SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!device_) {
        if (!warned_) { warning_(std::string("Audio unavailable; continuing silently and retrying: ")+SDL_GetError()); warned_=true; }
        return;
    }
    hz_=got.freq; resampler_=std::make_unique<Resampler>(hz_);
    std::cerr<<"Audio: "<<SDL_GetCurrentAudioDriver()<<", "<<hz_<<" Hz, mono S16, buffer "<<got.samples<<" samples.\n";
    warned_=false; reset();
}
void Audio::pause(bool paused) {
    if (paused_!=paused) { paused_=paused; reset(); }
}
void Audio::reset() {
    if (!device_) return;
    // A suppressed device must stay paused and empty, including during device
    // replacement and modal pauses. Resume only with a freshly primed queue.
    SDL_PauseAudioDevice(device_,1);
    SDL_ClearQueuedAudio(device_);
    resampler_=std::make_unique<Resampler>(hz_);
    if (paused_) return;
    std::vector<std::int16_t> silence(static_cast<std::size_t>(audio_target_seconds*hz_));
    if (SDL_QueueAudio(device_,silence.data(),static_cast<Uint32>(silence.size()*sizeof(std::int16_t)))!=0) {
        std::cerr<<"Cannot prime audio queue: "<<SDL_GetError()<<'\n'; close(); return;
    }
    SDL_PauseAudioDevice(device_,0);
}
void Audio::submit(std::span<const std::int16_t> input,double now) {
    if (!device_) open(now);
    if (!device_) return;
    if(paused_)return;
    auto queued=SDL_GetQueuedAudioSize(device_)/(2.0*hz_);
    if (queued==0) { ++underruns_; reset(); }
    if (queued>0.150) { ++resets_; reset(); }
    if (!device_) return;
    queued=SDL_GetQueuedAudioSize(device_)/(2.0*hz_);
    queue_min_=std::min(queue_min_,queued); queue_max_=std::max(queue_max_,queued);
    auto output=resampler_->process(input,rate_adjustment(queued));
    submitted_+=output.size();
    nonzero_+=std::count_if(output.begin(),output.end(),[](auto s) { return s!=0; });
    if (SDL_QueueAudio(device_,output.data(),static_cast<Uint32>(output.size()*sizeof(std::int16_t)))!=0) {
        std::cerr<<"Audio queue failed; retrying device: "<<SDL_GetError()<<'\n'; close();
    }
}
void Audio::report() const {
    std::cerr<<"audio_queue_ms="<<queue_min_*1000<<".."<<queue_max_*1000
             <<" underruns="<<underruns_<<" queue_resets="<<resets_
             <<" submitted="<<submitted_<<" nonzero="<<nonzero_<<'\n';
}
}
