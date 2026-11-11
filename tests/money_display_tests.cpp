// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise the shared chest's original text and money addition from a real
// recorded battle, with a seeded five-digit amount or a nearly full purse.
#include "probe.h"
#include "replay/recording.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <iostream>
using namespace psmodern;
using probe::check;
namespace {
std::uint32_t crc32(const std::vector<std::uint8_t>& bytes, std::uint32_t crc=0) {
    crc=~crc;
    for (const auto byte:bytes) {
        crc^=byte;
        for (int bit=0;bit<8;++bit) crc=(crc>>1)^(0xedb88320u&(0u-(crc&1)));
    }
    return ~crc;
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift=24;shift>=0;shift-=8) out.push_back(static_cast<std::uint8_t>(value>>shift));
}
void chunk(std::vector<std::uint8_t>& png, const char* type, const std::vector<std::uint8_t>& data) {
    put32(png,static_cast<std::uint32_t>(data.size()));
    std::vector<std::uint8_t> body(type,type+4);
    body.insert(body.end(),data.begin(),data.end());
    png.insert(png.end(),body.begin(),body.end());
    put32(png,crc32(body));
}
// An uncompressed PNG of the picture, each pixel scaled to a 2x2 block. The
// picture holds the six-bit colors of the console (two bits per channel).
void write_png(const std::filesystem::path& path, const Vdp::Picture& picture) {
    constexpr unsigned width=256, height=192, scale=2;
    std::vector<std::uint8_t> raw;
    for (unsigned y=0;y<height*scale;++y) {
        raw.push_back(0);
        for (unsigned x=0;x<width*scale;++x) {
            const auto color=picture[(y/scale)*width+x/scale];
            for (const auto channel:{color&3,(color>>2)&3,(color>>4)&3}) raw.push_back(static_cast<std::uint8_t>(channel*85));
        }
    }
    std::vector<std::uint8_t> zlib{0x78,0x01};
    for (std::size_t at=0;at<raw.size();at+=65535) {
        const auto size=std::min<std::size_t>(65535,raw.size()-at);
        zlib.push_back(at+size>=raw.size() ? 1 : 0);
        zlib.push_back(static_cast<std::uint8_t>(size)); zlib.push_back(static_cast<std::uint8_t>(size>>8));
        zlib.push_back(static_cast<std::uint8_t>(~size)); zlib.push_back(static_cast<std::uint8_t>(~size>>8));
        zlib.insert(zlib.end(),raw.begin()+static_cast<std::ptrdiff_t>(at),raw.begin()+static_cast<std::ptrdiff_t>(at+size));
    }
    std::uint32_t a=1, b=0;
    for (const auto byte:raw) { a=(a+byte)%65521; b=(b+a)%65521; }
    put32(zlib,(b<<16)|a);
    std::vector<std::uint8_t> png{0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a}, header;
    put32(header,width*scale); put32(header,height*scale);
    header.insert(header.end(),{8,2,0,0,0});
    chunk(png,"IHDR",header); chunk(png,"IDAT",zlib); chunk(png,"IEND",{});
    std::ofstream file(path,std::ios::binary);
    file.write(reinterpret_cast<const char*>(png.data()),static_cast<std::streamsize>(png.size()));
    check(static_cast<bool>(file),"cannot write the picture");
}


