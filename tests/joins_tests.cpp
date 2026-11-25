// SPDX-License-Identifier: GPL-3.0-or-later
// The joining segments no input recording reaches (milestone 14, Part I;
// game/joins.h): each one, from a prepared state of the identified ROM, runs
// under strict lockstep with the oracle (probe::Harness::run_cpp) with the
// flags clear and set and B at 1 and 2, so a conditional instruction takes both
// of its paths; every declared exit must be taken. The multi-instruction ones
// are swept with an IRQ and an NMI at every cycle.
#include "probe.h"
#include "game/joins.h"
#include <iostream>

using namespace psmodern;
int main(int argc,char** argv) {
    try {
        if (argc!=3) return 77;
        const auto rom=probe::load(argv[1],argv[2]); if (!rom) return 77;
        probe::Harness h(*rom);
        unsigned segments=0, runs=0;
        for (const auto list:game::joins::all) for (const auto& join:list) {
            const auto& routine=h.routine(join.entry);
            probe::check(routine.spec.unit==join.unit,"a joining segment's unit differs from its catalog");
            for (std::uint8_t flags:{std::uint8_t{0x00},std::uint8_t{0xFF}}) for (std::uint8_t b:{std::uint8_t{1},std::uint8_t{2}}) {
                probe::Setup setup; setup.seed=join.entry.address+flags+b;
                auto m=h.prepare(join.entry,setup);
                m.cpu().af.uint8_values.at_0=flags; m.cpu().bc.uint8_values.at_1=b;
                h.run_cpp(m,join.entry); ++runs;
            }
            for (std::size_t i=0;i<routine.spec.exits.size();++i)
                probe::check(routine.rendered[i]>0,"a declared exit of a joining segment was never taken");
            if (!routine.spec.single_instruction) {
                probe::Setup setup; setup.seed=join.entry.address;
                probe::sweep(h,join.entry,setup);
            }
            ++segments;
        }
        // The six interaction events whose handlers are the joins at 01:5535 and
        // 01:5538 (the places where an item is used; the game stopped there
        // from 1.0.0 to 1.3.0): from the interaction entry at 01:474B with the
        // event number in work RAM, through the dispatcher at 01:4769, each
        // segment under lockstep, to the handler's call with its return
        // address on the stack. The event numbers are read from the code table
        // of the player's ROM (182 words after the dispatcher, numbered from 1).
        unsigned routes=0;
        for (unsigned index=0;index<182;++index) {
            const auto low=static_cast<std::uint16_t>(0x4773+2*index);
            const auto handler=static_cast<std::uint16_t>(rom->byte(1,low) | rom->byte(1,static_cast<std::uint16_t>(low+1))<<8);
            if (handler!=0x5535 && handler!=0x5538) continue;
            const std::uint16_t callee=handler==0x5535 ? 0x2D25 : 0x1BE1;   // the button wait; the field menu
            probe::Setup setup; setup.seed=index;
            auto m=h.prepare({1,0x474B},setup);
            m.memory().write(0xC2DB,static_cast<std::uint8_t>(index+1));
            for (unsigned steps=0;m.cpu().pc.uint16_value!=callee;++steps) {
                probe::check(steps<8,"an interaction event did not reach its handler's call");
                const auto pc=m.cpu().pc.uint16_value;
                h.run_cpp(m,{pc<0x4000 ? 0u : 1u,pc});
            }
            const auto sp=m.cpu().sp.uint16_value;
            const auto returns=static_cast<std::uint16_t>(m.peek(sp) | m.peek(static_cast<std::uint16_t>(sp+1))<<8);
            probe::check(returns==handler+3,"an interaction event's handler call returns to the wrong place");
            ++routes;
        }
        probe::check(routes==6,"expected six interaction events at the item places");
        // Milestone 19 (the re-verified stop sweep): the two item-place calls
        // with an IRQ and, separately, an NMI raised at every cycle of the
        // call's run, always (not a sample): each run takes the interrupt before
        // or after the call and must still reach the call in C++ and match the
        // original at every instruction boundary.
        unsigned item_place_runs=0;
        for (const std::uint16_t pc:{std::uint16_t{0x5535},std::uint16_t{0x5538}}) {
            const RoutineEntry entry{1,pc};
            probe::Setup setup; setup.seed=pc;
            auto plain=h.prepare(entry,setup);
            const auto cycles=h.run_cpp(plain,entry).cycles;
            const auto sweep=h.sweep_interrupts(entry,setup,{},1);
            probe::check(sweep.runs==2*cycles,"an item-place call was not swept at every cycle");
            item_place_runs+=sweep.runs;
        }
        std::cout<<segments<<" joining segments, "<<runs<<" lockstep runs, every exit taken; "<<routes
                 <<" item-place events reach their call; "<<item_place_runs
                 <<" interrupt runs over every cycle of the two item-place calls.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
