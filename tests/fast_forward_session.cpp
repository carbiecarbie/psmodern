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
// Milestone 10: each session runs on the narrow and on the wide canvas. The
// driver has no wide picture here, so the wide canvas is the pillarbox.
bool test_wide=false;
std::vector<std::uint32_t> last_centre;
unsigned presents=0,checked_presents=0,band_uploads=0;
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
    if(!rect || rect->x!=(test_wide ? 44 : 0) || rect->y!=0 || rect->w!=256 || rect->h!=192 || pitch!=256*4)
        throw std::runtime_error("The centre upload is not the canvas centre");
    const auto* pixels=static_cast<const std::uint32_t*>(data);
    last_centre.assign(pixels,pixels+256*192);
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)
        if((speed==1 || x<234 || x>=254 || y<2 || y>=12) && pixels[y*256+x]!=app::rgb(machine.picture()[y*256+x]))
            throw std::runtime_error("Badge altered pixels outside its area");
    if(speed>1 && !got_badge[speed==2?0:1]) {
        got_badge[speed==2?0:1]=true;
        std::ofstream f(image_dir/(std::string(speed==2?"badge-2x":"badge-4x")+(test_wide ? "-wide.ppm" : ".ppm")),std::ios::binary);
        f<<"P6\n256 192\n255\n";
        for(unsigned i=0;i<256*192;++i){const char rgb[]{char(pixels[i]>>16),char(pixels[i]>>8),char(pixels[i])};f.write(rgb,3);}
    }
    return SDL_UpdateTexture(texture,rect,data,pitch);
}
void checked_bands(app::Display& display,const app::WidePicture* wide) {
    if(display.wide_canvas!=test_wide)throw std::runtime_error("The session canvas is not the one applied");
    if(wide)throw std::runtime_error("A wide picture without the widescreen option");
    ++band_uploads;app::upload_bands(display,wide);
}
// Every 32nd presented frame is read back from the software renderer: the
// canvas at the viewport's integer scale, black around it and, on the wide
// canvas, black bands beside the uploaded centre. The first one also checks
// the window: 768 x 576 narrow; wide, 3x (1032 x 576) does not fit the dummy
// display's 1024 x 768, so apply_canvas lowers it to 2x (688 x 384).
void checked_render_present(SDL_Renderer* renderer) {
    if(presents++%32==0 && !last_centre.empty()) {
        int w=0,h=0;if(SDL_GetRendererOutputSize(renderer,&w,&h)!=0)throw std::runtime_error("Reading the output size");
        if(checked_presents==0 && (test_wide ? w!=688 || h!=384 : w!=768 || h!=576))
            throw std::runtime_error("Unexpected window for the canvas: "+std::to_string(w)+"x"+std::to_string(h));
        const auto v=app::viewport(w,h,app::canvas_width(test_wide));
        std::vector<std::uint32_t> out(std::size_t(w)*h);
        if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,out.data(),w*4)!=0)throw std::runtime_error("Reading the presented frame");
        const int left=app::canvas_centre_x(test_wide);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            std::uint32_t expected=0xff000000;
            if(x>=v.x && x<v.x+v.width && y>=v.y && y<v.y+v.height) {
                const int cx=(x-v.x)/v.scale-left,cy=(y-v.y)/v.scale;
                if(cx>=0 && cx<256)expected=last_centre[cy*256+cx];
            }
            if((out[std::size_t(y)*w+x]^expected)&0xffffff)throw std::runtime_error("Presented frame differs from the canvas");
        }
        ++checked_presents;
    }
    SDL_RenderPresent(renderer);
}
void reset_script(bool recording) {
    sampled=press_stage=focus_event=quick_event=options_event=0;
    std::fill(std::begin(marks),std::end(marks),0u);std::fill(std::begin(times),std::end(times),0.0);
    got_badge[0]=got_badge[1]=sent_quit=release_key=sent_f5=saw_f5_reset=saw_before_f5=false;record_mode=recording;
    audio_speed=1;std::fill(std::begin(audible),std::end(audible),0u);std::fill(std::begin(queued_audible),std::end(queued_audible),0u);
    audio_device=0;removed_audio=false;last_centre.clear();presents=checked_presents=band_uploads=0;
}
}
// The scripted sessions pass their own argv and never reach the start screen.
#include "app/start_loop.h"
#include "app/windows_platform.h"
#define utf8_arguments(argc,argv) std::vector<std::string>(argv,argv+argc)
#define attach_parent_console()
#define run_launcher(...) std::optional<psmodern::app::SessionRoms>()
#define main fast_app_main
#define FrameDriver CheckedDriver
#define SDL_PollEvent scripted_poll
#define SDL_GetKeyboardState scripted_keys
#define SDL_GetKeyboardFocus scripted_focus
#define sample_input scripted_input
#define sample_menu_input scripted_menu
#define SDL_UpdateTexture(t,r,p,pitch) checked_upload(driver.machine(),clock.speed(),t,r,p,pitch)
#define apply_canvas(d,w) ((void)(w),psmodern::app::apply_canvas(d,test_wide))
#define upload_bands(d,w) checked_bands(d,w)
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
#undef apply_canvas
#undef upload_bands
#define SDL_QueueAudio checked_queue
#define SDL_RenderPresent checked_render_present
#include "../src/app/sdl_host.cpp"
#undef SDL_QueueAudio
#undef SDL_RenderPresent
int main(int argc,char** argv){try{
    if(argc!=3)return 1;if(!*argv[1] || !std::filesystem::exists(argv[1]))return 77;
    image_dir=argv[2];std::filesystem::create_directories(image_dir);
    const auto temp=std::filesystem::temp_directory_path()/("psmodern-speed-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp);SDL_setenv("APPDATA",temp.string().c_str(),1);
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    std::ifstream f(argv[1],std::ios::binary);auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
    shadow_rom=rom;game::register_routines(shadow_routines,sha1(*rom));
    // The shadow starts from the save the app will load (none before the first session).
    auto fresh_shadow=[&]() { auto saved=app::SaveStore::shared(temp/"PSModern");shadow=std::make_unique<Machine>(shadow_rom,saved.load()); };
    auto canvas_checked=[&](const char* session) {
        if(band_uploads==0 || checked_presents==0)throw std::runtime_error(std::string(session)+": no band upload or presented frame checked");
        std::cout<<session<<(test_wide ? ", wide canvas: " : ", narrow canvas: ")<<checked_presents<<" presented frames checked\n";
    };
    for(const bool wide:{false,true}) {
        test_wide=wide;reset_script(false);
        if(wide)fresh_shadow(); else shadow=std::make_unique<Machine>(rom);
        char* args[]{argv[0],argv[1]};if(fast_app_main(2,args))throw std::runtime_error("App session failed");
        for(int i=0;i<3;++i){const auto seconds=times[i+1]-times[i];const auto expected=(marks[i+1]-marks[i])*app::frame_seconds/(1u<<i);if(seconds<expected*.70 || seconds>expected*1.40)throw std::runtime_error("Unexpected speed interval: "+std::to_string(seconds));std::cout<<"interval "<<i<<": "<<seconds<<" seconds, "<<marks[i+1]-marks[i]<<" frames\n";}
        if(!got_badge[0] || !got_badge[1] || !sent_f5 || !saw_before_f5 || !saw_f5_reset)throw std::runtime_error("Missing badge or F5 reset");
        if(!audible[0] || !audible[1] || !audible[2])throw std::runtime_error("Missing continuous music at one speed");
        if(!queued_audible[0] || !queued_audible[1] || !queued_audible[2] || !removed_audio)
            throw std::runtime_error("Missing SDL music delivery or accelerated device recovery");
        canvas_checked("Speed session");
    }
    for(const bool wide:{false,true}) {
        test_wide=wide;reset_script(true);fresh_shadow();
        std::string record=(temp/(wide ? "session-wide.psrec" : "session.psrec")).string();char flag[]="--record";char* record_args[]{argv[0],argv[1],flag,record.data()};
        if(fast_app_main(4,record_args))throw std::runtime_error("Recording session failed");
        canvas_checked("Recording session");
    }
    test::remove_temporary_folder(temp);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
