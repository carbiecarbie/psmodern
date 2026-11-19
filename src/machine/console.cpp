// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/console.h"
#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace psmodern {
Console::RomReplacement Console::prepare_rom_replacement(std::shared_ptr<const Memory::Rom> image) {
    if(!image || image->size()!=512*1024)throw std::invalid_argument("Expected a 512 KiB replacement ROM image");
    return RomReplacement(std::move(image));
}
Console::Console(std::shared_ptr<const Memory::Rom> rom, Memory::CartridgeRam ram)
    : memory_(std::move(rom), ram) {
    initial_state::cpu(cpu_);
    sampled_int_=vdp_.irq();
    cpu_.set_int_line(sampled_int_);
}
// Instrumentation (traces, observers) is never copied.
Console::Console(const Console& o)
    : memory_(o.memory_), cpu_(o.cpu_), vdp_(o.vdp_), psg_(o.psg_), cycle_(o.cycle_), opcode_fetches_(o.opcode_fetches_),
      scheduled_nmi_(o.scheduled_nmi_), pads_(o.pads_), pending_pads_(o.pending_pads_), io_control_(o.io_control_),
      open_bus_(o.open_bus_), reset_(o.reset_), pause_(o.pause_), pending_reset_(o.pending_reset_), pending_pause_(o.pending_pause_),
      input_latched_(o.input_latched_), sampled_int_(o.sampled_int_), sampled_nmi_(o.sampled_nmi_),
      pause_nmi_level_(o.pause_nmi_level_), last_access_(o.last_access_) {}
Console& Console::operator=(const Console& o) {
    if (this != &o) {
        memory_=o.memory_; cpu_=o.cpu_; vdp_=o.vdp_; psg_=o.psg_; cycle_=o.cycle_; opcode_fetches_=o.opcode_fetches_;
        scheduled_nmi_=o.scheduled_nmi_; pads_=o.pads_; pending_pads_=o.pending_pads_; io_control_=o.io_control_;
        open_bus_=o.open_bus_; reset_=o.reset_; pause_=o.pause_; pending_reset_=o.pending_reset_; pending_pause_=o.pending_pause_;
        input_latched_=o.input_latched_; sampled_int_=o.sampled_int_; sampled_nmi_=o.sampled_nmi_;
        pause_nmi_level_=o.pause_nmi_level_;
        last_access_=o.last_access_;
    }
    return *this;
}
Console::PortTrace::PortTrace(Console& console)
    : console_(console), previous_(console.port_trace_) {
    console_.port_trace_=&accesses_;
}
Console::PortTrace::~PortTrace() {
    console_.port_trace_=previous_;
}
Console::WriteTrace::WriteTrace(Console& console)
    : console_(console), previous_(console.write_trace_) {
    console_.write_trace_=&writes_;
}
Console::WriteTrace::~WriteTrace() {
    console_.write_trace_=previous_;
}
void Console::record_access(PortAccess access) {
    last_access_=access;
    if (port_trace_) port_trace_->push_back(access);
}
bool Console::equivalent(const Console& o) const {
    auto state=[](const Console& m) {
        return std::tie(m.cycle_,m.opcode_fetches_,m.scheduled_nmi_,m.pads_,m.pending_pads_,
                        m.io_control_,m.open_bus_,m.reset_,m.pause_,m.pending_reset_,m.pending_pause_,
                        m.input_latched_,m.sampled_int_,m.sampled_nmi_,m.pause_nmi_level_);
    };
    return state(*this)==state(o) && same_registers(cpu_,o.cpu_) && memory_==o.memory_ &&
           vdp_==o.vdp_ && psg_==o.psg_;
}
bool Console::same_sound_state(const Psg& a, const Psg& b) {
    return std::tie(a.periods_,a.timers_,a.outputs_,a.attenuations_,a.lfsr_,a.noise_timer_,a.latch_,a.noise_control_,
                    a.noise_clock_,a.noise_output_,a.cycle_,a.write_frame_,a.frame_write_count_)==
           std::tie(b.periods_,b.timers_,b.outputs_,b.attenuations_,b.lfsr_,b.noise_timer_,b.latch_,b.noise_control_,
                    b.noise_clock_,b.noise_output_,b.cycle_,b.write_frame_,b.frame_write_count_);
}
bool Console::same_memory(const Memory& a, const Memory& b) {
    return std::tie(a.rom_,a.ram_,a.cartridge_ram_,a.banks_,a.mapper_control_,a.memory_control_)==
           std::tie(b.rom_,b.ram_,b.cartridge_ram_,b.banks_,b.mapper_control_,b.memory_control_);
}
bool Console::same_inputs(const Console& o) const {
    return std::tie(scheduled_nmi_,pads_,pending_pads_,reset_,pause_,pending_reset_,pending_pause_,input_latched_)==
           std::tie(o.scheduled_nmi_,o.pads_,o.pending_pads_,o.reset_,o.pause_,o.pending_reset_,o.pending_pause_,o.input_latched_);
}
std::uint8_t Console::peek(std::uint16_t address) const {
    return memory_.mapping(address).kind==Memory::Kind::unmapped ? open_bus_ : memory_.read(address);
}

