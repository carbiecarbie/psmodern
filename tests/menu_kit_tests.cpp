// SPDX-License-Identifier: GPL-3.0-or-later
// The milestone 12 menu kit (src/game/menu_kit.h) against the core, without a
// ROM (authored programs only):
//   1. every instruction the menu kit adds, rendered on a counting bus and
//      compared with the native steps from many register states (registers
//      with IX and IY, flags with their undocumented bits, MEMPTR, SP, PC,
//      work RAM, cycles, opcode fetches and R);
//   2. INI through the registered-routine mechanism with the per-call
//      verifier: a VRAM read loop (whole machine compared, the VDP's address,
//      read buffer and latch included, from a read buffer left by a write and
//      by a read), chunked with a conditional loop exit; and INI from the V
//      counter port, whose byte depends on the exact cycle of the read, from
//      start cycles spread over a scan line, so a read one T-state early or
//      late changes the bytes stored (shown by a deliberately mistimed
//      rendering that the verifier refuses);
//   3. the estimating bus: each segment's bound equals the cycles the live
//      call took, and an inline subroutine that does not return to its call
//      (a stack in ROM) makes the estimate uncertain, so the call is refused (the run stops).
#include "game/menu_kit.h"
#include "scaffold.h"
#include "probe.h"
#include "replay/verification.h"
#include <functional>
#include <iostream>
#include <random>
#include <tuple>
#include <type_traits>
#include <algorithm>

