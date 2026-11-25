// SPDX-License-Identifier: GPL-3.0-or-later
// The joining segments no input recording reaches (milestone 14, Part I;
// game/joins.h): each one, from a prepared state of the identified ROM, runs
// under strict lockstep with the oracle (probe::Harness::run_cpp) with the
// flags clear and set and B at 1 and 2, so a conditional instruction takes both
// of its paths; every declared exit must be taken. The multi-instruction ones
// are swept with an IRQ and an NMI at every cycle.
#include "probe.h"
#include "game/joins.h"
#include <array>
#include <iostream>
#include <set>

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
        // call's run, always (not a sample): each run must reach the call in
        // C++, match the original at every instruction boundary, and show its
        // interrupt accepted and returned from (the evidence below).
        unsigned item_place_runs=0, accepted_runs=0;
        for (const std::uint16_t pc:{std::uint16_t{0x5535},std::uint16_t{0x5538}}) {
            const RoutineEntry entry{1,pc};
            probe::Setup setup; setup.seed=pc;
            auto plain=h.prepare(entry,setup);
            const auto cycles=h.run_cpp(plain,entry).cycles;
            // The evidence that each run's interrupt was taken where it must be
            // (review 2026-11-25: with injection suppressed the sweep still
            // passed). The call is one instruction, so there is no boundary
            // inside it: an IRQ raised at any of its cycles is sampled at its
            // last one and an NMI edge is latched, and both are accepted at
            // the boundary right after it (the first top-level boundary after
            // the entry), at the callee with the call's return address on top
            // of the stack; the handler then returns there with SP restored.
            probe::check(rom->byte(1,pc)==0xCD,"an item-place handler is no longer a CALL");
            const auto callee=static_cast<std::uint16_t>(rom->byte(1,static_cast<std::uint16_t>(pc+1)) |
                                                         rom->byte(1,static_cast<std::uint16_t>(pc+2))<<8);
            std::vector<probe::SweepAcceptance> taken;
            const auto sweep=h.sweep_interrupts(entry,setup,{},1,&taken);
            probe::check(sweep.runs==2*cycles && taken.size()==sweep.runs,"an item-place call was not swept at every cycle");
            std::array<std::set<std::uint64_t>,2> offsets;
            for (const auto& e:taken) {
                const auto run=std::string(e.kind ? "NMI" : "IRQ")+" at +"+std::to_string(e.offset)+" of 01:"+std::to_string(pc);
                probe::check(!e.inside_body && e.boundary==1,"the "+run+" was not accepted at the boundary after the call");
                probe::check(e.resume==callee && e.sp==static_cast<std::uint16_t>(setup.sp-2) && e.top==pc+3,
                             "the "+run+" did not interrupt the callee with the call's return address on the stack");
                probe::check(e.returned,"after the "+run+" the handler did not return to the callee");
                offsets[e.kind].insert(e.offset);
            }
            for (const auto& kind:offsets)
                probe::check(kind.size()==cycles && *kind.rbegin()==cycles-1,"an item-place call's sweep missed a cycle");
            item_place_runs+=sweep.runs;
            accepted_runs+=static_cast<unsigned>(taken.size());
        }
        std::cout<<segments<<" joining segments, "<<runs<<" lockstep runs, every exit taken; "<<routes
                 <<" item-place events reach their call; "<<item_place_runs
                 <<" interrupt runs over every cycle of the two item-place calls, "<<accepted_runs
                 <<" with the interrupt accepted after the call and its handler returned.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
