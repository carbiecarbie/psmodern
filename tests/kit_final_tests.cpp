// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 14 Part 0b: the kits' final renderers against the oracle, on
// authored programs (no ROM). Each case is a two-instruction exact segment at
// 00:0100: a NOP, then the rendered final (the NOP puts an instruction
// boundary before the final, compared in strict mode). The segment runs under
// strict lockstep with the oracle (the pinned core) for every condition code,
// taken and not taken, with the flags, B, Q and the IFFs in every relevant
// state, so registers, Q, MEMPTR, R, the IFFs and the arbitration bits, the
// cycles, the stack writes and the port accesses are compared at every
// boundary, the one after the final included.
//   transfers   JP, JP cc, JR, JR cc, DJNZ, JP (HL), CALL, CALL cc, RST 08
//   returns     RET, RET cc (taken and continuing), RETN
//   interrupts  EI and DI, with the frame interrupt pending at the boundary
//   plain       LD SP,nn, EXX, EX AF,AF', CCF and SCF with Q in every state
//   slices      LDIR and OTIR slices, the OUTI run
//   check       the opcode check ran and refuses a wrong declared pattern
#include "oracle/native_machine.h"
#include "game/map_kit.h"
#include "machine/machine.h"
#include "oracle/lockstep.h"
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace psmodern;
namespace k=psmodern::game::kit;
namespace mk=psmodern::game::map_kit;

namespace {
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }

constexpr std::uint16_t entry=0x0100, final_at=0x0101, stack_top=0xC1F0, target=0x0200, caller=0x0300;
using Logic=std::function<k::Exit(mk::Instructions<mk::Live>&)>;
using Estimate=std::function<k::Exit(mk::Instructions<mk::Estimate>&)>;

// A program: NOP at 0100 then `code` at 0101; JR $ at the jump target, at the
// caller (the return address on the stack) and after the final.
std::shared_ptr<Memory::Rom> program(std::initializer_list<std::uint8_t> code, bool at_entry=false) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    (*rom)[entry]=0x00;
    (*rom)[0x0038]=0xDB; (*rom)[0x0039]=0xBF; (*rom)[0x003A]=0xFB; (*rom)[0x003B]=0xC9; // the frame interrupt: IN A,(BF); EI; RET
    (*rom)[0x0066]=0xED; (*rom)[0x0067]=0x45;                                           // the NMI: RETN
    std::uint16_t at=at_entry ? entry : final_at;
    for (auto byte:code) (*rom)[at++]=byte;
    for (std::uint16_t loop:{target,caller,static_cast<std::uint16_t>(0x0008)}) { (*rom)[loop]=0x18; (*rom)[loop+1]=0xFE; }
    for (std::uint16_t i=0;i<0x20;++i) (*rom)[0x0400+i]=static_cast<std::uint8_t>(0x11*i+3); // data for the slices
    return rom;
}
struct State {
    std::uint8_t f=0, q=0, a=0x5A, b=1, c=0xBE;
    std::uint16_t hl=target, de=0xC100, sp=stack_top;
    bool iff=true;
    std::uint16_t af_=0x1234, bc_=0x5678, de_=0x9ABC, hl_=0xDEF0;
};
Machine start(std::shared_ptr<Memory::Rom> rom, const State& s) {
    Machine m(rom);
    (void)m.read_port(0xBF,m.cycles()); m.advance(1);
    auto& cpu=m.cpu();
    cpu.im=1; cpu.iff1=cpu.iff2=s.iff; cpu.pc.uint16_value=entry; cpu.sp.uint16_value=s.sp;
    cpu.af.uint8_values.at_1=s.a; cpu.af.uint8_values.at_0=s.f; cpu.q=s.q;
    cpu.bc.uint8_values.at_1=s.b; cpu.bc.uint8_values.at_0=s.c;
    cpu.hl.uint16_value=s.hl; cpu.de.uint16_value=s.de;
    cpu.af_.uint16_value=s.af_; cpu.bc_.uint16_value=s.bc_; cpu.de_.uint16_value=s.de_; cpu.hl_.uint16_value=s.hl_;
    m.memory().write(s.sp,caller&0xff); m.memory().write(static_cast<std::uint16_t>(s.sp+1),caller>>8);
    return m;
}
unsigned cases_run=0;
// Runs the segment once under strict lockstep (the whole machines compared
// at the boundary after it) and checks that it ended at the declared exit
// with its final rendered.
template<class L> void run(std::initializer_list<std::uint8_t> code, std::vector<SegmentExit> exits, const State& s, L logic,
                           const std::function<void(Machine&)>& adjust = {}, bool at_entry=false) {
    RoutineRegistry registry;
    mk::add_segment(registry,{0,entry},mk::segment({0,entry},exits),logic);
    auto m=start(program(code,at_entry),s);
    if (adjust) adjust(m);
    oracle::Lockstep lockstep(m,"kit final",oracle::Mode::instructions);
    lockstep.step(m,&registry);
    const auto& routine=*registry.find({0,entry});
    std::uint64_t rendered=0;
    for (auto n:routine.rendered) rendered+=n;
    check(routine.counters.cpp_calls==1 && rendered==1,"the final was not rendered by the body");
    ++cases_run;
}
constexpr k::Condition conditions[]{k::Condition::nz,k::Condition::z,k::Condition::nc,k::Condition::c,
                                    k::Condition::po,k::Condition::pe,k::Condition::p,k::Condition::m};
