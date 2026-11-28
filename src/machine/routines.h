// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/cpu.h"
#include "hw/memory.h"
#include <bitset>
#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace psmodern {
class Machine;

struct RoutineEntry {
    unsigned bank;
    std::uint16_t address;
    auto operator<=>(const RoutineEntry&) const = default;
};
// Reasons a segment refuses a call at its entry: the state is one its body
// does not render, and the run stops with UnhandledState naming the segment
// and the reason (there is no processor core to run the original instead).
// For an exact segment (see RegisteredRoutine::exact) irq, nmi, ei_delay and
// interrupt_delay never apply, and frame means only that the estimated bound
// does not fit in one frame: the uncertain path of a state guard (or a sprite
// slice on the wrong port). guard: the segment's explicit entry guard
// (RegisteredRoutine::guard) does not hold (milestone 14 review R2). The
// names keep the transition's word "fallback" for the census columns.
enum class RoutineFallback : unsigned { irq, nmi, frame, ei_delay, interrupt_delay,
    halted, unmapped_stack, mapper_write, guard, count };
inline constexpr std::array<std::string_view,static_cast<unsigned>(RoutineFallback::count)>
    routine_fallback_names{"irq","nmi","frame","ei_delay","interrupt_delay","halted","unmapped_stack","mapper_write","guard"};
// The engine met a state no rendered code handles: an address where no
// segment begins, a segment refusing its entry state (RoutineFallback), or an
// interrupt response other than IM 1. Nothing runs in its place: the program
// stops the game with this message (app FrameDriver).
class UnhandledState : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The instruction a rendered final (or other renderer of milestone 14) renders,
// declared for the test-side opcode check (review H4): the byte at the
// instruction's address, after the `prefix` byte when it is not 0, must equal
// `value` under `mask`. The program itself never evaluates a pattern: only a
// check installed by the tests (Machine::set_opcode_check) reads it.
struct OpcodePattern {
    std::uint8_t prefix, value, mask;
    static constexpr OpcodePattern exactly(std::uint8_t value) { return {0,value,0xff}; }
    static constexpr OpcodePattern prefixed(std::uint8_t prefix, std::uint8_t value) { return {prefix,value,0xff}; }
};
// A state a body cannot render, met inside the body (an explicit in-body guard,
// review R2): the live run stops with this error naming the segment and the
// guard. The estimating run of the same body usually meets the state first;
// the segment then refuses its entry (reason frame).
class GuardFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
// cpp_calls counts the calls the body ran; fallback_calls the calls refused at
// the entry (each stopped the run), by reason in reasons. irq/nmi_deliveries
// count interrupts accepted at an instruction boundary inside this segment's
// body, abandoned the calls left by a Reset at depth, unused_sync the calls of
// a segment not declared exact whose body registered a register sync.
struct RoutineCounters {
    std::uint64_t cpp_calls=0, fallback_calls=0;
    std::array<std::uint64_t,static_cast<unsigned>(RoutineFallback::count)> reasons{};
    std::uint64_t irq_deliveries=0, nmi_deliveries=0, abandoned=0, unused_sync=0;
    std::uint64_t fallback(RoutineFallback reason) const { return reasons.at(static_cast<unsigned>(reason)); }
};