using namespace psmodern;
using probe::check;
namespace mk=game::menu_kit;
namespace {
// ---- 1. Instructions against the native steps ----------------------------------------------
struct CountingBus {
    Machine& machine;
    std::uint64_t cycles=0, fetches=0;
    void fetch(std::uint16_t) { ++fetches; }
    std::uint8_t read(std::uint16_t address) { return machine.memory().read(address); }
    void write(std::uint16_t address, std::uint8_t value) { machine.memory().write(address,value); }
    void advance(std::uint64_t count) { cycles+=count; }
    std::uint64_t elapsed() const { return cycles; }
    void expect(std::uint16_t, OpcodePattern) {} void interrupts(bool) {} void return_from_nmi() {} bool guard(bool holds, std::string_view) { return holds; }
};
using Z=mk::Instructions<CountingBus>;
constexpr std::uint16_t code_at=0x1000, callee_at=0x1100;
using mk::Index;

struct KitCase {
    const char* name;
    std::function<std::vector<std::uint8_t>(std::uint8_t n)> code;   // n: an operand byte varied per state
    std::function<std::uint16_t(Z&)> render;                         // returns the next PC
    std::vector<std::uint8_t> callee{};                              // code at callee_at
};
void kit_case(const KitCase& c, unsigned states) {
    for (std::uint64_t seed=0;seed<states;++seed) {
        const auto n=static_cast<std::uint8_t>(seed*37+11);
        auto rom=std::make_shared<Memory::Rom>(512*1024,0);
        const auto code=c.code(n);
        std::copy(code.begin(),code.end(),rom->begin()+code_at);
        std::copy(c.callee.begin(),c.callee.end(),rom->begin()+callee_at);
        Machine rendered(rom);
        (void)rendered.read_port(0xbf,rendered.cycles()); rendered.advance(1);
        auto& cpu=rendered.cpu();
        probe::randomize(cpu,seed+1);
        std::mt19937_64 random(seed*977+5);
        cpu.hl.uint16_value=static_cast<std::uint16_t>(0xC000+random()%0x0F00);
        cpu.de.uint16_value=static_cast<std::uint16_t>(0xD000+random()%0x0E00);
        cpu.sp.uint16_value=static_cast<std::uint16_t>(0xC100+random()%0x1D00);
        cpu.ix_iy[0].uint16_value=static_cast<std::uint16_t>(0xC100+random()%0x1D00);
        cpu.ix_iy[1].uint16_value=static_cast<std::uint16_t>(0xC100+random()%0x1D00);
        if (seed%7==0) cpu.af.uint8_values.at_1=0;                    // zero results and taken conditions
        cpu.pc.uint16_value=code_at; cpu.iff1=cpu.iff2=0;
        for (std::uint16_t a=0xC000;a<0xE000;a+=3) rendered.memory().write(a,static_cast<std::uint8_t>(random()));
        oracle::NativeMachine native(rendered);   // the original: the pinned core
        const auto cycles=native.cycles(), fetches=native.opcode_fetches();
        const auto r_before=native.cpu().r;
        CountingBus bus{rendered};
        auto registers=mk::load(rendered.cpu());
        Z z(bus,registers);
        const auto pc=c.render(z);
        mk::store(rendered.cpu(),registers);
        // The native steps up to the PC the rendering reached (an index
        // prefix is its own core step, resume XY).
        for (unsigned steps=0;steps<16;++steps) {
            do native.step(); while (native.cpu().resume==Z80::resume_xy);
            if (native.cpu().pc.uint16_value==pc) break;
        }
        const auto& a=native.cpu(); const auto& b=rendered.cpu();
        std::string fields;
        auto compare=[&](const char* field, unsigned x, unsigned y) { if (x!=y) fields+=std::string(" ")+field; };
        compare("AF",a.af.uint16_value,b.af.uint16_value); compare("BC",a.bc.uint16_value,b.bc.uint16_value);
        compare("DE",a.de.uint16_value,b.de.uint16_value); compare("HL",a.hl.uint16_value,b.hl.uint16_value);
        compare("IX",a.ix_iy[0].uint16_value,b.ix_iy[0].uint16_value); compare("IY",a.ix_iy[1].uint16_value,b.ix_iy[1].uint16_value);
        compare("SP",a.sp.uint16_value,b.sp.uint16_value); compare("MEMPTR",a.memptr.uint16_value,b.memptr.uint16_value);
        compare("PC",a.pc.uint16_value,pc);
        compare("cycles",static_cast<unsigned>(native.cycles()-cycles),static_cast<unsigned>(bus.cycles));
        compare("fetches",static_cast<unsigned>(native.opcode_fetches()-fetches),static_cast<unsigned>(bus.fetches));
        compare("R",static_cast<unsigned>((native.cpu().r-r_before)&0x7f),static_cast<unsigned>(bus.fetches&0x7f));
        if (native.memory().ram()!=rendered.memory().ram()) fields+=" RAM";
        check(fields.empty(),std::string("menu kit rendering of ")+c.name+" differs from the core (state "+std::to_string(seed)+"):"+fields);
    }
}
std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> list) { return list; }
std::uint16_t next(unsigned length) { return static_cast<std::uint16_t>(code_at+length); }

// A callee with two returns: or a; ret z; inc a; ret (the shape of the digit
// helper and the per-colour fade steps).
template<class C> std::uint16_t two_returns(C& c) {
    c.or_a(callee_at);
    if (c.return_if(callee_at+1,c.zero())) return c.r.wz;
    c.increment(callee_at+2,c.r.a);
    return c.return_from(callee_at+3);
}

