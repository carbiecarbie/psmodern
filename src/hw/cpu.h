// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/differences.h"
#include <cstdint>

namespace psmodern {
// One register pair: the 16-bit value, its two bytes (at_0 the low one) and
// the bytes as an array.
union RegisterPair {
    std::uint16_t uint16_value;
    struct { std::uint8_t at_0, at_1; } uint8_values;
    std::uint8_t uint8_array[2];
};
static_assert(sizeof(RegisterPair)==2);
// The bytes of the last instruction the arbitration looks at (see data).
union InstructionBytes {
    std::uint32_t uint32_value;
    std::uint16_t uint16_array[2];
    std::uint8_t uint8_array[4];
};
static_assert(sizeof(InstructionBytes)==4);

// The processor's register file and interrupt arbitration state (milestone
// 14). The program has no processor core: the rendered C++ instructions read
// and write these fields, the machine performs the interrupt responses in
// C++, and the test oracle runs the pinned core on a copy of them. The member
// names and encodings are those of the pinned core's structure, which this
// replaces, so the arbitration rules and the oracle comparison read the same
// fields:
// - data: data[0] and data[1] are the opcode bytes of the last instruction
//   (EI, RETN and LD A,I/R are recognised from them at the next boundary);
//   data[2] keeps IFF1 from before a RETN. data[3] and xy are the core's own
//   scratch, carried only for the oracle.
// - request: the pending requests (request_nmi, request_int) and the
//   NMI-reject marker an NMI response leaves for the next boundary.
// - int_line: the INT input as last sampled; halt_line and resume: the core's
//   HALT and prefix-resumption states (the game never executes HALT, and a
//   rendered instruction never stops after a prefix).
struct Z80 {
    static constexpr std::uint8_t request_reject_nmi=2, request_nmi=4, request_int=8;
    static constexpr std::uint8_t resume_halt=1, resume_xy=2;
    InstructionBytes data;
    RegisterPair ix_iy[2];
    RegisterPair pc, sp, xy, memptr;
    RegisterPair af, af_, bc, bc_, de, de_, hl, hl_;
    std::uint8_t r, i, im, request, resume, iff1, iff2, q, int_line, halt_line;
    // The INT input sampled for the next boundary: a deasserted line clears
    // the INT request, an asserted one sets it while IFF1 is set.
    void set_int_line(bool level) {
        int_line=level;
        if (!level) request&=static_cast<std::uint8_t>(~request_int);
        else if (iff1) request|=request_int;
    }
    // An NMI edge.
    void raise_nmi() { request|=request_nmi; }
};
// The register file's fields that differ, for a divergence report: every
// field above except xy (the core's per-instruction scratch), with data[1..3]
// only while they are live (after a prefix or a resumption).
void register_differences(const Z80& a, const Z80& b, DifferenceList& out);
bool same_registers(const Z80& a, const Z80& b);
}
