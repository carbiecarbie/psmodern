// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/machine.h"
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <tuple>

namespace psmodern {
Machine::RomReplacement Machine::prepare_rom_replacement(std::shared_ptr<const Memory::Rom> image) {
    if(!image || image->size()!=512*1024)throw std::invalid_argument("Expected a 512 KiB replacement ROM image");
    return RomReplacement(std::move(image));
}
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
Machine::PortTrace::PortTrace(Machine& machine)
    : machine_(machine), previous_(machine.port_trace_) {
    machine_.port_trace_=&accesses_;
}
Machine::PortTrace::~PortTrace() {
    machine_.port_trace_=previous_;
}
void Machine::record_access(PortAccess access) {
    last_access_=access;
    if (port_trace_) port_trace_->push_back(access);
}
bool Machine::equivalent(const Machine& o) const {
    auto state=[](const Machine& m) {
        return std::tie(m.cycle_,m.opcode_fetches_,m.scheduled_nmi_,m.pads_,m.pending_pads_,
                        m.io_control_,m.open_bus_,m.reset_,m.pause_,m.pending_reset_,m.pending_pause_,
                        m.input_latched_,m.sampled_int_,m.sampled_nmi_,m.pause_nmi_level_);
    };
    return state(*this)==state(o) && cpu_.equivalent(o.cpu_) && memory_==o.memory_ &&
           vdp_==o.vdp_ && psg_==o.psg_;
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
std::uint64_t Machine::run_frame(const FrameInput& next_input, RoutineRegistry* routines, const Hook& hook, const WriteObserver& writes) {
    struct RestoreObserver {
        const WriteObserver*& slot;const WriteObserver* previous;
        ~RestoreObserver() {slot=previous;}
    } restore{write_observer_,write_observer_};
    write_observer_=writes ? &writes : nullptr;
    pending_pads_=next_input.pads; pending_reset_=next_input.reset; pending_pause_=next_input.pause;
    auto target=(cycle_/frame_cycles+1)*frame_cycles;
    while (cycle_<target || cpu_.raw().resume==Z80_RESUME_XY) step(hook,routines);
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
    const auto& spec=routine->spec;
    constexpr auto reason_count=static_cast<unsigned>(RoutineFallback::count);
    std::array<bool,reason_count> applies{};
    auto reason=[&](RoutineFallback why, bool applied) { applies[static_cast<unsigned>(why)]=applied; };
    if (!spec.exits.empty() && spec.exits.front().sprite_outputs) {
        const auto count=spec.exits.front().sprite_outputs;
        if (bound!=count*16u && (cpu_.raw().bc.uint16_value&255)==0xbe)
            throw std::runtime_error("Invalid sprite output cycle bound");
        for (unsigned i=0;i<count;++i) {
            const auto opcode=static_cast<std::uint16_t>(pc+2*i);
            if (memory_.mapping(opcode)!=mapping || memory_.mapping(opcode+1)!=mapping ||
                peek(opcode)!=0xed || peek(opcode+1)!=0xa3)
                throw std::runtime_error("Sprite output slice must contain only OUTI in bank 1 ROM");
        }

    }
    if (!spec.exits.empty() && spec.exits.front().kind==RoutineExit::output_repeat) {
        if (memory_.mapping(pc+1).kind!=Memory::Kind::rom || peek(pc)!=0xED || peek(pc+1)!=0xB3)
            throw std::runtime_error("Video register output must start at a ROM OTIR");
        if (bound!=video_register_output_cycles())
            throw std::runtime_error("Invalid video register output bound");
    }
    if (!spec.exits.empty() && spec.exits.front().kind==RoutineExit::copy_repeat) {
        const auto second=static_cast<std::uint16_t>(pc+1);
        if (memory_.mapping(second).kind!=Memory::Kind::rom || peek(pc)!=0xed || peek(second)!=0xb0)
            throw std::runtime_error("Registered block copy must start at a ROM LDIR");
        const auto count=(bound+20)/21;
        if (!count || count>copy_iterations(cpu_.raw().bc.uint16_value) ||
            copy_duration(cpu_.raw().bc.uint16_value,count)!=bound)
            throw std::runtime_error("Invalid forward block copy bound");
        if (spec.exits.front().native_frame_tail &&
            (memory_.mapping(second)!=mapping || bound!=block_copy_frame_cycles()))
            throw std::runtime_error("Scene copy frame exit has the wrong mapping or bound");
        const auto last=static_cast<unsigned>(cpu_.raw().de.uint16_value)+count-1;
        reason(RoutineFallback::mapper_write,last>=0xfffc);
    }
    reason(RoutineFallback::halted,cpu_.raw().halt_line!=0);
    reason(RoutineFallback::interrupt_delay,(cpu_.raw().request & Z80_REQUEST_REJECT_NMI)!=0);
    const auto sp=cpu_.raw().sp.uint16_value;
    // A scratch stack below SP also needs mapped bytes outside the mapper registers.
    auto stack_byte=[&](unsigned offset) {
        const auto address=static_cast<std::uint16_t>(sp+offset);
        return memory_.mapping(address).kind==Memory::Kind::unmapped || (spec.scratch_stack && address>=0xfffc);
    };
    bool stack=stack_byte(0) || stack_byte(1);
    for (unsigned i=1;i<=spec.scratch_stack;++i) stack=stack || stack_byte(0x10000-i);
    // Inherited words above SP are popped by the body or the core: every byte
    // from SP to SP+inherited+1 must be mapped and off the mapper registers.
    if (spec.inherited_stack) for (unsigned i=0;i<spec.inherited_stack+2;++i) {
        const auto address=static_cast<std::uint16_t>(sp+i);
        stack=stack || memory_.mapping(address).kind==Memory::Kind::unmapped || address>=0xfffc;
    }
    reason(RoutineFallback::unmapped_stack,stack);
    reason(RoutineFallback::ei_delay,cpu_.raw().data.uint8_array[0]==0xfb);
    const auto irq=cpu_.raw().iff1 ? vdp_.next_irq_conservative() : std::nullopt;
    const auto nmi=next_nmi();
    reason(RoutineFallback::irq,irq && *irq<end);
    reason(RoutineFallback::nmi,nmi && *nmi<end);
    const auto frame_left=frame_cycles-cycle_%frame_cycles;
    const bool final_copy=!spec.exits.empty() && spec.exits.front().kind==RoutineExit::copy_repeat &&
        spec.exits.front().native_frame_tail;
    const auto copy_count=(bound+20)/21;
    const bool copy_tail=final_copy && copy_count>=2 && 21*(copy_count-1)<frame_left;
    const bool final_load=!spec.exits.empty() && spec.exits.front().kind==RoutineExit::core_final &&
        spec.exits.front().native_frame_tail;
    if (final_load && (bound!=20 || peek(pc)!=0x21 || peek(pc+3)!=0x01 ||
        memory_.mapping(pc+5)!=mapping))
        throw std::runtime_error("Dialogue row frame exit must load HL and BC from ROM");
    // Native execution finishes the instruction which straddles the frame.
    // The dialogue setup may do the same when its C++ load has finished
    // strictly before that boundary. IRQ and NMI use the full bound above.
    const bool wrong_sprite_port=!spec.exits.empty() && spec.exits.front().sprite_outputs &&
        (cpu_.raw().bc.uint16_value&255)!=0xbe;
    reason(RoutineFallback::frame,wrong_sprite_port || (final_load ? frame_left<=10 : frame_left<bound && !copy_tail));
    // A forced single instruction runs on the core with no added delay, so
    // window reasons do not apply. HALT must run on the core without the
    // adjustment. The validated RAM store can also run at an NMI-reject
    // boundary: the core clears the rejection and executes that instruction.
    const bool hard=applies[static_cast<unsigned>(RoutineFallback::halted)] ||
        (applies[static_cast<unsigned>(RoutineFallback::interrupt_delay)] && !spec.absolute_wram_store);
    const bool forced=spec.force_admit && !hard;
    if (spec.force_admit && hard) ++counters.forced_failed;
    if (!forced && std::find(applies.begin(),applies.end(),true)!=applies.end()) {
        for (unsigned i=0;i<reason_count;++i) counters.reasons[i]+=applies[i];
        ++counters.fallback_calls; return false;
    }
    if (forced) { for (unsigned i=0;i<reason_count;++i) counters.forced_reasons[i]+=applies[i]; ++counters.forced_calls; }
    ++counters.cpp_calls;
    bool fell_back=false;
    auto body=[&]()->std::optional<SegmentExit> {
        std::optional<Z80> saved;
        if (forced) saved=cpu_.raw();
        RoutineContext context(*this,bound,spec,forced);
        try { routine->body(context); }
        catch (const std::exception&) { if (!forced || context.stepped_) throw; }
        if (forced && !context.stepped_) {
            // Before its core step a forced body can only have changed registers.
            cpu_.raw()=*saved; fell_back=true; return std::nullopt;
        }
        context.check_bound();
        if (!context.finished_) throw std::runtime_error("Registered routine omitted its original return");
        return context.exit_;
    };
    if (routines.verifier) routines.verifier->run(*this,{mapping.bank,pc},spec,forced,bound,
        counters.cpp_calls+counters.fallback_calls,body);
    else body();
    if (fell_back) {
        for (unsigned i=0;i<reason_count;++i) counters.forced_reasons[i]-=applies[i];
        --counters.cpp_calls; --counters.forced_calls;
        ++counters.fallback_calls; ++counters.forced_failed;
        return false;
    }
    return true;
}
std::uint64_t Machine::routine_window() const {
    std::uint64_t window=frame_cycles-cycle_%frame_cycles;
    auto limit=[&](std::optional<std::uint64_t> event) {
        if (event) window=std::min(window,*event>cycle_ ? *event-cycle_ : 0);
    };
    if (cpu_.raw().iff1) limit(vdp_.next_irq_conservative());
    limit(next_nmi());
    return window;
}
unsigned Machine::video_register_output_cycles() const {
    const auto bc=cpu_.raw().bc.uint16_value;
    if ((bc&255)!=0xBF) return 2*frame_cycles;
    const unsigned remaining=bc>>8;
    if (remaining==1) return 16;
    const auto source=cpu_.raw().hl.uint16_value;
    if (memory_.mapping(source).kind==Memory::Kind::unmapped) return 21;
    const auto reg=vdp_.control_write_register(peek(source));
    if (reg && (*reg==0 || *reg==1 || *reg==10)) return 21;
    const unsigned pair=remaining==2 ? 37 : 42;
    return pair<=routine_window() ? pair : 21;
}
unsigned Machine::block_copy_cycles() const {
    const auto remaining=cpu_.raw().bc.uint16_value;
    unsigned count=copy_iterations(remaining);
    const auto maximum=copy_duration(remaining,count);
    const auto window=routine_window();
    if (maximum<=window) return maximum;
    // All shorter sections stop before the last byte, which alone costs 16
    // cycles. If even one transfer will cross a boundary, ordinary admission
    // rejects it and the core handles the instruction.
    count=std::max(1u,static_cast<unsigned>(window/21));
    return copy_duration(remaining,count);
}
// Only the two opted-in scene palette copies use this selection. A real
// C++ transfer must finish strictly before the frame; the last transfer is
// still executed by the core. Interrupt windows cover its complete duration.
unsigned Machine::block_copy_frame_cycles() const {
    const auto ordinary=block_copy_cycles();
    const auto remaining=cpu_.raw().bc.uint16_value;
    const auto frame_left=frame_cycles-cycle_%frame_cycles;
    if (ordinary>=frame_left || frame_left<=21) return ordinary;
    const unsigned before_final=static_cast<unsigned>((frame_left-1)/21);
    const unsigned count=std::min(copy_iterations(remaining),before_final+1);
    if (count<2 || count<=static_cast<unsigned>((ordinary+20)/21)) return ordinary;
    const auto candidate=copy_duration(remaining,count);
    const auto irq=cpu_.raw().iff1 ? vdp_.next_irq_conservative() : std::nullopt;
    const auto nmi=next_nmi();
    if ((irq && *irq<cycle_+candidate) || (nmi && *nmi<cycle_+candidate)) return ordinary;
    return candidate;
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
namespace {
// A late NMI edge must not be visible to the hook or core until after this
// instruction. Restore it even if a host callback throws.
struct HideLateNmi {
    Z80& cpu;
    bool late;
    HideLateNmi(Z80& cpu,bool late,bool hide=true) : cpu(cpu),late(late) {
        if (late && hide) cpu.request &= ~Z80_REQUEST_NMI;
    }
    ~HideLateNmi() { if (late) cpu.request |= Z80_REQUEST_NMI; }
};
}
std::uint64_t Machine::step(const Hook& hook, RoutineRegistry* routines) {
    sync(cycle_);
    const bool unsampled_nmi=(cpu_.raw().request & Z80_REQUEST_NMI) && !sampled_nmi_;
    z80_int(&cpu_.raw(), sampled_int_);
    const bool accepted=interrupt_accepted_at_boundary();
    const bool boundary=cpu_.raw().resume==0 && !accepted;
    bool observed=false, native=false, callback_nmi=false;
    const auto pc=cpu_.raw().pc.uint16_value;
    if (boundary && hook && routines && routines->may_observe(pc)) {
        const auto mapping=memory_.mapping(pc);
        const auto* entry=mapping.kind==Memory::Kind::rom ? routines->find({mapping.bank,pc}) : nullptr;
        if (entry && entry->spec.observe_entry) {
            // The callback can replace the ROM and registry, set the PC or
            // advance the clock. Do not retain its old routine or bound.
            auto arbitration=[&] {
                const auto& cpu=cpu_.raw();
                return std::tuple{cycle_,sampled_int_,sampled_nmi_,vdp_.irq(),+cpu.request,
                    +cpu.iff1,+cpu.iff2,+cpu.resume,+cpu.halt_line,cpu.data.uint32_value};
            };
            const auto before=arbitration();
            const auto image=memory_.rom_;
            std::optional<std::uint16_t> target;
            {
                HideLateNmi mask(cpu_.raw(),unsampled_nmi);
                target=hook(pc,mapping);
                // The pre-existing late edge is hidden in this scope. A bit
                // asserted by the callback is a new edge, visible to the
                // native instruction just as on the path without a registry.
                callback_nmi=unsampled_nmi && (cpu_.raw().request & Z80_REQUEST_NMI);
            }
            observed=true;
            native=callback_nmi || target.has_value() || cpu_.raw().pc.uint16_value!=pc || before!=arbitration() || image!=memory_.rom_;
            if (target) cpu_.raw().pc.uint16_value=*target;
        }
    }
    // Check while an entry-cycle NMI is still visible. Only the native path
    // hides an edge which missed the preceding instruction's E-1 sample.
    if (boundary && !native && routines && !routines->empty()) {
        const auto entry=cycle_;
        if (run_routine(*routines)) return cycle_-entry;
    }
    std::uint64_t elapsed;
    {
        // Keep a callback-created edge visible, but still restore the old
        // late edge after the native step, as the original hook scope does.
        HideLateNmi mask(cpu_.raw(),unsampled_nmi,!callback_nmi);
        if (boundary && !observed && hook) {
            if (auto target=hook(pc,memory_.mapping(pc))) cpu_.raw().pc.uint16_value=*target;
        }
        elapsed=cpu_.step(cycle_);
    }
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
void Machine::write(std::uint16_t addr,std::uint8_t value) {
    memory_.write(addr,value);open_bus_=value;
    if(write_observer_)(*write_observer_)(addr,value);
}
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
    record_access({cycle,port,value,false});
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
    record_access({cycle,port,value,true});
}
}
