// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <bitset>
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
    halted, unmapped_stack, mapper_write, count };
inline constexpr std::array<std::string_view,static_cast<unsigned>(RoutineFallback::count)>
    routine_fallback_names{"disabled","irq","nmi","frame","ei_delay","interrupt_delay","halted","unmapped_stack","mapper_write"};
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
// last instruction: CALL nn, CALL cc,nn, JP nn, JP (HL), RST 08, or an unprefixed instruction
// without I/O, memory write or control transfer that is not HALT, EI or DI;
// at a conditional exit only JP cc,nn, JR cc,e or DJNZ. RST 08 requires
// ordinary admission and scratch_stack covering its two pushed bytes too.
// JP (HL) requires ordinary admission and preserves SP.
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
// Interrupt-mask changes can terminate an ordinary segment. The core samples
// the lines and owns IFF1, IFF2 and the one-instruction delay following EI.
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
    // Execute a final LD (FFFC),A on the core to select cartridge RAM.
    bool cartridge_control=false;
    std::optional<std::uint16_t> wram_seed{};
    SeedTarget seed_target=SeedTarget::work_ram;
    // LD (HL),1 publishes a save in a consecutive range of cartridge flags.
    struct FlagRange {
        std::uint16_t first,last;
        bool operator==(const FlagRange&) const = default;
    };
    std::optional<FlagRange> cartridge_flags{};
    // Narrow reset-only native operations, with exact declared operands.
    bool reset_stack=false;
    bool return_from_nmi=false;
    // The final native load may finish in the next frame: dialogue LD BC,nn,
    // or an opted-in scene LDIR after at least one complete C++ transfer.
    bool native_frame_tail=false;
    // Exact slot-2 bank store before masking interrupts for the dialogue upload.
    bool window_bank_store=false;
    // Number of consecutive OUTI instructions ending in this native write.
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
    // A single original LD (nn),HL may be executed by the core when both
    // bytes are mapped work RAM at this exact address. No other store is
    // allowed. If RAM is disabled the forced body is rejected before the
    // step and the unchanged original instruction runs natively.
    std::optional<std::uint16_t> absolute_wram_store=std::nullopt;
    // Bytes above the entry SP that belong to the unit (pushed by an earlier
    // segment): even, at most max_inherited_stack except the fixed IRQ return. Dispatch falls back when
    // any of them is unmapped or on the mapper registers.
    unsigned inherited_stack=0;
    // The host observes this entry before admission. A redirect executes
    // the destination's first original instruction with the same hook call.
    bool observe_entry=false;
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
    std::uint8_t input_increment(std::uint16_t port, std::uint16_t destination);
    void write_port(std::uint16_t port, std::uint8_t value, std::uint64_t offset);
    // Execute the original path's returning instruction through the core.
    // This is the final operation, and its cycles/fetches are included.
    void finish_return(std::uint16_t address);
    // Execute a declared core_final instruction through the core, likewise.
    void finish_core(std::uint16_t address);
    void finish_copy(std::uint16_t address);
    void finish_output(std::uint16_t address);
    // The slice bound is fixed before the body changes BC, memory or time.
    unsigned copy_count() const { return static_cast<unsigned>((bound_+20)/21); }
    const std::optional<SegmentExit>& last_exit() const { return exit_; }
private:
    friend class Machine;
    RoutineContext(Machine& machine, std::uint64_t bound, const SegmentSpec& spec, bool forced);
    void active() const;
    void bus() const;
    void at_offset(std::uint64_t offset);
    void check_bound() const;
    SegmentExit prepare_final(SegmentExit exit);
    void final_step();
    Machine& machine_;
    const SegmentSpec& spec_;
    const std::uint64_t entry_, bound_;
    const std::uint16_t stack_, initial_bc_, initial_de_, initial_hl_;
    const bool forced_;
    bool finished_=false, stepped_=false, irq_mapper_written_=false;
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
    bool may_observe(std::uint16_t pc) const { return observed_pcs_[pc]; }
private:
    void insert(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body);
    std::map<RoutineEntry,RegisteredRoutine> routines_;
    std::bitset<65536> observed_pcs_;
};
}
