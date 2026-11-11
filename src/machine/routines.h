// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Z80.h>
#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

namespace psmodern {
class Machine;

struct RoutineEntry {
    unsigned bank;
    std::uint16_t address;
    auto operator<=>(const RoutineEntry&) const = default;
};
enum class RoutineFallback : unsigned { disabled, irq, nmi, frame, ei_delay, interrupt_delay,
    halted, unmapped_stack, count };
inline constexpr std::array<std::string_view,static_cast<unsigned>(RoutineFallback::count)>
    routine_fallback_names{"disabled","irq","nmi","frame","ei_delay","interrupt_delay","halted","unmapped_stack"};
// forced_calls counts admitted calls of a force_admit segment (also counted in
// cpp_calls); forced_reasons the window reasons such calls ran through, kept
// apart from fallback reasons. forced_failed counts forced calls which fell back.
struct RoutineCounters {
    std::uint64_t cpp_calls=0, fallback_calls=0, forced_calls=0, forced_failed=0;
    std::array<std::uint64_t,static_cast<unsigned>(RoutineFallback::count)> reasons{}, forced_reasons{};
    std::uint64_t fallback(RoutineFallback reason) const { return reasons.at(static_cast<unsigned>(reason)); }
    std::uint64_t forced(RoutineFallback reason) const { return forced_reasons.at(static_cast<unsigned>(reason)); }
};

// returned: the core executes the returning instruction at address.
// core_final: the core executes the instruction at address as the segment's
// last instruction: CALL nn, CALL cc,nn, JP nn, or an unprefixed instruction
// without I/O, memory write or control transfer that is not HALT, EI or DI.
enum class RoutineExit : unsigned { returned, core_final };
struct SegmentExit {
    RoutineExit kind;
    std::uint16_t address;
    bool operator==(const SegmentExit&) const = default;
};
// A segment of the rewritten routine `unit`. An empty exit list (only from the
// three-argument add) accepts any returning instruction, as in milestone 2.
// single_instruction: the segment is exactly its final instruction at the
// entry; the body may only adjust registers around it. force_admit: window
// reasons never cause fallback (requires single_instruction and the one exit
// core_final at the entry).
inline constexpr unsigned max_scratch_stack=16;
struct SegmentSpec {
    RoutineEntry unit;
    std::vector<SegmentExit> exits;
    bool slot2_mapper=false;
    unsigned scratch_stack=0;
    bool single_instruction=false;
    bool force_admit=false;
};

// A capability for one replacement call. Bus operations must follow the original
// path's order. Port offsets are absolute offsets from this call's entry cycle.
class RoutineContext {
public:
    RoutineContext(const RoutineContext&)=delete;
    RoutineContext& operator=(const RoutineContext&)=delete;
    Z80& cpu();
    std::uint64_t elapsed() const;
    void advance(std::uint64_t cycles);
    std::uint8_t read(std::uint16_t address);
    void write(std::uint16_t address, std::uint8_t value);
    std::uint8_t fetch_opcode(std::uint16_t address);
    void account_fetches(std::uint64_t count);
    std::uint8_t read_port(std::uint16_t port, std::uint64_t offset);
    void write_port(std::uint16_t port, std::uint8_t value, std::uint64_t offset);
    // Execute the original path's returning instruction through the core.
    // This is the final operation, and its cycles/fetches are included.
    void finish_return(std::uint16_t address);
    // Execute a declared core_final instruction through the core, likewise.
    void finish_core(std::uint16_t address);
    const std::optional<SegmentExit>& last_exit() const { return exit_; }
private:
    friend class Machine;
    RoutineContext(Machine& machine, std::uint64_t bound, const SegmentSpec& spec, bool forced);
    void active() const;
    void bus() const;
    void at_offset(std::uint64_t offset);
    void check_bound() const;
    void prepare_final(SegmentExit exit);
    void final_step();
    Machine& machine_;
    const SegmentSpec& spec_;
    const std::uint64_t entry_, bound_;
    const std::uint16_t stack_;
    const bool forced_;
    bool finished_=false, stepped_=false;
    std::optional<SegmentExit> exit_;
};

struct RegisteredRoutine {
    using Bound = std::function<std::uint64_t(const Machine&)>;
    using Body = std::function<void(RoutineContext&)>;
    Bound bound;
    Body body;
    SegmentSpec spec;
    RoutineCounters counters;
};

class RoutineVerifier {
public:
    virtual ~RoutineVerifier()=default;
    // body returns the exit it took, or nothing when a forced call fell back
    // before its core step (the machine is then unchanged and runs natively).
    virtual void run(Machine&, RoutineEntry, const SegmentSpec&, bool forced, std::uint64_t bound,
                     std::uint64_t call, const std::function<std::optional<SegmentExit>()>& body)=0;
};

// Host-owned instrumentation: deliberately absent from Machine and its copies.
class RoutineRegistry {
public:
    // Milestone 2 form: a whole routine (unit = entry) ending in any return.
    void add(RoutineEntry entry, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body);
    void add(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body);
    bool empty() const { return routines_.empty(); }
    bool enabled=true;
    RoutineVerifier* verifier=nullptr;
    RegisteredRoutine* find(RoutineEntry entry);
    const RegisteredRoutine* find(RoutineEntry entry) const;
    const auto& entries() const { return routines_; }
private:
    void insert(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body);
    std::map<RoutineEntry,RegisteredRoutine> routines_;
};
}
