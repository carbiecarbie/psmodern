// SPDX-License-Identifier: GPL-3.0-or-later
// Dual-run verification of milestone 14 (planning 4, review H3): an oracle
// NativeMachine runs beside the engine. At each engine instruction boundary
// it is stepped to the same cycle, where it must be at an instruction
// boundary too, and the two are compared through oracle::equal together with
// every memory write and port access since the previous comparison. At every
// frame observer the whole machines are compared, with the host queues of
// oracle::queue_differences (cartridge RAM dirty flag, PSG writes waiting).
#pragma once
#include "oracle/native_machine.h"
#include "machine/machine.h"
#include "machine/routines.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace psmodern::oracle {
// The test-side opcode check of the renderers (review H4): installed on the
// engine by every Lockstep for its run, it compares each declared pattern
// (RoutineContext::expect_opcode) with the byte at that address in the image
// the engine runs and, when the build configured them (PSMODERN_ROM_US and
// PSMODERN_ROM_BR), at the same bank and offset in both game images. A
// mismatch throws. The program never installs it.
void check_opcode(const Machine& engine, std::uint16_t address, OpcodePattern pattern);
// Checks made in this process (tests assert the check ran).
std::uint64_t opcode_checks();
// Instruction addresses reached at boundaries under lockstep (the run-time
// half of the rendered-address census): ROM locations by bank*0x4000+offset,
// `rendered` inside a body, `native` at a top-level boundary (a segment entry
// or a native step: the census tool tells them apart). Collected only when
// enabled; written by tests that are given PSMODERN_COVERAGE_DIR.
struct Coverage {
    std::vector<std::uint8_t> rendered, native;
    bool enabled=false;
};
Coverage& coverage();
enum class Mode {
    segments,      // every top-level segment boundary (the default)
    instructions,  // every instruction boundary, inside bodies and handlers too
};
struct FrameSample {
    std::vector<PsgWrite> writes;
    PsgSamples samples;
};
class Lockstep {
public:
    // The oracle starts as a transfer of the engine's state; the engine must
    // be at a top-level boundary.
    Lockstep(const Machine& engine, std::string label, Mode mode = Mode::segments);
    Lockstep(const Lockstep&)=default;
    Lockstep& operator=(const Lockstep&)=default;
    // One engine run_frame under lockstep. At the frame observer the whole
    // machines are compared, the PSG output of both is drained, and the
    // engine's is handed to `at_frame` with the engine as the reference
    // emulator was sampled. Throws a divergence report.
    using FrameCallback = std::function<void(const Machine& engine, FrameSample& sample)>;
    std::uint64_t run_frame(Machine& engine, const FrameInput& input, RoutineRegistry* routines,
                            const FrameCallback& at_frame = {}, const Machine::Hook& hook = {},
                            const Machine::WriteObserver& writes = {});
    // Steps the oracle to the engine's cycle (the engine at a top-level
    // boundary, for example where run_frame returned or a hook stopped it)
    // and compares the whole machines; then both may be changed alike.
    void synchronize(Machine& engine);
    // One engine top-level step under lockstep, then a whole-machine
    // comparison at the boundary where it ended.
    std::uint64_t step(Machine& engine, RoutineRegistry* routines, const Machine::Hook& hook = {});
    NativeMachine& oracle() { return oracle_; }
    const NativeMachine& oracle() const { return oracle_; }
    std::uint64_t comparisons() const { return comparisons_; }
    std::uint64_t body_comparisons() const { return body_comparisons_; }
    std::uint64_t frame_comparisons() const { return frame_comparisons_; }
    Mode mode() const { return mode_; }
    void set_label(std::string label) { label_=std::move(label); }
private:
    class Session;
    void compare(Machine& engine, const Machine::Boundary& boundary, Scope scope, Session& session);
    [[noreturn]] void fail(const Machine& engine, const Machine::Boundary& boundary, const std::string& what) const;
    NativeMachine oracle_;
    std::string label_;
    Mode mode_;
    std::uint64_t comparisons_=0, body_comparisons_=0, frame_comparisons_=0;
    // The PC and mapping of the last top-level boundary: the segment the
    // engine is in when a divergence shows inside it.
    std::uint16_t segment_pc_=0;
    unsigned segment_bank_=0;
};
}
