// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/initial_state.h"
#include <cstddef>
#include <vector>

namespace psmodern {
struct PsgWrite {
    std::uint64_t cycle;
    std::uint8_t byte;
    bool operator==(const PsgWrite&) const = default;
};
struct PsgSamples {
    std::uint64_t first_cycle;
    std::vector<std::int16_t> values;
    bool operator==(const PsgSamples&) const = default;
};
// Sega's integrated SN76489. Hardware documentation: Maxim's SN76489 notes,
// https://www.smspower.org/Development/SN76489; decisions in development.md.
class Psg {
public:
    static constexpr std::uint64_t frame_cycles = 228*262;
    static constexpr std::uint64_t sample_cycles = 16, cpu_hz = 3579545;
    static constexpr std::size_t max_writes = 8192;
    static constexpr std::size_t max_samples = 16384;
    // round(8191 * 10^(-attenuation/10)); attenuation 15 is disconnected.
    static constexpr std::array<std::int16_t,16> volume_table{
        8191,6506,5168,4105,3261,2590,2057,1634,1298,1031,819,651,517,411,326,0};
    void sync(std::uint64_t cycle);
    void write(std::uint8_t byte, std::uint64_t cycle);
    // Preflight before Machine advances any other device for this access.
    void check_write(std::uint64_t cycle) const;
    void check_sync(std::uint64_t cycle) const;
    const auto& writes() const { return writes_; }
    std::vector<PsgWrite> take_writes();
    PsgSamples take_samples();
    std::uint64_t cycle() const { return cycle_; }
    const auto& periods() const { return periods_; }
    const auto& timers() const { return timers_; }
    const auto& outputs() const { return outputs_; }
    const auto& attenuations() const { return attenuations_; }
    std::uint8_t latch() const { return latch_; }
    std::uint8_t noise_control() const { return noise_control_; }
    std::uint16_t noise_timer() const { return noise_timer_; }
    std::uint16_t lfsr() const { return lfsr_; }
    bool noise_clock() const { return noise_clock_; }
    bool noise_output() const { return noise_output_; }
    std::int16_t sample() const;
    bool operator==(const Psg&) const = default;
private:
    void apply(std::uint8_t byte);
    void tick();
    std::array<std::uint16_t,3> periods_ = initial_state::psg_periods;
    std::array<std::uint16_t,3> timers_ = initial_state::psg_timers;
    std::array<bool,3> outputs_ = initial_state::psg_outputs;
    std::array<std::uint8_t,4> attenuations_ = initial_state::psg_attenuations;
    std::uint16_t lfsr_ = initial_state::psg_lfsr, noise_timer_ = initial_state::psg_noise_timer;
    std::uint8_t latch_ = initial_state::psg_latch, noise_control_ = initial_state::psg_noise_control;
    bool noise_clock_ = initial_state::psg_noise_clock, noise_output_ = initial_state::psg_noise_output;
    std::uint64_t cycle_ = initial_state::cpu_cycle;
    std::uint64_t write_frame_ = 0;
    std::size_t frame_write_count_ = 0;
    std::vector<PsgWrite> writes_;
    PsgSamples samples_{sample_cycles,{}};
};
}
