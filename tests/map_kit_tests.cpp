// SPDX-License-Identifier: GPL-3.0-or-later
// The milestone 11 map kit (src/game/map_kit.h) against the core, without a
// ROM (authored programs only):
//   1. every instruction the map kit adds, rendered on a counting bus and
//      compared with one native step from many register states (registers
//      with IX and IY, flags with their undocumented bits, MEMPTR, SP, PC,
//      work RAM, cycles and opcode fetches);
//   2. the port instructions, RST 08 inline and inline calls through the
//      registered-routine mechanism with the per-call verifier: whole-machine
//      comparison with the original program (VDP and PSG state included) and
//      every PSG write with its exact cycle, on the live bus;
//   3. the estimating bus: each segment's bound equals the cycles the live
//      call took, and an inline return that does not come back (a stack in
//      ROM) makes the estimate uncertain, so the call is refused (the run stops).
#include "game/map_kit.h"
#include "scaffold.h"
#include "probe.h"
#include "replay/verification.h"
#include <functional>
#include <iostream>
#include <random>
#include <tuple>
#include <type_traits>

using namespace psmodern;
using probe::check;
namespace mk=game::map_kit;
namespace {
// ---- 1. Instructions against one native step ------------------------------------------

struct CountingBus {
    Machine& machine;
    std::uint64_t cycles=0, fetches=0;
    void fetch(std::uint16_t) { ++fetches; }
    void indexed_bit_opcode(std::uint8_t value) { machine.cpu().data.uint8_array[3]=value; }
    std::uint8_t read(std::uint16_t address) { return machine.memory().read(address); }
    void write(std::uint16_t address, std::uint8_t value) { machine.memory().write(address,value); }
    void advance(std::uint64_t count) { cycles+=count; }
    std::uint64_t elapsed() const { return cycles; }
    void expect(std::uint16_t, OpcodePattern) {} void interrupts(bool) {} void return_from_nmi() {} bool guard(bool holds, std::string_view) { return holds; }
};
using Z=mk::Instructions<CountingBus>;
constexpr std::uint16_t code_at=0x1000;
using mk::Index;

struct KitCase {
    const char* name;
    std::function<std::vector<std::uint8_t>(std::uint8_t n)> code;   // n: an operand byte varied per state
    std::function<std::uint16_t(Z&)> render;                         // returns the next PC
};
void kit_case(const KitCase& c, unsigned states) {
    for (std::uint64_t seed=0;seed<states;++seed) {
        const auto n=static_cast<std::uint8_t>(seed*37+11);
        auto rom=std::make_shared<Memory::Rom>(512*1024,0);
        const auto code=c.code(n);
        std::copy(code.begin(),code.end(),rom->begin()+code_at);
        if (code.size()==4 && code[1]==0xCB) (*rom)[code_at+4]=0xDD;
        Machine rendered(rom);
        (void)rendered.read_port(0xbf,rendered.cycles()); rendered.advance(1);
        auto& cpu=rendered.cpu();
        probe::randomize(cpu,seed+1);
        std::mt19937_64 random(seed*977+5);
        cpu.hl.uint16_value=static_cast<std::uint16_t>(0xC000+random()%0x0F00);
        cpu.de.uint16_value=static_cast<std::uint16_t>(0xD000+random()%0x0E00);
        cpu.sp.uint16_value=static_cast<std::uint16_t>(0xC100+random()%0x1D00);
        // Index registers in work RAM away from its ends, so that every
        // displacement (-128..127) stays in RAM.
        cpu.ix_iy[0].uint16_value=static_cast<std::uint16_t>(0xC100+random()%0x1D00);
        cpu.ix_iy[1].uint16_value=static_cast<std::uint16_t>(0xC100+random()%0x1D00);
        if (seed%5==0) cpu.bc.uint16_value=1;                       // LDI to zero
        cpu.pc.uint16_value=code_at; cpu.iff1=cpu.iff2=0;
        for (std::uint16_t a=0xC000;a<0xE000;a+=3) rendered.memory().write(a,static_cast<std::uint8_t>(random()));
        oracle::NativeMachine native(rendered);   // the original: the pinned core, one instruction
        const auto cycles=native.cycles(), fetches=native.opcode_fetches();
        const auto r_before=native.cpu().r;
        // An index prefix is its own core step (resume XY) before the instruction.
        do native.step(); while (native.cpu().resume==Z80::resume_xy);
        CountingBus bus{rendered};
        auto registers=mk::load(rendered.cpu());
        Z z(bus,registers);
        const auto pc=c.render(z);
        mk::store(rendered.cpu(),registers);
        const auto& a=native.cpu(); const auto& b=rendered.cpu();
        std::string fields;
        auto compare=[&](const char* field, unsigned x, unsigned y) { if (x!=y) fields+=std::string(" ")+field; };
        compare("AF",a.af.uint16_value,b.af.uint16_value); compare("BC",a.bc.uint16_value,b.bc.uint16_value);
        compare("DE",a.de.uint16_value,b.de.uint16_value); compare("HL",a.hl.uint16_value,b.hl.uint16_value);
        compare("IX",a.ix_iy[0].uint16_value,b.ix_iy[0].uint16_value); compare("IY",a.ix_iy[1].uint16_value,b.ix_iy[1].uint16_value);
        compare("SP",a.sp.uint16_value,b.sp.uint16_value); compare("MEMPTR",a.memptr.uint16_value,b.memptr.uint16_value);
        compare("PC",a.pc.uint16_value,pc);
        // (data[3] is the core's scratch byte, not compared since milestone 14.)
        compare("cycles",static_cast<unsigned>(native.cycles()-cycles),static_cast<unsigned>(bus.cycles));
        compare("fetches",static_cast<unsigned>(native.opcode_fetches()-fetches),static_cast<unsigned>(bus.fetches));
        // R counts M1 fetches only (7 bits).
        compare("R",static_cast<unsigned>((native.cpu().r-r_before)&0x7f),static_cast<unsigned>(bus.fetches&0x7f));
        if (native.memory().ram()!=rendered.memory().ram()) fields+=" RAM";
        check(fields.empty(),std::string("map kit rendering of ")+c.name+" differs from the core (state "+std::to_string(seed)+"):"+fields);
        // Milestone 14: the core's saved indexed operation byte (data[3]) is
        // scratch the core rewrites before any later use (a kit renders both
        // prefix fetches of an instruction together), so the kits no longer
        // write it and the lockstep equality excludes it.
    }
}
std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> list) { return list; }
std::uint16_t next(unsigned length) { return static_cast<std::uint16_t>(code_at+length); }

