// SPDX-License-Identifier: GPL-3.0-or-later
// A dialogue-only setup leaves a stale reward. A pending settings change must
// apply at the following dispatcher and the next battle must prepare a fresh
// reward at the new rate. Starts at a real recorded map-battle entry.
#include "app/frame_driver.h"
#include "game/routines.h"
#include "replay/recording.h"
#include "rom/sha1.h"
#include "temporary_cleanup.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace psmodern;
namespace {
void check(bool ok,const std::string& what){if(!ok) throw std::runtime_error(what);}
unsigned word(const Machine& m,std::uint16_t a){return m.memory().read(a)|(m.memory().read(a+1)<<8);}
void put(Machine& m,std::uint16_t a,unsigned v){m.memory().write(a,static_cast<std::uint8_t>(v));m.memory().write(a+1,static_cast<std::uint8_t>(v>>8));}
std::uint64_t calls(RoutineRegistry& r,RoutineEntry entry){const auto* p=r.find(entry);check(p!=nullptr,"missing registered entry");return p->counters.cpp_calls+p->counters.fallback_calls;}
}
int main(int argc,char** argv){try {
    if(argc!=3)throw std::runtime_error("Expected ROM and matching first-battle recording");
    if(!*argv[1] || !std::filesystem::exists(argv[1]) || !std::filesystem::exists(argv[2]))return 77;
    std::ifstream f(argv[1],std::ios::binary);
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
    auto recording=replay::read(argv[2]);check(sha1(*rom)==recording.rom_sha1,"ROM and recording differ");
    check(sha1(Memory::CartridgeRam{})==recording.cart_ram_sha1,"recording needs nonzero cartridge RAM");
    Machine machine(rom);RoutineRegistry routines;game::register_routines(routines,recording.rom_sha1);
    std::optional<Machine> before;std::uint64_t at_frame=0;
    Machine::Hook find=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t>{
        if(pc==0x3c1a && map==Memory::Mapping{Memory::Kind::rom,0} && !before)before=machine;return {};
    };
    for(std::uint64_t frame=0;frame<recording.frames && !before;++frame){
        machine.run_frame(recording.input_at(frame),routines,find);machine.take_samples();machine.take_psg_writes();at_frame=frame;
    }
    check(before.has_value(),"recording did not reach the map battle setup");
    const auto enemy=before->memory().read(0xc2e6);
    const auto root=std::filesystem::temp_directory_path()/("psmodern-stale-reward-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    {
        const auto version=identify_digest(recording.rom_sha1);
        app::FrameDriver original(rom,app::SaveStore(root/"original",version));
        app::FrameDriver changed(rom,app::SaveStore(root/"changed",version));
        original.machine()=*before;changed.machine()=*before;
        // 01:55A9 prepares a portrait and returns through dialogue, without
        // entering a battle. The captured interaction handler's return stack
        // stays intact, so the original dispatcher resumes afterwards.
        original.machine().cpu().pc.uint16_value=0x55a9;changed.machine().cpu().pc.uint16_value=0x55a9;
        unsigned frames=0;
        for(;frames<1200 && calls(original.routines(),game::battle_money_store)==0;++frames){
            const auto a=original.run({}),b=changed.run({});check(!a.save_error && !b.save_error,"save error during dialogue setup");
            check(original.machine().equivalent(changed.machine()),"dialogue setup diverged before selection");
        }
        check(calls(original.routines(),game::battle_money_store)==1,"dialogue-only setup did not produce a reward");
        const unsigned stale=word(original.machine(),0xc2dd);
        check(stale==word(changed.machine(),0xc2dd),"dialogue reward differs before selection");
        // Restore the actual recorded encounter selection, simulating a later
        // encounter, and mark the stale value so replacement is observable
        // even when both enemies happen to have the same money reward.
        for(auto* d:{&original,&changed}){d->machine().memory().write(0xc2e6,enemy);put(d->machine(),0xc2dd,0x4321);}
        check(!changed.choose_options({100,400}),"could not queue settings");
        check(changed.options_pending() && changed.effective_options()==game::Options{},"selection applied before dispatcher");
        bool activated=false,second_setup=false;
        unsigned base_money=0,base_exp=0;
        for(unsigned n=0;n<5000 && !second_setup;++n){
            FrameInput input{};input.pads[0].one=n%30<4;
            const auto a=original.run(input),b=changed.run(input);check(!a.save_error && !b.save_error,"save error after selection");
            if(!changed.options_pending())activated=true;
            if(calls(original.routines(),game::battle_money_store)>=2){
                second_setup=true;check(activated && changed.effective_options()==game::Options{100,400},"fresh setup did not use the activated setting");
                check(calls(changed.routines(),game::battle_money_store)>=1,"new registry missed the fresh setup store");
                base_money=word(original.machine(),0xc2dd);base_exp=word(original.machine(),0xc2d0);
                check(base_money && base_money!=0x4321 && word(changed.machine(),0xc2dd)==std::min(65535u,base_money*4),"fresh setup did not replace stale money at the new rate");
            }
        }
        check(second_setup,"no later battle setup after dialogue");
        bool original_awarded=false,changed_awarded=false;unsigned displayed=0;
        const auto initial_money=word(original.machine(),0xc4e0);
        for(unsigned n=0;n<8000 && !(original_awarded && changed_awarded);++n){
            FrameInput input=recording.input_at(at_frame+n);
            const auto a=original.run(input),b=changed.run(input);check(!a.save_error && !b.save_error,"save error in later battle");
            if(!original_awarded && calls(original.routines(),game::experience_award_unit))original_awarded=true;
            if(!changed_awarded && calls(changed.routines(),game::experience_award_unit)){
                changed_awarded=true;displayed=word(changed.machine(),0xc2c5);
                base_exp=word(changed.machine(),0xc2d0);
            }
        }
        check(original_awarded && changed_awarded,"later battle did not reach both experience awards");
        check(base_exp!=0,"later battle did not provide a meaningful experience reward");
        check(displayed==std::min(65535u,base_exp*4),"later battle experience display used a different rate");
        bool original_paid=false,changed_paid=false;
        for(unsigned n=0;n<4000 && !(original_paid && changed_paid);++n){
            FrameInput input{};input.pads[0].one=n%30<4;
            const auto a=original.run(input),b=changed.run(input);check(!a.save_error && !b.save_error,"save error in battle chest");
            original_paid=calls(original.routines(),game::money_add_unit)>0;
            changed_paid=calls(changed.routines(),game::money_add_unit)>0;
        }
        check(original_paid && changed_paid,"later battle did not award its money");
        check(word(original.machine(),0xc4e0)==std::min(65535u,initial_money+base_money),"original battle money was wrong");
        check(word(changed.machine(),0xc4e0)==std::min(65535u,initial_money+std::min(65535u,base_money*4)),"later battle money used a different rate");
        std::cout<<"Dialogue setup reward "<<stale<<", fresh battle money "<<base_money<<" -> "<<base_money*4
                 <<", experience "<<base_exp<<" -> "<<displayed<<"; recorded frame "<<at_frame+1<<".\n";
    }
    test::remove_temporary_folder(root);return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
