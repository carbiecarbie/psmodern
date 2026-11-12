// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/routines.h"
#include "machine/machine.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace psmodern;
namespace {
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
// Every registered entry must be one the game headers name; the routines
// already rewritten must be present. No count is fixed, so later parts only
// add their names.
bool declared(RoutineEntry entry) {
    auto in=[&](const auto& list) { return std::find(list.begin(),list.end(),entry)!=list.end(); };
    auto dungeon=[&](const auto& list) { return std::any_of(list.begin(),list.end(),[&](const auto& s) { return s.entry==entry; }); };
    return entry==game::controller_poll_entry || entry==game::random_entry || entry==game::money_add_unit ||
        in(game::encounter_entries) || in(game::experience_entries) || in(game::battle_setup_entries) ||
        dungeon(game::dungeon::cell_segments) || dungeon(game::dungeon::movement_segments) || dungeon(game::dungeon::object_segments);
}
void catalog() {
    RoutineRegistry unknown;
    check(!game::register_routines(unknown,"authored fixture") && unknown.empty(),"unknown image gets no game routines");
    const game::Options variants[]{{},{0,200},{200,400},{50,400}};
    for(auto digest:{"07fcf297be4f4c9d92cd3f119a7ac48467e06838","fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd"})
        for(const auto& options:variants) {
            RoutineRegistry r;
            check(game::register_routines(r,digest,options),"both versions get game routines");
            check(r.find(game::controller_poll_entry)!=nullptr,"controller entry exists");
            const auto* random=r.find(game::random_entry);
            check(random!=nullptr && random->spec.unit==game::random_entry,"random-number entry exists");
            check(r.find(game::money_add_unit)!=nullptr,"shared money entry exists");
            for(const auto entry:game::battle_setup_entries)
                check(r.find(entry)!=nullptr && r.find(entry)->spec.unit==game::battle_setup_unit,
                      "battle-setup segment exists and belongs to its unit");
            // The dungeon cell readers (milestone 7); the movement and object
            // segments, once registered, belong to the units their lists name.
            for(const auto& [entry,unit]:game::dungeon::cell_segments)
                check(r.find(entry)!=nullptr && r.find(entry)->spec.unit==unit,"dungeon cell reader exists and is its own unit");
            auto unit_ok=[&](const auto& segments) {
                for(const auto& [entry,unit]:segments) if(const auto* found=r.find(entry)) check(found->spec.unit==unit,"dungeon segment belongs to its listed unit");
            };
            unit_ok(game::dungeon::movement_segments); unit_ok(game::dungeon::object_segments);
            for(const auto& [entry,routine]:r.entries()) check(declared(entry),"registered entry is not declared by name");
        }
}
// Option ranges and their validation, shared with the command line.
void options() {
    using game::Options;
    check(game::original_mode(Options{}) && Options{}.encounter_percent==100 && Options{}.experience_percent==100,
          "options default to the original");
    const auto& encounters=game::encounter_percent_choices; const auto& experience=game::experience_percent_choices;
    check(encounters.count==4 && encounters.values[0]==0 && encounters.values[1]==50 && encounters.values[2]==100 && encounters.values[3]==200 &&
          experience.count==3 && experience.values[0]==100 && experience.values[1]==200 && experience.values[2]==400 &&
          encounters.original==100 && experience.original==100,"allowed option values");
    check(encounters.text()=="0, 50, 100 or 200" && experience.text()=="100, 200 or 400","allowed values as text");
    check(game::switch_like_encounter_percent==50 && game::switch_like_experience_percent==400 &&
          encounters.contains(game::switch_like_encounter_percent) && experience.contains(game::switch_like_experience_percent),
          "the Switch-like values are allowed");
    auto refused=[](Options o) {
        try { game::validate(o); } catch(const std::invalid_argument&) { return true; }
        return false;
    };
    for(unsigned e:{0u,50u,100u,200u}) for(unsigned x:{100u,200u,400u}) check(!refused(Options{e,x}),"every allowed combination is accepted");
    for(Options o:{Options{25,100},Options{99,100},Options{101,100},Options{150,100},Options{400,100},Options{401,100},Options{100,0},Options{100,25},
                   Options{100,50},Options{100,99},Options{100,101},Options{100,300},Options{100,1000},Options{100,1001}})
        check(refused(o),"values that are not allowed are refused");
    try { game::validate(Options{400,100}); check(false,"encounter 400 validated"); }
    catch(const std::invalid_argument& e) { check(std::string(e.what()).find("must be one of 0, 50, 100 or 200 percent, got 400")!=std::string::npos,"encounter message lists the values"); }
    try { game::validate(Options{100,1000}); check(false,"experience 1000 validated"); }
    catch(const std::invalid_argument& e) { check(std::string(e.what()).find("must be one of 100, 200 or 400 percent, got 1000")!=std::string::npos,"experience message lists the values"); }
    RoutineRegistry r;
    try { (void)game::register_routines(r,"07fcf297be4f4c9d92cd3f119a7ac48467e06838",Options{100,0}); check(false,"invalid options registered"); }
    catch(const std::invalid_argument&) { check(r.empty(),"invalid options register nothing"); }
}
void compare(const std::shared_ptr<const Memory::Rom>& rom,RoutineRegistry& registry,unsigned current,unsigned previous,unsigned mode=0) {
    Machine native(rom);
    auto pad=[](unsigned bits) { return Pad{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),bool(bits&16),bool(bits&32)}; };
    native.set_pad(0,pad(current)); native.set_pad(1,pad(current>>6));
    native.advance(1); // Latch pending input before entering the routine.
    auto expected_current=current;
    if(mode==1) { native.write_port(0x3f,0,native.cycles()); expected_current|=0x20; }
    if(mode==2) { native.memory().set_memory_control(native.memory().memory_control()|0x04); expected_current=0x23; }
    auto& cpu=native.cpu();
    cpu.pc.uint16_value=game::controller_poll_entry.address;
    const std::uint16_t stack=mode==3 ? 0xdfff : 0xc700;
    cpu.sp.uint16_value=stack;
    native.memory().write(stack,0x80);native.memory().write(static_cast<std::uint16_t>(stack+1),0x01);
    native.memory().write(0xc204,previous);native.memory().write(0xc205,0xa5);
    cpu.af.uint16_value=(previous<<8)|current;
    cpu.bc.uint16_value=0x12e3;cpu.hl.uint16_value=0x9876;
    cpu.r=previous;cpu.r7=previous&0x80;cpu.q=current;
    cpu.memptr.uint16_value=0xabcd;
    Machine cpp=native;
    const auto calls=registry.find(game::controller_poll_entry)->counters.cpp_calls;
    const auto start=native.cycles(),fetches=native.opcode_fetches();
    for(unsigned instruction=0;instruction<10;++instruction) native.step();
    cpp.step(registry);
    if(!native.equivalent(cpp)) {
        std::string fields;
        for(const auto& field:native.differences(cpp)) fields+=' '+field.field;
        throw std::runtime_error("Controller state differs:"+fields);
    }
    check(cpp.last_access()==native.last_access(),"controller port timestamp differs");
    check(cpp.cycles()-start==70 && cpp.opcode_fetches()-fetches==10,"controller timing or refresh differs");
    check(registry.find(game::controller_poll_entry)->counters.cpp_calls==calls+1,"C++ body did not run");
    check(cpp.cpu().pc.uint16_value==0x180 && cpp.cpu().sp.uint16_value==static_cast<std::uint16_t>(stack+2),"caller return differs");
    // Independent button-by-button expectation: press, hold and release have
    // distinct meanings. This checks the game result as well as equivalence.
    unsigned pressed=0;
    for(unsigned bit=0;bit<8;++bit) if((expected_current&(1u<<bit)) && !(previous&(1u<<bit))) pressed|=1u<<bit;
    check(cpp.memory().read(0xc204)==expected_current && cpp.memory().read(0xc205)==pressed,"held/pressed button results differ");
}
}
int main(int argc,char** argv) { try {
    catalog(); options();
    if(argc==1) return 0;
    if(argc!=3) return 1;
    if(!*argv[1] || !std::filesystem::exists(argv[1])) return 77;
    const std::string_view region=argv[2];
    check(region=="us" || region=="br","Expected ROM version us or br");
    check(identify_rom(argv[1])==(region=="us" ? RomVersion::us_rev_a : RomVersion::brazilian),"Wrong ROM version.");
    std::ifstream f(argv[1],std::ios::binary);
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
    RoutineRegistry registry;
    check(game::register_routines(registry,sha1(*rom)),"identified ROM registry");
    // Every possible controller byte and previous held byte, including the
    // second pad's two low bits, both R bit-7 states and all initial flags.
    for(unsigned current=0;current<256;++current)
        for(unsigned previous=0;previous<256;++previous) compare(rom,registry,current,previous);
    for(unsigned mode=1;mode<=3;++mode)
        for(unsigned current=0;current<256;++current) compare(rom,registry,current,255-current,mode);
    std::cout<<"66304 controller calls (65536 held/pressed transitions, 768 in the three special modes): "
               "complete native/C++ equivalence, port timing, cycles and fetches.\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