void instructions() {
    const std::vector<KitCase> cases{
        {"nop",[](auto) { return bytes({0x00}); },[](Z& z) { z.nop(code_at); return next(1); }},
        {"xor n",[](auto n) { return bytes({0xEE,n}); },[](Z& z) { z.xor_immediate(code_at); return next(2); }},
        {"rlca",[](auto) { return bytes({0x07}); },[](Z& z) { z.rotate_left_circular_a(code_at); return next(1); }},
        {"rl b",[](auto) { return bytes({0xCB,0x10}); },[](Z& z) { z.rotate_left_through_carry(code_at,z.r.b); return next(2); }},
        {"inc (hl)",[](auto) { return bytes({0x34}); },[](Z& z) { z.increment_at_hl(code_at); return next(1); }},
        {"dec (hl)",[](auto) { return bytes({0x35}); },[](Z& z) { z.decrement_at_hl(code_at); return next(1); }},
        {"sbc hl,bc",[](auto) { return bytes({0xED,0x42}); },[](Z& z) { z.subtract_hl_with_carry(code_at,z.bc()); return next(2); }},
        {"sbc hl,de",[](auto) { return bytes({0xED,0x52}); },[](Z& z) { z.subtract_hl_with_carry(code_at,z.de()); return next(2); }},
        {"ldi",[](auto) { return bytes({0xED,0xA0}); },[](Z& z) { z.load_increment(code_at); return next(2); }},
        {"ld ix,nn",[](auto n) { return bytes({0xDD,0x21,n,0xC8}); },[](Z& z) { z.load_index(code_at,Index::ix); return next(4); }},
        {"ld iy,nn",[](auto n) { return bytes({0xFD,0x21,n,0xC8}); },[](Z& z) { z.load_index(code_at,Index::iy); return next(4); }},
        {"add ix,de",[](auto) { return bytes({0xDD,0x19}); },[](Z& z) { z.add_to_index(code_at,Index::ix,z.de()); return next(2); }},
        {"add iy,de",[](auto) { return bytes({0xFD,0x19}); },[](Z& z) { z.add_to_index(code_at,Index::iy,z.de()); return next(2); }},
        {"push ix",[](auto) { return bytes({0xDD,0xE5}); },[](Z& z) { z.push_index(code_at,Index::ix); return next(2); }},
        {"pop ix",[](auto) { return bytes({0xDD,0xE1}); },[](Z& z) { z.pop_index(code_at,Index::ix); return next(2); }},
        {"push iy",[](auto) { return bytes({0xFD,0xE5}); },[](Z& z) { z.push_index(code_at,Index::iy); return next(2); }},
        {"pop iy",[](auto) { return bytes({0xFD,0xE1}); },[](Z& z) { z.pop_index(code_at,Index::iy); return next(2); }},
        {"ld a,(iy+d)",[](auto n) { return bytes({0xFD,0x7E,n}); },[](Z& z) { z.load_from_indexed(code_at,Index::iy,z.r.a); return next(3); }},
        {"ld c,(iy+d)",[](auto n) { return bytes({0xFD,0x4E,n}); },[](Z& z) { z.load_from_indexed(code_at,Index::iy,z.r.c); return next(3); }},
        {"ld l,(iy+d)",[](auto n) { return bytes({0xFD,0x6E,n}); },[](Z& z) { z.load_from_indexed(code_at,Index::iy,z.r.l); return next(3); }},
        {"ld a,(ix+d)",[](auto n) { return bytes({0xDD,0x7E,n}); },[](Z& z) { z.load_from_indexed(code_at,Index::ix,z.r.a); return next(3); }},
        {"ld (iy+d),a",[](auto n) { return bytes({0xFD,0x77,n}); },[](Z& z) { z.store_to_indexed(code_at,Index::iy,z.r.a); return next(3); }},
        {"ld (iy+d),b",[](auto n) { return bytes({0xFD,0x70,n}); },[](Z& z) { z.store_to_indexed(code_at,Index::iy,z.r.b); return next(3); }},
        {"ld (ix+d),a",[](auto n) { return bytes({0xDD,0x77,n}); },[](Z& z) { z.store_to_indexed(code_at,Index::ix,z.r.a); return next(3); }},
        {"ld (iy+d),n",[](auto n) { return bytes({0xFD,0x36,n,static_cast<std::uint8_t>(n^0x5A)}); },
            [](Z& z) { z.store_immediate_indexed(code_at,Index::iy); return next(4); }},
        {"add a,(iy+d)",[](auto n) { return bytes({0xFD,0x86,n}); },[](Z& z) { z.add_from_indexed(code_at,Index::iy); return next(3); }},
        {"cp (iy+d)",[](auto n) { return bytes({0xFD,0xBE,n}); },[](Z& z) { z.compare_indexed(code_at,Index::iy); return next(3); }},
        {"cp (ix+d)",[](auto n) { return bytes({0xDD,0xBE,n}); },[](Z& z) { z.compare_indexed(code_at,Index::ix); return next(3); }},
        {"inc (iy+d)",[](auto n) { return bytes({0xFD,0x34,n}); },[](Z& z) { z.increment_indexed(code_at,Index::iy); return next(3); }},
        {"dec (iy+d)",[](auto n) { return bytes({0xFD,0x35,n}); },[](Z& z) { z.decrement_indexed(code_at,Index::iy); return next(3); }},
        {"bit 0,(iy+d)",[](auto n) { return bytes({0xFD,0xCB,n,0x46}); },[](Z& z) { z.test_bit_indexed(code_at,Index::iy,0); return next(4); }},
        {"bit 1,(iy+d)",[](auto n) { return bytes({0xFD,0xCB,n,0x4E}); },[](Z& z) { z.test_bit_indexed(code_at,Index::iy,1); return next(4); }},
        {"bit 7,(ix+d)",[](auto n) { return bytes({0xDD,0xCB,n,0x7E}); },[](Z& z) { z.test_bit_indexed(code_at,Index::ix,7); return next(4); }},
        {"set 0,(iy+d)",[](auto n) { return bytes({0xFD,0xCB,n,0xC6}); },[](Z& z) { z.set_bit_indexed(code_at,Index::iy,0); return next(4); }},
        {"set 1,(iy+d)",[](auto n) { return bytes({0xFD,0xCB,n,0xCE}); },[](Z& z) { z.set_bit_indexed(code_at,Index::iy,1); return next(4); }},
        // The milestone 7 instructions keep working through the extended registers.
        {"ld a,(hl)",[](auto) { return bytes({0x7E}); },[](Z& z) { z.load_from_hl(code_at,z.r.a); return next(1); }},
        {"push bc",[](auto) { return bytes({0xC5}); },[](Z& z) { z.push(code_at,z.r.b,z.r.c); return next(1); }},
        {"ld de,(nn)",[](auto n) { return bytes({0xED,0x5B,n,0xC5}); },[](Z& z) { z.load_pair_direct(code_at,z.r.d,z.r.e); return next(4); }},
        {"call nn",[](auto) { return bytes({0xCD,0x45,0x23}); },[](Z& z) { return z.call(code_at); }},
        {"call nz,nn",[](auto n) { return bytes({0xC4,n,0x23}); },[](Z& z) {
            return z.call_if(code_at,!z.zero()) ? z.r.wz : next(3);
        }},
        {"ret p",[](auto) { return bytes({0xF0}); },[](Z& z) { return z.return_if(code_at,!z.sign()) ? z.r.wz : next(1); }},
        {"jp m,nn",[](auto n) { return bytes({0xFA,n,0x23}); },[](Z& z) { return z.jump_if(code_at,z.sign()) ? z.r.wz : next(3); }},
    };
    for (const auto& c:cases) kit_case(c,400);
    std::cout<<cases.size()<<" map kit renderings match the core over 400 states each.\n";
}

