// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/psg.h"
#include <stdexcept>
#include <utility>

namespace psmodern {
void Psg::check_write(std::uint64_t cycle) const {
    if (cycle<cycle_) throw std::invalid_argument("PSG cannot run backwards");
    if (writes_.size()==max_writes ||
        (cycle/frame_cycles==write_frame_ && frame_write_count_==max_writes))
        throw std::overflow_error("PSG write log full: drain output and respect the per-frame limit");
}
void Psg::write(std::uint8_t byte,std::uint64_t cycle) {
    check_write(cycle);
    sync(cycle);
    // The same event path is used for live writes and log replay.
    writes_.push_back({cycle,byte});
    if (cycle/frame_cycles!=write_frame_) {
        write_frame_=cycle/frame_cycles; frame_write_count_=0;
    }
    ++frame_write_count_;
    apply(writes_.back().byte);
}
void Psg::apply(std::uint8_t byte) {
    if (byte&0x80) latch_=(byte>>4)&7;
    const auto channel=latch_>>1;
    if (latch_&1) attenuations_[channel]=byte&15;
    else if (channel<3) {
        auto& period=periods_[channel];
        if (byte&0x80) period=(period&0x3f0)|(byte&15);
        else period=(period&15)|((byte&0x3f)<<4);
    } else {
        noise_control_=byte&7;
        lfsr_=initial_state::psg_lfsr;
    }
}
void Psg::tick() {
    for (unsigned i=0;i<3;++i) {
        if (timers_[i]!=0) --timers_[i];
        if (timers_[i]==0) { timers_[i]=periods_[i]; outputs_[i]=!outputs_[i]; }
    }
    if (noise_timer_!=0) --noise_timer_;
    if (noise_timer_==0) {
        const auto rate=noise_control_&3;
        noise_timer_=rate==3 ? periods_[2] : (0x10<<rate);
        noise_clock_=!noise_clock_;
        if (noise_clock_) {
            const auto feedback=(lfsr_&1)^((noise_control_&4) ? ((lfsr_>>3)&1) : 0);
            lfsr_=static_cast<std::uint16_t>((lfsr_>>1)|(feedback<<15));
            // Maxim's implementation samples bit 0 after shifting.
            noise_output_=(lfsr_&1)!=0;
        }
    }
}
std::int16_t Psg::sample() const {
    int mixed=noise_output_ ? volume_table[attenuations_[3]] : 0;
    for (unsigned i=0;i<3;++i)
        if (periods_[i]<=1 || outputs_[i]) mixed+=volume_table[attenuations_[i]];
    return static_cast<std::int16_t>(mixed); // four channels <= 4*8191 = 32764
}
void Psg::sync(std::uint64_t target) {
    if (target<cycle_) throw std::invalid_argument("PSG cannot run backwards");
    auto ticks=target/sample_cycles-cycle_/sample_cycles;
    while (ticks--) { tick(); samples_.values.push_back(sample()); }
    cycle_=target;
}
std::vector<PsgWrite> Psg::take_writes() {
    return std::exchange(writes_,{});
}
PsgSamples Psg::take_samples() {
    return std::exchange(samples_,PsgSamples{(cycle_/sample_cycles+1)*sample_cycles,{}});
}
}
