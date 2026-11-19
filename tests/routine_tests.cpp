// SPDX-License-Identifier: GPL-3.0-or-later
// The registered-segment mechanism without a processor core (milestone 14):
// the registry's validation, dispatch by bank, the stop on unrendered code and
// on refused entry states, and the rules a body is held to. The cases of the
// transition (core finals, forced admission, fallback to the core, the
// disabled registry) were removed with the core; the interrupt mechanism of
// exact bodies is tested in core_boundary_tests, the final renderers in
// kit_final_tests and the oracle in lockstep_tests.
#include "machine/machine.h"
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
using namespace psmodern;

namespace psmodern {
struct VdpFixture {
    static void state(Vdp& v, std::uint64_t cycle, unsigned counter, unsigned reload,
                      unsigned enables, unsigned pending) {
        v.cycle_=cycle; v.processed_dot_=(2*initial_state::vdp_dot+3*cycle)/2;
        v.line_counter_=counter; v.regs_[10]=reload;
        v.regs_[0]=enables&1 ? 0x10 : 0; v.regs_[1]=enables&2 ? 0x20 : 0;
        v.line_flag_=pending&1; v.frame_flag_=pending&2; v.irq_cache_.valid=false;
    }
};
}
namespace {
void check(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
template<class F> std::string failure(F operation) {
    try { operation(); } catch (const std::exception& e) { return e.what(); }
    throw std::runtime_error("an expected error did not occur");
}
template<class F> void rejects(F operation, std::string_view message) {
    const auto text=failure(operation);
    if (text.find(message)==std::string::npos) throw std::runtime_error("wrong diagnostic: "+text);
}
template<class F> void stops(F operation, std::string_view message) {
    try { operation(); } catch (const UnhandledState& e) {
        if (std::string_view(e.what()).find(message)==std::string_view::npos) throw std::runtime_error(std::string("wrong stop message: ")+e.what());
        return;
    }
    throw std::runtime_error("the engine did not stop");
}
constexpr auto R=RoutineExit::returned;
constexpr auto F=RoutineExit::core_final;
std::shared_ptr<const Memory::Rom> program(std::initializer_list<std::uint8_t> code) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    std::copy(code.begin(),code.end(),rom->begin()); return rom;
}
void stack(Machine& m) {
    m.cpu().sp.uint16_value=0xc100;
    m.memory().write(0xc100,0); m.memory().write(0xc101,2);
}
RegisteredRoutine::Bound cost(std::uint64_t cycles) { return [=](const Machine&) {return cycles;}; }
void nop(RoutineContext& c, std::uint16_t pc) { (void)c.fetch_opcode(pc); c.advance(4); }
// NOP; NOP; RET at 00:0000, rendered (the RET pops the caller 00:0200).
void two_nops_and_return(RoutineContext& c) {
    nop(c,0); nop(c,1);
    (void)c.fetch_opcode(2);
    auto& z=c.cpu();
    const auto low=c.read(z.sp.uint16_value), high=c.read(static_cast<std::uint16_t>(z.sp.uint16_value+1));
    c.advance(10);
    z.sp.uint16_value=static_cast<std::uint16_t>(z.sp.uint16_value+2);
    z.memptr.uint16_value=static_cast<std::uint16_t>(low|(high<<8));
    c.finish({R,2},z.memptr.uint16_value);
}
SegmentSpec segment(std::uint16_t unit, std::initializer_list<SegmentExit> exits) { return {{0,unit},exits}; }

void registry() {
    auto body=[](RoutineContext& c){ two_nops_and_return(c); };
    RoutineRegistry r;
    r.add({0,0},segment(0,{{R,2}}),cost(18),body);
    rejects([&]{r.add({0,0},segment(0,{{R,2}}),cost(18),body);},"Duplicate");
    rejects([&]{r.add({0,0xc000},segment(0,{{R,2}}),cost(18),body);},"Invalid");
    auto refused=[&](RoutineEntry entry, SegmentSpec spec) {
        RoutineRegistry other;
        rejects([&]{other.add(entry,std::move(spec),cost(4),body);},"Invalid registered");
    };
    refused({0,0},{{0,0},{}});                           // no exit declared
    refused({0,0},{{0,0},{{F,1},{F,1}}});                // an exit declared twice
    refused({0,0},{{0,0},{{F,0}}});                      // the whole segment, not declared single-instruction
    refused({0,0},{{0,0},{{R,2},{F,0}}});
    refused({0,0},{{0,0},{{R,2}},false,max_scratch_stack+1});
    refused({0,0},{{0,0},{{F,0xc000}}});
    refused({0,0},{{32,0},{{F,1}}});
    refused({0,0},{{0,0},{{F,1}},false,0,true});          // single instruction with its exit elsewhere
    SegmentSpec single{{0,0x10},{{F,0x10}}}; single.single_instruction=true;
    r.add({0,0x10},single,cost(4),[](RoutineContext& c){ nop(c,0x10); c.finish({F,0x10},0x11); });
    check(r.find({0,0x10})->exact,"a single-instruction segment is exact");
    // A registered body runs and ends at its declared exit, nothing else.
    Machine m(program({0,0,0xc9})); stack(m);
    m.step(r);
    check(m.cpu().pc.uint16_value==0x200 && m.cpu().sp.uint16_value==0xc102 && m.cycles()==18,"a rendered body returns");
    check(r.find({0,0})->counters.cpp_calls==1 && r.find({0,0})->rendered[0]==1,"the call and its exit are counted");
    std::cout<<"registry: validation and one rendered call\n";
}

// No rendered code: the engine stops with a message naming the place.
void unrendered() {
    Machine empty(program({0})); stack(empty);
    stops([&]{empty.step();},"00:0000: no rewritten code begins at this address");
    RoutineRegistry r;
    r.add({0,0},segment(0,{{R,2}}),cost(18),[](RoutineContext& c){ two_nops_and_return(c); });
    // After the call the caller at 00:0200 has no segment.
    Machine m(program({0,0,0xc9})); stack(m);
    m.step(r);
    stops([&]{m.step(r);},"00:0200: no rewritten code");
    // The same address in another bank, in RAM or in unmapped memory is not the segment.
    for (unsigned mode=0;mode<4;++mode) {
        Machine other(program({0})); stack(other);
        RoutineRegistry banked; unsigned calls=0;
        banked.add({2,0x8000},{{2,0x8000},{{R,0x8000}}},cost(10),[&](RoutineContext& c){ ++calls; c.finish({R,0x8000},0); });
        other.cpu().pc.uint16_value=0x8000;
        if (mode==0) other.memory().write(0xffff,3);
        if (mode==1) other.memory().write(0xfffc,8);
        if (mode==2) other.cpu().pc.uint16_value=0xc000;
        if (mode==3) other.memory().set_memory_control(0x40);
        stops([&]{other.step(banked);},mode==1 || mode==2 ? "RAM" : mode==3 ? "unmapped" : std::string("03")+":8000");
        check(calls==0,"a segment of another bank or of RAM never runs");
    }
    // The run_frame of the program stops too.
    Machine frame(program({0})); stack(frame);
    stops([&]{frame.run_frame({},r);},"no rewritten code");
    // An interrupt response other than IM 1.
    Machine im2(program({0})); stack(im2);
    im2.cpu().im=2; im2.cpu().iff1=im2.cpu().iff2=1;
    check(im2.vdp().irq(),"the power-on frame interrupt is pending");
    im2.advance(1);
    stops([&]{im2.step(r);},"other than IM 1");
    std::cout<<"unrendered: the engine stops with a message\n";
}

// A state a segment refuses at its entry stops the run with the reason, counted;
// nothing runs in its place and the machine is unchanged.
void refusals() {
    auto refused=[](RoutineRegistry& r, Machine& m, RoutineFallback reason, std::string_view message) {
        const auto pc=m.cpu().pc.uint16_value; const auto cycle=m.cycles();
        stops([&]{m.step(r);},message);
        const auto& counters=r.find({0,0})->counters;
        check(counters.fallback(reason)==1 && counters.fallback_calls==1 && counters.cpp_calls==0,"the refusal is counted");
        check(m.cpu().pc.uint16_value==pc && m.cycles()==cycle,"a refused call changed the machine");
    };
    {
        RoutineRegistry r;
        r.add_exact({0,0},segment(0,{{R,2}}),EntryGuard{"A is zero",[](const Machine& m){ return m.cpu().af.uint8_values.at_1==0; }},
                    cost(18),[](RoutineContext& c){ two_nops_and_return(c); });
        Machine m(program({0,0,0xc9})); stack(m); m.cpu().af.uint8_values.at_1=1;
        refused(r,m,RoutineFallback::guard,"refused (guard): A is zero");
    }
    {
        RoutineRegistry r;
        SegmentSpec spec=segment(0,{{R,2}}); spec.scratch_stack=2;
        r.add({0,0},spec,cost(18),[](RoutineContext& c){ two_nops_and_return(c); });
        Machine m(program({0,0,0xc9})); stack(m); m.cpu().sp.uint16_value=0xfffe;
        refused(r,m,RoutineFallback::unmapped_stack,"refused (unmapped_stack)");
    }
    {
        RoutineRegistry r;
        r.add_exact({0,0},segment(0,{{R,2}}),cost(2*Machine::frame_cycles),[](RoutineContext& c){ two_nops_and_return(c); });
        Machine m(program({0,0,0xc9})); stack(m);
        refused(r,m,RoutineFallback::frame,"refused (frame)");
    }
    {
        RoutineRegistry r;
        r.add({0,0},segment(0,{{R,2}}),cost(18),[](RoutineContext& c){ two_nops_and_return(c); });
        Machine m(program({0,0,0xc9})); stack(m); m.cpu().halt_line=1;
        refused(r,m,RoutineFallback::halted,"refused (halted)");
    }
    {
        // A body without register sync refuses a call an event falls inside.
        RoutineRegistry r;
        r.add({0,0},segment(0,{{R,2}}),cost(18),[](RoutineContext& c){ two_nops_and_return(c); });
        Machine m(program({0,0,0xc9})); stack(m);
        m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles-10);
        refused(r,m,RoutineFallback::frame,"refused (frame)");
    }
    std::cout<<"refusals: guard, stack, uncertain bound, halt and window stop the run\n";
}

