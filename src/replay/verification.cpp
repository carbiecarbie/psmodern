// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/verification.h"
#include "rom/sha1.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace psmodern::replay {
namespace {
std::string describe(SegmentExit exit) {
    std::ostringstream out;
    out<<(exit.kind==RoutineExit::returned ? "returned " : "core_final ")<<std::hex<<std::uppercase
       <<std::setfill('0')<<std::setw(4)<<exit.address;
    if (exit.stack) out<<std::dec<<" stack "<<std::showpos<<static_cast<int>(exit.stack);
    return out.str();
}
}
void CallVerifier::run(Machine& machine, RoutineEntry entry, const SegmentSpec& spec, bool forced,
                       std::uint64_t bound, std::uint64_t call,
                       const std::function<std::optional<SegmentExit>()>& body) {
    const auto cycle=machine.cycles();
    std::ostringstream header;
    header<<"Divergence: recording "<<recording_<<", frame "<<cycle/Machine::frame_cycles+1
          <<", cycle "<<cycle<<", routine "<<std::hex<<std::uppercase<<std::setfill('0')
          <<std::setw(2)<<entry.bank<<":"<<std::setw(4)<<entry.address
          <<std::dec<<", call "<<call<<'\n';
    auto fail=[&](const std::string& reason) {
        throw std::runtime_error(header.str()+reason);
    };
    // Peek is const and bypasses all bus callbacks, including open-bus updates.
    auto word=[&](const Machine& m, std::uint16_t at) {
        return static_cast<std::uint16_t>(m.peek(at) | (m.peek(static_cast<std::uint16_t>(at+1))<<8));
    };
    const auto sp=machine.cpu().sp.uint16_value;
    const auto target=word(machine,sp);
    auto declared=[&](RoutineExit kind, std::uint16_t address)->const SegmentExit* {
        const auto found=std::find(spec.exits.begin(),spec.exits.end(),SegmentExit{kind,address});
        return found==spec.exits.end() ? nullptr : &*found;
    };
    // The word at the entry SP is the caller's return address unless the unit
    // inherited it: an inheriting segment gets this stop only when it declares
    // a returned exit at the entry stack.
    const bool entry_return=spec.exits.empty() || spec.inherited_stack==0 ||
        std::any_of(spec.exits.begin(),spec.exits.end(),[](const SegmentExit& e) {
            return e.kind==RoutineExit::returned && e.stack==0;
        });
    Machine original=machine;
    const auto end=cycle+bound;
    const auto frame_end=cycle-cycle%Machine::frame_cycles+Machine::frame_cycles;
    SegmentExit native{RoutineExit::core_final,entry.address};
    if (forced) {
        // A forced single instruction: one native step, admitted in any window.
        original.step();
        if (original.cycles()>end) fail("understated bound");
    } else {
        // A returned exit declared above the entry stack, armed at a boundary
        // at its address with its SP; the target is read at that SP.
        std::optional<std::pair<const SegmentExit*,std::uint16_t>> armed;
        for (std::uint16_t start=entry.address;;) {
            if (original.interrupt_accepted_at_boundary()) fail("window-check failure: interrupt accepted");
            const bool boundary=original.cpu().resume==0;
            const auto pc=original.cpu().pc.uint16_value, at=original.cpu().sp.uint16_value;
            if (boundary) {
                start=pc; armed.reset();
                const auto* held=declared(RoutineExit::returned,pc);
                if (held && held->stack>0 && at==static_cast<std::uint16_t>(sp+held->stack))
                    armed.emplace(held,word(original,at));
            }
            // A core_final exit is the instruction at a declared address, reached at
            // a boundary with the entry stack plus its offset and, for a loop exit,
            // its declared cycles elapsed; it is the last step.
            const auto* final=boundary ? declared(RoutineExit::core_final,pc) : nullptr;
            const bool last=final && at==static_cast<std::uint16_t>(sp+final->stack) &&
                original.cycles()-cycle>=final->after;
            // Native execution only; no registry, hooks, or verifier on this copy.
            original.step();
            const bool complete=!last && original.cpu().resume==0;
            const auto after_pc=original.cpu().pc.uint16_value, after_sp=original.cpu().sp.uint16_value;
            const bool returned=complete && entry_return &&
                after_sp==static_cast<std::uint16_t>(sp+2) && after_pc==target;
            const bool held_return=complete && !returned && armed &&
                after_sp==static_cast<std::uint16_t>(sp+armed->first->stack+2) && after_pc==armed->second;
            const bool stopped=last || returned || held_return;
            if (last) native=*final;
            else if (held_return) native=*armed->first;
            else if (returned) {
                const auto* plain=declared(RoutineExit::returned,start);
                native=plain && plain->stack==0 ? *plain : SegmentExit{RoutineExit::returned,start};
            }
            // A frame boundary exactly at a declared exit's end is outside [S,E).
            if (original.cycles()>frame_end || (original.cycles()==frame_end && !stopped))
                fail("window-check failure: frame boundary");
            if (original.cycles()>end) fail("understated bound");
            if (stopped) break;
            if (original.cycles()==end) fail("understated bound");
        }
    }
    std::optional<SegmentExit> exit;
    try { exit=body(); }
    catch (const std::exception&) { fail("routine execution failure: return or bound guard"); }
    // A forced call that fell back left the machine unchanged; it runs natively.
    if (!exit) {
        if (!forced) fail("missing exit: the body returned no exit");
        return;
    }
    const auto differences=original.differences(machine);
    // Exits match by their whole declaration, so a different address, kind or
    // stack offset is an exit mismatch.
    if (differences.empty() && exit->same_declaration(native)) return;
    std::ostringstream report;
    for (const auto& d:differences) {
        report<<d.field<<": ";
        if (d.can_carry_rom) report<<"different (values redacted)";
        else report<<"original="<<d.original<<", replacement="<<d.replacement;
        report<<'\n';
    }
    if (!exit->same_declaration(native)) report<<"exit mismatch: original="<<describe(native)<<", replacement="<<describe(*exit)<<'\n';
    fail(report.str());
}
Manifest manifest(const Machine& m, const std::vector<std::uint8_t>& writes) {
    return {sha1(m.picture()),sha1(m.memory().ram()),sha1(m.vdp().vram()),
        sha1(m.vdp().cram()),sha1(m.memory().cartridge_ram()),
        sha1(cpu_bytes(m.cpu())),sha1(writes)};
}
void compare_manifests(const std::string& recording, std::uint64_t frame,
                       const Manifest& original, const Manifest& replacement) {
    for (std::size_t i=0;i<original.size();++i)
        if (original[i]!=replacement[i]) throw std::runtime_error(
            "Recording divergence: recording "+recording+", frame "+std::to_string(frame)+
            ", component "+manifest_components[i]);
}
}
