// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/tempo_audio.h"
#include "game/routines.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include <algorithm>
#include <stdexcept>

namespace psmodern::app {
namespace {
std::unique_ptr<Machine> sound_engine(const Machine& source) {
    const auto& image=source.memory().image();
    // The C++ sound driver belongs to the identified game; an unknown image has
    // no independent sound (the frame driver then mutes accelerated audio).
    try { (void)identify_digest(sha1(*image)); }
    catch(const std::runtime_error&) { throw std::runtime_error("Independent sound needs an identified ROM."); }
    auto engine=std::make_unique<Machine>(image);
    // Its picture is never shown: switch the video chip's display and frame
    // interrupt off (a status read first clears a half-written control pair).
    engine->read_port(0xbf,engine->cycles());
    engine->write_port(0xbf,0x00,engine->cycles());engine->write_port(0xbf,0x81,engine->cycles());
    engine->take_psg_writes();engine->take_samples();
    // The game's whole memory, with the sound driver's bank in slot 2.
    engine->memory()=source.memory();
    engine->memory().write(0xfffc,0);engine->memory().write(0xffff,12);
    return engine;
}
}
TempoAudio::TempoAudio(const Machine& source)
    :engine_(sound_engine(source)),psg_(source.psg()),
     cycle_(source.cycles()),source_cycle_(cycle_),quarter_cycle_(cycle_*4),first_sample_(0) {
    game::register_sound(routines_);
    fresh_=engine_->cpu();
    // Keep the unsubmitted prefix, including its original sample phase.
    auto prefix=psg_.take_samples();first_sample_=prefix.first_cycle;
    samples_.insert(samples_.end(),prefix.values.begin(),prefix.values.end());
    psg_.take_writes();
    call(0x8043);++ticks_;
}
void TempoAudio::speed(unsigned value) {
    if(value!=1 && value!=2 && value!=4)throw std::invalid_argument("Invalid sound speed.");
    speed_=value;
}
void TempoAudio::collect() {
    auto next=psg_.take_samples();
    samples_.insert(samples_.end(),next.values.begin(),next.values.end());
    psg_.take_writes();
    if(samples_.size()>Psg::max_samples)throw std::runtime_error("Independent sound exceeded its output bound.");
}
void TempoAudio::move_to(std::uint64_t source_cycle) {
    if(source_cycle<source_cycle_)throw std::logic_error("Sound events are out of order.");
    quarter_cycle_+=(source_cycle-source_cycle_)*(4/speed_);source_cycle_=source_cycle;
    const auto target=quarter_cycle_/4;
    if(target>cycle_) {psg_.sync(target);cycle_=target;collect();}
}
void TempoAudio::call(std::uint16_t entry) {
    // One call of the sound driver, as the game's interrupt handler makes it:
    // the dispatcher's alternate HL points at the sound records, the stack is
    // a private area outside them, interrupts stay disabled and no game code
    // runs. The engine's own clock only measures the call; its sound chip
    // only times the writes, which are replayed on the presentation clock.
    auto& engine=*engine_;
    auto& cpu=engine.cpu();
    cpu=fresh_;
    cpu.pc.uint16_value=entry;cpu.sp.uint16_value=0xdfee;cpu.hl_.uint16_value=0xc00c;
    engine.memory().write(0xdfee,static_cast<std::uint8_t>(call_return));
    engine.memory().write(0xdfef,static_cast<std::uint8_t>(call_return>>8));
    const auto start=engine.cycles();
    Console::PortTrace ports(engine);
    Console::WriteTrace writes(engine);
    // (A step that leaves an instruction unfinished keeps the PC inside it,
    // so it neither ends the call nor leaves the bank.)
    while(cpu.pc.uint16_value!=call_return) {
        if(engine.cycles()-start>=Machine::frame_cycles)throw std::runtime_error("Independent sound routine exceeded its cycle bound.");
        const auto pc=cpu.pc.uint16_value;
        if(pc<0x8000 || pc>=0xc000)throw std::runtime_error("Independent sound left its ROM bank.");
        engine.step(routines_);
    }
    if(cpu.sp.uint16_value!=0xdff0)throw std::runtime_error("Independent sound returned with an invalid stack.");
    // The driver writes only its records and its stack, and only the sound
    // chip's port. Reads reach the private copy of the game's memory.
    for(const auto& write:writes.writes())
        if(!((write.address>=0xc000 && write.address<0xc200) || (write.address>=0xdf00 && write.address<0xdff0)))
            throw std::runtime_error("Independent sound wrote outside its verified memory.");
    for(const auto& access:ports.accesses())
        if(!access.write || access.port!=0x7f)throw std::runtime_error("Independent sound used an unexpected port.");
    for(const auto& write:engine.take_psg_writes())psg_.write(write.byte,cycle_+(write.cycle-start));
    engine.take_samples();
    cycle_+=engine.cycles()-start;
    psg_.sync(cycle_);collect();
}
void TempoAudio::request(std::uint64_t source_cycle,std::uint8_t request) {
    move_to(source_cycle);
    // Deliver every consumed request in order, without accelerating sequence
    // timing or building a queue of stale effects. This is an optional host
    // scheduling policy; the ROM still decides channel priorities.
    if(request!=0x80) {engine_->memory().write(0xc004,request);call(0x82ef);}
}
void TempoAudio::update(std::uint64_t source_cycle,std::uint8_t command) {
    request(source_cycle,command);
    quarter_ticks_+=4/speed_;
    if(quarter_ticks_>=4) {
        quarter_ticks_-=4;
        call(0x8043);++ticks_;
    }
}
void TempoAudio::initialize(std::uint64_t source_cycle) {
    move_to(source_cycle);call(0x8000);
}
void TempoAudio::silence(std::uint64_t source_cycle) {move_to(source_cycle);call(0x801f);}
PsgSamples TempoAudio::finish(std::uint64_t source_cycle) {
    move_to(source_cycle);
    PsgSamples result{first_sample_,{}};
    // A call can end beyond the presentation watermark. Retain those future
    // samples until due instead of emitting a burst or moving backward.
    while(!samples_.empty() && first_sample_<=quarter_cycle_/4) {
        result.values.push_back(samples_.front());samples_.pop_front();first_sample_+=Psg::sample_cycles;
    }
    return result;
}
}
