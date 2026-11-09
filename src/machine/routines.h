// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Z80.h>
#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <map>

namespace psmodern {
class Machine;

struct RoutineEntry {
    unsigned bank;
    std::uint16_t address;
    auto operator<=>(const RoutineEntry&) const = default;
};
enum class RoutineFallback : unsigned { disabled, irq, nmi, frame, ei_delay, interrupt_delay,
    halted, unmapped_stack, count };
struct RoutineCounters {
    std::uint64_t cpp_calls=0, fallback_calls=0;
    std::array<std::uint64_t,static_cast<unsigned>(RoutineFallback::count)> reasons{};
    std::uint64_t fallback(RoutineFallback reason) const { return reasons.at(static_cast<unsigned>(reason)); }
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
private:
    friend class Machine;
    RoutineContext(Machine& machine, std::uint64_t bound);
    void active() const;
    void at_offset(std::uint64_t offset);
    void check_bound() const;
    Machine& machine_;
    const std::uint64_t entry_, bound_;
    const std::uint16_t stack_;
    bool finished_=false;
};

struct RegisteredRoutine {
    using Bound = std::function<std::uint64_t(const Machine&)>;
    using Body = std::function<void(RoutineContext&)>;
    Bound bound;
    Body body;
    RoutineCounters counters;
};

class RoutineVerifier {
public:
    virtual ~RoutineVerifier()=default;
    virtual void run(Machine&, RoutineEntry, std::uint64_t bound, std::uint64_t call,
                     const std::function<void()>& body)=0;
};

// Host-owned instrumentation: deliberately absent from Machine and its copies.
class RoutineRegistry {
public:
    void add(RoutineEntry entry, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body);
    bool empty() const { return routines_.empty(); }
    bool enabled=true;
    RoutineVerifier* verifier=nullptr;
    RegisteredRoutine* find(RoutineEntry entry);
    const RegisteredRoutine* find(RoutineEntry entry) const;
private:
    std::map<RoutineEntry,RegisteredRoutine> routines_;
};
}
