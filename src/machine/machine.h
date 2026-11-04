// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/memory.h"
#include "hw/cpu.h"
#include <Z80.h>
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace psmodern {
struct PortAccess {
    std::uint64_t cycle;
    std::uint8_t port, value;
    bool write;
    bool operator==(const PortAccess&) const = default;
};
struct Pad {
    bool up=false, down=false, left=false, right=false, one=false, two=false;
    bool operator==(const Pad&) const = default;
};
class Machine : private CpuBus {
public:
    using Hook = std::function<std::optional<std::uint16_t>(std::uint16_t, Memory::Mapping)>;
    explicit Machine(std::shared_ptr<const Memory::Rom> rom, Memory::CartridgeRam ram = {});
    Machine(const Machine& other);
    Machine& operator=(const Machine& other);
    std::uint64_t step(const Hook& hook = {});
    void advance(std::uint64_t cycles) { cycle_ += cycles; deliver_nmi(); }
    std::uint64_t cycles() const { return cycle_; }
    std::uint64_t opcode_fetches() const { return opcode_fetches_; }
    void set_int(bool level);
    void set_pause(bool level);
    std::optional<std::uint64_t> next_nmi() const;
    void schedule_nmi(std::uint64_t cycle);
    void set_pad(unsigned index, Pad pad) { pads_.at(index) = pad; }
    void set_reset(bool pressed) { reset_ = pressed; }
    std::uint8_t read_port(std::uint16_t port, std::uint64_t cycle);
    void write_port(std::uint16_t port, std::uint8_t value, std::uint64_t cycle);
    Memory& memory() { return memory_; }
    const Memory& memory() const { return memory_; }
    Z80& cpu() { return cpu_.raw(); }
    const Z80& cpu() const { return cpu_.raw(); }
    const std::vector<PortAccess>& accesses() const { return accesses_; }
    bool equivalent(const Machine& other) const;
private:
    void bind();
    void deliver_nmi();
    std::uint8_t opcode(std::uint16_t address) override;
    std::uint8_t read(std::uint16_t address) override;
    void write(std::uint16_t address, std::uint8_t value) override;
    std::uint8_t input(std::uint16_t port, std::uint64_t cycle) override;
    void output(std::uint16_t port, std::uint8_t value, std::uint64_t cycle) override;
    Memory memory_;
    Cpu cpu_;
    std::uint64_t cycle_ = 0;
    std::uint64_t opcode_fetches_ = 0;
    std::optional<std::uint64_t> scheduled_nmi_;
    std::array<Pad, 2> pads_{};
    std::uint8_t io_control_ = 0x0f;
    std::uint8_t open_bus_ = 0;
    bool reset_ = false, pause_ = false, int_ = false;
    std::vector<PortAccess> accesses_;
};
}
