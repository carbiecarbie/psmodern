// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "replay/recording.h"
#include "rom/rom.h"
#include "temporary_cleanup.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace psmodern;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void scenario(std::shared_ptr<const Memory::Rom> rom,RomVersion version,const std::filesystem::path& folder,
                     const std::filesystem::path& path,unsigned at,bool dungeon,bool animated_corner=false) {
    const auto recording=replay::read(path);
    app::FrameDriver d(rom,app::SaveStore(folder,version));
    check(!d.request_menu(),"startup accepted options");
    unsigned rejected=0;
    for(unsigned n=0;n<at;++n){
        const auto mode=d.machine().peek(0xc202);
        if(mode!=9 && mode!=11){check(!d.request_menu(),"non-exploration accepted options");++rejected;}
        auto out=d.run(recording.input_at(n));check(!out.save_error,"save failed");
    }
    check(rejected>100,"non-exploration coverage missing");
    d.speed(4);
    for(unsigned n=0;n<40;++n){const auto out=d.run({});check(!out.audio_error,"accelerated exploration sound failed");}
    bool requested=false;
    for(unsigned n=0;n<120 && !requested;++n){d.run({});requested=d.request_menu();}
    check(requested,"idle exploration did not admit options");
    const auto mode=d.machine().peek(0xc202);
    check(mode==(dungeon?11:9),"wrong exploration scene");
    const auto initial=d.machine().picture();
    Machine uninterrupted(d.machine());
    const auto ram=d.machine().memory().ram();
    d.run({});check(d.menu_active(),"native menu did not open in immediate frame");d.speed(1);
    unsigned writes=0;std::size_t samples=0;unsigned frames_with_writes=0;bool nonzero=false;
    std::array<std::vector<std::uint8_t>,4> sound_blocks;
    bool presentation_nonzero=false;
    for(unsigned n=0;n<240;++n){FrameInput hostile{};hostile.pause=true;hostile.reset=true;hostile.pads[0].right=true;
        const auto out=d.run(hostile);writes+=out.writes.size();samples+=out.samples.values.size();frames_with_writes+=!out.writes.empty();
        check(out.playback && !out.audio_error,"options lost the independent sound timeline");
        for(auto value:out.playback->values)presentation_nonzero|=value!=0;
        for(const auto& w:out.writes)sound_blocks[n/60].push_back(w.byte);
        for(auto value:out.samples.values)nonzero|=value!=0;
        check(d.menu_active(),"menu lost its native call");check(!d.request_menu(),"nested request accepted");}
    check(presentation_nonzero && writes>100 && samples>100000 && frames_with_writes>60 && nonzero &&
          (sound_blocks[0]!=sound_blocks[1] || sound_blocks[1]!=sound_blocks[2]),"native music did not continue");
    for(unsigned n=0x300;n<0x800;++n)check(d.machine().memory().ram()[n]==ram[n],"party or position changed while menu was open");
    d.close_menu();unsigned close_frames=0;while(d.menu_active()&&close_frames++<100)d.run({});
    check(!d.menu_active(),"native menu did not return");
    d.speed(4);
    for(unsigned n=0;n<20;++n){const auto out=d.run({});check(out.playback && !out.audio_error,"options close lost accelerated sound");}
    check(d.machine().peek(0xc202)==mode,"cancel leaked into game action");
    check(d.machine().peek(0xc204)==0 && d.machine().peek(0xc205)==0,"cancel left a button pressed");
    for(unsigned n=0;n<1+240+close_frames+20;++n){uninterrupted.run_frame({});uninterrupted.take_samples();uninterrupted.take_psg_writes();}
    for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x) {
        const auto i=y*256+x;const auto actual=d.machine().picture()[i];
        if(actual==initial[i] || actual==uninterrupted.picture()[i])continue;
        // The BR world checkpoint contains four visible rows of an animated
        // water tile at the top edge. All other pixels must be restored.
        const auto water=[](unsigned c){return c==0 || c==3 || c==37 || c==42 || c==63;};
        check(animated_corner && x>=8 && x<16 && y<4 && water(actual) && water(initial[i]),
              "native cleanup did not restore the exploration picture");
    }
    auto& memory=d.machine().memory();
    for(const auto address:{0xc204,0xc205,0xc212}) {
        const auto saved=d.machine().peek(address);memory.write(address,1);
        check(!d.request_menu(),"held input or pause accepted options");memory.write(address,saved);
    }
    bool reopened=d.request_menu();for(unsigned n=0;n<120&&!reopened;++n){d.run({});reopened=d.request_menu();}
    check(reopened,"options could not reopen after cleanup");d.run({});check(d.menu_active(),"reopen failed");
    d.close_menu();for(unsigned n=0;n<100&&d.menu_active();++n)d.run({});check(!d.menu_active(),"second close failed");
    FrameInput game_button{};game_button.pads[0].two=true;bool delivered=false;
    for(unsigned n=0;n<12;++n){d.run(game_button);delivered|=(d.machine().peek(0xc204)&0x20)!=0;}
    check(delivered,"original button 2 did not reach the game after closing");
    if(dungeon) {
        bool saw_menu_wait=false;
        for(unsigned n=0;n<150;++n) {
            d.run({});auto& machine=d.machine();const auto& cpu=machine.cpu();const auto pc=cpu.pc.uint16_value,sp=cpu.sp.uint16_value;
            if(pc>=0x55 && pc<=0x5b) {
                const auto ret=static_cast<unsigned>(machine.peek(sp)|(machine.peek(static_cast<std::uint16_t>(sp+1))<<8));
                if(ret!=0x0f48 && machine.peek(0xc202)==11) {saw_menu_wait=true;check(!d.request_menu(),"native menu accepted options");}
            }
        }
        check(saw_menu_wait,"original dungeon menu wait not observed");
        FrameInput cancel{};cancel.pads[0].one=true;
        for(unsigned n=0;n<8;++n)d.run(cancel);
        for(unsigned n=0;n<100;++n){d.run({});check(!d.menu_active(),"rejected request opened later");}
        const auto direction=d.machine().peek(0xc30a);FrameInput turn{};turn.pads[0].left=true;
        for(unsigned n=0;n<20;++n)d.run(turn);
        check(d.machine().peek(0xc30a)!=direction,"dungeon controls did not resume after menu");
    }
    std::cout<<(dungeon?"dungeon":"map")<<": "<<writes<<" PSG writes during 240 frames; restored and reopened.\n";
}
int main(int argc,char**argv){try{
    if(argc!=5)return 1;if(!*argv[1]||!std::filesystem::is_regular_file(argv[1]))return 77;
    std::ifstream f(argv[1],std::ios::binary);auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
    const auto version=identify_rom(argv[1]);const bool br=std::string(argv[4])=="br";
    const auto root=std::filesystem::temp_directory_path()/("psmodern-native-options-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"city");std::filesystem::create_directories(root/"world");std::filesystem::create_directories(root/"dungeon");
    scenario(rom,version,root/"city",argv[2],3400,false);
    scenario(rom,version,root/"world",argv[2],br?11500:9500,false,br);
    scenario(rom,version,root/"dungeon",argv[3],7000,true);
    test::remove_temporary_folder(root);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
