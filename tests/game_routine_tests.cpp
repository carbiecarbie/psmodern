// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/routines.h"
#include "machine/machine.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace psmodern;
namespace {
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
void catalog() {
    RoutineRegistry unknown;
    check(!game::register_routines(unknown,"authored fixture") && unknown.empty(),"unknown image gets no game routines");
    for(auto digest:{"07fcf297be4f4c9d92cd3f119a7ac48467e06838","fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd"}) {
        RoutineRegistry r;
        check(game::register_routines(r,digest) && r.entries().size()==1,"both versions register the first routine");
        check(r.find(game::controller_poll_entry)!=nullptr,"controller entry exists");
    }
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
    catalog();
    if(argc==1) return 0;
    if(argc!=2) return 1;
    if(!*argv[1] || !std::filesystem::exists(argv[1])) return 77;
    identify_rom(argv[1]);
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
    std::cout<<"65536 controller transitions: complete native/C++ equivalence, port timing, cycles and fetches.\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
