// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/tempo_audio.h"
#include <algorithm>
#include <stdexcept>

namespace psmodern::app {
TempoAudio::TempoAudio(const Machine& source)
    :memory_(source.memory()),cpu_(*this),psg_(source.psg()),
     cycle_(source.cycles()),source_cycle_(cycle_),quarter_cycle_(cycle_*4),first_sample_(0) {
    memory_.write(0xfffc,0);memory_.write(0xffff,12);
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
    // Private stack is outside all sound records; no interrupts or game code
    // execute here. The dispatcher uses the same alternate-HL convention as
    // the full update. Reset CPU bookkeeping between bounded native calls.
    cpu_=Cpu(*this);
    auto& cpu=cpu_.raw();cpu.pc.uint16_value=entry;cpu.sp.uint16_value=0xdfee;
    cpu.hl_.uint16_value=0xc00c;
    memory_.write(0xdfee,0);memory_.write(0xdfef,0);
    const auto start=cycle_;
    while(cpu.pc.uint16_value!=0 || cpu.resume) {
        if(cycle_-start>=Machine::frame_cycles)throw std::runtime_error("Independent sound routine exceeded its cycle bound.");
        cycle_+=cpu_.step(cycle_);
    }
    if(cpu.sp.uint16_value!=0xdff0)throw std::runtime_error("Independent sound returned with an invalid stack.");
    psg_.sync(cycle_);collect();
}
void TempoAudio::request(std::uint64_t source_cycle,std::uint8_t request) {
    move_to(source_cycle);
    // Deliver every consumed request in order, without accelerating sequence
    // timing or building a queue of stale effects. This is an optional host
    // scheduling policy; the ROM still decides channel priorities.
    if(request!=0x80) {memory_.write(0xc004,request);call(0x82ef);}
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
    // A native call can end beyond the presentation watermark. Retain those
    // future samples until due instead of emitting a burst or moving backward.
    while(!samples_.empty() && first_sample_<=quarter_cycle_/4) {
        result.values.push_back(samples_.front());samples_.pop_front();first_sample_+=Psg::sample_cycles;
    }
    return result;
}
std::uint8_t TempoAudio::opcode(std::uint16_t address) {
    if(address<0x8000 || address>=0xc000)throw std::runtime_error("Independent sound left its ROM bank.");
    return memory_.read(address);
}
std::uint8_t TempoAudio::read(std::uint16_t address) {
    // Some command data pointers reach the fixed ROM bank as well.
    if(address<0xc200 || (address>=0xdf00 && address<0xdff0))return memory_.read(address);
    throw std::runtime_error("Independent sound read outside its verified memory.");
}
void TempoAudio::write(std::uint16_t address,std::uint8_t value) {
    if((address>=0xc000 && address<0xc200) || (address>=0xdf00 && address<0xdff0)) {memory_.write(address,value);return;}
    throw std::runtime_error("Independent sound wrote outside its verified memory.");
}
std::uint8_t TempoAudio::input(std::uint16_t,std::uint64_t) {throw std::runtime_error("Unexpected independent sound port read.");}
void TempoAudio::output(std::uint16_t port,std::uint8_t value,std::uint64_t cycle) {
    if((port&0xff)!=0x7f)throw std::runtime_error("Independent sound wrote an unexpected port.");
    psg_.write(value,cycle);
}
}
