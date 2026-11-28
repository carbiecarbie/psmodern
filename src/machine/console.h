// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/memory.h"
#include "hw/cpu.h"
#include "hw/vdp.h"
#include "hw/psg.h"
#include "hw/initial_state.h"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace psmodern {
namespace oracle { struct CoreBus; }
struct RoutineEntry;
struct PortAccess {
    std::uint64_t cycle;
    std::uint8_t port, value;
    bool write;
    bool operator==(const PortAccess&) const = default;
};
// A processor write to the memory map: work RAM, cartridge RAM, the mapper
// registers or a ROM address (which the cartridge ignores).
struct MemoryWrite {
    std::uint16_t address;
    std::uint8_t value;
    bool operator==(const MemoryWrite&) const = default;
};
struct Pad {
    bool up=false, down=false, left=false, right=false, one=false, two=false;
    bool operator==(const Pad&) const = default;
};
struct FrameInput {
    std::array<Pad,2> pads{};
    bool reset=false, pause=false;
};

// The console around the processor: memory, the video and sound chips, the
// controller ports, the clock, the interrupt lines as the processor samples
// them, and the processor's register file. The engine (Machine) and the test
// oracle (oracle::NativeMachine) are both a Console, so the hardware glue
// (ports, sync, the input latch, the Pause and scheduled NMI edges) exists
// once. The register file is the project's own (hw/cpu.h); the program has
// no processor core, and only the test oracle runs one, on these fields.
class Console {
public:
    // Verification instrumentation bound to this console for one scope.
    // Snapshots carry hardware state only. Leaving the scope restores the
    // previous collector, including when the verifier reports a divergence.
    class PortTrace {
    public:
        explicit PortTrace(Console& console);
        ~PortTrace();
        PortTrace(const PortTrace&)=delete;
        PortTrace& operator=(const PortTrace&)=delete;
        const std::vector<PortAccess>& accesses() const { return accesses_; }
        std::vector<PortAccess>& accesses() { return accesses_; }
    private:
        Console& console_;
        std::vector<PortAccess> accesses_;
        std::vector<PortAccess>* previous_;
    };
    // The same for processor memory writes (not host pokes through memory()).
    class WriteTrace {
    public:
        explicit WriteTrace(Console& console);
        ~WriteTrace();
        WriteTrace(const WriteTrace&)=delete;
        WriteTrace& operator=(const WriteTrace&)=delete;
        const std::vector<MemoryWrite>& writes() const { return writes_; }
        std::vector<MemoryWrite>& writes() { return writes_; }
    private:
        Console& console_;
        std::vector<MemoryWrite> writes_;
        std::vector<MemoryWrite>* previous_;
    };
    // A fully prepared immutable image. Construction validates shape before
    // a host may reach its proven instruction boundary; installation cannot
    // allocate, throw or change any other machine field. This is not Reset.
    class RomReplacement {
    public:
        RomReplacement(const RomReplacement&)=default;
        RomReplacement& operator=(const RomReplacement&)=default;
    private:
        friend class Console;
        explicit RomReplacement(std::shared_ptr<const Memory::Rom> image):image_(std::move(image)) {}
        std::shared_ptr<const Memory::Rom> image_;
    };
    static RomReplacement prepare_rom_replacement(std::shared_ptr<const Memory::Rom> image);
    void replace_rom(const RomReplacement& prepared) noexcept { memory_.rom_=prepared.image_; }
    static constexpr std::uint64_t frame_cycles=228*262;
    // MesenCE samples Pause on VDP dot 315 when VCounter reaches 261.
    // With 342 dots/line and 3 master clocks/T versus 2/dot, this is
    // 260*228+210 T. CE observes NMI through the instruction end E;
    // our arbiter samples at E-1, so represent that boundary one T earlier.
    // This is a reference timing convention, not a hardware measurement.
    static constexpr std::uint64_t pause_sample_offset=260*228+209;
    // Read-only host instrumentation, scoped to one frame and never copied
    // into snapshots. Both native and registered-routine writes pass here.
    using WriteObserver = std::function<void(std::uint16_t,std::uint8_t)>;
    // Optional address check for isolated driver calls. Like the tracing
    // hooks it belongs to this console and never follows a snapshot.
    void set_read_guard(void (*guard)(std::uint16_t)) { read_guard_=guard; }
    explicit Console(std::shared_ptr<const Memory::Rom> rom, Memory::CartridgeRam ram = {});
    Console(const Console& other);
    Console& operator=(const Console& other);
    // Time passing on the bus without an opcode fetch: one instruction's
    // interval [S,E) with the interrupt lines sampled at E-1.
    void advance(std::uint64_t cycles);
    std::uint64_t cycles() const { return cycle_; }
    std::uint64_t opcode_fetches() const { return opcode_fetches_; }
    void set_pause(bool level) { pending_pause_=level; }
    std::optional<std::uint64_t> next_nmi() const;
    std::optional<std::uint64_t> next_irq() const { return vdp_.next_irq(); }
    void schedule_nmi(std::uint64_t cycle);
    void set_pad(unsigned index, Pad pad) { pending_pads_.at(index)=pad; }
    void set_reset(bool pressed) { pending_reset_=pressed; }
    // The input the next frame boundaries latch (what run_frame passes on).
    void set_input(const FrameInput& input) {
        pending_pads_=input.pads; pending_reset_=input.reset; pending_pause_=input.pause;
    }
    std::uint8_t read_port(std::uint16_t port, std::uint64_t cycle);
    void write_port(std::uint16_t port, std::uint8_t value, std::uint64_t cycle,
                    const RoutineEntry* writer=nullptr,std::uint16_t entry_sp=0);
    // Read-only presentation tap. A window and a scene may write the same
    // address; the rendered writer identifies which should appear in the
    // clean view. Neither the copy's reads nor its IRQs feed the console.
    class VideoObserver {
    public:
        virtual void video_access(const Console&,PortAccess,const RoutineEntry*,std::uint16_t entry_sp)=0;
    protected:
        ~VideoObserver()=default;
    };
    void set_video_observer(VideoObserver* observer) { video_observer_=observer; }
    Memory& memory() { return memory_; }
    const Memory& memory() const { return memory_; }
    Vdp& vdp() { return vdp_; }
    const Vdp& vdp() const { return vdp_; }
    const Psg& psg() const { return psg_; }
    std::vector<PsgWrite> take_psg_writes() { return psg_.take_writes(); }
    PsgSamples take_samples() { return psg_.take_samples(); }
    const Vdp::Picture& picture() const { return vdp_.picture(); }
    Z80& cpu() { return cpu_; }
    const Z80& cpu() const { return cpu_; }
    // Single bounded probe for timing tests; never retains a history.
    const std::optional<PortAccess>& last_access() const { return last_access_; }
    bool equivalent(const Console& other) const;
    // The sound chip's state without its output queues (the PSG writes and
    // samples a host drains), which differ only by when they were taken.
    static bool same_sound_state(const Psg& a, const Psg& b);
    // Memory without the cartridge RAM's dirty flag (the save store's
    // bookkeeping, cleared by a host flush).
    static bool same_memory(const Memory& a, const Memory& b);
    // The controller ports' latched and pending input and the scheduled NMI.
    bool same_inputs(const Console& other) const;
    // Values are ordered as original (*this), replacement (other).
    std::vector<FieldDifference> differences(const Console& other) const;
    std::uint8_t peek(std::uint16_t address) const;
    // The arbitration the pinned core performs at an instruction boundary
    // (Z80.c 2505-2623 with the RETN bookkeeping of 1141-1146), given the INT
    // level sampled at the previous instruction's E-1. A late NMI edge (not
    // sampled at E-1) is not visible to this boundary.
    enum class Acceptance : std::uint8_t { none, nmi, irq };
    Acceptance pending_acceptance() const;
    bool interrupt_accepted_at_boundary() const { return pending_acceptance()!=Acceptance::none; }
    bool sampled_int() const { return sampled_int_; }
    bool sampled_nmi() const { return sampled_nmi_; }
    bool pause_nmi_level() const { return pause_nmi_level_; }
    std::uint8_t io_control() const { return io_control_; }
    std::uint8_t open_bus() const { return open_bus_; }
protected:
    friend class RoutineContext;
    friend struct oracle::CoreBus;
    void record_access(PortAccess access);
    void sync(std::uint64_t target);
    void latch_input();
    // The end of one instruction or response of `cycles` length on the native
    // path: the lines are sampled at E-1, after any I/O in the interval.
    void finish_interval(std::uint64_t cycles);
    // The processor's bus accesses (an opcode fetch counts toward
    // opcode_fetches; a read of an unmapped address returns the open bus).
    std::uint8_t opcode(std::uint16_t address);
    std::uint8_t read(std::uint16_t address);
    void write(std::uint16_t address, std::uint8_t value);
    Memory memory_;
    Z80 cpu_{};
    Vdp vdp_;
    Psg psg_;
    std::uint64_t cycle_ = initial_state::cpu_cycle;
    std::uint64_t opcode_fetches_ = 0;
    std::optional<std::uint64_t> scheduled_nmi_;
    std::array<Pad, 2> pads_{}, pending_pads_{};
    std::uint8_t io_control_ = initial_state::io_control;
    std::uint8_t open_bus_ = initial_state::open_bus;
    bool reset_ = false, pause_ = false, pending_reset_ = false, pending_pause_ = false;
    bool input_latched_ = false, sampled_int_ = false, sampled_nmi_ = false,
        pause_nmi_level_ = false;
    std::optional<PortAccess> last_access_;
    std::vector<PortAccess>* port_trace_=nullptr;
    std::vector<MemoryWrite>* write_trace_=nullptr;
    const WriteObserver* write_observer_=nullptr;
    VideoObserver* video_observer_=nullptr;
    void (*read_guard_)(std::uint16_t)=nullptr;
};
}
