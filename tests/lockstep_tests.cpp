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
void returned(Machine& m, RoutineRegistry* r=nullptr) {
    for (unsigned n=0;n<1000;++n) {
        m.step({},r);
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
    Machine frame(program({0,0,0xc9})); entry(frame); frame.advance(Machine::frame_cycles-8);
    auto before=frame;
    auto text=failure([&] { verifier.run(frame,{0,0},18,7,[]{throw std::runtime_error("Body must not run");}); });
    check(text.find("window-check failure: frame boundary")!=std::string::npos && frame.equivalent(before),"Shadow detects frame crossing without touching real state");
    Machine irq(program({0,0,0xc9})); entry(irq); irq.read_port(0xbf,0); irq.cpu().iff1=irq.cpu().iff2=1;
    irq.advance(43980); before=irq;
    text=failure([&] { verifier.run(irq,{0,0},40,8,[]{}); });
    check(text.find("window-check failure: interrupt accepted")!=std::string::npos && irq.equivalent(before),"Shadow independently detects native interrupt acceptance");
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
        else if(test=="windows") windows(); else if(test=="fields") fields(); else fault(test);
        std::cout<<"Lockstep test passed: "<<test<<'\n'; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