// An exact body that writes a mapper register or the memory control port
// must not go on rendering instructions from memory it remapped: the original
// fetches its next instruction from the new mapping, which a rendered body
// cannot follow, so the run stops at that fetch. Remapping another slot,
// writing the same bank again, or remapping in the body's last instruction
// (the engine then dispatches the next address in the new mapping) is fine.
// The body works on the register file directly: nothing to store or load.
struct Direct final : RoutineContext::RegisterSync {
    void store(Z80&) const override {}
    void load(const Z80&) override {}
};
void self_remap() {
    auto image=std::make_shared<Memory::Rom>(512*1024,0);
    auto run=[&](std::uint16_t address, std::uint8_t value, bool port, bool last=false) {
        Machine m(image); stack(m); m.cpu().pc.uint16_value=0x4000;
        RoutineRegistry r;
        SegmentSpec spec{{1,0x4000},{{F,0x4003},{F,0x4004}}};
        r.add_exact({1,0x4000},spec,cost(17),[=](RoutineContext& c) {
            Direct sync; RoutineContext::SyncScope scope(c,sync);
            (void)c.fetch_opcode(0x4000);
            if (port) c.write_port(0x3e,value,10); else { c.advance(10); c.write(address,value); }
            c.advance(3);
            if (last) { c.finish({F,0x4003},0x4003); return; }
            (void)c.fetch_opcode(0x4003); c.advance(4); c.finish({F,0x4004},0x4004);
        });
        try { m.step(r); } catch (const UnhandledState& e) {
            return std::string(e.what()).find("remapped the memory holding its own code")!=std::string::npos;
        }
        return false;
    };
    check(run(0xfffe,5,false),"remapping the slot of the body's code stops the run");
    check(!run(0xfffe,1,false),"writing the slot's own bank again is fine");
    check(!run(0xffff,5,false) && !run(0xfffd,5,false),"remapping another slot is fine");
    check(run(0x3e,0x40,true),"disabling the cartridge under the body stops the run");
    check(!run(0xfffe,5,false,true) && !run(0x3e,0x40,true,true),"a remapping final instruction leaves the next fetch to the engine");
    std::cout<<"self remap: a body remapping its own code stops the run\n";
}

