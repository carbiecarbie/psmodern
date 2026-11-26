// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include "app/automap_screen.h"
#include "app/controls_screen.h"
#include "app/localization.h"
#include "app/sound_preference.h"
#include "app/frame_driver.h"
#include "app/shared_save.h"
#include "app/start_loop.h"
#include "app/platform.h"
#ifdef __ANDROID__
#include "app/android_controls.h"
#endif
#include "PSModernVersion.h"
#include "rom/sha1.h"
#include "replay/recording.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
using namespace psmodern;
using namespace psmodern::app;
// Shared only by this single UI thread, including failures before/after play.
// Updates after a successful language Save keep the outer failure dialog in
// the actual UI language rather than guessing from a missing-ROM preference.
Language failure_language=Language::english;
constexpr int play_cancelled=2;
std::optional<RomVersion> choose_legacy_save(const LegacySaves& saves,Language language,SDL_Window* window) {
    std::vector<SDL_MessageBoxButtonData> buttons;
    for(unsigned i=0;i<2;++i) {
        if(!saves[i].error.empty())std::cerr<<"Legacy "<<(i ? "BR" : "US")<<": "<<saves[i].error<<'\n';
        if(saves[i].state==LegacySaveState::available)
            buttons.push_back({0,int(i),ui_text(i ? UiText::legacy_br : UiText::legacy_us,language).data()});
    }
    buttons.push_back({SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,2,ui_text(UiText::cancel,language).data()});
    const bool unavailable=saves[0].state==LegacySaveState::unavailable || saves[1].state==LegacySaveState::unavailable;
    const SDL_MessageBoxData data{SDL_MESSAGEBOX_INFORMATION,window,ui_text(UiText::legacy_title,language).data(),
        ui_text(unavailable ? UiText::legacy_unavailable : UiText::legacy_choice,language).data(),int(buttons.size()),buttons.data(),nullptr};
    int selected=-1;
    sdl_check(SDL_ShowMessageBox(&data,&selected)==0,"Choosing old saved games");
    if(selected!=0 && selected!=1)return {};
    return selected==0 ? RomVersion::us_rev_a : RomVersion::brazilian;
}
// Plays a loaded ROM until the window closes. From the command line the
// window opens only after the ROM, the save lock and the recording checks
// pass; from the start screen it is the start screen's window (fullscreen
// kept) and buttons still held from choosing Play do not reach the game.
int play(const SessionRoms& session,Display* launcher_display,const std::optional<std::filesystem::path>& record_path,
         const game::OptionOverrides& overrides) {
    const auto version=session.source_version; const auto& rom=session.image(session.active);
    auto language=session.active;
    failure_language=language;
    auto preference=session.preference;
    std::optional<SaveStore> store;
    try {
        store=open_shared_save(production_save_folder(),[&](const LegacySaves& saves) {
            return choose_legacy_save(saves,language,launcher_display ? launcher_display->window.get() : nullptr);
        });
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';message(std::string(ui_text(UiText::legacy_failed,language)));return 1;
    }
    if(!store)return play_cancelled;
#ifdef __ANDROID__
    // SDLActivity runs native code on a Java thread with a small stack.
    auto owned_driver=std::make_unique<FrameDriver>(rom,std::move(*store),overrides,record_path.has_value(),session.sound_rom);
    auto& driver=*owned_driver;
#else
    FrameDriver driver(rom,std::move(*store),overrides,record_path.has_value(),session.sound_rom);
#endif
    driver.configure_languages(session);
    // The installation's sound preference. It is kept as saved even while FM
    // cannot play (no Japanese ROM, or its engine stopped until power cycle).
    auto sound_preference=SoundPreference::psg;
    if(!record_path) {
        const auto saved=load_sound_preference(production_save_folder());
        if(saved.warning)std::cerr<<*saved.warning<<'\n';
        sound_preference=saved.preference;
        driver.choose_fm(sound_preference==SoundPreference::fm);
    }
    // Whether the Sound row can offer FM now: the engine can stop at any
    // frame, Options open or not, and returns only at power cycle.
    auto sound_availability=[&] {
        return driver.fm_available() ? SoundAvailability::available :
            session.sound_rom ? SoundAvailability::failed : SoundAvailability::no_rom;
    };
    RomFont font(*rom);OptionsMenu menu;MapScreen map_screen;InputRelease release;
    if(launcher_display) release.consume();
    if(record_path && std::filesystem::exists(*record_path))
        throw std::runtime_error("Recording output already exists: "+record_path->string());
    if(record_path && !std::filesystem::is_directory(record_path->has_parent_path() ? record_path->parent_path() : std::filesystem::current_path()))
        throw std::runtime_error("Recording parent folder does not exist.");
    std::optional<replay::Recorder> recorder;
    if(record_path) recorder.emplace(sha1(*rom),sha1(driver.machine().memory().cartridge_ram()));
    std::optional<std::string> last_save_error;
    std::optional<std::string> stopped; // why the engine stopped the game, if it did
    auto report_save=[&](const std::optional<std::string>& error,UiText summary=UiText::save_write_failed) {
        if (error) {
            if (error!=last_save_error) {std::cerr<<*error<<'\n';message(std::string(ui_text(summary,language)));}
            else std::cerr<<*error<<'\n';
        }
        last_save_error=error;
    };
    report_save(driver.store().settings_warning(),UiText::preferences_warning);
    if(driver.store().automap_warning())report_save(driver.store().automap_warning(),UiText::automap_warning);
    for(const auto& warning:session.warnings)std::cerr<<warning<<'\n';
    if(session.language_unavailable)message(std::string(ui_text(UiText::language_fallback,language)));
    // The player's controls; a damaged file is noted and never stops play.
    // Nothing is written here: only the Controls screen saves.
    auto loaded_bindings=load_bindings(production_save_folder());
    if(loaded_bindings.warning) std::cerr<<*loaded_bindings.warning<<'\n';
    // "TAB / RB": an action's key and controller labels, either one omitted
    // when unbound.
    auto hint=[](const Bindings& b,Action a) {
        std::string text=b.keys[a] ? std::string(key_token(*b.keys[a])) : std::string();
        if(b.pads[a]) text+=(text.empty() ? "" : " / ")+std::string(pad_label(*b.pads[a]));
        return text;
    };
    auto base_title=[&](const Bindings& b) {
        const auto power=hint(b,Action::power);
        return std::string("PSModern - Phantasy Star")+
            " - "+std::string(ui_text(UiText::presentation_language,language))+": "+std::string(language_label(language,language))+
            (recorder ? " - "+std::string(ui_text(UiText::recording,language))+
                (power.empty() ? std::string() : " ("+power+": "+std::string(ui_text(UiText::power_cycle_disabled,language))+")") : "");
    };
    try {
        const auto title=base_title(loaded_bindings.bindings);
        Display own_display;
        if(!launcher_display) own_display=open_display(title);
        auto& display=launcher_display ? *launcher_display : own_display;
        auto* window=display.window.get();
        SDL_SetWindowTitle(window,title.c_str());
        // Milestone 10: the session's canvas, 344 wide with the widescreen
        // option, else the narrow 256 of 0.4.0. The start screen before this
        // was always narrow.
        bool wide_canvas=driver.widescreen_option();
        apply_canvas(display,wide_canvas);
        driver.set_widescreen(wide_canvas);
        // The driver's wide picture of the last frame, or null (pillarbox).
        auto wide_picture=[&]()->const WidePicture* { return driver.wide_picture(); };
        std::array<std::uint32_t,256*192> pixels{};
        Vdp::Picture menu_background{};
        // Beside menu_background: the wide picture when the hold began, so a
        // held menu keeps its bands.
        std::optional<WidePicture> wide_menu_background;
        auto capture_menu_background=[&]() {
            menu_background=driver.machine().picture();
            if(const auto* wide=wide_picture())wide_menu_background=*wide; else wide_menu_background.reset();
        };
        Controller controller; controller.refresh();
        Audio audio([&](const std::string& details) {
            std::cerr<<details<<'\n';message(std::string(ui_text(UiText::audio_unavailable,language)),window);
        });
        auto& enter=controller.enter();
        controller.set_bindings(loaded_bindings.bindings);
        ControlsScreen controls_screen;
        const double frequency=static_cast<double>(SDL_GetPerformanceFrequency());
        auto now=[&]() { return SDL_GetPerformanceCounter()/frequency; };
        audio.submit({},now()); // Open and prime the device before starting the frame clock.
        const auto start=now(); double next_report=start+10;
        auto report_slow=[&](std::string_view operation,double began,std::uint64_t frame) {
            const auto elapsed=now()-began;
            if(elapsed>=.045)std::cerr<<"host_slow="<<operation<<" frame="<<frame<<" duration_ms="<<elapsed*1000<<'\n';
        };
        FrameClock clock(start);
        std::uint64_t frames=0; bool quit=false,toggle_held=false,map_held=false;
        unsigned chosen_speed=1; SpeedShortcut speed_shortcut;
#ifdef __ANDROID__
        AndroidControls mobile(display,font,controller.connected());
        auto focused=[] { return android_foreground(); };
#else
        auto focused=[&] { return SDL_GetKeyboardFocus()==window; };
#endif
        auto console_input=[&]() {
            auto input=sample_input(window,controller,enter);
#ifdef __ANDROID__
            input=mobile.frame(input);
#endif
            return input;
        };
        auto effective_speed=[&]() { return !recorder && focused() && !driver.menu_active() ? chosen_speed : 1u; };
        auto sync_speed=[&]() {
            const unsigned speed=effective_speed();
            if (clock.speed()!=speed) { clock.speed(speed,now()); driver.speed(speed); }
        };
        std::string displayed_title;
        std::map<unsigned,std::uint64_t> present_frames; unsigned present_speed=1;
        auto report=[&]() {
            const auto elapsed=now()-start;
            std::cerr<<"frames="<<frames<<" elapsed_s="<<elapsed
                     <<" emulation_minus_wall_ms="<<(frames*frame_seconds-elapsed)*1000
                     <<" lag_rebase_s="<<clock.discarded_seconds()
                     <<" speed="<<clock.speed()<<" sound="<<(driver.fm_selected() ? "fm" : "psg")<<'\n';
            audio.report();
            // Issue 7: how many game frames each presented picture carried
            // since the last report, at one speed (restarted when it changes).
            if (!present_frames.empty()) {
                std::cerr<<"present_frames speed="<<present_speed;
                for (const auto& [carried,pictures]:present_frames) std::cerr<<' '<<carried<<':'<<pictures;
                std::cerr<<'\n';
                present_frames.clear();
            }
        };
        while (!quit) {
#ifdef __ANDROID__
            if (android_foreground()) mobile.layout(!driver.menu_active(),driver.automap_enabled());
#endif
            SDL_Event event;
            struct SpeedEvent { bool keyboard,down; };
            std::vector<SpeedEvent> speed_events;
            bool fullscreen_changed=false;
            bool lost_focus=false,controller_changed=false,options_pressed=false,power_cycled=false;
            while (SDL_PollEvent(&event)) {
                if (event.type==SDL_QUIT) quit=true;
#ifdef __ANDROID__
                // SDL 2 delivers the background pair before its next pump sleeps.
                // Flush on this thread while the driver is between frames.
                if (event.type==SDL_APP_WILLENTERBACKGROUND) {
                    mobile.clear(); release.consume(); speed_shortcut.focus_lost();
                    chosen_speed=1; sync_speed(); audio.pause(true);
                    if (const auto error=driver.flush()) std::cerr<<*error<<'\n';
                }
                if (event.type==SDL_APP_DIDENTERFOREGROUND) {
                    clock.rebase(now()); next_report=now()+10; audio.pause(false); mobile.redraw=true;
                }
                if (android_foreground()) mobile.event(event);
#endif
                if (event.type==SDL_WINDOWEVENT && event.window.windowID==SDL_GetWindowID(window) &&
                    event.window.event==SDL_WINDOWEVENT_FOCUS_LOST) { lost_focus=true; speed_shortcut.focus_lost(); }
                if (event.type==SDL_CONTROLLERDEVICEADDED || event.type==SDL_CONTROLLERDEVICEREMOVED) {
                    controller_changed=true;controller.refresh();speed_shortcut.controller_changed();
                }
                if (event.type==SDL_AUDIODEVICEREMOVED && !event.adevice.iscapture) audio.removed(event.adevice.which);
                if ((event.type==SDL_KEYDOWN || event.type==SDL_KEYUP) && event.key.keysym.sym==SDLK_RETURN &&
                    enter.event(event.type==SDL_KEYDOWN,event.key.keysym.mod&KMOD_ALT,event.key.repeat)) {
                    fullscreen_changed=true;
                    const auto began=now();toggle_fullscreen(window);report_slow("fullscreen",began,frames);
                }
                // The shortcuts follow the bindings. A pad change is read once
                // per event (a held trigger reports one press). Return, when
                // bound, never presses with Alt or while Alt+Enter consumes it.
                const auto& b=controller.bindings();
                const auto pad=controller.translate(event);
                auto key_is=[&](Action a)->std::optional<bool> {
                    if(!b.keys[a])return std::nullopt;
                    const auto change=key_change(event,window,*b.keys[a]);
                    if(change==true && *b.keys[a]==SDL_SCANCODE_RETURN && ((event.key.keysym.mod&KMOD_ALT) || !enter.pause(true)))
                        return std::nullopt;
                    return change;
                };
                auto pad_is=[&](Action a)->std::optional<bool> {
                    return pad && b.pads[a]==pad->input ? std::optional<bool>(pad->down) : std::nullopt;
                };
                if (const auto down=key_is(Action::speed)) speed_events.push_back({true,*down});
                if (const auto down=pad_is(Action::speed)) speed_events.push_back({false,*down});
                if (key_is(Action::options)==true || pad_is(Action::options)==true) options_pressed=true;
                if ((key_is(Action::power)==true || pad_is(Action::power)==true) && !recorder && !driver.menu_active() &&
                    !controls_screen.visible()) { report_save(driver.power_cycle()); chosen_speed=1; std::cerr<<"audio_event=power_cycle frame="<<frames<<" host=game\n"; audio.reset(); power_cycled=true; }
                if (pad_is(Action::fullscreen)==true && !controls_screen.visible()) { fullscreen_changed=true; toggle_fullscreen(window); }
            }
            if (quit) break;
#ifdef __ANDROID__
            if (!android_foreground()) { SDL_Delay(5); continue; }
#endif
            auto menu_input=sample_menu_input(window,controller);
#ifdef __ANDROID__
            menu_input=mobile.menu(menu_input,false,driver.menu_active());
#endif
            const bool speed_allowed=!recorder && !driver.menu_active() && !menu_input.toggle && !options_pressed && !power_cycled;
#ifdef __ANDROID__
            if (speed_allowed && mobile.speed_pressed()) chosen_speed=chosen_speed==1 ? 2 : chosen_speed==2 ? 4 : 1;
#endif
            if(!lost_focus) for(const auto& speed_event:speed_events) {
                if(!speed_event.keyboard && controller_changed)continue;
                const bool pressed=speed_event.keyboard ? speed_shortcut.keyboard_event(speed_event.down,menu_input.focused,speed_allowed) :
                    speed_shortcut.shoulder_event(speed_event.down,menu_input.focused,speed_allowed);
                if(pressed)chosen_speed=chosen_speed==1 ? 2 : chosen_speed==2 ? 4 : 1;
            }
            {
                const auto& b=controller.bindings();
                const auto* keys=SDL_GetKeyboardState(nullptr);
                const auto speed_key=b.keys[Action::speed];
                const bool key_held=menu_input.focused && speed_key && keys[*speed_key] &&
                    (*speed_key!=SDL_SCANCODE_RETURN || (enter.pause(true) && !keys[SDL_SCANCODE_LALT] && !keys[SDL_SCANCODE_RALT]));
                const bool pad_held=b.pads[Action::speed] && controller.held(*b.pads[Action::speed]);
                if (speed_shortcut.update(menu_input.focused,key_held,pad_held,speed_allowed))
                    chosen_speed=chosen_speed==1 ? 2 : chosen_speed==2 ? 4 : 1;
            }
            sync_speed();
            const bool opening=!driver.menu_active() && menu_input.toggle && !toggle_held;
            toggle_held=menu_input.toggle;
            if(opening && !recorder && menu_input.focused && !menu_input.up && !menu_input.down &&
                !menu_input.left && !menu_input.right && !menu_input.accept && !menu_input.cancel) {
                const auto input=console_input();
                if(!input.reset && !input.pause && input.pads==std::array<Pad,2>{} && driver.request_menu())
                    capture_menu_background();
            }
            // The map binding (M / X by default) opens the automap at the dungeon walking
            // wait, holding the game as Options does; with the option off the
            // press does nothing. The key never reaches the game's pad.
            const bool map_opening=!driver.menu_active() && menu_input.map && !map_held;
            map_held=menu_input.map;
            if(map_opening && !recorder && driver.automap_enabled() && menu_input.focused && !menu_input.up && !menu_input.down &&
                !menu_input.left && !menu_input.right && !menu_input.accept && !menu_input.cancel && !menu_input.toggle) {
                const auto input=console_input();
                if(!input.reset && !input.pause && input.pads==std::array<Pad,2>{} && driver.request_menu(HostMenu::map))
                    capture_menu_background();
            }
            if(map_screen.visible() && map_screen.update(menu_input)==MenuAction::close) {
                map_screen.close();driver.close_menu();release.consume();
            }
            // The Controls screen opens over Options and returns to it; the
            // game stays held by the Options bridge throughout. Its own fixed
            // navigation reads the raw snapshot, never the bindings.
            if(controls_screen.visible()) {
                auto input=sample_controls_input(window,controller,enter);
#ifdef __ANDROID__
                input=mobile.navigation(input,controls_screen.capturing());
#endif
                if(controls_screen.update(input,now())==ControlsAction::save) {
                    try {
                        save_bindings(production_save_folder(),controls_screen.draft());
                        controller.set_bindings(controls_screen.draft());controls_screen.saved();
                    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';controls_screen.failed(); }
                }
                if(!controls_screen.visible())menu.resume();
            } else if(menu.visible()) {
                menu.sound_available(sound_availability());
                const auto action=menu.update(menu_input,now());
                if(action==MenuAction::apply) {
                    const auto began=now();
                    const auto error=driver.choose_options(menu.draft(),menu.automap_draft(),menu.widescreen_draft());
                    report_slow("save-options",began,frames);
                    if(error) { report_save(error);menu.failed(); }
                    else {
                        // Applied at once, as the automap is.
                        wide_canvas=driver.widescreen_option();
                        apply_canvas(display,wide_canvas);
                        driver.set_widescreen(wide_canvas);
                        menu.close();driver.close_menu();report_save({});release.consume(); }
                } else if(action==MenuAction::close) { menu.close();driver.close_menu();release.consume(); }
                else if(action==MenuAction::controls) controls_screen.open(controller.bindings());
                else if(action==MenuAction::language) {
                    // The Language row switches at once, saved as the Language
                    // screen's Save is: the labels change now, the game text
                    // when Options closes.
                    try {
                        driver.prepare_language(menu.language_draft());
                        save_language(production_save_folder(),menu.language_draft());
                        driver.commit_language();
                        preference=menu.language_draft();language=resolve_language(preference,version);
                        failure_language=language;
                        menu.language_saved();
                    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';menu.language_failed(preference);}
                } else if(action==MenuAction::sound) {
                    // The Sound row (milestone 17) switches what is heard at
                    // once, the music at the same point, after the file is saved.
                    try {
                        save_sound_preference(production_save_folder(),menu.sound_draft());
                        sound_preference=menu.sound_draft();
                        driver.choose_fm(sound_preference==SoundPreference::fm);
                        menu.sound_saved();
                        std::cerr<<"audio_event=sound frame="<<frames<<" sound="<<(driver.fm_selected() ? "fm" : "psg")<<'\n';
                    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';menu.sound_failed(sound_preference);}
                }
            }
            const auto count=clock.due(now());
            for (unsigned i=0;i<count;++i) {
                const auto input=release.filter(console_input(),menu_input.any(),menu_input.focused);
#ifdef __ANDROID__
                mobile.end_frame();
                if (!android_foreground()) break;
#endif
                const bool was_active=driver.menu_active();
                FrameOutput output;
                const auto began=now();
                try { output=driver.run(input); }
                catch(const std::exception& error) {
                    // Milestone 14 (planning decision 6): code with no C++ body,
                    // or a state a body does not handle, stops the game; there
                    // is no Z80 to fall back on. The recording keeps the frame
                    // that stopped it, so that it reproduces the stop.
                    if(recorder) recorder->add(input);
                    stopped=error.what();
                    break;
                }
                report_slow(driver.menu_active() ? "menu-frame" : "game-frame",began,frames+1);
                if(recorder) recorder->add(input);
                ++frames; report_save(output.save_error);
                if(output.audio_error)std::cerr<<*output.audio_error<<'\n';
                audio.submit(output.playback ? output.playback->values : output.samples.values,now(),frames,
                    fullscreen_changed ? "fullscreen" : lost_focus || !menu_input.focused ? "unfocused" :
                    driver.menu_active() ? "host-menu" : options_pressed ? "options-request" :
                    input.pads[0].one || input.pads[0].two ? "game-button" : "game",clock.speed(),output.playback_sample_cycles);
                if(!was_active && driver.menu_active()) {
                    if(driver.menu_purpose()==HostMenu::map) map_screen.open();
                    else {
                        menu.open(driver.chosen_options(),driver.options_pending(),driver.automap_option(),driver.widescreen_option());
                        menu.set_language(preference,{session.available(LanguagePreference::native),
                            session.available(LanguagePreference::english),session.available(LanguagePreference::portuguese)});
                        menu.set_sound(sound_preference,sound_availability());
                    }
                    release.consume();
                }
                if(was_active && !driver.menu_active())release.consume();
                // A menu may open or finish closing in this frame. Discard
                // the remainder calculated at the previous effective speed.
                if (clock.speed()!=effective_speed()) { sync_speed(); break; }
            }
#ifdef __ANDROID__
            mobile.end_poll();
            if (!android_foreground()) continue;
#endif
            if (stopped) {
                // The player sees a message box over the game window (in the
                // session language, the technical details in English below
                // it); closing it ends the session as closing the window does,
                // except that the save written is the last frame end's (see
                // FrameDriver::flush), and the program exits with status 1.
                std::cerr<<"Game stopped: "<<*stopped<<'\n';
                message(game_stopped_message(language,*stopped),window);
                break;
            }
            const auto& effective=driver.effective_options();
            const auto& b=controller.bindings();
            const auto speed_hint=hint(b,Action::speed),options_hint=hint(b,Action::options),map_hint=hint(b,Action::map);
            const auto current_title=base_title(b)+" - "+std::string(ui_text(UiText::encounters,language))+" "+std::to_string(effective.encounter_percent)+
                "% - "+std::string(ui_text(UiText::experience_meseta,language))+" "+std::to_string(effective.experience_percent)+"%"+
                (driver.options_pending() ? " - "+std::string(ui_text(UiText::changes_waiting,language)) : "")+
                " - "+std::string(ui_text(UiText::speed,language))+" "+std::to_string(clock.speed())+"X"+(speed_hint.empty() ? "" : " ("+speed_hint+")")+
                (options_hint.empty() ? "" : " - "+std::string(ui_text(UiText::options,language))+": "+options_hint)+
                (driver.automap_enabled() && !map_hint.empty() ? " - "+std::string(ui_text(UiText::map,language))+": "+map_hint : "");
            if(current_title!=displayed_title) {SDL_SetWindowTitle(window,current_title.c_str());displayed_title=current_title;}
            bool redraw=count || menu.visible() || map_screen.visible();
#ifdef __ANDROID__
            mobile.layout(!driver.menu_active(),driver.automap_enabled());
            redraw|=mobile.redraw;
#endif
            if (redraw && !(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED)) {
                const auto& picture=driver.menu_active() ? menu_background : driver.machine().picture();
                // The wide picture only on the wide canvas; the narrow canvas
                // shows the VDP picture alone, as 0.4.0 did.
                const WidePicture* wide=!display.wide_canvas ? nullptr :
                    driver.menu_active() ? (wide_menu_background ? &*wide_menu_background : nullptr) : wide_picture();
                centre_pixels(pixels,picture,wide);
                if(controls_screen.visible())controls_screen.render(pixels,font,language);
                else if(menu.visible()) {
                    // The frames just run may have stopped FM: show what is heard.
                    menu.sound_available(sound_availability());
                    menu.render(pixels,font,language);
                }
                else if(map_screen.visible() && driver.map_view())render_automap(pixels,font,*driver.map_view(),language);
                else if(clock.speed()>1) {
                    for(int y=2;y<12;++y) for(int x=234;x<254;++x) pixels[y*256+x]=0xff000000;
                    font.text(pixels,236,3,clock.speed()==2 ? "2X" : "4X",0xffffffff);
                }
                // The overlays stay 256 wide in the centre; the bands go round them.
                upload_bands(display,wide);
                const auto centre=centre_rect(display);
                sdl_check(SDL_UpdateTexture(display.texture.get(),&centre,pixels.data(),256*sizeof(std::uint32_t))==0,"Uploading picture");
                const auto began=now();
#ifdef __ANDROID__
                mobile.present(language,clock.speed());
#else
                present(display);
#endif
                report_slow("present",began,frames);
                if (clock.speed()!=present_speed) { present_frames.clear(); present_speed=clock.speed(); }
                ++present_frames[count];
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
    return session_status(error.has_value(),driver.stopped().has_value());
}
}
int main(int argc,char** argv) {
    attach_parent_console();
#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS,"0");
    // SDL on Android turns mouse buttons into touches by default (issue 6).
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS,"0");
    SDL_SetHint(SDL_HINT_ANDROID_BLOCK_ON_PAUSE,"1");
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON,"1");
    SDL_SetHint(SDL_HINT_ORIENTATIONS,"LandscapeLeft LandscapeRight Portrait PortraitUpsideDown");
#else
    SDL_SetMainReady();
#endif
    try {
        const auto words=utf8_arguments(argc,argv);
        std::vector<const char*> pointers;
        for (const auto& word:words) pointers.push_back(word.c_str());
        const auto args=parse_app_arguments(static_cast<int>(pointers.size()),pointers.data());
        if (args.mode==AppMode::version) { std::cout<<"PSModern "<<PSMODERN_VERSION<<'\n'; return 0; }
#ifdef __ANDROID__
        sdl_check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)==0,"Starting SDL");
        watch_android_lifecycle();
#endif
        const auto initial_language=load_language(production_save_folder()).preference;
        failure_language=initial_language==LanguagePreference::portuguese ? Language::portuguese : Language::english;
#ifndef __ANDROID__
        sdl_check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_TIMER)==0,"Starting SDL");
#endif
        int result=0;
        if (args.mode==AppMode::launcher) {
            std::optional<Display> display;
            do {
                const auto chosen=run_launcher(display,&failure_language);
                if(!chosen) {result=0;break;}
                result=play(*chosen,&*display,{},{});
            } while(result==play_cancelled);
        } else {
            const auto preferences=load_language(production_save_folder());
            const auto remembered=load_rom_list(production_save_folder());
            const auto sound_path=args.record_path ? SoundRomPathLoad{} : load_sound_rom_path(production_save_folder());
            if(sound_path.warning)std::cerr<<*sound_path.warning<<'\n';
            const auto session=load_session_roms(args.rom,remembered.list,preferences.preference,args.record_path.has_value(),sound_path.path);
            failure_language=session.active;
            if(preferences.warning) {std::cerr<<*preferences.warning<<'\n';message(std::string(ui_text(UiText::language_file_warning,session.active)));}
            if(remembered.warning)std::cerr<<*remembered.warning<<'\n';
            result=play(session,nullptr,args.record_path,args.overrides);
        }
        SDL_Quit(); return result==play_cancelled ? 0 : result;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        message(std::string(ui_text(UiText::startup_failed,failure_language))); SDL_Quit(); return 1;
    }
}
