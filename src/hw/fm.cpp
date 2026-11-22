// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/fm.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

extern "C" {
#include "opll.h"
}

namespace psmodern {
namespace {
constexpr std::uint64_t slot_cycles = 4;
constexpr auto slots_per_sample = Fm::sample_cycles/slot_cycles;
// The YM2413's two multiplexed outputs each code silence as +1 per slot.
constexpr std::int32_t idle_slot = 2;
constexpr auto idle_sum = static_cast<std::int32_t>(idle_slot*slots_per_sample);
constexpr std::int32_t gain = 16;
}

struct Fm::Chip {
    opll_t state;
};

Fm::Fm(bool log_writes) : chip_(std::make_unique<Chip>()), log_writes_(log_writes) {
    OPLL_Reset(&chip_->state,opll_type_ym2413);
    // The first clock returns the reset output (zero) before the idle codes.
    mixed_ = idle_slot;
}

Fm::~Fm() = default;

void Fm::sync(std::uint64_t target) {
    if (target<cycle_) throw std::invalid_argument("FM chip cannot run backwards");
    const auto samples_due = target/sample_cycles-cycle_/sample_cycles;
    if (samples_due>max_samples-samples_.values.size())
        throw std::overflow_error("FM sample queue full: drain samples every frame");

    auto slots_due = target/slot_cycles-cycle_/slot_cycles;
    auto slots_in_sample = static_cast<unsigned>((cycle_%sample_cycles)/slot_cycles);
    while (slots_due--) {
        std::int32_t outputs[2];
        OPLL_Clock(&chip_->state,outputs);
        mixed_ += outputs[0]+outputs[1];
        if (++slots_in_sample==slots_per_sample) {
            const auto sample = (mixed_-idle_sum)*gain;
            samples_.values.push_back(static_cast<std::int16_t>(std::clamp<std::int32_t>(
                sample,std::numeric_limits<std::int16_t>::min(),std::numeric_limits<std::int16_t>::max())));
            mixed_ = 0;
            slots_in_sample = 0;
        }
    }
    cycle_ = target;
}

void Fm::write(std::uint8_t port,std::uint8_t value,std::uint64_t cycle) {
    if (port!=0xf0 && port!=0xf1) throw std::invalid_argument("Invalid YM2413 port");
    if (log_writes_ && writes_.size()==max_writes)
        throw std::overflow_error("FM write log full: clear the log after each capture");
    // Like the PSG, any clock edge at the bus cycle precedes the write.
    sync(cycle);
    if (log_writes_) writes_.push_back({cycle,port,value});
    OPLL_Write(&chip_->state,port&1,value);
}

PsgSamples Fm::take_samples() {
    return std::exchange(samples_,PsgSamples{(cycle_/sample_cycles+1)*sample_cycles,{}});
}
}