// The kinds of a segment's last instruction, which its body renders like any
// other (milestone 14): returned, a returning instruction; core_final, any
// other last instruction (the name is the transition's, when the core ran
// it); copy_repeat, the last transfer of an LDIR slice; output_repeat, the
// last write of an OTIR slice.
enum class RoutineExit : unsigned { returned, core_final, copy_repeat, output_repeat };
// One bounded section of a forward block copy. A zero count represents 65536.
constexpr unsigned copy_iterations(std::uint16_t count) {
    return count && count<16 ? count : 16;
}
constexpr unsigned copy_duration(std::uint16_t remaining,unsigned count) {
    return count*21-(remaining==count ? 5 : 0);
}
inline constexpr unsigned max_inherited_stack=8;
inline constexpr std::uint16_t max_exit_after=16384;
// Interrupt-mask changes can terminate an ordinary segment (EI or DI as the
// declared last instruction).
enum class InterruptMask : std::uint8_t { none, disable=0xF3, enable=0xFB };
enum class SeedTarget : std::uint8_t { work_ram, cartridge_ram, sprite_shadow_zero };
struct SegmentExit {
    RoutineExit kind;
    std::uint16_t address;
    // SP before the final instruction minus the entry SP, in bytes (even).
    // Negative: words the segment pushed and holds across its final
    // instruction; positive: inherited words it popped.
    std::int8_t stack=0;
    // core_final only: the final instruction is JP cc,nn, JR cc,e or DJNZ,
    // and nothing else.
    bool conditional=false;
    // core_final only: taken at the first arrival with at least this many
    // cycles elapsed since entry (a loop exit); 0 is the first arrival.
    std::uint16_t after=0;
    InterruptMask interrupt_mask=InterruptMask::none;
    // The last instruction is LD (FFFC),A, selecting cartridge RAM.
    bool cartridge_control=false;
    std::optional<std::uint16_t> wram_seed{};
    SeedTarget seed_target=SeedTarget::work_ram;
    // LD (HL),1 publishes a save in a consecutive range of cartridge flags.
    struct FlagRange {
        std::uint16_t first,last;
        bool operator==(const FlagRange&) const = default;
    };
    std::optional<FlagRange> cartridge_flags{};
    // The reset code's LD SP,nn, and RETN.
    bool reset_stack=false;
    bool return_from_nmi=false;
    // The last instruction may finish in the next frame: dialogue LD BC,nn,
    // or an opted-in scene LDIR slice (Machine::block_copy_frame_cycles).
    bool native_frame_tail=false;
    // Exact slot-2 bank store before masking interrupts for the dialogue upload.
    bool window_bank_store=false;
    // Number of consecutive OUTI instructions ending in this last write.
    unsigned sprite_outputs=0;
    struct MapperImmediate {
        std::uint16_t address;
        std::uint8_t value;
        bool operator==(const MapperImmediate&) const = default;
    };
    std::optional<MapperImmediate> mapper_immediate{};
    // An exit is identified by kind and address; the other fields are its
    // properties. same_declaration compares every field.
    bool operator==(const SegmentExit& other) const { return kind==other.kind && address==other.address; }
    bool same_declaration(const SegmentExit& other) const {
        return *this==other && stack==other.stack && conditional==other.conditional && after==other.after &&
               interrupt_mask==other.interrupt_mask && cartridge_control==other.cartridge_control &&
               wram_seed==other.wram_seed && seed_target==other.seed_target && cartridge_flags==other.cartridge_flags &&
               reset_stack==other.reset_stack && return_from_nmi==other.return_from_nmi &&
               native_frame_tail==other.native_frame_tail && window_bank_store==other.window_bank_store &&
               sprite_outputs==other.sprite_outputs &&
               mapper_immediate==other.mapper_immediate;
    }
};
// A segment of the rewritten routine `unit`. An empty exit list (only from the
// three-argument add) cannot end a call: every body names a declared exit.
// single_instruction: the segment is exactly its final instruction at the
// entry (no instruction boundary inside it).
inline constexpr unsigned max_scratch_stack=16;
struct SegmentSpec {
    RoutineEntry unit;
    std::vector<SegmentExit> exits;
    bool slot2_mapper=false;
    unsigned scratch_stack=0;
    bool single_instruction=false;
    // Bytes above the entry SP that belong to the unit (pushed by an earlier
    // segment): even, at most max_inherited_stack except the fixed IRQ return. Dispatch refuses the call
    // when any of them is unmapped or on the mapper registers.
    unsigned inherited_stack=0;
    // The host observes this entry before admission. After a redirect the
    // segment at the destination runs, with no second hook call.
    bool observe_entry=false;
};

struct RegisteredRoutine;
// Thrown through suspended bodies when an interrupt handler running at depth
// reaches the reset code (00:0000 or 00:0084, whose next instruction loads
// SP): the interrupted bodies are never resumed. Deliberately not derived
// from std::exception, so that no `catch (const std::exception&)` swallows
// it; only the top-level Machine::step catches it.
struct RoutineAbandoned {};

