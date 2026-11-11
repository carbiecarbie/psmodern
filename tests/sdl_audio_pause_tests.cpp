// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include <iostream>
#include <stdexcept>
namespace {
SDL_AudioDeviceID last_device=0;
unsigned nonzero_submissions=0;
int capture_queue(SDL_AudioDeviceID device,const void* data,Uint32 size) {
    const auto* samples=static_cast<const std::int16_t*>(data);
    for(Uint32 i=0;i<size/sizeof(*samples);++i)if(samples[i]!=0){++nonzero_submissions;break;}
    return SDL_QueueAudio(device,data,size);
}
SDL_AudioDeviceID capture_open(const char* name,int capture,const SDL_AudioSpec* wanted,SDL_AudioSpec* got,int flags) {
    last_device=SDL_OpenAudioDevice(name,capture,wanted,got,flags);return last_device;
}
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
}
#define SDL_OpenAudioDevice capture_open
#define SDL_QueueAudio capture_queue
#include "../src/app/sdl_host.cpp"
#undef SDL_OpenAudioDevice
#undef SDL_QueueAudio
int main(){try{
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    check(SDL_Init(SDL_INIT_AUDIO)==0,"dummy audio initialization");
    {
        psmodern::app::Audio audio;audio.pause(true);audio.submit({},0);
        check(last_device && SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PAUSED,"new device remains paused inside menu");
        const std::array<std::int16_t,4000> samples=[] { std::array<std::int16_t,4000> a{};a.fill(12000);return a; }();
        auto suppressed=[&](const char* why) {
            check(SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PAUSED && SDL_GetQueuedAudioSize(last_device)==0,why);
        };
        audio.submit(samples,.1);suppressed("paused device queues no nonzero audio");
        audio.removed(last_device);audio.submit(samples,2);suppressed("replacement remains paused and empty");
        audio.pause(false);check(SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PLAYING,"pause close resumes audio");
        const auto before=nonzero_submissions;audio.submit(samples,2.1);
        check(nonzero_submissions>before,"1x queues nonzero source audio");
        // The host now supplies normal-rate PCM at every gameplay speed.
        const auto continuous=nonzero_submissions;audio.submit(samples,2.2);
        check(nonzero_submissions>continuous,"continuous source remains audible");
        audio.removed(last_device);audio.submit(samples,4);
        check(SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PLAYING,"replacement resumes continuous sound");
        audio.pause(true);suppressed("pause stays empty");
        audio.pause(false);check(SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PLAYING,"normal audio resumes after pause and acceleration");
        check(SDL_GetQueuedAudioSize(last_device)<=48000*2/10,"normal queue is freshly bounded");
        const auto resumed=nonzero_submissions;audio.submit(samples,4.1);
        check(nonzero_submissions>resumed,"1x submits fresh nonzero sound after acceleration");
        audio.reset();
        check(SDL_GetAudioDeviceStatus(last_device)==SDL_AUDIO_PLAYING,"1x directly restores playback");
        check(SDL_GetQueuedAudioSize(last_device)<=48000*2/10,"direct return starts with bounded queue");
        audio.pause(true);suppressed("pause clears queued game audio");
    }
    SDL_Quit();std::cout<<"SDL audio modal pause passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';SDL_Quit();return 1;}}
