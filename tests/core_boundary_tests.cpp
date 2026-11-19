// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 14 Part 0a: instruction boundaries inside registered bodies, on
// authored programs (no ROM needed). Every scenario runs under strict
// lockstep with the oracle (the pinned core), so each interrupt acceptance,
// nested handler, return and observer point is compared with the original
// at every instruction boundary.
//   sweep        IRQ and NMI at every cycle of an exact body (nested delivery,
//                re-arbitration at the return, both kinds)
//   pairs        an NMI and an IRQ close together: the late NMI that survives
//                an INT acceptance, RETN with IFF1 clear before it, an NMI
//                landing in a handler nested in a body (depth 2)
//   ld_a_r       LD A,R followed by an interrupt, at top level and in a body
//   ei           EI followed by a pending interrupt, then an exact body
//   vdp          a VDP register-1 write in a body followed by a pending interrupt
//   abandon      Reset during an interrupt nested in a body at SP = CB00
//   observers    frame and boundary observers exactly once, inside bodies too;
//                the overshoot invariant
//   hooks        host hooks at depth are passive; a registry rebuild at depth
//                is refused and the suspended body stays valid
#include "game/dungeon_kit.h"
#include "scaffold.h"
#include "game/map_kit.h"
#include "machine/machine.h"
#include "oracle/lockstep.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace psmodern;
namespace k=psmodern::game::kit;
namespace mk=psmodern::game::map_kit;

namespace {
void check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }

struct Program {
    std::shared_ptr<Memory::Rom> rom=std::make_shared<Memory::Rom>(512*1024,0);
    void put(std::uint16_t at, std::initializer_list<std::uint8_t> bytes) {
        for (auto byte:bytes) (*rom)[at++]=byte;
    }
};
constexpr std::uint16_t caller=0x0200, body_entry=0x0100, body_return=0x012A, stack_top=0xC1F0;
constexpr std::uint16_t irq_counter=0xC010, nmi_counter=0xC011;
// The handlers: the IRQ one acknowledges the frame interrupt and counts it in
// C010 through a registered exact segment (authored addresses 003B-0041, final NOP at 0042);
// the NMI one counts in C011 and returns with RETN.
void handlers(Program& p, bool reset_from_irq=false) {
    if (reset_from_irq) p.put(0x0038,{0xDB,0xBF,0xC3,0x84,0x00});                    // IN A,(BF); JP 0084
    else p.put(0x0038,{0xF5,0xDB,0xBF,0x3A,0x10,0xC0,0x3C,0x3C,0x3C,0x3C,0x00,      // PUSH AF; IN A,(BF); LD A,(C010); INC A x4; NOP
                       0x32,0x10,0xC0,0xF1,0xFB,0xC9});                               // LD (C010),A; POP AF; EI; RET
    p.put(0x0066,{0xF5,0x3A,0x11,0xC0,0x3C,0x32,0x11,0xC0,0xF1,0xED,0x45});          // PUSH AF; LD A,(C011); INC A; LD (C011),A; POP AF; RETN
    p.put(0x0084,{0xF3,0x31,0x00,0xCB,0x18,0xFE});                                   // DI; LD SP,CB00; JR $
    p.put(caller,{0x18,0xFE});                                                         // JR $
}
// The body at 00:0100: LD A,5; LD B,7; ADD A,B; LD HL,C100; LD (HL),A; INC HL;
// eight times (INC A; ADD A,B; LD (HL),A; INC HL); RET.
void body_code(Program& p) {
    p.put(0x0100,{0x3E,0x05,0x06,0x07,0x80,0x21,0x00,0xC1,0x77,0x23});
    for (std::uint16_t i=0;i<8;++i) p.put(static_cast<std::uint16_t>(0x010A+4*i),{0x3C,0x80,0x77,0x23});
    p.put(body_return,{0xC9});
}
template<class Z> k::Exit body_logic(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x0100,r.a); z.load_immediate(0x0102,r.b); z.add_register(0x0104,r.b);
    z.load_hl(0x0105); z.store_register_to_hl(0x0108,r.a); z.increment_hl(0x0109);
    for (std::uint16_t i=0;i<8;++i) {
        const auto at=static_cast<std::uint16_t>(0x010A+4*i);
        z.increment(at,r.a); z.add_register(at+1,r.b); z.store_register_to_hl(at+2,r.a); z.increment_hl(at+3);
    }
    return scaffold::returns(body_return,k::return_cycles);
}
template<class Z> k::Exit handler_logic(Z& z) {
    z.load_a_direct(0x003B);
    for (std::uint16_t at=0x003E;at<=0x0041;++at) z.increment(at,z.r.a);
    return scaffold::continues(0x0042,4);
}
void register_body(RoutineRegistry& registry) {
    k::add_segment(registry,{0,body_entry},k::segment({0,body_entry},{k::returned_at(body_return)}),
                   2*Machine::frame_cycles,[](auto& z) { return body_logic(z); });
}
void register_handler(RoutineRegistry& registry) {
    k::add_segment(registry,{0,0x003B},k::segment({0,0x0038},{k::core_final_at(0x0042)}),
                   2*Machine::frame_cycles,[](auto& z) { return handler_logic(z); });
}
// A power-on machine at `pc` with IM 1, the return address to `caller` on the
// stack and the VDP's power-on frame interrupt acknowledged.
Machine start(const Program& p, std::uint16_t pc, std::uint16_t sp=stack_top, bool iff=true) {
    Machine m(p.rom);
    (void)m.read_port(0xBF,m.cycles()); m.advance(1);
    auto& cpu=m.cpu();
    cpu.im=1; cpu.iff1=cpu.iff2=iff; cpu.pc.uint16_value=pc; cpu.sp.uint16_value=sp;
    m.memory().write(sp,caller&0xff); m.memory().write(static_cast<std::uint16_t>(sp+1),caller>>8);
    return m;
}
// Moves time so that the frame interrupt rises `offset` cycles from now.
void irq_in(Machine& m, std::uint64_t offset) {
    const auto irq=m.next_irq();
    check(irq && *irq>m.cycles()+offset,"frame interrupt not ahead");
    m.advance(*irq-m.cycles()-offset);
}
// Steps under strict lockstep until the program parks at `caller` (or the
// step limit), and, when `done` is given, until it also holds.
void run_to_caller(oracle::Lockstep& lockstep, Machine& m, RoutineRegistry& registry,
                   const std::function<bool()>& done = {}, unsigned limit=100000) {
    auto finished=[&] { return m.cpu().pc.uint16_value==caller && (!done || done()); };
    for (unsigned i=0;i<limit && !finished();++i) lockstep.step(m,&registry);
    check(finished(),"program did not reach its caller");
}

