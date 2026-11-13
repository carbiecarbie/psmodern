// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/verification.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace psmodern;
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
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> std::string failure(F run) {
    try { run(); } catch (const std::runtime_error& e) { return e.what(); }
    throw std::runtime_error("Expected verification failure");
}
auto program(std::initializer_list<std::uint8_t> bytes) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    std::copy(bytes.begin(),bytes.end(),rom->begin()); return rom;
}
void entry(Machine& m) {
    m.cpu().pc.uint16_value=0; m.cpu().sp.uint16_value=0xc100;
    m.memory().write(0xc100,0); m.memory().write(0xc101,2);
}
auto cost(std::uint64_t bound) { return [=](const Machine&) {return bound;}; }
void simple(RoutineContext& c) {
    c.fetch_opcode(0); c.advance(4); c.fetch_opcode(1); c.advance(4); c.finish_return(2);
}
void returned(Machine& m) {
    for (unsigned n=0;n<1000;++n) {
        m.step();
        if (m.cpu().pc.uint16_value==0x200 && m.cpu().sp.uint16_value==0xc102) return;
    }
    throw std::runtime_error("Synthetic call did not return");
}
void fault(std::string_view name) {
    const bool port=name=="vdp_late" || name=="psg_late";
    auto rom=port ? program({0x3e,0x90,0xd3,static_cast<std::uint8_t>(name=="vdp_late" ? 0xbe : 0x7f),0,0,0xc9})
                  : program({0,0,0xc9});
    Machine m(rom); entry(m);
    // Place the VDP access on opposite sides of the dot-35 CPU slot.
    if (port) m.advance(5);
    RoutineRegistry r; replay::CallVerifier verifier("synthetic.psrec"); r.verifier=&verifier;
    unsigned calls=0;
    r.add({0,0},[&](const Machine&) {return name=="bound" && calls==1 ? 17 : port ? 36 : 18;},
        [&](RoutineContext& c) {
            const bool inject=++calls==2;
            auto& z=c.cpu();
            if (port) {
                c.fetch_opcode(0); z.af.uint8_values.at_1=c.read(1); c.advance(7);
                c.fetch_opcode(2); const auto p=c.read(3);
                c.write_port(p,z.af.uint8_values.at_1,inject ? 18 : 17);
                c.advance(18-c.elapsed());
                c.fetch_opcode(4); c.advance(4); c.fetch_opcode(5); c.advance(4); c.finish_return(6);
            } else {
                c.fetch_opcode(0); c.advance(4); c.fetch_opcode(1);
                if (inject && name=="fetches") c.account_fetches(128);
                if (inject && name=="ram") c.write(0xc020,0x73);
                c.advance(inject && name=="cycle" ? 3 : 4); c.finish_return(2);
                if (inject && name=="r") z.r^=1;
                if (inject && name=="flag") z.af.uint8_values.at_0^=1;
                if (inject && name=="memptr") z.memptr.uint16_value^=1;
                if (inject && name=="q") z.q^=1;
                if (inject && name=="latch") m.write_port(0xbf,0x37,m.cycles());
                if (inject && name=="open_bus") (void)z.read(z.context,0xc020);
            }
        });
    m.step(r); check(calls==1,"First call must pass cleanly");
    entry(m);
    // Restore the slot phase for the timing fault's second call.
    if (port) { m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles+5); m.take_samples(); }
    const auto text=failure([&] { m.step(r); });
    check(text.find("recording synthetic.psrec, frame ")!=std::string::npos &&
        text.find(", cycle ")!=std::string::npos && text.find("routine 00:0000, call 2\n")!=std::string::npos,
        "Failure must identify recording, frame, cycle, entry and second call");
    const std::pair<std::string_view,std::string_view> expected[]{
        {"cycle","machine.cycle:"},{"r","cpu.r:"},{"fetches","machine.opcode_fetches:"},
        {"flag","cpu.f:"},{"memptr","cpu.memptr:"},{"q","cpu.q:"},{"ram","memory.ram[32]:"},
        {"vdp_late","vdp.vram[15723]:"},{"psg_late","psg.writes[1].cycle:"},
        {"latch","vdp.control_latch:"},{"open_bus","machine.open_bus: different (values redacted)\n"},
        {"bound","understated bound"}};
    for (const auto& [test,field]:expected) if (name==test) {
        if (text.find(field)==std::string::npos) throw std::runtime_error("Missing expected field: "+std::string(field)+"\n"+text);
    }
    if (name=="fetches") check(text.find("cpu.r:")==std::string::npos,"128 fetches preserve R");
    if (name=="open_bus") check(text.find("original=")==std::string::npos && text.find("replacement=")==std::string::npos,
        "Open-bus fault must never expose either value");
    check(r.find({0,0})->counters.cpp_calls==2,"Verifier must stop at the failing call");
}
void dispatch() {
    // Initial VDP frame IRQ is asserted, with IFF1 clear.
    Machine off(program({0,0,0xc9})); entry(off); Machine native=off;
    RoutineRegistry r; replay::CallVerifier verifier("dispatch.psrec"); r.verifier=&verifier;
    r.add({0,0},cost(18),simple);
    off.step(r); returned(native);
    check(off.equivalent(native) && r.find({0,0})->counters.cpp_calls==1,"IFF1 clear with INT asserted dispatches exactly");

    auto rom=program({0,0,0xc9}); (*rom)[0x100]=0xfb;
    Machine delay(rom); entry(delay); delay.cpu().pc.uint16_value=0x100;
    delay.step(); delay.cpu().pc.uint16_value=0;
    native=delay; const auto before=r.find({0,0})->counters;
    delay.step(r); native.step();
    check(delay.equivalent(native),"EI-delayed fallback must execute the same native instruction");
    check(r.find({0,0})->counters.fallback_calls==before.fallback_calls+1 &&
        r.find({0,0})->counters.fallback(RoutineFallback::ei_delay)==1,"EI delay has a named counted fallback");

    // Resuming a prefix is still the same native instruction and call.
    for (bool disable:{false,true}) {
        Machine prefixed(program({0xdd,0x21,0,0,0xc9})); entry(prefixed);
        if (!disable) prefixed.advance(Machine::frame_cycles-20);
        RoutineRegistry fallback; fallback.enabled=!disable;
        fallback.add({0,0},cost(24),[](RoutineContext&) {throw std::runtime_error("Fallback body ran");});
        native=prefixed;
        prefixed.step(fallback); native.step();
        check(prefixed.cpu().resume==Z80_RESUME_XY,"Fixture suspends at a prefix");
        prefixed.step(fallback); native.step();
        prefixed.step(fallback); native.step();
        check(prefixed.equivalent(native) && fallback.find({0,0})->counters.fallback_calls==1 &&
            fallback.find({0,0})->counters.fallback(disable ? RoutineFallback::disabled : RoutineFallback::frame)==1,
            "Resuming a prefix must not count the same fallback call again");
    }
    // The handler clears the source before restoring the interrupted PC.
    (*rom)[0x38]=0xdb; (*rom)[0x39]=0xbf; (*rom)[0x3a]=0xc9;
    Machine irq(rom); entry(irq); irq.cpu().iff1=irq.cpu().iff2=1; irq.advance(1);
    native=irq; const auto calls=r.find({0,0})->counters.cpp_calls;
    irq.step(r); native.step();
    check(irq.equivalent(native) && irq.cpu().pc.uint16_value==0x38,"Entry interrupt is accepted first");
    check(r.find({0,0})->counters.cpp_calls==calls && r.find({0,0})->counters.fallback_calls==1,
        "An accepted interrupt starts no routine call");
    irq.step(r); irq.step(r); native.step(); native.step();
    check(irq.cpu().pc.uint16_value==0,"Handler returns to the registered entry");
    irq.step(r); returned(native);
    check(irq.equivalent(native) && r.find({0,0})->counters.cpp_calls==calls+1,"Call dispatches after handler returns");
}
void clean() {
    auto rom=program({0xcd,0x00,0x01,0xc3,0,0}); (*rom)[0x102]=0xc9;
    Machine native(rom), plain=native, checked=native;
    RoutineRegistry a,b; replay::CallVerifier verifier("clean.psrec"); b.verifier=&verifier;
    auto body=[](RoutineContext& c) {c.fetch_opcode(0x100);c.advance(4);c.fetch_opcode(0x101);c.advance(4);c.finish_return(0x102);};
    a.add({0,0x100},cost(18),body); b.add({0,0x100},cost(18),body);
    replay::FrameWrites na,pa,ca;
    for (unsigned frame=1;frame<=3;++frame) {
        FrameInput input; input.pads[0].one=frame==2;
        native.run_frame(input); plain.run_frame(input,a); checked.run_frame(input,b);
        check(plain.equivalent(checked) && plain.last_access()==checked.last_access(),"Lockstep option preserves the real machine and timing");
        const auto n=replay::manifest(native,na.take(frame,native.take_psg_writes()));
        const auto p=replay::manifest(plain,pa.take(frame,plain.take_psg_writes()));
        const auto c=replay::manifest(checked,ca.take(frame,checked.take_psg_writes()));
        replay::compare_manifests("clean.psrec",frame,n,p); replay::compare_manifests("clean.psrec",frame,n,c);
        native.take_samples(); plain.take_samples(); checked.take_samples();
    }
    check(b.find({0,0x100})->counters.cpp_calls>100,"Clean verification exercises many real calls");
    replay::Manifest good{};
    for (unsigned i=0;i<7;++i) {
        auto bad=good; bad[i]="different";
        const auto text=failure([&] { replay::compare_manifests("clean.psrec",2,good,bad); });
        check(text=="Recording divergence: recording clean.psrec, frame 2, component "+std::string(replay::manifest_components[i]),
            "Recording compare names the exact frame and component");
    }
}
void windows() {
    // Invoke the verifier directly to simulate a broken admission query.
    replay::CallVerifier verifier("window.psrec");
    const SegmentSpec spec{{0,0},{}};
    Machine frame(program({0,0,0xc9})); entry(frame); frame.advance(Machine::frame_cycles-8);
    auto before=frame;
    auto text=failure([&] { verifier.run(frame,{0,0},spec,false,18,7,[]()->std::optional<SegmentExit> {throw std::runtime_error("Body must not run");}); });
    check(text.find("window-check failure: frame boundary")!=std::string::npos && frame.equivalent(before),"Shadow detects frame crossing without touching real state");
    Machine irq(program({0,0,0xc9})); entry(irq); irq.read_port(0xbf,0); irq.cpu().iff1=irq.cpu().iff2=1;
    irq.advance(43980); before=irq;
    text=failure([&] { verifier.run(irq,{0,0},spec,false,40,8,[]()->std::optional<SegmentExit> {return {};}); });
    check(text.find("window-check failure: interrupt accepted")!=std::string::npos && irq.equivalent(before),"Shadow independently detects native interrupt acceptance");
}
void exits() {
    // A core_final exit one cycle before the frame end crosses it; the verifier
    // rejects the window before the body runs.
    replay::CallVerifier verifier("exits.psrec");
    const SegmentSpec spec{{0,0},{{RoutineExit::core_final,0}},false,0,true};
    const auto never=[]()->std::optional<SegmentExit> {throw std::runtime_error("Body must not run");};
    Machine frame(program({0,0,0xc9})); entry(frame); frame.advance(Machine::frame_cycles-1);
    auto before=frame;
    auto text=failure([&] { verifier.run(frame,{0,0},spec,false,4,1,never); });
    check(text.find("window-check failure: frame boundary")!=std::string::npos && frame.equivalent(before),
          "A core_final step crossing the frame end is a window failure");
    // The step costs 4 cycles: a bound of 3 is one cycle too small.
    Machine over(program({0,0,0xc9})); entry(over); before=over;
    text=failure([&] { verifier.run(over,{0,0},spec,false,3,1,never); });
    check(text.find("understated bound")!=std::string::npos && over.equivalent(before),
          "A core_final step over its bound is reported");
    // Only a forced call may return no exit.
    Machine missing(program({0,0,0xc9})); entry(missing);
    text=failure([&] { verifier.run(missing,{0,0},spec,false,4,1,[]()->std::optional<SegmentExit> {return std::nullopt;}); });
    check(text.find("missing exit")!=std::string::npos,"A non-forced body without an exit is a verifier failure");
    Machine forced(program({0,0,0xc9})); entry(forced);
    verifier.run(forced,{0,0},SegmentSpec{{0,0},{{RoutineExit::core_final,0}},false,0,true,true},true,4,1,
        []()->std::optional<SegmentExit> {return std::nullopt;});
}
void returned_with(Machine& m, RoutineRegistry* r) {
    for (unsigned n=0;n<1000;++n) {
        if (m.cpu().pc.uint16_value==0x200 && m.cpu().sp.uint16_value==0xc102) return;
        m.step({},r);
    }
    throw std::runtime_error("Synthetic segments did not return");
}
constexpr auto R=RoutineExit::returned;
constexpr auto F=RoutineExit::core_final;
SegmentSpec segment(std::initializer_list<SegmentExit> exits) { return {{0,0},exits}; }
SegmentSpec forced_spec() { return {{0,0},{{F,0}},false,0,true,true}; }
void nop(RoutineContext& c, std::uint16_t pc) { c.fetch_opcode(pc); c.advance(4); }
auto image(const std::vector<std::pair<std::uint16_t,std::vector<std::uint8_t>>>& parts) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    for (const auto& [at,bytes]:parts) std::copy(bytes.begin(),bytes.end(),rom->begin()+at);
    return rom;
}
void catch_up(Machine& cpp, Machine& native, const char* message) {
    while (native.cycles()<cpp.cycles()) native.step();
    check(cpp.equivalent(native),message);
    for (unsigned i=0;i<6;++i) check(cpp.step()==native.step() && cpp.equivalent(native),message);
}
// Milestone 7 mechanism under the per-call verifier: held stack words,
// conditional final jumps and loop exits.
SegmentSpec spec_of(std::uint16_t unit, std::initializer_list<SegmentExit> exits, unsigned scratch=0, unsigned inherited=0) {
    SegmentSpec spec{{0,unit},exits}; spec.scratch_stack=scratch; spec.inherited_stack=inherited; return spec;
}
void push_word(RoutineContext& c, std::uint16_t pc, std::uint16_t value) {
    auto& z=c.cpu(); const auto sp=z.sp.uint16_value;
    c.fetch_opcode(pc);
    c.write(static_cast<std::uint16_t>(sp-1),static_cast<std::uint8_t>(value>>8));
    c.write(static_cast<std::uint16_t>(sp-2),static_cast<std::uint8_t>(value));
    z.sp.uint16_value=static_cast<std::uint16_t>(sp-2); c.advance(11);
}
std::uint16_t pop_word(RoutineContext& c, std::uint16_t pc) {
    auto& z=c.cpu(); const auto sp=z.sp.uint16_value;
    c.fetch_opcode(pc);
    const auto lo=c.read(sp), hi=c.read(static_cast<std::uint16_t>(sp+1));
    z.sp.uint16_value=static_cast<std::uint16_t>(sp+2); c.advance(10);
    return static_cast<std::uint16_t>(lo|(hi<<8));
}
// PUSH HL [PUSH DE]; CALL 0100 (or LD A,42 held over); [POP DE] POP HL; RET.
struct Held { std::shared_ptr<Memory::Rom> rom; std::uint16_t final, next; };
Held held_program(unsigned words, bool plain) {
    std::vector<std::uint8_t> code{0xe5};
    if (words==2) code.push_back(0xd5);
    const auto final=static_cast<std::uint16_t>(code.size());
    if (plain) code.insert(code.end(),{0x3e,0x42}); else code.insert(code.end(),{0xcd,0x00,0x01});
    const auto next=static_cast<std::uint16_t>(code.size());
    if (words==2) code.push_back(0xd1);
    code.insert(code.end(),{0xe1,0xc9});
    return {image({{0,code},{0x100,{0x21,0,0,0x11,0,0,0xc9}}}),final,next};
}
std::uint64_t held_first(unsigned words, bool plain) { return 11*words+(plain ? 7 : 17); }
void add_held(RoutineRegistry& r, const Held& p, unsigned words, bool plain) {
    const auto offset=static_cast<std::int8_t>(2*words);
    r.add({0,0},spec_of(0,{{F,p.final,static_cast<std::int8_t>(-offset)}},2*words),cost(held_first(words,plain)),
        [=](RoutineContext& c) {
            push_word(c,0,c.cpu().hl.uint16_value);
            if (words==2) push_word(c,1,c.cpu().de.uint16_value);
            c.finish_core(p.final);
        });
    r.add({0,p.next},spec_of(0,{{R,static_cast<std::uint16_t>(p.next+words),offset}},0,2*words),cost(10*words+10),
        [=](RoutineContext& c) {
            if (words==2) c.cpu().de.uint16_value=pop_word(c,p.next);
            c.cpu().hl.uint16_value=pop_word(c,static_cast<std::uint16_t>(p.next+words-1));
            c.finish_return(static_cast<std::uint16_t>(p.next+words));
        });
}
void held_exits(replay::CallVerifier& verifier) {
    for (unsigned words:{1u,2u}) for (bool plain:{false,true}) {
        const auto p=held_program(words,plain);
        Machine m(p.rom); entry(m); m.cpu().hl.uint16_value=0x1234; m.cpu().de.uint16_value=0x5678;
        Machine native=m;
        RoutineRegistry r; r.verifier=&verifier; add_held(r,p,words,plain);
        returned_with(m,&r); returned(native);
        check(m.equivalent(native) && r.find({0,0})->counters.cpp_calls==1 && r.find({0,p.next})->counters.cpp_calls==1,
              "Verified held-word chain equals the original");
    }
    // Both held-word exits ending exactly at a frame boundary are verified cleanly.
    const auto p=held_program(1,false);
    for (bool first:{true,false}) {
        Machine m(p.rom); entry(m); m.cpu().hl.uint16_value=0x1234;
        if (!first) { m.cpu().pc.uint16_value=4; m.cpu().sp.uint16_value=0xc0fe; m.memory().write(0xc0fe,0x34); m.memory().write(0xc0ff,0x12); }
        const std::uint64_t bound=first ? 28 : 20;
        m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles-bound);
        Machine native=m;
        RoutineRegistry r; r.verifier=&verifier; add_held(r,p,1,false);
        m.step(r);
        check(r.find({0,static_cast<std::uint16_t>(first ? 0 : 4)})->counters.cpp_calls==1 && m.cycles()%Machine::frame_cycles==0,
              "Held-word exit ending exactly at a frame boundary is admitted");
        catch_up(m,native,"Frame boundary at a held-word exit is outside the window");
    }
    // The native path returns at the entry stack, not at the declared +2: the
    // copy does not stop there and runs past the bound.
    {
        Machine m(image({{4,{0x00,0xc9}}})); entry(m);
        m.cpu().pc.uint16_value=4; m.cpu().sp.uint16_value=0xc0fe; m.memory().write(0xc0fe,0x00); m.memory().write(0xc0ff,0x03);
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,4},spec_of(0,{{R,5,2}},0,2),cost(20),[](RoutineContext& c) {
            c.cpu().hl.uint16_value=pop_word(c,4); c.finish_return(5);
        });
        const auto text=failure([&] { m.step(r); });
        check(text.find("understated bound")!=std::string::npos,"A return at an undeclared stack offset is a divergence");
    }
    // Exits are reported with their stack offsets.
    {
        Machine m(program({0xe5,0x3e,0x01,0x3e,0x01})); entry(m);
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0},spec_of(0,{{F,1,-2},{F,3,-2}},2),cost(25),[](RoutineContext& c) {
            push_word(c,0,c.cpu().hl.uint16_value);
            c.fetch_opcode(1); c.cpu().af.uint8_values.at_1=c.read(2); c.advance(7); c.finish_core(3);
        });
        const auto text=failure([&] { m.step(r); });
        check(text.find("exit mismatch: original=core_final 0001 stack -2, replacement=core_final 0003 stack -2\n")!=std::string::npos,
              "Held-word exits are reported with their offsets");
    }
    // False-stop attempt: the native POP of the inherited word leaves SP at
    // entry+2 and PC equal to that word. The copy goes on to the RET [+2].
    {
        Machine m(image({{0x61,{0xe1,0xc9}}})); entry(m);
        m.cpu().pc.uint16_value=0x61; m.cpu().sp.uint16_value=0xc0fe; m.memory().write(0xc0fe,0x62); m.memory().write(0xc0ff,0x00);
        Machine native=m;
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0x61},spec_of(0x61,{{R,0x62,2}},0,2),cost(20),[](RoutineContext& c) {
            c.cpu().hl.uint16_value=pop_word(c,0x61); c.finish_return(0x62);
        });
        m.step(r); returned(native);
        check(m.equivalent(native) && m.cpu().hl.uint16_value==0x62 && r.find({0,0x61})->counters.cpp_calls==1,
              "A POP of the inherited word into PC-equal state does not stop the copy");
    }
    // RET Z at a declared [+2] returned exit: untaken continues to the next
    // return, taken stops there. The held word again equals the PC after the POP.
    for (bool zero:{false,true}) {
        Machine m(image({{0x70,{0xe1,0xc8,0xc9}}})); entry(m);
        m.cpu().pc.uint16_value=0x70; m.cpu().sp.uint16_value=0xc0fe; m.memory().write(0xc0fe,0x71); m.memory().write(0xc0ff,0x00);
        m.cpu().af.uint8_values.at_0=zero ? 0x40 : 0;
        Machine native=m;
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0x70},spec_of(0x70,{{R,0x71,2},{R,0x72,2}},0,2),[](const Machine& x) {
            return x.cpu().af.uint8_values.at_0&0x40 ? 21 : 26;
        },[](RoutineContext& c) {
            c.cpu().hl.uint16_value=pop_word(c,0x70);
            if (c.cpu().af.uint8_values.at_0&0x40) { c.finish_return(0x71); return; }
            c.fetch_opcode(0x71); c.advance(5); c.finish_return(0x72);
        });
        m.step(r); returned(native);
        check(m.equivalent(native) && r.find({0,0x70})->counters.cpp_calls==1,"RET cc at a [+2] exit stops only when taken");
    }
    // A body passing the taken RET Z: the mismatch prints the positive offsets.
    {
        Machine m(image({{0x70,{0xe1,0xc8,0xc9}}})); entry(m);
        m.cpu().pc.uint16_value=0x70; m.cpu().sp.uint16_value=0xc0fe; m.memory().write(0xc0fe,0x71); m.memory().write(0xc0ff,0x00);
        m.cpu().af.uint8_values.at_0=0x40;
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0x70},spec_of(0x70,{{R,0x71,2},{R,0x72,2}},0,2),cost(26),[](RoutineContext& c) {
            c.cpu().hl.uint16_value=pop_word(c,0x70); c.fetch_opcode(0x71); c.advance(5); c.finish_return(0x72);
        });
        const auto text=failure([&] { m.step(r); });
        check(text.find("exit mismatch: original=returned 0071 stack +2, replacement=returned 0072 stack +2\n")!=std::string::npos,
              "Positive offsets are printed with their sign");
    }
}
struct Conditional { std::vector<std::uint8_t> code; unsigned length; bool taken; std::uint8_t flags, b; std::uint64_t cycles; };
std::vector<Conditional> conditional_cases() {
    constexpr unsigned bits[]{6,0,2,7};
    std::vector<Conditional> cases;
    for (bool taken:{true,false}) {
        for (unsigned condition=0;condition<8;++condition) {
            const auto flag=static_cast<std::uint8_t>(((condition&1)==static_cast<unsigned>(taken))<<bits[condition/2]);
            cases.push_back({{static_cast<std::uint8_t>(0xc2+8*condition),0x10,0x00},3,taken,flag,0,10});
            if (condition<4) cases.push_back({{static_cast<std::uint8_t>(0x20+8*condition),0x0d},2,taken,flag,0,taken ? 12u : 7u});
        }
        cases.push_back({{0x10,0x0d},2,taken,0,static_cast<std::uint8_t>(taken ? 5 : 1),taken ? 13u : 8u});
    }
    cases.push_back({{0x10,0x0d},2,true,0,0,13}); // DJNZ with B=0 wraps to 255 and is taken
    return cases;
}
void conditional_exits(replay::CallVerifier& verifier) {
    for (const auto& k:conditional_cases()) for (bool at_frame:{false,true}) {
        std::vector<std::uint8_t> code{0x00};
        code.insert(code.end(),k.code.begin(),k.code.end()); code.push_back(0xc9);
        Machine m(image({{0,code},{0x10,{0x00,0xc9}}})); entry(m);
        m.cpu().af.uint8_values.at_0=k.flags; m.cpu().bc.uint8_values.at_1=k.b;
        // The exact cost, so that the frame boundary falls at the jump's end.
        if (at_frame) m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles-(4+k.cycles));
        Machine native=m;
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0},segment({{F,1,0,true}}),cost(4+k.cycles),[](RoutineContext& c) { nop(c,0); c.finish_core(1); });
        m.step(r);
        check(r.find({0,0})->counters.cpp_calls==1 && m.cpu().pc.uint16_value==(k.taken ? 0x10 : 1+k.length) &&
              (!at_frame || m.cycles()%Machine::frame_cycles==0),"Verified conditional final jump");
        catch_up(m,native,"Conditional final jump equals the original");
    }
    // A body leaving the wrong flag sends the core the other way: the verifier
    // names PC and flags, not the exit.
    for (std::uint8_t op:{0xca,0x28}) {
        Machine m(image({{0,{0x00,op,0x10,0x00,0xc9}},{0x10,{0xc9}}})); entry(m); m.cpu().af.uint8_values.at_0=0x40;
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0},segment({{F,1,0,true}}),cost(16),[](RoutineContext& c) {
            nop(c,0); c.cpu().af.uint8_values.at_0^=0x40; c.finish_core(1);
        });
        const auto text=failure([&] { m.step(r); });
        check(text.find("cpu.pc:")!=std::string::npos && text.find("cpu.f:")!=std::string::npos &&
              text.find("exit mismatch")==std::string::npos,"A wrong flag at a conditional exit names PC and flags");
    }
    // A wrong B at DJNZ likewise.
    {
        Machine m(image({{0,{0x00,0x10,0x0d,0xc9}},{0x10,{0xc9}}})); entry(m); m.cpu().bc.uint8_values.at_1=1;
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0},segment({{F,1,0,true}}),cost(17),[](RoutineContext& c) {
            nop(c,0); c.cpu().bc.uint8_values.at_1=2; c.finish_core(1);
        });
        const auto text=failure([&] { m.step(r); });
        check(text.find("cpu.pc:")!=std::string::npos && text.find("cpu.bc:")!=std::string::npos,"A wrong B at DJNZ names PC and B");
    }
    // A conditional exit with a held word and a loop threshold: PUSH HL; NOP;
    // JR NZ. The arrival at the jump is 15 cycles after the entry.
    for (bool taken:{true,false}) for (std::uint16_t after:{15,16}) {
        Machine m(image({{0,{0xe5,0x00,0x20,0x0c,0xc9}},{0x10,{0xc9}}})); entry(m);
        m.cpu().hl.uint16_value=0x0200; m.cpu().af.uint8_values.at_0=taken ? 0 : 0x40;
        Machine native=m;
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0},spec_of(0,{{F,2,-2,true,after}},2),cost(27),[](RoutineContext& c) {
            push_word(c,0,c.cpu().hl.uint16_value); nop(c,1); c.finish_core(2);
        });
        if (after==16) {
            // Unverified, the context refuses the early arrival itself.
            r.verifier=nullptr;
            const auto text=failure([&] { m.step(r); });
            check(text.find("loop exit before its declared cycles")!=std::string::npos,"A conditional loop exit is refused before its cycles");
            continue;
        }
        m.step(r);
        check(r.find({0,0})->counters.cpp_calls==1 && m.cpu().sp.uint16_value==0xc0fe &&
              m.cpu().pc.uint16_value==(taken ? 0x10 : 4),"Verified conditional exit with a held word and a threshold");
        catch_up(m,native,"Conditional held-word exit equals the original");
    }
}
// Authored scans (same shapes as tests/routine_tests.cpp):
//   plain at base: LD A,(HL); CP C; JR Z,+3; INC HL; JR -7; RET
//   push  at base: PUSH BC; LD A,(HL); CP C; JR Z,+4; INC HL; JR -7; NOP; POP BC; RET
std::vector<std::uint8_t> scan_code(bool push) {
    if (push) return {0xc5,0x7e,0xb9,0x28,0x04,0x23,0x18,0xf9,0x00,0xc1,0xc9};
    return {0x7e,0xb9,0x28,0x03,0x23,0x18,0xf9,0xc9};
}
std::uint8_t compare_flags(std::uint8_t a, std::uint8_t v) {
    const auto result=static_cast<std::uint8_t>(a-v);
    return static_cast<std::uint8_t>((result&0x80) | (result ? 0 : 0x40) | (v&0x28) | ((a^v^result)&0x10) |
        (((a^v)&(a^result)&0x80) ? 0x04 : 0) | 0x02 | (a<v ? 0x01 : 0));
}
enum class Start { plain, push, resume };
std::uint16_t loop_of(std::uint16_t base, Start start) { return start==Start::plain ? base : static_cast<std::uint16_t>(base+1); }
std::uint64_t scan_cost(const Machine& m, Start start, std::uint16_t after) {
    std::uint64_t cycles=start==Start::push ? 11 : start==Start::resume ? 12 : 0;
    auto hl=m.cpu().hl.uint16_value; const auto c=m.cpu().bc.uint8_values.at_0;
    for (;;) {
        cycles+=11;
        if (m.peek(hl)==c) return cycles+12+(start==Start::plain ? 0 : 10)+10;
        cycles+=7;
        if (cycles>=after) return cycles+6;
        cycles+=18; ++hl;
    }
}
enum class Stop { first, late, early };
void scan_body(RoutineContext& c, std::uint16_t base, Start start, std::uint16_t after, Stop stop) {
    auto& z=c.cpu();
    const auto loop=loop_of(base,start);
    if (start==Start::push) push_word(c,base,z.bc.uint16_value);
    if (start==Start::resume) {
        const auto jr=static_cast<std::uint16_t>(base+6);
        c.fetch_opcode(jr); (void)c.read(static_cast<std::uint16_t>(jr+1)); z.memptr.uint16_value=loop; c.advance(12);
    }
    bool skipped=false;
    for (;;) {
        c.fetch_opcode(loop); z.af.uint8_values.at_1=c.read(z.hl.uint16_value); c.advance(7);
        c.fetch_opcode(static_cast<std::uint16_t>(loop+1));
        z.af.uint8_values.at_0=compare_flags(z.af.uint8_values.at_1,z.bc.uint8_values.at_0); c.advance(4);
        const auto jz=static_cast<std::uint16_t>(loop+2);
        c.fetch_opcode(jz); const auto offset=static_cast<std::int8_t>(c.read(static_cast<std::uint16_t>(jz+1)));
        if (z.af.uint8_values.at_0&0x40) {
            z.memptr.uint16_value=static_cast<std::uint16_t>(jz+2+offset); c.advance(12);
            if (start==Start::plain) { c.finish_return(static_cast<std::uint16_t>(base+7)); return; }
            z.bc.uint16_value=pop_word(c,static_cast<std::uint16_t>(base+9));
            c.finish_return(static_cast<std::uint16_t>(base+10)); return;
        }
        c.advance(7);
        const auto exit=static_cast<std::uint16_t>(loop+4);
        const bool qualifies=c.elapsed()>=after;
        if ((stop==Stop::first && qualifies) || (stop==Stop::late && qualifies && skipped) ||
            (stop==Stop::early && c.elapsed()+36>=after)) { c.finish_core(exit); return; }
        skipped=skipped || qualifies;
        c.fetch_opcode(exit); ++z.hl.uint16_value; c.advance(6);
        c.fetch_opcode(static_cast<std::uint16_t>(exit+1)); (void)c.read(static_cast<std::uint16_t>(exit+2));
        z.memptr.uint16_value=loop; c.advance(12);
    }
}
void add_scan(RoutineRegistry& r, std::uint16_t base, bool push, std::uint16_t after, Stop stop=Stop::first) {
    const auto exit=static_cast<std::uint16_t>(loop_of(base,push ? Start::push : Start::plain)+4);
    const std::uint64_t extra=stop==Stop::late ? 36 : 0;
    if (!push) {
        r.add({0,base},spec_of(base,{{F,exit,0,false,after},{R,static_cast<std::uint16_t>(base+7)}}),
            [=](const Machine& m){return scan_cost(m,Start::plain,after)+extra;},
            [=](RoutineContext& c){scan_body(c,base,Start::plain,after,stop);});
        return;
    }
    r.add({0,base},spec_of(base,{{F,exit,-2,false,after},{R,static_cast<std::uint16_t>(base+10)}},2),
        [=](const Machine& m){return scan_cost(m,Start::push,after)+extra;},
        [=](RoutineContext& c){scan_body(c,base,Start::push,after,stop);});
    r.add({0,static_cast<std::uint16_t>(base+6)},spec_of(base,{{F,exit,0,false,after},{R,static_cast<std::uint16_t>(base+10),2}},0,2),
        [=](const Machine& m){return scan_cost(m,Start::resume,after)+extra;},
        [=](RoutineContext& c){scan_body(c,base,Start::resume,after,stop);});
}
Machine scan_machine(bool push, unsigned records) {
    Machine m(image({{0x40,scan_code(push)}})); entry(m); m.cpu().pc.uint16_value=0x40;
    for (unsigned i=0;i<records;++i) m.memory().write(static_cast<std::uint16_t>(0xc200+i),static_cast<std::uint8_t>(0x10+i%7));
    m.memory().write(static_cast<std::uint16_t>(0xc200+records),0x99);
    m.cpu().hl.uint16_value=0xc200; m.cpu().bc.uint16_value=0x7799;
    return m;
}
void loop_exits(replay::CallVerifier& verifier) {
    for (bool push:{false,true}) for (std::uint16_t after:{0,40,100,128}) for (unsigned records:{0u,3u,12u}) {
        Machine m=scan_machine(push,records), native=m;
        RoutineRegistry r; r.verifier=&verifier; add_scan(r,0x40,push,after);
        returned_with(m,&r); returned(native);
        check(m.equivalent(native),"Verified split scan equals the original");
    }
    // One record late: same exit, different state. One record early: refused.
    for (bool push:{false,true}) {
        Machine late=scan_machine(push,12);
        RoutineRegistry r; r.verifier=&verifier; add_scan(r,0x40,push,100,Stop::late);
        auto text=failure([&] { late.step(r); });
        check(text.find("cpu.hl:")!=std::string::npos && text.find("machine.cycle:")!=std::string::npos &&
              text.find("exit mismatch")==std::string::npos,"A scan stopping one record late diverges");
        Machine early=scan_machine(push,12);
        RoutineRegistry e; e.verifier=&verifier; add_scan(e,0x40,push,100,Stop::early);
        text=failure([&] { early.step(e); });
        check(text.find("routine execution failure")!=std::string::npos,"A scan stopping one record early is refused");
    }
    // A split scan crossing a frame boundary, run whole with routines on (and
    // verified) and off. The continuation falls back exactly while the
    // boundary is inside its bound; the first segment is far from it.
    for (std::uint64_t distance:{700u,701u,717u,735u}) {
        Machine m=scan_machine(true,40);
        m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles-distance);
        Machine native=m;
        RoutineRegistry r; r.verifier=&verifier; add_scan(r,0x40,true,100);
        std::uint64_t expected=0;
        for (unsigned n=0;n<10000 && !(m.cpu().pc.uint16_value==0x200 && m.cpu().sp.uint16_value==0xc102);++n) {
            if (m.cpu().resume==0 && m.cpu().pc.uint16_value==0x46)
                expected+=Machine::frame_cycles-m.cycles()%Machine::frame_cycles<scan_cost(m,Start::resume,100);
            m.step({},&r);
        }
        returned(native);
        const auto& start=r.find({0,0x40})->counters;
        const auto& resume=r.find({0,0x46})->counters;
        check(m.equivalent(native),"A split scan across a frame boundary equals the original");
        check(start.cpp_calls==1 && start.fallback_calls==0,"The scan's first segment ran in C++");
        check(expected>0 && resume.fallback_calls==expected && resume.fallback(RoutineFallback::frame)==expected &&
              resume.cpp_calls>=2,"The continuation falls back only while the boundary is inside its bound");
    }
}
void restart_exit(replay::CallVerifier& verifier) {
    auto registry=[&](bool held,unsigned scratch) {
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0},spec_of(0,{{F,1,static_cast<std::int8_t>(held ? -2 : 0)}},scratch),cost(held ? 22 : 15),
            [=](RoutineContext& c) {
                if(held)push_word(c,0,c.cpu().hl.uint16_value);else nop(c,0);
                c.finish_core(1);
            });
        return r;
    };
    for(bool held:{false,true})for(unsigned stack:{0xc100u,0x2000u,0x8100u}) {
        Machine m(program({static_cast<std::uint8_t>(held ? 0xe5 : 0),0xcf})); entry(m);
        m.cpu().sp.uint16_value=static_cast<std::uint16_t>(stack);m.cpu().hl.uint16_value=0x3579;
        if(stack==0x8100)m.memory().write(0xfffc,8); // cartridge RAM stack
        Machine native=m;auto r=registry(held,held ? 4 : 2);
        const auto start=m.cycles();m.step(r);
        while(native.cycles()<m.cycles())native.step();
        check(m.equivalent(native) && m.cycles()-start==(held ? 22u : 15u),"RST 08 preserves full native state and timing");
        check(m.cpu().pc.uint16_value==8 && m.cpu().sp.uint16_value==stack-(held ? 4 : 2),"RST 08 fixed target and held stack");
        check(r.find({0,0})->counters.cpp_calls==1,"RST 08 test reaches C++");
    }
    for(unsigned stack:{0u,1u,0xfffdu,0xc100u,0xc001u}) {
        Machine m(program({0,0xcf}));entry(m);m.cpu().sp.uint16_value=static_cast<std::uint16_t>(stack);
        if(stack==0xc100 || stack==0xc001)m.memory().set_memory_control(0x10);
        Machine native=m;auto r=registry(false,2);m.step(r);native.step();
        check(m.equivalent(native) && r.find({0,0})->counters.fallback(RoutineFallback::unmapped_stack)==1,
              "Wrapped, mapper or unmapped RST stack falls back before executing a body");
    }
    for(bool held:{false,true}) {
        Machine m(program({static_cast<std::uint8_t>(held ? 0xe5 : 0),0xcf}));entry(m);
        auto r=registry(held,held ? 2 : 0);
        r.verifier=nullptr;
        const auto text=failure([&]{m.step(r);});
        check(text.find("declared stack space")!=std::string::npos,"RST requires budget for its own push beyond held words");
    }
    for(std::uint8_t opcode:{0xc7,0xd7,0xdf,0xe7,0xef,0xf7,0xff,0xdd,0xfd}) {
        Machine m(program({0,opcode,0xcf}));entry(m);auto r=registry(false,2);
        // No verifier: a refused opcode can take the reference copy to a
        // different endpoint; this checks the replacement's own guard.
        r.verifier=nullptr;
        check(failure([&]{m.step(r);}).find("not allowed")!=std::string::npos,"Other restart vectors and prefixes remain refused");
    }
    {
        Machine m(program({0xcf}));entry(m);auto spec=forced_spec();spec.scratch_stack=2;
        RoutineRegistry r;r.add({0,0},spec,cost(11),[](RoutineContext& c){c.finish_core(0);});
        Machine native=m;m.step(r);native.step();
        check(m.equivalent(native) && r.find({0,0})->counters.cpp_calls==0 && r.find({0,0})->counters.forced_failed==1,
              "Forced RST is refused before its step and falls back unchanged");
    }
    for(unsigned remaining=1;remaining<=16;++remaining)for(bool nmi:{false,true}) {
        Machine m(program({0,0xcf}));entry(m);
        if(nmi)m.schedule_nmi(m.cycles()+remaining);
        else m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles-remaining);
        Machine native=m;auto r=registry(false,2);m.step(r);
        while(native.cycles()<m.cycles())native.step();
        check(m.equivalent(native),"RST respects frame and NMI admission boundaries");
        check(r.find({0,0})->counters.cpp_calls==(remaining>=15 ? 1u : 0u),"RST only enters within its full timing window");
    }
    {
        Machine m(program({0,0xcf}));entry(m);m.read_port(0xbf,m.cycles());m.cpu().iff1=m.cpu().iff2=1;
        m.advance(43980);Machine native=m;auto r=registry(false,2);m.step(r);
        while(native.cycles()<m.cycles())native.step();
        check(m.equivalent(native) && r.find({0,0})->counters.cpp_calls==0,"RST falls back near the frame IRQ");
    }
}
void segments() {
    replay::CallVerifier verifier("segments.psrec");
    restart_exit(verifier);
    // Hand-off by CALL into a shared helper, resume at the next segment, a
    // plain core instruction falling through into the helper, which returns.
    Machine chain(image({{0,{0x00,0xcd,0x07,0x00,0x00,0x3e,0x42,0x00,0xc9}}})); entry(chain);
    Machine native=chain;
    RoutineRegistry r; r.verifier=&verifier;
    r.add({0,0},segment({{F,1}}),cost(21),[](RoutineContext& c) { nop(c,0); c.finish_core(1); });
    r.add({0,4},segment({{F,5}}),cost(11),[](RoutineContext& c) { nop(c,4); c.finish_core(5); });
    r.add({0,7},SegmentSpec{{0,7},{{R,8}}},cost(14),[](RoutineContext& c) { nop(c,7); c.finish_return(8); });
    returned_with(chain,&r); returned(native);
    check(chain.equivalent(native) && r.find({0,7})->counters.cpp_calls==2,"Verified segment chain equals the original");

    // Each exit kind ending exactly at a frame boundary is verified cleanly.
    struct Shape { std::vector<std::uint8_t> code; SegmentExit exit; std::uint64_t bound; std::uint8_t flags; };
    const Shape shapes[]{{{0,0,0xc9},{R,2},18,0},{{0,0xcd,0,1},{F,1},21,0},{{0,0xc4,0,1},{F,1},21,0},
        {{0,0xc4,0,1},{F,1},14,0x40},{{0,0xc3,0,1},{F,1},14,0},{{0,0x3e,0x42},{F,1},11,0}};
    for (const auto& shape:shapes) {
        Machine m(image({{0,shape.code},{0x100,{0x00,0xc9}}})); entry(m); m.cpu().af.uint8_values.at_0=shape.flags;
        m.advance(Machine::frame_cycles-shape.bound); native=m;
        RoutineRegistry s; s.verifier=&verifier;
        const auto exit=shape.exit;
        s.add({0,0},segment({exit}),cost(shape.bound),[=](RoutineContext& c) {
            nop(c,0);
            if (exit.kind==R) { nop(c,1); c.finish_return(2); } else c.finish_core(1);
        });
        m.step(s);
        check(s.find({0,0})->counters.cpp_calls==1 && m.cycles()%Machine::frame_cycles==0,
              "Segment ending exactly at a frame boundary is admitted");
        catch_up(m,native,"Frame boundary at a declared exit is outside the window");
    }

    // The original reaches its first declared exit; the replacement skips it.
    Machine skip(program({0x3e,0x01,0x3e,0x01})); entry(skip);
    RoutineRegistry two; two.verifier=&verifier;
    two.add({0,0},segment({{F,2},{F,4}}),cost(14),[](RoutineContext& c) {
        auto& z=c.cpu(); c.fetch_opcode(0); z.af.uint8_values.at_1=c.read(1); c.advance(7); c.finish_core(4);
    });
    auto text=failure([&] { skip.step(two); });
    check(text.find("exit mismatch: original=core_final 0002, replacement=core_final 0004\n")!=std::string::npos,
          "Different exits are reported");
    // A core_final segment with a wrong result is reported by field.
    Machine flag(program({0,0})); entry(flag);
    RoutineRegistry wrong; wrong.verifier=&verifier;
    wrong.add({0,0},segment({{F,1}}),cost(8),[](RoutineContext& c) {
        nop(c,0); c.cpu().af.uint8_values.at_0^=1; c.finish_core(1);
    });
    text=failure([&] { flag.step(wrong); });
    check(text.find("cpu.f:")!=std::string::npos && text.find("exit mismatch")==std::string::npos,
          "Core-final divergence names the field");
    held_exits(verifier); conditional_exits(verifier); loop_exits(verifier);
}
void forced() {
    replay::CallVerifier verifier("forced.psrec");
    const auto single=program({0xc6,0x05});
    auto body=[](RoutineContext& c) {
        auto& z=c.cpu(); z.de.uint8_values.at_0^=0x3c; c.finish_core(0); z.de.uint8_values.at_0^=0x3c;
    };
    // IRQ, NMI and frame boundary at every cycle of the forced instruction.
    for (unsigned event=0;event<3;++event) for (unsigned d=event==2 ? 1 : 0;d<=7;++d) {
        Machine m(single); entry(m);
        if (event==0) { m.read_port(0xbf,0); m.cpu().iff1=m.cpu().iff2=1; m.advance(43986-d); }
        if (event==1) { m.set_pause(true); m.read_port(0xbf,0); m.advance(Machine::pause_sample_offset-d); }
        if (event==2) m.advance(Machine::frame_cycles-d);
        Machine native=m;
        RoutineRegistry r; r.verifier=&verifier; r.add({0,0},forced_spec(),cost(7),body);
        m.step(r);
        const auto& n=r.find({0,0})->counters;
        check(n.cpp_calls==1 && n.forced_calls==1 && n.forced_failed==0,"Forced instruction admitted and verified");
        catch_up(m,native,"Forced instruction equals the original, also several steps later");
    }
    // A forced body that leaves a net register change diverges from the native step.
    {
        Machine m(single); entry(m);
        RoutineRegistry r; r.verifier=&verifier;
        r.add({0,0},forced_spec(),cost(7),[](RoutineContext& c) {
            c.cpu().de.uint8_values.at_0^=0x3c; c.finish_core(0);
        });
        const auto text=failure([&] { m.step(r); });
        check(text.find("routine 00:0000, call 1\n")!=std::string::npos && text.find("cpu.de:")!=std::string::npos &&
              text.find("exit mismatch")==std::string::npos,"A forced body leaving a register change is a field divergence");
    }
    // A rejected forced body falls back without a verifier report.
    Machine m(single); entry(m); Machine native=m;
    RoutineRegistry r; r.verifier=&verifier;
    r.add({0,0},forced_spec(),cost(7),[](RoutineContext& c) { c.advance(1); c.finish_core(0); });
    m.step(r); native.step();
    check(m.equivalent(native) && r.find({0,0})->counters.forced_failed==1,"Forced failure is native under verification");
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
    b=before; b.cpu().r^=1; b.cpu().r7^=1;
    b.memory().set_memory_control(0x37); b.write_port(0x3f,0x6d,b.cycles());
    diff=before.differences(b);
    unsigned raw_fields=0;
    for (const auto& d:diff) if (d.field=="cpu.r" || d.field=="cpu.r7" ||
        d.field=="machine.io_control" || d.field=="memory.memory_control") {
        ++raw_fields;
        check(d.can_carry_rom && d.original.empty() && d.replacement.empty(),"Raw byte copies stay redacted");
    }
    check(raw_fields==4,"All four raw byte copies are reported");
    b=before; b.cpu().memptr.uint16_value^=1;
    check(!before.equivalent(b) && !before.differences(b).empty(),"Equivalence is exactly an empty difference list");
    b=before; b.cpu().af.uint8_values.at_1=b.read_port(0xbf,b.cycles());
    diff=before.differences(b);
    check(std::any_of(diff.begin(),diff.end(),[](const auto& d){return d.field=="cpu.a" && d.can_carry_rom && d.original.empty();}),"Port-read results retained in a register are redacted");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==2,"Expected test case"); const std::string_view test=argv[1];
        if (test=="dispatch") dispatch(); else if(test=="clean") clean();
        else if(test=="windows") windows(); else if(test=="fields") fields();
        else if(test=="segments") segments(); else if(test=="forced") forced(); else if(test=="exits") exits(); else fault(test);
        std::cout<<"Lockstep test passed: "<<test<<'\n'; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