// ---- 2 and 3. Port output, RST 08 and inline calls through the mechanism ----------------

// Authored image: the VDP address helper at 00:0008 writing `helper_port`
// (the PSG port 7Fh so that every write is logged with its cycle, or the VDP
// control port), and the parts given.
std::shared_ptr<const Memory::Rom> authored(std::uint8_t helper_port,
        std::initializer_list<std::pair<std::uint16_t,std::vector<std::uint8_t>>> parts) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    const std::vector<std::uint8_t> helper{0x7B,0xD3,helper_port,0x7A,0xD3,helper_port,0xC9};
    std::copy(helper.begin(),helper.end(),rom->begin()+mk::vdp_address_helper);
    for (const auto& [at,code]:parts) std::copy(code.begin(),code.end(),rom->begin()+at);
    // Slot 1 keeps bank 1 of this image: fill it so a ROM stack reads non-zero words.
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
    arrange(m,random);
    return m;
}
template<class M> void until_return(M& m, RoutineRegistry* registry, std::vector<std::uint64_t>* bounds=nullptr) {
    for (unsigned i=0;i<1000000 && m.cpu().pc.uint16_value!=0x0180;++i) {
        if constexpr (!std::is_same_v<M,Machine>) { (void)registry; (void)bounds; (void)m.step(); continue; }
        else {
        if (!registry) { (void)m.step(); continue; }
        // The estimate of every C++ call equals the cycles the call takes.
        const auto* routine=registry->find({0,m.cpu().pc.uint16_value});
        const auto before=routine ? routine->counters.cpp_calls : 0;
        const auto bound=routine ? routine->bound(m) : 0;
        const auto elapsed=m.step(*registry);
        if (routine && routine->counters.cpp_calls==before+1) {
            check(bound==elapsed,"estimated bound "+std::to_string(bound)+" differs from the live call's "+std::to_string(elapsed)+" cycles");
            if (bounds) bounds->push_back(bound);
        }
        }
    }
    check(m.cpu().pc.uint16_value==0x0180,"authored program returned");
}
struct Calls { std::uint64_t cpp=0, fallbacks=0; };
// Runs the program with the registry (every call verified) and without, and
// requires equal machines and equal PSG write logs (byte and cycle).
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
    check(native.take_psg_writes()==cpp.take_psg_writes(),what+": PSG writes differ in value or cycle");
    check(native.last_access()==cpp.last_access(),what+": last port access differs");
    Calls calls;
    for (const auto& [entry,routine]:registry.entries()) { calls.cpp+=routine.counters.cpp_calls; calls.fallbacks+=routine.counters.fallback_calls; }
    return calls;
}

