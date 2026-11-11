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
SDL_Window* focused_window=nullptr;
SDL_Window* scripted_focus(){return focused_window;}
const Uint8* scripted_keys(int*) {
    static std::array<Uint8,SDL_NUM_SCANCODES> keys{};keys.fill(0);
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
};
int scripted_poll(SDL_Event* event){
    if(!sent_quit && ((mode_under_test==1 && sampled>=35) || (mode_under_test!=1 && was_active && tested && !tested->menu_active())))
        {sent_quit=true;event->type=SDL_QUIT;return 1;}
    return 0;
}
FrameInput scripted_input(SDL_Window*,const app::Controller&,const app::EnterKey&){++sampled;return {};}
app::MenuInput scripted_menu(SDL_Window* w,const app::Controller&){
    focused_window=w;
    using app::MenuInput;
    if(sampled<12 || speed_stage<2 || release_speed)return {};
    if(!toggle_sent){toggle_sent=true;return {.toggle=true};}
    if(!tested || !tested->menu_active())return {};
    was_active=true;
    switch(stage++){
        case 0:return {};
        case 1:return mode_under_test==0 ? MenuInput{.right=true} : MenuInput{};
        case 2:return {};
        case 3:return {.accept=true};
        default:return {};
    }
}
int checked_upload(const Machine& machine,unsigned speed,bool visible,bool active,const Vdp::Picture& background,SDL_Texture* texture,const SDL_Rect* rect,const void* pixels,int pitch){
    const auto* rendered=static_cast<const std::uint32_t*>(pixels);
    if(visible){
        if(speed!=1)throw std::runtime_error("Options ran accelerated");
        if(mode_under_test==0 && !saved_menu){
            saved_menu=true;std::ofstream f(image_dir/"options-4x-session.ppm",std::ios::binary);f<<"P6\n256 192\n255\n";
            for(unsigned i=0;i<256*192;++i){const char rgb[]{char(rendered[i]>>16),char(rendered[i]>>8),char(rendered[i])};f.write(rgb,3);}
        }
        if(machine.cycles()>last_modal_cycle){last_modal_cycle=machine.cycles();++modal_frames;}
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)
            if((x<24||x>=232||y<40||y>=160) && rendered[y*256+x]!=app::rgb(background[y*256+x]))
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
            if(rendered[i]!=app::rgb(machine.picture()[i]))throw std::runtime_error("Closed options changed the game picture");
        }
    }
    return SDL_UpdateTexture(texture,rect,pixels,pitch);
}
}
// The scripted sessions pass their own argv and never reach the start screen.
#include "app/start_loop.h"
#include "app/windows_platform.h"
#define utf8_arguments(argc,argv) std::vector<std::string>(argv,argv+argc)
#define attach_parent_console()
#define run_launcher(display) std::optional<std::filesystem::path>()
#define main options_app_main
#define FrameDriver SeededDriver
#define SDL_PollEvent scripted_poll
#define SDL_GetKeyboardFocus scripted_focus
#define SDL_GetKeyboardState scripted_keys
#define sample_input scripted_input
#define sample_menu_input scripted_menu
#define SDL_UpdateTexture(t,r,p,pitch) checked_upload(driver.machine(),clock.speed(),menu.visible(),driver.menu_active(),menu_background,t,r,p,pitch)
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
#undef SDL_UpdateTexture
int main(int argc,char**argv){try{
    if(argc!=3)return 1;if(!*argv[1]||!std::filesystem::is_regular_file(argv[1]))return 77;
    recording_path=argv[2];const auto root=std::filesystem::temp_directory_path()/("psmodern-options-session-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);SDL_setenv("APPDATA",root.string().c_str(),1);SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    const auto version=identify_rom(argv[1]);
    image_dir=std::filesystem::current_path()/(version==RomVersion::us_rev_a ? "options-us" : "options-br");std::filesystem::create_directories(image_dir);
    for(mode_under_test=0;mode_under_test<3;++mode_under_test){
        sampled=stage=modal_frames=speed_stage=0;release_speed=false;sent_quit=toggle_sent=was_active=restored_speed=saved_menu=saved_game=false;last_modal_cycle=0;tested=nullptr;
        char rate[]="--encounter-rate",value[]="200",raw[]="--no-routines",record[]="--record";
        std::string recording=(root/"session.psrec").string();
        char* normal[]{argv[0],argv[1],rate,value};char* recording_args[]{argv[0],argv[1],record,recording.data()};char* raw_args[]{argv[0],argv[1],raw};
        const auto result=options_app_main(mode_under_test==2?3:4,mode_under_test==0?normal:mode_under_test==1?recording_args:raw_args);
        if(result || (mode_under_test==1 ? was_active : !was_active || modal_frames==0 || !restored_speed))throw std::runtime_error("Scripted options app session failed");
        app::SaveStore store(root/"PSModern",version);
        if(store.load_preferences(store.load())!=game::Options{0,100})throw std::runtime_error("App Apply or restricted session changed preferences incorrectly");
    }
    test::remove_temporary_folder(root);std::cout<<"SDL options sessions: Apply, recording ignored, raw-Z80 readonly, live sound and backdrop passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
