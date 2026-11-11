// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include <iostream>
#include <stdexcept>
namespace {
SDL_AudioDeviceID last_device=0;
SDL_AudioDeviceID capture_open(const char* name,int capture,const SDL_AudioSpec* wanted,SDL_AudioSpec* got,int flags) {
    last_device=SDL_OpenAudioDevice(name,capture,wanted,got,flags);return last_device;
}
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
}
#define SDL_OpenAudioDevice capture_open
#include "../src/app/sdl_host.cpp"
#undef SDL_OpenAudioDevice
int main(){try{
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    check(SDL_Init(SDL_INIT_AUDIO)==0,"dummy audio initialization");
    {
        psmodern::app::Audio audio;audio.pause(true);audio.submit({},0);
        check(last_device && SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PAUSED,"new device remains paused inside menu");
        const auto queued=SDL_GetQueuedAudioSize(last_device);
        const std::array<std::int16_t,100> samples{};audio.submit(samples,.1);
        check(SDL_GetQueuedAudioSize(last_device)==queued,"menu does not queue emulation samples");
        audio.removed(last_device);audio.submit({},2);
        check(SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PAUSED,"replaced device remains paused inside menu");
        audio.pause(false);check(SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PLAYING,"menu close resumes audio");
        audio.pause(true);check(SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PAUSED && SDL_GetQueuedAudioSize(last_device)==0,"pause clears queued game audio");
    }
    SDL_Quit();std::cout<<"SDL audio modal pause passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';SDL_Quit();return 1;}}