void ports_and_inline() {
    using namespace mk;
    // A: out (7Fh),a; three OUTI to port C; RST 08 (helper to 7Fh); nop; ret.
    const auto a=authored(0x7F,{{0x0100,{0xD3,0x7F,0xED,0xA3,0xED,0xA3,0xED,0xA3,0xCF,0x00,0xC9}}});
    // B: a counted OUTI loop with a conditional loop exit on its JP NZ:
    //   0200 ld c,7Fh; 0202 ld b,40h; 0204 outi; 0206 jp nz,0204; 0209 ret
    const auto b=authored(0x7F,{{0x0200,{0x0E,0x7F,0x06,0x40,0xED,0xA3,0xC2,0x04,0x02,0xC9}}});
    // C: an inline call into an unrolled tail: 0300 call 0310; 0303 ret;
    //    0310 outi x4; 0318 ret.
    const auto c=authored(0x7F,{{0x0300,{0xCD,0x10,0x03,0xC9}},{0x0310,{0xED,0xA3,0xED,0xA3,0xED,0xA3,0xED,0xA3,0xC9}}});
    // D: the VDP: RST 08 with the real control port, then OUTI to the data port.
    //    0400 rst 08; 0401 ld c,BEh; 0403 outi x2; 0407 ret
    const auto d=authored(0xBF,{{0x0400,{0xCF,0x0E,0xBE,0xED,0xA3,0xED,0xA3,0xC9}}});

    auto program_a=[](auto& z) {
        z.output(0x0100);
        z.output_increment_run(0x0102,3);
        (void)z.restart_vdp_address(0x0108);
        z.nop(0x0109);
        return scaffold::returns(0x010A,return_cycles);
    };
    constexpr std::uint16_t after=200;
    auto loop=[](auto& z, std::uint16_t at) {
        for (;;) {
            z.output_increment(at);
            if (z.loop_exit(after)) return scaffold::continues(0x0206,jump_cycles);
            if (!z.jump_if(0x0206,!z.zero())) return scaffold::returns(0x0209,return_cycles);
            at=0x0204;
        }
    };
    auto program_c=[](auto& z) {
        (void)z.inline_call(0x0300,0x0318,[](auto& callee) { callee.output_increment_run(0x0310,4); });
        return scaffold::returns(0x0303,return_cycles);
    };
    auto program_d=[](auto& z) {
        (void)z.restart_vdp_address(0x0400);
        z.load_immediate(0x0401,z.r.c);
        z.output_increment_run(0x0403,2);
        return scaffold::returns(0x0407,return_cycles);
    };
    for (std::uint64_t seed=1;seed<=60;++seed) {
        RoutineRegistry ra;
        add_segment(ra,{0,0x0100},segment({0,0x0100},{returned_at(0x010A)},{.scratch_stack=2}),program_a);
        const auto ca=same_as_native(started(a,0x0100,seed,[](Machine& m, std::mt19937_64& random) {
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xC800+random()%0x0400);
            m.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(random());   // any B, wrapping included
            m.cpu().bc.uint8_values.at_0=0x7F;
        }),ra,"out, outi and RST 08 (seed "+std::to_string(seed)+")");
        check(ca.cpp==1 && ca.fallbacks==0,"program A ran in C++");

        RoutineRegistry rb;
        add_segment(rb,{0,0x0200},segment({0,0x0200},{conditional_loop_at(0x0206,after),returned_at(0x0209)}),
            [loop](auto& z) { z.load_immediate(0x0200,z.r.c); z.load_immediate(0x0202,z.r.b); return loop(z,0x0204); });
        add_segment(rb,{0,0x0204},segment({0,0x0200},{conditional_loop_at(0x0206,after),returned_at(0x0209)}),
            [loop](auto& z) { return loop(z,0x0204); });
        const auto cb=same_as_native(started(b,0x0200,seed,[](Machine& m, std::mt19937_64& random) {
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xC800+random()%0x0400);
        }),rb,"OUTI loop with a conditional loop exit (seed "+std::to_string(seed)+")");
        // 64 OUTI of 26 cycles: the first chunk includes 14 cycles of setup.
        check(cb.cpp>=8 && cb.fallbacks==0,"program B ran in C++ chunks");

        RoutineRegistry rc;
        add_segment(rc,{0,0x0300},segment({0,0x0300},{returned_at(0x0303)},{.scratch_stack=2}),program_c);
        const auto cc=same_as_native(started(c,0x0300,seed,[](Machine& m, std::mt19937_64& random) {
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xC800+random()%0x0400);
            m.cpu().bc.uint8_values.at_0=0x7F;
        }),rc,"inline call into an unrolled tail (seed "+std::to_string(seed)+")");
        check(cc.cpp==1 && cc.fallbacks==0,"program C ran in C++");

        RoutineRegistry rd;
        add_segment(rd,{0,0x0400},segment({0,0x0400},{returned_at(0x0407)},{.scratch_stack=2}),program_d);
        const auto cd=same_as_native(started(d,0x0400,seed,[](Machine& m, std::mt19937_64& random) {
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xC800+random()%0x0400);
            // A VRAM write address (01 in the top bits of D).
            m.cpu().de.uint16_value=static_cast<std::uint16_t>(0x4000|(random()&0x3FFF));
        }),rd,"RST 08 to the VDP and OUTI to VRAM (seed "+std::to_string(seed)+")");
        check(cd.cpp==1 && cd.fallbacks==0,"program D ran in C++");
    }

    // A stack in ROM: the RST pushes nothing and the helper returns to the
    // word it reads there. The estimate becomes uncertain, the bound is the
    // maximum (two frames), and dispatch refuses the call: the run stops
    // (milestone 14: there is no core to run the original), unchanged.
    {
        RoutineRegistry ra;
        add_segment(ra,{0,0x0100},segment({0,0x0100},{returned_at(0x010A)},{.scratch_stack=2}),program_a);
        Machine m=started(a,0x0100,3,[](Machine& machine, std::mt19937_64&) {
            machine.cpu().sp.uint16_value=0x7000;
            machine.cpu().bc.uint8_values.at_0=0x7F;
        });
        check(ra.find({0,0x0100})->bound(m)==uncertain_maximum,"a ROM stack makes the RST 08 estimate uncertain");
        const auto before=m.cycles();
        bool stopped=false;
        try { m.step(ra); } catch (const UnhandledState&) { stopped=true; }
        const auto& counters=ra.find({0,0x0100})->counters;
        check(stopped && counters.cpp_calls==0 && counters.fallback_calls==1 && counters.fallback(RoutineFallback::frame)==1,
              "a ROM stack refuses the call (reason frame) and stops the run");
        check(m.cycles()==before && m.cpu().pc.uint16_value==0x0100,"the refusal runs nothing");
    }
    // Milestone 14 lifted the VDP guard for exact bodies: a body whose RST 08
    // helper writes register 1 through the control port (the second byte 81h
    // selects it) runs in C++ and matches the original at every boundary.
    {
        RoutineRegistry rd;
        add_segment(rd,{0,0x0400},segment({0,0x0400},{returned_at(0x0407)},{.scratch_stack=2}),program_d);
        Machine m=started(d,0x0400,4,[](Machine& machine, std::mt19937_64&) { machine.cpu().de.uint16_value=0x8160; });
        oracle::Lockstep lockstep(m,"register 1 write",oracle::Mode::instructions);
        lockstep.step(m,&rd);
        check(rd.find({0,0x0400})->counters.cpp_calls==1,"a register 1 write from a rendered body runs in C++");
    }
    std::cout<<"Port output, RST 08 and inline calls match the original through the mechanism; bounds are exact.\n";
}
void restart_write_observers() {
    for(unsigned variant=0;variant<4;++variant) {
    const unsigned duration=variant==0 ? 100 : variant==3 ? 20 : 67;
    const std::uint16_t entry=variant==0 ? 0x0400 : variant==1 ? 0x0500 : 0x0600;
    const auto rom=authored(0xBF,{{0x0400,{0xCF,0x0E,0xBE,0xED,0xA3,0xED,0xA3,0xC9}},
        {0x0038,{0xF5,0xDB,0xBF,0xF1,0xFB,0xED,0x4D}},{0x0066,{0xED,0x45}},
        {0x0500,{0xCD,0x08,0x00,0xC9}},{0x0600,{0xC4,0x08,0x00,0xC9}}});
    const auto logic=[=](auto& z) {
        if(variant==0) {
            z.restart_vdp_address(0x0400);
            z.load_immediate(0x0401,z.r.c);
            z.output_increment_run(0x0403,2);
            return scaffold::returns(0x0407,mk::return_cycles);
        }
        const bool called=variant==1 ? (z.call(entry),true) : z.call_if(entry,!z.zero());
        if(called) {
            z.copy(0x0008,z.r.a,z.r.e);
            z.output(0x0009);
            z.copy(0x000B,z.r.a,z.r.d);
            z.output(0x000C);
            check(z.return_from(0x000E)==entry+3,"inline CALL returns to its exact continuation");
        }
        return scaffold::returns(static_cast<std::uint16_t>(entry+3),mk::return_cycles);
    };
    for(unsigned sp:{0xC100u,0xDF00u,0xE100u,0x8100u})
    for(unsigned event=0;event<3;++event)
    for(unsigned distance:{0u,1u,duration-1,duration,duration+1}) {
        if(event==0 && distance==0) continue;
        RoutineRegistry registry;
        mk::add_segment(registry,{0,entry},mk::segment({0,entry},{mk::returned_at(static_cast<std::uint16_t>(variant==0 ? 0x0407 : entry+3))}, {.scratch_stack=2}),logic);
        auto actual=started(rom,entry,sp+distance,[&](Machine& m,auto&) {
            if(sp==0x8100) m.memory().write(0xFFFC,8);
            m.cpu().sp.uint16_value=static_cast<std::uint16_t>(sp);
            m.memory().write(static_cast<std::uint16_t>(sp),0x80);
            m.memory().write(static_cast<std::uint16_t>(sp+1),1);
            m.cpu().hl.uint16_value=0xC000;
            m.cpu().de.uint16_value=0x4000;
            m.cpu().bc.uint16_value=0x027F;
            if(variant>=2) {
                if(variant==3) m.cpu().af.uint8_values.at_0|=mk::zero_flag;
                else m.cpu().af.uint8_values.at_0&=static_cast<std::uint8_t>(~mk::zero_flag);
            }
        });
        if(event==0) {
            const auto phase=actual.cycles()%Machine::frame_cycles;
            actual.advance((Machine::frame_cycles-distance+Machine::frame_cycles-phase)%Machine::frame_cycles);
        } else if(event==1) {
            actual.write_port(0xBF,0x20,actual.cycles());
            actual.write_port(0xBF,0x81,actual.cycles());
            const auto irq=actual.vdp().next_irq();
            check(irq && *irq>=actual.cycles()+distance,"RST observer fixture has a future frame IRQ");
            actual.advance(*irq-actual.cycles()-distance);
            actual.cpu().iff1=actual.cpu().iff2=1;
        } else actual.schedule_nmi(actual.cycles()+distance);
        Machine native=actual;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
        std::vector<Write> expected,observed;
        std::vector<std::uint64_t> expected_starts,actual_starts;
        struct Stopped {};
        auto run=[=](Machine& m,RoutineRegistry* r,std::vector<Write>& writes,std::vector<std::uint64_t>& starts) {
            Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) {
                writes.emplace_back(address,value,m.cycles());
            };
            Machine::Hook stop=[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
                if(pc==entry) starts.push_back(m.cycles());
                if(pc==0x0180) throw Stopped{};
                return {};
            };
            try { for(unsigned frame=0;frame<4;++frame) m.run_frame({},r,stop,observer); }
            catch(const Stopped&) { return; }
            throw std::runtime_error("RST observer fixture did not return");
        };
        run(native,nullptr,expected,expected_starts);
        run(actual,&registry,observed,actual_starts);
        check(expected_starts.size()==1,"native RST/CALL instruction entry observed exactly once");
        check(actual.equivalent(native),"RST callback correction keeps CPU, VDP, PSG and event state");
        check(expected==observed,"RST stack writes have identical addresses, values, order and instruction cycles");
        check(expected_ports.accesses()==actual_ports.accesses(),"RST callback correction keeps every port offset");
        bool found=false;
        for(std::size_t i=1;i<observed.size();++i) {
            const auto& high=observed[i-1]; const auto& low=observed[i];
            if(std::get<0>(high)==sp-1 && std::get<1>(high)==(entry>>8) &&
               std::get<0>(low)==sp-2 && std::get<1>(low)==(variant==0 ? 1 : 3) &&
               std::get<2>(high)==expected_starts.front()) {
                check(std::get<2>(high)==std::get<2>(low),"both RST stack bytes share the native callback cycle");
                found=true;
            }
        }
        check(found==(variant!=3),"RST/CALL return pair: variant="+std::to_string(variant)+" sp="+std::to_string(sp)+" event="+std::to_string(event)+" distance="+std::to_string(distance));
    }
    }
}