void instructions() {
    const std::vector<KitCase> cases{
        {"cpl",[](auto) { return bytes({0x2F}); },[](Z& z) { z.complement_a(code_at); return next(1); }},
        {"res 6,d",[](auto) { return bytes({0xCB,0xB2}); },[](Z& z) { z.reset_bit(code_at,6,z.r.d); return next(2); }},
        {"res 0,a",[](auto) { return bytes({0xCB,0x87}); },[](Z& z) { z.reset_bit(code_at,0,z.r.a); return next(2); }},
        {"res 7,h",[](auto) { return bytes({0xCB,0xBC}); },[](Z& z) { z.reset_bit(code_at,7,z.r.h); return next(2); }},
        {"set 7,h",[](auto) { return bytes({0xCB,0xFC}); },[](Z& z) { z.set_bit(code_at,7,z.r.h); return next(2); }},
        {"set 0,b",[](auto) { return bytes({0xCB,0xC0}); },[](Z& z) { z.set_bit(code_at,0,z.r.b); return next(2); }},
        {"set 3,e",[](auto) { return bytes({0xCB,0xDB}); },[](Z& z) { z.set_bit(code_at,3,z.r.e); return next(2); }},
        {"sub (iy+d)",[](auto n) { return bytes({0xFD,0x96,n}); },[](Z& z) { z.subtract_indexed(code_at,Index::iy); return next(3); }},
        {"sub (ix+d)",[](auto n) { return bytes({0xDD,0x96,n}); },[](Z& z) { z.subtract_indexed(code_at,Index::ix); return next(3); }},
        {"inline subroutine with two returns",[](auto) { return bytes({0xCD,0x00,0x11}); },
            [](Z& z) { (void)z.inline_subroutine(code_at,[](auto& c) { return two_returns(c); }); return next(3); },
            bytes({0xB7,0xC8,0x3C,0xC9})},
        // The earlier kits keep working through the menu kit's instructions.
        {"bit 3,c",[](auto) { return bytes({0xCB,0x59}); },[](Z& z) { z.test_bit(code_at,3,z.r.c); return next(2); }},
        {"srl a",[](auto) { return bytes({0xCB,0x3F}); },[](Z& z) { z.shift_right_logical(code_at,z.r.a); return next(2); }},
        {"rr e",[](auto) { return bytes({0xCB,0x1B}); },[](Z& z) { z.rotate_right_through_carry(code_at,z.r.e); return next(2); }},
        {"rrca",[](auto) { return bytes({0x0F}); },[](Z& z) { z.rotate_right_circular_a(code_at); return next(1); }},
        {"cp (iy+d)",[](auto n) { return bytes({0xFD,0xBE,n}); },[](Z& z) { z.compare_indexed(code_at,Index::iy); return next(3); }},
        {"ld (iy+d),a",[](auto n) { return bytes({0xFD,0x77,n}); },[](Z& z) { z.store_to_indexed(code_at,Index::iy,z.r.a); return next(3); }},
        {"ldi",[](auto) { return bytes({0xED,0xA0}); },[](Z& z) { z.load_increment(code_at); return next(2); }},
    };
    for (const auto& c:cases) kit_case(c,400);
    std::cout<<cases.size()<<" menu kit renderings match the core over 400 states each.\n";
}

