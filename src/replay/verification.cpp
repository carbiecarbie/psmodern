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
    const auto sp=machine.cpu().sp.uint16_value;
    const auto target=static_cast<std::uint16_t>(machine.peek(sp) |
        (machine.peek(static_cast<std::uint16_t>(sp+1))<<8));
    auto core_final=[&](std::uint16_t address) {
        return std::find(spec.exits.begin(),spec.exits.end(),SegmentExit{RoutineExit::core_final,address})!=spec.exits.end();
    };
    Machine original=machine;
    const auto end=cycle+bound;
    const auto frame_end=cycle-cycle%Machine::frame_cycles+Machine::frame_cycles;
    SegmentExit native{RoutineExit::core_final,entry.address};
    if (forced) {
        // A forced single instruction: one native step, admitted in any window.
        original.step();
        if (original.cycles()>end) fail("understated bound");
    } else for (std::uint16_t start=entry.address;;) {
        if (original.interrupt_accepted_at_boundary()) fail("window-check failure: interrupt accepted");
        const bool boundary=original.cpu().resume==0;
        const auto pc=original.cpu().pc.uint16_value;
        if (boundary) start=pc;
        // A core_final exit is the instruction at a declared address, reached at
        // a boundary with the entry stack; it is the last step.
        const bool last=boundary && original.cpu().sp.uint16_value==sp && core_final(pc);
        // Native execution only; no registry, hooks, or verifier on this copy.
        original.step();
        const bool returned=!last && original.cpu().resume==0 &&
            original.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp+2) &&
            original.cpu().pc.uint16_value==target;
        const bool stopped=last || returned;
        if (stopped) native=SegmentExit{last ? RoutineExit::core_final : RoutineExit::returned,start};
        // A frame boundary exactly at a declared exit's end is outside [S,E).
        if (original.cycles()>frame_end || (original.cycles()==frame_end && !stopped))
            fail("window-check failure: frame boundary");
        if (original.cycles()>end) fail("understated bound");
        if (stopped) break;
        if (original.cycles()==end) fail("understated bound");
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
    if (differences.empty() && *exit==native) return;
    std::ostringstream report;
    for (const auto& d:differences) {
        report<<d.field<<": ";
        if (d.can_carry_rom) report<<"different (values redacted)";
        else report<<"original="<<d.original<<", replacement="<<d.replacement;
        report<<'\n';
    }
    if (*exit!=native) report<<"exit mismatch: original="<<describe(native)<<", replacement="<<describe(*exit)<<'\n';
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
