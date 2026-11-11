// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "game/routines.h"
#include "replay/recording.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include "temporary_cleanup.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace psmodern;
using app::TempoAudio;
void check(bool ok,const char* why) { if(!ok)throw std::runtime_error(why); }

// An unrestricted bus executes the original whole driver. Unlike TempoAudio,
// this oracle has no speed, request forwarding, tick division or output bound.
class NativeSound : private CpuBus {
public:
    explicit NativeSound(const Machine& source)
        :memory(source.memory()),cpu_(*this),psg_(source.psg()),cycle_(source.cycles()) {
        const auto prefix=psg_.take_samples();first_=prefix.first_cycle;
        pending_=prefix.values;psg_.take_writes();
    }
    void at(std::uint64_t target) {
        if(target>cycle_) { psg_.sync(target);cycle_=target;collect(); }
    }
    void call(unsigned entry) {
        cpu_=Cpu(*this);auto& cpu=cpu_.raw();cpu.pc.uint16_value=entry;
        cpu.sp.uint16_value=0xdfee;cpu.hl_.uint16_value=0xc00c;
        memory.write(0xdfee,0);memory.write(0xdfef,0);
        const auto start=cycle_;
        do {cycle_+=cpu_.step(cycle_);check(cycle_-start<Machine::frame_cycles,"native sound oracle did not return");}
        while(cpu.pc.uint16_value || cpu.resume);
        psg_.sync(cycle_);collect();
    }
    PsgSamples until(std::uint64_t target) {
        at(target);PsgSamples out{first_+used_*Psg::sample_cycles,{}};
        const auto count=target<first_ ? 0 : (target-first_)/Psg::sample_cycles+1;
        check(count<=pending_.size(),"native oracle lacks due samples");
        out.values.assign(pending_.begin()+used_,pending_.begin()+count);used_=count;
        return out;
    }
    Memory memory;
private:
    void collect() {const auto next=psg_.take_samples();pending_.insert(pending_.end(),next.values.begin(),next.values.end());psg_.take_writes();}
    std::uint8_t opcode(std::uint16_t address) override {return memory.read(address);}
    std::uint8_t read(std::uint16_t address) override {return memory.read(address);}
    void write(std::uint16_t address,std::uint8_t value) override {memory.write(address,value);}
    std::uint8_t input(std::uint16_t,std::uint64_t) override {throw std::runtime_error("native sound unexpectedly read a port");}
    void output(std::uint16_t,std::uint8_t value,std::uint64_t cycle) override {psg_.write(value,cycle);}
    Cpu cpu_;Psg psg_;std::uint64_t cycle_,first_=0;std::size_t used_=0;
    std::vector<std::int16_t> pending_;
};
Machine capture(std::shared_ptr<const Memory::Rom> rom) {
    Machine source(rom);source.memory().write(0xfffc,0);source.memory().write(0xffff,12);
    auto& cpu=source.cpu();cpu.pc.uint16_value=0x8000;cpu.sp.uint16_value=0xdfee;
    source.memory().write(0xdfee,0);source.memory().write(0xdfef,0);
    while(cpu.pc.uint16_value || cpu.resume)source.step();
    // This pending music request belongs to the first update captured by the
    // constructor, including the unsubmitted initialization samples before it.
    source.memory().write(0xc004,0x81);cpu.pc.uint16_value=0x8043;
    return source;
}
void sound_state(const TempoAudio& audio,const NativeSound& native) {
    for(unsigned address=0xc000;address<0xc200;++address)
        check(audio.memory().read(address)==native.memory.read(address),"sound records differ from the whole native driver");
}
void normal_music(const Machine& source,unsigned speed) {
    TempoAudio audio(source);audio.speed(speed);NativeSound native(source);native.call(0x8043);
    const auto start=source.cycles();bool nonzero=false;std::size_t total=0;
    for(unsigned frame=0;frame<=speed*120;++frame) {
        const auto target=start+frame*Machine::frame_cycles/speed;
        if(frame) {
            audio.update(start+frame*Machine::frame_cycles,0x80);
            if(frame%speed==0) {native.at(target);native.call(0x8043);}
        }
        const auto samples=audio.finish(start+frame*Machine::frame_cycles);
        check(samples==native.until(target),"normal-tempo PCM differs from original full-driver execution");
        check(audio.ticks()==1+frame/speed,"normal-tempo driver tick count");
        for(auto value:samples.values)nonzero|=value!=0;
        total+=samples.values.size();sound_state(audio,native);
    }
    check(nonzero && total>400000,"native music output was empty or silent");
}
void mixed_clock(const Machine& source) {
    TempoAudio audio(source);std::uint64_t source_cycle=source.cycles(),quarters=source_cycle*4;
    std::size_t total=0;unsigned tick_quarters=0;std::uint64_t ticks=1;
    // Odd cycle distances retain the fractional cycle across every speed change.
    for(unsigned n=0;n<120;++n) {
        const unsigned speed=std::array<unsigned,6>{2,4,1,4,2,4}[n%6];audio.speed(speed);
        const auto delta=Machine::frame_cycles+1;source_cycle+=delta;quarters+=delta*(4/speed);
        if(n%5) {audio.update(source_cycle,0x80);tick_quarters+=4/speed;if(tick_quarters>=4){tick_quarters-=4;++ticks;}}
        const auto samples=audio.finish(source_cycle);total+=samples.values.size();
        check(total==(quarters/4)/Psg::sample_cycles,"mixed speed lost a fractional source cycle or output a future sample");
        check(audio.ticks()==ticks,"missing sound service advanced sequence timing");
        check(audio.finish(source_cycle).values.empty(),"same output watermark emitted samples twice");
    }
}
void requests(const Machine& source) {
    TempoAudio audio(source);audio.speed(4);NativeSound native(source);native.call(0x8043);
    auto source_cycle=source.cycles();auto target=source_cycle;unsigned ticks=1;
    // Separate native update admission from consumed requests. Several commands
    // at one timestamp must execute promptly without adding sequence ticks.
    for(unsigned frame=1;frame<=80;++frame) {
        source_cycle+=Machine::frame_cycles;target=source.cycles()+frame*Machine::frame_cycles/4;
        native.at(target);audio.update(source_cycle,0x80);
        if(frame%4==0){native.call(0x8043);++ticks;}
        if(frame==7 || frame==31) {
            for(auto command:{0xa0,0xa1,0xd5}) {
                audio.request(source_cycle,command);native.memory.write(0xc004,command);native.call(0x82ef);
            }
        }
        if(frame==17){audio.silence(source_cycle);native.call(0x801f);}
        if(frame==23){audio.initialize(source_cycle);native.call(0x8000);}
        if(frame==24){audio.request(source_cycle,0x81);native.memory.write(0xc004,0x81);native.call(0x82ef);}
        check(audio.ticks()==ticks,"request, initialization or silence added a timing tick");
        sound_state(audio,native);
        check(audio.finish(source_cycle)==native.until(target),"commands were delayed, reordered or replayed from a backlog");
    }
}
void integration(std::shared_ptr<const Memory::Rom> rom,RomVersion version,const replay::Recording& recording,
                 const std::filesystem::path& root) {
    app::FrameDriver normal(rom,app::SaveStore(root/"normal",version));
    app::FrameDriver fast(rom,app::SaveStore(root/"fast",version));
    app::FrameDriver recorded(rom,app::SaveStore(root/"recorded",version),true,{},true);
    bool nonzero=false,saw_playback=false;std::size_t playback_samples=0;
    for(unsigned frame=0;frame<1400;++frame) {
        const unsigned speed=frame<100 ? 1 : frame<500 ? 2 : frame<1100 ? 4 : 1;
        fast.speed(speed);recorded.speed(4);
        const auto input=recording.input_at(frame);
        std::unique_ptr<Machine> first_capture;
        if(frame==100) {
            for(auto* driver:{&normal,&fast,&recorded})driver->machine().memory().write(0xc004,0x81);
            Machine observed(normal.machine());
            observed.run_frame(input,normal.routines(),[&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
                if(!first_capture && pc==0x8043 && mapping==Memory::Mapping{Memory::Kind::rom,12})
                    first_capture=std::make_unique<Machine>(observed);
                return {};
            });
            check(first_capture && first_capture->peek(0xc004)==0x81,"first capture did not retain its pending music request");
        }
        const auto a=normal.run(input),b=fast.run(input),c=recorded.run(input);
        check(!a.playback && !c.playback,"original or recorded run created independent audio");
        check(!a.audio_error && !b.audio_error && !c.audio_error,"independent audio reported an error");
        check(a.writes==b.writes && a.samples==b.samples && a.writes==c.writes && a.samples==c.samples,
              "presentation speed changed emulated PSG writes or original samples");
        check(normal.machine().equivalent(fast.machine()) && normal.machine().equivalent(recorded.machine()),
              "presentation speed changed gameplay machine state");
        if(frame<100)check(!b.playback,"1x created an independent sound path");
        if(first_capture) {
            NativeSound first(*first_capture);first.call(0x8043);
            const auto target=first_capture->cycles()+(fast.machine().cycles()-first_capture->cycles())/2;
            check(b.playback && *b.playback==first.until(target),"first pending request was lost or relayed twice");
        }
        if(b.playback){saw_playback=true;playback_samples+=b.playback->values.size();for(auto v:b.playback->values)nonzero|=v!=0;}
    }
    check(saw_playback && nonzero && playback_samples>200000,"accelerated integration produced no music");
    check(!fast.power_cycle(),"power cycle could not flush save");
    const auto reset=fast.run({});check(!reset.playback && !reset.audio_error,"power cycle retained an accelerated sound timeline");
}
void fade_provenance(std::shared_ptr<const Memory::Rom> rom,RomVersion version,
                     const replay::Recording& recording,const std::filesystem::path& root) {
    app::FrameDriver driver(rom,app::SaveStore(root/"fade",version));
    for(unsigned frame=0;frame<100;++frame)driver.run(recording.input_at(frame));
    driver.machine().memory().write(0xc004,0x81);driver.speed(4);
    std::unique_ptr<NativeSound> native;std::uint64_t start=0;unsigned phase=0,internal_resets=0;
    for(unsigned frame=0;frame<40;++frame) {
        if(frame==5) {
            // Force only the gameplay driver's fade to finish. Its internal
            // reset must not end the independent music on the faster clock.
            for(auto [address,value]:std::array<std::pair<unsigned,unsigned>,4>{{{0xc002,1},{0xc006,0x80},{0xc00a,0x0b},{0xc00b,1}}})
                driver.machine().memory().write(address,value);
        }
        Machine observed(driver.machine());
        observed.run_frame(recording.input_at(100+frame),driver.routines(),
            [&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
                if(pc==0x8043 && mapping==Memory::Mapping{Memory::Kind::rom,12}) {
                    if(!native){start=observed.cycles();native=std::make_unique<NativeSound>(observed);native->call(0x8043);}
                    else if(++phase==4){phase=0;native->at(start+(observed.cycles()-start)/4);native->call(0x8043);}
                }
                return {};
            },[&](std::uint16_t address,std::uint8_t value) {
                if((address&0x1fff)!=4)return;
                check(observed.memory().mapping(observed.cpu().pc.uint16_value)==Memory::Mapping{Memory::Kind::rom,12},
                      "fade fixture unexpectedly generated an external request");
                if(frame>=5 && value==0)++internal_resets;
            });
        const auto out=driver.run(recording.input_at(100+frame));
        check(native && out.playback && !out.audio_error,"fade fixture lost independent audio");
        check(*out.playback==native->until(start+(driver.machine().cycles()-start)/4),
              "gameplay driver's internal fade/reset reached the independent music clock");
    }
    check(internal_resets>0,"fade fixture never reached the native internal reset");
}
void registered_request(std::shared_ptr<const Memory::Rom> rom) {
    Machine source=capture(rom);const auto start=source.cycles();TempoAudio audio(source);audio.speed(4);
    NativeSound native(source);native.call(0x8043);
    RoutineRegistry routines;game::register_routines(routines,sha1(*rom),{});
    auto& cpu=source.cpu();cpu.pc.uint16_value=0x1774;cpu.hl.uint16_value=0xffff;
    cpu.af.uint8_values.at_0&=0xfe;cpu.ix_iy[0].uint16_value=0xc600;cpu.ix_iy[1].uint16_value=0xc400;
    source.memory().write(0xc208,1); // Keep the interrupt-disabled native wait busy.
    unsigned requests=0;
    const auto text=static_cast<std::uint16_t>((*rom)[0x179b]|((*rom)[0x179c]<<8));
    source.run_frame({},routines,[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
        // Finish the bounded award segment, then wait without invoking text.
        if(pc==text)return 0x0055;return {};
    },[&](std::uint16_t address,std::uint8_t value) {
        if(address!=0xc004)return;++requests;
        check(value==(*rom)[0x1793],"registered award requested the wrong sound");
        check(source.memory().mapping(source.cpu().pc.uint16_value)==Memory::Mapping{Memory::Kind::rom,0},
              "registered award lost its external request origin");
        audio.request(source.cycles(),value);native.at(start+(source.cycles()-start)/4);
        native.memory.write(address,value);native.call(0x82ef);
    });
    check(requests==1 && routines.find({0,0x1774})->counters.cpp_calls==1,
          "registered experience segment did not expose its sound write");
    sound_state(audio,native);
}
}
int main(int argc,char** argv) {try {
    if(argc!=3)return 1;
    if(!*argv[1] || !std::filesystem::is_regular_file(argv[1]))return 77;
    std::ifstream file(argv[1],std::ios::binary);
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    const auto source=capture(rom);normal_music(source,2);normal_music(source,4);mixed_clock(source);requests(source);
    const auto root=std::filesystem::temp_directory_path()/("psmodern-tempo-audio-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto version=identify_rom(argv[1]);const auto recording=replay::read(argv[2]);
    integration(rom,version,recording,root);fade_provenance(rom,version,recording,root);registered_request(rom);
    test::remove_temporary_folder(root);
    std::cout<<"Native sound oracle: exact music PCM at 2x/4x, mixed timing, requests and unchanged gameplay passed.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
