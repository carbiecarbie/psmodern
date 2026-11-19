// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/machine.h"
#include <algorithm>
#include <stdexcept>
#include <limits>
#include <string>
#include <tuple>

namespace psmodern {
Machine::Machine(const Machine& o) : Console(o) {
    if (o.depth_) throw std::logic_error("A machine cannot be copied inside a nested interrupt delivery");
}
Machine& Machine::operator=(const Machine& o) {
    if (o.depth_ || depth_) throw std::logic_error("A machine cannot be assigned inside a nested interrupt delivery");
    Console::operator=(o);
    return *this;
}
std::uint64_t Machine::run_frame(const FrameInput& next_input, RoutineRegistry* routines, const Hook& hook,
                                 const WriteObserver& writes, const FrameObserver& frame) {
    if (depth_) throw std::logic_error("A frame cannot run inside a nested interrupt delivery");
    struct Restore {
        Machine& m; const WriteObserver* writes; const FrameObserver* frame;
        ~Restore() { m.write_observer_=writes; m.frame_observer_=frame; m.frame_observed_=true; }
    } restore{*this,write_observer_,frame_observer_};
    write_observer_=writes ? &writes : nullptr;
    pending_pads_=next_input.pads; pending_reset_=next_input.reset; pending_pause_=next_input.pause;
    const auto target=(cycle_/frame_cycles+1)*frame_cycles;
    frame_target_=target; frame_observed_=false; frame_observer_=frame ? &frame : nullptr;
    // (A prefix resumption exists only under the test scaffolding.)
    while (cycle_<target || cpu_.resume==Z80::resume_xy) step(hook,routines);
    // The first top-level boundary at or after the target, unless the
    // observer already fired at a boundary inside the last step.
    if (!cpu_.resume) observe_boundary(cpu_.pc.uint16_value,false,false);
    if (!frame_observed_) { frame_observed_=true; if (frame_observer_) (*frame_observer_)(); }
    const auto overshoot=cycle_-target;
    // The overshoot invariant (planning 3.5, review R6): every top-level step
    // ends before the next frame target.
    if (overshoot>=frame_cycles) throw std::logic_error("A top-level step ran past the next frame target");
    if (routines) {
        auto& census=routines->census_;
        ++census.frames;
        if (overshoot>census.max_overshoot) {
            census.max_overshoot=overshoot;
            const auto pc=cpu_.pc.uint16_value;
            census.max_overshoot_entry={memory_.mapping(pc).bank,pc};
        }
    }
    return overshoot;
}
namespace {
std::string hex(unsigned value, int width) {
    static constexpr char digits[]="0123456789ABCDEF";
    std::string out;
    for (int shift=4*(width-1);shift>=0;shift-=4) out+=digits[(value>>shift)&15];
    return out;
}
}
void Machine::unhandled(std::uint16_t pc, const std::string& what) const {
    const auto mapping=memory_.mapping(pc);
    const auto where=mapping.kind==Memory::Kind::rom ? hex(mapping.bank,2)+":"+hex(pc,4) :
        std::string(mapping.kind==Memory::Kind::unmapped ? "unmapped " : "RAM ")+hex(pc,4);
    throw UnhandledState("The rewritten game does not handle this state at "+where+": "+what);
}
bool Machine::run_routine(RoutineRegistry& routines) {
    const auto pc=cpu_.pc.uint16_value;
    const auto mapping=memory_.mapping(pc);
    if (mapping.kind!=Memory::Kind::rom) return false;
    auto* routine=routines.find({mapping.bank,pc});
    if (!routine) return false;
    auto& counters=routine->counters;
    const auto& spec=routine->spec;
    // A state the segment does not render stops the run with a message
    // naming the segment and the reason (counted first, for the tests).
    auto refuse=[&](RoutineFallback why, const std::string& what) {
        ++counters.fallback_calls; ++counters.reasons[static_cast<unsigned>(why)];
        unhandled(pc,"segment of unit "+hex(spec.unit.bank,2)+":"+hex(spec.unit.address,4)+" refused ("+
                  std::string(routine_fallback_names[static_cast<unsigned>(why)])+"): "+what);
    };
    // An explicit entry guard: the states the body renders.
    if (routine->guard && !routine->guard->holds(*this)) refuse(RoutineFallback::guard,std::string(routine->guard->name));
    const auto bound=routine->bound(*this);
    if (!bound || bound>std::numeric_limits<std::uint64_t>::max()-cycle_)
        throw std::runtime_error("Invalid registered routine cycle bound");
    const auto end=cycle_+bound;
    const bool exact=routine->exact;
    if (!spec.exits.empty() && spec.exits.front().kind==RoutineExit::copy_repeat) {
        const auto count=(bound+20)/21;
        if (!count || count>copy_iterations(cpu_.bc.uint16_value) ||
            copy_duration(cpu_.bc.uint16_value,count)!=bound)
            throw std::runtime_error("Invalid forward block copy bound");
        // A copy reaching the mapper registers can remap the slot holding its
        // own LDIR, which the original fetches again for every transfer: the
        // rendered slice does not follow that.
        const auto last=static_cast<unsigned>(cpu_.de.uint16_value)+count-1;
        if (last>=0xfffc) refuse(RoutineFallback::mapper_write,"a block copy reaches the mapper registers");
    }
    if (cpu_.halt_line) refuse(RoutineFallback::halted,"the processor is halted");
    // The NMI-reject marker left by an NMI acceptance: an exact body clears
    // it at its entry (below), as the core does at this boundary.
    const bool reject=(cpu_.request & Z80::request_reject_nmi)!=0;
    if (reject && !exact) refuse(RoutineFallback::interrupt_delay,"the NMI-reject marker is set at a body without register sync");
    const auto sp=cpu_.sp.uint16_value;
    // A scratch stack below SP also needs mapped bytes outside the mapper registers.
    auto stack_byte=[&](unsigned offset) {
        const auto address=static_cast<std::uint16_t>(sp+offset);
        return memory_.mapping(address).kind==Memory::Kind::unmapped || (spec.scratch_stack && address>=0xfffc);
    };
    bool stack=stack_byte(0) || stack_byte(1);
    for (unsigned i=1;i<=spec.scratch_stack;++i) stack=stack || stack_byte(0x10000-i);
    // Inherited words above SP are popped by the body: every byte from SP to
    // SP+inherited+1 must be mapped and off the mapper registers.
    if (spec.inherited_stack) for (unsigned i=0;i<spec.inherited_stack+2;++i) {
        const auto address=static_cast<std::uint16_t>(sp+i);
        stack=stack || memory_.mapping(address).kind==Memory::Kind::unmapped || address>=0xfffc;
    }
    if (stack) refuse(RoutineFallback::unmapped_stack,"the stack is unmapped or on the mapper registers");
    const bool wrong_sprite_port=!spec.exits.empty() && spec.exits.front().sprite_outputs &&
        (cpu_.bc.uint16_value&255)!=0xbe;
    if (exact) {
        // An exact body takes interrupts and frame ends at its own boundaries.
        // A bound beyond one frame is the estimate's uncertain path: a state
        // the body's guards refuse.
        if (wrong_sprite_port || bound>frame_cycles)
            refuse(RoutineFallback::frame,"the estimated path is uncertain (a state the segment's guards refuse)");
    } else {
        // A body without register sync cannot take an event inside it: the
        // whole call must fit before the next interrupt and frame end.
        if (cpu_.data.uint8_array[0]==0xfb) refuse(RoutineFallback::ei_delay,"the call follows EI");
        const auto irq=cpu_.iff1 ? vdp_.next_irq_conservative() : std::nullopt;
        const auto nmi=next_nmi();
        if (irq && *irq<end) refuse(RoutineFallback::irq,"an interrupt falls inside a body without register sync");
        if (nmi && *nmi<end) refuse(RoutineFallback::nmi,"an NMI falls inside a body without register sync");
        const auto frame_left=frame_cycles-cycle_%frame_cycles;
        if (wrong_sprite_port || frame_left<bound) refuse(RoutineFallback::frame,"a frame end falls inside a body without register sync");
    }
    ++counters.cpp_calls;
    if (reject) {
        // The marker discards every request at this boundary and the entry
        // instruction runs (Z80.c 2560-2561); an NMI edge not sampled at the
        // previous instruction's end is not seen yet and survives.
        const bool late_nmi=(cpu_.request & Z80::request_nmi) && !sampled_nmi_;
        cpu_.request=late_nmi ? Z80::request_nmi : 0;
    }
    RoutineContext context(*this,*routine,bound,exact);
    try { routine->body(context); }
    catch (const RoutineAbandoned&) { ++counters.abandoned; throw; }
    context.check_bound();
    if (!context.finished_) throw std::runtime_error("Registered routine omitted its original return");
    routine->last_exit=context.exit_;
    return true;
}
std::uint64_t Machine::routine_window() const {
    std::uint64_t window=frame_cycles-cycle_%frame_cycles;
    auto limit=[&](std::optional<std::uint64_t> event) {
        if (event) window=std::min(window,*event>cycle_ ? *event-cycle_ : 0);
    };
    if (cpu_.iff1) limit(vdp_.next_irq_conservative());
    limit(next_nmi());
    return window;
}
unsigned Machine::video_register_output_cycles() const {
    const auto bc=cpu_.bc.uint16_value;
    if ((bc&255)!=0xBF) return 2*frame_cycles;
    const unsigned remaining=bc>>8;
    if (remaining==1) return 16;
    const auto source=cpu_.hl.uint16_value;
    if (memory_.mapping(source).kind==Memory::Kind::unmapped) return 21;
    const auto reg=vdp_.control_write_register(peek(source));
    if (reg && (*reg==0 || *reg==1 || *reg==10)) return 21;
    const unsigned pair=remaining==2 ? 37 : 42;
    return pair<=routine_window() ? pair : 21;
}
unsigned Machine::block_copy_cycles() const {
    const auto remaining=cpu_.bc.uint16_value;
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
    const auto remaining=cpu_.bc.uint16_value;
    const auto frame_left=frame_cycles-cycle_%frame_cycles;
    if (ordinary>=frame_left || frame_left<=21) return ordinary;
    const unsigned before_final=static_cast<unsigned>((frame_left-1)/21);
    const unsigned count=std::min(copy_iterations(remaining),before_final+1);
    if (count<2 || count<=static_cast<unsigned>((ordinary+20)/21)) return ordinary;
    const auto candidate=copy_duration(remaining,count);
    const auto irq=cpu_.iff1 ? vdp_.next_irq_conservative() : std::nullopt;
    const auto nmi=next_nmi();
    if ((irq && *irq<cycle_+candidate) || (nmi && *nmi<cycle_+candidate)) return ordinary;
    return candidate;
}

namespace {
// A late NMI edge must not be visible to the hook until after this
// instruction. Restore it even if a host callback throws.
struct HideLateNmi {
    Z80& cpu;
    bool late;
    HideLateNmi(Z80& cpu,bool late) : cpu(cpu),late(late) {
        if (late) cpu.request &= static_cast<std::uint8_t>(~Z80::request_nmi);
    }
    ~HideLateNmi() { if (late) cpu.request |= Z80::request_nmi; }
};
// What a host hook may not change while a body is suspended below it.
struct HookState {
    std::uint16_t pc, sp, af, bc, de, hl, af_, bc_, de_, hl_, ix, iy, memptr;
    std::uint8_t i, r, iff1, iff2, im, request, resume, halt_line;
    std::uint64_t cycle, generation;
    bool sampled_int, sampled_nmi;
    const void* rom;
    bool operator==(const HookState&) const = default;
};
}
void Machine::observe_boundary(std::uint16_t pc, bool in_body, bool rendered) {
    if (cycle_==observed_cycle_) return;
    observed_cycle_=cycle_;
    if (boundary_observer_) (*boundary_observer_)({pc,depth_,in_body,rendered});
    if (frame_observer_ && !frame_observed_ && cycle_>=frame_target_) { frame_observed_=true; (*frame_observer_)(); }
}
bool Machine::accepts_in_cpp(Acceptance kind) const {
    // IM 1 is the only mode the game uses; HALT is never executed. Both
    // remaining cases stop the run (UnhandledState).
    const auto& cpu=cpu_;
    return !cpu.halt_line && !cpu.resume && (kind==Acceptance::nmi || cpu.im==1);
}
// The pinned core's response (Z80.c 2560-2582 for NMI; 2603-2650 and IM 1 at
// 2899-2904 for INT), in C++: no opcode read, R incremented, PC pushed high
// byte first, MEMPTR = PC = vector, Q and the previous-opcode byte cleared.
std::uint64_t Machine::accept(Acceptance kind, bool late_nmi) {
    auto& cpu=cpu_;
    std::uint64_t cycles;
    std::uint16_t vector;
    if (kind==Acceptance::nmi) {
        // Every request becomes the NMI-reject marker: a second NMI edge
        // before the next instruction is discarded (Z80.c 2563-2565).
        cpu.request=Z80::request_reject_nmi; cpu.iff1=0;
        vector=0x66; cycles=11;
    } else {
        // INT acceptance clears every request bit in the core; an NMI edge
        // not yet sampled survives it (rule 3).
        cpu.request=late_nmi ? Z80::request_nmi : 0; cpu.iff1=cpu.iff2=0;
        // NMOS: an INT accepted right after LD A,I or LD A,R resets PF
        // (Z80.c 2636-2641); IM 1 then clears Q.
        if (cpu.data.uint8_array[0]==0xed && (cpu.data.uint8_array[1]&0xf7)==0x57)
            cpu.af.uint8_values.at_0=static_cast<std::uint8_t>(cpu.af.uint8_values.at_0&~0x04);
        vector=0x38; cycles=13;
    }
    cpu.r=static_cast<std::uint8_t>((cpu.r&0x80)|((cpu.r+1)&0x7f));
    cpu.data.uint8_array[0]=0; cpu.q=0;
    const auto pc=cpu.pc.uint16_value;
    cpu.sp.uint16_value=static_cast<std::uint16_t>(cpu.sp.uint16_value-2);
    write(static_cast<std::uint16_t>(cpu.sp.uint16_value+1),static_cast<std::uint8_t>(pc>>8));
    write(cpu.sp.uint16_value,static_cast<std::uint8_t>(pc));
    cpu.memptr.uint16_value=cpu.pc.uint16_value=vector;
    // The response samples the lines at its own E-1, like an instruction.
    finish_interval(cycles);
    return cycles;
}
void Machine::deliver(Acceptance kind, bool late_nmi, std::uint16_t at) {
    auto& cpu=cpu_;
    const auto saved_sp=cpu.sp.uint16_value;
    struct Depth {
        unsigned& depth;
        explicit Depth(unsigned& d) : depth(d) { ++depth; }
        ~Depth() { --depth; }
    } depth(depth_);
    accept(kind,late_nmi);
    static const Hook no_hook;
    const Hook& hook=active_hook_ ? *active_hook_ : no_hook;
    auto* routines=active_routines_;
    while (cpu.pc.uint16_value!=at || cpu.sp.uint16_value!=saved_sp || cpu.resume) {
        // Reset jumps from the handler to 00:0084, whose next instruction
        // loads SP: the suspended bodies are never resumed (review R5).
        const auto pc=cpu.pc.uint16_value;
        if (!cpu.resume && (pc==0x0084 || pc==0x0000) && memory_.mapping(pc)==Memory::Mapping{Memory::Kind::rom,0})
            throw RoutineAbandoned{};
        step(hook,routines);
    }
}
void Machine::body_boundary(RoutineContext& context, std::uint16_t at, bool synced) {
    auto& cpu=cpu_;
    if (!context.exact_) {
        // Admission keeps every event out of the window of a body that is
        // not exact, so neither an interrupt nor a frame end can fall here.
        if (frame_observer_ && !frame_observed_ && cycle_>=frame_target_)
            throw std::logic_error("A frame ended inside a registered body that is not exact");
        return;
    }
    if (!synced && !context.sync_)
        throw std::logic_error("An exact registered body has no register sync");
    // A body at depth that renders the reset code inline (00:0000 or 00:0084
    // in bank 0) abandons the suspended bodies, as deliver() does between
    // nested steps (review R5; review of Part 0a). The register file is left
    // complete first, with the PC at the reset code, as there (review R1 L-1).
    if (depth_ && (at==0x0084 || at==0x0000) && memory_.mapping(at)==Memory::Mapping{Memory::Kind::rom,0}) {
        if (!synced) context.sync_->store(cpu);
        cpu.pc.uint16_value=at;
        throw RoutineAbandoned{};
    }
    // Under the test-side checks (a lockstep run): an address the host
    // watches must begin its own observed segment, never be rendered inline
    // by another body, which would bypass the hook (review of Part 0a, M2).
    if (opcode_check_ && active_routines_ && active_routines_->may_observe(at)) {
        const auto mapping=memory_.mapping(at);
        const auto* watched=mapping.kind==Memory::Kind::rom ? active_routines_->find({mapping.bank,at}) : nullptr;
        if (watched && watched->spec.observe_entry)
            throw std::logic_error("A body rendered a host-observed address inline");
    }
    cpu.pc.uint16_value=at;
    bool stored=synced, rendered=true, delivered=false;
    for (;;) {
        // Machine::advance sampled both lines at the previous instruction's
        // E-1 and drove the INT input; arbitration reads only those samples.
        // The NMI-reject marker is cleared at the entry of the segment an NMI
        // response leads to (run_routine), and a handler returns here only
        // after that: no boundary inside a body can see it.
        if (cpu.request & Z80::request_reject_nmi)
            throw std::logic_error("The NMI-reject marker reached an instruction boundary inside a body");
        const auto kind=pending_acceptance();
        const bool frame_due=frame_observer_ && !frame_observed_ && cycle_>=frame_target_;
        if (kind==Acceptance::none && !frame_due && !boundary_observer_) break;
        if (!stored) { context.sync_->store(cpu); stored=true; }
        const bool late_nmi=(cpu.request & Z80::request_nmi) && !sampled_nmi_;
        cpu.set_int_line(sampled_int_);
        observe_boundary(at,true,rendered);
        if (kind==Acceptance::none) break;
        if (!accepts_in_cpp(kind)) unhandled(at,"an interrupt response other than IM 1 (or while halted) inside a body");
        auto& counters=context.routine_.counters;
        if (kind==Acceptance::nmi) ++counters.nmi_deliveries; else ++counters.irq_deliveries;
        if (active_routines_) {
            auto& census=active_routines_->census_;
            if (kind==Acceptance::nmi) ++census.body_nmi; else ++census.body_irq;
        }
        const auto start=cycle_;
        deliver(kind,late_nmi,at);
        context.nested_cycles_+=cycle_-start;
        rendered=false;
        delivered=true;
    }
    if (delivered) {
        if (!synced) context.sync_->load(cpu);
        context.delivered_=true;
        context.instruction_start_=cycle_;
    }
}
void Machine::record_step(RoutineRegistry* routines, std::uint64_t start, std::uint16_t pc, Memory::Mapping mapping) {
    if (!routines || depth_) return;
    auto& census=routines->census_;
    if (cycle_-start>census.max_step) {
        census.max_step=cycle_-start;
        census.max_step_entry={mapping.bank,pc};
    }
}
std::uint64_t Machine::step(const Hook& hook, RoutineRegistry* routines) {
    const auto start=cycle_;
    struct Active {
        Machine& m; const Hook* hook; RoutineRegistry* routines;
        ~Active() { m.active_hook_=hook; m.active_routines_=routines; }
    } active{*this,active_hook_,active_routines_};
    active_hook_=&hook; active_routines_=routines;
    sync(cycle_);
    auto& cpu=cpu_;
    const bool unsampled_nmi=(cpu.request & Z80::request_nmi) && !sampled_nmi_;
    cpu.set_int_line(sampled_int_);
    const auto pc=cpu.pc.uint16_value;
    const auto pc_mapping=memory_.mapping(pc);
    if (cpu.resume) return scaffold(routines,start,pc,unsampled_nmi,"a prefix resumption");
    observe_boundary(pc,false,false);
    // A step with no rewritten code under the originals scaffolding (the
    // reference run of the game's tests): the whole step, an interrupt
    // response included, is the pinned core's, so the reference never shares
    // the engine's arbitration it is compared with (reviews R1 M-3, R3 M3).
    // The host hook keeps the engine's timing: it is not called at a boundary
    // where an interrupt response takes the step.
    if (scaffold_step_ && !scaffold_with_registry_ && (!routines || routines->empty()))
        return scaffold(routines,start,pc,unsampled_nmi,"no rewritten code begins at this address",
                        pending_acceptance()==Acceptance::none ? &hook : nullptr);
    const auto kind=pending_acceptance();
    if (kind!=Acceptance::none) {
        if (!accepts_in_cpp(kind))
            return scaffold(routines,start,pc,unsampled_nmi,"an interrupt response other than IM 1 (or while halted)");
        accept(kind,unsampled_nmi);
        if (routines) {
            auto& census=routines->census_;
            if (kind==Acceptance::nmi) ++(depth_ ? census.nested_nmi : census.top_nmi);
            else ++(depth_ ? census.nested_irq : census.top_irq);
        }
        record_step(routines,start,pc,pc_mapping);
        return cycle_-start;
    }
    auto snapshot=[&] {
        const auto& c=cpu_;
        return HookState{c.pc.uint16_value,c.sp.uint16_value,c.af.uint16_value,c.bc.uint16_value,c.de.uint16_value,
            c.hl.uint16_value,c.af_.uint16_value,c.bc_.uint16_value,c.de_.uint16_value,c.hl_.uint16_value,
            c.ix_iy[0].uint16_value,c.ix_iy[1].uint16_value,c.memptr.uint16_value,c.i,c.r,c.iff1,c.iff2,c.im,
            c.request,c.resume,c.halt_line,cycle_,routines ? routines->generation() : 0,sampled_int_,sampled_nmi_,
            memory_.rom_.get()};
    };
    // The host hook, at the entry of a segment flagged observe_entry only.
    // It can replace the ROM and registry or redirect the PC; the segment at
    // the resulting PC then runs, with no second hook call. Hooks below a
    // suspended body must be passive (review H2).
    bool hooked=false;
    if (hook && routines && routines->may_observe(pc)) {
        const auto* entry=pc_mapping.kind==Memory::Kind::rom ? routines->find({pc_mapping.bank,pc}) : nullptr;
        if (entry && entry->spec.observe_entry) {
            hooked=true;
            const auto state=depth_ ? snapshot() : HookState{};
            std::optional<std::uint16_t> target;
            {
                HideLateNmi mask(cpu,unsampled_nmi);
                target=hook(pc,pc_mapping);
            }
            if (depth_ && (target || !(state==snapshot())))
                throw std::logic_error("A host hook changed the machine while a registered body was suspended");
            if (target) cpu.pc.uint16_value=*target;
            // A hook that advanced the clock or raised an NMI can make an
            // interrupt due at this boundary: it is accepted before the
            // instruction, as the core accepted it on the native path.
            const auto due=pending_acceptance();
            if (due!=Acceptance::none) {
                if (!accepts_in_cpp(due))
                    return scaffold(routines,start,pc,(cpu.request & Z80::request_nmi) && !sampled_nmi_,
                                    "an interrupt response other than IM 1 (or while halted)");
                accept(due,(cpu.request & Z80::request_nmi) && !sampled_nmi_);
                if (routines) {
                    auto& census=routines->census_;
                    if (due==Acceptance::nmi) ++(depth_ ? census.nested_nmi : census.top_nmi);
                    else ++(depth_ ? census.nested_irq : census.top_irq);
                }
                record_step(routines,start,pc,pc_mapping);
                return cycle_-start;
            }
        }
    }
    if (routines && !routines->empty()) {
        if (depth_) {
            if (run_routine(*routines)) return cycle_-start;
        } else {
            struct Pin {
                RoutineRegistry& r;
                explicit Pin(RoutineRegistry& registry) : r(registry) { r.pin(); }
                ~Pin() { r.unpin(); }
            } pin(*routines);
            try {
                if (run_routine(*routines)) { record_step(routines,start,pc,pc_mapping); return cycle_-start; }
            } catch (const RoutineAbandoned&) {
                ++routines->census_.abandoned;
                record_step(routines,start,pc,pc_mapping);
                return cycle_-start;
            }
        }
    }
    return scaffold(routines,start,pc,unsampled_nmi,"no rewritten code begins at this address",hooked ? nullptr : &hook);
}
Machine::ScaffoldStep Machine::scaffold_step_=nullptr;
bool Machine::scaffold_with_registry_=false;
std::uint64_t Machine::scaffold(RoutineRegistry* routines, std::uint64_t start, std::uint16_t pc, bool unsampled_nmi,
                                const std::string& what, const Hook* hook) {
    if (!scaffold_step_ || (routines && !routines->empty() && !scaffold_with_registry_)) unhandled(cpu_.pc.uint16_value,what);
    // The host hook at the scaffolding's instruction boundary, as the native
    // step of the transition called it (passive at depth); a redirect runs
    // the target's instruction instead.
    if (hook && *hook) {
        const auto& c=cpu_;
        auto state=[&] {
            return HookState{c.pc.uint16_value,c.sp.uint16_value,c.af.uint16_value,c.bc.uint16_value,c.de.uint16_value,
                c.hl.uint16_value,c.af_.uint16_value,c.bc_.uint16_value,c.de_.uint16_value,c.hl_.uint16_value,
                c.ix_iy[0].uint16_value,c.ix_iy[1].uint16_value,c.memptr.uint16_value,c.i,c.r,c.iff1,c.iff2,c.im,
                c.request,c.resume,c.halt_line,cycle_,routines ? routines->generation() : 0,sampled_int_,sampled_nmi_,
                memory_.rom_.get()};
        };
        const auto before=state();
        std::optional<std::uint16_t> target;
        {
            HideLateNmi mask(cpu_,unsampled_nmi);
            target=(*hook)(cpu_.pc.uint16_value,memory_.mapping(cpu_.pc.uint16_value));
        }
        if (depth_ && (target || !(before==state())))
            throw std::logic_error("A host hook changed the machine while a registered body was suspended");
        if (target) cpu_.pc.uint16_value=*target;
    }
    std::uint64_t elapsed;
    {
        // A late NMI edge is not visible to this instruction.
        HideLateNmi mask(cpu_,unsampled_nmi);
        elapsed=scaffold_step_(*this);
    }
    finish_interval(elapsed);
    if (routines) ++routines->census_.scaffold_steps;
    record_step(routines,start,pc,memory_.mapping(pc));
    return elapsed;
}
}
