// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/machine.h"
#include <algorithm>
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
    const auto saved=cpp; Machine copied=cpp; copied=cpp;
    ++registry.find({0,0})->counters.cpp_calls;
    registry.enabled=false;
    check(copied.equivalent(saved),"registry flags and counters live outside copied machine state");

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
        else {results(); vdp_results(); windows(); registry_and_guards(); scratch_overwrite();}
        std::cout<<"Registered routine tests passed.\n"; return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