void index_push_write_observers() {
    for(const auto which:{Index::ix,Index::iy})
    for(unsigned sp:{0xC100u,0xDF00u,0xE100u,0x8100u})
    for(unsigned remaining:{1u,38u,39u,40u,1000u}) {
        const auto prefix=static_cast<std::uint8_t>(which==Index::ix ? 0xDD : 0xFD);
        const auto rom=authored(0xBF,{{0x0500,{prefix,0xE5,prefix,0xE1,0xC9}}});
        RoutineRegistry registry;
        mk::add_segment(registry,{0,0x0500},mk::segment({0,0x0500},{mk::returned_at(0x0504)},
            {.scratch_stack=2}),[=](auto& z) {
                z.push_index(0x0500,which);
                z.pop_index(0x0502,which);
                return scaffold::returns(0x0504,mk::return_cycles);
            });
        auto actual=started(rom,0x0500,sp+remaining,[&](Machine& m,auto&) {
            if(sp==0x8100) m.memory().write(0xFFFC,8);
            m.cpu().sp.uint16_value=static_cast<std::uint16_t>(sp);
            m.memory().write(static_cast<std::uint16_t>(sp),0x80);
            m.memory().write(static_cast<std::uint16_t>(sp+1),1);
            m.cpu().ix_iy[0].uint16_value=0xA15C;
            m.cpu().ix_iy[1].uint16_value=0xA15C;
        });
        const auto phase=actual.cycles()%Machine::frame_cycles;
        actual.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
        Machine native=actual;
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
        std::vector<Write> expected,observed;
        std::uint64_t start=0;
        struct Stopped {};
        auto run=[&](Machine& m,RoutineRegistry* r,std::vector<Write>& writes) {
            Machine::WriteObserver observer=[&](std::uint16_t a,std::uint8_t v) {
                writes.emplace_back(a,v,m.cycles());
            };
            Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
                if(pc==0x0500) start=m.cycles();
                if(pc==0x0180) throw Stopped{};
                return {};
            };
            try {for(unsigned frame=0;frame<3;++frame) m.run_frame({},r,hook,observer);}
            catch(const Stopped&) {return;}
            throw std::runtime_error("index PUSH fixture did not recover its stack and return");
        };
        run(native,nullptr,expected);
        const auto native_start=start;
        run(actual,&registry,observed);
        check(actual.equivalent(native),"index PUSH/POP preserves complete state and stack recovery");
        check(expected==observed && observed.size()==2,"index PUSH preserves every write callback");
        check(observed[0]==Write{static_cast<std::uint16_t>(sp-1),0xA1,native_start+4} &&
              observed[1]==Write{static_cast<std::uint16_t>(sp-2),0x5C,native_start+4},
              "IX/IY writes high then low exactly four cycles after instruction entry");
    }
}