// Flags in every state a condition reads: none, all, and each tested flag.
constexpr std::uint8_t flag_states[]{0x00,0xFF,0x40,0x01,0x04,0x80,0xBE};
std::uint8_t code_of(std::uint8_t base, k::Condition cc) { return static_cast<std::uint8_t>(base|(static_cast<unsigned>(cc)<<3)); }

void transfers() {
    for (auto f:flag_states) {
        State s; s.f=f; s.q=static_cast<std::uint8_t>(f^0x28);
        run({0xC3,target&0xff,target>>8},{k::core_final_at(final_at)},s,[](auto& z) { z.nop(entry); return z.final_jump(final_at); });
        run({0x18,static_cast<std::uint8_t>(target-final_at-2)},{k::core_final_at(final_at)},s,
            [](auto& z) { z.nop(entry); return z.final_relative(final_at); });
        run({0xE9},{k::core_final_at(final_at)},s,[](auto& z) { z.nop(entry); return z.final_jump_hl(final_at); });
        run({0xCD,target&0xff,target>>8},{k::core_final_at(final_at)},s,[](auto& z) { z.nop(entry); return z.final_call(final_at); });
        run({0xCF},{k::core_final_at(final_at)},s,[](auto& z) { z.nop(entry); return z.final_restart(final_at,0x08); });
        for (auto cc:conditions) {
            run({code_of(0xC2,cc),target&0xff,target>>8},{k::conditional_at(final_at)},s,
                [cc](auto& z) { z.nop(entry); return z.final_jump_if(final_at,cc); });
            run({code_of(0xC4,cc),target&0xff,target>>8},{k::core_final_at(final_at)},s,
                [cc](auto& z) { z.nop(entry); return z.final_call_if(final_at,cc); });
            if (static_cast<unsigned>(cc)<4)
                run({code_of(0x20,cc),static_cast<std::uint8_t>(target-final_at-2)},{k::conditional_at(final_at)},s,
                    [cc](auto& z) { z.nop(entry); return z.final_relative_if(final_at,cc); });
        }
    }
    for (std::uint8_t b:{0,1,2,0x80}) {
        State s; s.b=b;
        run({0x10,static_cast<std::uint8_t>(target-final_at-2)},{k::conditional_at(final_at)},s,
            [](auto& z) { z.nop(entry); return z.final_decrement_jump(final_at); });
    }
}
void returns() {
    for (auto f:flag_states) {
        State s; s.f=f; s.q=f;
        run({0xC9},{k::returned_at(final_at)},s,[](auto& z) { z.nop(entry); return z.final_return(final_at); });
        for (auto cc:conditions)
            // ret cc, then (not taken) an XOR A ending the segment at 0103.
            run({code_of(0xC0,cc),0xAF},{k::returned_at(final_at),k::core_final_at(final_at+1)},s,[cc](auto& z) {
                z.nop(entry);
                if (auto exit=z.final_return_if(final_at,cc)) return *exit;
                z.xor_a(final_at+1);
                return k::fall_through(final_at+1,final_at+2);
            });
    }
    for (int iff1=0;iff1<2;++iff1) for (int iff2=0;iff2<2;++iff2) {
        State s;
        run({0xED,0x45},{k::core_final_at(final_at)},s,[](auto& z) { z.nop(entry); return z.final_return_from_nmi(final_at); },
            [&](Machine& m) { m.cpu().iff1=static_cast<std::uint8_t>(iff1); m.cpu().iff2=static_cast<std::uint8_t>(iff2); });
    }
}
// EI and DI as finals, with the frame interrupt rising at every cycle around
// the final: after EI the next instruction runs before it is accepted.
void interrupts() {
    for (int enable=0;enable<2;++enable)
        for (std::uint64_t offset=0;offset<16;++offset) {
            State s; s.iff=!enable;
            const auto code=static_cast<std::uint8_t>(enable ? 0xFB : 0xF3);
            run({code},{k::core_final_at(final_at)},s,[enable](auto& z) {
                z.nop(entry);
                return enable ? z.final_enable_interrupts(final_at) : z.final_disable_interrupts(final_at);
            },[offset](Machine& m) {
                const auto irq=m.next_irq();
                check(irq && *irq>m.cycles()+offset,"frame interrupt not ahead");
                m.advance(*irq-m.cycles()-offset);
            });
        }
}
void plain() {
    run({0x31,0x00,0xCB},{k::core_final_at(final_at)},State{},[](auto& z) {
        z.nop(entry); z.load_stack_pointer(final_at); return k::fall_through(final_at,final_at+3);
    });
    run({0xD9},{k::core_final_at(final_at)},State{},[](auto& z) {
        z.nop(entry); z.exchange_alternates(final_at); return k::fall_through(final_at,final_at+1);
    });
    run({0x08},{k::core_final_at(final_at)},State{},[](auto& z) {
        z.nop(entry); z.exchange_af(final_at); return k::fall_through(final_at,final_at+1);
    });
    // CCF and SCF read the previous instruction's Q: after the NOP (Q = 0)
    // and after an OR A (Q = F), with F and A in several states.
    for (auto f:flag_states) for (std::uint8_t a:{0x00,0x28,0xD7,0xFF}) {
        State s; s.f=f; s.a=a; s.q=0xFF;
        run({0x3F},{k::core_final_at(final_at)},s,[](auto& z) {
            z.nop(entry); z.complement_carry(final_at); return k::fall_through(final_at,final_at+1);
        });
        run({0x37},{k::core_final_at(final_at)},s,[](auto& z) {
            z.nop(entry); z.set_carry(final_at); return k::fall_through(final_at,final_at+1);
        });
        RoutineRegistry unused;
        auto rom=program({0x3F});
        (*rom)[entry]=0xB7; // OR A, then CCF
        RoutineRegistry registry;
        mk::add_segment(registry,{0,entry},mk::segment({0,entry},{k::core_final_at(final_at)}),[](auto& z) {
            z.or_a(entry); z.complement_carry(final_at); return k::fall_through(final_at,final_at+1);
        });
        auto m=start(rom,s);
        oracle::Lockstep lockstep(m,"ccf after or",oracle::Mode::instructions);
        lockstep.step(m,&registry);
        ++cases_run;
    }
    // Review R1 M-1: CCF and SCF after a CP, with an NMI or an IRQ raised at
    // every cycle of the body: an interrupt accepted at the boundary before
    // them changes the previous instruction's Q (the handler's last one).
    for (const std::uint8_t op:{0x3F,0x37}) for (int kind=0;kind<2;++kind) for (std::uint64_t k=1;k<=30;++k) {
        State s; s.f=0; s.q=0; s.a=0x77; s.iff=kind==0;
        run({0x3E,0x00,0xFE,0x28,op,0xC9},{k::returned_at(entry+5)},s,[op](auto& z) {
            z.load_immediate(entry,z.r.a); z.compare_immediate(entry+2);
            if (op==0x3F) z.complement_carry(entry+4); else z.set_carry(entry+4);
            return z.final_return(entry+5);
        },[kind,k](Machine& m) {
            if (kind==1) { m.schedule_nmi(m.cycles()+k); return; }
            const auto irq=m.next_irq();
            check(irq && *irq>m.cycles()+k,"frame interrupt not ahead");
            m.advance(*irq-m.cycles()-k);
        },true);
    }
}
void slices() {
    for (std::uint16_t count:{1,2,5,16,17,40}) {
        State s; s.hl=0x0400; s.de=0xC100;
        run({0xED,0xB0},{SegmentExit{RoutineExit::copy_repeat,entry}},s,[](auto& z) {
            return z.final_block_copy(entry);
        },[count](Machine& m) { m.cpu().bc.uint16_value=count; },true);
    }
    for (std::uint8_t count:{1,2,3,16,17,30}) {
        State s; s.hl=0x0400; s.b=count; s.c=0xBE;
        run({0xED,0xB3},{SegmentExit{RoutineExit::output_repeat,entry}},s,[](auto& z) {
            return z.final_output_repeat(entry);
        },{},true);
    }
    State s; s.hl=0x0400; s.b=8; s.c=0xBE;
    run({0xED,0xA3,0xED,0xA3,0xED,0xA3},{k::core_final_at(final_at+4)},s,[](auto& z) {
        z.nop(entry); z.output_increment_run(final_at,3); return k::fall_through(final_at+4,final_at+6);
    });
}
// The opcode check ran for the renderers above, and a renderer declaring the
// wrong condition is refused under lockstep.
void opcode_check() {
    const auto before=oracle::opcode_checks();
    State s;
    run({0xC2,target&0xff,target>>8},{k::conditional_at(final_at)},s,
        [](auto& z) { z.nop(entry); return z.final_jump_if(final_at,k::Condition::nz); });
    check(oracle::opcode_checks()>before,"the opcode check ran");
    bool refused=false;
    try {
        run({0xC2,target&0xff,target>>8},{k::conditional_at(final_at)},s,
            [](auto& z) { z.nop(entry); return z.final_jump_if(final_at,k::Condition::z); });
    } catch (const std::runtime_error& e) { refused=std::string_view(e.what()).find("Opcode check")!=std::string_view::npos; }
    check(refused,"a wrong declared condition is refused by the opcode check");
    // Without lockstep (the program), nothing checks: the same body runs.
    RoutineRegistry registry;
    mk::add_segment(registry,{0,entry},mk::segment({0,entry},{k::conditional_at(final_at)}),
        [](auto& z) { z.nop(entry); return z.final_jump_if(final_at,k::Condition::z); });
    auto m=start(program({0xC2,target&0xff,target>>8}),s);
    m.step(registry);
    check(m.opcode_check()==nullptr,"the engine has no opcode check outside lockstep");
}
// The restrictions lifted for exact bodies (review of Part 0a, M3): a mapper
// write and a memory-control (port 3E) write inside an exact body, under
// strict lockstep.
void lifted() {
    for (std::uint8_t bank:{2,5}) for (std::uint8_t control:{0xA8,0xAB}) {
        State s;
        run({0x3E,bank,0x32,0xFF,0xFF,0x3E,control,0xD3,0x3E,0x00},{k::core_final_at(final_at+9)},s,[](auto& z) {
            z.nop(entry);
            z.load_immediate(final_at,z.r.a); z.store_a_direct(final_at+2);
            z.load_immediate(final_at+5,z.r.a); z.output(final_at+7);
            z.nop(final_at+9);
            return k::fall_through(final_at+9,final_at+10);
        });
    }
}
// The judge itself (review of Part 0a, B1): an authored exact body renders
// NOP; NOP; LD (C010),A; NOP; RET exactly on the register file, then damages
// one field before it ends; the lockstep must report a divergence in both
// modes. Without damage it passes.
struct Direct final : RoutineContext::RegisterSync {
    void store(Z80&) const override {}
    void load(const Z80&) override {}
};
void judge() {
    auto rom=program({0x00,0x32,0x10,0xC0,0x00,0xC9});
    using Damage=std::function<void(RoutineContext&)>;
    const std::vector<std::pair<const char*,Damage>> damages{
        {"none",{}},
        {"A",[](RoutineContext& c) { c.cpu().af.uint8_values.at_1^=1; }},
        {"F",[](RoutineContext& c) { c.cpu().af.uint8_values.at_0^=0x08; }},
        {"BC",[](RoutineContext& c) { c.cpu().bc.uint16_value^=1; }},
        {"DE",[](RoutineContext& c) { c.cpu().de.uint16_value^=0x100; }},
        {"HL",[](RoutineContext& c) { c.cpu().hl.uint16_value^=1; }},
        {"IX",[](RoutineContext& c) { c.cpu().ix_iy[0].uint16_value^=1; }},
        {"IY",[](RoutineContext& c) { c.cpu().ix_iy[1].uint16_value^=1; }},
        {"AF'",[](RoutineContext& c) { c.cpu().af_.uint16_value^=1; }},
        {"BC'",[](RoutineContext& c) { c.cpu().bc_.uint16_value^=1; }},
        {"DE'",[](RoutineContext& c) { c.cpu().de_.uint16_value^=1; }},
        {"HL'",[](RoutineContext& c) { c.cpu().hl_.uint16_value^=1; }},
        {"SP",[](RoutineContext& c) { c.cpu().sp.uint16_value^=2; }},
        {"MEMPTR",[](RoutineContext& c) { c.cpu().memptr.uint16_value^=1; }},
        {"Q",[](RoutineContext& c) { c.cpu().q^=0x10; }},
        {"R",[](RoutineContext& c) { c.cpu().r^=1; }},
        {"I",[](RoutineContext& c) { c.cpu().i^=1; }},
        {"IFF1",[](RoutineContext& c) { c.cpu().iff1^=1; }},
        {"IFF2",[](RoutineContext& c) { c.cpu().iff2^=1; }},
        {"IM",[](RoutineContext& c) { c.cpu().im=2; }},
        {"after EI",[](RoutineContext& c) { c.cpu().data.uint8_array[0]=0xFB; }},
        {"request",[](RoutineContext& c) { c.cpu().request|=Z80::request_reject_nmi; }},
        {"fetch count",[](RoutineContext& c) { (void)c.fetch_opcode(0x0105); }},
        {"cycles",[](RoutineContext& c) { c.advance(1); }},
        {"RAM write",[](RoutineContext& c) { c.write(0xC020,1); }},
        {"port access",[](RoutineContext& c) { c.write_port(0x7F,0x9F,c.elapsed()); }},
        {"open bus",[](RoutineContext& c) { (void)c.read(0x0401); }},
    };
    unsigned detected=0;
    for (const auto mode:{oracle::Mode::segments,oracle::Mode::instructions})
        for (const auto& [name,damage]:damages) {
            RoutineRegistry registry;
            registry.add_exact({0,entry},k::segment({0,entry},{k::returned_at(0x0105)}),[](const Machine&) { return 200; },
                [damage](RoutineContext& c) {
                    Direct sync; RoutineContext::SyncScope scope(c,sync);
                    auto& cpu=c.cpu();
                    (void)c.fetch_opcode(entry); c.advance(4);
                    (void)c.fetch_opcode(final_at); c.advance(4);
                    (void)c.fetch_opcode(0x0102);
                    const auto address=static_cast<std::uint16_t>(c.read(0x0103)|(c.read(0x0104)<<8));
                    cpu.memptr.uint16_value=static_cast<std::uint16_t>(((cpu.af.uint8_values.at_1)<<8)|((address+1)&0xff));
                    c.write(address,cpu.af.uint8_values.at_1); c.advance(13);
                    // (the program's NOP at 0105 is replaced by the RET below)
                    (void)c.fetch_opcode(0x0105);
                    const auto sp=cpu.sp.uint16_value;
                    const auto caller_low=c.read(sp), caller_high=c.read(static_cast<std::uint16_t>(sp+1));
                    cpu.sp.uint16_value=static_cast<std::uint16_t>(sp+2);
                    cpu.memptr.uint16_value=static_cast<std::uint16_t>(caller_low|(caller_high<<8));
                    c.advance(10);
                    if (damage) damage(c);
                    c.finish({RoutineExit::returned,0x0105},cpu.memptr.uint16_value);
                });
            auto image=std::make_shared<Memory::Rom>(*rom);
            (*image)[0x0105]=0xC9;
            auto m=start(image,State{});
            bool diverged=false;
            try {
                oracle::Lockstep lockstep(m,"judge",mode);
                lockstep.step(m,&registry);
            } catch (const std::runtime_error& e) {
                diverged=std::string_view(e.what()).find("Lockstep divergence")!=std::string_view::npos;
            }
            check(diverged==(damage!=nullptr),std::string("judge, damage ")+name+(mode==oracle::Mode::segments ? " (segments)" : " (instructions)")+
                  (damage ? ": not detected" : ": an undamaged body diverged"));
            detected+=diverged;
        }
    std::cout<<"judge: "<<detected<<" damaged runs detected in both modes\n";
}
}
int main(int argc, char** argv) {
    // The authored programs' code that no test renders runs on the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::authored);
    try {
        const std::string_view which=argc>1 ? argv[1] : "all";
        const std::map<std::string_view,void(*)()> cases{{"transfers",transfers},{"returns",returns},
            {"interrupts",interrupts},{"plain",plain},{"slices",slices},{"check",opcode_check},{"judge",judge},{"lifted",lifted}};
        if (which=="all") for (const auto& [name,run_case]:cases) run_case();
        else {
            const auto found=cases.find(which);
            check(found!=cases.end(),"unknown case");
            found->second();
        }
        std::cout<<which<<": "<<cases_run<<" segments rendered under strict lockstep, "<<oracle::opcode_checks()
                 <<" opcode checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
