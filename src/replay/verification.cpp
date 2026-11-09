// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/verification.h"
#include "rom/sha1.h"
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace psmodern::replay {
void CallVerifier::run(Machine& machine, RoutineEntry entry, std::uint64_t bound,
                       std::uint64_t call, const std::function<void()>& body) {
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
    Machine original=machine;
    const auto end=cycle+bound;
    const auto frame_end=cycle-cycle%Machine::frame_cycles+Machine::frame_cycles;
    for (;;) {
        if (original.interrupt_accepted_at_boundary()) fail("window-check failure: interrupt accepted");
        // Native execution only; no registry, hooks, or verifier on this copy.
        original.step();
        const bool returned=original.cpu().resume==0 &&
            original.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp+2) &&
            original.cpu().pc.uint16_value==target;
        // A frame boundary exactly at the returning boundary is outside [S,E).
        if (original.cycles()>frame_end || (original.cycles()==frame_end && !returned))
            fail("window-check failure: frame boundary");
        if (original.cycles()>end) fail("understated bound");
        if (returned) break;
        if (original.cycles()==end) fail("understated bound");
    }
    try { body(); }
    catch (const std::exception&) { fail("routine execution failure: return or bound guard"); }
    const auto differences=original.differences(machine);
    if (differences.empty()) return;
    std::ostringstream report;
    for (const auto& d:differences) {
        report<<d.field<<": ";
        if (d.can_carry_rom) report<<"different (values redacted)";
        else report<<"original="<<d.original<<", replacement="<<d.replacement;
        report<<'\n';
    }
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
