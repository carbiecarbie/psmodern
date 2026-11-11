// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/machine.h"
#include <deque>

namespace psmodern::app {
// Optional presentation-only sound timeline. The gameplay machine is never
// written. Entries execute from the player's identified ROM, not copied code.
class TempoAudio : private CpuBus {
public:
    // Capture at 0C:8043, before the native update starts. The current update
    // (and its pending request) is executed once here; do not relay it again.
    explicit TempoAudio(const Machine& source);
    void speed(unsigned value);
    void update(std::uint64_t source_cycle,std::uint8_t request);
    void request(std::uint64_t source_cycle,std::uint8_t request);
    void initialize(std::uint64_t source_cycle);
    void silence(std::uint64_t source_cycle);
    PsgSamples finish(std::uint64_t source_cycle);
    const Memory& memory() const { return memory_; }
    std::uint64_t ticks() const { return ticks_; }
private:
    void move_to(std::uint64_t source_cycle);
    void call(std::uint16_t entry);
    void collect();
    std::uint8_t opcode(std::uint16_t address) override;
    std::uint8_t read(std::uint16_t address) override;
    void write(std::uint16_t address,std::uint8_t value) override;
    std::uint8_t input(std::uint16_t,std::uint64_t) override;
    void output(std::uint16_t port,std::uint8_t value,std::uint64_t cycle) override;
    Memory memory_;
    Cpu cpu_;
    Psg psg_;
    std::uint64_t cycle_,source_cycle_,quarter_cycle_,first_sample_,ticks_=0;
    unsigned speed_=1,quarter_ticks_=0;
    std::deque<std::int16_t> samples_;
};
}
