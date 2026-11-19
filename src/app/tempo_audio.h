// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/machine.h"
#include <deque>
#include <memory>

namespace psmodern::app {
// Optional presentation-only sound timeline. The gameplay machine is never
// written. The sound driver's C++ segments run on a private engine (its own
// memory copied from the game, its own sound chip and its own segment
// registry, so the game's counters and census never see these calls); the
// sound they make is replayed on a separate sound chip that follows the
// presentation clock.
class TempoAudio {
public:
    // Capture at 0C:8043, before the game's own update starts. The current
    // update (and its pending request) is executed once here; do not relay it
    // again. Reads the source only: it may be called inside a nested delivery.
    explicit TempoAudio(const Machine& source);
    TempoAudio(const TempoAudio&)=delete;
    TempoAudio& operator=(const TempoAudio&)=delete;
    void speed(unsigned value);
    void update(std::uint64_t source_cycle,std::uint8_t request);
    void request(std::uint64_t source_cycle,std::uint8_t request);
    void initialize(std::uint64_t source_cycle);
    void silence(std::uint64_t source_cycle);
    PsgSamples finish(std::uint64_t source_cycle);
    // The private engine's memory (the sound records live in C000-C1FF).
    const Memory& memory() const { return engine_->memory(); }
    std::uint64_t ticks() const { return ticks_; }
    // The private engine's registry: its own call counters and transition
    // census (tests check that the driver ran as C++ segments).
    const RoutineRegistry& routines() const { return routines_; }
    // Where every call returns: a work RAM address, so never a segment entry
    // (segments begin in ROM) and never executed (the call ends before it).
    static constexpr std::uint16_t call_return=0xdff0;
private:
    void move_to(std::uint64_t source_cycle);
    void call(std::uint16_t entry);
    void collect();
    std::unique_ptr<Machine> engine_;
    RoutineRegistry routines_;
    // The register file of the freshly built engine: every call starts from
    // it, as every call of the driver did before milestone 14.
    Z80 fresh_{};
    Psg psg_;
    std::uint64_t cycle_,source_cycle_,quarter_cycle_,first_sample_,ticks_=0;
    unsigned speed_=1,quarter_ticks_=0;
    std::deque<std::int16_t> samples_;
};
}
