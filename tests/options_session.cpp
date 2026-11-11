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
unsigned sampled=0,stage=0,modal_frames=0,mode_under_test=0;
bool sent_quit=false,toggle_sent=false,was_active=false;
std::uint64_t last_modal_cycle=0;
app::FrameDriver* tested=nullptr;
std::filesystem::path recording_path;
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
app::MenuInput scripted_menu(SDL_Window*,const app::Controller&){
    using app::MenuInput;
    if(sampled<5)return {};
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
int checked_upload(const Machine& machine,bool visible,bool active,const Vdp::Picture& background,SDL_Texture* texture,const SDL_Rect* rect,const void* pixels,int pitch){
    const auto* rendered=static_cast<const std::uint32_t*>(pixels);
    if(visible){
        if(machine.cycles()>last_modal_cycle){last_modal_cycle=machine.cycles();++modal_frames;}
        for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x)
            if((x<24||x>=232||y<40||y>=160) && rendered[y*256+x]!=app::rgb(background[y*256+x]))
                throw std::runtime_error("Options changed pixels outside its window");
    }else if(!active){
        for(std::size_t i=0;i<machine.picture().size();++i)
            if(rendered[i]!=app::rgb(machine.picture()[i]))throw std::runtime_error("Closed options changed the game picture");
    }
    return SDL_UpdateTexture(texture,rect,pixels,pitch);
}
}
#define main options_app_main
#define FrameDriver SeededDriver
#define SDL_PollEvent scripted_poll
#define sample_input scripted_input
#define sample_menu_input scripted_menu
#define SDL_UpdateTexture(t,r,p,pitch) checked_upload(driver.machine(),menu.visible(),driver.menu_active(),menu_background,t,r,p,pitch)
#include "../src/app/main.cpp"
#undef main
#undef FrameDriver
#undef SDL_PollEvent
#undef sample_input
#undef sample_menu_input
#undef SDL_UpdateTexture
int main(int argc,char**argv){try{
    if(argc!=3)return 1;if(!*argv[1]||!std::filesystem::is_regular_file(argv[1]))return 77;
    recording_path=argv[2];const auto root=std::filesystem::temp_directory_path()/("psmodern-options-session-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);SDL_setenv("APPDATA",root.string().c_str(),1);SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    const auto version=identify_rom(argv[1]);
    for(mode_under_test=0;mode_under_test<3;++mode_under_test){
        sampled=stage=modal_frames=0;sent_quit=toggle_sent=was_active=false;last_modal_cycle=0;tested=nullptr;
        char rate[]="--encounter-rate",value[]="200",raw[]="--no-routines",record[]="--record";
        std::string recording=(root/"session.psrec").string();
        char* normal[]{argv[0],argv[1],rate,value};char* recording_args[]{argv[0],argv[1],record,recording.data()};char* raw_args[]{argv[0],argv[1],raw};
        const auto result=options_app_main(mode_under_test==2?3:4,mode_under_test==0?normal:mode_under_test==1?recording_args:raw_args);
        if(result || (mode_under_test==1 ? was_active : !was_active || modal_frames==0))throw std::runtime_error("Scripted options app session failed");
        app::SaveStore store(root/"PSModern",version);
        if(store.load_preferences(store.load())!=game::Options{0,100})throw std::runtime_error("App Apply or restricted session changed preferences incorrectly");
    }
    test::remove_temporary_folder(root);std::cout<<"SDL options sessions: Apply, recording ignored, raw-Z80 readonly, live sound and backdrop passed.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
