// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 14 Part C4: the interrupt sweep of every segment of a file
// (recipe step 5). Each segment that has more than one instruction runs once
// from a plain state (frame interrupt enabled, registers from a fixed seed,
// work RAM as at power-on, plus whatever `adjust` sets), then again with an
// IRQ and with an NMI raised at about 60 evenly spaced cycles of its run, each
// under strict lockstep with the oracle (probe::Harness::sweep_interrupts).
// A segment that cannot run from the plain state (the path would leave the
// segment's own frame, or the state it reads is not set) is reported by name
// with the reason: the test fails and the caller adds an `adjust` for it.
#pragma once
#include "probe.h"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace c4 {
using namespace psmodern;
// A list item is a Segment (`.entry`), a RoutineEntry, or a bank 1 address.
template<class T> RoutineEntry entry_of(const T& item, unsigned bank=1) {
    if constexpr (requires { item.entry; }) return item.entry;
    else if constexpr (requires { item.bank; }) return item;
    else return RoutineEntry{static_cast<std::uint8_t>(bank),static_cast<std::uint16_t>(item)};
}
struct SweepTotals { unsigned segments=0, single=0, runs=0, irq=0, nmi=0, exits_declared=0, exits_taken=0; std::string untaken; };
// `adjust(entry,machine)` may set registers or memory for the entry; it
// returns nothing and is called for every segment (it ignores the others).
template<class Entries>
SweepTotals sweep_file(probe::Harness& harness, const char* file, const Entries& entries,
                       const std::function<void(RoutineEntry,Machine&)>& adjust={}, unsigned samples=30, unsigned seeds=3) {
    SweepTotals totals;
    std::string failures;
    for (const auto& item:entries) {
        const RoutineEntry entry=entry_of(item);
        const auto& spec=harness.routine(entry).spec;
        if (spec.single_instruction) { ++totals.single; continue; }
        auto touch=[&](Machine& m) { if (adjust) adjust(entry,m); };
        try {
            // Several register seeds take different paths through the body
            // (flags, loop counts); the sweep must deliver interrupts inside the
            // body for at least one of them, and every run must match the oracle.
            unsigned irq=0, nmi=0, runs=0;
            std::set<std::uint32_t> taken;
            for (unsigned variant=0;variant<seeds;++variant) {
                probe::Setup setup; setup.iff1=setup.iff2=true; setup.seed=7+entry.address+variant*0x101;
                auto probe_machine=harness.prepare(entry,setup);
                touch(probe_machine);
                const auto length=harness.run_cpp(probe_machine,entry).cycles;
                if (const auto& exit=harness.routine(entry).last_exit) taken.insert((static_cast<std::uint32_t>(exit->kind)<<16)|exit->address);
                const unsigned stride=static_cast<unsigned>(std::max<std::uint64_t>(1,length/samples));
                const auto result=harness.sweep_interrupts(entry,setup,touch,stride);
                irq+=result.irq_deliveries; nmi+=result.nmi_deliveries; runs+=result.runs;
            }
            if (irq==0 || nmi==0) throw std::runtime_error("nothing was delivered inside the body");
            totals.exits_declared+=static_cast<unsigned>(spec.exits.size()); totals.exits_taken+=static_cast<unsigned>(taken.size());
            if (taken.size()<spec.exits.size()) {
                char b[16]; std::snprintf(b,sizeof b," %04X(%u/%u)",entry.address,static_cast<unsigned>(taken.size()),static_cast<unsigned>(spec.exits.size()));
                totals.untaken+=b;
            }
            ++totals.segments; totals.runs+=runs; totals.irq+=irq; totals.nmi+=nmi;
        } catch (const std::exception& e) {
            failures+="  "+std::to_string(entry.bank)+":"+[&] { char b[8]; std::snprintf(b,sizeof b,"%04X",entry.address); return std::string(b); }()+" "+e.what()+"\n";
        }
    }
    std::cout<<"sweep "<<file<<": "<<totals.segments<<" segments ("<<totals.single<<" single instructions skipped), "<<totals.runs
             <<" runs under strict lockstep, "<<totals.irq<<" IRQ and "<<totals.nmi<<" NMI delivered inside bodies\n";
    if (!failures.empty()) throw std::runtime_error("sweep of "+std::string(file)+" failed for:\n"+failures);
    return totals;
}
}