// IRQ and NMI at every cycle of the body.
void sweep() {
    Program p; handlers(p); body_code(p);
    RoutineRegistry registry; register_body(registry); register_handler(registry);
    auto plain=start(p,body_entry);
    const auto t0=plain.cycles();
    { oracle::Lockstep l(plain,"plain",oracle::Mode::instructions); l.step(plain,&registry); }
    const auto length=plain.cycles()-t0;
    unsigned irq=0, nmi=0, runs=0;
    for (int kind=0;kind<2;++kind)
        for (std::uint64_t offset=0;offset<length+20;++offset) {
            auto m=start(p,body_entry);
            if (kind==0) irq_in(m,offset); else m.schedule_nmi(m.cycles()+offset);
            oracle::Lockstep lockstep(m,"sweep",oracle::Mode::instructions);
            const auto counter=kind==0 ? irq_counter : nmi_counter;
            run_to_caller(lockstep,m,registry,[&] { return m.peek(counter)!=0; });
            check(m.peek(counter)==(kind==0 ? 4 : 1),"the handler ran once");
            ++runs;
        }
    const auto& counters=registry.find({0,body_entry})->counters;
    irq=static_cast<unsigned>(counters.irq_deliveries); nmi=static_cast<unsigned>(counters.nmi_deliveries);
    check(irq>length/8 && nmi>length/8,"interrupts were delivered inside the body");
    std::cout<<"sweep: body of "<<length<<" cycles, "<<runs<<" runs, "<<irq<<" IRQ and "<<nmi<<" NMI delivered inside it\n";
}
// An NMI near an IRQ: every combination of the two around the body's start.
void pairs() {
    Program p; handlers(p); body_code(p);
    RoutineRegistry registry; register_body(registry); register_handler(registry);
    unsigned runs=0;
    for (std::uint64_t irq=10;irq<70;irq+=3)
        for (std::uint64_t nmi=0;nmi<120;++nmi) {
            auto m=start(p,body_entry);
            irq_in(m,irq); m.schedule_nmi(m.cycles()+nmi);
            oracle::Lockstep lockstep(m,"pairs",oracle::Mode::instructions);
            run_to_caller(lockstep,m,registry,[&] { return m.peek(irq_counter) && m.peek(nmi_counter); });
            check(m.peek(irq_counter)==4 && m.peek(nmi_counter)==1,"both handlers ran once");
            ++runs;
        }
    const auto& census=registry.census();
    check(census.nested_nmi+census.body_nmi>0,"an NMI landed inside the IRQ handler");
    const auto& handler=registry.find({0,0x003B})->counters;
    check(handler.nmi_deliveries>0,"an NMI was delivered inside the handler's body at depth 2");
    std::cout<<"pairs: "<<runs<<" runs; NMI in the handler's body "<<handler.nmi_deliveries
             <<", NMI between handler segments "<<census.nested_nmi<<"\n";
}
// LD A,R then an interrupt: the NMOS rule clears PF in F (rule 5).
struct Direct final : RoutineContext::RegisterSync {
    void store(Z80&) const override {}
    void load(const Z80&) override {}
};
void ld_a_r() {
    Program p; handlers(p);
    p.put(0x0100,{0xED,0x5F,0x00,0x00,0xC9});                  // LD A,R; NOP; NOP; RET
    RoutineRegistry none;
    RoutineRegistry registry;
    // The same instructions as a hand-written exact body on the register file.
    registry.add_exact({0,0x0100},k::segment({0,0x0100},{k::returned_at(0x0104)}),[](const Machine&) { return 27; },
        [](RoutineContext& c) {
            Direct sync; RoutineContext::SyncScope scope(c,sync);
            c.fetch_opcode(0x0100); c.fetch_opcode(0x0101);
            auto& cpu=c.cpu();
            const auto a=cpu.r;
            cpu.af.uint8_values.at_1=a;
            cpu.af.uint8_values.at_0=static_cast<std::uint8_t>((a&0xA8)|(a ? 0 : 0x40)|(cpu.iff2 ? 0x04 : 0)|(cpu.af.uint8_values.at_0&1));
            cpu.q=cpu.af.uint8_values.at_0; // LD A,R writes the flags: Q = F
            c.advance(9);
            c.fetch_opcode(0x0102); c.advance(4);
            c.fetch_opcode(0x0103); c.advance(4);
            scaffold::native_return(c,0x0104);
        });
    unsigned delivered=0;
    for (auto* r:{&none,&registry})
        for (std::uint64_t offset=0;offset<30;++offset) {
            auto m=start(p,0x0100);
            m.cpu().r=0x7f;
            irq_in(m,offset);
            oracle::Lockstep lockstep(m,"ld a,r",oracle::Mode::instructions);
            run_to_caller(lockstep,m,*r);
        }
    delivered=static_cast<unsigned>(registry.find({0,0x0100})->counters.irq_deliveries);
    check(delivered>0,"an IRQ was delivered right after the rendered LD A,R");
    std::cout<<"ld_a_r: top level and "<<delivered<<" deliveries inside a body after LD A,R\n";
}
// EI with the interrupt already pending: the instruction after EI runs first.
void ei() {
    Program p; handlers(p); body_code(p);
    p.put(0x00FF,{0xFB});                                       // EI, then the body at 0100
    RoutineRegistry registry; register_body(registry); register_handler(registry);
    for (std::uint64_t offset=0;offset<40;++offset) {
        auto m=start(p,0x00FF,stack_top,false);
        irq_in(m,offset);
        oracle::Lockstep lockstep(m,"ei",oracle::Mode::instructions);
        run_to_caller(lockstep,m,registry,[&] { return m.peek(irq_counter)!=0; });
        check(m.peek(irq_counter)==4,"the pending interrupt ran once");
    }
    check(registry.find({0,body_entry})->counters.irq_deliveries>0,"a pending interrupt was taken inside the body after EI");
    std::cout<<"ei: the interrupt pending at EI was taken after the body's first instruction\n";
}
// A VDP register-1 write enabling the frame interrupt while its flag is set:
// the line rises at once and the next boundary in the body takes it.
void vdp() {
    Program p; handlers(p);
    p.put(0x0100,{0x3E,0x60,0xD3,0xBF,0x3E,0x81,0xD3,0xBF,0x00,0xC9});  // LD A,60; OUT (BF),A; LD A,81; OUT (BF),A; NOP; RET
    RoutineRegistry registry; register_handler(registry);
    mk::add_segment(registry,{0,0x0100},mk::segment({0,0x0100},{mk::returned_at(0x0109)}),[](auto& z) {
        z.load_immediate(0x0100,z.r.a); z.output(0x0102);
        z.load_immediate(0x0104,z.r.a); z.output(0x0106);
        z.nop(0x0108);
        return scaffold::returns(0x0109,mk::return_cycles);
    });
    auto m=start(p,0x0100);
    // Frame interrupt disabled (register 1 = 40h), then past its line so the
    // flag is set while the line stays low.
    m.write_port(0xBF,0x40,m.cycles()); m.write_port(0xBF,0x81,m.cycles());
    m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles-1000);
    check(!m.vdp().irq(),"the frame interrupt is disabled");
    oracle::Lockstep lockstep(m,"vdp",oracle::Mode::instructions);
    run_to_caller(lockstep,m,registry);
    check(registry.find({0,0x0100})->counters.irq_deliveries==1,"the register write raised the interrupt inside the body");
    check(m.peek(irq_counter)==4,"the handler ran once");
    std::cout<<"vdp: register-1 write followed by the pending interrupt, delivered inside the body\n";
}
// Reset during an interrupt nested in a body running at SP = CB00.
void abandon() {
    Program p; handlers(p,true); body_code(p);
    RoutineRegistry registry; register_body(registry);
    unsigned abandoned=0;
    for (std::uint64_t offset=20;offset<200;offset+=7) {
        auto m=start(p,body_entry,0xCB00);
        irq_in(m,offset);
        oracle::Lockstep lockstep(m,"abandon",oracle::Mode::instructions);
        for (unsigned i=0;i<64 && m.cpu().pc.uint16_value!=0x0088;++i) lockstep.step(m,&registry);
        check(m.cpu().pc.uint16_value==0x0088 && m.cpu().sp.uint16_value==0xCB00,"the reset code ran to its loop");
        check(m.depth()==0,"the engine is back at top level");
    }
    abandoned=static_cast<unsigned>(registry.find({0,body_entry})->counters.abandoned);
    check(abandoned>0 && registry.census().abandoned==abandoned,"the suspended body was abandoned");
    std::cout<<"abandon: "<<abandoned<<" bodies abandoned by Reset at SP = CB00\n";
}
// Observers: the frame observer once per frame, at any depth, the boundary
// observer once per boundary; the overshoot invariant.
void observers() {
    Program p; handlers(p); body_code(p);
    // A long exact body: 2000 times LD B,7 then RET, straddling frame ends.
    std::vector<std::uint8_t> code;
    for (int i=0;i<2000;++i) { code.push_back(0x06); code.push_back(0x07); }
    for (std::size_t i=0;i<code.size();++i) (*p.rom)[0x1000+i]=code[i];
    (*p.rom)[0x1000+code.size()]=0xC9;
    p.put(caller,{0xCD,0x00,0x10,0x18,0xFB});                    // CALL 1000; JR caller
    RoutineRegistry registry; register_handler(registry);
    k::add_segment(registry,{0,0x1000},k::segment({0,0x1000},{k::returned_at(static_cast<std::uint16_t>(0x1000+code.size()))}),
                   2*Machine::frame_cycles,[n=code.size()](auto& z) {
        for (std::size_t i=0;i<n;i+=2) z.load_immediate(static_cast<std::uint16_t>(0x1000+i),z.r.b);
        return scaffold::returns(static_cast<std::uint16_t>(0x1000+n),k::return_cycles);
    });
    auto m=start(p,caller);
    oracle::Lockstep lockstep(m,"observers",oracle::Mode::instructions);
    unsigned in_body=0;
    std::uint64_t max_overshoot=0;
    for (unsigned frame=0;frame<120;++frame) {
        unsigned fired=0;
        const auto target=(m.cycles()/Machine::frame_cycles+1)*Machine::frame_cycles;
        const auto overshoot=lockstep.run_frame(m,{},&registry,[&](const Machine& engine,oracle::FrameSample&) {
            ++fired;
            check(engine.cycles()>=target,"frame observer before the target");
            if (engine.cpu().pc.uint16_value>=0x1000 && engine.cpu().pc.uint16_value<0x1000+code.size()) ++in_body;
        });
        check(fired==1,"frame observer fired exactly once");
        check(overshoot<Machine::frame_cycles,"overshoot invariant");
        max_overshoot=std::max(max_overshoot,overshoot);
    }
    check(in_body>0,"some frame observers fired inside the long body");
    check(registry.census().max_overshoot==max_overshoot && max_overshoot>0,"overshoot measured");
    // Boundary observer: once per boundary (distinct cycles, increasing).
    std::uint64_t last=0; unsigned count=0; bool ordered=true;
    const Machine::BoundaryObserver observer=[&](const Machine::Boundary&) {
        ordered=ordered && (count==0 || m.cycles()>last); last=m.cycles(); ++count;
    };
    m.set_boundary_observer(&observer);
    for (int i=0;i<8;++i) m.step(registry);
    m.set_boundary_observer(nullptr);
    check(ordered && count>2000,"boundary observer once per boundary");
    std::cout<<"observers: 120 frames, "<<in_body<<" frame observers inside the body, maximum overshoot "
             <<max_overshoot<<" cycles\n";
}
// Hooks at depth are passive.
void hooks() {
    Program p; handlers(p); body_code(p);
    RoutineRegistry registry; register_body(registry); register_handler(registry);
    auto run=[&](const Machine::Hook& hook) {
        auto m=start(p,body_entry);
        irq_in(m,30);
        for (unsigned i=0;i<100000 && m.cpu().pc.uint16_value!=caller;++i) m.step(registry,hook);
        return m.peek(irq_counter);
    };
    unsigned at_depth=0;
    check(run([&](std::uint16_t pc,Memory::Mapping) -> std::optional<std::uint16_t> {
        if (pc>=0x0038 && pc<0x0050) ++at_depth;
        return {};
    })==4,"a passive hook changes nothing");
    check(at_depth>0,"the hook ran inside the handler");
    bool refused=false;
    try {
        auto m=start(p,body_entry); irq_in(m,30);
        const Machine::Hook changes=[&](std::uint16_t pc,Memory::Mapping) -> std::optional<std::uint16_t> {
            if (pc==0x0038 && m.depth()) m.cpu().bc.uint16_value^=1;
            return {};
        };
        for (unsigned i=0;i<100000 && m.cpu().pc.uint16_value!=caller;++i) m.step(registry,changes);
    } catch (const std::logic_error&) { refused=true; }
    check(refused,"a hook changing a register at depth is refused");
    refused=false;
    try {
        auto m=start(p,body_entry); irq_in(m,30);
        const Machine::Hook rebuild=[&](std::uint16_t pc,Memory::Mapping) -> std::optional<std::uint16_t> {
            if (pc==0x0038 && m.depth()) {
                RoutineRegistry next; register_body(next); register_handler(next);
                registry=std::move(next);
            }
            return {};
        };
        for (unsigned i=0;i<100000 && m.cpu().pc.uint16_value!=caller;++i) m.step(registry,rebuild);
    } catch (const std::logic_error&) { refused=true; }
    check(refused,"a registry rebuild at depth is refused (the suspended body stays alive)");
    std::cout<<"hooks: passive at depth ("<<at_depth<<" calls), changes refused\n";
}
}
int main(int argc, char** argv) {
    // The authored programs' code that no test renders runs on the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::authored);
    try {
        const std::string_view which=argc>1 ? argv[1] : "all";
        const std::map<std::string_view,void(*)()> cases{{"sweep",sweep},{"pairs",pairs},{"ld_a_r",ld_a_r},{"ei",ei},
            {"vdp",vdp},{"abandon",abandon},{"observers",observers},{"hooks",hooks}};
        if (which=="all") for (const auto& [name,run]:cases) run();
        else {
            const auto found=cases.find(which);
            check(found!=cases.end(),"unknown case");
            found->second();
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
