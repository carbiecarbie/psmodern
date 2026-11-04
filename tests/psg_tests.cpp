// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/psg.h"
#include "machine/machine.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace psmodern;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void period(Psg& p,unsigned channel,unsigned value) {
    p.write(0x80|(channel<<5)|(value&15),p.cycle());
    p.write((value>>4)&63,p.cycle());
}
void startup() {
    Psg p;
    check(p.periods()==std::array<std::uint16_t,3>{} && p.timers()==std::array<std::uint16_t,3>{},"initial tone periods and timers");
    check(p.outputs()==std::array<bool,3>{} && p.attenuations()==std::array<std::uint8_t,4>{15,15,15,15},"initial tone outputs and attenuation");
    check(p.lfsr()==0x8000 && p.noise_timer()==0 && !p.noise_clock() && !p.noise_output() &&
          p.noise_control()==0 && p.latch()==0 && p.cycle()==0 && p.sample()==0,"initial noise and clock state");
    check(p.writes().empty() && p.take_samples()==PsgSamples{16,{}},"initial outputs empty");
}
void protocol() {
    Psg p; p.write(0x3f,0); // initial latch is tone 0
    check(p.periods()[0]==0x3f0,"data without previous latch uses initial tone 0 latch");
    for (unsigned ch=0;ch<3;++ch) {
        period(p,ch,0x2ae);
        check(p.periods()[ch]==0x2ae && p.latch()==2*ch,"10-bit tone register and latch");
        p.write(0x80|(ch<<5)|3,0);
        check(p.periods()[ch]==0x2a3,"tone latch replaces low nibble immediately");
        p.write(0x7f,0); check(p.periods()[ch]==0x3f3,"tone data masks bit 6 and retains low nibble");
        p.write(0,0); check(p.periods()[ch]==3,"successive data preserves latch");
    }
    for (unsigned ch=0;ch<4;++ch) {
        p.write(0x90|(ch<<5)|15,0);
        check(p.attenuations()[ch]==15 && p.latch()==ch*2+1,"all volume latches");
        p.write(0x72,0); check(p.attenuations()[ch]==2,"data after volume latch uses low four bits");
        p.write(0x0a,0); check(p.attenuations()[ch]==10,"volume latch persists");
    }
    p.write(0xef,0); check(p.noise_control()==7 && p.latch()==6,"noise latch discards bit 3");
    p.write(0x7a,0); check(p.noise_control()==2 && p.latch()==6,"noise data uses low three bits");
}
void tone() {
    for (unsigned ch=0;ch<3;++ch) {
        Psg p; period(p,ch,3);
        p.sync(15); check(!p.outputs()[ch] && p.timers()[ch]==0,"no tick before cycle 16");
        p.sync(16); check(p.outputs()[ch] && p.timers()[ch]==3,"first tick flips and reloads");
        p.sync(48); check(p.outputs()[ch] && p.timers()[ch]==1,"period counts divided clocks");
        p.sync(64); check(!p.outputs()[ch] && p.timers()[ch]==3,"half wave expires after period ticks");
        period(p,ch,7); check(p.timers()[ch]==3,"tone write does not reset counter");
        p.sync(112); check(p.outputs()[ch] && p.timers()[ch]==7,"next expiration uses updated period");
    }
    for (unsigned value:{0u,1u,2u,0x3ffu}) {
        Psg p; period(p,0,value); p.write(0x90,0);
        p.sync(16); check(p.sample()==8191,"first tone output high");
        p.sync(32);
        check(p.sample()==8191,"period zero and one force +1; period two has not expired");
        if (value<=1) { p.sync(48); p.sync(64); check(p.sample()==8191,"smallest Sega periods stay +1"); }
        else {
            p.sync(16*(value+1));
            check(p.sample()==0,"full 10-bit period flips low on exact expiration");
        }
    }
}
void noise() {
    constexpr std::array<std::uint16_t,20> white{
        0x4000,0x2000,0x1000,0x0800,0x0400,0x0200,0x0100,0x0080,0x0040,0x0020,
        0x0010,0x0008,0x8004,0x4002,0x2001,0x9000,0x4800,0x2400,0x1200,0x0900};
    for (bool white_mode:{false,true}) {
        Psg p; p.write(white_mode ? 0xe4 : 0xe0,0);
        for (unsigned shift=0;shift<white.size();++shift) {
            const auto when=16+512*shift;
            p.sync(when-1);
            auto before=p.lfsr();
            p.sync(when);
            const auto expected=white_mode ? white[shift] : static_cast<std::uint16_t>(0x8000>>((shift+1)%16));
            check(p.lfsr()==expected && p.noise_output()==((expected&1)!=0),"first noise states and output bits");
            check(p.lfsr()!=before,"noise shifts on rising input edge");
            p.sync(when+256); check(p.lfsr()==expected,"falling edge does not shift");
        }
        auto timer=p.noise_timer(); auto clock=p.noise_clock(); auto output=p.noise_output();
        p.write(0x06,p.cycle()); // data, still latched to noise
        check(p.lfsr()==0x8000 && p.noise_control()==6,"data noise write resets LFSR");
        check(p.noise_timer()==timer && p.noise_clock()==clock && p.noise_output()==output,"noise control leaves divider phase and output latch alone");
        p.write(0xe6,p.cycle()); check(p.lfsr()==0x8000,"same-value latch write also resets LFSR");
    }
    for (unsigned rate=0;rate<4;++rate) {
        Psg p; period(p,2,5); p.write(0xe0|rate,0);
        unsigned half=rate==3 ? 5 : (16<<rate);
        p.sync(16); check(p.lfsr()==0x4000 && p.noise_timer()==half,"noise first rising edge and reload");
        p.sync(16+16*half); check(p.lfsr()==0x4000 && !p.noise_clock(),"noise falling edge at half period");
        p.sync(16+32*half-1); check(p.lfsr()==0x4000,"noise not early");
        p.sync(16+32*half); check(p.lfsr()==0x2000,"N/512 N/1024 N/2048 and tone2-derived shift rate");
    }
    Psg variable; period(variable,2,5); variable.write(0xe3,0); variable.sync(16);
    period(variable,2,9); variable.sync(96);
    check(variable.noise_timer()==9 && variable.lfsr()==0x4000,"tone2 noise rate takes new period at reload");
    for (unsigned small:{0u,1u}) {
        Psg p; period(p,2,small); p.write(0xe3,0); p.sync(16); p.sync(48);
        check(p.lfsr()==0x2000,"small tone2 periods still clock the noise divider");
    }
}
void attenuation() {
    constexpr std::array<std::int16_t,16> expected{8191,6506,5168,4105,3261,2590,2057,1634,1298,1031,819,651,517,411,326,0};
    check(Psg::volume_table==expected,"integer 2dB attenuation table");
    for (unsigned ch=0;ch<4;++ch) {
        Psg p;
        if (ch==3) { p.write(0xe0,0); p.sync(16+14*512); check(p.noise_output(),"noise high for mixer test"); }
        for (unsigned a=0;a<16;++a) {
            p.write(0x90|(ch<<5)|a,p.cycle());
            check(p.sample()==expected[a],"each channel uses all attenuation levels, including off");
        }
    }
    Psg p; p.write(0xe0,0); p.sync(16+14*512);
    for (unsigned ch=0;ch<4;++ch) p.write(0x90|(ch<<5),p.cycle());
    check(p.sample()==32764,"four full volume channels sum without clipping or overflow");
}
std::shared_ptr<const Memory::Rom> program(std::initializer_list<std::uint8_t> bytes) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    std::copy(bytes.begin(),bytes.end(),rom->begin()); return rom;
}
void timing_and_replay() {
    auto rom=program({0x3e,0x90,0xd3,0x7f,0x3e,0x9f,0xd3,0x40,0xc3,0,0});
    Machine m(rom); m.step(); // snapshot in a partial divide-by-16 interval
    auto copy=m; Machine assigned(program({0})); assigned=m;
    auto replay=m.psg();
    while (m.cycles()<2000) { m.step(); copy.step(); assigned.step(); }
    check(m.equivalent(copy) && m.equivalent(assigned),"snapshot includes chip, writes and samples");
    auto writes=m.take_psg_writes();
    check(writes.size()>2 && writes[0]==PsgWrite{17,0x90} && writes[1]==PsgWrite{35,0x9f},"PSG events stamped at OUT T3 on both mirror ports");
    for (auto event:writes) replay.write(event.byte,event.cycle);
    replay.sync(m.cycles());
    auto samples=m.take_samples();
    check(samples==copy.take_samples() && samples==assigned.take_samples() && samples==replay.take_samples(),"live, copied, assigned and log-replayed samples identical");
    check(samples.first_cycle==16 && samples.values[0]==0 && samples.values[1]==8191 && samples.values[2]==0,"T3 write affects the following sample");
    check(writes==copy.take_psg_writes() && writes==assigned.take_psg_writes() && writes==replay.take_writes(),"identical write logs");
    check(m.psg()==replay,"replayed chip has identical complete state");
    check(m.equivalent(copy) && m.equivalent(assigned),"matching drains restore machine equality");
    Machine stepped(program({0})), advanced(program({0}));
    for (auto* machine:{&stepped,&advanced}) {
        machine->write_port(0x40,0x83,0); machine->write_port(0x7e,0,0); machine->write_port(0x7f,0x90,0);
    }
    for (unsigned i=0;i<1024;++i) stepped.step();
    advanced.advance(4096); check(stepped.psg()==advanced.psg(),"advance and NOP stepping match the PSG and all output");
    Psg tied; tied.write(0x90,16); tied.sync(32);
    check(tied.take_samples()==PsgSamples{16,{0,8191}},"divide tick and sample precede a write at the same cycle");
    Psg chunked,whole; whole.sync(501);
    for (auto cycle:{0,1,15,16,16,17,255,256,501}) chunked.sync(cycle);
    check(whole==chunked,"chunk boundaries never change samples or phase");
    auto first=chunked.take_samples(); chunked.sync(512);
    check(first.values.size()==31 && chunked.take_samples()==PsgSamples{512,{0}},"sample drains preserve absolute grid");
    auto before=chunked; bool rejected=false;
    try { chunked.write(0x90,511); } catch (const std::invalid_argument&) { rejected=true; }
    check(rejected && chunked==before,"backward event rejected without changing state");

    Psg sounding;
    period(sounding,0,7); period(sounding,1,13); period(sounding,2,5);
    for (unsigned ch=0;ch<4;++ch) sounding.write(0x90|(ch<<5)|ch,0);
    sounding.write(0xe7,0); sounding.sync(333);
    sounding.take_writes(); sounding.take_samples();
    check(sounding.timers()[0]!=0 && sounding.noise_timer()!=0 && sounding.lfsr()!=0x8000,
          "replay fixture starts with active tone and noise dividers between ticks");
    auto sounding_copy=sounding, sounding_replay=sounding;
    constexpr std::array<PsgWrite,8> events{{{336,0x95},{351,0xa9},{352,0x02},{399,0xe4},
                                            {511,0x03},{512,0xf2},{601,0xc2},{608,0x01}}};
    for (auto event:events) {
        sounding.write(event.byte,event.cycle);
        // Splitting advancement before every event must preserve its phase.
        sounding_copy.sync(event.cycle-1); sounding_copy.write(event.byte,event.cycle);
    }
    auto recorded=sounding.take_writes();
    for (auto event:recorded) sounding_replay.write(event.byte,event.cycle);
    for (auto* chip:{&sounding,&sounding_copy,&sounding_replay}) chip->sync(2049);
    auto active_samples=sounding.take_samples();
    check(active_samples==sounding_copy.take_samples() && active_samples==sounding_replay.take_samples(),
          "active tones and noise: live, snapshot and recorded-log samples match");
    check(recorded==sounding_copy.take_writes() && recorded==sounding_replay.take_writes(),
          "active replay preserves same-cycle events and their order");
    check(sounding==sounding_copy && sounding==sounding_replay,"active replay has identical complete PSG state");
}
void bound() {
    Machine m(program({0}));
    for (std::size_t i=0;i<Psg::max_writes;++i) m.write_port(0x40,0x9f,0);
    auto copy=m; bool full=false;
    try { m.write_port(0x7f,0x90,0); } catch (const std::overflow_error&) { full=true; }
    check(full && m.equivalent(copy),"overflow explicitly fails before any machine state changes");
    auto log=m.take_psg_writes();
    check(log.size()==Psg::max_writes && std::all_of(log.begin(),log.end(),[](auto e){return e==PsgWrite{0,0x9f};}),"every accepted event retained in full log");
    full=false;
    try { m.write_port(0x40,0x90,0); } catch (const std::overflow_error&) { full=true; }
    check(full && m.psg().writes().empty(),"draining cannot bypass per-frame bound");
    m.advance(Machine::frame_cycles); m.write_port(0x7f,0x90,m.cycles());
    check(m.take_psg_writes()==std::vector<PsgWrite>{{Machine::frame_cycles,0x90}},"new frame has a fresh write budget");
    Psg pending;
    for (std::size_t i=0;i<Psg::max_writes;++i) pending.write(0x9f,i*Psg::frame_cycles/Psg::max_writes);
    auto before=pending; full=false;
    try { pending.write(0x90,Psg::frame_cycles); } catch (const std::overflow_error&) { full=true; }
    check(full && before==pending,"cross-frame unread output also fails instead of growing or dropping data");
    pending.take_writes(); pending.write(0x90,Psg::frame_cycles);
    check(pending.writes().size()==1,"host drain permits next frame output");
}
}
int main() {
    try { startup(); protocol(); tone(); noise(); attenuation(); timing_and_replay(); bound();
        std::cout<<"PSG: 7 test groups passed.\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