void indexed_write_observers() {
    struct RecordingBus {
        Machine& machine;
        std::uint64_t cycles=0,fetches=0;
        std::vector<std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>> writes;
        void fetch(std::uint16_t) {++fetches;}
        void indexed_bit_opcode(std::uint8_t value) {machine.cpu().data.uint8_array[3]=value;}
        std::uint8_t read(std::uint16_t a) {return machine.peek(a);}
        void write(std::uint16_t a,std::uint8_t v) {writes.emplace_back(a,v,cycles);machine.memory().write(a,v);}
        void advance(std::uint64_t n) {cycles+=n;}
        std::uint64_t elapsed() const {return cycles;}
        void expect(std::uint16_t,OpcodePattern) {}
    };
    unsigned differences=0;
    for(unsigned mode=0;mode<9;++mode)
    for(const auto which:{Index::ix,Index::iy}) {
        const auto prefix=static_cast<std::uint8_t>(which==Index::ix ? 0xDD : 0xFD);
        const auto code=mode==0 ? std::vector<std::uint8_t>{prefix,0x77,3} :
            mode==1 ? std::vector<std::uint8_t>{prefix,0x36,3,0xA5} :
            mode==2 ? std::vector<std::uint8_t>{prefix,0x34,3} :
            mode==3 ? std::vector<std::uint8_t>{prefix,0x35,3} :
                      mode==4 ? std::vector<std::uint8_t>{prefix,0xCB,3,0xDE} :
            mode==5 ? std::vector<std::uint8_t>{0xED,0xA0} :
            mode==6 ? std::vector<std::uint8_t>{0xCB,0xDE} :
            mode==7 ? std::vector<std::uint8_t>{0xED,0xB0} :
                      std::vector<std::uint8_t>{0xED,0x43,3,0xC1};
        auto rom=std::make_shared<Memory::Rom>(512*1024,0);
        std::copy(code.begin(),code.end(),rom->begin()+0x0500);
        Machine native(rom);
        native.cpu().pc.uint16_value=0x0500;
        native.cpu().iff1=native.cpu().iff2=0;
        native.cpu().ix_iy[0].uint16_value=native.cpu().ix_iy[1].uint16_value=0xC100;
        native.cpu().af.uint16_value=0x5AFF;
        native.cpu().hl.uint16_value=0xC103; native.cpu().de.uint16_value=0xC200;
        native.cpu().bc.uint16_value=1;
        native.memory().write(0xC103,0x7F);
        Machine rendered=native;
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
        std::vector<Write> expected;
        struct Stopped {};
        Machine::WriteObserver observer=[&](std::uint16_t a,std::uint8_t v) {expected.emplace_back(a,v,native.cycles());};
        Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
            if(pc==0x0500+code.size()) throw Stopped{};
            return {};
        };
        try {native.run_frame({},nullptr,hook,observer);} catch(const Stopped&) {}
        RecordingBus bus{rendered,0,0,{}};
        auto registers=mk::load(rendered.cpu());
        mk::Instructions<RecordingBus> z(bus,registers);
        if(mode==0) z.store_to_indexed(0x0500,which,z.r.a);
        if(mode==1) z.store_immediate_indexed(0x0500,which);
        if(mode==2) z.increment_indexed(0x0500,which);
        if(mode==3) z.decrement_indexed(0x0500,which);
        if(mode==4) z.set_bit_indexed(0x0500,which,3);
        if(mode==5) z.load_increment(0x0500);
        if(mode==6) z.set_bit_at_hl(0x0500,3);
        if(mode==7) z.block_copy_byte(0x0500);
        if(mode==8) z.store_pair_direct(0x0500,z.r.b,z.r.c);
        if(expected!=bus.writes) {
            ++differences;
            std::cerr<<"indexed callback mode="<<mode<<" prefix="<<unsigned(prefix)
                <<" native="<<std::get<2>(expected.front())<<" cpp="<<std::get<2>(bus.writes.front())<<'\n';
        }
        check(expected.size()==(mode==8 ? 2u : 1u) && native.cycles()==bus.cycles,"indexed write preserves total instruction duration");
        check(native.memory().ram()==rendered.memory().ram(),"indexed write value matches native");
    }
    check(differences==0,"every indexed write has the original callback time");
}

}

int main() {
    // The authored programs' code that no test renders runs on the oracle's core.
    oracle::Scaffold scaffold;
    try {
        instructions();
        ports_and_inline(); restart_write_observers(); index_push_write_observers(); indexed_write_observers();
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
