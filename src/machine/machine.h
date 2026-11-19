// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/console.h"
#include "machine/routines.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace psmodern {
// The engine: the console running the game's registered C++ segments. It has
// no processor core (milestone 14): every instruction is rendered by a
// segment's body, and interrupts are accepted in C++ at every instruction
// boundary: between segments and, for an exact segment, inside its body,
// where the handler runs by nested dispatch and the body resumes after it.
// A state no rendered code handles (an address with no segment, a segment
// refused at its entry, an interrupt response other than IM 1) stops the run
// with UnhandledState; nothing runs in its place.
class Machine : public Console {
public:
    using Hook = std::function<std::optional<std::uint16_t>(std::uint16_t, Memory::Mapping)>;
    // One instruction boundary as the engine reaches it: the PC of the next
    // instruction, the nesting depth of interrupt deliveries inside bodies,
    // whether it lies inside a body (or before its final instruction), and
    // whether the previous instruction was rendered by a body (its Q is then
    // not rendered yet: Part 0b of milestone 14 adds it).
    struct Boundary {
        std::uint16_t pc;
        unsigned depth;
        bool in_body;
        bool rendered;
    };
    // Test instrumentation: called at every instruction boundary where the
    // register file holds the whole state (top level, nested handlers, and
    // inside exact bodies), once per boundary. It must not change the machine.
    using BoundaryObserver = std::function<void(const Boundary&)>;
    // Called exactly once per run_frame, at the first instruction boundary at
    // or after the frame target, at any depth, with the registers synced: the
    // state the reference emulator is sampled in. It must not change the
    // machine (taking PSG writes or samples is allowed), copy it or run it.
    using FrameObserver = std::function<void()>;
    explicit Machine(std::shared_ptr<const Memory::Rom> rom, Memory::CartridgeRam ram = {})
        : Console(std::move(rom),ram) {}
    // A Machine is a value only at top level: copying one from inside a
    // nested delivery (an observer at depth) is refused.
    Machine(const Machine& other);
    Machine& operator=(const Machine& other);
    // One top-level step: an interrupt response, or one segment with every
    // handler delivered inside it.
    std::uint64_t step(const Hook& hook = {}, RoutineRegistry* routines = nullptr);
    std::uint64_t step(RoutineRegistry& routines, const Hook& hook = {}) { return step(hook,&routines); }
    // Runs steps until the first top-level boundary at or after the next
    // frame target and returns how far past the target it stopped.
    std::uint64_t run_frame(const FrameInput& next_input, RoutineRegistry* routines = nullptr, const Hook& hook = {},
                            const WriteObserver& writes = {}, const FrameObserver& frame = {});
    std::uint64_t run_frame(const FrameInput& input, RoutineRegistry& routines, const Hook& hook = {},
                            const WriteObserver& writes = {}, const FrameObserver& frame = {}) {
        return run_frame(input,&routines,hook,writes,frame);
    }
    unsigned block_copy_cycles() const;
    unsigned block_copy_frame_cycles() const;
    unsigned video_register_output_cycles() const;
    // Instrumentation, never copied; the observer must outlive its use.
    void set_boundary_observer(const BoundaryObserver* observer) { boundary_observer_=observer; }
    // Test-side opcode check of the renderers (review H4): called with the
    // address and declared pattern of every rendered instruction that declares
    // one (RoutineContext::expect_opcode). The program installs none, so it
    // never reads an opcode to decide anything. Never copied.
    using OpcodeCheck = std::function<void(const Machine&, std::uint16_t, OpcodePattern)>;
    void set_opcode_check(const OpcodeCheck* check) { opcode_check_=check; }
    const OpcodeCheck* opcode_check() const { return opcode_check_; }
    unsigned depth() const { return depth_; }
    // Test scaffolding (milestone 14). The program installs none and links no
    // processor core, so for it an address where no segment begins, a
    // interrupt response other than IM 1, or a prefix resumption
    // stops the run (UnhandledState). Only the test oracle provides a step
    // (oracle::native_step, the pinned core on the register file), which the
    // mechanism tests install for the parts of their authored programs they
    // do not render (callers, idle loops, handlers). Process-wide; every use
    // is counted in the registry's census (scaffold_steps), and the game's
    // probes and checkpoint runs require that count to stay zero.
    // With `with_registry` false the scaffolding serves only steps given no
    // registry (or an empty one): an original run with no rewritten code, as
    // the game's tests use for comparison, where the scaffolding then takes
    // the whole step, interrupt responses included (the engine's arbitration
    // is not used, so the reference matches oracle::NativeMachine); a step
    // with routines registered still stops at unrendered code.
    using ScaffoldStep = std::uint64_t (*)(Machine&);
    static void set_scaffold_step(ScaffoldStep step, bool with_registry) { scaffold_step_=step; scaffold_with_registry_=with_registry; }
    static ScaffoldStep scaffold_step() { return scaffold_step_; }
    static bool scaffold_with_registry() { return scaffold_with_registry_; }
private:
    friend class RoutineContext;
    bool run_routine(RoutineRegistry& routines);
    // An instruction boundary inside a body (or before its final): observers,
    // then arbitration and nested delivery until no interrupt is due.
    void body_boundary(RoutineContext& context, std::uint16_t at, bool synced);
    void observe_boundary(std::uint16_t pc, bool in_body, bool rendered);
    bool accepts_in_cpp(Acceptance kind) const;
    std::uint64_t accept(Acceptance kind, bool late_nmi);
    // Runs the handler accepted at `at` by nested dispatch until it returns
    // there; throws RoutineAbandoned when it reaches the reset code.
    void deliver(Acceptance kind, bool late_nmi, std::uint16_t at);
    void record_step(RoutineRegistry* routines, std::uint64_t start, std::uint16_t pc, Memory::Mapping mapping);
    [[noreturn]] void unhandled(std::uint16_t pc, const std::string& what) const;
    std::uint64_t routine_window() const;
    // Transient engine state; none of it is copied.
    unsigned depth_=0;
    const Hook* active_hook_=nullptr;
    RoutineRegistry* active_routines_=nullptr;
    const BoundaryObserver* boundary_observer_=nullptr;
    const OpcodeCheck* opcode_check_=nullptr;
    const FrameObserver* frame_observer_=nullptr;
    std::uint64_t frame_target_=0;
    bool frame_observed_=true;
    std::uint64_t observed_cycle_=~std::uint64_t{0};
    static ScaffoldStep scaffold_step_;
    static bool scaffold_with_registry_;
    std::uint64_t scaffold(RoutineRegistry* routines, std::uint64_t start, std::uint16_t pc, bool unsampled_nmi,
                           const std::string& what, const Hook* hook=nullptr);
};
}
