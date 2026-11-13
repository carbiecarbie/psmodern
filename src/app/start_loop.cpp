// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/start_loop.h"
#include "app/controls_screen.h"
#include "app/language_screen.h"
#include "app/localization.h"
#include "app/frame_driver.h"
#include "app/start_screen.h"
#include "app/windows_platform.h"
#include <array>
#include <iostream>

namespace psmodern::app {
namespace {
std::string launcher_title(Language language) {
    return std::string("PSModern - Phantasy Star - ")+std::string(ui_text(UiText::presentation_language,language))+
        ": "+std::string(language_label(language,language));
}
// Dialogs, files and ROM identification for the launcher. Dialogs belong to
// the start screen window once it exists.
class SdlLauncherPlatform : public LauncherPlatform {
public:
    SDL_Window* window=nullptr;
    Language language=Language::english;
    std::optional<std::filesystem::path> pick_rom() override { return pick_rom_file(window,language); }
    void show_message(const std::string& text) override {
        show_notice(UiText::rom_refused,text);
    }
    void show_notice(UiText id,const std::string& text) override {
        std::cerr<<text<<'\n';
        const auto translated=std::string(ui_text(id,language));
        if (SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,"PSModern",translated.c_str(),window)!=0)
            std::cerr<<"Cannot show message: "<<SDL_GetError()<<'\n';
    }
    bool ask_retry(const std::string& text,const char* give_up) override {
        std::cerr<<text<<'\n';
        const auto again=std::string(ui_text(UiText::choose_again,language));
        const auto back=std::string(ui_text(std::string_view(give_up)=="Quit" ? UiText::quit : UiText::back,language));
        const auto translated=std::string(ui_text(UiText::rom_refused,language))+"\n\n"+std::string(ui_text(UiText::rom_choose_help,language));
        const SDL_MessageBoxButtonData buttons[]={
            {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,again.c_str()},{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,back.c_str()}};
        const SDL_MessageBoxData data{SDL_MESSAGEBOX_ERROR,window,"PSModern",translated.c_str(),2,buttons,nullptr};
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
std::optional<SessionRoms> run_launcher(std::optional<Display>& display,Language* current_language) {
    SdlLauncherPlatform platform;
    const auto loaded_language=load_language(production_save_folder());
    auto preference=loaded_language.preference;
    const auto remembered_language=load_rom_list(production_save_folder()).list;
    const auto initial_profile=!remembered_language.us && remembered_language.br ? RomVersion::brazilian : RomVersion::us_rev_a;
    platform.language=resolve_language(preference,initial_profile);
    if(current_language)*current_language=platform.language;
    RomList unreadable;
    auto list=prepare_launcher(platform,&unreadable);
    if (!list) return {};
    auto refresh_language=[&]() {
        const auto profile=list->us ? RomVersion::us_rev_a : RomVersion::brazilian;
        const auto wanted=resolve_language(preference,profile);
        platform.language=list->get(language_rom(wanted)) ? wanted : native_language(profile);
        if(platform.window)SDL_SetWindowTitle(platform.window,launcher_title(platform.language).c_str());
        if(current_language)*current_language=platform.language;
    };
    refresh_language();
    if(loaded_language.warning)platform.show_notice(UiText::language_file_warning,*loaded_language.warning);
    const RomFont font(*load_rom(primary_rom(*list)).rom);
    display.emplace(open_display(launcher_title(platform.language)));
    auto* window=display->window.get();
    platform.window=window;
    Controller controller; controller.refresh(); auto& enter=controller.enter();
    // The player's controls, for the Controls screen and the fullscreen button. A damaged file is reported and never blocks.
    const auto loaded=load_bindings(production_save_folder());
    if (loaded.warning) std::cerr<<*loaded.warning<<'\n';
    controller.set_bindings(loaded.bindings);
    ControlsScreen controls;
    LanguageScreen languages;
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
            if (pad && pad->down && !controls.visible() && !languages.visible() && controller.bindings().pads[Action::fullscreen]==pad->input) toggle_fullscreen(window);
            if ((event.type==SDL_KEYDOWN || event.type==SDL_KEYUP) && event.key.keysym.sym==SDLK_RETURN &&
                enter.event(event.type==SDL_KEYDOWN,event.key.keysym.mod&KMOD_ALT,event.key.repeat)) toggle_fullscreen(window);
        }
        if(languages.visible()) {
            if(languages.update(sample_start_input(window,controller,enter),now())==LanguageAction::save) {
                try {
                    save_language(production_save_folder(),languages.draft());
                    preference=languages.draft();refresh_language();languages.saved();
                } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';languages.failed();}
            }
            if(!languages.visible())screen=StartScreen(*list,StartAction::language);
        } else if (controls.visible()) {
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
        case StartAction::play: return load_session_roms(primary_rom(*list),*list,preference);
        case StartAction::quit: return {};
        case StartAction::choose: {
            // The new screen waits for a neutral input again, with the cursor
            // on PLAY after a ROM was added, or back on this entry.
            const auto added=choose_rom(platform,*list,false,unreadable);
            refresh_language();
            screen=StartScreen(*list,added ? StartAction::play : StartAction::choose);
            break;
        }
        case StartAction::controls: controls.open(controller.bindings()); break;
        case StartAction::language: {
            const auto available=load_session_roms(primary_rom(*list),*list,preference);
            languages.open(preference,available.available(Language::english),available.available(Language::portuguese),platform.language);
            break;
        }
        case StartAction::open_saves:
            try { open_folder(production_save_folder()); }
            catch (const std::exception& e) { platform.show_notice(UiText::cannot_open_saves,std::string("Cannot open the saves folder: ")+e.what()); }
            break;
        case StartAction::none: break;
        }
        if (!(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED)) {
            if(languages.visible())languages.render(pixels,font,platform.language);
            else if (controls.visible()) controls.render(pixels,font,platform.language);
            else screen.render(pixels,font,platform.language,preference);
            sdl_check(SDL_UpdateTexture(display->texture.get(),nullptr,pixels.data(),256*sizeof(std::uint32_t))==0,"Uploading picture");
            present(*display);
        }
        SDL_Delay(10);
    }
}
}
