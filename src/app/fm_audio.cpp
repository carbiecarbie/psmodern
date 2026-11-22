// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/fm_audio.h"
#include "game/japanese_sound.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include <algorithm>
#include <stdexcept>

namespace psmodern::app {
namespace {
constexpr std::uint16_t request_byte=0xc004,return_address=0xdff0;
bool driver_ram(std::uint16_t a) {
    return (a>=0xc000 && a<=0xc1ee) || (a>=0xdf00 && a<return_address);
}
}
FmAudio::FmAudio(std::shared_ptr<const Memory::Rom> image) {
    if (!image || identify_cartridge_digest(sha1(*image))!=CartridgeKind::japanese_sound)
        throw std::invalid_argument("The FM sound driver requires the identified Japanese cartridge.");
    engine_=std::make_unique<Machine>(std::move(image));
    auto& memory=engine_->memory();
    memory.write(0xffff,12);
    memory.write(0xc000,1);
    engine_->read_port(0xbf,0);
    engine_->write_port(0xbf,0,0);
    engine_->write_port(0xbf,0x81,0);
    engine_->set_read_guard([](std::uint16_t address){
        if (address>=0xc000 && !driver_ram(address))
            throw std::runtime_error("FM driver read outside its work RAM or stack.");
    });
    game::register_japanese_sound(routines_);
    caller_=engine_->cpu();
    // The full update sets these before dispatch. Direct requests between
    // divided ticks enter with the same alternate pointer and FM condition.
    caller_.hl_.uint16_value=0xc00c;
    caller_.af_.uint16_value=0x0100;
}
void FmAudio::advance(std::uint64_t cycle) {
    if(cycle<source_)throw std::logic_error("FM events are out of order.");
    quarters_+=(cycle-source_)*(4/speed_);
    source_=cycle;
}
void FmAudio::speed(unsigned speed,std::uint64_t cycle) {
    if(speed!=1 && speed!=2 && speed!=4)throw std::invalid_argument("Invalid FM speed.");
    advance(cycle);
    speed_=speed;
}
void FmAudio::call(std::uint16_t entry,std::uint64_t when) {
    auto& m=*engine_;
    auto& r=m.cpu();
    r=caller_;
    r.pc.uint16_value=entry;
    r.sp.uint16_value=return_address-2;
    m.memory().write(return_address-2,static_cast<std::uint8_t>(return_address));
    m.memory().write(return_address-1,return_address>>8);
    Console::PortTrace ports(m);
    Console::WriteTrace memory(m);
    const auto start=m.cycles();
    while(r.pc.uint16_value!=return_address) {
        if(m.memory().mapping(r.pc.uint16_value)!=Memory::Mapping{Memory::Kind::rom,12})
            throw std::runtime_error("FM driver left its sound bank.");
        m.step(routines_);
        if(m.cycles()-start>Machine::frame_cycles)
            throw std::runtime_error("FM driver exceeded one frame.");
    }
    if(r.sp.uint16_value!=return_address)throw std::runtime_error("Invalid FM driver return stack.");
    for(const auto& w:memory.writes())
        if(!driver_ram(w.address))throw std::runtime_error("FM driver wrote outside its work RAM or stack.");
    // An effect may arrive before the previous call would have returned on
    // the listener clock. Serialize the two calls, preserving each bus cycle.
    const auto begin=std::max(call_end_,when);
    for(auto access:ports.accesses()) {
        if(!access.write || (access.port!=0x7f && access.port!=0xf0 && access.port!=0xf1))
            throw std::runtime_error("FM driver used an unexpected port.");
        if(access.port==0x7f)continue; // The Japanese audio gate selects FM alone.
        access.cycle=begin+(access.cycle-start);
        writes_.push_back(access);
    }
    call_end_=begin+(m.cycles()-start);
    m.take_psg_writes();
    m.take_samples();
    if(writes_.size()>Psg::max_writes)throw std::runtime_error("FM sound exceeded its output bound.");
}
void FmAudio::begin_tick(std::uint64_t cycle) {
    advance(cycle);
    if(tick_)throw std::logic_error("Unconsumed FM update.");
    tick_quarters_+=4/speed_;
    if(tick_quarters_>=4) {
        tick_quarters_-=4;
        if(speed_==1)tick_=quarters_/4;
        else call(0x8052,quarters_/4);
    }
}
void FmAudio::consume(std::uint64_t cycle,std::optional<std::uint8_t> external) {
    advance(cycle);
    if(external)engine_->memory().write(request_byte,*external);
    if(tick_) {
        // The US fade has now had an opportunity to replace the external
        // write. The JP driver receives the survivor inside its native tick
        // at the original update timestamp, before it advances its voices.
        call(0x8052,*tick_);
        tick_.reset();
    } else if(external && *external!=0x80)call(0x8466,quarters_/4);
}
void FmAudio::initialize(std::uint64_t cycle) { advance(cycle);call(0x8000,quarters_/4); }
void FmAudio::silence(std::uint64_t cycle) { advance(cycle);call(0x801f,quarters_/4); }
PsgSamples FmAudio::finish(std::uint64_t cycle) {
    advance(cycle);
    // If the frame observer falls between update and dispatch, the first
    // writes of the pending tick are still unknown. Retain that short tail.
    const auto due=tick_.value_or(quarters_/4);
    while(!writes_.empty() && writes_.front().cycle<=due) {
        const auto w=writes_.front();
        chip_.write(w.port,w.value,w.cycle);
        writes_.pop_front();
    }
    chip_.sync(due);
    return chip_.take_samples();
}
}
