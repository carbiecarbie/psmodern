// SPDX-License-Identifier: GPL-3.0-or-later
// Mechanism tests of the engine on authored programs (no ROM), run against
// the pinned core through oracle::Lockstep (milestone 14, Part T). They
// replaced the per-call verifier's own tests of milestones 2 to 11.
//   fault cases   cycle r fetches flag memptr q ram vdp_late psg_late
//                 extra_port_read latch open_bus iff1 iff2 im i sp alternates
//                 request scheduled_nmi pad: an exact body damaged in one
//                 way is reported by the lockstep, naming the segment (and the
//                 field, where the report names it); bound: an understated bound is the
//                 engine's own refusal.
//   dispatch      IFF1 clear with INT asserted, the EI delay (a body without
//                 register sync is refused there: the run stops), a resumed
//                 index prefix (test scaffolding), an interrupt at the entry
//                 (the engine accepts it first and starts no call).
//   clean         an undamaged exact body over three frames: whole machines
//                 compared at each frame observer, the manifests equal, and
//                 the engine identical to a run without the lockstep.
//   windows       admission windows of a body that is not exact (a frame end,
//                 an IRQ or an NMI inside its bound refuse the call and stop the
//                 run; a clear window runs it).
//   fields        the component field lists and their backstop.
// (Milestone 14 Part I removed the cases of the transition's core finals:
// segments, forced and exits tested the core's execution of a body's final
// instruction and forced admission, which no longer exist; the final
// renderers are tested in kit_final_tests.)
//   nmi_reject    a second NMI edge (a Pause edge) during the response of an
//                 NMI taken inside a body.
#include "replay/verification.h"
#include "scaffold.h"
#include "game/dungeon_kit.h"
#include "machine/machine.h"
#include "oracle/lockstep.h"
#include <algorithm>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
using namespace psmodern;
namespace k=psmodern::game::kit;
namespace psmodern {
struct VdpFixture {
    static void change_cache(Vdp& v) {
        v.irq_cache_.valid=!v.irq_cache_.valid;
        v.irq_cache_.event=123;
    }
    static void raw_latches(Vdp& v) {
        v.horizontal_scroll_^=0x6d; v.vertical_scroll_^=0x37;
        v.preparing_sprites_[0].x=0x73; v.prepared_sprites_[0].x=0x31;
    }
};
}
namespace {
void check(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
// The report of a run that must fail (any runtime_error: a lockstep
// divergence or the engine's own refusal).
template<class F> std::string failure(F run, std::source_location where=std::source_location::current()) {
    try { run(); } catch (const std::runtime_error& e) { return e.what(); }
    throw std::runtime_error("Expected a failure (line "+std::to_string(where.line())+")");
}
bool diverged(const std::string& text) { return text.find("Lockstep divergence")!=std::string::npos; }
auto program(std::initializer_list<std::uint8_t> bytes) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    std::copy(bytes.begin(),bytes.end(),rom->begin()); return rom;
}
// SP = C100 with the return address 0200 on the stack.
void entry(Machine& m) {
    m.cpu().pc.uint16_value=0; m.cpu().sp.uint16_value=0xc100;
    m.memory().write(0xc100,0); m.memory().write(0xc101,2);
}
auto cost(std::uint64_t bound) { return [=](const Machine&) {return bound;}; }
constexpr auto R=RoutineExit::returned;
constexpr auto F=RoutineExit::core_final;
SegmentSpec segment(std::initializer_list<SegmentExit> exits) { return {{0,0},exits}; }
// The body keeps its registers in the register file.
struct Direct final : RoutineContext::RegisterSync {
    void store(Z80&) const override {}
    void load(const Z80&) override {}
};
void nop(RoutineContext& c, std::uint16_t pc) { (void)c.fetch_opcode(pc); c.advance(4); }
// A rendered RET at `at` (10 cycles), ending the exact body at its declared
// returned exit.
void render_return(RoutineContext& c, std::uint16_t at) {
    auto& z=c.cpu();
    (void)c.fetch_opcode(at);
    const auto sp=z.sp.uint16_value;
    const auto low=c.read(sp), high=c.read(static_cast<std::uint16_t>(sp+1));
    z.sp.uint16_value=static_cast<std::uint16_t>(sp+2);
    z.memptr.uint16_value=static_cast<std::uint16_t>(low|(high<<8));
    c.advance(10);
    c.finish({R,at},z.memptr.uint16_value);
}
bool returned_to_caller(const Machine& m) { return m.cpu().pc.uint16_value==0x200 && m.cpu().sp.uint16_value==0xc102; }
// Steps under lockstep until the program is back at its caller.
void returned_with(oracle::Lockstep& ls, Machine& m, RoutineRegistry* r) {
    for (unsigned n=0;n<1000;++n) {
        if (returned_to_caller(m)) return;
        ls.step(m,r);
    }
    throw std::runtime_error("Synthetic segments did not return");
}
// A few more steps without the registry: the machines stay equal after the
// segment, also across the frame boundary it ended at.

// ----------------------------------------------------------------------------
// Fault cases: an exact body NOP; NOP; RET (or LD A,90; OUT (n),A; NOP; NOP;
// RET) rendered on the register file, with one defect.
std::string fault_run(std::string_view name, bool inject, oracle::Mode mode, std::uint64_t bound_adjust=0) {
    const bool port=name=="vdp_late" || name=="psg_late" || name=="extra_port_read";
    auto rom=port ? program({0x3e,0x90,0xd3,static_cast<std::uint8_t>(name=="vdp_late" ? 0xbe : 0x7f),0,0,0xc9})
                  : program({0,0,0xc9});
    (*rom)[0x401]=0x5a;
    Machine m(rom); entry(m);
    // Place the VDP access on a given side of the dot-35 CPU slot.
    if (port) m.advance(5);
    RoutineRegistry r;
    r.add_exact({0,0},segment({{R,static_cast<std::uint16_t>(port ? 6 : 2)}}),cost((port ? 36 : 18)-bound_adjust),
        [&,inject](RoutineContext& c) {
            Direct sync; RoutineContext::SyncScope scope(c,sync);
            auto& z=c.cpu();
            if (port) {
                (void)c.fetch_opcode(0); const auto a=c.read(1); z.af.uint8_values.at_1=a; c.advance(7);
                (void)c.fetch_opcode(2); const auto p=c.read(3);
                z.memptr.uint16_value=static_cast<std::uint16_t>((a<<8)|((p+1)&0xff));
                if (inject && name=="extra_port_read") (void)c.read_port(0x7e,16);
                c.write_port(p,a,inject && name!="extra_port_read" ? 18 : 17);
                c.advance(18-c.elapsed());
                nop(c,4); nop(c,5); render_return(c,6);
            } else {
                (void)c.fetch_opcode(0); c.advance(4); (void)c.fetch_opcode(1);
                if (inject && name=="fetches") c.account_fetches(128);
                if (inject && name=="ram") c.write(0xc020,0x73);
                c.advance(inject && name=="cycle" ? 3 : 4);
                (void)c.fetch_opcode(2);
                const auto sp=z.sp.uint16_value;
                const auto low=c.read(sp), high=c.read(static_cast<std::uint16_t>(sp+1));
                z.sp.uint16_value=static_cast<std::uint16_t>(sp+2);
                z.memptr.uint16_value=static_cast<std::uint16_t>(low|(high<<8));
                c.advance(10);
                if (inject && name=="r") z.r^=1;
                if (inject && name=="flag") z.af.uint8_values.at_0^=1;
                if (inject && name=="memptr") z.memptr.uint16_value^=1;
                if (inject && name=="q") z.q^=1;
                if (inject && name=="latch") m.write_port(0xbf,0x37,m.cycles());
                if (inject && name=="open_bus") (void)c.read(0x0401);
                // Review R1 L-5: the remaining fields equal() compares.
                if (inject && name=="iff1") z.iff1^=1;
                if (inject && name=="iff2") z.iff2^=1;
                if (inject && name=="im") z.im=2;
                if (inject && name=="i") z.i^=1;
                if (inject && name=="sp") z.sp.uint16_value^=2;
                if (inject && name=="alternates") z.de_.uint16_value^=1;
                if (inject && name=="request") z.request|=Z80::request_nmi;
                if (inject && name=="scheduled_nmi") m.schedule_nmi(m.cycles()+5000);
                if (inject && name=="pad") { FrameInput input{}; input.pads[0].up=true; m.set_input(input); }
                c.finish({R,2},z.memptr.uint16_value);
            }
        });
    try {
        oracle::Lockstep ls(m,"synthetic",mode);
        ls.step(m,&r);
    } catch (const std::runtime_error& e) { return e.what(); }
    return {};
}
void fault(std::string_view name) {
    if (name=="bound") {
        // The bound is the estimating dry run of the path: a body that takes
        // longer than it declared is refused by the engine itself.
        const auto text=fault_run("bound",false,oracle::Mode::segments,1);
        check(text.find("declared cycle bound")!=std::string::npos,"An understated bound is refused: "+text);
        return;
    }
    for (const auto mode:{oracle::Mode::segments,oracle::Mode::instructions}) {
        const auto clean=fault_run(name,false,mode);
        check(clean.empty(),"The undamaged body must pass: "+clean);
        const auto text=fault_run(name,true,mode);
        check(diverged(text),"The damaged body must be reported: "+std::string(name));
        // The report names the damaged segment (review R3 L6).
        // Inside the damaged body (00:0000-00:0006) or at the boundary after it,
        // where the caller at 00:0200 begins (the whole-machine comparison; a
        // damaged MEMPTR returns to 00:0201).
        const bool inside=text.find("segment 00:0000, instruction 00:000")!=std::string::npos;
        const bool after=text.find("segment 00:0200, instruction 00:0200, depth 0\n")!=std::string::npos ||
            (name=="memptr" && text.find("segment 00:0201, instruction 00:0201, depth 0\n")!=std::string::npos);
        check(after || (inside && mode==oracle::Mode::instructions),"The divergence report names the location\n"+text);
        if (mode!=oracle::Mode::segments) continue;
        // The whole-machine comparison at the end of the step names the field.
        const std::pair<std::string_view,std::string_view> expected[]{
            {"r","cpu.r:"},{"fetches","machine.opcode_fetches:"},{"flag","cpu.f:"},{"memptr","cpu.memptr:"},
            {"q","cpu.q:"},{"ram","memory writes differ at write 1"},
            {"vdp_late","port accesses differ at access 1"},{"psg_late","port accesses differ at access 1"},
            {"latch","vdp.control_latch:"},{"open_bus","machine.open_bus: different (values redacted)\n"},
            {"extra_port_read","port accesses differ at access 1"},
            {"iff1","cpu.iff1: oracle=0, engine=1"},{"iff2","cpu.iff2: oracle=0, engine=1"},{"im","cpu.im: oracle=1, engine=2"},
            {"i","cpu.i:"},{"sp","cpu.sp:"},{"alternates","cpu.de_:"},{"request","arbitration.request: oracle=0, engine=4"},
            {"scheduled_nmi","machine.scheduled_nmi: oracle=none"},{"pad","machine.pending_pads[0].up: oracle=0, engine=1"}};
        for (const auto& [test,field]:expected) if (name==test)
            check(text.find(field)!=std::string::npos,"Missing expected field: "+std::string(field)+"\n"+text);
        if (name=="vdp_late") check(text.find("vdp.vram[")!=std::string::npos,"A late VDP write also leaves different video memory\n"+text);
        if (name=="fetches") check(text.find("cpu.r:")==std::string::npos,"128 fetches preserve R\n"+text);
        if (name=="open_bus") check(text.find("oracle=")==std::string::npos && text.find("engine=")==std::string::npos,
            "Open-bus fault must never expose either value\n"+text);
    }
}

// ----------------------------------------------------------------------------
void simple_body(RoutineContext& c, std::uint16_t at) {
    Direct sync; RoutineContext::SyncScope scope(c,sync);
    nop(c,at); nop(c,static_cast<std::uint16_t>(at+1)); render_return(c,static_cast<std::uint16_t>(at+2));
}
void dispatch() {
    // Initial VDP frame IRQ is asserted, with IFF1 clear: dispatches exactly.
    {
        Machine m(program({0,0,0xc9})); entry(m);
        RoutineRegistry r;
        r.add_exact({0,0},segment({{R,2}}),cost(18),[](RoutineContext& c) { simple_body(c,0); });
        oracle::Lockstep ls(m,"dispatch");
        returned_with(ls,m,&r);
        check(r.find({0,0})->counters.cpp_calls==1,"IFF1 clear with INT asserted dispatches exactly");
    }
    // The instruction after EI: a body that is not exact falls back (named,
    // counted) and the native instruction equals the oracle's.
    {
        auto rom=program({0,0,0xc9}); (*rom)[0x100]=0xfb;
        (*rom)[0x38]=0xdb; (*rom)[0x39]=0xbf; (*rom)[0x3a]=0xc9; // IN A,(BF); RET: the frame interrupt handler
        Machine delay(rom); entry(delay); delay.cpu().pc.uint16_value=0x100;
        delay.step(); delay.cpu().pc.uint16_value=0;
        RoutineRegistry r;
        r.add({0,0},segment({{R,2}}),cost(18),[](RoutineContext& c) { c.fetch_opcode(0); c.advance(4); c.fetch_opcode(1); c.advance(4); scaffold::native_return(c,2); });
        const auto text=failure([&]{ delay.step(r); });
        check(text.find("refused (ei_delay)")!=std::string::npos && r.find({0,0})->counters.fallback_calls==1 &&
              r.find({0,0})->counters.fallback(RoutineFallback::ei_delay)==1,"EI delay refuses a body without register sync, named and counted");
        // An exact body takes the same state inside itself instead.
        Machine exact(rom); entry(exact); exact.cpu().pc.uint16_value=0x100; exact.step(); exact.cpu().pc.uint16_value=0;
        RoutineRegistry e;
        e.add_exact({0,0},segment({{R,2}}),cost(18),[](RoutineContext& c) { simple_body(c,0); });
        oracle::Lockstep exact_ls(exact,"ei delay exact",oracle::Mode::instructions);
        returned_with(exact_ls,exact,&e);
        check(e.find({0,0})->counters.cpp_calls==1 && e.find({0,0})->counters.fallback_calls==0,"An exact body is admitted after EI");
    }
    // A prefix the test scaffolding suspends at resumes like the native core.
    {
        Machine prefixed(program({0xdd,0x21,0,0,0xc9})); entry(prefixed);
        prefixed.advance(Machine::frame_cycles-20);
        RoutineRegistry none;
        oracle::NativeMachine native(prefixed);
        for (unsigned i=0;i<3;++i) {
            prefixed.step(none); native.step();
            if (i==0) check(prefixed.cpu().resume==Z80::resume_xy,"Fixture suspends at a prefix");
            check(oracle::equal(prefixed,native,oracle::Scope::whole),"A prefix steps like the native core");
        }
        check(none.census().scaffold_steps==3,"the prefix ran on the scaffolding");
    }
    // An interrupt at the entry is accepted first (in C++): no call is
    // started or counted; the handler returns to the registered entry and the
    // call is dispatched then.
    {
        auto rom=program({0,0,0xc9});
        (*rom)[0x38]=0xdb; (*rom)[0x39]=0xbf; (*rom)[0x3a]=0xc9; // IN A,(BF); RET: the handler clears the source
        Machine irq(rom); entry(irq); irq.cpu().iff1=irq.cpu().iff2=1; irq.advance(1);
        RoutineRegistry r;
        r.add_exact({0,0},segment({{R,2}}),cost(18),[](RoutineContext& c) { simple_body(c,0); });
        oracle::Lockstep ls(irq,"entry interrupt");
        ls.step(irq,&r);
        check(irq.cpu().pc.uint16_value==0x38,"Entry interrupt is accepted first");
        check(r.find({0,0})->counters.cpp_calls==0 && r.find({0,0})->counters.fallback_calls==0,
            "An accepted interrupt starts no routine call");
        ls.step(irq,&r); ls.step(irq,&r);
        check(irq.cpu().pc.uint16_value==0,"Handler returns to the registered entry");
        returned_with(ls,irq,&r);
        check(r.find({0,0})->counters.cpp_calls==1,"Call dispatches after handler returns");
    }
}
void clean() {
    auto rom=program({0xcd,0x00,0x01,0xc3,0,0}); (*rom)[0x102]=0xc9;
    Machine m(rom);
    RoutineRegistry r;
    r.add_exact({0,0x100},segment({{R,0x102}}),cost(18),[](RoutineContext& c) { simple_body(c,0x100); });
    // The same frames without the lockstep: the lockstep must leave the engine
    // identical to a plain run (review R3 L6).
    Machine plain(rom);
    RoutineRegistry plain_routines;
    plain_routines.add_exact({0,0x100},segment({{R,0x102}}),cost(18),[](RoutineContext& c) { simple_body(c,0x100); });
    oracle::Lockstep ls(m,"clean");
    for (unsigned frame=1;frame<=3;++frame) {
        FrameInput input; input.pads[0].one=frame==2;
        plain.run_frame(input,plain_routines,{},{},[&] { (void)plain.take_psg_writes(); (void)plain.take_samples(); });
        bool observed=false;
        ls.run_frame(m,input,&r,[&](const Machine& engine,oracle::FrameSample&) {
            observed=true;
            // The lockstep compared the whole machines at this observer; the
            // recording manifests of the two agree as well.
            replay::compare_manifests("clean.psrec",frame,replay::manifest(ls.oracle(),{}),replay::manifest(engine,{}));
        });
        check(observed,"The frame observer ran");
        check(oracle::equal(m,plain,oracle::Scope::whole) && oracle::queue_differences(m,plain).empty(),
              "The lockstep leaves the engine identical to a plain run");
    }
    check(r.find({0,0x100})->counters.cpp_calls>100,"Clean lockstep exercises many real calls");
    check(ls.frame_comparisons()==3,"Three whole-machine comparisons");
    replay::Manifest good{};
    for (unsigned i=0;i<7;++i) {
        auto bad=good; bad[i]="different";
        const auto text=failure([&] { replay::compare_manifests("clean.psrec",2,good,bad); });
        check(text=="Recording divergence: recording clean.psrec, frame 2, component "+std::string(replay::manifest_components[i]),
            "Recording compare names the exact frame and component");
    }
}
// The admission windows of a body that is not exact: a frame end, an IRQ or
// an NMI inside its bound refuse the call (counted; the run stops, there is no
// core to run it), a clear window runs it in C++, equal to the oracle.
void windows() {
    auto body=[](RoutineContext& c) {
        c.fetch_opcode(0); c.advance(4); c.fetch_opcode(1); c.advance(4); scaffold::native_return(c,2);
    };
    enum class Event { none, frame, irq, nmi };
    for (const auto event:{Event::none,Event::frame,Event::irq,Event::nmi}) {
        Machine m(program({0,0,0xc9})); entry(m);
        if (event==Event::frame) m.advance(Machine::frame_cycles-8);
        if (event==Event::irq) { m.read_port(0xbf,0); m.cpu().iff1=m.cpu().iff2=1; m.advance(43980); }
        if (event==Event::nmi) m.schedule_nmi(m.cycles()+10);
        RoutineRegistry r;
        r.add({0,0},segment({{R,2}}),cost(event==Event::irq ? 40 : 18),body);
        if (event==Event::none) { oracle::Lockstep ls(m,"windows"); ls.step(m,&r); }
        else (void)failure([&]{ m.step(r); });
        const auto& counters=r.find({0,0})->counters;
        const auto reason=event==Event::frame ? RoutineFallback::frame : event==Event::irq ? RoutineFallback::irq : RoutineFallback::nmi;
        if (event==Event::none) check(counters.cpp_calls==1 && counters.fallback_calls==0,"A clear window runs the body in C++");
        else check(counters.cpp_calls==0 && counters.fallback(reason)==1,"A window with an event inside refuses the call");
    }
}
void fields() {
    // An authored component models adding a field without updating its list.
    // Exercise the same backstop used by all three real components.
    struct Component {
        int listed=0, unlisted=0;
        bool operator==(const Component&) const = default;
    };
    for (const std::string name:{"memory","vdp","psg"}) {
        Component original, replacement;
        DifferenceList list;
        list.backstop(name,original,replacement);
        check(list.fields.empty(),"Equal components must not trigger the backstop");
        replacement.unlisted=0x73;
        list.add(name+".listed",original.listed,replacement.listed);
        list.add("other.listed",0,1);
        list.backstop(name,original,replacement);
        check(list.fields.size()==2,"Another component must not suppress an unlisted field");
        const auto& d=list.fields.back();
        check(d.field==name+" (unlisted field)" && d.can_carry_rom &&
            d.original.empty() && d.replacement.empty(),"Unlisted fields must be named with both values redacted");
        list.fields.clear();
        replacement.listed=1;
        list.add(name+".listed",original.listed,replacement.listed);
        list.backstop(name,original,replacement);
        check(list.fields.size()==1 && list.fields[0].field==name+".listed",
            "A listed difference must not gain a duplicate backstop report");
    }
    Machine a(program({0})); entry(a); Machine b=a;
    const auto before=a;
    VdpFixture::change_cache(b.vdp());
    check(before.differences(b).empty() && before.equivalent(b),"Derived IRQ cache remains excluded from equivalence");
    b=before;
    check(a.peek(0xc101)==2 && a.equivalent(before) && a.last_access()==before.last_access(),"Peeking a stack byte is side-effect free");
    a.memory().set_memory_control(0x10); const auto unmapped=a;
    check(a.peek(0xc101)==a.peek(0xc100) && a.equivalent(unmapped) && a.last_access()==unmapped.last_access(),"Unmapped peek leaves open bus untouched");
    b.cpu().data.uint8_array[0]^=1;
    auto diff=before.differences(b);
    check(diff.size()==1 && diff[0].field=="cpu.data[0]" && diff[0].can_carry_rom && diff[0].original.empty() && diff[0].replacement.empty(),"Core data bytes are named without values");
    b=before; b.cpu().i^=1;
    diff=before.differences(b);
    check(diff.size()==1 && diff[0].field=="cpu.i" && diff[0].can_carry_rom && diff[0].original.empty(),"I can retain a byte read by the program");
    b=before; VdpFixture::raw_latches(b.vdp());
    diff=before.differences(b);
    check(diff.size()==4 && std::all_of(diff.begin(),diff.end(),[](const auto& d) {
        return d.can_carry_rom && d.original.empty() && d.replacement.empty();
    }),"Scroll and sprite position latches carry raw read bytes and must be redacted");
    b=before; b.cpu().r^=1;
    b.memory().set_memory_control(0x37); b.write_port(0x3f,0x6d,b.cycles());
    diff=before.differences(b);
    unsigned raw_fields=0;
    for (const auto& d:diff) if (d.field=="cpu.r" ||
        d.field=="machine.io_control" || d.field=="memory.memory_control") {
        ++raw_fields;
        check(d.can_carry_rom && d.original.empty() && d.replacement.empty(),"Raw byte copies stay redacted");
    }
    check(raw_fields==3,"All three raw byte copies are reported");
    b=before; b.cpu().memptr.uint16_value^=1;
    check(!before.equivalent(b) && !before.differences(b).empty(),"Equivalence is exactly an empty difference list");
    b=before; b.cpu().af.uint8_values.at_1=b.read_port(0xbf,b.cycles());
    diff=before.differences(b);
    check(std::any_of(diff.begin(),diff.end(),[](const auto& d){return d.field=="cpu.a" && d.can_carry_rom && d.original.empty();}),"Port-read results retained in a register are redacted");
}

void nmi_reject() {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    auto put=[&](std::uint16_t at, std::initializer_list<std::uint8_t> bytes) { for (auto b:bytes) (*rom)[at++]=b; };
    // The NMI handler counts in C011 and returns with RETN; the body is 24
    // times LD A,n / LD (C100),A so that it runs across the Pause sample.
    put(0x0066,{0xF5,0x3A,0x11,0xC0,0x3C,0x32,0x11,0xC0,0xF1,0xED,0x45});
    std::vector<std::uint8_t> code;
    for (int i=0;i<24;++i) code.insert(code.end(),{0x3E,static_cast<std::uint8_t>(i),0x32,0x00,0xC1});
    code.push_back(0xC9);
    for (std::size_t i=0;i<code.size();++i) (*rom)[0x0100+i]=code[i];
    put(0x0200,{0x18,0xFE});
    const auto end=static_cast<std::uint16_t>(0x0100+code.size()-1);
    auto build=[&](RoutineRegistry& registry) {
        k::add_segment(registry,{0,0x0100},k::segment({0,0x0100},{k::returned_at(end)}),2*Machine::frame_cycles,[=](auto& z) {
            for (int i=0;i<24;++i) {
                const auto at=static_cast<std::uint16_t>(0x0100+5*i);
                z.load_immediate(at,z.r.a); z.store_a_direct(at+2);
            }
            return scaffold::returns(end,k::return_cycles);
        });
    };
    const std::uint64_t pause_edge=Machine::frame_cycles+Machine::pause_sample_offset;
    unsigned discarded=0, taken=0, runs=0;
    for (std::uint64_t k_offset=0;k_offset<60;++k_offset) {
        Machine m(rom);
        (void)m.read_port(0xBF,m.cycles()); m.advance(1);
        auto& cpu=m.cpu();
        cpu.im=1; cpu.iff1=cpu.iff2=0; cpu.sp.uint16_value=0xC1F0;
        m.memory().write(0xC1F0,0x00); m.memory().write(0xC1F1,0x02);
        // Pause is latched at the frame boundary: raise it, pass the boundary,
        // and start the body about a hundred cycles before the edge.
        m.set_pause(true);
        m.advance(pause_edge-120-m.cycles());
        cpu.pc.uint16_value=0x0100;
        // The first NMI, k_offset cycles before the Pause edge.
        m.schedule_nmi(pause_edge-k_offset);
        RoutineRegistry registry; build(registry);
        oracle::Lockstep ls(m,"nmi reject",oracle::Mode::instructions);
        for (unsigned i=0;i<2000 && m.cpu().pc.uint16_value!=0x0200;++i) ls.step(m,&registry);
        check(m.cpu().pc.uint16_value==0x0200,"the body returned");
        const auto count=m.peek(0xC011);
        check(count==1 || count==2,"the handler ran once or twice");
        (count==1 ? discarded : taken)++;
        ++runs;
        if (count==1) check(registry.find({0,0x0100})->counters.nmi_deliveries==1,"one NMI was delivered inside the body");
    }
    check(discarded>0 && taken>0,"the sweep covers an edge inside the response and one outside it");
    std::cout<<"nmi_reject: "<<runs<<" runs, "<<discarded<<" with the second edge discarded, "<<taken<<" with both taken\n";
}
}
int main(int argc,char** argv) {
    // The authored programs' callers and handlers run on the oracle's core.
    oracle::Scaffold scaffold;
    try {
        check(argc==2,"Expected test case"); const std::string_view test=argv[1];
        if (test=="dispatch") dispatch(); else if(test=="clean") clean();
        else if(test=="windows") windows(); else if(test=="fields") fields();
        else if(test=="nmi_reject") nmi_reject(); else fault(test);
        std::cout<<"Lockstep test passed: "<<test<<'\n'; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
