// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/memory.h"
#include "hw/cpu.h"
#include "hw/vdp.h"
#include <Z80.h>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>

namespace psmodern {
#ifdef PSMODERN_TEST_PROBES
struct PortAccess {
    std::uint64_t cycle;
    std::uint8_t port, value;
    bool write;
    bool operator==(const PortAccess&) const = default;
};
#endif
struct Pad {
    bool up=false, down=false, left=false, right=false, one=false, two=false;
    bool operator==(const Pad&) const = default;
};
class Machine : private CpuBus {
public:
    static constexpr std::uint64_t frame_cycles=228*262;
    static constexpr std::uint64_t pause_sample_offset=260*228+212;
    using Hook = std::function<std::optional<std::uint16_t>(std::uint16_t, Memory::Mapping)>;
    explicit Machine(std::shared_ptr<const Memory::Rom> rom, Memory::CartridgeRam ram = {});
    Machine(const Machine& other);
    Machine& operator=(const Machine& other);
    std::uint64_t step(const Hook& hook = {});
    void advance(std::uint64_t cycles);
    std::uint64_t run_frame();
    std::uint64_t cycles() const { return cycle_; }
    std::uint64_t opcode_fetches() const { return opcode_fetches_; }
    void set_pause(bool level) { pending_pause_=level; }
    std::optional<std::uint64_t> next_nmi() const;
    std::optional<std::uint64_t> next_irq() const { return vdp_.next_irq(); }
    void schedule_nmi(std::uint64_t cycle);
    void set_pad(unsigned index, Pad pad) { pending_pads_.at(index)=pad; }
    void set_reset(bool pressed) { pending_reset_=pressed; }
    std::uint8_t read_port(std::uint16_t port, std::uint64_t cycle);
    void write_port(std::uint16_t port, std::uint8_t value, std::uint64_t cycle);
    Memory& memory() { return memory_; }
    const Memory& memory() const { return memory_; }
    Vdp& vdp() { return vdp_; }
    const Vdp& vdp() const { return vdp_; }
    Z80& cpu() { return cpu_.raw(); }
    const Z80& cpu() const { return cpu_.raw(); }
#ifdef PSMODERN_TEST_PROBES
    // Single bounded probe for timing tests; never retains a history.
    const std::optional<PortAccess>& last_access() const { return last_access_; }
#endif
    bool equivalent(const Machine& other) const;
private:
    void bind();
    void sync(std::uint64_t target);
    void latch_input();
    void deliver_nmi();
    std::uint8_t opcode(std::uint16_t address) override;
    std::uint8_t read(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t input(std::uint16_t port, std::uint64_t cycle) override;
    void output(std::uint16_t port, std::uint8_t value, std::uint64_t cycle) override;
    Memory memory_;
    Cpu cpu_;
    Vdp vdp_;
    std::uint64_t cycle_ = initial_state::cpu_cycle;
    std::uint64_t opcode_fetches_ = 0;
    std::optional<std::uint64_t> scheduled_nmi_;
    std::array<Pad, 2> pads_{}, pending_pads_{};
    std::uint8_t io_control_ = initial_state::io_control;
    std::uint8_t open_bus_ = initial_state::open_bus;
    bool reset_ = false, pause_ = false, pending_reset_ = false, pending_pause_ = false;
    bool input_latched_ = false, sampled_int_ = false, pause_nmi_level_ = false;
#ifdef PSMODERN_TEST_PROBES
    std::optional<PortAccess> last_access_;
#endif
};
}