// A capability for one call of a segment's body. Bus operations must follow the
// original path's order. Port offsets are absolute offsets from this call's
// entry cycle, in the body's own time (see elapsed()).
class RoutineContext {
public:
    // A body keeping registers outside the register file registers one of
    // these for the time it runs. At an instruction boundary inside the body
    // the machine stores them into the register file (for an observer or an
    // interrupt handler) and loads them back after a nested delivery.
    class RegisterSync {
    public:
        virtual void store(Z80& cpu) const = 0;
        virtual void load(const Z80& cpu) = 0;
        // The body's copy of Q, if it keeps one: the context clears it at
        // every opcode fetch (an instruction that writes the flags sets it to
        // F afterwards). Without one, the register file's Q is cleared.
        virtual std::uint8_t* q_register() { return nullptr; }
    protected:
        ~RegisterSync()=default;
    };
    // Registration for one scope; unregistered on every exit, the
    // abandonment of RoutineAbandoned included.
    class SyncScope {
    public:
        SyncScope(RoutineContext& context, RegisterSync& sync);
        ~SyncScope();
        SyncScope(const SyncScope&)=delete;
        SyncScope& operator=(const SyncScope&)=delete;
    private:
        RoutineContext& context_;
        RegisterSync* previous_;
    };
    RoutineContext(const RoutineContext&)=delete;
    RoutineContext& operator=(const RoutineContext&)=delete;
    Z80& cpu();
    // Cycles of the body's own instructions since entry: the time of an
    // interrupt handler delivered inside the body is not included, so this is
    // the same count the estimating dry run of the bound makes.
    std::uint64_t elapsed() const;
    void advance(std::uint64_t cycles);
    std::uint8_t read(std::uint16_t address);
    void write(std::uint16_t address, std::uint8_t value);
    // An opcode fetch. The first fetch after time advanced begins a new
    // instruction at `address`: an instruction boundary (3.3 of the
    // milestone 14 plan), where an exact body takes interrupts and observers.
    std::uint8_t fetch_opcode(std::uint16_t address);
    // The previous instruction's Q as the current instruction's first opcode
    // fetch found it, after the boundary before it (where a delivered handler
    // leaves its own last Q), before the fetch cleared it: what CCF and SCF
    // read (review R1 M-1).
    std::uint8_t previous_q() const { return previous_q_; }
    void account_fetches(std::uint64_t count);
    std::uint8_t read_port(std::uint16_t port, std::uint64_t offset);
    std::uint8_t input_increment(std::uint16_t port, std::uint16_t destination);
    void write_port(std::uint16_t port, std::uint8_t value, std::uint64_t offset);
    // The end of the call: the body rendered its final instruction (the
    // declared exit `exit`, identified by kind and address) and the machine
    // continues at `next_pc`. The registers must already be stored in the
    // register file.
    void finish(const SegmentExit& exit, std::uint16_t next_pc);
    // The test-side opcode check of a renderer (see OpcodePattern); a no-op
    // unless a check is installed.
    // Inline and free in the program, which installs no check.
    void expect_opcode(std::uint16_t address, OpcodePattern pattern) { if (checking_opcodes_) check_opcode(address,pattern); }
    // Interrupt state changed by a rendered instruction, as the pinned core
    // does it: EI and DI (IFF1 = IFF2 = enable; the request word follows the
    // sampled INT line), and RETN (IFF1 = IFF2, the IFF1 before it kept for
    // the arbitration of the next boundary).
    void set_interrupts(bool enable);
    void return_from_nmi();
    // An explicit in-body guard that does not hold: throws GuardFailure.
    [[noreturn]] void guard_failed(std::string_view what) const;
    // The slice bound is fixed before the body changes BC, memory or time.
    unsigned copy_count() const { return static_cast<unsigned>((bound_+20)/21); }
    const std::optional<SegmentExit>& last_exit() const { return exit_; }
    // Whether this call takes interrupts and frame ends inside its body.
    bool exact() const { return exact_; }
private:
    friend class Machine;
    RoutineContext(Machine& machine, RegisteredRoutine& routine, std::uint64_t bound, bool exact);
    void active() const;
    void bus() const;
    void at_offset(std::uint64_t offset);
    void check_bound() const;
    // The longest a body may run (its own time) once a delivery released it
    // from its estimated bound: the bound plus two frames.
    std::uint64_t runaway_cap() const { return bound_+2*228*262; }
    Machine& machine_;
    RegisteredRoutine& routine_;
    const SegmentSpec& spec_;
    const std::uint64_t entry_, bound_;
    const bool exact_;
    bool finished_=false, irq_mapper_written_=false, delivered_=false;
    std::uint64_t instruction_start_, nested_cycles_=0;
    unsigned instruction_fetches_=0;
    // The address of the instruction being rendered, and the check that a
    // write to a mapper register or the memory control port did not remap
    // the memory holding it: the original fetches its next instructions from
    // the new mapping, which a rendered body cannot follow (the run stops).
    // Finding 3 of milestone 14: the mapping before the body's first write of
    // a mapper register or the memory control port, for the five regions of
    // the address space (00-03FF, 0400-3FFF, the three slots above). A later
    // instruction of the same body fetched where the mapping changed would
    // come, on the original, from the new mapping: the run stops instead.
    std::optional<std::array<Memory::Mapping,5>> before_remap_;
    void note_remap();
    void code_stays_mapped(std::uint16_t address) const;
    RegisterSync* sync_=nullptr;
    std::uint8_t* q_=nullptr;
    std::uint8_t previous_q_=0;
    const std::uint16_t entry_pc_,entry_sp_;
    const unsigned entry_bank_;
    std::optional<SegmentExit> exit_;
    // Whether a test-side opcode check was installed when the call began.
    const bool checking_opcodes_;
    void check_opcode(std::uint16_t address, OpcodePattern pattern);
};

