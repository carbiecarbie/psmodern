// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/option_flags.h"
#include "game/routines.h"
#include "replay/recording.h"
#include "replay/census.h"
#include "replay/checkpoint.h"
#include "rom/sha1.h"
#include "temporary_cleanup.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace psmodern;
using namespace psmodern::replay;
void check(bool ok) {if(!ok)throw std::runtime_error("Replay assertion failed");}
int main() {
    auto folder=std::filesystem::temp_directory_path()/("psmodern-replay-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        FrameWrites frame_writes;
        check(frame_writes.take(1,{{59735,0x90},{59736,0x9f},{59740,0xaa}})==
              std::vector<std::uint8_t>({0xe9,0x57,0x90}));
        check(frame_writes.take(2,{})==std::vector<std::uint8_t>({0,0,0x9f,0,4,0xaa}));
        Z80 cpu{};cpu.af.uint16_value=0x1234;cpu.af_.uint16_value=0x5678;
        cpu.pc.uint16_value=0xabcd;cpu.r=0x81;cpu.iff2=1;cpu.halt_line=1;
        auto bytes=cpu_bytes(cpu);
        check(bytes[0]==0x12 && bytes[1]==0x34 && bytes[8]==0x56 && bytes[9]==0x78 &&
              bytes[22]==0xab && bytes[23]==0xcd && bytes[25]==0x81 && bytes[27]==1 && bytes[29]==1);
        Recorder rec(std::string(40,'a'),sha1(Memory::CartridgeRam{}));
        FrameInput input{};rec.add(input);input.pads[0].one=true;input.pads[1].up=true;input.pause=true;input.reset=true;
        rec.add(input);rec.add(input);rec.add({});
        check(rec.recording().changes.size()==2&&rec.recording().frames==4);
        const auto text=write(rec.recording());check(write(parse(text))==text);
        check(parse(text).input_at(2).pads[1].up&&!parse(text).input_at(3).pause);
        auto refuse=[](std::string s){try{parse(s);}catch(const std::exception& e){check(std::string(e.what()).find("line ")!=std::string::npos);return;}throw std::runtime_error("Accepted invalid recording");};
        for(const auto& pair:std::vector<std::pair<std::string,std::string>>{{"psmodern-recording 1","psmodern-recording 2"},{"rom ","unknown "},{"p1=1","p1=Z"},{"input 3","input 1"},{"end 4","end 1"},{"pause=1","pause=2"},{"p2=U","p2=UU"}}){auto s=text;s.replace(s.find(pair.first),pair.first.size(),pair.second);refuse(s);}
        refuse("");refuse(text.substr(text.find('\n')+1));refuse(text.substr(0,text.rfind("end ")));
        refuse(text+"input 5 p1=1 p2=- pause=0 reset=0\n");refuse(text+"junk\n");refuse(text+"\n");
        refuse(text.substr(0,text.size()-1));refuse(text.substr(0,10)+"\r"+text.substr(10));
        auto s=text;s.replace(s.find("input 1"),7,"input 01");refuse(s);
        // The shared option flags (also used by psmodern_replay): edges, rejected forms, refusals, explicit 100.
        {
            using game::OptionFlags;
            auto take=[](std::vector<const char*> words,OptionFlags& flags) {
                words.insert(words.begin(),"psmodern_replay");
                for(int i=1;i<static_cast<int>(words.size());++i) if(!flags.take(static_cast<int>(words.size()),words.data(),i))
                    throw std::runtime_error("unknown argument");
            };
            auto parsed=[&](std::vector<const char*> words){OptionFlags flags;take(words,flags);return flags;};
            auto fails=[&](std::vector<const char*> words,const std::string& part){
                OptionFlags flags;try{take(words,flags);}catch(const std::runtime_error& e){return std::string(e.what()).find(part)!=std::string::npos;}return false;};
            check(parsed({}).options==game::Options{});
            check(parsed({"--encounter-rate","0"}).options.encounter_percent==0&&parsed({"--encounter-rate","200"}).options.encounter_percent==200);
            check(parsed({"--experience-rate","200"}).options.experience_percent==200&&parsed({"--experience-rate","400"}).options.experience_percent==400);
            check(parsed({"--experience-rate","400","--encounter-rate","50"}).options==game::Options{50,400}&&parsed({"--experience-rate","400","--encounter-rate","25"}).options==game::Options{25,400});
            for(const char* flag:{"--encounter-rate","--experience-rate"}){
                check(fails({flag},"needs a value"));
                for(const char* bad:{"","-1","+1","1e2","5.0"," 5","abc","0x20"})check(fails({flag,bad},"whole number of percent"));
                check(fails({flag,"18446744073709551616"},"percent, got")&&fails({flag,"4294967296"},"percent, got"));
                check(fails({flag,"100",flag,"100"},"more than once"));
            }
            check(fails({"--encounter-rate","24"},"one of 0, 25, 50, 100 or 200")&&fails({"--encounter-rate","400"},"one of 0, 25, 50, 100 or 200")&&
                  fails({"--experience-rate","25"},"one of 100, 200 or 400")&&fails({"--experience-rate","1000"},"one of 100, 200 or 400")&&
                  fails({"--experience-rate","0"},"one of 100, 200 or 400"));
            // Refusals name the flag and its value; explicit 100 is original mode and never refused.
            auto refused=[&](std::vector<const char*> words,const std::string& part){
                try{parsed(words).refuse_with("--lockstep-calls","reason");}catch(const std::runtime_error& e){return std::string(e.what()).find(part)!=std::string::npos;}return false;};
            check(refused({"--encounter-rate","50"},"--encounter-rate 50 cannot be combined with --lockstep-calls"));
            check(refused({"--experience-rate","400"},"--experience-rate 400 cannot be combined with --lockstep-calls"));
            parsed({}).refuse_with("--lockstep-calls","reason");
            parsed({"--encounter-rate","100","--experience-rate","100"}).refuse_with("--lockstep-calls","reason");
            check(game::original_mode(parsed({"--encounter-rate","100","--experience-rate","100"}).options));
            // Identical registries for explicit 100 and no flags, and options reach registration.
            for(auto digest:{"07fcf297be4f4c9d92cd3f119a7ac48467e06838","fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd"}){
                RoutineRegistry plain,hundred;
                check(game::register_routines(plain,digest)&&game::register_routines(hundred,digest,parsed({"--experience-rate","100","--encounter-rate","100"}).options));
                check(plain.entries().size()==hundred.entries().size());
                for(const auto& [entry,routine]:plain.entries()){const auto* other=hundred.find(entry);check(other&&other->spec.unit==routine.spec.unit&&std::equal(other->spec.exits.begin(),other->spec.exits.end(),routine.spec.exits.begin(),routine.spec.exits.end(),[](const SegmentExit& x,const SegmentExit& y){return x.same_declaration(y);}));}
                RoutineRegistry changed;check(game::register_routines(changed,digest,parsed({"--encounter-rate","50","--experience-rate","400"}).options));
            }
        }
        std::filesystem::create_directory(folder);auto path=folder/"test.psrec";
        write_atomic_new(path,rec.recording());check(write(read(path))==text);
        bool refused=false;try{write_atomic_new(path,rec.recording());}catch(...){refused=true;}check(refused&&write(read(path))==text);
        refused=false;try{write_atomic_new(folder/"absent"/"x",rec.recording());}catch(...){refused=true;}check(refused&&!std::filesystem::exists(folder/"absent"/"x"));
        {
            // Census writer and checkpoint rule on authored counters.
            RoutineRegistry registry;
            auto add=[&](unsigned bank,std::uint16_t address,RoutineEntry unit){
                SegmentSpec spec{};spec.unit=unit;spec.exits={{RoutineExit::returned,0x100}};
                registry.add({bank,address},spec,[](const Machine&){return std::uint64_t{4};},[](RoutineContext&){});
                return &registry.find({bank,address})->counters;};
            auto* controller=add(0,0x0339,{0,0x0339});
            auto* segment=add(1,0x5FB1,{1,0x5F63});
            auto* option=add(0,0x05B1,{0,0x05B1});
            auto* rare=add(0,0x1773,{0,0x1754});
            auto problems=[&]{return checkpoint_problems(registry,std::array{RoutineEntry{0,0x0339}});};
            auto mentions=[&](const std::string& entry){
                std::size_t n=0;for(const auto& p:problems())if(p.starts_with(entry))++n;return n;};
            controller->cpp_calls=10;segment->cpp_calls=18;segment->fallback_calls=2;
            segment->forced_reasons[1]=3;segment->reasons[3]=2;
            std::ostringstream out;write_census(out,registry);
            std::istringstream in(out.str());std::string header,line;std::getline(in,header);
            check(header=="entry	unit	cpp	fallback	forced	forced_failed	disabled	irq	nmi	frame	ei_delay	interrupt_delay	halted	unmapped_stack"
                  "	forced_disabled	forced_irq	forced_nmi	forced_frame	forced_ei_delay	forced_interrupt_delay	forced_halted	forced_unmapped_stack");
            std::getline(in,line);check(line.starts_with("00:0339	00:0339	10	0	0	0	"));
            std::getline(in,line);check(line.starts_with("00:05B1	00:05B1	0	0	"));
            std::getline(in,line);check(line.starts_with("00:1773	00:1754	"));
            std::getline(in,line);check(line=="01:5FB1	01:5F63	18	2	0	0	0	0	0	2	0	0	0	0	0	3	0	0	0	0	0	0");
            check(!std::getline(in,line));
            // Exactly 90% of 20 passes; one fewer fails; fewer than 20 calls are not gated.
            check(problems().empty());
            segment->cpp_calls=17;segment->fallback_calls=3;check(mentions("01:5FB1")==1);
            segment->cpp_calls=0;segment->fallback_calls=19;check(mentions("01:5FB1")==0);
            segment->fallback_calls=20;check(mentions("01:5FB1")==1);
            segment->cpp_calls=18;segment->fallback_calls=2;
            rare->fallback_calls=5;check(problems().empty());
            // Forced calls are refused in original mode, forced failures too.
            option->forced_calls=1;check(mentions("00:05B1")==1);
            option->forced_calls=0;option->forced_failed=1;check(mentions("00:05B1")==1);
            option->forced_failed=0;check(problems().empty());
            // The controller must be reached and keeps 90% even with few calls.
            controller->cpp_calls=0;check(mentions("00:0339")==1);
            controller->cpp_calls=9;controller->fallback_calls=1;check(problems().empty());
            controller->cpp_calls=8;controller->fallback_calls=2;check(mentions("00:0339")==1);
            check(checkpoint_problems(registry,std::array{RoutineEntry{0,0x1234}}).size()==1);
            // An empty registry has no controller: a recognized ROM that registers nothing is a defect.
            check(checkpoint_problems(RoutineRegistry{},std::array{RoutineEntry{0,0x0339}}).size()==1);
            check(checkpoint_problems(RoutineRegistry{},std::span<const RoutineEntry>()).empty());
        }
        test::remove_temporary_folder(folder);std::cout<<"Format refusals, canonical round trip, change detection and atomic publication passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';test::remove_temporary_folder(folder);return 1;}
}
