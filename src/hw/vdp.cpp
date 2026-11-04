// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/vdp.h"
#include <stdexcept>

namespace psmodern {
std::uint8_t Vdp::read_data() {
    control_latch_=false;
    auto value=buffer_; buffer_=vram_[address_]; address_=(address_+1)&0x3fff;
    return value;
}
std::uint8_t Vdp::read_status(std::uint8_t open_bus) {
    auto value=static_cast<std::uint8_t>((frame_visible_ ? 0x80 : 0) | (overflow_ ? 0x40 : 0) |
        (collision_ ? 0x20 : 0) | (open_bus & 0x1f));
    frame_flag_=frame_visible_=line_flag_=overflow_=collision_=control_latch_=false;
    return value;
}
void Vdp::write_data(std::uint8_t value) {
    control_latch_=false; buffer_=value;
    if (code_==3) cram_[address_&31]=value&0x3f;
    else vram_[address_]=value;
    address_=(address_+1)&0x3fff;
}
void Vdp::write_control(std::uint8_t value) {
    if (!control_latch_) { address_=(address_&0x3f00)|value; control_latch_=true; return; }
    control_latch_=false; code_=value>>6; address_=(address_&0xff)|((value&0x3f)<<8);
    if (code_==0) { buffer_=vram_[address_]; address_=(address_+1)&0x3fff; }
    else if (code_==2 && (value&15)<regs_.size()) regs_[value&15]=address_&0xff;
}
}

namespace psmodern {
namespace {
constexpr std::uint64_t dots_per_line=342;
constexpr std::uint64_t irq_dot=315, line_dot=316, vcounter_dot=318, visible_dot=319;
// CPU cycle 0 is VDP dot 1. One T-state advances three half-dots.
constexpr std::uint64_t cpu_at_dot(std::uint64_t dot) {
    return (2*dot-2*initial_state::vdp_dot+2)/3;
}
}
void Vdp::sync(std::uint64_t target) {
    if (target < cycle_) throw std::invalid_argument("VDP cannot run backwards");
    const auto old_half=2*initial_state::vdp_dot+3*cycle_, new_half=2*initial_state::vdp_dot+3*target;
    auto first=(old_half/2)/dots_per_line;
    auto last=(new_half/2)/dots_per_line;
    for (auto line=first; line<=last; ++line) {
        auto scanline=line%262;
        auto reached=[&](std::uint64_t dot) {
            auto event=2*(line*dots_per_line+dot);
            return event>old_half && event<=new_half;
        };
        if (scanline==192 && reached(irq_dot)) frame_flag_=true;
        if (reached(line_dot)) {
            if (scanline<192 || scanline==261) {
                if (line_counter_==0) { line_counter_=regs_[10]; line_flag_=true; }
                else --line_counter_;
            } else line_counter_=regs_[10];
        }
        // Inference: a control read in the gap cancels the later status bit.
        if (scanline==192 && reached(visible_dot) && frame_flag_) frame_visible_=true;
    }
    cycle_=target;
}
std::uint8_t Vdp::v_counter() const {
    auto position=(2*initial_state::vdp_dot+3*cycle_)/2;
    auto line=static_cast<unsigned>((position/dots_per_line)%262);
    if (position%dots_per_line>=vcounter_dot) line=(line+1)%262;
    return static_cast<std::uint8_t>(line<=0xda ? line : line-6);
}
void Vdp::latch_h_counter() {
    auto p=static_cast<int>(dot());
    // VDPTest uses signed division toward zero around HCount zero.
    h_latch_=static_cast<std::uint8_t>(p<296 ? p/2 : (p-342)/2);
}
std::optional<std::uint64_t> Vdp::next_irq() const {
    if (irq()) return cycle_;
    if (!(regs_[0]&0x10) && !(regs_[1]&0x20)) return {};
    auto copy=*this;
    auto first=((2*initial_state::vdp_dot+3*cycle_)/2)/dots_per_line;
    for (auto line=first; line<=first+524; ++line) {
        for (auto dot:{irq_dot,line_dot}) {
            auto event=cpu_at_dot(line*dots_per_line+dot);
            if (event<=cycle_) continue;
            copy.sync(event);
            if (copy.irq()) return event;
        }
    }
    return {};
}
}
