// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include "app/frame_driver.h"
#include "rom/sha1.h"
#include "replay/recording.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace psmodern;
using namespace psmodern::app;
int play(const std::filesystem::path& path,const std::optional<std::filesystem::path>& record_path,bool routines_enabled,
         const game::OptionOverrides& overrides) {
    const auto version=identify_rom(path);
    std::ifstream file(path,std::ios::binary);
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    if (file.bad() || rom->size()!=512*1024 || identify_digest(sha1(*rom))!=version)
        throw std::runtime_error("ROM changed or could not be read completely while loading.");
    FrameDriver driver(rom,SaveStore(production_save_folder(),version),routines_enabled,overrides,record_path.has_value());
    RomFont font(*rom);OptionsMenu menu;InputRelease release;
    if(record_path && std::filesystem::exists(*record_path))
        throw std::runtime_error("Recording output already exists: "+record_path->string());
    if(record_path && !std::filesystem::is_directory(record_path->has_parent_path() ? record_path->parent_path() : std::filesystem::current_path()))
        throw std::runtime_error("Recording parent folder does not exist.");
    std::optional<replay::Recorder> recorder;
    if(record_path) recorder.emplace(sha1(*rom),sha1(driver.machine().memory().cartridge_ram()));
    std::optional<std::string> last_save_error;
    auto report_save=[&](const std::optional<std::string>& error) {
        if (error) {
            if (error!=last_save_error) message(*error);
            else std::cerr<<*error<<'\n';
        }
        last_save_error=error;
    };
    report_save(driver.store().settings_warning());
    try {
        const auto title=std::string("PSModern - Phantasy Star (")+rom_name(version)+")"+
            (recorder ? " - RECORDING (F5 disabled)" : "");
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
        Vdp::Picture menu_background{};
        Controller controller; controller.refresh(); Audio audio; EnterKey enter;
        const double frequency=static_cast<double>(SDL_GetPerformanceFrequency());
        auto now=[&]() { return SDL_GetPerformanceCounter()/frequency; };
        audio.submit({},now()); // Open and prime the device before starting the frame clock.
        const auto start=now(); double next_report=start+10;
        FrameClock clock(start);
        std::uint64_t frames=0; bool quit=false,toggle_held=false;
        unsigned chosen_speed=1; SpeedShortcut speed_shortcut;
        auto effective_speed=[&]() { return !recorder && SDL_GetKeyboardFocus()==window.get() && !driver.menu_active() ? chosen_speed : 1u; };
        auto sync_speed=[&]() {
            const unsigned speed=effective_speed();
            if (clock.speed()!=speed) { clock.speed(speed,now()); audio.speed(speed); }
        };
        std::string displayed_title;
        auto report=[&]() {
            const auto elapsed=now()-start;
            std::cerr<<"frames="<<frames<<" elapsed_s="<<elapsed
                     <<" emulation_minus_wall_ms="<<(frames*frame_seconds-elapsed)*1000
                     <<" lag_rebase_s="<<clock.discarded_seconds()<<'\n';
            audio.report();
        };
        while (!quit) {
            SDL_Event event;
            std::vector<SDL_Event> speed_events;
            bool lost_focus=false,controller_changed=false,options_pressed=false,power_cycled=false;
            while (SDL_PollEvent(&event)) {
                if (event.type==SDL_QUIT) quit=true;
                if ((event.type==SDL_KEYDOWN || event.type==SDL_KEYUP) && event.key.windowID==SDL_GetWindowID(window.get()) &&
                    event.key.keysym.scancode==SDL_SCANCODE_TAB && !event.key.repeat) speed_events.push_back(event);
                if ((event.type==SDL_CONTROLLERBUTTONDOWN || event.type==SDL_CONTROLLERBUTTONUP) &&
                    event.cbutton.button==SDL_CONTROLLER_BUTTON_RIGHTSHOULDER && controller.owns(event.cbutton.which)) speed_events.push_back(event);
                if (event.type==SDL_KEYDOWN && event.key.windowID==SDL_GetWindowID(window.get()) &&
                    event.key.keysym.scancode==SDL_SCANCODE_ESCAPE && !event.key.repeat) options_pressed=true;
                if (event.type==SDL_CONTROLLERBUTTONDOWN && event.cbutton.button==SDL_CONTROLLER_BUTTON_Y &&
                    controller.owns(event.cbutton.which)) options_pressed=true;
                if (event.type==SDL_WINDOWEVENT && event.window.windowID==SDL_GetWindowID(window.get()) &&
                    event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) { lost_focus=true; speed_shortcut.focus_lost(); }
                if (event.type==SDL_CONTROLLERDEVICEADDED || event.type==SDL_CONTROLLERDEVICEREMOVED) {
                    controller_changed=true;controller.refresh();speed_shortcut.controller_changed();
                }
                if (event.type==SDL_AUDIODEVICEREMOVED && !event.adevice.iscapture) audio.removed(event.adevice.which);
                if ((event.type==SDL_KEYDOWN || event.type==SDL_KEYUP) && event.key.keysym.sym==SDLK_RETURN &&
                    enter.event(event.type==SDL_KEYDOWN,event.key.keysym.mod&KMOD_ALT,event.key.repeat)) {
                    const bool fullscreen=SDL_GetWindowFlags(window.get())&SDL_WINDOW_FULLSCREEN_DESKTOP;
                    sdl_check(SDL_SetWindowFullscreen(window.get(),fullscreen ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP)==0,"Toggling fullscreen");
                }
                if (event.type==SDL_KEYDOWN && !event.key.repeat) {
                    if (event.key.keysym.sym==SDLK_F5 && !recorder && !driver.menu_active()) { report_save(driver.power_cycle()); chosen_speed=1; audio.reset(); power_cycled=true; }
                }
            }
            if (quit) break;
            const auto menu_input=sample_menu_input(window.get(),controller);
            const bool speed_allowed=!recorder && !driver.menu_active() && !menu_input.toggle && !options_pressed && !power_cycled;
            if(!lost_focus) for(const auto& speed_event:speed_events) {
                const bool keyboard=speed_event.type==SDL_KEYDOWN || speed_event.type==SDL_KEYUP;
                if(!keyboard && controller_changed)continue;
                const bool down=keyboard ? speed_event.type==SDL_KEYDOWN : speed_event.type==SDL_CONTROLLERBUTTONDOWN;
                const bool pressed=keyboard ? speed_shortcut.keyboard_event(down,menu_input.focused,speed_allowed) :
                    speed_shortcut.shoulder_event(down,menu_input.focused,speed_allowed);
                if(pressed)chosen_speed=chosen_speed==1 ? 2 : chosen_speed==2 ? 4 : 1;
            }
            const auto* keys=SDL_GetKeyboardState(nullptr);
            if (speed_shortcut.update(menu_input.focused,menu_input.focused && keys[SDL_SCANCODE_TAB],
                                      controller.fast_forward(),speed_allowed))
                chosen_speed=chosen_speed==1 ? 2 : chosen_speed==2 ? 4 : 1;
            sync_speed();
            const bool opening=!driver.menu_active() && menu_input.toggle && !toggle_held;
            toggle_held=menu_input.toggle;
            if(opening && !recorder && menu_input.focused && !menu_input.up && !menu_input.down &&
                !menu_input.left && !menu_input.right && !menu_input.accept && !menu_input.cancel) {
                const auto input=sample_input(window.get(),controller,enter);
                if(!input.reset && !input.pause && input.pads==std::array<Pad,2>{} && driver.request_menu())
                    menu_background=driver.machine().picture();
            }
            if(menu.visible()) {
                const auto action=menu.update(menu_input,now());
                if(action==MenuAction::apply) {
                    if(const auto error=driver.choose_options(menu.draft())) { report_save(error);menu.failed(); }
                    else { menu.close();driver.close_menu();report_save({});release.consume(); }
                } else if(action==MenuAction::close) { menu.close();driver.close_menu();release.consume(); }
            }
            const auto count=clock.due(now());
            for (unsigned i=0;i<count;++i) {
                const auto input=release.filter(sample_input(window.get(),controller,enter),menu_input.any(),menu_input.focused);
                const bool was_active=driver.menu_active();
                const auto output=driver.run(input);
                if(recorder) recorder->add(input);
                ++frames; report_save(output.save_error);
                audio.submit(output.samples.values,now());
                if(!was_active && driver.menu_active()) {
                    menu.open(driver.chosen_options(),driver.options_restricted(),driver.options_pending(),false);
                    release.consume();
                }
                if(was_active && !driver.menu_active())release.consume();
                // A menu may open or finish closing in this frame. Discard
                // the remainder calculated at the previous effective speed.
                if (clock.speed()!=effective_speed()) { sync_speed(); break; }
            }
            const auto& effective=driver.effective_options();
            const auto current_title=title+" - Encounters "+std::to_string(effective.encounter_percent)+
                "% - Experience/Meseta "+std::to_string(effective.experience_percent)+"%"+
                (driver.options_pending() ? " - CHANGES SAVED, WAITING FOR GAME" : "")+
                " - Speed "+std::to_string(clock.speed())+"X (Tab / RB) - Options: Esc / Y";
            if(current_title!=displayed_title) {SDL_SetWindowTitle(window.get(),current_title.c_str());displayed_title=current_title;}
            if ((count || menu.visible()) && !(SDL_GetWindowFlags(window.get())&SDL_WINDOW_MINIMIZED)) {
                const auto& picture=driver.menu_active() ? menu_background : driver.machine().picture();
                std::transform(picture.begin(),picture.end(),pixels.begin(),rgb);
                if(menu.visible())menu.render(pixels,font);
                else if(clock.speed()>1) {
                    for(int y=2;y<12;++y) for(int x=234;x<254;++x) pixels[y*256+x]=0xff000000;
                    font.text(pixels,236,3,clock.speed()==2 ? "2X" : "4X",0xffffffff);
                }
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
    if(recorder) {
        replay::write_atomic_new(*record_path,recorder->recording());
        std::cerr<<"recorded_frames="<<recorder->recording().frames<<" recording="<<record_path->string()<<'\n';
    }
    std::cerr<<"save_writes="<<driver.store().writes()<<'\n';
    return error ? 1 : 0;
}
}
int main(int argc,char** argv) {
    SDL_SetMainReady();
    try {
        const auto args=parse_app_arguments(argc,argv);
        sdl_check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)==0,"Starting SDL");
        const auto result=play(args.rom,args.record_path,args.routines_enabled,args.overrides); SDL_Quit(); return result;
    } catch (const std::exception& e) {
        message(e.what()); SDL_Quit(); return 1;
    }
}