// ---- 2 and 3. INI and the inline subroutine through the mechanism --------------------------------
// Authored image: the VDP address helper at 00:0008 (ld a,e; out (BFh),a;
// ld a,d; out (BFh),a; ret) and the parts given.
std::shared_ptr<const Memory::Rom> authored(std::initializer_list<std::pair<std::uint16_t,std::vector<std::uint8_t>>> parts) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    const std::vector<std::uint8_t> helper{0x7B,0xD3,0xBF,0x7A,0xD3,0xBF,0xC9};
    std::copy(helper.begin(),helper.end(),rom->begin()+mk::vdp_address_helper);
    for (const auto& [at,code]:parts) std::copy(code.begin(),code.end(),rom->begin()+at);
    for (unsigned i=0x4000;i<0x8000;++i) (*rom)[i]=static_cast<std::uint8_t>(i*7+3);
    return rom;
}
Machine started(const std::shared_ptr<const Memory::Rom>& rom, std::uint16_t pc, std::uint64_t seed,
                const std::function<void(Machine&,std::mt19937_64&)>& arrange) {
    Machine m(rom);
    (void)m.read_port(0xbf,m.cycles()); m.advance(1);
    probe::randomize(m.cpu(),seed);
    m.cpu().sp.uint16_value=0xDF00; m.cpu().pc.uint16_value=pc; m.cpu().iff1=m.cpu().iff2=0;
    m.memory().write(0xDF00,0x80); m.memory().write(0xDF01,0x01);
    std::mt19937_64 random(seed*131+7);
    for (std::uint16_t a=0xC000;a<0xDE00;++a) m.memory().write(a,static_cast<std::uint8_t>(random()));
    // Video memory from the seed, written through the ports.
    const auto at=m.cycles();
    m.write_port(0xbf,0x00,at); m.write_port(0xbf,0x40,at);
    for (unsigned i=0;i<0x4000;++i) m.write_port(0xbe,static_cast<std::uint8_t>(random()),at);
    arrange(m,random);
    return m;
}
template<class M> void until_return(M& m, RoutineRegistry* registry) {
    for (unsigned i=0;i<1000000 && m.cpu().pc.uint16_value!=0x0180;++i) {
        if constexpr (!std::is_same_v<M,Machine>) { (void)registry; (void)m.step(); continue; }
        else {
        if (!registry) { (void)m.step(); continue; }
        // The estimate of every C++ call equals the cycles the call takes.
        const auto* routine=registry->find({0,m.cpu().pc.uint16_value});
        const auto before=routine ? routine->counters.cpp_calls : 0;
        const auto bound=routine ? routine->bound(m) : 0;
        const auto elapsed=m.step(*registry);
        if (routine && routine->counters.cpp_calls==before+1)
            check(bound==elapsed,"estimated bound "+std::to_string(bound)+" differs from the live call's "+std::to_string(elapsed)+" cycles");
        }
    }
    check(m.cpu().pc.uint16_value==0x0180,"authored program returned");
}
struct Calls { std::uint64_t cpp=0, fallbacks=0; };
Calls same_as_native(Machine cpp, RoutineRegistry& registry, const std::string& what) {
    {
        // Every call is also compared with the oracle at each of its
        // instruction boundaries (milestone 14), on copies of both.
        Machine checked=cpp; RoutineRegistry copy=registry;
        oracle::Lockstep lockstep(checked,what,oracle::Mode::instructions);
        for (unsigned i=0;i<1000000 && checked.cpu().pc.uint16_value!=0x0180;++i) lockstep.step(checked,&copy);
    }
    oracle::NativeMachine native(cpp);
    until_return(cpp,&registry); until_return(native,nullptr);
    std::string fields;
    for (const auto& difference:native.differences(cpp)) fields+=" "+difference.field;
    check(fields.empty(),what+" differs from the original program:"+fields);
    check(native.last_access()==cpp.last_access(),what+": last port access differs");
    Calls calls;
    for (const auto& [entry,routine]:registry.entries()) { calls.cpp+=routine.counters.cpp_calls; calls.fallbacks+=routine.counters.fallback_calls; }
    return calls;
}

// A deliberately mistimed INI (the read one T-state early), to show that the
// V counter program detects a wrong T-state.
template<class Bus> void mistimed_input_increment(mk::Instructions<Bus>& z, std::uint16_t at) {
    auto& r=z.r;
    z.bus().fetch(at); z.bus().fetch(at+1);
    const auto port=z.bc();
    const auto value=z.bus().in(port,11);
    z.bus().write(z.hl(),value);
    z.set_hl(static_cast<std::uint16_t>(z.hl()+1));
    r.wz=static_cast<std::uint16_t>(port+1);
    r.b=static_cast<std::uint8_t>(r.b-1);
    const unsigned t=value+static_cast<std::uint8_t>(r.c+1);
    r.f=static_cast<std::uint8_t>((r.b&(mk::sign_flag|mk::y_flag|mk::x_flag))|(r.b ? 0 : mk::zero_flag)|
        game::kit::parity(static_cast<std::uint8_t>((t&7)^r.b))|(t>255 ? (mk::half_flag|mk::carry_flag) : 0)|((value>>6)&mk::subtract_flag));
    z.bus().advance(5);
}