// The declaration rules of the special exits (ported from the milestone 7-13
// cases whose run-time half compared a core-executed final with the native
// step): every wrong declaration is refused at registration, and every special
// field takes part in the declaration identity lockstep compares.
void declarations() {
    auto invalid=[](RoutineEntry at, const SegmentSpec& bad, const char* what) {
        RoutineRegistry x;
        if (failure([&]{x.add(at,bad,cost(80),[](RoutineContext&){});}).find("Invalid")==std::string::npos)
            throw std::runtime_error(std::string("accepted a wrong declaration: ")+what);
    };
    auto valid=[](RoutineEntry at, const SegmentSpec& spec) { RoutineRegistry x; x.add(at,spec,cost(80),[](RoutineContext&){}); };
    {   // Reset stack (00:0000 LD SP,nn).
        SegmentExit reset{F,0}; reset.reset_stack=true;
        SegmentSpec spec{{0,0},{reset}}; spec.single_instruction=true; spec.observe_entry=true; valid({0,0},spec);
        auto a=spec; a.observe_entry=false; invalid({0,0},a,"reset without observed entry");
        auto b=spec; b.scratch_stack=2; invalid({0,0},b,"reset with scratch stack");
        auto c=spec; c.exits[0].conditional=true; invalid({0,0},c,"conditional reset");
    }
    {   // NMI return (POP AF; RETN).
        SegmentExit exit{F,1,2}; exit.return_from_nmi=true;
        SegmentSpec spec{{0,0},{exit}}; spec.inherited_stack=2; valid({0,0},spec);
        auto a=spec; a.exits[0].conditional=true; invalid({0,0},a,"conditional NMI return");
        auto b=spec; b.exits[0].reset_stack=true; invalid({0,0},b,"NMI return with reset stack");
        auto c=spec; c.exits[0].interrupt_mask=InterruptMask::enable; invalid({0,0},c,"NMI return with mask");
    }
    {   // Seeds: cartridge RAM, work RAM, sprite shadow.
        SegmentExit exit{F,0}; exit.wram_seed=0x8000; exit.seed_target=SeedTarget::cartridge_ram;
        SegmentSpec spec{{0,0},{exit}}; spec.single_instruction=true; valid({0,0},spec);
        auto a=spec; a.exits[0].wram_seed=0x8001; invalid({0,0},a,"cartridge seed address");
        auto b=spec; b.exits[0].wram_seed.reset(); invalid({0,0},b,"seed target without seed");
        auto different=exit; different.seed_target=SeedTarget::work_ram;
        check(!exit.same_declaration(different),"seed target kind participates in declaration identity");
        SegmentExit work{F,0}; work.wram_seed=0xc300;
        SegmentSpec ws{{0,0},{work}}; ws.single_instruction=true; valid({0,0},ws);
        auto c=ws; c.slot2_mapper=true; invalid({0,0},c,"seed with slot 2 mapper");
        auto d=ws; d.exits[0].cartridge_control=true; invalid({0,0},d,"seed with cartridge control");
        auto e=ws; e.exits[0].interrupt_mask=InterruptMask::disable; invalid({0,0},e,"seed with mask");
        auto undeclared=work; undeclared.wram_seed.reset();
        check(!work.same_declaration(undeclared),"seed declaration is compared by lockstep");
        const RoutineEntry clear{0,0x0FF3};
        SegmentExit zero{F,0x0FFC}; zero.wram_seed=0xC800; zero.seed_target=SeedTarget::sprite_shadow_zero;
        SegmentSpec zs{clear,{zero}}; valid(clear,zs);
        for (unsigned variant=0;variant<6;++variant) {
            auto bad=zs; auto pc=clear;
            if (variant==0) bad.unit.address++;
            if (variant==1) pc.address++;
            if (variant==2) bad.exits.front().wram_seed=0xC900;
            if (variant==3) bad.exits.front().address++;
            if (variant==4) bad.slot2_mapper=true;
            if (variant==5) bad.exits.front().cartridge_control=true;
            invalid(pc,bad,"sprite clear seed");
        }
    }
    {   // The interrupt handler's mapper pair (00:0143-014B).
        SegmentExit exit{F,0x014c,4}; exit.cartridge_control=true;
        SegmentSpec spec{{0,0x0143},{exit}}; spec.inherited_stack=4; spec.slot2_mapper=true; valid({0,0x0147},spec);
        for (unsigned mode=0;mode<5;++mode) {
            auto bad=spec; RoutineEntry entry{0,0x0147};
            if(mode==0) entry.address=0x0148;
            if(mode==1) bad.unit.address=0x0147;
            if(mode==2) bad.exits[0].address=0x014b;
            if(mode==3) bad.inherited_stack=2;
            if(mode==4) bad.exits[0].return_from_nmi=true;
            invalid(entry,bad,"interrupt mapper pair");
        }
    }
    {   // The interrupt handler's long stack (00:014F).
        SegmentExit exit{F,0x0157,12}; exit.interrupt_mask=InterruptMask::enable;
        SegmentSpec spec{{0,0x014f},{exit}}; spec.inherited_stack=12; valid({0,0x014f},spec);
        for (unsigned mode=0;mode<7;++mode) {
            auto bad=spec; RoutineEntry entry{0,0x014f};
            if(mode==0) entry.address=0x014e;
            if(mode==1) bad.unit.address=0x014e;
            if(mode==2) bad.exits[0].address=0x0158;
            if(mode==3) bad.exits[0].interrupt_mask=InterruptMask::disable;
            if(mode==4) bad.scratch_stack=2;
            if(mode==5) bad.slot2_mapper=true;
            if(mode==6) bad.inherited_stack=10;
            invalid(entry,bad,"interrupt long stack");
        }
    }
    {   // The window bank store (00:3A89).
        const RoutineEntry entry{0,0x3A89};
        SegmentExit exit{F,entry.address}; exit.window_bank_store=true;
        SegmentSpec spec{{0,0x3A83},{exit}}; spec.single_instruction=true; valid(entry,spec);
        for (unsigned variant=0;variant<4;++variant) {
            auto bad=spec; auto at=entry;
            if(variant==0) bad.exits[0].conditional=true;
            if(variant==1) bad.unit.address=0x3A84;
            if(variant==2) at.address=0x3A88;
            if(variant==3) bad.exits[0].interrupt_mask=InterruptMask::disable;
            invalid(at,bad,"window bank store");
        }
        auto unmarked=exit; unmarked.window_bank_store=false;
        check(!exit.same_declaration(unmarked),"window bank declaration is part of lockstep identity");
    }
    {   // The scene copy's frame tail (01:61B9).
        const RoutineEntry entry{1,0x61B9};
        SegmentExit exit{RoutineExit::copy_repeat,entry.address}; exit.native_frame_tail=true;
        SegmentSpec spec{entry,{exit}}; valid(entry,spec);
        auto ordinary=exit; ordinary.native_frame_tail=false;
        check(!exit.same_declaration(ordinary),"scene native tail participates in declaration identity");
        for (unsigned fault=0;fault<15;++fault) {
            auto bad=spec; auto pc=entry;
            if (fault==0) pc.address++;
            if (fault==1) bad.unit.address++;
            if (fault==2) bad.exits.front().kind=F;
            if (fault==3) bad.observe_entry=true;
            if (fault==4) bad.scratch_stack=2;
            if (fault==5) bad.slot2_mapper=true;
            if (fault==6) bad.inherited_stack=2;
            if (fault==7) bad.single_instruction=true;
            if (fault==8) bad.exits.front().stack=2;
            if (fault==9) bad.exits.front().conditional=true;
            if (fault==10) bad.exits.front().after=1;
            if (fault==11) bad.exits.front().interrupt_mask=InterruptMask::enable;
            if (fault==12) bad.exits.front().wram_seed=0xC400;
            if (fault==13) bad.exits.front().cartridge_control=true;
            if (fault==14) bad.exits.front().sprite_outputs=2;
            invalid(pc,bad,"scene copy tail");
        }
    }
    {   // The video slices (01:591E sprite outputs, 00:3451 row load tail).
        const RoutineEntry sprite{1,0x591E},row{0,0x3451};
        SegmentExit out{F,0x5920}; out.sprite_outputs=2;
        SegmentExit load{F,0x3454}; load.native_frame_tail=true;
        for (bool is_sprite:{false,true}) {
            const auto entry=is_sprite ? sprite : row;
            SegmentSpec spec{is_sprite ? RoutineEntry{1,0x588B} : RoutineEntry{0,0x3439},{is_sprite ? out : load}};
            valid(entry,spec);
            for (unsigned fault=0;fault<8;++fault) {
                auto bad=spec; auto pc=entry;
                if (fault==0) bad.unit.address++;
                if (fault==1) pc.address++;
                if (fault==2) bad.exits.front().address++;
                if (fault==3) bad.exits.front().conditional=true;
                if (fault==4) bad.exits.front().after=1;
                if (fault==5) bad.observe_entry=true;
                if (fault==6) bad.exits.front().interrupt_mask=InterruptMask::enable;
                if (fault==7) bad.scratch_stack=2;
                invalid(pc,bad,is_sprite ? "sprite outputs" : "row load tail");
            }
        }
    }
    {   // Cartridge control (LD (FFFC),A).
        SegmentExit exit{F,0}; exit.cartridge_control=true;
        SegmentSpec spec{{0,0},{exit}}; spec.single_instruction=true; valid({0,0},spec);
        auto returning=spec; returning.exits[0].kind=R; invalid({0,0},returning,"returning cartridge control");
        auto undeclared=exit; undeclared.cartridge_control=false;
        check(!exit.same_declaration(undeclared),"lockstep distinguishes cartridge control declaration");
    }
    std::cout<<"declarations: special exits validated and part of the declaration identity\n";
}

