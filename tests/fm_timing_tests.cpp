// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/fm_audio.h"
#include "oracle/native_machine.h"
#include "rom/rom.h"
#include <algorithm>
#include <iostream>

namespace {
using namespace psmodern;
using app::FmAudio;
struct ExpectedWrite { std::uint64_t cycle;std::uint8_t port,value; };
// Execute complete calls on the original CPU. Record the whole bus sequence
// before applying it to a fresh chip, independently of the host's output queue.
struct Reference {
    oracle::NativeMachine machine;
    std::uint64_t end=0;
    std::vector<ExpectedWrite> writes;
    explicit Reference(std::shared_ptr<const Memory::Rom> rom):machine(std::move(rom)) {
        machine.memory().write(0xffff,12);
        machine.memory().write(0xc000,1);
    }
    void call(std::uint16_t entry,std::uint64_t time,int command=-1) {
        auto& m=machine;
        if(command>=0)m.memory().write(0xc004,static_cast<std::uint8_t>(command));
        auto& r=m.cpu();
        initial_state::cpu(r);
        r.hl_.uint16_value=0xc00c;r.af_.uint16_value=0x0100;
        r.pc.uint16_value=entry;r.sp.uint16_value=0xdfee;
        m.memory().write(0xdfee,0xf0);m.memory().write(0xdfef,0xdf);
        const auto start=m.cycles();
        Console::PortTrace trace(m);
        do {
            m.step();
            if(m.cycles()-start>Machine::frame_cycles)throw std::runtime_error("Oracle call did not return");
        } while(r.pc.uint16_value!=0xdff0);
        const auto begins=std::max(time,end);
        for(const auto& w:trace.accesses())if(w.port==0xf0 || w.port==0xf1)
            writes.push_back({begins+w.cycle-start,w.port,w.value});
        end=begins+m.cycles()-start;
        m.take_psg_writes();m.take_samples();
    }
};
struct Event {
    unsigned speed;
    int command;          // -1: no surviving external write
    bool update;          // false: paused game omitted the update
    bool score_tick;      // explicit expected native call
};
void timing(const std::shared_ptr<const Memory::Rom>& rom) {
    auto ref=std::make_unique<Reference>(rom);
    FmAudio audio(rom);
    ref->call(0x8000,0);audio.initialize(0);
    constexpr Event events[]{
        {1,0x81,true,true},{1,-1,true,true},{1,0xa5,true,true},
        {2,0xa5,true,false},{2,0x85,true,true},{2,0xd6,true,false},
        {4,0xd6,true,false},{4,0x87,true,true},{4,0xa5,true,false},
        {4,-1,false,false},{4,-1,false,false},{4,0xa5,true,false},
        {1,-1,true,true},{1,0xd7,true,true},{1,-1,true,true},
        {2,0x8b,true,true},{2,-1,true,false},{1,-1,true,true},
    };
    std::uint64_t source=0,due=0;
    struct Batch { std::uint64_t due;PsgSamples samples; };
    std::vector<Batch> out;
    auto finish=[&](std::uint64_t at,std::uint64_t until){out.push_back({until,audio.finish(at)});};
    for(auto e:events) {
        audio.speed(e.speed,source);
        constexpr auto frame=Machine::frame_cycles;
        constexpr std::uint64_t offset=frame/4,dispatch_delay=816;
        const auto tick=due+offset/e.speed;
        const auto dispatch=due+(offset+dispatch_delay)/e.speed;
        if(e.update) {
            audio.begin_tick(source+offset);
            if(e.speed==1) {
                // A real frame observer may separate the two observations.
                // No samples after the pending tick can be returned yet.
                finish(source+offset+12,tick);
                ref->call(0x8052,tick,e.command);
            } else {
                if(e.score_tick)ref->call(0x8052,tick);
                if(e.command>=0)ref->call(0x8466,dispatch,e.command);
            }
            const auto external=e.command<0 ? std::nullopt : std::optional{static_cast<std::uint8_t>(e.command)};
            audio.consume(source+offset+dispatch_delay,external);
        }
        source+=frame;due+=frame/e.speed;
        finish(source,due);
    }
    // Silence and reinitialization make their own timed writes after the
    // song is underway. They also test queues extending past a frame end.
    audio.silence(source);ref->call(0x801f,due);
    audio.initialize(source+100);ref->call(0x8000,due+100);
    finish(source+Machine::frame_cycles,due+Machine::frame_cycles);
    Fm chip;
    std::size_t i=0;
    bool audible=false;
    for(const auto& batch:out) {
        while(i<ref->writes.size() && ref->writes[i].cycle<=batch.due) {
            const auto w=ref->writes[i++];
            chip.write(w.port,w.value,w.cycle);
        }
        chip.sync(batch.due);
        if(chip.take_samples()!=batch.samples)throw std::runtime_error("FM samples differ from timed Z80 calls at cycle "+std::to_string(batch.due));
        audible|=std::any_of(batch.samples.values.begin(),batch.samples.values.end(),[](auto n){return n!=0;});
    }
    if(!audible || i!=ref->writes.size())throw std::runtime_error("Incomplete FM oracle playback");
}
}
int main(int argc,char** argv)try {
    if(argc!=2 || !*argv[1] || !std::filesystem::is_regular_file(argv[1]))return 77;
    timing(std::make_shared<psmodern::Memory::Rom>(psmodern::read_rom_image(argv[1])));
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
