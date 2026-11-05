// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include "replay/recording.h"
#include "rom/sha1.h"
#include <fstream>
#include <chrono>
#include <iostream>
#include <stdexcept>
static unsigned sampled=0;
static bool sent_f5=false;
static bool sent_quit=false;
static std::unique_ptr<psmodern::Machine> shadow;
static std::vector<std::string> pictures;
static int scripted_poll(SDL_Event* e) {
    if(sampled>=32&&!sent_quit){sent_quit=true;e->type=SDL_QUIT;return 1;}
    if(sampled>=4&&!sent_f5){sent_f5=true;e->type=SDL_KEYDOWN;e->key.repeat=0;e->key.keysym.sym=SDLK_F5;return 1;}
    return 0;
}
static psmodern::FrameInput scripted_input(SDL_Window* w,const psmodern::app::Controller&) {
    if(std::string(SDL_GetWindowTitle(w)).find("F5 disabled")==std::string::npos)throw std::runtime_error("Missing recording title");
    psmodern::FrameInput input{};input.pads[0].one=sampled>=4&&sampled<8;input.pads[1].up=sampled==12;input.pause=sampled==20;
    ++sampled;shadow->run_frame(input);shadow->take_psg_writes();shadow->take_samples();
    pictures.push_back(psmodern::sha1(shadow->picture()));return input;
}
static int checked_upload(SDL_Texture* t,const SDL_Rect* r,const void* pixels,int pitch) {
    const auto* rgb=static_cast<const std::uint32_t*>(pixels);
    for(std::size_t i=0;i<shadow->picture().size();++i)if(rgb[i]!=psmodern::app::rgb(shadow->picture()[i]))throw std::runtime_error("App picture differs from replay");
    return SDL_UpdateTexture(t,r,pixels,pitch);
}
#define main recorded_app_main
#define SDL_PollEvent scripted_poll
#define sample_input scripted_input
#define SDL_UpdateTexture checked_upload
#include "../src/app/main.cpp"
#undef main
#undef SDL_PollEvent
#undef sample_input
#undef SDL_UpdateTexture
int main(int argc,char** argv) {
    if(argc!=4)return 1;
    if(!*argv[1]||!std::filesystem::exists(argv[1]))return 77;
    try {
        const auto out=std::filesystem::path(argv[2])/std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());std::filesystem::create_directories(out);
        SDL_setenv("APPDATA",out.string().c_str(),1);SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
        std::ifstream f(argv[1],std::ios::binary);auto rom=std::make_shared<psmodern::Memory::Rom>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
        shadow=std::make_unique<psmodern::Machine>(rom);
        std::string rec=(out/"session.psrec").string();char flag[]="--record";char* args[]={argv[0],argv[1],flag,rec.data()};
        if(recorded_app_main(4,args)!=0)return 1;
        const auto recording=psmodern::replay::read(rec);if(recording.frames!=sampled||!sent_f5)throw std::runtime_error("Wrong recorded frame count");
        psmodern::Machine replay(rom);
        std::ofstream hashes(out/"app-pictures.txt",std::ios::binary);
        for(unsigned n=0;n<sampled;++n){replay.run_frame(recording.input_at(n));replay.take_psg_writes();replay.take_samples();if(psmodern::sha1(replay.picture())!=pictures[n])throw std::runtime_error("Recorded frames differ");hashes<<n+1<<' '<<pictures[n]<<'\n';}
        std::cout<<"Scripted SDL record session: "<<sampled<<" identical frames; F5 disabled\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