// Review R3 M2: behaviour of the engine that lost its tests when the core went.
// (a) The host hook at an observed entry never sees an NMI edge the coming
// instruction has not sampled yet, and the edge survives the hook, also when
// the hook throws; it is accepted at the next boundary.
void hook_late_nmi() {
    RoutineRegistry r;
    SegmentSpec spec{{0,0},{{F,0}}}; spec.single_instruction=true; spec.observe_entry=true;
    r.add_exact({0,0},spec,cost(4),[](RoutineContext& c){ nop(c,0); c.finish({F,0},1); });
    for (const bool throwing:{false,true}) {
        Machine m(program({0,0})); stack(m);
        m.schedule_nmi(m.cycles());
        bool seen=false, hidden=false;
        const Machine::Hook hook=[&](std::uint16_t, Memory::Mapping)->std::optional<std::uint16_t> {
            seen=true; hidden=!(m.cpu().request&Z80::request_nmi);
            if (throwing) throw std::runtime_error("the host hook failed");
            return {};
        };
        if (throwing) rejects([&]{ m.step(r,hook); },"the host hook failed"); else m.step(r,hook);
        check(seen && hidden,"the hook saw an NMI edge the instruction has not sampled");
        check((m.cpu().request&Z80::request_nmi)!=0,"a late NMI edge was lost around the hook");
        if (!throwing) {
            m.step(r);
            check(m.cpu().pc.uint16_value==0x0066,"the late NMI is accepted at the next boundary");
        }
    }
    std::cout<<"hook late NMI: hidden from the hook, kept when it returns or throws\n";
}
// (b) A body without register sync: each event refused exactly from the cycle
// before its end (E-1) and admitted at its end (E), with the reason, the
// message, and the machine unchanged; (d) the NMI-reject marker refuses it.
void window_refusals() {
    constexpr std::uint64_t length=18;
    auto attempt=[](Machine& m, const char* what, std::optional<RoutineFallback> reason, std::string_view message) {
        RoutineRegistry r;
        r.add({0,0},segment(0,{{R,2}}),cost(length),[](RoutineContext& c){ two_nops_and_return(c); });
        const auto pc=m.cpu().pc.uint16_value; const auto sp=m.cpu().sp.uint16_value; const auto cycle=m.cycles();
        const auto& counters=r.find({0,0})->counters;
        if (!reason) {
            m.step(r);
            if (counters.cpp_calls!=1 || counters.fallback_calls) throw std::runtime_error(std::string("not admitted: ")+what);
            return;
        }
        stops([&]{m.step(r);},message);
        if (counters.fallback(*reason)!=1 || counters.fallback_calls!=1 || counters.cpp_calls)
            throw std::runtime_error(std::string("the refusal is not counted: ")+what);
        if (m.cpu().pc.uint16_value!=pc || m.cpu().sp.uint16_value!=sp || m.cycles()!=cycle)
            throw std::runtime_error(std::string("a refused call changed the machine: ")+what);
    };
    for (const std::uint64_t distance:{length-1,length}) {
        const bool inside=distance<length;
        {   // The frame interrupt.
            Machine m(program({0,0,0xc9})); stack(m);
            m.cpu().iff1=m.cpu().iff2=1;
            (void)m.read_port(0xbf,m.cycles()); m.advance(1);
            const auto irq=m.next_irq();
            check(irq && *irq>m.cycles()+distance,"the frame interrupt is ahead");
            m.advance(*irq-m.cycles()-distance);
            attempt(m,"irq",inside ? std::optional{RoutineFallback::irq} : std::nullopt,"refused (irq): an interrupt falls inside");
        }
        {   // A scheduled NMI.
            Machine m(program({0,0,0xc9})); stack(m);
            m.schedule_nmi(m.cycles()+distance);
            attempt(m,"nmi",inside ? std::optional{RoutineFallback::nmi} : std::nullopt,"refused (nmi): an NMI falls inside");
        }
        {   // The frame end.
            Machine m(program({0,0,0xc9})); stack(m);
            m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles-distance);
            attempt(m,"frame",inside ? std::optional{RoutineFallback::frame} : std::nullopt,"refused (frame): a frame end falls inside");
        }
    }
    {   // The instruction before was EI.
        Machine m(program({0,0,0xc9})); stack(m);
        m.cpu().data.uint8_array[0]=0xfb;
        attempt(m,"ei",RoutineFallback::ei_delay,"refused (ei_delay): the call follows EI");
    }
    {   // (d) The NMI-reject marker an NMI response leaves for the next boundary.
        Machine m(program({0,0,0xc9})); stack(m);
        m.cpu().request|=Z80::request_reject_nmi;
        attempt(m,"reject marker",RoutineFallback::interrupt_delay,"refused (interrupt_delay): the NMI-reject marker");
    }
    std::cout<<"window refusals: IRQ, NMI and frame end refused at E-1 and admitted at E; EI delay; reject marker\n";
}
// (c) The register comparator: data[1..3] are dead after an unprefixed
// instruction and compared after a prefix or in a resumption; data[0] and
// every named register always; xy never.
void register_comparator() {
    Z80 a{}; Z80 b=a;
    b.data.uint8_array[1]=1; b.data.uint8_array[2]=2; b.data.uint8_array[3]=3; b.xy.uint16_value=0x1234;
    check(same_registers(a,b),"dead scratch after an unprefixed instruction is not state");
    for (const std::uint8_t prefix:{0xcb,0xed,0xdd,0xfd}) {
        auto x=a, y=b; x.data.uint8_array[0]=y.data.uint8_array[0]=prefix;
        check(!same_registers(x,y),"the scratch after a prefix is state");
    }
    auto resumed=b; resumed.resume=Z80::resume_xy;
    auto resumed_a=a; resumed_a.resume=Z80::resume_xy;
    check(!same_registers(resumed_a,resumed),"the scratch in a resumption is state");
    auto opcode=a; opcode.data.uint8_array[0]=0xfb;
    check(!same_registers(a,opcode),"data[0] (the EI delay, RETN and LD A,I/R rules) is state");
    auto memptr=a; memptr.memptr.uint16_value=1;
    auto q=a; q.q=1;
    check(!same_registers(a,memptr) && !same_registers(a,q),"MEMPTR and Q are state");
    std::cout<<"register comparator: dead scratch rule\n";
}
// (e) The fixed-bank interrupt epilogue restores the slot-2 mapper once.
void interrupt_mapper_once() {
    SegmentExit exit{F,0x014c,4}; exit.cartridge_control=true;
    SegmentSpec spec{{0,0x0143},{exit}}; spec.inherited_stack=4; spec.slot2_mapper=true;
    RoutineRegistry r;
    unsigned writes=0;
    r.add_exact({0,0x0147},spec,cost(40),[&](RoutineContext& c){
        nop(c,0x0147); c.write(0xffff,2); ++writes; c.write(0xffff,3); ++writes;
    });
    Machine m(program({0})); stack(m); m.cpu().pc.uint16_value=0x0147;
    rejects([&]{m.step(r);},"Interrupt mapper may be restored only once");
    check(writes==1,"the first restore is allowed");
    std::cout<<"interrupt mapper: restored only once\n";
}
// (f) Held and inherited stack words and loop thresholds in declarations.
void stack_declarations() {
    auto accepted=[](RoutineEntry at, const SegmentSpec& spec) {
        RoutineRegistry x; return failure([&]{ x.add(at,spec,cost(80),[](RoutineContext&){}); throw std::runtime_error("accepted"); });
    };
    auto valid=[&](const SegmentSpec& spec, const char* what) {
        if (accepted({0,0},spec)!="accepted") throw std::runtime_error(std::string("refused a right declaration: ")+what);
    };
    auto invalid=[&](const SegmentSpec& spec, const char* what) {
        if (accepted({0,0},spec).find("Invalid")==std::string::npos) throw std::runtime_error(std::string("accepted a wrong declaration: ")+what);
    };
    SegmentSpec base{{0,0},{{F,1}}};
    { auto s=base; s.inherited_stack=max_inherited_stack; s.exits[0].stack=static_cast<std::int8_t>(max_inherited_stack); valid(s,"inherited at the limit"); }
    { auto s=base; s.inherited_stack=max_inherited_stack+2; invalid(s,"inherited above the limit"); }
    { auto s=base; s.inherited_stack=3; invalid(s,"odd inherited stack"); }
    { auto s=base; s.scratch_stack=4; s.exits[0].stack=-4; valid(s,"held words within the scratch stack"); }
    { auto s=base; s.scratch_stack=4; s.exits[0].stack=-6; invalid(s,"held words beyond the scratch stack"); }
    { auto s=base; s.scratch_stack=4; s.exits[0].stack=-3; invalid(s,"odd stack offset"); }
    { auto s=base; s.inherited_stack=2; s.exits[0].stack=4; invalid(s,"popped beyond the inherited words"); }
    { SegmentSpec s{{0,0},{{R,1}}}; s.scratch_stack=2; s.exits[0].stack=-2; invalid(s,"a return popping no held word"); }
    { SegmentSpec s{{0,0},{{R,1}}}; s.exits[0].conditional=true; invalid(s,"a conditional return exit"); }
    { SegmentSpec s{{0,0},{{R,1}}}; s.exits[0].after=1; invalid(s,"a return with a loop threshold"); }
    { auto s=base; s.exits[0].after=max_exit_after; valid(s,"a loop threshold at the limit"); }
    { auto s=base; s.exits[0].after=static_cast<std::uint16_t>(max_exit_after+1); invalid(s,"a loop threshold above the limit"); }
    { SegmentSpec s{{0,0},{{F,0}}}; s.single_instruction=true; s.inherited_stack=2; invalid(s,"a single instruction inheriting words"); }
    { SegmentSpec s{{0,0},{{F,0}}}; s.single_instruction=true; s.exits[0].after=1; invalid(s,"a single instruction with a loop"); }
    // Interrupt masks: an EI or DI final, never conditional, looping, returning or with the slot-2 mapper.
    { auto s=base; s.exits[0].interrupt_mask=InterruptMask::enable; valid(s,"an EI final"); }
    { auto s=base; s.exits[0].interrupt_mask=InterruptMask::disable; valid(s,"a DI final"); }
    { auto s=base; s.exits[0].interrupt_mask=InterruptMask::enable; s.exits[0].conditional=true; invalid(s,"a conditional mask"); }
    { auto s=base; s.exits[0].interrupt_mask=InterruptMask::enable; s.exits[0].after=1; invalid(s,"a looping mask"); }
    { auto s=base; s.exits[0].interrupt_mask=InterruptMask::enable; s.slot2_mapper=true; invalid(s,"a mask with the slot-2 mapper"); }
    { SegmentSpec s{{0,0},{{R,1}}}; s.exits[0].interrupt_mask=InterruptMask::disable; invalid(s,"a returning mask"); }
    std::cout<<"stack declarations: held, inherited, loop and mask rules\n";
}

