// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "oracle/native_machine.h"
#include "game/routines.h"
#include "hw/initial_state.h"
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

// The original whole driver, executed by the oracle's native machine (the
// pinned core, no C++ segment) on a copy of the game's console. Unlike
// TempoAudio, this oracle has no speed, request forwarding, tick division or
// output bound, and its time runs on the game's clock.
class NativeSound {
public:
    explicit NativeSound(const Console& source) : machine_(source) {
        machine_.set_input({});machine_.cpu().request=0;
        const auto prefix=machine_.take_samples();first_=prefix.first_cycle;
        pending_=prefix.values;machine_.take_psg_writes();
    }
    void at(std::uint64_t target) {
        if(target>machine_.cycles()) { machine_.advance(target-machine_.cycles());collect(); }
    }
    void call(unsigned entry) {
        // Each call from the power-on register file, interrupts disabled,
        // the dispatcher's alternate HL on the sound records.
        auto& cpu=machine_.cpu();initial_state::cpu(cpu);cpu.request=0;
        cpu.pc.uint16_value=static_cast<std::uint16_t>(entry);cpu.sp.uint16_value=0xdfee;cpu.hl_.uint16_value=0xc00c;
        machine_.memory().write(0xdfee,0);machine_.memory().write(0xdfef,0);
        const auto start=machine_.cycles();
        Console::PortTrace ports(machine_);
        do {machine_.step();check(machine_.cycles()-start<Machine::frame_cycles,"native sound oracle did not return");}
        while(cpu.pc.uint16_value || cpu.resume);
        for(const auto& access:ports.accesses())check(access.write && access.port==0x7f,"native sound used a port other than the sound chip");
        collect();
    }
    PsgSamples until(std::uint64_t target) {
        at(target);PsgSamples out{first_+used_*Psg::sample_cycles,{}};
        const auto count=target<first_ ? 0 : (target-first_)/Psg::sample_cycles+1;
        check(count<=pending_.size(),"native oracle lacks due samples");
        out.values.assign(pending_.begin()+used_,pending_.begin()+count);used_=count;
        return out;
    }
    const Memory& memory() const { return machine_.memory(); }
    Memory& memory() { return machine_.memory(); }
private:
    void collect() {const auto next=machine_.take_samples();pending_.insert(pending_.end(),next.values.begin(),next.values.end());machine_.take_psg_writes();}
    oracle::NativeMachine machine_;
    std::uint64_t first_=0;std::size_t used_=0;
    std::vector<std::int16_t> pending_;
};
// The driver's initialization run by the engine with the sound segments.
Machine capture(std::shared_ptr<const Memory::Rom> rom) {
    Machine source(rom);source.memory().write(0xfffc,0);source.memory().write(0xffff,12);
    auto& cpu=source.cpu();cpu.pc.uint16_value=0x8000;cpu.sp.uint16_value=0xdfee;
    source.memory().write(0xdfee,0);source.memory().write(0xdfef,0);
    RoutineRegistry sound;game::register_sound(sound);
    while(cpu.pc.uint16_value || cpu.resume)source.step(sound);
    // This pending music request belongs to the first update captured by the
    // constructor, including the unsubmitted initialization samples before it.
    source.memory().write(0xc004,0x81);cpu.pc.uint16_value=0x8043;
    return source;
}
void sound_state(const TempoAudio& audio,const NativeSound& native) {
    for(unsigned address=0xc000;address<0xc200;++address)
        check(audio.memory().read(address)==native.memory().read(address),"sound records differ from the whole native driver");
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
    // The private engine ran the C++ sound segments and nothing else: it has
    // no core (milestone 14), so any instruction no segment renders would have
    // stopped it. Every call ended at a declared exit.
    std::uint64_t calls=0,ended=0;
    for(const auto& [entry,routine]:audio.routines().entries()) {
        calls+=routine.counters.cpp_calls;
        for(const auto count:routine.rendered)ended+=count;
    }
    check(calls>0 && ended==calls,"the independent sound did not run only C++ sound segments");
    std::cout<<"Independent sound engine at "<<speed<<"x: "<<calls<<" segment calls, all rendered.\n";
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
                audio.request(source_cycle,command);native.memory().write(0xc004,command);native.call(0x82ef);
            }
        }
        if(frame==17){audio.silence(source_cycle);native.call(0x801f);}
        if(frame==23){audio.initialize(source_cycle);native.call(0x8000);}
        if(frame==24){audio.request(source_cycle,0x81);native.memory().write(0xc004,0x81);native.call(0x82ef);}
        check(audio.ticks()==ticks,"request, initialization or silence added a timing tick");
        sound_state(audio,native);
        check(audio.finish(source_cycle)==native.until(target),"commands were delayed, reordered or replayed from a backlog");
    }
}
void integration(std::shared_ptr<const Memory::Rom> rom,RomVersion version,const replay::Recording& recording,
                 const std::filesystem::path& root) {
    app::FrameDriver normal(rom,app::SaveStore(root/"normal",version));
    app::FrameDriver fast(rom,app::SaveStore(root/"fast",version));
    app::FrameDriver recorded(rom,app::SaveStore(root/"recorded",version),{},true);
    bool nonzero=false,saw_playback=false;std::size_t playback_samples=0;
    // Milestone 14: a driver's sound for a frame is taken at its frame
    // observer (the frame end), which a run can pass.
    std::uint64_t fast_frame_end=0;
    fast.set_frame_probe([&](const Machine& m) { fast_frame_end=m.cycles(); });
    for(unsigned frame=0;frame<1400;++frame) {
        const unsigned speed=frame<100 ? 1 : frame<500 ? 2 : frame<1100 ? 4 : 1;
        fast.speed(speed);recorded.speed(4);
        const auto input=recording.input_at(frame);
        // The console as the first capture sees it (the update at 0C:8043 runs
        // inside the interrupt handler, possibly nested in a segment, where a
        // hook must not copy the Machine: the oracle copies the console).
        std::optional<NativeSound> first;std::uint64_t first_cycle=0;
        if(frame==100) {
            for(auto* driver:{&normal,&fast,&recorded})driver->machine().memory().write(0xc004,0x81);
            Machine observed(normal.machine());
            observed.run_frame(input,normal.routines(),[&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
                if(!first && pc==0x8043 && mapping==Memory::Mapping{Memory::Kind::rom,12}) {
                    check(observed.peek(0xc004)==0x81,"first capture did not retain its pending music request");
                    first.emplace(observed);first_cycle=observed.cycles();
                }
                return {};
            });
            check(first.has_value(),"no first capture");
        }
        const auto a=normal.run(input),b=fast.run(input),c=recorded.run(input);
        check(!a.playback && !c.playback,"original or recorded run created independent audio");
        check(!a.audio_error && !b.audio_error && !c.audio_error,"independent audio reported an error");
        check(a.writes==b.writes && a.samples==b.samples && a.writes==c.writes && a.samples==c.samples,
              "presentation speed changed emulated PSG writes or original samples");
        check(normal.machine().equivalent(fast.machine()) && normal.machine().equivalent(recorded.machine()),
              "presentation speed changed gameplay machine state");
        if(frame<100)check(!b.playback,"1x created an independent sound path");
        if(first) {
            first->call(0x8043);
            const auto target=first_cycle+(fast_frame_end-first_cycle)/2;
            check(b.playback && *b.playback==first->until(target),"first pending request was lost or relayed twice");
        }
        if(b.playback){saw_playback=true;playback_samples+=b.playback->values.size();for(auto v:b.playback->values)nonzero|=v!=0;}
    }
    check(saw_playback && nonzero && playback_samples>200000,"accelerated integration produced no music");
    check(!fast.power_cycle(),"power cycle could not flush save");
    const auto reset=fast.run({});check(!reset.playback && !reset.audio_error,"power cycle retained an accelerated sound timeline");
}
// The host-observed sound events (the four watched entries, at the cycle and
// with the request byte the hook sees) and the external sound requests (C004
// written outside the driver's bank, which the frame driver relays), in order.
using Event=std::array<std::uint64_t,4>;
bool sound_entry(std::uint16_t pc,Memory::Mapping mapping) {
    return (mapping==Memory::Mapping{Memory::Kind::rom,0} && (pc==0x02ed || pc==0x02f5)) ||
           (mapping==Memory::Mapping{Memory::Kind::rom,12} && (pc==0x8043 || pc==0x82ef));
}
// The original's events: the oracle's native machine stepped one instruction
// at a time, an event at every instruction boundary where the host's hook
// would be called (no interrupt response due there).
void oracle_step(oracle::NativeMachine& m,std::vector<Event>& events) {
    m.settle();
    const auto pc=m.cpu().pc.uint16_value;const auto mapping=m.memory().mapping(pc);
    if(!m.cpu().resume && m.pending_acceptance()==Console::Acceptance::none && sound_entry(pc,mapping))
        events.push_back({pc,m.cycles(),mapping.bank,m.peek(0xc004)});
    Console::WriteTrace writes(m);
    m.step();
    for(const auto& write:writes.writes())
        if((write.address&0x1fff)==4 && m.memory().mapping(write.address).kind==Memory::Kind::ram &&
           mapping!=Memory::Mapping{Memory::Kind::rom,12})
            events.push_back({0xc004,0,mapping.bank,write.value});
}
// Compare the host-observed sound event sequence and the application output
// of the frame driver with the original (the oracle), frame by frame. The
// same inputs cover normal and accelerated playback, a request and two Pause
// edges. (Before milestone 14 the original was a frame driver with its
// registry disabled; a Machine is no longer an independent original.)
void observed_sound_events(std::shared_ptr<const Memory::Rom> rom,RomVersion version,
                           const replay::Recording& recording,const std::filesystem::path& root) {
    app::FrameDriver replaced(rom,app::SaveStore(root/"events-cpp",version));
    oracle::NativeMachine original(replaced.machine());
    std::array<unsigned,4> totals{};
    std::uint64_t compared=0,saves=0,playback_frames=0;
    for(unsigned frame=0;frame<400;++frame) {
        const unsigned speed=frame<100 ? 1 : frame<250 ? 2 : 4;
        replaced.speed(speed);
        auto input=recording.input_at(frame);
        input.pause=(frame>=150 && frame<154) || (frame>=200 && frame<204);
        // Both machines start every frame at the same boundary (checked below).
        if(frame==50) {
            replaced.machine().memory().write(0xc004,0x81);
            original.memory().write(0xc004,0x81);
        }
        if (frame==260 || frame==300) {
            auto change=[&](Console& machine) {
                machine.cpu().pc.uint16_value=frame==260 ? 0x02f5 : 0x02ed;
                machine.cpu().sp.uint16_value=0xdf00;
                machine.cpu().iff1=machine.cpu().iff2=0;
                machine.memory().write(0xdf00,0x55);
                machine.memory().write(0xdf01,0);
            };
            change(replaced.machine());change(original);
        }
        // The engine's events: a copy of the driver's machine runs the frame
        // with a passive hook and a write observer, as the driver's own run.
        Machine shadow(replaced.machine());
        std::vector<Event> cpp_events,native_events;
        shadow.run_frame(input,replaced.routines(),[&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
            if(sound_entry(pc,mapping)) cpp_events.push_back({pc,shadow.cycles(),mapping.bank,shadow.peek(0xc004)});
            return {};
        },[&](std::uint16_t address,std::uint8_t value) {
            const auto writer=shadow.memory().mapping(shadow.cpu().pc.uint16_value);
            if((address&0x1fff)==4 && shadow.memory().mapping(address).kind==Memory::Kind::ram &&
               writer!=Memory::Mapping{Memory::Kind::rom,12})
                cpp_events.push_back({0xc004,0,writer.bank,value});
        });
        for(const auto& event:cpp_events) {
            if(event[0]==0xc004)continue;
            const unsigned index=event[0]==0x02ed ? 0 : event[0]==0x02f5 ? 1 : event[0]==0x8043 ? 2 : 3;
            ++totals[index];
        }
        // The original to its frame end (the first instruction boundary at or
        // after the frame target), where the driver's frame observer runs.
        original.set_input(input);
        const auto target=(original.cycles()/Machine::frame_cycles+1)*Machine::frame_cycles;
        while(original.cycles()<target || original.cpu().resume==Z80::resume_xy)oracle_step(original,native_events);
        const auto writes=original.take_psg_writes();const auto samples=original.take_samples();
        const bool dirty=original.memory().cartridge_ram_dirty();original.memory().clear_cartridge_ram_dirty();
        replaced.set_frame_probe([&](const Machine& m) {
            ++compared;
            check(m.cycles()==original.cycles(),"the frame observer is not at the original's frame end");
            if (oracle::equal(m,original,oracle::Scope::whole)) return;
            std::cerr<<"sound comparison frame "<<frame<<" differences:";
            for(const auto& d:oracle::differences(m,original,oracle::Scope::whole)) std::cerr<<" "<<d;
            std::cerr<<"\n";
            throw std::runtime_error("observed sound bridge changed gameplay state");
        });
        const auto saves_before=replaced.store().writes();
        const auto a=replaced.run(input);
        replaced.set_frame_probe({});
        check(a.writes==writes && a.samples==samples,"observed sound bridge changed the frame's PSG writes or samples");
        check(!a.audio_error && !a.save_error,"observed sound fixture reported an error");
        check(replaced.store().writes()==saves_before+(dirty ? 1 : 0),"the save was not written exactly when the original's cartridge RAM changed");
        saves+=dirty;
        if(speed>1) { check(a.playback.has_value(),"accelerated frame without playback");playback_frames+=!a.playback->values.empty(); }
        else check(!a.playback,"normal speed created independent audio");
        // The original to where the driver's run ended; the whole machines
        // and the sound written since the frame end agree there.
        while(original.cycles()<replaced.machine().cycles())oracle_step(original,native_events);
        check(original.cycles()==replaced.machine().cycles(),"the original has no instruction boundary where the driver's run ended");
        check(oracle::equal(replaced.machine(),original,oracle::Scope::whole) &&
              replaced.machine().psg().writes()==original.psg().writes(),"the driver's machine differs from the original after the frame");
        if(cpp_events!=native_events) {
            std::cerr<<"frame "<<frame<<": engine";for(const auto& e:cpp_events)std::cerr<<" "<<std::hex<<e[0]<<"@"<<std::dec<<e[1]<<"/"<<e[3];
            std::cerr<<"\noriginal";for(const auto& e:native_events)std::cerr<<" "<<std::hex<<e[0]<<"@"<<std::dec<<e[1]<<"/"<<e[3];
            std::cerr<<"\n";
            throw std::runtime_error("observed sound event order, cycle or request differs");
        }
    }
    check(compared==400,"the frame observer did not run every frame");
    check(totals[0] && totals[1] && totals[2] && totals[3],"sound event fixture missed an observed entry");
    check(playback_frames>0,"accelerated frames produced no playback");
    std::cout<<"Observed sound events: 400 frames equal to the original, "<<totals[2]<<" updates, "<<totals[3]<<" requests, "<<saves<<" saves.\n";
}
void fade_provenance(std::shared_ptr<const Memory::Rom> rom,RomVersion version,
                     const replay::Recording& recording,const std::filesystem::path& root) {
    app::FrameDriver driver(rom,app::SaveStore(root/"fade",version));
    for(unsigned frame=0;frame<100;++frame)driver.run(recording.input_at(frame));
    driver.machine().memory().write(0xc004,0x81);driver.speed(4);
    std::unique_ptr<NativeSound> native;std::uint64_t start=0;unsigned phase=0,internal_resets=0;
    // Milestone 14: the frame's playback ends at the frame observer.
    std::uint64_t frame_end=0;
    driver.set_frame_probe([&](const Machine& m) { frame_end=m.cycles(); });
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
        check(*out.playback==native->until(start+(frame_end-start)/4),
              "gameplay driver's internal fade/reset reached the independent music clock");
    }
    check(internal_resets>0,"fade fixture never reached the native internal reset");
}
void registered_request(std::shared_ptr<const Memory::Rom> rom) {
    Machine source=capture(rom);const auto start=source.cycles();TempoAudio audio(source);audio.speed(4);
    NativeSound native(source);native.call(0x8043);
    const auto text=static_cast<std::uint16_t>((*rom)[0x179b]|((*rom)[0x179c]<<8));
    // Finish the bounded award segment, then wait without invoking text: the
    // game's registry with a test stub at the text routine's entry that goes
    // on at the frame wait (00:0055) instead, running nothing (a host hook
    // sees only observed entries since milestone 14, so the redirect is a stub).
    RoutineRegistry game_routines;game::register_routines(game_routines,sha1(*rom),{});
    RoutineRegistry routines;
    for(const auto& [entry,routine]:game_routines.entries()) {
        if(entry==RoutineEntry{0,text}) continue;
        if(routine.guard) routines.add_exact(entry,routine.spec,*routine.guard,routine.bound,routine.body);
        else if(routine.exact) routines.add_exact(entry,routine.spec,routine.bound,routine.body);
        else routines.add(entry,routine.spec,routine.bound,routine.body);
    }
    SegmentSpec stub{{0,text},{SegmentExit{RoutineExit::core_final,text}}};stub.single_instruction=true;
    routines.add({0,text},stub,[](const Machine&){return std::uint64_t{1};},
                 [text](RoutineContext& c){c.finish({RoutineExit::core_final,text},0x0055);});
    auto& cpu=source.cpu();cpu.pc.uint16_value=0x1774;cpu.hl.uint16_value=0xffff;
    cpu.af.uint8_values.at_0&=0xfe;cpu.ix_iy[0].uint16_value=0xc600;cpu.ix_iy[1].uint16_value=0xc400;
    source.memory().write(0xc208,1); // Keep the interrupt-disabled wait busy.
    unsigned requests=0;
    source.run_frame({},routines,{},[&](std::uint16_t address,std::uint8_t value) {
        if(address!=0xc004)return;++requests;
        check(value==(*rom)[0x1793],"registered award requested the wrong sound");
        check(source.memory().mapping(source.cpu().pc.uint16_value)==Memory::Mapping{Memory::Kind::rom,0},
              "registered award lost its external request origin");
        audio.request(source.cycles(),value);native.at(start+(source.cycles()-start)/4);
        native.memory().write(address,value);native.call(0x82ef);
    });
    check(requests==1 && routines.find({0,0x1774})->counters.cpp_calls==1,
          "registered experience segment did not expose its sound write");
    sound_state(audio,native);
}
}
int main(int argc,char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
    if(argc!=3)return 1;
    if(!*argv[1] || !std::filesystem::is_regular_file(argv[1]))return 77;
    std::ifstream file(argv[1],std::ios::binary);
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    const auto source=capture(rom);normal_music(source,2);normal_music(source,4);mixed_clock(source);requests(source);
    const auto root=std::filesystem::temp_directory_path()/("psmodern-tempo-audio-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto version=identify_rom(argv[1]);const auto recording=replay::read(argv[2]);
    integration(rom,version,recording,root);observed_sound_events(rom,version,recording,root);fade_provenance(rom,version,recording,root);registered_request(rom);
    test::remove_temporary_folder(root);
    std::cout<<"Native sound oracle: exact music PCM at 2x/4x, mixed timing, requests and unchanged gameplay passed.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
