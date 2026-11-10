// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/machine.h"
#include <algorithm>
#include <stdexcept>
#include <limits>

namespace psmodern {
Machine::Machine(std::shared_ptr<const Memory::Rom> rom, Memory::CartridgeRam ram)
    : memory_(std::move(rom), ram), cpu_(*this) {
    sampled_int_=vdp_.irq();
    z80_int(&cpu_.raw(),sampled_int_);
}
Machine::Machine(const Machine& o)
    : memory_(o.memory_), cpu_(o.cpu_), vdp_(o.vdp_), psg_(o.psg_), cycle_(o.cycle_), opcode_fetches_(o.opcode_fetches_),
      scheduled_nmi_(o.scheduled_nmi_), pads_(o.pads_), pending_pads_(o.pending_pads_), io_control_(o.io_control_),
      open_bus_(o.open_bus_), reset_(o.reset_), pause_(o.pause_), pending_reset_(o.pending_reset_), pending_pause_(o.pending_pause_),
      input_latched_(o.input_latched_), sampled_int_(o.sampled_int_), sampled_nmi_(o.sampled_nmi_),
      pause_nmi_level_(o.pause_nmi_level_)
      , last_access_(o.last_access_)
      { bind(); }
Machine& Machine::operator=(const Machine& o) {
    if (this != &o) {
        memory_=o.memory_; cpu_=o.cpu_; vdp_=o.vdp_; psg_=o.psg_; cycle_=o.cycle_; opcode_fetches_=o.opcode_fetches_;
        scheduled_nmi_=o.scheduled_nmi_; pads_=o.pads_; pending_pads_=o.pending_pads_; io_control_=o.io_control_;
        open_bus_=o.open_bus_; reset_=o.reset_; pause_=o.pause_; pending_reset_=o.pending_reset_; pending_pause_=o.pending_pause_;
        input_latched_=o.input_latched_; sampled_int_=o.sampled_int_; sampled_nmi_=o.sampled_nmi_;
        pause_nmi_level_=o.pause_nmi_level_;
        last_access_=o.last_access_;
        bind();
    }
    return *this;
}
void Machine::bind() { cpu_.bind(*this); }
bool Machine::equivalent(const Machine& o) const {
    return differences(o).empty();
}
std::uint8_t Machine::peek(std::uint16_t address) const {
    return memory_.mapping(address).kind==Memory::Kind::unmapped ? open_bus_ : memory_.read(address);
}

void Machine::latch_input() {
    pads_=pending_pads_; reset_=pending_reset_;
    pause_=pending_pause_; input_latched_=true;
}
void Machine::sync(std::uint64_t target) {
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
            if (pause_ && !pause_nmi_level_) z80_nmi(&cpu_.raw());
            pause_nmi_level_=pause_;
        }
        now=next;
    }
    vdp_.sync(target);
    psg_.sync(target);
    if (scheduled_nmi_ && *scheduled_nmi_<=target) { z80_nmi(&cpu_.raw()); scheduled_nmi_.reset(); }
}
void Machine::advance(std::uint64_t cycles) {
    if (!cycles) return;
    psg_.check_sync(cycle_+cycles); // Preflight the whole interval before either sync.
    // Replacement intervals [S,E) sample at E-1, just like Machine::step.
    sync(cycle_+cycles-1); sampled_int_=vdp_.irq();
    sampled_nmi_=(cpu_.raw().request & Z80_REQUEST_NMI)!=0;
    z80_int(&cpu_.raw(),sampled_int_);
    cycle_+=cycles; sync(cycle_);
}
std::uint64_t Machine::run_frame(const FrameInput& next_input, RoutineRegistry* routines) {
    pending_pads_=next_input.pads; pending_reset_=next_input.reset; pending_pause_=next_input.pause;
    auto target=(cycle_/frame_cycles+1)*frame_cycles;
    while (cycle_<target || cpu_.raw().resume==Z80_RESUME_XY) step({},routines);
    return cycle_-target;
}
void Machine::schedule_nmi(std::uint64_t cycle) {
    if (cycle<cycle_) throw std::invalid_argument("NMI is in the past");
    scheduled_nmi_=cycle;
}
std::optional<std::uint64_t> Machine::next_nmi() const {
    if (cpu_.raw().request & Z80_REQUEST_NMI) return cycle_;
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
bool Machine::run_routine(RoutineRegistry& routines) {
    const auto pc=cpu_.raw().pc.uint16_value;
    const auto mapping=memory_.mapping(pc);
    if (mapping.kind!=Memory::Kind::rom) return false;
    auto* routine=routines.find({mapping.bank,pc});
    if (!routine) return false;
    auto& counters=routine->counters;
    if (!routines.enabled) {
        ++counters.fallback_calls; ++counters.reasons[static_cast<unsigned>(RoutineFallback::disabled)];
        return false;
    }
    const auto bound=routine->bound(*this);
    if (!bound || bound>std::numeric_limits<std::uint64_t>::max()-cycle_)
        throw std::runtime_error("Invalid registered routine cycle bound");
    const auto end=cycle_+bound;
    bool fallback=false;
    auto reason=[&](RoutineFallback why, bool applies) {
        if (applies) { fallback=true; ++counters.reasons[static_cast<unsigned>(why)]; }
    };
    reason(RoutineFallback::halted,cpu_.raw().halt_line!=0);
    reason(RoutineFallback::interrupt_delay,(cpu_.raw().request & Z80_REQUEST_REJECT_NMI)!=0);
    const auto sp=cpu_.raw().sp.uint16_value;
    reason(RoutineFallback::unmapped_stack,memory_.mapping(sp).kind==Memory::Kind::unmapped ||
        memory_.mapping(static_cast<std::uint16_t>(sp+1)).kind==Memory::Kind::unmapped);
    reason(RoutineFallback::ei_delay,cpu_.raw().data.uint8_array[0]==0xfb);
    const auto irq=cpu_.raw().iff1 ? vdp_.next_irq_conservative() : std::nullopt;
    const auto nmi=next_nmi();
    reason(RoutineFallback::irq,irq && *irq<end);
    reason(RoutineFallback::nmi,nmi && *nmi<end);
    reason(RoutineFallback::frame,frame_cycles-cycle_%frame_cycles<bound);
    if (fallback) { ++counters.fallback_calls; return false; }
    ++counters.cpp_calls;
    auto body=[&] {
        RoutineContext context(*this,bound);
        routine->body(context);
        context.check_bound();
        if (!context.finished_) throw std::runtime_error("Registered routine omitted its original return");
    };
    if (routines.verifier) routines.verifier->run(*this,{mapping.bank,pc},bound,counters.cpp_calls+counters.fallback_calls,body);
    else body();
    return true;
}
bool Machine::interrupt_accepted_at_boundary() const {
    if (cpu_.raw().resume && cpu_.raw().resume!=Z80_RESUME_HALT) return false;
    const bool unsampled_nmi=(cpu_.raw().request & Z80_REQUEST_NMI) && !sampled_nmi_;
    auto request=cpu_.raw().request & ~(unsampled_nmi ? Z80_REQUEST_NMI : 0);
    // z80_int at Z80.c:2380-2383 clears a deasserted line, and only
    // creates a request for an asserted line when IFF1 is set.
    if (!sampled_int_) request &= ~Z80_REQUEST_INT;
    else if (cpu_.raw().iff1) request |= Z80_REQUEST_INT;
    // Mirrors the pinned Z80.c RETN bookkeeping (lines 1141-1146) and
    // request/RETN acceptance checks (2505-2507,2560-2618). Resumed index
    // work precedes that loop; halted work can reenter it (2443-2492).
    const bool reject_nmi=(request & Z80_REQUEST_REJECT_NMI)!=0;
    const bool nmi=!reject_nmi && (request & Z80_REQUEST_NMI);
    const bool reti_delay=cpu_.raw().data.uint8_array[0]==0xed &&
        (cpu_.raw().data.uint8_array[1]&0xc7)==0x45 && !(cpu_.raw().data.uint8_array[2]&1);
    const bool irq=!reject_nmi && !nmi && (request & Z80_REQUEST_INT) &&
        cpu_.raw().data.uint8_array[0]!=0xfb && !reti_delay;
    return nmi || irq;
}
std::uint64_t Machine::step(const Hook& hook, RoutineRegistry* routines) {
    sync(cycle_);
    const bool unsampled_nmi=(cpu_.raw().request & Z80_REQUEST_NMI) && !sampled_nmi_;
    z80_int(&cpu_.raw(), sampled_int_);
    const bool accepted=interrupt_accepted_at_boundary();
    // Check while an entry-cycle NMI is still visible. Only the native path
    // hides an edge which missed the preceding instruction's E-1 sample.
    if (cpu_.raw().resume==0 && !accepted && routines && !routines->empty()) {
        const auto entry=cycle_;
        if (run_routine(*routines)) return cycle_-entry;
    }
    if (unsampled_nmi) cpu_.raw().request &= ~Z80_REQUEST_NMI;
    if (cpu_.raw().resume==0 && !accepted && hook) {
        const auto pc=cpu_.raw().pc.uint16_value;
        if (auto target=hook(pc,memory_.mapping(pc))) cpu_.raw().pc.uint16_value=*target;
    }
    const auto elapsed=cpu_.step(cycle_);
    if (unsampled_nmi) cpu_.raw().request |= Z80_REQUEST_NMI;
    // [S,E) samples the line at E-1, after any earlier I/O in that instruction.
    sync(cycle_+elapsed-1); sampled_int_=vdp_.irq();
    sampled_nmi_=(cpu_.raw().request & Z80_REQUEST_NMI)!=0;
    cycle_+=elapsed; sync(cycle_);
    return elapsed;
}
std::uint8_t Machine::opcode(std::uint16_t addr) { ++opcode_fetches_; return read(addr); }
std::uint8_t Machine::read(std::uint16_t addr) {
    if (memory_.mapping(addr).kind!=Memory::Kind::unmapped) open_bus_=memory_.read(addr);
    return open_bus_;
}
void Machine::write(std::uint16_t addr,std::uint8_t value) { memory_.write(addr,value); open_bus_=value; }
std::uint8_t Machine::input(std::uint16_t addr,std::uint64_t cycle) { return read_port(addr,cycle); }
void Machine::output(std::uint16_t addr,std::uint8_t value,std::uint64_t cycle) { write_port(addr,value,cycle); }
std::uint8_t Machine::read_port(std::uint16_t raw,std::uint64_t cycle) {
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
    last_access_=PortAccess{cycle,port,value,false};
    return value;
}
void Machine::write_port(std::uint16_t raw,std::uint8_t value,std::uint64_t cycle) {
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
    last_access_=PortAccess{cycle,port,value,true};
}
}
