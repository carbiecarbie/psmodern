// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/start_loop.h"
#include "app/controls_screen.h"
#include "app/frame_driver.h"
#include "app/start_screen.h"
#include "app/windows_platform.h"
#include "PSModernVersion.h"
#include <array>
#include <iostream>

namespace psmodern::app {
namespace {
// Dialogs, files and ROM identification for the launcher. Dialogs belong to
// the start screen window once it exists.
class SdlLauncherPlatform : public LauncherPlatform {
public:
    SDL_Window* window=nullptr;
    std::optional<std::filesystem::path> pick_rom() override { return pick_rom_file(window); }
    void show_message(const std::string& text) override {
        std::cerr<<text<<'\n';
        if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,"PSModern",text.c_str(),window)!=0)
            std::cerr<<"Cannot show message: "<<SDL_GetError()<<'\n';
    }
    bool ask_retry(const std::string& text,const char* give_up) override {
        std::cerr<<text<<'\n';
        const SDL_MessageBoxButtonData buttons[]={
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Choose again"},{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,give_up}};
        const SDL_MessageBoxData data{SDL_MESSAGEBOX_ERROR,window,"PSModern",text.c_str(),2,buttons,nullptr};
        int pressed=0;
        sdl_check(SDL_ShowMessageBox(&data,&pressed)==0,"Showing a message");
        return pressed==1;
    }
    RomListLoad load_list() override { return load_rom_list(production_save_folder()); }
    void save_list(const RomList& list) override { save_rom_list(production_save_folder(),list); }
    RomVersion identify(const std::filesystem::path& path) override {
        // Only a file that is definitely gone is missing; any other failure to
        // look at it (offline share, unplugged drive) may pass.
        std::error_code error;
        const auto status=std::filesystem::status(path,error);
        if (error) throw RomUnreadable(error.message());
        if (status.type()==std::filesystem::file_type::not_found) throw std::runtime_error("The file is missing.");
        return identify_rom(path);
    }
};
}
std::optional<std::filesystem::path> run_launcher(std::optional<Display>& display) {
    SdlLauncherPlatform platform;
    RomList unreadable;
    auto list=prepare_launcher(platform,&unreadable);
    if (!list) return {};
    const RomFont font(*load_rom(font_rom(*list)).rom);
    display.emplace(open_display(std::string("PSModern ")+PSMODERN_VERSION));
    auto* window=display->window.get();
    platform.window=window;
    Controller controller; controller.refresh(); auto& enter=controller.enter();
    // The player's controls, for the Controls screen and the fullscreen button. A damaged file is reported and never blocks.
    const auto loaded=load_bindings(production_save_folder());
    if (loaded.warning) std::cerr<<*loaded.warning<<'\n';
    controller.set_bindings(loaded.bindings);
    ControlsScreen controls;
    StartScreen screen(*list);
    std::array<std::uint32_t,256*192> pixels{};
    const double frequency=static_cast<double>(SDL_GetPerformanceFrequency());
    auto now=[&]() { return SDL_GetPerformanceCounter()/frequency; };
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type==SDL_QUIT) return {};
            if (event.type==SDL_CONTROLLERDEVICEADDED || event.type==SDL_CONTROLLERDEVICEREMOVED) controller.refresh();
            const auto pad=controller.translate(event);
            if (pad && pad->down && !controls.visible() && controller.bindings().pads[Action::fullscreen]==pad->input) toggle_fullscreen(window);
            if ((event.type==SDL_KEYDOWN || event.type==SDL_KEYUP) && event.key.keysym.sym==SDLK_RETURN &&
                enter.event(event.type==SDL_KEYDOWN,event.key.keysym.mod&KMOD_ALT,event.key.repeat)) toggle_fullscreen(window);
        }
        if (controls.visible()) {
            if (controls.update(sample_controls_input(window,controller,enter),now())==ControlsAction::save) {
                try {
                    save_bindings(production_save_folder(),controls.draft());
                    controller.set_bindings(controls.draft());
                    controls.saved();
                } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; controls.failed(); }
            }
            // Back on the start screen it waits for a neutral input again, with the cursor on CONTROLS.
            if (!controls.visible()) screen=StartScreen(*list,StartAction::controls);
        } else switch (screen.update(sample_start_input(window,controller,enter),now())) {
        case StartAction::play_us: return list->us;
        case StartAction::play_br: return list->br;
        case StartAction::quit: return {};
        case StartAction::choose: {
            // The new screen waits for a neutral input again, with the cursor
            // on the ROM just added or back on this entry.
            const auto added=choose_rom(platform,*list,false,unreadable);
            screen=StartScreen(*list,added ? play_action(*added) : StartAction::choose);
            break;
        }
        case StartAction::controls: controls.open(controller.bindings()); break;
        case StartAction::open_saves:
            try { open_folder(production_save_folder()); }
            catch (const std::exception& e) { message(std::string("Cannot open the saves folder: ")+e.what(),window); }
            break;
        case StartAction::none: break;
        }
        if (!(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED)) {
            if (controls.visible()) controls.render(pixels,font); else screen.render(pixels,font);
            sdl_check(SDL_UpdateTexture(display->texture.get(),nullptr,pixels.data(),256*sizeof(std::uint32_t))==0,"Uploading picture");
            present(*display);
        }
        SDL_Delay(10);
    }
}
}
