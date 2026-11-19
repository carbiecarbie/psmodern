// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 14 Parts 0a and 0b: forced-interrupt sweeps on the real ROM. For
// each segment below an IRQ and an NMI are raised at every cycle (or every
// `stride` cycles) of the segment's run (probe::sweep); each run is compared
// with the oracle at every instruction boundary, nested handlers included.
// This is the one check of a body that keeps a value read before a boundary
// and uses it after a handler changed that memory, and of Q and MEMPTR left
// by a handler. Exit 77 without the ROM.
//   kits       one segment of each kit family
//   wrappers   every private run wrapper and hand-written body with a sync
//   special    a repeat slice besides 0C:8028, the 00:0008 helper, the
//              frame-update tile transfer, the carry-complement sites, the
//              compare-and-repeat scan, the compare-increment, the indexed
//              bit reset
//   examples   the worked examples of Part 0b (frame wait included)
#include "probe.h"
#include <iostream>
#include <map>

using namespace psmodern;
namespace {
struct Case {
    const char* name;
    RoutineEntry entry;
    std::function<void(Machine&)> adjust;
    unsigned stride=1;
};
probe::Setup plain_setup() { probe::Setup s; s.iff1=s.iff2=true; s.seed=7; return s; }
void run(probe::Harness& harness, const std::vector<Case>& cases) {
    unsigned runs=0, irq=0, nmi=0;
    for (const auto& c:cases) {
        std::cout<<c.name<<": ";
        const auto result=probe::sweep(harness,c.entry,plain_setup(),c.adjust,c.stride);
        runs+=result.runs; irq+=result.irq_deliveries; nmi+=result.nmi_deliveries;
    }
    std::cout<<"Forced-interrupt sweeps: "<<runs<<" runs under strict lockstep, "<<irq<<" IRQ and "<<nmi
             <<" NMI delivered inside bodies.\n";
}
std::vector<Case> kits() {
    return {
        // milestone 7 kit: the main loop's game-mode dispatch.
        {"dungeon kit 00:00AF",{0,0x00AF},{}},
        // milestone 11 kit: the scroll registers of the name-table frame kind.
        {"map kit 00:01EF",{0,0x01EF},{}},
        // milestone 12 kit: the battle name box's register loads.
        {"menu kit 00:2EAC",{0,0x2EAC},{}},
        // the sound driver's silence transfer: OTIR iterations.
        {"menu kit 0C:8028",{12,0x8028},[](Machine& m) {
            m.cpu().bc.uint8_values.at_1=8; m.cpu().bc.uint8_values.at_0=0x7f;
            m.cpu().hl.uint16_value=0x803b;
            const auto sp=m.cpu().sp.uint16_value;
            m.memory().write(static_cast<std::uint16_t>(sp+4),0x80); m.memory().write(static_cast<std::uint16_t>(sp+5),0x01);
        }},
    };
}
std::vector<Case> wrappers() {
    return {
        // money addition (private kit, final return rendered).
        {"money 00:297A",{0,0x297A},[](Machine& m) { m.cpu().de.uint16_value=1234; }},
        // encounters: the world-map check, with its carry complement at 01:5F86.
        {"encounters 01:5F63",{1,0x5F63},{},2},
        // encounters: the dungeon check.
        {"encounters 00:0F4B",{0,0x0F4B},{}},
        // experience award steps.
        {"experience 00:1710",{0,0x1710},{}},
        {"experience 00:1748",{0,0x1748},{},2},
        // battle setup: the experience and money fields.
        {"battle setup 01:60BF",{1,0x60BF},{},2},
        // text: the output-port writer.
        {"text 00:33DA",{0,0x33DA},{},2},
        // (The block transfer wrapper's legacy LDIR slices end before the
        // next interrupt by construction; C1 sweeps the rendered slice.)
    };
}
std::vector<Case> special() {
    return {
        // the VDP address helper at 00:0008 (two port writes and its return).
        {"helper 00:0008",{0,0x0008},{}},
        // the frame-update tile transfer (OUTI loop).
        {"frame update 00:0226",{0,0x0226},[](Machine& m) {
            m.cpu().hl.uint16_value=0xC100; m.cpu().bc.uint16_value=0x10BE; m.cpu().af.uint8_values.at_1=2;
        },2},
        // arithmetic: the division group with its carry complement.
        {"arithmetic 00:04FE",{0,0x04FE},[](Machine& m) { m.cpu().hl.uint16_value=0x1234; m.cpu().de.uint16_value=0x0056; },4},
        // the dungeon layout's compare-and-repeat scan.
        {"dungeon layout 01:6D56",{1,0x6D56},{},2},
        // the save-slot erase in cartridge RAM (compare-increment family).
        {"scenes persistence 00:0847",{0,0x0847},[](Machine& m) { m.memory().write(0xFFFC,8); }},
        // the sound driver's command that resets indexed bits.
        {"sound sequence 0C:85AD",{12,0x85AD},[](Machine& m) {
            m.cpu().ix_iy[0].uint16_value=0xC100; m.cpu().de.uint16_value=0xC200;
        }},
    };
}
std::vector<Case> examples() {
    return {
        // (The frame wait's steps are single instructions: interrupts are
        // taken between them at top level, under lockstep in every checkpoint.)
        // dungeon kit: the cell reader.
        {"dungeon cells 01:6BCA",{1,0x6BCA},{}},
        // map kit: the save path's first segment (pushes, cartridge control).
        {"save menu 00:1D0E",{0,0x1D0E},{}},
        // menu kit: the interaction dispatch's table base and call.
        {"interaction setup 01:4752",{1,0x4752},{}},
    };
}
}
// Reset abandonment on the real code (review-2 of Part 0a): the Reset button
// is pressed (its previous state, kept in work RAM by the interrupt handler,
// is released), and the frame interrupt lands at every boundary of the main
// loop's dispatch body (00:00AF, exact). The handler's reset check (00:010F,
// branch at 00:011A) is taken and goes to the reset code, whose stack load
// abandons the suspended body. Compared with the oracle at every instruction.
void reset_abandonment(probe::Harness& harness) {
    const RoutineEntry body{0,0x00AF};
    unsigned abandoned_runs=0, runs=0;
    for (std::uint64_t offset=0;offset<60;++offset) {
        auto m=harness.prepare(body,plain_setup());
        // The handler keeps the button's previous port bits at the address
        // its LD HL,nn at 00:0113 names: all released.
        const auto previous=static_cast<std::uint16_t>(m.peek(0x0114)|(m.peek(0x0115)<<8));
        m.memory().write(previous,0xFF);
        m.set_reset(true);
        // Latch the pressed button at the next frame boundary, then bring the
        // frame interrupt `offset` cycles after the body's entry.
        m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles+1);
        (void)m.read_port(0xBF,m.cycles()); m.advance(1); // acknowledge the frame interrupt already raised
        m.cpu().iff1=m.cpu().iff2=1;
        const auto irq=m.next_irq();
        probe::check(irq && *irq>m.cycles()+offset,"reset probe needs the frame interrupt ahead");
        m.advance(*irq-m.cycles()-offset);
        const auto before=harness.routine(body).counters.abandoned;
        oracle::Lockstep lockstep(m,"reset +"+std::to_string(offset),oracle::Mode::instructions);
        for (unsigned steps=0;steps<400 && !(m.cpu().pc.uint16_value>=0x0085 && m.cpu().pc.uint16_value<0x00AF &&
                                              m.cpu().sp.uint16_value==0xCB00);++steps)
            lockstep.step(m,&harness.registry());
        ++runs;
        if (harness.routine(body).counters.abandoned>before) ++abandoned_runs;
        probe::check(m.depth()==0,"the engine is back at top level after the reset");
    }
    probe::check(abandoned_runs>0,"no run abandoned the main loop's body");
    std::cout<<"reset: "<<runs<<" runs, "<<abandoned_runs<<" abandoned the suspended 00:00AF body under strict lockstep\n";
}
int main(int argc, char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        probe::check(argc==3 || argc==4,"Expected <rom> <us|br> [kits|wrappers|special|examples|reset].");
        auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        probe::Harness harness(*rom);
        const std::string which=argc==4 ? argv[3] : "kits";
        if (which=="reset") { reset_abandonment(harness); return 0; }
        const std::map<std::string,std::vector<Case>(*)()> groups{{"kits",kits},{"wrappers",wrappers},{"special",special},{"examples",examples}};
        const auto found=groups.find(which);
        probe::check(found!=groups.end(),"unknown group");
        run(harness,found->second());
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