void Console::latch_input() {
    pads_=pending_pads_; reset_=pending_reset_;
    pause_=pending_pause_; input_latched_=true;
}
void Console::sync(std::uint64_t target) {
    if (target<vdp_.cycle()) throw std::invalid_argument("Machine cannot run backwards");
    psg_.check_sync(target);
    if (!input_latched_) latch_input();
    auto now=vdp_.cycle();
    while (now<target) {
        auto frame=(now/frame_cycles+1)*frame_cycles;
        auto pause_sample=(now/frame_cycles)*frame_cycles+pause_sample_offset;
        if (pause_sample<=now) pause_sample+=frame_cycles;
        auto next=std::min({target,frame,pause_sample});
        vdp_.sync(next);
        if (next==frame) latch_input();
        if (next==pause_sample) {
            if (pause_ && !pause_nmi_level_) cpu_.raise_nmi();
            pause_nmi_level_=pause_;
        }
        now=next;
    }
    vdp_.sync(target);
    psg_.sync(target);
    if (scheduled_nmi_ && *scheduled_nmi_<=target) { cpu_.raise_nmi(); scheduled_nmi_.reset(); }
}
void Console::advance(std::uint64_t cycles) {
    if (!cycles) return;
    psg_.check_sync(cycle_+cycles); // Preflight the whole interval before either sync.
    // Replacement intervals [S,E) sample at E-1, just like a native step.
    sync(cycle_+cycles-1); sampled_int_=vdp_.irq();
    sampled_nmi_=(cpu_.request & Z80::request_nmi)!=0;
    cpu_.set_int_line(sampled_int_);
    cycle_+=cycles; sync(cycle_);
}
void Console::finish_interval(std::uint64_t cycles) {
    // [S,E) samples the line at E-1, after any earlier I/O in that instruction.
    sync(cycle_+cycles-1); sampled_int_=vdp_.irq();
    sampled_nmi_=(cpu_.request & Z80::request_nmi)!=0;
    cycle_+=cycles; sync(cycle_);
}
void Console::schedule_nmi(std::uint64_t cycle) {
    if (cycle<cycle_) throw std::invalid_argument("NMI is in the past");
    scheduled_nmi_=cycle;
}
std::optional<std::uint64_t> Console::next_nmi() const {
    if (cpu_.request & Z80::request_nmi) return cycle_;
    auto next=scheduled_nmi_;
    auto frame_start=(cycle_/frame_cycles)*frame_cycles;
    auto sample=frame_start+pause_sample_offset;
    bool level=pause_nmi_level_;
    if (sample>cycle_) {
        bool input=input_latched_ ? pause_ : pending_pause_;
        if (input && !level && (!next || sample<*next)) next=sample;
        level=input;
    }
    sample=frame_start+frame_cycles+pause_sample_offset;
    if (pending_pause_ && !level && (!next || sample<*next)) next=sample;
    return next;
}
Console::Acceptance Console::pending_acceptance() const {
    const auto& cpu=cpu_;
    if (cpu.resume && cpu.resume!=Z80::resume_halt) return Acceptance::none;
    const bool unsampled_nmi=(cpu.request & Z80::request_nmi) && !sampled_nmi_;
    auto request=cpu.request & ~(unsampled_nmi ? Z80::request_nmi : 0);
    // The INT sample (Z80::set_int_line, the pinned core's z80_int at
    // Z80.c:2380-2383) clears a deasserted line, and only creates a request
    // for an asserted line when IFF1 is set.
    if (!sampled_int_) request &= ~Z80::request_int;
    else if (cpu.iff1) request |= Z80::request_int;
    // Mirrors the pinned Z80.c RETN bookkeeping (lines 1141-1146) and
    // request/RETN acceptance checks (2505-2507,2560-2618). Resumed index
    // work precedes that loop; halted work can reenter it (2443-2492).
    const bool reject_nmi=(request & Z80::request_reject_nmi)!=0;
    if (reject_nmi) return Acceptance::none;
    if (request & Z80::request_nmi) return Acceptance::nmi;
    const bool reti_delay=cpu.data.uint8_array[0]==0xed &&
        (cpu.data.uint8_array[1]&0xc7)==0x45 && !(cpu.data.uint8_array[2]&1);
    const bool irq=(request & Z80::request_int) && cpu.data.uint8_array[0]!=0xfb && !reti_delay;
    return irq ? Acceptance::irq : Acceptance::none;
}
std::uint8_t Console::opcode(std::uint16_t addr) { ++opcode_fetches_; return read(addr); }
std::uint8_t Console::read(std::uint16_t addr) {
    if (memory_.mapping(addr).kind!=Memory::Kind::unmapped) open_bus_=memory_.read(addr);
    return open_bus_;
}
void Console::write(std::uint16_t addr,std::uint8_t value) {
    memory_.write(addr,value);open_bus_=value;
    if(write_trace_)write_trace_->push_back({addr,value});
    if(write_observer_)(*write_observer_)(addr,value);
}
std::uint8_t Console::read_port(std::uint16_t raw,std::uint64_t cycle) {
    sync(cycle);
    const auto port=static_cast<std::uint8_t>(raw);
    std::uint8_t value;
    if (port<0x40) value=open_bus_;
    else if (port<0x80) value=(port&1) ? vdp_.h_counter() : vdp_.v_counter();
    else if (port<0xc0) value=(port&1) ? vdp_.read_status(open_bus_) : vdp_.read_data();
    else if (!memory_.io_enabled()) value=open_bus_;
    else {
        auto low=[](bool pressed)->unsigned { return pressed ? 0 : 1; };
        auto pin=[&](unsigned direction,unsigned output,bool input) {
            return (io_control_&(1u<<direction)) ? low(input) : ((io_control_>>output)&1u);
        };
        auto& a=pads_[0]; auto& b=pads_[1];
        if (!(port&1)) value=low(a.up)|(low(a.down)<<1)|(low(a.left)<<2)|(low(a.right)<<3)|
            (low(a.one)<<4)|(pin(0,4,a.two)<<5)|(low(b.up)<<6)|(low(b.down)<<7);
        else value=low(b.left)|(low(b.right)<<1)|(low(b.one)<<2)|(pin(2,6,b.two)<<3)|
            (low(reset_)<<4)|0x20|(pin(1,5,false)<<6)|(pin(3,7,false)<<7);
    }
    record_access({cycle,port,value,false});
    return value;
}
void Console::write_port(std::uint16_t raw,std::uint8_t value,std::uint64_t cycle) {
    const auto port=static_cast<std::uint8_t>(raw);
    if (port>=0x40 && port<0x80) psg_.check_write(cycle);
    sync(cycle);
    if (port<0x40) {
        if (port&1) {
            auto th=[](std::uint8_t control,unsigned direction,unsigned output) {
                return (control&(1u<<direction)) || (control&(1u<<output));
            };
            if ((!th(io_control_,1,5) && th(value,1,5)) || (!th(io_control_,3,7) && th(value,3,7))) vdp_.latch_h_counter();
            io_control_=value;
        } else memory_.set_memory_control(value);
    } else if (port<0x80) {
        psg_.write(value,cycle);
    } else if (port<0xc0) {
        if (port&1) vdp_.write_control(value); else vdp_.write_data(value);
    }
    record_access({cycle,port,value,true});
}
}