std::uint16_t word(const Machine& m,std::uint16_t at) {
    return static_cast<std::uint16_t>(m.memory().read(at)|(m.memory().read(at+1)<<8));
}
void write_word(Machine& m,std::uint16_t at,unsigned value) {
    m.memory().write(at,static_cast<std::uint8_t>(value));
    m.memory().write(at+1,static_cast<std::uint8_t>(value>>8));
}
}
int main(int argc,char** argv) { try {
    if(argc!=5 && argc!=6) throw std::runtime_error("Expected ROM, version, recording, cartridge RAM and optional image directory");
    const auto rom=probe::load(argv[1],argv[2]);
    if(!rom || !std::filesystem::exists(argv[3]) || !std::filesystem::exists(argv[4])) return 77;
    auto recording=replay::read(argv[3]); check(recording.rom_sha1==rom->sha1,"recording ROM");
    Memory::CartridgeRam cart{}; std::ifstream f(argv[4],std::ios::binary);
    check(static_cast<bool>(f.read(reinterpret_cast<char*>(cart.data()),cart.size())),"cartridge RAM");
    Machine machine(rom->image,cart); RoutineRegistry registry;
    game::register_routines(registry,rom->sha1);
    std::optional<Machine> checkpoint,decision,opened; std::uint64_t at_frame=0;
    auto find=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
        if(pc==0x2925 && map==Memory::Mapping{Memory::Kind::rom,0} && !checkpoint) checkpoint=machine;
        if(pc==0x28ec && map==Memory::Mapping{Memory::Kind::rom,0} && !decision) decision=machine;
        if(pc==0x28ee && map==Memory::Mapping{Memory::Kind::rom,0} && !opened) opened=machine;
        return {};
    };
    for(std::uint64_t n=0;n<recording.frames && !checkpoint;++n) {
        machine.run_frame(recording.input_at(n),registry,find);
        machine.take_samples(); machine.take_psg_writes(); at_frame=n;
    }
    check(checkpoint.has_value(),"recording did not reach money chest");
    check(decision.has_value() && opened.has_value(),"recording did not reach chest decision and opening");
    std::filesystem::path out=argc==6 ? argv[5] : "";
    if(!out.empty()) std::filesystem::create_directories(out);
    for(auto [reward,total]:{std::pair{50000u,0u},std::pair{65535u,65000u}}) {
        Machine trial=*checkpoint; RoutineRegistry routines; game::register_routines(routines,rom->sha1);
        write_word(trial,0xc2dd,reward); write_word(trial,0xc4e0,total);
        bool displayed=false,added=false;
        for(unsigned frame=0;frame<500;++frame) {
            auto input=recording.input_at(at_frame+frame);
            input.pads[0].one=frame>=120 && frame%30<4;
            trial.run_frame(input,routines); trial.take_samples();trial.take_psg_writes();
            displayed=displayed || word(trial,0xc2c5)==reward;
            added=added || word(trial,0xc4e0)==std::min(65535u,total+reward);
            if(!out.empty() && frame>=30 && frame<=180 && frame%15==0)
                write_png(out/(std::string(argv[2])+"-money-"+std::to_string(reward)+"-frame-"+std::to_string(frame)+".png"),trial.picture());
        }
        check(displayed,"chest dialogue did not receive the five-digit amount");
        check(added,"party did not receive the capped chest amount");
    }
    // Once at the common chest, changing the experience option must not
    // affect nonbattle treasure, traps, refusing a chest or item-only paths.
    // Both trials start from the same recorded game state and run the real
    // text, prompt continuation, animation and addition code.
    struct ChestCase { const char* name; bool refuse; unsigned trap,money,item; bool addition; };
    const ChestCase cases[]{
        {"refused",true,0x3d,123,0,false}, {"empty",false,0x3d,0,0,false},
        {"item-only",false,0x3d,0,1,true}, {"money",false,0x3d,123,0,true},
        {"trap",false,0x3e,123,0,true}};
    for(const auto& test:cases) {
        Machine a=test.refuse ? *decision : *opened; Machine b=a;
        for(auto* m:{&a,&b}) {
            if(test.refuse) m->cpu().af.uint8_values.at_1=1;
            m->memory().write(0xc80f,static_cast<std::uint8_t>(test.trap));
            write_word(*m,0xc2dd,test.money); m->memory().write(0xc2df,static_cast<std::uint8_t>(test.item));
            write_word(*m,0xc4e0,20);
        }
        RoutineRegistry original,enhanced; game::register_routines(original,rom->sha1);
        game::register_routines(enhanced,rom->sha1,game::Options{100,400});
        bool done_a=false,done_b=false;
        auto hook_a=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
            if(pc==0x00af && map==Memory::Mapping{Memory::Kind::rom,0}) done_a=true; return {};
        };
        auto hook_b=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
            if(pc==0x00af && map==Memory::Mapping{Memory::Kind::rom,0}) done_b=true; return {};
        };
        for(unsigned frame=0;frame<2400 && !done_a && !done_b;++frame) {
            FrameInput input{}; input.pads[0].one=frame%30<4;
            a.run_frame(input,original,hook_a); b.run_frame(input,enhanced,hook_b);
            a.take_samples();a.take_psg_writes();b.take_samples();b.take_psg_writes();
            check(a.equivalent(b),std::string("common chest diverged at ")+test.name);
        }
        check(done_a && done_b,std::string("common chest did not finish: ")+test.name);
        const auto& calls=enhanced.find(game::money_add_unit)->counters;
        check((calls.cpp_calls+calls.fallback_calls>0)==test.addition,std::string("unexpected chest addition: ")+test.name);
        check(word(b,0xc4e0)==std::min(65535u,20+test.money*(test.addition?1u:0u)),
              std::string("unexpected chest money: ")+test.name);
    }
    // Enter a map-chest caller using a captured game state and a known return
    // address. This executes the map caller, its prompt and the shared chest.
    // Separately begin at the accepted item-sale continuation, which removes
    // the selected item and calls the same money addition.
    for(bool map_chest:{true,false}) {
        Machine a=*opened; a.cpu().pc.uint16_value=map_chest ? 0x69c7 : 0x2d01;
        a.cpu().sp.uint16_value=0xdf00; a.memory().write(0xdf00,0xaf); a.memory().write(0xdf01,0x00);
        a.cpu().bc.uint8_values.at_1=1; a.memory().write(0xc80f,0x3d);
        write_word(a,0xc2dd,123); a.memory().write(0xc2df,0); write_word(a,0xc4e0,20);
        write_word(a,0xc2c5,123); write_word(a,0xc29b,0xc4c0);
        a.memory().write(0xc4c0,1); a.memory().write(0xc4e2,1);
        Machine b=a; RoutineRegistry original,enhanced; game::register_routines(original,rom->sha1);
        game::register_routines(enhanced,rom->sha1,game::Options{100,400});
        bool done_a=false,done_b=false;
        const auto end=static_cast<std::uint16_t>(map_chest ? 0x00af : 0x2d0a);
        auto hook_a=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
            if(pc==end && map==Memory::Mapping{Memory::Kind::rom,0}) done_a=true; return {};
        };
        auto hook_b=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
            if(pc==end && map==Memory::Mapping{Memory::Kind::rom,0}) done_b=true; return {};
        };
        for(unsigned n=0;n<2400 && !done_a && !done_b;++n) {
            FrameInput input{}; input.pads[0].one=n%30<4;
            a.run_frame(input,original,hook_a);b.run_frame(input,enhanced,hook_b);
            a.take_samples();a.take_psg_writes();b.take_samples();b.take_psg_writes();
            check(a.equivalent(b),map_chest ? "map chest diverged" : "sale diverged");
        }
        check(done_a && done_b,map_chest ? "map chest did not finish" : "sale did not reach message");
        check(word(b,0xc4e0)==143,map_chest ? "map chest reward changed" : "sale price changed");
        check(enhanced.find(game::money_add_unit)->counters.cpp_calls>0,"shared money addition did not run");
        if(!map_chest) check(b.memory().read(0xc4e2)==0,"sold item was not removed");
    }
    // This scripted caller clears the produced battle money after setup.
    // Start from a real game state, enter that caller and observe the actual
    // battle entry; no invented replacement for the script is executed.
    {
        Machine scripted=*opened; scripted.cpu().pc.uint16_value=0x55bb;
        scripted.cpu().sp.uint16_value=0xdf00;
        RoutineRegistry routines; game::register_routines(routines,rom->sha1,game::Options{100,400});
        bool entered=false;
        auto hook=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
            if(pc==0x100f && map==Memory::Mapping{Memory::Kind::rom,0}) entered=true; return {};
        };
        for(unsigned n=0;n<1200 && !entered;++n) {
            scripted.run_frame({},routines,hook); scripted.take_samples();scripted.take_psg_writes();
        }
        check(entered && word(scripted,0xc2dd)==0,"scripted battle did not clear its money");
        const auto& calls=routines.find(game::battle_money_store)->counters;
        check(calls.forced_calls==1 && calls.forced_failed==0,"scripted battle setup did not scale once before clearing");
    }
    std::cout<<argv[2]<<": five-digit chest and capped total passed at recording frame "<<at_frame+1<<".\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;} }
