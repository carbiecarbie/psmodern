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
        std::cout<<segments<<" joining segments, "<<runs<<" lockstep runs, every exit taken.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
