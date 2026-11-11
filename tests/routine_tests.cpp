// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/machine.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
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
template<class F> void rejects(F operation, std::string_view message) {
    try { operation(); } catch (const std::exception& e) {
        check(std::string_view(e.what()).find(message)!=std::string_view::npos,"wrong guard diagnostic"); return;
    }
    throw std::runtime_error("missing registered routine guard");
}
std::shared_ptr<const Memory::Rom> program(std::initializer_list<std::uint8_t> code) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    std::copy(code.begin(),code.end(),rom->begin()); return rom;
}
void stack(Machine& m) {
    m.cpu().sp.uint16_value=0xc100;
    m.memory().write(0xc100,0); m.memory().write(0xc101,2);
}
void original_call(Machine& m) {
    for (unsigned i=0;i<1000;++i) {
        m.step();
        if (m.cpu().sp.uint16_value==0xc102 && m.cpu().pc.uint16_value==0x200) return;
    }
    throw std::runtime_error("synthetic original did not return");
}
RegisteredRoutine::Bound cost(std::uint64_t cycles) { return [=](const Machine&) {return cycles;}; }
void simple_body(RoutineContext& c) {
    c.fetch_opcode(0); c.advance(4); c.fetch_opcode(1); c.advance(4); c.finish_return(2);
}
void results() {
    Machine native(program({0xdd,0x21,0x23,0xc1,0x3e,0x7f,0x3c,0x32,0x20,0xc0,
                            0xd3,0x7f,0xdb,0x3e,0x32,0x21,0xc0,0xcb,0x00,0xc9}));
    stack(native); native.cpu().r=0xfe;
    Machine cpp=native;
    const Machine start=native;
    RoutineRegistry registry;
    registry.add({0,0},[](const Machine& m) { return m.cpu().r&0x80 ? 91 : 92; },[](RoutineContext& c) {
        auto& z=c.cpu();
        c.fetch_opcode(0); c.advance(4); c.fetch_opcode(1);
        auto lo=c.read(2); auto hi=c.read(3); z.ix_iy[0].uint16_value=lo|(hi<<8); c.advance(10);
        c.fetch_opcode(4); z.af.uint8_values.at_1=c.read(5); c.advance(7);
        c.fetch_opcode(6); ++z.af.uint8_values.at_1; z.af.uint8_values.at_0=0x94; c.advance(4);
        c.fetch_opcode(7); lo=c.read(8); hi=c.read(9); c.write(lo|(hi<<8),z.af.uint8_values.at_1); c.advance(13);
        c.fetch_opcode(10); const auto output=c.read(11);
        c.write_port(output,z.af.uint8_values.at_1,48); c.advance(1);
        c.fetch_opcode(12); const auto input=c.read(13);
        z.af.uint8_values.at_1=c.read_port(input,59); c.advance(1);
        c.fetch_opcode(14); lo=c.read(15); hi=c.read(16); c.write(lo|(hi<<8),z.af.uint8_values.at_1); c.advance(13);
        c.fetch_opcode(17); c.fetch_opcode(18); z.bc.uint8_values.at_1=0; z.af.uint8_values.at_0=0x44; c.advance(8);
        c.finish_return(19);
    });
    const auto fetches=cpp.opcode_fetches();
    unsigned hooks=0;
    auto hook=[&](std::uint16_t,Memory::Mapping)->std::optional<std::uint16_t> {++hooks;return {};};
    check(cpp.step(registry,hook)==91 && hooks==0,"complete call replaces exactly one step and bypasses native hook");
    original_call(native);
    check(cpp.equivalent(native),"register, flag, RAM, port, hidden state and exact cycles");
    check(cpp.cpu().pc.uint16_value==0x200 && cpp.cpu().sp.uint16_value==0xc102,"no extra instruction after return");
    check(cpp.cpu().af.uint16_value==0x3e44 && cpp.memory().read(0xc020)==0x80 &&
          cpp.memory().read(0xc021)==0x3e,"synthetic arithmetic and open bus input");
    check(cpp.last_access()==native.last_access() && cpp.psg().writes()==std::vector<PsgWrite>{{48,0x80}},"T3 port order and timestamp");
    check(cpp.opcode_fetches()-fetches==11 && cpp.cpu().r==0x89,"fetches include prefixes and return, preserving R bit 7");
    check(registry.find({0,0})->counters.cpp_calls==1,"C++ call count");
    // The registry is host state. Its counters from the first call do not
    // change a second identical call, and switching the same registry off
    // sends that entry down the original path to the same machine state.
    Machine second=start;
    check(second.step(registry)==91 && second.equivalent(cpp) && registry.find({0,0})->counters.cpp_calls==2,
          "a used registry repeats the same call exactly");
    registry.enabled=false;
    Machine third=start;
    for (unsigned i=0;i<1000 && !(third.cpu().sp.uint16_value==0xc102 && third.cpu().pc.uint16_value==0x200);++i)
        third.step(registry);
    const auto& counters=registry.find({0,0})->counters;
    check(third.equivalent(cpp) && counters.cpp_calls==2 && counters.fallback_calls==1 &&
          counters.fallback(RoutineFallback::disabled)==1,
          "switching the same registry off falls back to the same result, counted once");

    // Bulk fetch accounting differs by 128 even when R is equal.
    auto bulk_rom=std::make_shared<Memory::Rom>(512*1024,0); (*bulk_rom)[128]=0xc9;
    Machine bulk(bulk_rom); stack(bulk); bulk.cpu().r=0xff;
    Machine bulk_native=bulk;
    const auto initial=bulk.cpu().r;
    RoutineRegistry many;
    many.add({0,0},cost(522),[=](RoutineContext& c) {
        c.account_fetches(128); check(c.cpu().r==initial,"bulk accounting keeps R bit 7 and low-bit wrap");
        c.advance(512); c.finish_return(128);
    });
    bulk.step(many); check(bulk.opcode_fetches()==129 && bulk.cpu().r==0x80,"bulk count does not collapse to R");
    original_call(bulk_native); check(bulk.equivalent(bulk_native),"bulk fetch accounting matches the same original program");
}
void vdp_results() {
    Machine native(program({0x3e,0x42,0xd3,0xbf,0x3e,0x82,0xd3,0xbf,
                            0x3e,0x19,0xd3,0xbe,0xdb,0xbf,0xc9}));
    stack(native); Machine cpp=native; RoutineRegistry r;
    r.add({0,0},cost(85),[](RoutineContext& c) {
        auto& z=c.cpu();
        for (unsigned i=0;i<3;++i) {
            const auto pc=static_cast<std::uint16_t>(4*i);
            c.fetch_opcode(pc); z.af.uint8_values.at_1=c.read(pc+1); c.advance(7);
            c.fetch_opcode(pc+2); const auto port=c.read(pc+3);
            c.write_port(port,z.af.uint8_values.at_1,18*i+17); c.advance(1);
        }
        c.fetch_opcode(12); const auto port=c.read(13);
        z.af.uint8_values.at_1=c.read_port(port,64); c.advance(1); c.finish_return(14);
    });
    cpp.step(r); original_call(native);
    check(cpp.equivalent(native) && cpp.last_access()==native.last_access(),"allowed VDP register/data writes and timed status read match original");
    check(cpp.vdp().registers()[2]==0x42,"unrestricted VDP register remains writable");
}
void window_case(bool nmi, unsigned distance, std::uint64_t bound, bool fallback) {
    Machine native(program({0,0,0xc9})); stack(native);
    if (nmi) native.set_pause(true);
    native.read_port(0xbf,0); native.cpu().iff1=native.cpu().iff2=nmi ? 0 : 1;
    const std::uint64_t event=nmi ? Machine::pause_sample_offset : 43986;
    native.advance(event-distance);
    Machine cpp=native;
    RoutineRegistry registry; registry.add({0,0},cost(bound),simple_body);
    cpp.step(registry);
    if (fallback) {
        check(cpp.cpu().pc.uint16_value==1,"fallback executes original first instruction");
        while (cpp.cpu().pc.uint16_value!=0x200) cpp.step();
    }
    original_call(native);
    check(cpp.equivalent(native),"window case equals the same original program");
    const auto& calls=registry.find({0,0})->counters;
    check(calls.cpp_calls==!fallback && calls.fallback_calls==fallback,"window outcome counting");
    check(calls.fallback(nmi ? RoutineFallback::nmi : RoutineFallback::irq)==fallback,"window fallback reason");
    // At event==E the edge missed the return's E-1 sample. One more
    // instruction samples it; at event==E-1 the next boundary accepts it.
    for (unsigned i=0;i<3;++i) {
        check(cpp.step()==native.step() && cpp.equivalent(native),"post-call E-1 acceptance agrees");
    }
}
void windows() {
    // Actual E = entry+18. Inclusive last sample E-1 must fall back;
    // first cycle outside [entry,E), E, is safe and sampled one instruction later.
    window_case(false,17,18,true);
    window_case(false,18,18,false);
    // IRQ at actual E+2, before bound E=entry+24: still falls back.
    window_case(false,20,24,true);
    window_case(true,17,18,true);
    window_case(true,18,18,false);
    // IFF1 clear means a maskable line is harmless throughout the window.
    Machine off(program({0,0,0xc9})); stack(off); Machine original=off;
    RoutineRegistry allowed; allowed.add({0,0},cost(18),simple_body);
    off.step(allowed); original_call(original); check(off.equivalent(original),"disabled maskable interrupts allow replacement");

    // NMI first visible at entry: missed preceding E-1, so native executes
    // one instruction before accepting. The registry must still reject it.
    Machine entry(program({0,0,0xc9})); stack(entry); entry.schedule_nmi(4); entry.advance(4);
    Machine baseline=entry;
    RoutineRegistry r; r.add({0,0},cost(18),simple_body);
    check(entry.step(r)==baseline.step() && entry.equivalent(baseline),"entry-visible NMI falls back without accepting early");
    check(r.find({0,0})->counters.fallback(RoutineFallback::nmi)==1,"entry-visible NMI counted");
    check(entry.step()==11 && baseline.step()==11 && entry.equivalent(baseline),"entry-visible edge accepted after next E-1");

    // A request already sampled at E-1 suppresses both dispatch and hook.
    Machine pending(program({0,0,0xc9})); stack(pending); pending.schedule_nmi(0); pending.advance(1);
    baseline=pending; unsigned hooks=0;
    auto hook=[&](std::uint16_t,Memory::Mapping)->std::optional<std::uint16_t> {++hooks;return {};};
    pending.step(r,hook); baseline.step();
    check(hooks==0 && pending.equivalent(baseline),"accepted interrupt suppresses registry and existing hook");

    for (unsigned distance:{17,18}) {
        Machine frame(program({0,0,0xc9})); stack(frame);
        frame.advance(Machine::frame_cycles-distance); baseline=frame;
        RoutineRegistry f; f.add({0,0},cost(18),simple_body);
        frame.step(f); if (distance==17) while(frame.cpu().pc.uint16_value!=0x200) frame.step();
        original_call(baseline);
        check(frame.equivalent(baseline),"frame crossing fallback and boundary exactly at E");
        check(f.find({0,0})->counters.fallback(RoutineFallback::frame)==(distance==17),"frame reason counted");
    }
    Machine multiple(program({0,0,0xc9})); stack(multiple); multiple.set_pause(true);
    multiple.read_port(0xbf,0); multiple.cpu().iff1=1;
    multiple.advance(1);
    RoutineRegistry reasons; reasons.add({0,0},cost(Machine::frame_cycles+1),simple_body);
    baseline=multiple; multiple.step(reasons); baseline.step();
    const auto& c=reasons.find({0,0})->counters;
    check(multiple.equivalent(baseline) && c.fallback_calls==1 && c.fallback(RoutineFallback::irq)==1 &&
          c.fallback(RoutineFallback::nmi)==1 && c.fallback(RoutineFallback::frame)==1,"one fallback can count all applicable reasons");
}
void registry_and_guards() {
    RoutineRegistry empty;
    Machine a(program({0})),b=a;
    a.run_frame({},empty); b.run_frame({}); check(a.equivalent(b),"empty frame registry is byte-equivalent");
    RoutineRegistry disabled; disabled.enabled=false; disabled.add({0,0},cost(18),simple_body);
    Machine d(program({0,0,0xc9})); stack(d); Machine native=d;
    d.step(disabled); native.step(); check(d.equivalent(native),"disabled registry executes original");
    check(disabled.find({0,0})->counters.fallback(RoutineFallback::disabled)==1,"disabled count");
    rejects([&]{disabled.add({0,0},cost(18),simple_body);},"Duplicate");
    rejects([&]{disabled.add({0,0xc000},cost(18),simple_body);},"Invalid");

    auto guard=[](RegisteredRoutine::Body body, std::string_view diagnostic, std::uint64_t bound=18) {
        Machine m(program({0,0,0xc9})); stack(m);
        RoutineRegistry r; r.add({0,0},cost(bound),std::move(body));
        rejects([&]{m.step(r);},diagnostic);
    };
    for (unsigned reg:{0,1,10}) for (unsigned port:{0x81,0xbf}) {
        guard([=](RoutineContext& c){c.write_port(port,0x55,0);c.write_port(port,0x80|reg,0);},"interrupt control");
        Machine half(program({0,0,0xc9})); stack(half); half.write_port(port,0x55,0);
        RoutineRegistry r; r.add({0,0},cost(18),[=](RoutineContext& c){c.write_port(port,0x80|reg,0);});
        const auto before=half.vdp().registers();
        rejects([&]{half.step(r);},"interrupt control"); check(half.vdp().registers()==before,"entry half-latch guard rejects before changing register");
    }
    for (unsigned address=0xfffc;address<=0xffff;++address)
        guard([=](RoutineContext& c){c.write(address,2);},"mapper");
    for (unsigned port:{0x00,0x3e}) guard([=](RoutineContext& c){c.write_port(port,0,0);},"mapping");
    guard([](RoutineContext& c){c.advance(19);},"exceeded");
    guard(simple_body,"exceeded",17); // Includes the core's returning instruction.
    guard([](RoutineContext&){},"omitted");
    guard([](RoutineContext& c){c.finish_return(0);},"returning instruction");
    guard([](RoutineContext& c){c.advance(2);c.read_port(0x3e,1);},"past");

    // Same address in different banks, fixed first KiB, cartridge RAM,
    // work RAM and unmapped cartridge must never alias the ROM registry.
    for (unsigned mode=0;mode<5;++mode) {
        Machine m(program({0})); stack(m); m.cpu().pc.uint16_value=0x8000;
        RoutineRegistry r; unsigned calls=0;
        r.add({2,static_cast<std::uint16_t>(mode==4 ? 0 : 0x8000)},cost(10),[&](RoutineContext&){++calls;});
        if (mode==0) m.memory().write(0xffff,3);
        if (mode==1) m.memory().write(0xfffc,8);
        if (mode==2) m.cpu().pc.uint16_value=0xc000;
        if (mode==3) m.memory().set_memory_control(0x40);
        if (mode==4) { m.cpu().pc.uint16_value=0; m.memory().write(0xfffd,2); }
        native=m; m.step(r); native.step(); check(calls==0 && m.equivalent(native),"ROM mapping and bank gate");
    }
    // Positive bank dispatch, and conditional / ignored-prefix returns.
    for (const auto code:{0xc9,0xc0,0xc8}) {
        auto rom=std::make_shared<Memory::Rom>(512*1024,0);
        (*rom)[3*0x4000]=0xdd; (*rom)[3*0x4000+1]=code;
        Machine m(rom); stack(m); m.memory().write(0xffff,3); m.cpu().pc.uint16_value=0x8000;
        m.cpu().af.uint8_values.at_0=code==0xc8 ? 0x40 : 0;
        native=m;
        RoutineRegistry r; r.add({3,0x8000},cost(15),[](RoutineContext& c){c.finish_return(0x8000);});
        m.step(r); original_call(native); check(m.equivalent(native),"mapped original returning instruction");
    }
    constexpr unsigned bits[]{6,0,2,7};
    for (unsigned condition=0;condition<8;++condition) {
        const auto code=static_cast<std::uint8_t>(0xc0+condition*8);
        Machine m(program({code})); stack(m); m.cpu().af.uint8_values.at_0=(condition&1)<<bits[condition/2];
        native=m; RoutineRegistry r; r.add({0,0},cost(11),[](RoutineContext& c){c.finish_return(0);});
        m.step(r); original_call(native); check(m.equivalent(native),"each conditional returning path uses the core");
        Machine untaken(program({code})); stack(untaken);
        untaken.cpu().af.uint8_values.at_0=(!(condition&1))<<bits[condition/2];
        rejects([&]{untaken.step(r);},"returning instruction");
    }
    Machine unmapped_stack(program({0xc9})); stack(unmapped_stack);
    unmapped_stack.memory().set_memory_control(0x10); native=unmapped_stack;
    RoutineRegistry unmapped_return;
    unmapped_return.add({0,0},cost(10),[](RoutineContext& c){c.finish_return(0);});
    unmapped_stack.step(unmapped_return); native.step();
    check(unmapped_stack.equivalent(native),"return reads an unmapped stack through the real open bus");
}
void scratch_overwrite() {
    // Perturb only excluded scratch, then execute every prefix family and
    // the ordinary and extended returns. Live prefix bytes stay compared.
    const std::vector<std::vector<std::uint8_t>> paths{
        {0xcb,0x00}, {0xed,0x44}, {0xdd,0x21,0x12,0xc0}, {0xfd,0x21,0x34,0xc0},
        {0xdd,0xcb,0,0x46}, {0xfd,0xcb,0,0x46}, {0xdd,0xfd,0x21,0x34,0xc0},
        {0xed,0x45}, {0xed,0x4d}, {0xc9}, {0x00}, {0xdb,0x3e}
    };
    for (const auto& bytes:paths) {
        auto rom=std::make_shared<Memory::Rom>(512*1024,0); std::copy(bytes.begin(),bytes.end(),rom->begin());
        Machine a(rom); stack(a); a.cpu().ix_iy[0].uint16_value=a.cpu().ix_iy[1].uint16_value=0xc000;
        Machine b=a;
        a.cpu().data.uint8_array[0]=b.cpu().data.uint8_array[0]=0xc9;
        for (unsigned j=1;j<4;++j) {a.cpu().data.uint8_array[j]=0x55;b.cpu().data.uint8_array[j]=0xaa;}
        a.cpu().xy.uint16_value=0x1234; b.cpu().xy.uint16_value=0xabcd;
        check(a.equivalent(b),"dead scratch excluded after an unprefixed instruction");
        do {check(a.step()==b.step(),"overwrite path exact cycles");} while(a.cpu().resume==Z80_RESUME_XY);
        if (bytes[0]==0xcb || bytes[0]==0xed || (bytes[0]&0xdf)==0xdd)
            check(a.cpu().data.uint8_array[1]==b.cpu().data.uint8_array[1],"prefix operation byte overwritten before dispatch");
        if ((bytes[0]&0xdf)==0xdd) check(a.cpu().xy.uint16_value==b.cpu().xy.uint16_value,"index temporary overwritten before use");
        // data[2] is overwritten by the extended returning instructions;
        // data[3] by the indexed bit instruction. Other unused bytes persist.
        if (bytes[0]==0xed && (bytes[1]&0xc7)==0x45)
            check(a.cpu().data.uint8_array[2]==b.cpu().data.uint8_array[2],"interrupt-delay marker overwritten");
        if (bytes.size()==4 && bytes[1]==0xcb)
            check(a.cpu().data.uint8_array[3]==b.cpu().data.uint8_array[3],"indexed operation byte overwritten");
        // Complete a following unprefixed instruction: the deliberately
        // conservative comparator retains every byte while data[0] is a prefix.
        check(a.step()==b.step() && a.equivalent(b),"scratch replaced before use, unprefixed comparison restored");
    }
    for (unsigned prefix:{0xcb,0xed,0xdd,0xfd}) {
        Machine a(program({0})); Machine b=a; a.cpu().data.uint8_array[0]=b.cpu().data.uint8_array[0]=prefix;
        b.cpu().data.uint8_array[1]^=1; check(!a.equivalent(b),"live prefix bytes remain compared");
    }
    Machine a(program({0})); Machine b=a;
    b.cpu().memptr.uint16_value^=1; check(!a.equivalent(b),"MEMPTR remains compared");
    b=a; b.cpu().q^=1; check(!a.equivalent(b),"Q remains compared");
    b=a; b.cpu().data.uint8_array[0]^=1; check(!a.equivalent(b),"last opcode remains compared");
}
constexpr auto R=RoutineExit::returned;
constexpr auto F=RoutineExit::core_final;
SegmentSpec segment(std::uint16_t unit, std::initializer_list<SegmentExit> exits) { return {{0,unit},exits}; }
SegmentSpec forced_spec(std::uint16_t entry) { return {{0,entry},{{F,entry}},false,0,true,true}; }
void nop(RoutineContext& c, std::uint16_t pc) { c.fetch_opcode(pc); c.advance(4); }
std::shared_ptr<const Memory::Rom> image(const std::vector<std::pair<std::uint16_t,std::vector<std::uint8_t>>>& parts) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    for (const auto& [at,bytes]:parts) std::copy(bytes.begin(),bytes.end(),rom->begin()+at);
    return rom;
}
void until_return(Machine& m, RoutineRegistry* r=nullptr) {
    for (unsigned i=0;i<1000;++i) {
        if (m.cpu().pc.uint16_value==0x200 && m.cpu().sp.uint16_value==0xc102) return;
        m.step({},r);
    }
    throw std::runtime_error("synthetic segments did not return");
}
// Native catch-up to the same boundary, then several native steps on both.
void same_as_native(Machine& cpp, Machine& native, const char* message) {
    while (native.cycles()<cpp.cycles()) native.step();
    check(cpp.equivalent(native),message);
    for (unsigned i=0;i<6;++i) check(cpp.step()==native.step() && cpp.equivalent(native),message);
}
void segments() {
    // A chain: the entry hands off by CALL to a shared helper, which returns
    // to the next segment; that one ends in a plain two-byte instruction on
    // the core and falls through into the helper, which returns.
    Machine cpp(image({{0,{0x00,0xcd,0x07,0x00,0x00,0x3e,0x42,0x00,0xc9}}})); stack(cpp);
    Machine native=cpp;
    RoutineRegistry r;
    r.add({0,0},segment(0,{{F,1}}),cost(21),[](RoutineContext& c) {
        nop(c,0); c.finish_core(1); check(c.last_exit()==SegmentExit{F,1},"call exit recorded");
    });
    r.add({0,4},segment(0,{{F,5}}),cost(11),[](RoutineContext& c) {
        nop(c,4); c.finish_core(5); check(c.last_exit()==SegmentExit{F,5},"plain exit recorded");
    });
    r.add({0,7},segment(7,{{R,8}}),cost(14),[](RoutineContext& c) {
        nop(c,7); c.finish_return(8); check(c.last_exit()==SegmentExit{R,8},"return exit recorded");
    });
    cpp.step(r);
    check(cpp.cpu().pc.uint16_value==7 && cpp.cpu().sp.uint16_value==0xc0fe,"the core's CALL leaves the machine at the callee");
    until_return(cpp,&r); until_return(native);
    check(cpp.equivalent(native),"segment chain equals the original program");
    check(r.find({0,0})->counters.cpp_calls==1 && r.find({0,4})->counters.cpp_calls==1 &&
          r.find({0,7})->counters.cpp_calls==2 && r.find({0,7})->counters.fallback_calls==0,
          "shared entry runs in C++ when reached by call and by fall-through");

    for (bool taken:{true,false}) {
        Machine m(image({{0,{0x00,0xc4,0x00,0x01,0x00,0xc9}},{0x100,{0x00,0xc9}}})); stack(m);
        m.cpu().af.uint8_values.at_0=taken ? 0 : 0x40; native=m;
        RoutineRegistry cc;
        cc.add({0,0},segment(0,{{F,1}}),[](const Machine& x) { return x.cpu().af.uint8_values.at_0&0x40 ? 14 : 21; },
            [](RoutineContext& c) { nop(c,0); c.finish_core(1); });
        cc.add({0,4},segment(0,{{R,5}}),cost(14),[](RoutineContext& c) { nop(c,4); c.finish_return(5); });
        m.step(cc);
        check(m.cpu().pc.uint16_value==(taken ? 0x100 : 4),"conditional call taken and untaken on the core");
        until_return(m,&cc); until_return(native);
        check(m.equivalent(native) && cc.find({0,4})->counters.cpp_calls==1,"conditional call resumes at the next segment");
    }
    Machine jump(image({{0,{0x00,0xc3,0x00,0x01}},{0x100,{0x00,0xc9}}})); stack(jump); native=jump;
    RoutineRegistry tail;
    tail.add({0,0},segment(0,{{F,1}}),cost(14),[](RoutineContext& c) { nop(c,0); c.finish_core(1); });
    jump.step(tail);
    check(jump.cpu().pc.uint16_value==0x100 && jump.cpu().sp.uint16_value==0xc100,"tail jump keeps the caller's stack");
    until_return(jump,&tail); until_return(native); check(jump.equivalent(native),"tail jump equals the original program");

    // A declared slot-2 mapper write goes through the bus like the original's.
    Machine mapper(program({0x3e,0x05,0x32,0xff,0xff,0x00})); stack(mapper); native=mapper;
    RoutineRegistry slot;
    slot.add({0,0},SegmentSpec{{0,0},{{F,5}},true},cost(24),[](RoutineContext& c) {
        auto& z=c.cpu();
        c.fetch_opcode(0); z.af.uint8_values.at_1=c.read(1); c.advance(7);
        c.fetch_opcode(2); const auto lo=c.read(3), hi=c.read(4);
        c.write(static_cast<std::uint16_t>(lo|(hi<<8)),z.af.uint8_values.at_1);
        z.memptr.uint16_value=static_cast<std::uint16_t>((z.af.uint8_values.at_1<<8)|((lo+1)&0xff)); c.advance(13);
        c.finish_core(5);
    });
    mapper.step(slot); for (unsigned i=0;i<3;++i) native.step();
    check(mapper.equivalent(native) && mapper.memory().mapping(0x8000).bank==5,"declared slot-2 mapper write");

    // Scratch stack traffic through the bus: a ROM stack reads back ROM bytes,
    // a cartridge-RAM stack its own writes.
    for (std::uint16_t top:{0x0100,0x8100}) {
        Machine s(program({0xe5,0xe1,0x3c})); s.cpu().sp.uint16_value=top; s.cpu().hl.uint16_value=0x1234;
        if (top==0x8100) s.memory().write(0xfffc,8);
        native=s;
        RoutineRegistry scratch;
        scratch.add({0,0},SegmentSpec{{0,0},{{F,2}},false,2},cost(25),[](RoutineContext& c) {
            auto& z=c.cpu(); const auto sp=z.sp.uint16_value;
            c.fetch_opcode(0); c.write(static_cast<std::uint16_t>(sp-1),z.hl.uint8_values.at_1);
            c.write(static_cast<std::uint16_t>(sp-2),z.hl.uint8_values.at_0); c.advance(11);
            c.fetch_opcode(1); z.hl.uint8_values.at_0=c.read(static_cast<std::uint16_t>(sp-2));
            z.hl.uint8_values.at_1=c.read(static_cast<std::uint16_t>(sp-1)); c.advance(10);
            c.finish_core(2);
        });
        s.step(scratch); for (unsigned i=0;i<3;++i) native.step();
        check(s.equivalent(native) && s.cpu().hl.uint16_value==(top==0x0100 ? 0 : 0x1234) &&
              scratch.find({0,0})->counters.cpp_calls==1,"scratch stack in ROM and cartridge RAM through the bus");
    }
    // Scratch bytes reaching the mapper registers fall back; a normal stack does not.
    for (std::uint16_t top:{0xfffe,0x0000,0x0001,0xc102}) {
        Machine s(program({0xe5,0xe1,0x3c})); s.cpu().sp.uint16_value=top; native=s;
        RoutineRegistry scratch;
        scratch.add({0,0},SegmentSpec{{0,0},{{F,2}},false,2},cost(25),[](RoutineContext&) {
            throw std::runtime_error("unexpected scratch body");
        });
        const bool fallback=top!=0xc102;
        if (fallback) { s.step(scratch); native.step(); check(s.equivalent(native),"scratch fallback is native"); }
        else rejects([&]{s.step(scratch);},"unexpected scratch body");
        check(scratch.find({0,0})->counters.fallback(RoutineFallback::unmapped_stack)==fallback,"scratch stack range checked");
    }
}
void segment_faults() {
    auto guard=[](std::shared_ptr<const Memory::Rom> rom, SegmentSpec spec, std::uint64_t bound,
                  RegisteredRoutine::Body body, std::string_view diagnostic) {
        Machine m(std::move(rom)); stack(m);
        RoutineRegistry r; r.add({0,0},std::move(spec),cost(bound),std::move(body));
        rejects([&]{m.step(r);},diagnostic);
    };
    const auto nops=program({0,0,0xc9});
    guard(nops,segment(0,{{F,1}}),18,[](RoutineContext& c){nop(c,0);nop(c,1);c.finish_return(2);},"not declared");
    guard(nops,segment(0,{{R,2}}),8,[](RoutineContext& c){nop(c,0);c.finish_core(1);},"not declared");
    Machine legacy(nops); stack(legacy); RoutineRegistry old;
    old.add({0,0},cost(8),[](RoutineContext& c){nop(c,0);c.finish_core(1);});
    rejects([&]{legacy.step(old);},"not declared");
    for (std::uint8_t op:{0xc9,0xc0,0xc2,0xe9,0x18,0x20,0x10,0xc7,0xff,0xdb,0xd3,0x32,0x22,0x02,0x12,0x34,0x35,
                          0x36,0x77,0x70,0xe5,0xe3,0x76,0xf3,0xfb,0xcb,0xdd,0xed,0xfd,
                          // SP-changing plain instructions: POP rr, LD SP,nn, INC SP, DEC SP, LD SP,HL
                          0xc1,0xd1,0xe1,0xf1,0x31,0x33,0x3b,0xf9})
        guard(image({{0,{op,0,0,0}}}),SegmentSpec{{0,0},{{F,0}},false,0,true},30,[](RoutineContext& c){c.finish_core(0);},"not allowed");
    guard(nops,segment(0,{{F,1}}),8,[](RoutineContext& c){nop(c,0);--c.cpu().sp.uint16_value;c.finish_core(1);},"entry stack");
    guard(nops,segment(0,{{R,2}}),18,[](RoutineContext& c){nop(c,0);nop(c,1);++c.cpu().sp.uint16_value;c.finish_return(2);},"entry stack");
    guard(nops,segment(0,{{F,1}}),8,[](RoutineContext& c){nop(c,0);},"omitted");
    guard(program({0,0xcd,0,1}),segment(0,{{F,1}}),20,[](RoutineContext& c){nop(c,0);c.finish_core(1);},"exceeded");
    guard(nops,segment(0,{{F,1}}),8,[](RoutineContext& c){c.write(0xffff,3);},"mapper");
    for (std::uint16_t address=0xfffc;address<0xffff;++address)
        guard(nops,SegmentSpec{{0,0},{{F,1}},true},8,[=](RoutineContext& c){c.write(address,3);},"mapper");
    // The power-on frame interrupt line is asserted; enabling it in C++ would
    // make the core respond to it instead of executing the final instruction.
    guard(nops,segment(0,{{F,1}}),8,[](RoutineContext& c){
        nop(c,0); c.cpu().iff1=c.cpu().iff2=1; c.finish_core(1);
    },"accepted interrupt");

    const std::vector<std::function<void(RoutineContext&)>> uses{
        [](RoutineContext& c){(void)c.read(0);}, [](RoutineContext& c){c.write(0xc000,1);},
        [](RoutineContext& c){(void)c.fetch_opcode(0);}, [](RoutineContext& c){c.account_fetches(1);},
        [](RoutineContext& c){c.advance(1);}, [](RoutineContext& c){(void)c.read_port(0xdc,0);},
        [](RoutineContext& c){c.write_port(0x7f,0,0);}};
    for (const auto& use:uses)
        guard(nops,SegmentSpec{{0,0},{{F,0}},false,0,true},4,[=](RoutineContext& c){use(c);c.finish_core(0);},"single-instruction");
    // A forced body that the context rejects before its core step falls back
    // with its register changes undone; so do an undeclared exit and a missing finish.
    auto failures=uses;
    failures.push_back([](RoutineContext& c){c.finish_return(0);});
    failures.push_back([](RoutineContext& c){c.cpu().sp.uint16_value^=2;});
    for (const auto& use:failures) {
        Machine m(nops); stack(m); Machine native=m;
        RoutineRegistry r;
        r.add({0,0},forced_spec(0),cost(4),[=](RoutineContext& c){
            c.cpu().bc.uint16_value^=0x5a5a; use(c); c.finish_core(0);
        });
        m.step(r); native.step();
        const auto& n=r.find({0,0})->counters;
        check(m.equivalent(native) && n.forced_failed==1 && n.fallback_calls==1 && n.cpp_calls==0 && n.forced_calls==0,
              "a rejected forced body falls back with its registers restored");
        // Its fallback carries no window reason: the forced failure accounts for it.
        check(std::all_of(n.reasons.begin(),n.reasons.end(),[](auto count){return count==0;}) &&
              std::all_of(n.forced_reasons.begin(),n.forced_reasons.end(),[](auto count){return count==0;}),
              "a forced body rejected before its step leaves no reason counted");
    }

    auto refused=[](RoutineEntry entry, SegmentSpec spec, std::string_view message) {
        RoutineRegistry r;
        rejects([&]{r.add(entry,std::move(spec),cost(4),[](RoutineContext& c){c.finish_core(0);});},message);
    };
    refused({0,0},{{0,0},{{F,0}},false,0,false,true},"Invalid registered segment");
    refused({0,0},{{0,0},{{R,0}},false,0,true,true},"Invalid registered segment");
    refused({0,0},{{0,0},{{F,1}},false,0,true,true},"Invalid registered segment");
    refused({0,0},{{0,0},{{F,0},{R,0}},false,0,true,true},"Invalid registered segment");
    refused({0,0},{{0,0},{{F,1}},false,0,true,false},"Invalid registered segment");
    refused({0,0},{{0,0},{}},"Invalid registered segment");
    refused({0,0},{{0,0},{{F,1},{F,1}}},"Invalid registered segment");
    // A core_final exit at the entry is the whole segment and must be declared single-instruction.
    refused({0,0},{{0,0},{{F,0}}},"Invalid registered segment");
    refused({0,0},{{0,0},{{R,2},{F,0}}},"Invalid registered segment");
    // The scratch stack is bounded.
    refused({0,0},{{0,0},{{R,2}},false,max_scratch_stack+1},"Invalid registered segment");
    refused({0,0},{{0,0},{{F,0xc000}}},"Invalid registered segment");
    refused({0,0},{{32,0},{{F,1}}},"Invalid registered segment");
    refused({0,0xc000},{{0,0},{{F,1}}},"Invalid registered routine");
    RoutineRegistry accepted;
    accepted.add({0,8},{{0,8},{{R,10}},false,max_scratch_stack},cost(4),[](RoutineContext& c){c.finish_return(10);});
    accepted.add({0,0x10},{{0,0x10},{{F,0x10}},false,0,true},cost(4),[](RoutineContext& c){c.finish_core(0x10);});
    accepted.add({0,0},forced_spec(0),cost(4),[](RoutineContext& c){c.finish_core(0);});
    rejects([&]{accepted.add({0,0},segment(0,{{F,1}}),cost(4),[](RoutineContext& c){c.finish_core(1);});},"Duplicate");
}
enum class Event { irq, nmi, frame };
Machine windowed(std::shared_ptr<const Memory::Rom> rom, Event event, std::uint64_t distance) {
    Machine m(std::move(rom)); stack(m);
    if (event==Event::irq) { m.read_port(0xbf,0); m.cpu().iff1=m.cpu().iff2=1; m.advance(43986-distance); }
    if (event==Event::nmi) { m.set_pause(true); m.read_port(0xbf,0); m.advance(Machine::pause_sample_offset-distance); }
    if (event==Event::frame) m.advance(Machine::frame_cycles-distance);
    return m;
}
RoutineFallback reason_of(Event event) {
    return event==Event::irq ? RoutineFallback::irq : event==Event::nmi ? RoutineFallback::nmi : RoutineFallback::frame;
}
void forced_body(RoutineContext& c) {
    // An option-style adjustment around the core step, netting to nothing.
    auto& z=c.cpu(); z.bc.uint8_values.at_1^=0x5a; c.finish_core(0); z.bc.uint8_values.at_1^=0x5a;
}
void segment_windows() {
    // Two instructions in C++ and a CALL on the core: 25 cycles. Every event
    // inside [entry,E) falls back; one at E is admitted.
    const auto multi=image({{0,{0x00,0x00,0xcd,0x00,0x01}},{0x100,{0x00,0xc9}}});
    const auto single=program({0xc6,0x05});
    for (auto event:{Event::irq,Event::nmi,Event::frame}) {
        const unsigned first=event==Event::frame ? 1 : 0;
        for (unsigned d=first;d<=25;++d) {
            Machine cpp=windowed(multi,event,d), native=cpp;
            RoutineRegistry r;
            r.add({0,0},segment(0,{{F,2}}),cost(25),[](RoutineContext& c){nop(c,0);nop(c,1);c.finish_core(2);});
            cpp.step(r);
            const auto& n=r.find({0,0})->counters;
            const bool fallback=d<25;
            check(n.cpp_calls==!fallback && n.fallback_calls==fallback && n.fallback(reason_of(event))==fallback,
                  "multi-instruction segment falls back at every cycle of its window");
            same_as_native(cpp,native,"segment window outcome equals the original");
        }
        // A forced single instruction is admitted at every cycle, with zero delay.
        for (unsigned d=first;d<=7;++d) {
            Machine cpp=windowed(single,event,d), native=cpp;
            RoutineRegistry r; r.add({0,0},forced_spec(0),cost(7),forced_body);
            cpp.step(r);
            const auto& n=r.find({0,0})->counters;
            check(n.cpp_calls==1 && n.forced_calls==1 && n.fallback_calls==0 && n.forced_failed==0 &&
                  n.forced(reason_of(event))==(d<7) && n.fallback(reason_of(event))==0,"forced admission counted");
            same_as_native(cpp,native,"forced instruction leaves the machine identical, also afterwards");
        }
    }
    // EI just executed with the line asserted, and an unmapped stack: admitted.
    Machine ei(image({{0,{0xc6,0x05}},{0x100,{0xfb}}})); stack(ei);
    ei.cpu().pc.uint16_value=0x100; ei.step(); ei.cpu().pc.uint16_value=0;
    Machine native=ei;
    RoutineRegistry r; r.add({0,0},forced_spec(0),cost(7),forced_body);
    ei.step(r);
    check(r.find({0,0})->counters.forced(RoutineFallback::ei_delay)==1 && r.find({0,0})->counters.cpp_calls==1,"forced EI delay");
    same_as_native(ei,native,"forced instruction after EI is accepted at the same boundary");
    Machine unmapped(single); stack(unmapped); unmapped.memory().set_memory_control(0x10); native=unmapped;
    unmapped.step(r);
    check(r.find({0,0})->counters.forced(RoutineFallback::unmapped_stack)==1,"forced unmapped stack");
    same_as_native(unmapped,native,"forced instruction with an unmapped stack");
    // The NMI response's acceptance marker at 0066 is a forced failure.
    Machine delay(image({{0x66,{0xc6,0x05}}})); stack(delay); delay.schedule_nmi(0); delay.advance(1); delay.step();
    check(delay.cpu().pc.uint16_value==0x66,"NMI response reaches its handler");
    native=delay;
    RoutineRegistry handler; handler.add({0,0x66},forced_spec(0x66),cost(7),[](RoutineContext& c){c.finish_core(0x66);});
    delay.step(handler);
    const auto& n=handler.find({0,0x66})->counters;
    check(n.forced_failed==1 && n.fallback_calls==1 && n.fallback(RoutineFallback::interrupt_delay)==1 &&
          n.cpp_calls==0 && n.forced_calls==0,"interrupt delay at a forced entry is a counted forced failure");
    same_as_native(delay,native,"forced failure runs natively");
}
void absolute_store_segment() {
    auto spec=forced_spec(0); spec.absolute_wram_store=0xc220;
    RoutineRegistry registry;
    registry.add({0,0},spec,cost(16),[](RoutineContext& c) {
        auto& cpu=c.cpu(); const auto original=cpu.hl.uint16_value;
        cpu.hl.uint16_value=0x4567; c.finish_core(0); cpu.hl.uint16_value=original;
    });
    for (auto event:{Event::irq,Event::nmi,Event::frame}) {
        const auto first=event==Event::frame ? 1u : 0u;
        for (unsigned distance=first;distance<=16;++distance) {
            Machine actual=windowed(program({0x22,0x20,0xc2}),event,distance); actual.cpu().hl.uint16_value=0x1234;
            Machine expected=actual; expected.cpu().hl.uint16_value=0x4567;
            expected.step(); expected.cpu().hl.uint16_value=0x1234;
            actual.step(registry);
            check(actual.equivalent(expected),"store transform preserves native cycles, registers and bus state");
        }
    }
    Machine delay(image({{0x66,{0x22,0x20,0xc2}}})); stack(delay);
    delay.schedule_nmi(0); delay.advance(1); delay.step();
    check(delay.cpu().pc.uint16_value==0x66,"NMI reached handler store");
    delay.cpu().hl.uint16_value=0x1234; Machine expected=delay;
    expected.cpu().hl.uint16_value=0x4567; expected.step(); expected.cpu().hl.uint16_value=0x1234;
    auto at_handler=forced_spec(0x66); at_handler.absolute_wram_store=0xc220;
    RoutineRegistry handler; handler.add({0,0x66},at_handler,cost(16),[](RoutineContext& c){
        auto& cpu=c.cpu(); const auto original=cpu.hl.uint16_value;
        cpu.hl.uint16_value=0x4567; c.finish_core(0x66); cpu.hl.uint16_value=original;
    });
    delay.step(handler);
    check(handler.find({0,0x66})->counters.forced(RoutineFallback::interrupt_delay)==1 && delay.equivalent(expected),
          "store executes under NMI rejection with the same native result");
    Machine halted(image({{0,{0x76,0x22,0x20,0xc2}},{0x66,{0xed,0x45}}})); stack(halted);
    halted.cpu().hl.uint16_value=0x1234; halted.step();
    auto at_one=forced_spec(1); at_one.absolute_wram_store=0xc220;
    RoutineRegistry after_halt; after_halt.add({0,1},at_one,cost(16),[](RoutineContext& c){
        auto& cpu=c.cpu(); const auto original=cpu.hl.uint16_value;
        cpu.hl.uint16_value=0x4567; c.finish_core(1); cpu.hl.uint16_value=original;
    });
    halted.step(after_halt); // A halted core must not execute the store.
    check(halted.memory().read(0xc220)!=0x67,"HALT did not store the reward");
    halted.schedule_nmi(halted.cycles()+10);
    for(unsigned i=0;i<100 && halted.cpu().pc.uint16_value!=4;++i) halted.step(after_halt);
    check(halted.cpu().pc.uint16_value==4 && halted.memory().read(0xc220)==0x67 &&
          halted.memory().read(0xc221)==0x45 && halted.cpu().hl.uint16_value==0x1234 &&
          after_halt.find({0,1})->counters.forced_calls==1,"HALT resumes and transforms the store once");
    // With work RAM disabled there is no reward destination. Reject the
    // transformed store before its core step, restore registers and let the
    // native ignored write drive the ordinary open-bus value.
    Machine disabled(program({0x22,0x20,0xc2})); stack(disabled);
    disabled.cpu().hl.uint16_value=0x1234; disabled.memory().set_memory_control(0x10);
    Machine native_disabled=disabled; native_disabled.step();
    const auto before=registry.find({0,0})->counters.forced_failed;
    disabled.step(registry);
    check(disabled.equivalent(native_disabled) && registry.find({0,0})->counters.forced_failed==before+1,
          "disabled work RAM falls back to the unmodified native write");
    auto invalid=forced_spec(0); invalid.absolute_wram_store=0x8000;
    rejects([&]{RoutineRegistry bad; bad.add({0,0},invalid,cost(16),[](RoutineContext& c){c.finish_core(0);});},"Invalid");
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
}
int main(int argc,char** argv) {
    try {
        if(argc==2 && std::string_view(argv[1])=="irq_query") irq_query();
        else {results(); vdp_results(); windows(); registry_and_guards(); scratch_overwrite();
            segments(); segment_faults(); segment_windows(); absolute_store_segment();}
        std::cout<<"Registered routine tests passed.\n"; return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
