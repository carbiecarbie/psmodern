// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/dungeon_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
// The table holds little-endian addresses. Both the low byte and HL remain
// visible to the chosen handler. The dispatcher tail-calls it on the
// original caller's stack (a jump to HL, rendered here: milestone 14).
template<class Z> kit::Exit select_handler(Z& z,std::uint16_t at) {
    z.load_from_hl(at,z.r.a);
    z.increment_hl(at+1);
    z.load_from_hl(at+2,z.r.h);
    z.copy(at+3,z.r.l,z.r.a);
    return z.final_jump_hl(at+4);
}

// The two fixed-bank dispatchers add a byte offset to HL. Carry from
// the low-byte addition updates the table page, even if the table wraps.
template<class Z> kit::Exit select_at_offset(Z& z,std::uint16_t at) {
    auto& r=z.r;
    z.add_register(at,r.l);
    z.copy(at+1,r.l,r.a);
    z.add_with_carry(at+2,r.h);
    z.subtract_register(at+3,r.l);
    z.copy(at+4,r.h,r.a);
    return select_handler(z,at+5);
}
}

void register_dispatch(RoutineRegistry& registry) {
    auto add=[&](RoutineEntry entry,std::uint16_t jump,unsigned cycles,auto logic) {
        kit::add_segment(registry,entry,kit::segment(entry,{kit::core_final_at(jump)}),
                         [cycles](const Machine&) { return cycles; },logic);
    };
    add(dispatch_entries[0],0x00F0,52,[](auto& z) {
        // Commands index two-byte entries. Doubling wraps at eight bits.
        z.add_register(0x00E6,z.r.a);
        return select_at_offset(z,0x00E7);
    });
    add(dispatch_entries[1],0x0142,78,[](auto& z) {
        // The frame selector already contains a byte offset.
        z.load_a_direct(0x0131);
        z.and_immediate(0x0134);
        z.load_hl(0x0136);
        return select_at_offset(z,0x0139);
    });
    add(dispatch_entries[2],0x4772,61,[](auto& z) {
        // An interaction selects a word from the table supplied in DE.
        z.copy(0x4769,z.r.l,z.r.a);
        z.load_immediate(0x476A,z.r.h);
        z.add_to_hl(0x476C,z.hl());
        z.add_to_hl(0x476D,z.de());
        return select_handler(z,0x476E);
    });
}
}
