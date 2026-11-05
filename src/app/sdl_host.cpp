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
Controller::~Controller() { if (controller_) SDL_GameControllerClose(controller_); }
void Controller::refresh() {
    if (controller_ && !SDL_GameControllerGetAttached(controller_)) {
        SDL_GameControllerClose(controller_); controller_=nullptr;
    }
    if (!controller_) {
        for (int i=0;i<SDL_NumJoysticks();++i) {
            if (SDL_IsGameController(i)) {
                controller_=SDL_GameControllerOpen(i);
                if (controller_) break;
                std::cerr<<"Cannot open controller: "<<SDL_GetError()<<'\n';
            }
        }
    }
}
Buttons Controller::sample() const {
    if (!controller_ || !SDL_GameControllerGetAttached(controller_)) return {};
    auto b=[&](SDL_GameControllerButton button) { return SDL_GameControllerGetButton(controller_,button)!=0; };
    return controller({{b(SDL_CONTROLLER_BUTTON_DPAD_UP),b(SDL_CONTROLLER_BUTTON_DPAD_DOWN),
                        b(SDL_CONTROLLER_BUTTON_DPAD_LEFT),b(SDL_CONTROLLER_BUTTON_DPAD_RIGHT),
                        b(SDL_CONTROLLER_BUTTON_A),b(SDL_CONTROLLER_BUTTON_B)},
                       b(SDL_CONTROLLER_BUTTON_START),b(SDL_CONTROLLER_BUTTON_BACK)},
                      SDL_GameControllerGetAxis(controller_,SDL_CONTROLLER_AXIS_LEFTX),
                      SDL_GameControllerGetAxis(controller_,SDL_CONTROLLER_AXIS_LEFTY));
}
FrameInput sample_input(SDL_Window* window,const Controller& c) {
    SDL_PumpEvents(); // Exactly one snapshot for this emulated frame.
    Keys keys{};
    if (SDL_GetKeyboardFocus()==window) {
        const auto* k=SDL_GetKeyboardState(nullptr);
        keys={k[SDL_SCANCODE_UP]!=0,k[SDL_SCANCODE_DOWN]!=0,k[SDL_SCANCODE_LEFT]!=0,k[SDL_SCANCODE_RIGHT]!=0,
              k[SDL_SCANCODE_Z]!=0,k[SDL_SCANCODE_X]!=0,
              k[SDL_SCANCODE_RETURN] && !(k[SDL_SCANCODE_LALT]||k[SDL_SCANCODE_RALT]),k[SDL_SCANCODE_BACKSPACE]!=0};
    }
    return combine(keyboard(keys),c.sample());
}
Audio::Audio() {
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)!=0) {
        message(std::string("Audio unavailable; continuing silently: ")+SDL_GetError()); warned_=true;
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
        if (!warned_) { message(std::string("Audio unavailable; continuing silently and retrying: ")+SDL_GetError()); warned_=true; }
        return;
    }
    hz_=got.freq; resampler_=std::make_unique<Resampler>(hz_);
    std::cerr<<"Audio: "<<SDL_GetCurrentAudioDriver()<<", "<<hz_<<" Hz, mono S16, buffer "<<got.samples<<" samples.\n";
    warned_=false; reset();
    if (device_) SDL_PauseAudioDevice(device_,0);
}
void Audio::reset() {
    if (!device_) return;
    SDL_ClearQueuedAudio(device_); resampler_=std::make_unique<Resampler>(hz_);
    std::vector<std::int16_t> silence(static_cast<std::size_t>(audio_target_seconds*hz_));
    if (SDL_QueueAudio(device_,silence.data(),static_cast<Uint32>(silence.size()*sizeof(std::int16_t)))!=0) {
        std::cerr<<"Cannot prime audio queue: "<<SDL_GetError()<<'\n'; close();
    }
}
void Audio::submit(std::span<const std::int16_t> input,double now) {
    if (!device_) open(now);
    if (!device_) return;
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
