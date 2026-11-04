// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Z80.h>
#include <cstdint>

namespace psmodern {
class CpuBus {
public:
    virtual ~CpuBus() = default;
    virtual std::uint8_t opcode(std::uint16_t address) = 0;
    virtual std::uint8_t read(std::uint16_t address) = 0;
    virtual void write(std::uint16_t address, std::uint8_t value) = 0;
    virtual std::uint8_t input(std::uint16_t port, std::uint64_t cycle) = 0;
    virtual void output(std::uint16_t port, std::uint8_t value, std::uint64_t cycle) = 0;
};
class Cpu {
public:
    explicit Cpu(CpuBus& bus);
    Cpu(const Cpu& other);
    Cpu& operator=(const Cpu& other);
    void bind(CpuBus& bus);
    Z80& raw() { return state_; }
    const Z80& raw() const { return state_; }
    std::uint64_t step(std::uint64_t start_cycle);
    bool equivalent(const Cpu& other) const;
private:
    static zuint8 opcode(void*, zuint16);
    static zuint8 read(void*, zuint16);
    static void write(void*, zuint16, zuint8);
    static zuint8 input(void*, zuint16);
    static void output(void*, zuint16, zuint8);
    Z80 state_{};
    CpuBus* bus_;
    std::uint64_t start_cycle_ = 0;
};
}
