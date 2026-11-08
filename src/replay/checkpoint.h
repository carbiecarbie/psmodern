// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/machine.h"
#include <array>
#include <vector>
#include <stdexcept>

namespace psmodern::replay {
// Portable, big-endian register pairs followed by I, R, IFF1, IFF2, IM, HALT.
inline std::array<std::uint8_t,30> cpu_bytes(const Z80& c) {
    std::array<std::uint8_t,30> out{}; std::size_t at=0;
    auto pair=[&](std::uint16_t v) { out[at++]=v>>8; out[at++]=v&255; };
    pair(c.af.uint16_value); pair(c.bc.uint16_value); pair(c.de.uint16_value); pair(c.hl.uint16_value);
    pair(c.af_.uint16_value); pair(c.bc_.uint16_value); pair(c.de_.uint16_value); pair(c.hl_.uint16_value);
    pair(c.ix_iy[0].uint16_value); pair(c.ix_iy[1].uint16_value); pair(c.sp.uint16_value); pair(c.pc.uint16_value);
    out[at++]=c.i; out[at++]=c.r; out[at++]=c.iff1; out[at++]=c.iff2; out[at++]=c.im; out[at++]=c.halt_line;
    return out;
}
// Writes are grouped by absolute video interval, even if an instruction
// straddles a frame boundary. Each event is a big-endian 16-bit offset + byte.
class FrameWrites {
public:
    std::vector<std::uint8_t> take(std::uint64_t frame, std::vector<PsgWrite> writes) {
        pending_.insert(pending_.end(),writes.begin(),writes.end());
        std::vector<std::uint8_t> out; std::size_t used=0;
        for (const auto& w:pending_) {
            if (w.cycle >= frame*Machine::frame_cycles) break;
            if (w.cycle < (frame-1)*Machine::frame_cycles) throw std::runtime_error("Late PSG write.");
            auto offset=w.cycle%Machine::frame_cycles;
            out.push_back(offset>>8); out.push_back(offset&255); out.push_back(w.byte); ++used;
        }
        pending_.erase(pending_.begin(),pending_.begin()+used);return out;
    }
private:
    std::vector<PsgWrite> pending_;
};
}