void ports_and_inline() {
    using namespace mk;
    // E: a VRAM read loop as the window engine's: 0500 rst 08; 0501 ld c,BEh;
    //    0503 ini; 0505 jp nz,0503; 0508 ret.
    const auto e=authored({{0x0500,{0xCF,0x0E,0xBE,0xED,0xA2,0xC2,0x03,0x05,0xC9}}});
    // F: the V counter: 0600 ld c,7Eh; 0602 ini; 0604 jp nz,0602; 0607 ret.
    const auto f=authored({{0x0600,{0x0E,0x7E,0xED,0xA2,0xC2,0x02,0x06,0xC9}}});
    // G: an inline subroutine: 0700 call 0710; 0703 ret; 0710 or a; 0711 ret z; 0712 inc a; 0713 ret.
    const auto g=authored({{0x0700,{0xCD,0x10,0x07,0xC9}},{0x0710,{0xB7,0xC8,0x3C,0xC9}}});

    constexpr std::uint16_t after=300;
    auto read_loop=[](auto& z, std::uint16_t at) {
        for (;;) {
            z.input_increment(at);
            if (z.loop_exit(after)) return scaffold::continues(0x0505,jump_cycles);
            if (!z.jump_if(0x0505,!z.zero())) return scaffold::returns(0x0508,return_cycles);
            at=0x0503;
        }
    };
    auto counter_loop=[](auto& z, bool mistimed) {
        z.load_immediate(0x0600,z.r.c);
        for (;;) {
            if (mistimed) mistimed_input_increment(z,0x0602); else z.input_increment(0x0602);
            if (!z.jump_if(0x0604,!z.zero())) return scaffold::returns(0x0607,return_cycles);
        }
    };
    auto program_g=[](auto& z) {
        (void)z.inline_subroutine(0x0700,[](auto& c) {
            c.or_a(0x0710);
            if (c.return_if(0x0711,c.zero())) return c.r.wz;
            c.increment(0x0712,c.r.a);
            return c.return_from(0x0713);
        });
        return scaffold::returns(0x0703,return_cycles);
    };
    unsigned buffers=0, mistimed_refused=0, mistimed_bytes=0;
    for (std::uint64_t seed=1;seed<=60;++seed) {
        // E, from a read buffer left by a write (even seeds) or by a read (odd seeds).
        RoutineRegistry re;
        add_segment(re,{0,0x0500},segment({0,0x0500},{conditional_loop_at(0x0505,after),returned_at(0x0508)},{.scratch_stack=2}),
            [read_loop](auto& z) { (void)z.restart_vdp_address(0x0500); z.load_immediate(0x0501,z.r.c); return read_loop(z,0x0503); });
        add_segment(re,{0,0x0503},segment({0,0x0500},{conditional_loop_at(0x0505,after),returned_at(0x0508)}),
            [read_loop](auto& z) { return read_loop(z,0x0503); });
        const auto ce=same_as_native(started(e,0x0500,seed,[seed,&buffers](Machine& m, std::mt19937_64& random) {
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xC800+random()%0x0400);
            m.cpu().bc.uint8_values.at_1=seed==7 ? 0 : static_cast<std::uint8_t>(1+random()%64);   // B = 0 reads 256 bytes
            m.cpu().de.uint16_value=static_cast<std::uint16_t>(random()&0x3FFF);                 // a VRAM read address
            const auto at=m.cycles();
            m.write_port(0xbf,static_cast<std::uint8_t>(random()),at);
            m.write_port(0xbf,static_cast<std::uint8_t>(seed%2 ? (random()&0x3F) : (0x40|(random()&0x3F))),at);
            if (seed%2) (void)m.read_port(0xbe,at); else m.write_port(0xbe,static_cast<std::uint8_t>(random()),at);
            ++buffers;
        }),re,"INI from VRAM (seed "+std::to_string(seed)+")");
        check(ce.cpp>=1 && ce.fallbacks==0,"program E ran in C++");

        // F, from start cycles spread over more than one scan line.
        for (const bool mistimed:{false,true}) {
            RoutineRegistry rf;
            add_segment(rf,{0,0x0600},segment({0,0x0600},{returned_at(0x0607)}),[counter_loop,mistimed](auto& z) { return counter_loop(z,mistimed); });
            const auto m=started(f,0x0600,seed,[seed](Machine& machine, std::mt19937_64& random) {
                machine.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xC800+random()%0x0400);
                machine.cpu().bc.uint8_values.at_1=64;
                machine.advance(seed*41%300);
            });
            if (!mistimed) {
                const auto cf=same_as_native(m,rf,"INI from the V counter (seed "+std::to_string(seed)+")");
                check(cf.cpp==1 && cf.fallbacks==0,"program F ran in C++");
                continue;
            }
            try { (void)same_as_native(m,rf,"mistimed INI"); }
            catch (const std::exception& error) {
                // The lockstep sees different bytes (a read across a line change),
                // or the final comparison sees the port access one cycle early.
                const std::string what=error.what();
                if (what.find("Lockstep divergence")!=std::string::npos) ++mistimed_bytes;
                else if (what.find("last port access")!=std::string::npos) ++mistimed_refused;
            }
        }

        RoutineRegistry rg;
        add_segment(rg,{0,0x0700},segment({0,0x0700},{returned_at(0x0703)},{.scratch_stack=2}),program_g);
        const auto cg=same_as_native(started(g,0x0700,seed,[seed](Machine& m, std::mt19937_64&) {
            if (seed%3==0) m.cpu().af.uint8_values.at_1=0;   // the conditional return taken
        }),rg,"inline subroutine (seed "+std::to_string(seed)+")");
        check(cg.cpp==1 && cg.fallbacks==0,"program G ran in C++");
    }
    check(buffers==60,"every read-buffer state ran");
    check(mistimed_bytes>0,"a read one T-state early stores another V counter byte on some start cycle (the test can fail)");
    check(mistimed_bytes+mistimed_refused==60,"a read one T-state early is refused on every start cycle");

    // A stack in ROM: the call pushes nothing and the callee's return reads a
    // ROM word. The estimate becomes uncertain and dispatch refuses the call.
    {
        RoutineRegistry rg;
        add_segment(rg,{0,0x0700},segment({0,0x0700},{returned_at(0x0703)},{.scratch_stack=2}),program_g);
        Machine m=started(g,0x0700,3,[](Machine& machine, std::mt19937_64&) { machine.cpu().sp.uint16_value=0x7000; });
        check(rg.find({0,0x0700})->bound(m)==uncertain_maximum,"a ROM stack makes the inline subroutine's estimate uncertain");
        const auto before=m.cycles();
        bool stopped=false;
        try { m.step(rg); } catch (const UnhandledState&) { stopped=true; }
        const auto& counters=rg.find({0,0x0700})->counters;
        check(stopped && counters.cpp_calls==0 && counters.fallback_calls==1 && counters.fallback(RoutineFallback::frame)==1,
              "a ROM stack refuses the call (reason frame) and stops the run");
        check(m.cycles()==before && m.cpu().pc.uint16_value==0x0700,"the refusal runs nothing");
    }
    std::cout<<"INI from VRAM and from the V counter and the inline subroutine match the original through the mechanism ("
             <<"a read one T-state early stores another byte on "<<mistimed_bytes<<" of 60 start cycles and is refused by its port timing on the rest); bounds are exact.\n";
}
void input_increment_observers() {
    const auto rom=authored({{0x0500,{0xED,0xA2,0xC9}},
        {0x0038,{0xF5,0xDB,0xBF,0xF1,0xFB,0xED,0x4D}},{0x0066,{0xED,0x45}}});
    for(unsigned destination:{0xC100u,0xE100u,0x8100u})
    for(unsigned port:{0x7Eu,0xBEu,0xDCu,0xBFu})
    for(unsigned event=0;event<3;++event)
    for(unsigned distance:{0u,1u,25u,26u,27u,1000u}) {
        if(event==0 && distance==0) continue;
        RoutineRegistry registry;
        mk::add_segment(registry,{0,0x0500},mk::segment({0,0x0500},{mk::returned_at(0x0502)}),
            [](auto& z) {z.input_increment(0x0500);return scaffold::returns(0x0502,mk::return_cycles);});
        auto actual=started(rom,0x0500,destination+port,[&](Machine& m,auto&) {
            if(destination==0x8100) m.memory().write(0xFFFC,8);
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(destination);
            m.cpu().bc.uint16_value=static_cast<std::uint16_t>(0x0200|port);
            m.cpu().sp.uint16_value=0xDF00;
            m.memory().write(0xDF00,0x80);m.memory().write(0xDF01,1);
        });
        if(event==0) {
            const auto phase=actual.cycles()%Machine::frame_cycles;
            actual.advance((Machine::frame_cycles-distance+Machine::frame_cycles-phase)%Machine::frame_cycles);
        } else if(event==1) {
            actual.write_port(0xBF,0x20,actual.cycles());actual.write_port(0xBF,0x81,actual.cycles());
            const auto irq=actual.vdp().next_irq();
            check(irq && *irq>=actual.cycles()+distance,"INI fixture has a future IRQ");
            actual.advance(*irq-actual.cycles()-distance);actual.cpu().iff1=actual.cpu().iff2=1;
        } else actual.schedule_nmi(actual.cycles()+distance);
        Machine native=actual;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
        std::vector<Write> expected,observed;
        std::uint64_t instruction_start=0;
        struct Stopped {};
        auto run=[&](Machine& m,RoutineRegistry* r,std::vector<Write>& writes) {
            Machine::WriteObserver observer=[&](std::uint16_t a,std::uint8_t v) {writes.emplace_back(a,v,m.cycles());};
            Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
                if(pc==0x0500) instruction_start=m.cycles();
                if(pc==0x0180) throw Stopped{};
                return {};
            };
            try {for(unsigned frame=0;frame<4;++frame) m.run_frame({},r,hook,observer);}
            catch(const Stopped&) {return;}
            throw std::runtime_error("INI observer fixture did not return");
        };
        run(native,nullptr,expected);const auto native_start=instruction_start;
        run(actual,&registry,observed);
        check(actual.equivalent(native),"INI callback operation preserves complete state, flags, WZ, R and events");
        check(expected==observed,"INI preserves every callback address, byte, order and cycle");
        check(expected_ports.accesses()==actual_ports.accesses(),"INI preserves exact port values, order and timestamps");
        const auto store=std::find_if(observed.begin(),observed.end(),[&](const auto& w) {return std::get<0>(w)==destination;});
        check(store!=observed.end() && std::get<2>(*store)==native_start,"INI store callback stays at instruction entry");
        const auto read=std::find_if(actual_ports.accesses().begin(),actual_ports.accesses().end(),[&](const auto& a) {return !a.write && a.port==port && a.cycle==native_start+12;});
        check(read!=actual_ports.accesses().end() && read->value==std::get<1>(*store),"INI stores the byte read at T12");
    }
    for(unsigned destination:{0xC100u,0xFFFCu,0xFFFFu}) {
        RoutineRegistry registry;
        const auto bound=destination==0xC100 ? 15u : 26u;
        registry.add({0,0x0500},mk::segment({0,0x0500},{mk::returned_at(0x0502)}),
            [=](const Machine&) {return bound;},[=](RoutineContext& c) {
                c.input_increment(0xBF,static_cast<std::uint16_t>(destination));
                scaffold::native_return(c,0x0502);
            });
        auto actual=started(rom,0x0500,1,[](Machine&,auto&) {});const Machine before=actual;
        Machine::PortTrace ports(actual);
        bool refused=false;try {actual.step(registry);} catch(const std::runtime_error&) {refused=true;}
        check(refused && actual.equivalent(before) && ports.accesses().empty(),"INI refuses short bound or unauthorized mapper before I/O and mutation");
    }
}

}

int main() {
    // The authored programs' code that no test renders runs on the oracle's core.
    oracle::Scaffold scaffold;
    try {
        instructions();
        ports_and_inline(); input_increment_observers();
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
