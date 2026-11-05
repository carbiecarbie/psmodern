// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include "app/frame_driver.h"
#include "rom/sha1.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace psmodern;
using namespace psmodern::app;
int play(const std::filesystem::path& path) {
    const auto version=identify_rom(path);
    std::ifstream file(path,std::ios::binary);
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    if (file.bad() || rom->size()!=512*1024 || identify_digest(sha1(*rom))!=version)
        throw std::runtime_error("ROM changed or could not be read completely while loading.");
    FrameDriver driver(rom,SaveStore(production_save_folder(),version));
    std::optional<std::string> last_save_error;
    auto report_save=[&](const std::optional<std::string>& error) {
        if (error) {
            if (error!=last_save_error) message(*error);
            else std::cerr<<*error<<'\n';
        }
        last_save_error=error;
    };
    try {
        const auto title=std::string("PSModern - Phantasy Star (")+rom_name(version)+")";
        auto window=std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)>(
            SDL_CreateWindow(title.c_str(),SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,768,576,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI),SDL_DestroyWindow);
        sdl_check(bool(window),"Creating window");
        SDL_SetWindowMinimumSize(window.get(),256,192);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"0");
        auto renderer=std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)>(
            SDL_CreateRenderer(window.get(),-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC),SDL_DestroyRenderer);
        if (!renderer) renderer.reset(SDL_CreateRenderer(window.get(),-1,SDL_RENDERER_ACCELERATED));
        if (!renderer) renderer.reset(SDL_CreateRenderer(window.get(),-1,SDL_RENDERER_SOFTWARE));
        sdl_check(bool(renderer),"Creating renderer");
        SDL_RendererInfo renderer_info{};
        sdl_check(SDL_GetRendererInfo(renderer.get(),&renderer_info)==0,"Reading renderer information");
        SDL_DisplayMode display{};
        const int display_index=SDL_GetWindowDisplayIndex(window.get());
        const bool have_display=display_index>=0 && SDL_GetCurrentDisplayMode(display_index,&display)==0;
        std::cerr<<"Video: "<<renderer_info.name<<", vsync="
                 <<bool(renderer_info.flags&SDL_RENDERER_PRESENTVSYNC)
                 <<", display_hz="<<(have_display ? display.refresh_rate : 0)<<'\n';
        auto texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>(
            SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,256,192),SDL_DestroyTexture);
        sdl_check(bool(texture),"Creating picture texture");
        sdl_check(SDL_SetTextureScaleMode(texture.get(),SDL_ScaleModeNearest)==0,"Setting nearest-neighbour scaling");
        std::array<std::uint32_t,256*192> pixels{};
        Controller controller; controller.refresh(); Audio audio;
        const double frequency=static_cast<double>(SDL_GetPerformanceFrequency());
        auto now=[&]() { return SDL_GetPerformanceCounter()/frequency; };
        audio.submit({},now()); // Open and prime the device before starting the frame clock.
        const auto start=now(); double next_report=start+10;
        FrameClock clock(start);
        std::uint64_t frames=0; bool quit=false;
        auto report=[&]() {
            const auto elapsed=now()-start;
            std::cerr<<"frames="<<frames<<" elapsed_s="<<elapsed
                     <<" emulation_minus_wall_ms="<<(frames*frame_seconds-elapsed)*1000
                     <<" lag_rebase_s="<<clock.discarded_seconds()<<'\n';
            audio.report();
        };
        while (!quit) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type==SDL_QUIT) quit=true;
                if (event.type==SDL_CONTROLLERDEVICEADDED || event.type==SDL_CONTROLLERDEVICEREMOVED) controller.refresh();
                if (event.type==SDL_AUDIODEVICEREMOVED && !event.adevice.iscapture) audio.removed(event.adevice.which);
                if (event.type==SDL_KEYDOWN && !event.key.repeat) {
                    if (event.key.keysym.sym==SDLK_RETURN && (event.key.keysym.mod&KMOD_ALT)) {
                        const bool fullscreen=SDL_GetWindowFlags(window.get())&SDL_WINDOW_FULLSCREEN_DESKTOP;
                        sdl_check(SDL_SetWindowFullscreen(window.get(),fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP)==0,"Toggling fullscreen");
                    }
                    if (event.key.keysym.sym==SDLK_F5) { report_save(driver.power_cycle()); audio.reset(); }
                }
            }
            if (quit) break;
            const auto count=clock.due(now());
            for (unsigned i=0;i<count;++i) {
                const auto output=driver.run(sample_input(window.get(),controller));
                ++frames; report_save(output.save_error);
                audio.submit(output.samples.values,now());
            }
            if (count && !(SDL_GetWindowFlags(window.get())&SDL_WINDOW_MINIMIZED)) {
                const auto& picture=driver.machine().picture();
                std::transform(picture.begin(),picture.end(),pixels.begin(),rgb);
                sdl_check(SDL_UpdateTexture(texture.get(),nullptr,pixels.data(),256*sizeof(std::uint32_t))==0,"Uploading picture");
                int w,h; sdl_check(SDL_GetRendererOutputSize(renderer.get(),&w,&h)==0,"Reading output size");
                const auto v=viewport(w,h); const SDL_Rect rect{v.x,v.y,v.width,v.height};
                sdl_check(SDL_SetRenderDrawColor(renderer.get(),0,0,0,255)==0,"Setting black bars");
                sdl_check(SDL_RenderClear(renderer.get())==0,"Clearing picture");
                sdl_check(SDL_RenderCopy(renderer.get(),texture.get(),nullptr,&rect)==0,"Drawing picture");
                SDL_RenderPresent(renderer.get());
            }
            if (now()>=next_report) { report(); next_report=now()+10; }
            // Yield even in the last two milliseconds; the frame clock still
            // decides how many whole frames are due after the sleep.
            if (clock.deadline()>now()) SDL_Delay(1);
        }
        report();
    } catch (...) { report_save(driver.flush()); throw; }
    const auto error=driver.flush(); report_save(error);
    std::cerr<<"save_writes="<<driver.store().writes()<<'\n';
    return error ? 1 : 0;
}
}
int main(int argc,char** argv) {
    SDL_SetMainReady();
    try {
        if (argc!=2) throw std::runtime_error("Usage: psmodern.exe <rom-path>\nProvide an uncompressed US Rev-A or Brazilian .sms file.");
        sdl_check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)==0,"Starting SDL");
        const auto result=play(argv[1]); SDL_Quit(); return result;
    } catch (const std::exception& e) {
        message(e.what()); SDL_Quit(); return 1;
    }
}
