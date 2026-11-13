// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include "app/frame_driver.h"
#include "game/routines.h"
#include "temporary_cleanup.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
using namespace psmodern;
unsigned sampled=0,press_stage=0,focus_event=0,quick_event=0,options_event=0; unsigned marks[4]{}; double times[4]{}; bool got_badge[2]{},sent_quit=false,release_key=false,record_mode=false,sent_f5=false,saw_f5_reset=false,saw_before_f5=false;
unsigned audio_speed=1;std::size_t audible[3]{};
std::size_t queued_audible[3]{};SDL_AudioDeviceID audio_device=0;bool removed_audio=false;
int checked_queue(SDL_AudioDeviceID device,const void* data,Uint32 size) {
    audio_device=device;
    const auto* samples=static_cast<const std::int16_t*>(data);
    for(Uint32 i=0;i<size/sizeof(*samples);++i)if(samples[i])++queued_audible[audio_speed==4 ? 2 : audio_speed==2 ? 1 : 0];
    return SDL_QueueAudio(device,data,size);
}
std::shared_ptr<const Memory::Rom> shadow_rom;
std::unique_ptr<Machine> shadow; RoutineRegistry shadow_routines;
std::filesystem::path image_dir;
SDL_Window* focus_window=nullptr;
double time_now(){return static_cast<double>(SDL_GetPerformanceCounter())/SDL_GetPerformanceFrequency();}
int scripted_poll(SDL_Event* e) {
    if(!record_mode && sampled>=300 && !removed_audio && audio_device) {
        removed_audio=true;e->type=SDL_AUDIODEVICEREMOVED;e->adevice.which=audio_device;e->adevice.iscapture=0;return 1;
    }
    if(!record_mode && sampled>=100 && focus_event<3) {
        if(focus_event==1) {
            e->type=SDL_KEYUP;e->key.windowID=SDL_GetWindowID(focus_window);e->key.repeat=0;
            e->key.keysym.scancode=SDL_SCANCODE_TAB;e->key.keysym.sym=SDLK_TAB;
        } else {
            e->type=SDL_WINDOWEVENT;e->window.windowID=SDL_GetWindowID(focus_window);
            e->window.event=focus_event==0 ? SDL_WINDOWEVENT_FOCUS_LOST : SDL_WINDOWEVENT_FOCUS_GAINED;
        }
        ++focus_event;return 1;
    }
    if(!record_mode && sampled>=130 && options_event<3) {
        e->type=options_event==1 ? SDL_KEYUP : SDL_KEYDOWN;e->key.windowID=SDL_GetWindowID(focus_window);e->key.repeat=0;
        e->key.keysym.scancode=options_event==2 ? SDL_SCANCODE_ESCAPE : SDL_SCANCODE_TAB;
        e->key.keysym.sym=options_event==2 ? SDLK_ESCAPE : SDLK_TAB;++options_event;return 1;
    }
    if(!record_mode && sampled>=440 && quick_event<2) {
        e->type=quick_event++==0 ? SDL_KEYDOWN : SDL_KEYUP;e->key.windowID=SDL_GetWindowID(focus_window);
        e->key.repeat=0;e->key.keysym.scancode=SDL_SCANCODE_TAB;e->key.keysym.sym=SDLK_TAB;return 1;
    }
    if(!record_mode && sampled>=460 && !sent_f5){
        // The power-cycle binding is matched by scancode in the game window.
        sent_f5=true;e->type=SDL_KEYDOWN;e->key.windowID=SDL_GetWindowID(focus_window);e->key.repeat=0;
        e->key.keysym.scancode=SDL_SCANCODE_F5;e->key.keysym.sym=SDLK_F5;e->key.keysym.mod=KMOD_NONE;return 1;
    }
    if(sampled>=(record_mode ? 80u : 500u) && !sent_quit){sent_quit=true;e->type=SDL_QUIT;return 1;}return 0;
}
const Uint8* scripted_keys(int*) {
    static std::array<Uint8,SDL_NUM_SCANCODES> keys{}; keys.fill(0);
    if(record_mode){keys[SDL_SCANCODE_TAB]=sampled>=10 && sampled<20;return keys.data();}
    if(release_key) {release_key=false;return keys.data();}
    if(sampled>=100 && sampled<106) {keys[SDL_SCANCODE_TAB]=1;return keys.data();}
    constexpr unsigned thresholds[]{60,180,420};
    if(press_stage<3 && sampled>=thresholds[press_stage]) {
        keys[SDL_SCANCODE_TAB]=1;if(press_stage<3){times[press_stage+1]=time_now();marks[press_stage+1]=sampled;}
        ++press_stage;release_key=true;
    }
    return keys.data();
}
SDL_Window* scripted_focus(){return focus_window;}
app::MenuInput scripted_menu(SDL_Window* w,const app::Controller&){focus_window=w;return {};}
FrameInput scripted_input(SDL_Window*,const app::Controller&,const app::EnterKey&) {
    if(sampled==0)times[0]=time_now();
    ++sampled;FrameInput input;input.pause=!record_mode && (sampled==240 || sampled==280);return input;
}
class CheckedDriver : public app::FrameDriver {
public:
    using app::FrameDriver::FrameDriver;
    void speed(unsigned value) {audio_speed=value;app::FrameDriver::speed(value);}
    std::optional<std::string> power_cycle() {
        auto error=app::FrameDriver::power_cycle();shadow=std::make_unique<Machine>(shadow_rom,machine().memory().cartridge_ram());return error;
    }
    app::FrameOutput run(const FrameInput& input) {
        auto output=app::FrameDriver::run(input);
        if(output.audio_error)throw std::runtime_error(*output.audio_error);
        if(record_mode && output.playback)throw std::runtime_error("Recording used independent audio");
        if(output.playback)for(auto sample:output.playback->values)
            if(sample)++audible[audio_speed==4 ? 2 : audio_speed==2 ? 1 : 0];
        shadow->run_frame(input,shadow_routines);
        if(output.writes!=shadow->take_psg_writes() || output.samples!=shadow->take_samples())
            throw std::runtime_error("Frame PSG writes or samples differ from shadow");
        return output;
    }
};
int checked_upload(const Machine& machine,unsigned speed,SDL_Texture* texture,const SDL_Rect* rect,const void* data,int pitch) {
    if(record_mode && speed!=1)throw std::runtime_error("Recording speed changed");
    if(!sent_f5 && sampled>=440 && speed==2)saw_before_f5=true;
    if(sent_f5 && speed==1)saw_f5_reset=true;
    if(!record_mode && ((sampled>=100 && sampled<106) || (sampled>=130 && sampled<136)) && speed!=2)throw std::runtime_error("Held Tab after queued focus events changed speed");
    std::string differences;
    for(const auto& d:shadow->differences(machine))
        if(d.field!="memory.rom" && d.field!="memory.cartridge_ram_dirty") differences+=" "+d.field;
    if(!differences.empty())throw std::runtime_error("Machine differs from frame-by-frame shadow:"+differences);
    const auto* pixels=static_cast<const std::uint32_t*>(data);
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)
        if((speed==1 || x<234 || x>=254 || y<2 || y>=12) && pixels[y*256+x]!=app::rgb(machine.picture()[y*256+x]))
            throw std::runtime_error("Badge altered pixels outside its area");
    if(speed>1 && !got_badge[speed==2?0:1]) {
        got_badge[speed==2?0:1]=true;
        std::ofstream f(image_dir/(speed==2?"badge-2x.ppm":"badge-4x.ppm"),std::ios::binary);
        f<<"P6\n256 192\n255\n";
        for(unsigned i=0;i<256*192;++i){const char rgb[]{char(pixels[i]>>16),char(pixels[i]>>8),char(pixels[i])};f.write(rgb,3);}
    }
    return SDL_UpdateTexture(texture,rect,data,pitch);
}
}
// The scripted sessions pass their own argv and never reach the start screen.
#include "app/start_loop.h"
#include "app/windows_platform.h"
#define utf8_arguments(argc,argv) std::vector<std::string>(argv,argv+argc)
#define attach_parent_console()
#define run_launcher(display) std::optional<std::filesystem::path>()
#define main fast_app_main
#define FrameDriver CheckedDriver
#define SDL_PollEvent scripted_poll
#define SDL_GetKeyboardState scripted_keys
#define SDL_GetKeyboardFocus scripted_focus
#define sample_input scripted_input
#define sample_menu_input scripted_menu
#define SDL_UpdateTexture(t,r,p,pitch) checked_upload(driver.machine(),clock.speed(),t,r,p,pitch)
#include "../src/app/main.cpp"
#undef main
#undef utf8_arguments
#undef attach_parent_console
#undef run_launcher
#undef FrameDriver
#undef SDL_PollEvent
#undef SDL_GetKeyboardState
#undef SDL_GetKeyboardFocus
#undef sample_input
#undef sample_menu_input
#undef SDL_UpdateTexture
#define SDL_QueueAudio checked_queue
#include "../src/app/sdl_host.cpp"
#undef SDL_QueueAudio
int main(int argc,char** argv){try{
    if(argc!=3)return 1;if(!*argv[1] || !std::filesystem::exists(argv[1]))return 77;
    image_dir=argv[2];std::filesystem::create_directories(image_dir);
    const auto temp=std::filesystem::temp_directory_path()/("psmodern-speed-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp);SDL_setenv("APPDATA",temp.string().c_str(),1);
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    std::ifstream f(argv[1],std::ios::binary);auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
    shadow_rom=rom;shadow=std::make_unique<Machine>(rom);game::register_routines(shadow_routines,sha1(*rom));
    char* args[]{argv[0],argv[1]};if(fast_app_main(2,args))throw std::runtime_error("App session failed");
    for(int i=0;i<3;++i){const auto seconds=times[i+1]-times[i];const auto expected=(marks[i+1]-marks[i])*app::frame_seconds/(1u<<i);if(seconds<expected*.70 || seconds>expected*1.40)throw std::runtime_error("Unexpected speed interval: "+std::to_string(seconds));std::cout<<"interval "<<i<<": "<<seconds<<" seconds, "<<marks[i+1]-marks[i]<<" frames\n";}
    if(!got_badge[0] || !got_badge[1] || !sent_f5 || !saw_before_f5 || !saw_f5_reset)throw std::runtime_error("Missing badge or F5 reset");
    if(!audible[0] || !audible[1] || !audible[2])throw std::runtime_error("Missing continuous music at one speed");
    if(!queued_audible[0] || !queued_audible[1] || !queued_audible[2] || !removed_audio)
        throw std::runtime_error("Missing SDL music delivery or accelerated device recovery");
    record_mode=true;sampled=0;sent_quit=false;
    { app::SaveStore saved(temp/"PSModern",identify_rom(argv[1]));shadow=std::make_unique<Machine>(shadow_rom,saved.load()); }
    std::string record=(temp/"session.psrec").string();char flag[]="--record";char* record_args[]{argv[0],argv[1],flag,record.data()};
    if(fast_app_main(4,record_args))throw std::runtime_error("Recording session failed");
    test::remove_temporary_folder(temp);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
