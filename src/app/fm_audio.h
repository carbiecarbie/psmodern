// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/fm.h"
#include "machine/machine.h"
#include <deque>

namespace psmodern::app {
// A private copy of the Japanese sound unit. The listener clock advances at
// normal speed even when the gameplay machine supplies two or four updates
// per displayed interval. The chip keeps sounding between driver calls.
class FmAudio {
public:
    explicit FmAudio(std::shared_ptr<const Memory::Rom> image);
    void speed(unsigned speed,std::uint64_t source_cycle);
    void begin_tick(std::uint64_t source_cycle);
    void consume(std::uint64_t source_cycle,std::optional<std::uint8_t> external);
    void initialize(std::uint64_t source_cycle);
    void silence(std::uint64_t source_cycle);
    PsgSamples finish(std::uint64_t source_cycle);
private:
    void advance(std::uint64_t source_cycle);
    void call(std::uint16_t entry,std::uint64_t when);
    std::unique_ptr<Machine> engine_;
    RoutineRegistry routines_;
    Z80 caller_{};
    Fm chip_;
    std::deque<PortAccess> writes_;
    std::optional<std::uint64_t> tick_;
    std::uint64_t source_=0,quarters_=0,call_end_=0;
    unsigned speed_=1,tick_quarters_=0;
};
}
