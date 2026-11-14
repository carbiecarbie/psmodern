// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include "app/frame_driver.h"
#include "replay/recording.h"
#include "temporary_cleanup.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
using namespace psmodern;
unsigned sampled=0,stage=0,modal_frames=0,mode_under_test=0,speed_stage=0;
bool release_speed=false;
bool sent_quit=false,toggle_sent=false,was_active=false,restored_speed=false,saved_menu=false,saved_game=false;
// Mode 3: Options -> CONTROLS -> back to Options -> Button 2 saves on exit.
unsigned controls_calls=0,controls_frames=0;bool awaiting_controls=false,controls_closed=false;
// Mode 4: as mode 3, but the Controls screen sets BUTTON 1 to K, the first
// SAVE fails (controls.txt is a folder) and the second succeeds. While it is
// open the power-cycle key and pad input, the pad fullscreen input and the
// speed key are pressed; none of them may act.
std::filesystem::path controls_folder;
// Mode 5: change all values, fail to save on Button 2, then retry with Options.
std::filesystem::path blocked_options;
bool checked_options_failure=false;
app::Bindings mode4_start;
SDL_JoystickID virtual_pad=-1;
unsigned power_cycles=0,fullscreen_toggles=0,shortcut_events=0;
bool send_shortcuts=false,hold_tab=false,checked_failed_save=false,checked_new_binding=false;
void counted_fullscreen(SDL_Window* window){++fullscreen_toggles;app::toggle_fullscreen(window);}
SDL_Window* focused_window=nullptr;
SDL_Window* scripted_focus(){return focused_window;}
const Uint8* scripted_keys(int*) {
    static std::array<Uint8,SDL_NUM_SCANCODES> keys{};keys.fill(0);
    if(hold_tab){keys[SDL_SCANCODE_TAB]=1;return keys.data();}
    if(release_speed) {release_speed=false;return keys.data();}
    if(speed_stage<2 && sampled>=(speed_stage ? 6u : 2u)) {keys[SDL_SCANCODE_TAB]=1;++speed_stage;release_speed=true;}
    return keys.data();
}
std::uint64_t last_modal_cycle=0;
app::FrameDriver* tested=nullptr;
std::filesystem::path recording_path,image_dir;
class SeededDriver : public app::FrameDriver {
public:
    SeededDriver(std::shared_ptr<const Memory::Rom> rom,app::SaveStore store,bool enabled,game::OptionOverrides overrides,bool original)
        :app::FrameDriver(std::move(rom),std::move(store),enabled,overrides,original) {
        const auto rec=replay::read(recording_path);
        for(unsigned i=0;i<3400;++i)run(rec.input_at(i));
        tested=this;
    }
    std::optional<std::string> power_cycle(){++power_cycles;return app::FrameDriver::power_cycle();}
};
int scripted_poll(SDL_Event* event){
    if(send_shortcuts) {
        // F5 down/up, then the bound pad power and fullscreen inputs down/up, then Tab down/up.
        const unsigned i=shortcut_events++;
        *event={};
        if(i<2 || i>=6){
            event->type=i%2 ? SDL_KEYUP : SDL_KEYDOWN;event->key.windowID=SDL_GetWindowID(focused_window);
            event->key.keysym.scancode=i<2 ? SDL_SCANCODE_F5 : SDL_SCANCODE_TAB;event->key.keysym.sym=i<2 ? SDLK_F5 : SDLK_TAB;
        }else{
            event->type=i%2 ? SDL_CONTROLLERBUTTONUP : SDL_CONTROLLERBUTTONDOWN;event->cbutton.which=virtual_pad;
            event->cbutton.button=i<4 ? SDL_CONTROLLER_BUTTON_LEFTSTICK : SDL_CONTROLLER_BUTTON_RIGHTSTICK;
        }
        if(shortcut_events==8)send_shortcuts=false;
        return 1;
    }
    if(!sent_quit && ((mode_under_test==1 && sampled>=35) || (mode_under_test!=1 && was_active && tested && !tested->menu_active())))
        {sent_quit=true;event->type=SDL_QUIT;return 1;}
    return 0;
}
FrameInput scripted_input(SDL_Window*,const app::Controller&,const app::EnterKey&){++sampled;return {};}
app::MenuInput scripted_menu(SDL_Window* w,const app::Controller& controller){
    focused_window=w;
    using app::MenuInput;
    if(mode_under_test==4 && controls_closed && !checked_new_binding) {
        // The saved draft is the game's binding now: K is button 1, Z is not.
        checked_new_binding=true;
        app::InputSnapshot k,z;k.keys[SDL_SCANCODE_K]=true;z.keys[SDL_SCANCODE_Z]=true;
        const auto& b=controller.bindings();
        if(b.keys[app::Action::button1]!=SDL_SCANCODE_K || !app::frame_input(b,k).pads[0].one || app::frame_input(b,z).pads[0].one ||
           b.pads!=mode4_start.pads)
            throw std::runtime_error("The saved controls are not the game's controls");
    }
    if(sampled<12 || speed_stage<2 || release_speed)return {};
    if(!toggle_sent){toggle_sent=true;return {.toggle=true};}
    if(!tested || !tested->menu_active())return {};
    was_active=true;
    if(mode_under_test>=3) {
        // Two ups reach CONTROLS through LANGUAGE; accept opens Controls. Back in
        // Options (re-armed by a neutral snapshot), two downs wrap to ENCOUNTERS,
        // right changes 200 to 0 and either exit binding saves.
        if(mode_under_test==5) {
            switch(stage++) {
                case 1:return {.right=true};
                case 3:return {.down=true};
                case 5:return {.right=true};
                case 7:return {.down=true};
                case 9:return {.right=true};
                case 11:return {.down=true};
                case 13:return {.right=true};
                case 15:
                    blocked_options=tested->store().path();blocked_options+=".options";
                    std::filesystem::rename(blocked_options,controls_folder/"held-options");
                    std::filesystem::create_directory(blocked_options);
                    return {.cancel=true};
                case 17:
                    if(tested->chosen_options()!=game::Options{200,100} || tested->automap_option() || tested->widescreen_option())
                        throw std::runtime_error("Failed exit changed the active Options choice");
                    checked_options_failure=true;
                    std::filesystem::remove(blocked_options);
                    std::filesystem::rename(controls_folder/"held-options",blocked_options);
                    return {.toggle=true};
                default:return {};
            }
        }
        if(awaiting_controls && !controls_closed)return {};
        switch(stage++){
            case 1:return {.up=true};
            case 3:return {.up=true};
            case 5:awaiting_controls=true;return {.accept=true};
            case 7:return {.down=true};
            case 9:return {.down=true};
            case 11:return {.right=true};
            case 13:return mode_under_test==3 ? MenuInput{.cancel=true} : MenuInput{.toggle=true};
            default:return {};
        }
    }
    switch(stage++){
        case 0:return {};
        case 1:return mode_under_test==0 ? MenuInput{.right=true} : MenuInput{};
        case 2:return {};
        case 3:return mode_under_test==2 ? MenuInput{.cancel=true} : MenuInput{.accept=true};
        default:return {};
    }
}
// The Controls screen's raw snapshot. Mode 3: a fresh Escape on the second
// call closes it (no save, so nothing is written). Mode 4: a press, release
// script (see above); each odd call presses, each even call releases.
app::InputSnapshot scripted_controls(SDL_Window*,const app::Controller& controller,const app::EnterKey&){
    if(mode_under_test<3 || !awaiting_controls || controls_closed)throw std::runtime_error("Controls screen sampled outside its round trip");
    app::InputSnapshot input;
    const unsigned call=++controls_calls;
    if(mode_under_test==3){
        if(call==2){input.keys[SDL_SCANCODE_ESCAPE]=true;controls_closed=true;}
        return input;
    }
    // Presses on odd calls from 3: four downs to BUTTON 1, Z, K, six ups
    // to SAVE, Z (fails), Z (succeeds).
    static constexpr SDL_Scancode script[]{SDL_SCANCODE_DOWN,SDL_SCANCODE_DOWN,SDL_SCANCODE_DOWN,SDL_SCANCODE_DOWN,SDL_SCANCODE_Z,SDL_SCANCODE_K,
        SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_Z,SDL_SCANCODE_Z};
    constexpr unsigned count=std::size(script),failing=count-2;
    if(call==2){
        if(!controller.connected() || controller.bindings()!=mode4_start)throw std::runtime_error("Mode 4 needs the virtual controller and its controls file");
        send_shortcuts=true;hold_tab=true;
    }
    if(call==4){
        hold_tab=false;
        if(shortcut_events!=8 || power_cycles || fullscreen_toggles)throw std::runtime_error("A shortcut acted while the Controls screen was open");
    }
    if(call>=3 && call%2 && (call-3)/2<count){
        const unsigned step=(call-3)/2;
        if(step==failing){std::filesystem::remove(controls_folder/app::bindings_file_name);std::filesystem::create_directory(controls_folder/app::bindings_file_name);}
        if(step==failing+1){
            // The failed SAVE kept the screen open (it is still sampled) and applied nothing.
            if(controller.bindings()!=mode4_start || !std::filesystem::is_directory(controls_folder/app::bindings_file_name))
                throw std::runtime_error("A failed save changed the controls");
            checked_failed_save=true;std::filesystem::remove(controls_folder/app::bindings_file_name);
        }
        if(step==count-1)controls_closed=true;
        input.keys[script[step]]=true;
    }
    if(call>3+2*count)throw std::runtime_error("The Controls screen did not close after SAVE");
    return input;
}
// Mode 5 (the canvas): the session canvas follows the applied widescreen switch. Narrow, with
// the driver off, until the failed exit is checked; after the successful retry the canvas is the
// 344-based wide one and the driver is on.
bool saw_wide_after_apply=false;
void checked_bands(app::Display& display,const app::WidePicture* wide) {
    if(mode_under_test==5 && tested) {
        int output_width=0,output_height=0;
        if(SDL_GetRendererOutputSize(display.renderer.get(),&output_width,&output_height)!=0)throw std::runtime_error("Cannot read the output size");
        if(display.wide_canvas!=tested->widescreen())throw std::runtime_error("The canvas does not follow the driver's widescreen state");
        if(!checked_options_failure) {
            if(display.wide_canvas || tested->widescreen() || output_width%344==0)throw std::runtime_error("A failed Options exit changed the canvas");
        } else if(display.wide_canvas) {
            if(output_width%344!=0)throw std::runtime_error("The wide canvas is not a 344-based window");
            saw_wide_after_apply=true;
        }
    }
    app::upload_bands(display,wide);
}
int checked_upload(const Machine& machine,unsigned speed,bool visible,bool controls,bool active,const Vdp::Picture& background,SDL_Texture* texture,const SDL_Rect* rect,const void* pixels,int pitch){
    const auto* rendered=static_cast<const std::uint32_t*>(pixels);
    if(controls){
        // The Controls screen covers the whole picture over the held game.
        if(!visible || !active || speed!=1)throw std::runtime_error("Controls screen without the held Options bridge");
        if(machine.cycles()>last_modal_cycle){last_modal_cycle=machine.cycles();++modal_frames;}
        ++controls_frames;
    }else if(visible){
        if(speed!=1)throw std::runtime_error("Options ran accelerated");
        if(mode_under_test==0 && !saved_menu){
            saved_menu=true;std::ofstream f(image_dir/"options-4x-session.ppm",std::ios::binary);f<<"P6\n256 192\n255\n";
            for(unsigned i=0;i<256*192;++i){const char rgb[]{char(rendered[i]>>16),char(rendered[i]>>8),char(rendered[i])};f.write(rgb,3);}
        }
        if(machine.cycles()>last_modal_cycle){last_modal_cycle=machine.cycles();++modal_frames;}
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)
            if((x<24||x>=232||y<24||y>=176) && rendered[y*256+x]!=app::rgb(background[y*256+x]))
                throw std::runtime_error("Options changed pixels outside its window");
    }else if(!active){
        if(speed==4 && !was_active && !saved_game && mode_under_test==0) {
            saved_game=true;std::ofstream f(image_dir/"exploration-4x.ppm",std::ios::binary);f<<"P6\n256 192\n255\n";
            for(unsigned i=0;i<256*192;++i){const char rgb[]{char(rendered[i]>>16),char(rendered[i]>>8),char(rendered[i])};f.write(rgb,3);}
        }
        if(was_active && speed==4)restored_speed=true;
        for(std::size_t i=0;i<machine.picture().size();++i) {
            const auto x=i%256,y=i/256;
            if(speed>1 && x>=234 && x<254 && y>=2 && y<12)continue;
            // Widescreen on (the centre is uploaded at x 44): the band renderer's picture replaces the
            // VDP's blanked left column (x 0-7); everything else must still equal the VDP picture.
            if(rect && rect->x!=0 && x<8)continue;
            if(rendered[i]!=app::rgb(machine.picture()[i]))throw std::runtime_error("Closed options changed the game picture");
        }
    }
    return SDL_UpdateTexture(texture,rect,pixels,pitch);
}
// The start screen's Controls flow through the real run_launcher: CONTROLS,
// BUTTON 1 set to K and SAVE; back on the start screen the file is written,
// the bindings applied and the cursor re-armed on CONTROLS (accept reopens
// it); Escape leaves the Controls screen, then the start screen.
unsigned start_calls=0,launcher_controls_calls=0,launcher_sessions=0;bool checked_launcher_save=false;
int launcher_poll(SDL_Event*){return 0;}
app::MenuInput scripted_start(SDL_Window*,const app::Controller& controller,const app::EnterKey&){
    static unsigned session_seen=0,calls=0;
    if(session_seen!=launcher_sessions){session_seen=launcher_sessions;calls=0;}
    launcher_controls_calls=0;++calls;
    if(++start_calls>60)throw std::runtime_error("The start screen script did not finish");
    if(launcher_sessions==0)return calls==2 || calls==4 ? app::MenuInput{.down=true} : calls==6 ? app::MenuInput{.accept=true} : app::MenuInput{};
    if(launcher_sessions==1 && calls==1){
        auto expected=app::Bindings::defaults();expected.keys[app::Action::button1]=SDL_SCANCODE_K;
        const auto saved=app::load_bindings(controls_folder);
        if(controller.bindings()!=expected || saved.warning || saved.bindings!=expected)throw std::runtime_error("The start screen's SAVE did not write and apply the controls");
        checked_launcher_save=true;
    }
    if(calls==2)return launcher_sessions==1 ? app::MenuInput{.accept=true} : app::MenuInput{.cancel=true};
    return {};
}
app::InputSnapshot launcher_controls(SDL_Window*,const app::Controller&,const app::EnterKey&){
    if(launcher_controls_calls++==0)++launcher_sessions;
    const unsigned call=launcher_controls_calls;
    if(call>40)throw std::runtime_error("The start screen's Controls screen did not close");
    app::InputSnapshot input;
    if(launcher_sessions==2){input.keys[SDL_SCANCODE_ESCAPE]=call==2;return input;}
    // Presses on odd calls from 3: four downs to BUTTON 1, Z, K, six ups to SAVE, Z.
    static constexpr SDL_Scancode script[]{SDL_SCANCODE_DOWN,SDL_SCANCODE_DOWN,SDL_SCANCODE_DOWN,SDL_SCANCODE_DOWN,SDL_SCANCODE_Z,SDL_SCANCODE_K,
        SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_UP,SDL_SCANCODE_Z};
    if(call>=3 && call%2 && (call-3)/2<std::size(script))input.keys[script[(call-3)/2]]=true;
    return input;
}
}
#include "app/start_loop.h"
#include "app/windows_platform.h"
#define SDL_PollEvent launcher_poll
#define sample_start_input scripted_start
#define sample_controls_input launcher_controls
#define pick_rom_file(...) std::optional<std::filesystem::path>()
#define open_folder(folder) (void)(folder)
#include "../src/app/start_loop.cpp"
#undef SDL_PollEvent
#undef sample_start_input
#undef sample_controls_input
#undef pick_rom_file
#undef open_folder
// The scripted game sessions pass their own argv and never reach the start screen.
#define utf8_arguments(argc,argv) std::vector<std::string>(argv,argv+argc)
#define attach_parent_console()
#define run_launcher(...) std::optional<psmodern::app::SessionRoms>()
#define main options_app_main
#define FrameDriver SeededDriver
#define SDL_PollEvent scripted_poll
#define SDL_GetKeyboardFocus scripted_focus
#define SDL_GetKeyboardState scripted_keys
#define sample_input scripted_input
#define sample_menu_input scripted_menu
#define sample_controls_input scripted_controls
#define toggle_fullscreen counted_fullscreen
#define upload_bands(d,w) checked_bands(d,w)
#define SDL_UpdateTexture(t,r,p,pitch) checked_upload(driver.machine(),clock.speed(),menu.visible(),controls_screen.visible(),driver.menu_active(),menu_background,t,r,p,pitch)
#include "../src/app/main.cpp"
#undef main
#undef utf8_arguments
#undef attach_parent_console
#undef run_launcher
#undef FrameDriver
#undef SDL_PollEvent
#undef SDL_GetKeyboardFocus
#undef SDL_GetKeyboardState
#undef sample_input
#undef sample_menu_input
#undef sample_controls_input
#undef toggle_fullscreen
#undef SDL_UpdateTexture
#undef upload_bands
int main(int argc,char**argv){try{
    if(argc!=3)return 1;if(!*argv[1]||!std::filesystem::is_regular_file(argv[1]))return 77;
    recording_path=argv[2];const auto root=std::filesystem::temp_directory_path()/("psmodern-options-session-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);SDL_setenv("APPDATA",root.string().c_str(),1);SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    const auto version=identify_rom(argv[1]);
    image_dir=std::filesystem::current_path()/(version==RomVersion::us_rev_a ? "options-us" : "options-br");std::filesystem::create_directories(image_dir);
    controls_folder=root/"PSModern";
    for(mode_under_test=0;mode_under_test<6;++mode_under_test){
        sampled=stage=modal_frames=speed_stage=0;release_speed=false;sent_quit=toggle_sent=was_active=restored_speed=saved_menu=saved_game=false;last_modal_cycle=0;tested=nullptr;
        controls_calls=controls_frames=0;awaiting_controls=controls_closed=false;
        power_cycles=fullscreen_toggles=shortcut_events=0;send_shortcuts=hold_tab=checked_failed_save=checked_new_binding=false;
        checked_options_failure=saw_wide_after_apply=false;
        if(mode_under_test==4){
            // A controls file with the power cycle and fullscreen on the stick
            // buttons, and a virtual controller (hardware back-ends off, as in
            // sdl_input) so the pad events reach the bindings.
            mode4_start=app::Bindings::defaults();
            app::assign_pad(mode4_start,app::Action::power,app::PadInput::left_stick);app::assign_pad(mode4_start,app::Action::fullscreen,app::PadInput::right_stick);
            app::save_bindings(controls_folder,mode4_start);
            for(const char* driver:{SDL_HINT_JOYSTICK_RAWINPUT,SDL_HINT_JOYSTICK_HIDAPI,SDL_HINT_JOYSTICK_WGI,SDL_HINT_XINPUT_ENABLED,SDL_HINT_DIRECTINPUT_ENABLED})
                SDL_SetHintWithPriority(driver,"0",SDL_HINT_OVERRIDE);
            SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0xffff/0x5053",SDL_HINT_OVERRIDE);
            if(SDL_Init(SDL_INIT_GAMECONTROLLER)!=0 || SDL_NumJoysticks()!=0)throw std::runtime_error("Isolated controller subsystem");
            SDL_VirtualJoystickDesc descriptor{};descriptor.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
            descriptor.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;descriptor.naxes=SDL_CONTROLLER_AXIS_MAX;descriptor.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
            descriptor.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;descriptor.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
            descriptor.vendor_id=0xffff;descriptor.product_id=0x5053;descriptor.name="PSModern options session controller";
            const auto index=SDL_JoystickAttachVirtualEx(&descriptor);
            if(index<0)throw std::runtime_error("Virtual controller attachment");
            virtual_pad=SDL_JoystickGetDeviceInstanceID(index);
        }
        char rate[]="--encounter-rate",value[]="200",raw[]="--no-routines",record[]="--record";
        std::string recording=(root/"session.psrec").string();
        char* normal[]{argv[0],argv[1],rate,value};char* recording_args[]{argv[0],argv[1],record,recording.data()};char* raw_args[]{argv[0],argv[1],raw};
        const auto result=options_app_main(mode_under_test==2?3:4,mode_under_test==1?recording_args:mode_under_test==2?raw_args:normal);
        if(result || (mode_under_test==1 ? was_active : !was_active || modal_frames==0 || !restored_speed))throw std::runtime_error("Scripted options app session failed");
        if((mode_under_test==3 || mode_under_test==4) && (!controls_closed || controls_frames==0 || stage<14))throw std::runtime_error("Options -> Controls -> Options round trip incomplete");
        if(mode_under_test==5 && (!checked_options_failure || stage<18 || !saw_wide_after_apply))
            throw std::runtime_error("Failed Options exit did not keep all values for a successful retry");
        if(mode_under_test==4){
            SDL_Quit();   // balances the test's own SDL_Init; the virtual controller goes with it
            const auto saved=app::load_bindings(controls_folder);
            auto expected=mode4_start;expected.keys[app::Action::button1]=SDL_SCANCODE_K;
            if(!checked_failed_save || !checked_new_binding || power_cycles || fullscreen_toggles || saved.warning || saved.bindings!=expected)
                throw std::runtime_error("Options -> Controls -> SAVE did not write and apply the new controls");
            std::filesystem::remove(controls_folder/app::bindings_file_name);
        }
        if(std::filesystem::exists(root/"PSModern"/app::bindings_file_name))throw std::runtime_error("Closing the Controls screen wrote the controls file");
        auto store=app::SaveStore::shared(root/"PSModern");
        if(store.load_preferences(store.load())!=(mode_under_test==5 ? game::Options{0,200} : game::Options{0,100}) || store.automap_preference()!=(mode_under_test==5) || store.widescreen_preference()!=(mode_under_test==5))
            throw std::runtime_error("Options exit or restricted session persisted the wrong rates or automap");
    }
    {   // The start screen (video only: no controller is opened).
        app::RomList list;list.set(version,argv[1]);app::save_rom_list(controls_folder,list);
        if(SDL_Init(SDL_INIT_VIDEO)!=0)throw std::runtime_error("Starting SDL for the start screen");
        std::optional<app::Display> display;
        const auto chosen=app::run_launcher(display);
        display.reset();SDL_Quit();
        if(chosen || launcher_sessions!=2 || !checked_launcher_save)throw std::runtime_error("Start screen -> Controls -> SAVE -> Controls -> back -> quit incomplete");
    }
    test::remove_temporary_folder(root);std::cout<<"SDL options sessions: both exit bindings save, failed exit retries all values, recording ignored, raw-Z80 readonly, live sound, backdrop, the Controls screen (cancel, failed and good SAVE, shortcuts held off) and its start screen flow passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