// The register sync of a body keeping `R` registers, stored and loaded with
// the body's own `Store(Z80&,const R&)` and `R Load(const Z80&)`.
template<class R, auto Store, auto Load> class RegisterSyncOf final : public RoutineContext::RegisterSync {
public:
    explicit RegisterSyncOf(R& registers) : registers_(registers) {}
    void store(Z80& cpu) const override { Store(cpu,registers_); }
    void load(const Z80& cpu) override { registers_=Load(cpu); }
    std::uint8_t* q_register() override {
        if constexpr (requires { registers_.q; }) return &registers_.q; else return nullptr;
    }
private:
    R& registers_;
};

// An explicit entry guard (milestone 14 review R2): the machine states the
// body renders exactly. A state outside them stops the run (UnhandledState,
// reason guard, counted) with a message naming the segment and `name`. The
// predicate must only read the machine.
struct EntryGuard {
    std::string_view name;
    std::function<bool(const Machine&)> holds;
};

struct RegisteredRoutine {
    using Bound = std::function<std::uint64_t(const Machine&)>;
    using Body = std::function<void(RoutineContext&)>;
    Bound bound;
    Body body;
    SegmentSpec spec;
    RoutineCounters counters;
    // An exact body registers a RoutineContext::RegisterSync for its whole
    // run, so interrupts and frame ends are taken at its instruction
    // boundaries; its admission ignores the irq, nmi, frame (except an
    // uncertain bound) and ei_delay windows. Single-instruction segments
    // are exact too: they have no boundary inside.
    bool exact=false;
    // The exit the last completed call took (test instrumentation).
    std::optional<SegmentExit> last_exit;
    // Calls that ended at each declared exit (RoutineContext::finish), by
    // index in spec.exits.
    std::vector<std::uint64_t> rendered{};
    // The explicit entry guard, if the segment has one.
    std::optional<EntryGuard> guard{};
    // The source file that registered the segment (tooling: conversion
    // sheets, ownership, the per-file census). Not part of the behaviour.
    std::string_view origin{};
};

// Machine-wide census of one registry's runs (milestone 14): how the engine
// meets interrupts and frame ends.
struct TransitionCensus {
    // Interrupts accepted at a top-level boundary, at a boundary of a nested
    // handler between segments, or inside a body (also per segment).
    std::uint64_t top_irq=0, top_nmi=0, nested_irq=0, nested_nmi=0, body_irq=0, body_nmi=0;
    std::uint64_t abandoned=0, frames=0;
    // Steps the test scaffolding ran (Machine::set_scaffold_step): always 0
    // in the program, which has none.
    std::uint64_t scaffold_steps=0;
    // Longest top-level step (nested handlers included) and the largest
    // distance by which run_frame returned after its frame target.
    std::uint64_t max_step=0, max_overshoot=0;
    RoutineEntry max_step_entry{}, max_overshoot_entry{};
};

// Host-owned instrumentation: deliberately absent from Machine and its copies.
class RoutineRegistry {
public:
    RoutineRegistry()=default;
    RoutineRegistry(const RoutineRegistry& other);
    RoutineRegistry& operator=(const RoutineRegistry& other);
    RoutineRegistry(RoutineRegistry&& other) noexcept;
    // A rebuild while a body is suspended (pinned) keeps the old entries alive
    // until the outermost dispatch ends.
    RoutineRegistry& operator=(RoutineRegistry&& other) noexcept;
    // `where` records the registering source file (RegisteredRoutine::origin).
    void add(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body,
             std::source_location where=std::source_location::current());
    // A body that registers a RoutineContext::RegisterSync (the kits' run()).
    void add_exact(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body,
                   std::source_location where=std::source_location::current());
    // The same with an explicit entry guard (see EntryGuard).
    void add_exact(RoutineEntry entry, SegmentSpec spec, EntryGuard guard, RegisteredRoutine::Bound bound,
                   RegisteredRoutine::Body body, std::source_location where=std::source_location::current());
    bool empty() const { return routines_.empty(); }
    RegisteredRoutine* find(RoutineEntry entry);
    const RegisteredRoutine* find(RoutineEntry entry) const;
    const auto& entries() const { return routines_; }
    bool may_observe(std::uint16_t pc) const { return observed_pcs_[pc]; }
    TransitionCensus& census() { return census_; }
    const TransitionCensus& census() const { return census_; }
    // Changes whenever the set of routines changes (add, assignment).
    std::uint64_t generation() const { return generation_; }
private:
    friend class Machine;
    void insert(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body, bool exact,
                std::optional<EntryGuard> guard, std::source_location where);
    void validate(RoutineEntry entry, const SegmentSpec& spec) const;
    void pin() { ++pinned_; }
    void unpin() { if (!--pinned_) retired_.clear(); }
    std::map<RoutineEntry,RegisteredRoutine> routines_;
    std::bitset<65536> observed_pcs_;
    TransitionCensus census_;
    std::uint64_t generation_=0;
    unsigned pinned_=0;
    std::vector<std::map<RoutineEntry,RegisteredRoutine>> retired_;
};
}
