// SPDX-License-Identifier: GPL-3.0-or-later
// The test oracle of milestone 14: the console as it ran before any C++
// segment existed, every instruction and every interrupt response executed
// by the pinned core. Linked only by the tests and the replay runner; the
// program links neither this library nor the core.
#pragma once
#include "machine/console.h"
#include "machine/machine.h"
#include <cstdint>
#include <string>
#include <vector>

namespace psmodern::oracle {
struct CoreBus;
// Frozen native machine: the hardware glue is the engine's own (Console);
// the stepping below is the native path of Machine::step at main 4c6328f
// with no hook and no registry, and must not change with the engine. The
// pinned core runs on a copy of the register file for each step (it keeps
// no state of its own between steps beyond the register file's fields).
class NativeMachine : public Console {
public:
    explicit NativeMachine(std::shared_ptr<const Memory::Rom> rom, Memory::CartridgeRam ram = {})
        : Console(std::move(rom),ram) {}
    // State transfer from an engine machine at a top-level boundary: the
    // whole console, register file included, is copied.
    explicit NativeMachine(const Console& state) : Console(state) {}
    // One core step: an instruction, an interrupt response, or one prefix
    // of a prefixed instruction (resume set).
    std::uint64_t step();
    // Runs to the first instruction boundary at or after the next frame
    // target, as Machine::run_frame did, and returns the overshoot.
    std::uint64_t run_frame(const FrameInput& next_input);
    // Brings the hardware to the current cycle, as the start of a step does
    // (it raises an NMI scheduled for this cycle). Idempotent.
    void settle() { sync(cycle_); }
    // Test instrumentation: every processor write, as Machine::run_frame
    // passes it to its observer. The observer must outlive its use.
    void set_write_observer(const WriteObserver* observer) { write_observer_=observer; }
};
// Test scaffolding for the engine (Machine::set_scaffold_step): one pinned
// core step (an instruction, a response, or a prefix) on the engine's register
// file at its current cycle; the engine samples the lines afterwards. For the
// authored programs of the mechanism tests only.
std::uint64_t native_step(Machine& engine);
// Installs native_step for one scope (a test's main) and removes it after.
// `authored`: also for engines with a registry (the mechanism tests'
// authored programs, which render only part of their code); otherwise only
// for steps with no registry (an original run with no rewritten code, the
// comparison of the game's tests).
class Scaffold {
public:
    enum Use { originals, authored };
    explicit Scaffold(Use use=authored)
        : previous_(Machine::scaffold_step()), previous_registry_(Machine::scaffold_with_registry()) {
        Machine::set_scaffold_step(native_step,use==authored);
    }
    ~Scaffold() { Machine::set_scaffold_step(previous_,previous_registry_); }
    Scaffold(const Scaffold&)=delete;
    Scaffold& operator=(const Scaffold&)=delete;
private:
    Machine::ScaffoldStep previous_;
    bool previous_registry_;
};

// The documented equality of engine and oracle (review H3), on an abstract
// state: PC, SP, every register pair including the alternates, IX, IY, I, R,
// IFF1, IFF2, IM, MEMPTR, Q (rendered by the bodies since Part 0b), HALT and resume; the
// arbitration state mapped from the core's fields (the request bits after the
// boundary's INT sampling, the INT and NMI samples, previous instruction EI,
// RETN with IFF1 clear before it, LD A,I/R); the clock, opcode fetch count,
// open bus, I/O control, Pause level, scheduled NMI and controller latches.
// Excluded: the register file's instruction scratch (data[] bytes, xy) and
// the raw INT line (the request bits after sampling are compared). `whole`
// adds memory, the video chip, the sound chip and the controller latches,
// without the host queues and bookkeeping (PSG writes and samples waiting to
// be taken, the cartridge RAM dirty flag): writes are compared through the
// port traces.
enum class Scope { boundary, whole };
bool equal(const Console& engine, const Console& oracle, Scope scope);
// The differing fields, for a divergence report.
std::vector<std::string> differences(const Console& engine, const Console& oracle, Scope scope);
// The host queues `whole` leaves out that the program acts on (review R3 M4):
// the cartridge RAM dirty flag (the frame driver saves when it is set) and
// the PSG writes waiting to be taken. Lockstep compares them at every
// whole-machine comparison; a test comparing two whole machines uses `same_machine`.
std::vector<std::string> queue_differences(const Console& engine, const Console& oracle);
// equal(Scope::whole) and no queue difference; reports every differing field
// on the error stream when they differ.
bool same_machine(const Console& engine, const Console& oracle);
}
