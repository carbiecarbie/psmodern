// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/verification.h"
#include "game/observed.h"
#include "rom/sha1.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace psmodern::replay {
namespace {
const auto observed_pcs=[] {
    std::bitset<65536> pcs;
    for (auto entry:game::host_observed_instructions) pcs.set(entry.address);
    return pcs;
}();
std::string describe(SegmentExit exit) {
    std::ostringstream out;
    out<<(exit.kind==RoutineExit::returned ? "returned " : exit.kind==RoutineExit::copy_repeat ? "copy_repeat " :
          exit.kind==RoutineExit::output_repeat ? "output_repeat " : "core_final ")<<std::hex<<std::uppercase
       <<std::setfill('0')<<std::setw(4)<<exit.address;
    if (exit.stack) out<<std::dec<<" stack "<<std::showpos<<static_cast<int>(exit.stack);
    if (exit.interrupt_mask!=InterruptMask::none)
        out<<(exit.interrupt_mask==InterruptMask::enable ? " EI" : " DI");
    if (exit.cartridge_control) out<<" cartridge control";
    return out.str();
}
}
void CallVerifier::run(Machine& machine, RoutineEntry entry, const SegmentSpec& spec, bool forced,
                       std::uint64_t bound, std::uint64_t call,
                       const std::function<std::optional<SegmentExit>()>& body) {
    const auto cycle=machine.cycles();
    auto fail=[&](const std::string& reason) {
        std::ostringstream header;
        header<<"Divergence: recording "<<recording_<<", frame "<<cycle/Machine::frame_cycles+1
              <<", cycle "<<cycle<<", routine "<<std::hex<<std::uppercase<<std::setfill('0')
              <<std::setw(2)<<entry.bank<<":"<<std::setw(4)<<entry.address
              <<std::dec<<", call "<<call<<'\n';
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
    Machine::PortTrace original_ports(original);
    Machine::PortTrace replacement_ports(machine);
    const auto end=cycle+bound;
    const auto frame_end=cycle-cycle%Machine::frame_cycles+Machine::frame_cycles;
    SegmentExit native{RoutineExit::core_final,entry.address};
    if (!spec.exits.empty() && (spec.exits.front().kind==RoutineExit::copy_repeat ||
                                spec.exits.front().kind==RoutineExit::output_repeat)) {
        const auto count=(bound+20)/21;
        for (unsigned i=0;i<count;++i) {
            if (original.interrupt_accepted_at_boundary()) fail("window-check failure: interrupt accepted");
            if (original.cpu().pc.uint16_value!=entry.address) fail("unexpected block transfer address");
            const auto instruction_start=original.cycles();
            original.step();
            const bool last=i+1==count;
            const bool native_tail=last && spec.exits.front().kind==RoutineExit::copy_repeat &&
                spec.exits.front().native_frame_tail && count>=2 && instruction_start<frame_end;
            if (original.cycles()>frame_end && !native_tail)
                fail("block copy crosses the frame before its native final transfer");
            if (!last && original.cycles()>=frame_end)
                fail("block copy body must finish strictly before the frame boundary");
        }
        if (original.cycles()>end) fail("block copy exceeds bound");
        native=spec.exits.front();
    } else if (forced) {
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
            if (game_observers_ && boundary && observed_pcs[pc]) {
                const auto mapping=original.memory().mapping(pc);
                const RoutineEntry site{mapping.bank,pc};
                if (mapping.kind==Memory::Kind::rom &&
                    std::ranges::find(game::host_observed_instructions,site)!=game::host_observed_instructions.end() &&
                    !(original.cycles()==cycle && site==entry && spec.observe_entry))
                    fail("segment crosses a host-observed instruction");
            }
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
            const auto instruction_start=original.cycles();
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
            const bool crossed_on_load=last && final->native_frame_tail && instruction_start<frame_end &&
                instruction_start-cycle==10 && original.cycles()-instruction_start==10;
            if ((original.cycles()>frame_end && !crossed_on_load) || (original.cycles()==frame_end && !stopped))
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
    // Exits match by their whole declaration, so a different address, kind or
    // stack offset is an exit mismatch.
    const auto& expected=original_ports.accesses();
    const auto& actual=replacement_ports.accesses();
    const auto mismatch=std::mismatch(expected.begin(),expected.end(),actual.begin(),actual.end());
    const bool same_ports=mismatch.first==expected.end() && mismatch.second==actual.end();
    if (original.equivalent(machine) && same_ports && exit->same_declaration(native)) return;
    const auto differences=original.differences(machine);
    std::ostringstream report;
    if (!same_ports)
        report<<"port access sequence differs at event "<<(mismatch.first-expected.begin()+1)
              <<" (original count "<<expected.size()<<", replacement count "<<actual.size()<<")\n";
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
