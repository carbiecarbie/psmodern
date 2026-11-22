// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/psg.h"
#include <memory>

namespace psmodern {
struct FmWrite {
    std::uint64_t cycle;
    std::uint8_t port, value;
    bool operator==(const FmWrite&) const = default;
};

// The Japanese Master System's YM2413, clocked by the NTSC CPU clock.
class Fm {
public:
    static constexpr std::uint64_t sample_cycles = 72, cpu_hz = Psg::cpu_hz;
    static constexpr std::size_t max_samples = 16384, max_writes = 8192;

    explicit Fm(bool log_writes = false);
    ~Fm();

    void sync(std::uint64_t cycle);
    void write(std::uint8_t port, std::uint8_t value, std::uint64_t cycle);
    PsgSamples take_samples();
    std::uint64_t cycle() const { return cycle_; }
    const std::vector<FmWrite>& writes() const { return writes_; }
    void clear_writes() { writes_.clear(); }

private:
    struct Chip;
    std::unique_ptr<Chip> chip_;
    bool log_writes_;
    std::uint64_t cycle_ = 0;
    std::int32_t mixed_ = 0;
    std::vector<FmWrite> writes_;
    PsgSamples samples_{sample_cycles,{}};
};
}