// What a body is held to.
void body_rules() {
    auto guard=[](SegmentSpec spec, std::uint64_t bound, RegisteredRoutine::Body body, std::string_view diagnostic) {
        Machine m(program({0,0,0xc9})); stack(m);
        RoutineRegistry r; r.add({0,0},std::move(spec),cost(bound),std::move(body));
        rejects([&]{m.step(r);},diagnostic);
    };
    guard(segment(0,{{F,1}}),18,[](RoutineContext& c){ nop(c,0); c.finish({R,2},3); },"not declared");
    guard(segment(0,{{R,2}}),17,[](RoutineContext& c){ two_nops_and_return(c); },"exceeded");
    guard(segment(0,{{R,2}}),18,[](RoutineContext& c){ nop(c,0); },"omitted");
    guard(segment(0,{{R,2}}),18,[](RoutineContext& c){ c.advance(2); (void)c.read_port(0x3e,1); },"past");
    guard(segment(0,{{R,2}}),18,[](RoutineContext& c){ two_nops_and_return(c); c.advance(1); },"already returned");
    SegmentSpec single{{0,0},{{F,0}}}; single.single_instruction=true;
    guard(single,8,[](RoutineContext& c){ nop(c,0); nop(c,1); c.finish({F,0},2); },"second instruction");
    // A body without register sync may not change the mapping or the
    // interrupt registers of the video chip (an exact body may).
    for (unsigned address=0xfffc;address<=0xffff;++address)
        guard(segment(0,{{F,1}}),8,[=](RoutineContext& c){ c.write(address,2); },"mapper");
    for (unsigned port:{0x00,0x3e}) guard(segment(0,{{F,1}}),8,[=](RoutineContext& c){ c.write_port(port,0,0); },"mapping");
    for (unsigned reg:{0,1,10})
        guard(segment(0,{{F,1}}),8,[=](RoutineContext& c){ c.write_port(0xbf,0x55,0); c.write_port(0xbf,0x80|reg,0); },"interrupt control");
    // Review R3 M2 (g): the mirror port 0x81, and a latch half written before
    // the entry; the refused write leaves the video chip and the registers as
    // they were.
    for (unsigned port:{0xbfu,0x81u}) for (unsigned reg:{0u,1u,10u}) for (bool latched:{false,true}) {
        Machine m(program({0,0,0xc9})); stack(m);
        if (latched) m.write_port(0xbf,0x55,m.cycles());
        RoutineRegistry r; r.add({0,0},segment(0,{{F,1}}),cost(8),[=](RoutineContext& c){
            if (!latched) c.write_port(port,0x55,0);
            c.write_port(port,0x80|reg,0);
        });
        const auto before=m;
        rejects([&]{m.step(r);},"interrupt control");
        check(same_registers(m.cpu(),before.cpu()) && m.cycles()==before.cycles(),"a refused interrupt-control write changed the registers");
        // With the first byte latched before the entry the body wrote nothing to the chip.
        if (latched) check(m.vdp()==before.vdp(),"a refused interrupt-control write changed the video chip");
    }
    std::cout<<"body rules: undeclared exit, bound, return, port order, single instruction, restrictions\n";
}
}
void irq_query() {
    unsigned count=0, future=0, absent=0;
    for (unsigned line:{0,1,190,191,192,193,259,260,261})
    for (unsigned phase:{0,209,210,211,227})
    for (unsigned counter:{0,1,2,190,191,192,193,254,255})
    for (unsigned reload:{0,1,191,192,193,255})
    for (unsigned enables:{0,1,2,3})
    for (unsigned pending:{0,1,2,3}) {
        Vdp v; VdpFixture::state(v,line*228+phase,counter,reload,enables,pending);
        const auto unchanged=v;
        const auto cheap=v.next_irq_conservative();
        check(v==unchanged,"derived query cache excluded from hardware equivalence");
        check(v.next_irq_conservative()==cheap,"cached query stable");
        const auto exact=v.next_irq();
        check(!exact || (cheap && *cheap<=*exact),"conservative IRQ never later than exact simulation");
        if (exact && *exact>v.cycle()) ++future;
        if (!exact) ++absent;
        ++count;
    }
    check(future>1000 && absent>1000,"query matrix independently varies enable and pending flags");
    // Real transitions exercise invalidation by status, each source register,
    // counter clocks, pending flags, source disable and immediate enable.
    Vdp v;
    for (unsigned i=0;i<500;++i) {
        if (i%3==0) v.read_status(0);
        if (i%7==0) {v.write_control(i);v.write_control(0x80);}
        if (i%11==0) {v.write_control(i);v.write_control(0x81);}
        if (i%13==0) {v.write_control(i);v.write_control(0x8a);}
        auto cheap=v.next_irq_conservative(), exact=v.next_irq();
        check(!exact || (cheap && *cheap<=*exact),"query cache invalidated after real mutations");
        v.sync(v.cycle()+113);
    }
    std::cout<<"IRQ query states: "<<count+500<<'\n';
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="irq_query") irq_query();
        else {
            registry(); unrendered(); refusals(); body_rules(); self_remap(); declarations();
            hook_late_nmi(); window_refusals(); register_comparator(); interrupt_mapper_once(); stack_declarations();
        }
        std::cout<<"Registered routine tests passed.\n"; return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
