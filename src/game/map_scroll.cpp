// SPDX-License-Identifier: GPL-3.0-or-later
// World map and towns (milestone 11), part scroll:
// Steps, the camera, the screen window and the strips: the step 01:7143
// (and 01:714D), the screen window decode 01:723D (and 01:728A), the column
// and row strips 01:72A6 and 01:733A, and the whole screen 01:744B.
// Planned segments: map::scroll_segments in game/map.h, rendered with game/map_kit.h.
//
// What the code does, in short (docs/how-it-works/world-map.md, "Walking and
// the camera", has the full story):
//   - 01:714D is one frame of the party's step. A step counter counts the
//     frames of a step; when it runs out a new step starts from the pad (the
//     terrain check 01:74E4 may refuse it). Each frame moves the camera one
//     pixel on foot, two in a vehicle, records the direction as the pending
//     strip, and refills the window of metatiles when the camera enters
//     another screen; the strip producer then runs as a tail jump.
//   - 01:723D picks the 2 x 2 screens around the camera from the area's
//     screen table and run-length decodes each into one 256-byte slot of the
//     window ($CC00-$CFFF); 01:728A is the decoder.
//   - 01:72A6 (a column of 14 metatiles) and 01:733A (a row of 16) copy the
//     name-table words of the metatiles that scroll into view from the
//     metatile sheet into the name-table mirror ($D000); 01:744B does the
//     whole 12 x 16 screen after a map load.
// Every original instruction is rendered at its ROM address; bare calls to
// other units, and the tail jumps of 01:714D, stay native exits.
#include "game/map.h"
#include "game/map_kit.h"
#include "game/routines.h"
#include <optional>
#include <vector>

namespace psmodern::game {
namespace {
namespace mk=map_kit;
using mk::Exit; using mk::returns; using mk::continues;
using mk::returned_at; using mk::core_final_at; using mk::conditional_at; using mk::loop_at;

constexpr unsigned taken_return=mk::taken_conditional_return_cycles;
// A `call cc` declared as an exit ends the segment taken or not.
template<class Z> Exit call_if_exit(const Z&, std::uint16_t at, bool taken) {
    return continues(at,taken ? mk::call_cycles : mk::untaken_call_cycles);
}

// ---- 01:7143: the step ---------------------------------------------------------------
template<class Z> Exit step_save(Z& z) {                    // 01:7143: hold BC, DE, HL over the call
    auto& r=z.r;
    z.push(0x7143,r.b,r.c);
    z.push(0x7144,r.d,r.e);
    z.push(0x7145,r.h,r.l);
    return continues(0x7146,mk::call_cycles);
}
template<class Z> Exit step_restore(Z& z) {                 // 01:7149: and give them back
    auto& r=z.r;
    z.pop(0x7149,r.h,r.l);
    z.pop(0x714A,r.d,r.e);
    z.pop(0x714B,r.b,r.c);
    return returns(0x714C,mk::return_cycles);
}
// No direction is held (or every held one is blocked): the step ends.
template<class Z> Exit step_stop(Z& z) {                    // 01:722D-723C
    auto& r=z.r;
    z.load_immediate(0x722D,r.a);
    z.store_a_direct(0x722F);                               // the sound request (a bump)
    z.xor_a(0x7232);
    z.store_a_direct(0x7233);
    z.store_a_direct(0x7236);                               // no pending strip
    z.store_a_direct(0x7239);                               // and no step under way
    return returns(0x723C,mk::return_cycles);
}
// Right (bit 3): the terrain check for a new step, or 01:722D.
template<class Z> Exit step_right(Z& z) {                   // 01:7201-7209
    auto& r=z.r;
    z.test_bit(0x7201,3,r.c);
    if (z.relative_if(0x7203,z.zero())) return step_stop(z);
    z.load_immediate(0x7205,r.a);
    z.test_bit(0x7207,7,r.c);
    return call_if_exit(z,0x7209,!z.zero());
}
// Left (bit 2), with the camera's X in HL and the step in DE.
template<class Z> Exit step_left(Z& z) {                    // 01:71E6-71F4
    auto& r=z.r;
    z.load_immediate(0x71E6,r.d);
    z.load_hl_direct(0x71E8);
    z.copy(0x71EB,r.b,r.h);
    z.test_bit(0x71EC,2,r.c);
    if (z.relative_if(0x71EE,z.zero())) return step_right(z);
    z.load_immediate(0x71F0,r.a);
    z.test_bit(0x71F2,7,r.c);
    return call_if_exit(z,0x71F4,!z.zero());
}
// Down (bit 1).
template<class Z> Exit step_down(Z& z) {                    // 01:71AA-71B2
    auto& r=z.r;
    z.test_bit(0x71AA,1,r.c);
    if (z.relative_if(0x71AC,z.zero())) return step_left(z);
    z.load_immediate(0x71AE,r.a);
    z.test_bit(0x71B0,7,r.c);
    return call_if_exit(z,0x71B2,!z.zero());
}
// The camera's Y moved: store it with the strip request; a new screen row
// refills the window (the call is the exit; 01:71E3 jumps to the row strip).
template<class Z> Exit step_vertical_done(Z& z) {           // 01:71CC-71E0
    auto& r=z.r;
    z.store_a_direct(0x71CC);                               // pending strip: 1 up, 2 down
    z.load_immediate(0x71CF,r.a);
    z.store_a_direct(0x71D1);
    z.copy(0x71D4,r.a,r.d);
    z.store_a_direct(0x71D5);                               // the vertical scroll shadow
    z.copy(0x71D8,r.a,r.h);
    z.and_immediate(0x71D9);                                // eight screens down, then round
    z.copy(0x71DB,r.h,r.a);
    z.store_hl_direct(0x71DC);
    z.compare_register(0x71DF,r.b);
    return call_if_exit(z,0x71E0,!z.zero());
}
// The camera's X moved: store it and the horizontal scroll shadow (minus the
// camera's low byte); the final JP NZ refills the window on a new screen
// column, else 01:722A jumps to the column strip.
template<class Z> Exit step_horizontal_done(Z& z) {         // 01:7211-7227
    auto& r=z.r;
    z.store_a_direct(0x7211);                               // pending strip: 4 left, 8 right
    z.load_immediate(0x7214,r.a);
    z.store_a_direct(0x7216);
    z.copy(0x7219,r.a,r.l);
    z.negate(0x721A);
    z.store_a_direct(0x721C);
    z.copy(0x721F,r.a,r.h);
    z.and_immediate(0x7220);
    z.copy(0x7222,r.h,r.a);
    z.store_hl_direct(0x7223);
    z.compare_register(0x7226,r.b);
    return mk::jump_if_exit(0x7227);
}
template<class Z> Exit step_frame(Z& z) {                   // 01:714D-718D
    auto& r=z.r;
    z.load_hl(0x714D);
    z.load_from_hl(0x7150,r.a);
    z.decrement_at_hl(0x7151);                              // one frame of the step
    if (z.jump_if(0x7152,z.sign())) {                       // the step is over: read the pad
        z.store_to_hl(0x715C);
        z.load_a_direct(0x715E);
        z.and_immediate(0x7161);                            // the four directions
        z.or_immediate(0x7163);                             // bit 7: a new step, check the ground
        z.copy(0x7165,r.c,r.a);
        z.load_a_direct(0x7166);
        z.or_a(0x7169);
        z.load_immediate(0x716A,r.a);                       // 16 frames on foot
        if (!z.relative_if(0x716C,z.zero())) z.load_immediate(0x716E,r.a);   // 8 in a vehicle
        z.store_a_direct(0x7170);
    } else {                                                // the step goes on in its direction
        z.load_a_direct(0x7155);
        z.copy(0x7158,r.c,r.a);
        (void)z.jump(0x7159);
    }
    z.load_de(0x7173);                                      // one pixel a frame on foot,
    z.load_a_direct(0x7176);
    z.or_a(0x7179);
    if (!z.relative_if(0x717A,z.zero())) z.increment(0x717C,r.e);   // two in a vehicle
    z.load_a_direct(0x717D);
    z.copy(0x7180,r.d,r.a);
    z.load_hl_direct(0x7181);
    z.copy(0x7184,r.b,r.h);
    z.test_bit(0x7185,0,r.c);                               // up (bit 0)
    if (z.relative_if(0x7187,z.zero())) return step_down(z);
    z.load_immediate(0x7189,r.a);
    z.test_bit(0x718B,7,r.c);
    return call_if_exit(z,0x718D,!z.zero());
}
template<class Z> Exit step_after_up(Z& z) {                // 01:7190-71A7: move up unless blocked
    auto& r=z.r;
    if (z.relative_if(0x7190,!z.zero())) return step_down(z);
    z.copy(0x7192,r.a,r.d);
    z.subtract_register(0x7193,r.e);
    z.compare_immediate(0x7194);                            // the scroll register wraps at 224 lines
    if (!z.relative_if(0x7196,z.carry())) z.subtract_immediate(0x7198);
    z.copy(0x719A,r.d,r.a);
    z.copy(0x719B,r.a,r.l);
    z.subtract_register(0x719C,r.e);
    z.compare_immediate(0x719D);                            // the camera's Y wraps at 192 per screen
    if (!z.relative_if(0x719F,z.carry())) {
        z.subtract_immediate(0x71A1);
        z.decrement(0x71A3,r.h);
    }
    z.copy(0x71A4,r.l,r.a);
    z.load_immediate(0x71A5,r.a);
    (void)z.jump(0x71A7);
    return step_vertical_done(z);
}
template<class Z> Exit step_after_down(Z& z) {              // 01:71B5-71CA
    auto& r=z.r;
    if (z.relative_if(0x71B5,!z.zero())) return step_left(z);
    z.copy(0x71B7,r.a,r.d);
    z.add_register(0x71B8,r.e);
    z.compare_immediate(0x71B9);
    if (!z.relative_if(0x71BB,z.carry())) z.add_immediate(0x71BD);
    z.copy(0x71BF,r.d,r.a);
    z.copy(0x71C0,r.a,r.l);
    z.add_register(0x71C1,r.e);
    z.compare_immediate(0x71C2);
    if (!z.relative_if(0x71C4,z.carry())) {
        z.add_immediate(0x71C6);
        z.increment(0x71C8,r.h);
    }
    z.copy(0x71C9,r.l,r.a);
    z.load_immediate(0x71CA,r.a);
    return step_vertical_done(z);
}
template<class Z> Exit step_after_left(Z& z) {              // 01:71F7-71FE
    auto& r=z.r;
    if (z.relative_if(0x71F7,!z.zero())) return step_right(z);
    z.or_a(0x71F9);
    z.subtract_hl_with_carry(0x71FA,z.de());
    z.load_immediate(0x71FC,r.a);
    (void)z.jump(0x71FE);
    return step_horizontal_done(z);
}
template<class Z> Exit step_after_right(Z& z) {             // 01:720C-720F
    auto& r=z.r;
    if (z.relative_if(0x720C,!z.zero())) return step_stop(z);
    z.add_to_hl(0x720E,z.de());
    z.load_immediate(0x720F,r.a);
    return step_horizontal_done(z);
}

// ---- 01:723D: the window of metatiles -----------------------------------------------------
// The run-length decoder from its loop head 01:728C: a chunk ends at every
// arrival at the loop head, so a chunk is one run (the design had chunks of
// 512 cycles; with several runs a chunk, a frame boundary made every run of
// the chunk fall back natively, below the census gate). A byte 0 ends the screen;
// 1-7Fh is a run of the next byte repeated; 80h-FFh a literal run of its low
// 7 bits.
constexpr std::uint16_t decoder_after=0;
template<class Z> Exit decode_loop(Z&) {                    // 01:728C: always the first arrival's exit
    static_assert(decoder_after==0);
    return continues(0x728C,7);
}
template<class Z> Exit decode_tail(Z& z) {                  // 01:728D-72A3
    auto& r=z.r;
    for (;;) {
        z.or_a(0x728D);
        if (z.zero()) return returns(0x728E,taken_return);  // the end of the screen
        z.return_not_taken(0x728E);
        if (z.jump_if(0x728F,z.sign())) {                   // a literal run
            z.and_immediate(0x729D);
            z.copy(0x729F,r.c,r.a);
            z.increment_hl(0x72A0);
            z.block_copy(0x72A1);
            (void)z.jump(0x72A3);
        } else {                                            // a repeated byte
            z.copy(0x7292,r.b,r.a);
            z.increment_hl(0x7293);
            z.load_from_hl(0x7294,r.a);
            do {
                z.store_a_to_pair(0x7295,z.de());
                z.increment_pair(0x7296,r.d,r.e);
            } while (z.decrement_jump(0x7297));
            z.increment_hl(0x7299);
            (void)z.jump(0x729A);
        }
        if (z.loop_exit(decoder_after)) return continues(0x728C,7);
        z.load_from_hl(0x728C,r.a);
    }
}
template<class Z> Exit decode_screen(Z& z) {                // 01:728A
    z.load_immediate(0x728A,z.r.b);
    return decode_loop(z);
}
// HL = the next screen pointer word, read low byte first; DE = its slot.
template<class Z> void screen_pointer(Z& z, std::uint16_t at) {   // inc hl; ld a,(hl); inc hl; ld h,(hl); ld l,a
    auto& r=z.r;
    z.increment_hl(at);
    z.load_from_hl(at+1,r.a);
    z.increment_hl(at+2);
    z.load_from_hl(at+3,r.h);
    z.copy(at+4,r.l,r.a);
}
template<class Z> Exit window_start(Z& z) {                 // 01:723D-7261
    auto& r=z.r;
    z.load_a_direct(0x723D);
    z.store_a_direct(0x7240);                               // the bank of the area's screens
    z.load_a_direct(0x7243);                                // the screen row, times 18
    z.add_register(0x7246,r.a);
    z.copy(0x7247,r.e,r.a);
    z.add_register(0x7248,r.a);
    z.add_register(0x7249,r.a);
    z.add_register(0x724A,r.a);
    z.add_register(0x724B,r.e);
    z.copy(0x724C,r.e,r.a);
    z.load_a_direct(0x724D);                                // plus the screen column, times 2
    z.add_register(0x7250,r.a);
    z.add_register(0x7251,r.e);
    z.copy(0x7252,r.e,r.a);
    z.load_immediate(0x7253,r.d);
    z.load_hl_direct(0x7255);                               // into the area's 9 x 9 screen table
    z.add_to_hl(0x7258,z.de());
    z.load_from_hl(0x7259,r.a);
    z.increment_hl(0x725A);
    z.push(0x725B,r.h,r.l);
    z.load_from_hl(0x725C,r.h);
    z.copy(0x725D,r.l,r.a);
    z.load_de(0x725E);
    return continues(0x7261,mk::call_cycles);
}
template<class Z> Exit window_second(Z& z) {                // 01:7264-726E: the screen to the right
    auto& r=z.r;
    z.pop(0x7264,r.h,r.l);
    z.push(0x7265,r.h,r.l);
    screen_pointer(z,0x7266);
    z.load_de(0x726B);
    return continues(0x726E,mk::call_cycles);
}
template<class Z> Exit window_third(Z& z) {                 // 01:7271-727E: the screen below
    auto& r=z.r;
    z.pop(0x7271,r.h,r.l);
    z.load_de(0x7272);
    z.add_to_hl(0x7275,z.de());
    z.load_from_hl(0x7276,r.a);
    z.increment_hl(0x7277);
    z.push(0x7278,r.h,r.l);
    z.load_from_hl(0x7279,r.h);
    z.copy(0x727A,r.l,r.a);
    z.load_de(0x727B);
    return continues(0x727E,mk::call_cycles);
}
template<class Z> Exit window_fourth(Z& z) {                // 01:7281-7287, then the decoder
    auto& r=z.r;
    z.pop(0x7281,r.h,r.l);
    screen_pointer(z,0x7282);
    z.load_de(0x7287);
    return decode_screen(z);
}

// ---- 01:72A6, 01:733A, 01:744B: the strips --------------------------------------------------
// One metatile: its four name-table words (eight bytes) from the sheet at
// 8000h + 8 x number in slot 2, the top two words at DE and the bottom two a
// row of the mirror (40h bytes) further, 3Ch past the first pair. Ends after
// the second LDI group (the callers differ in how they step DE).
template<class Z> void metatile_words(Z& z, std::uint16_t at) {   // ld h,10h; add hl,hl x3; ldi x4
    auto& r=z.r;
    z.load_immediate(at,r.h);
    z.add_to_hl(at+2,z.hl());
    z.add_to_hl(at+3,z.hl());
    z.add_to_hl(at+4,z.hl());
    for (std::uint16_t i=0;i<4;++i) z.load_increment(static_cast<std::uint16_t>(at+5+2*i));
}
template<class Z> void next_mirror_row(Z& z, std::uint16_t at) {  // ld a,3Ch; add a,e; ld e,a; adc a,d; sub e; ld d,a
    auto& r=z.r;
    z.load_immediate(at,r.a);
    z.add_register(at+2,r.e);
    z.copy(at+3,r.e,r.a);
    z.add_with_carry(at+4,r.d);
    z.subtract_register(at+5,r.e);
}
template<class Z> void bottom_words(Z& z, std::uint16_t at) {     // ldi x4
    for (std::uint16_t i=0;i<4;++i) z.load_increment(static_cast<std::uint16_t>(at+2*i));
}

// The column strip: 14 metatiles down the window, one per chunk.
template<class Z> Exit column_iteration(Z& z) {              // 01:72FE-7339
    auto& r=z.r;
    metatile_words(z,0x72FE);
    next_mirror_row(z,0x730B);
    z.copy(0x7311,r.d,r.a);
    bottom_words(z,0x7312);
    z.pop(0x731A,r.h,r.l);
    z.load_immediate(0x731B,r.a);                           // the next metatile down the window:
    z.add_register(0x731D,r.l);
    z.compare_immediate(0x731E);                            // past the slot's 12 rows,
    if (!z.relative_if(0x7320,z.carry())) {                 // into the slot below
        z.subtract_immediate(0x7322);
        z.increment(0x7324,r.h);
        z.increment(0x7325,r.h);
    }
    z.copy(0x7326,r.l,r.a);
    next_mirror_row(z,0x7327);                              // the next mirror row pair,
    z.subtract_immediate(0x732D);                           // wrapping inside the 28 rows of the table
    if (!z.relative_if(0x732F,!z.carry())) z.add_immediate(0x7331);
    z.add_immediate(0x7333);
    z.copy(0x7335,r.d,r.a);
    z.pop(0x7336,r.b,r.c);
    if (!z.decrement_jump(0x7337)) return returns(0x7339,mk::return_cycles);
    z.push(0x72FB,r.b,r.c);
    z.push(0x72FC,r.h,r.l);
    return continues(0x72FD,7);                             // L 72FD after 0: one metatile a chunk
}
// The column's set-up is split in three pieces of 120-170 cycles (the frame
// boundary falls inside it often while a vehicle moves; see the census).
template<class Z> Exit column_start(Z& z) {                  // 01:72A6-72C3
    auto& r=z.r;
    z.load_a_direct(0x72A6);
    z.and_immediate(0x72A9);                                // a pending column (4 left, 8 right)?
    if (z.zero()) return returns(0x72AB,taken_return);
    z.return_not_taken(0x72AB);
    z.copy(0x72AC,r.b,r.a);
    z.load_a_direct(0x72AD);
    z.store_a_direct(0x72B0);                               // the bank of the metatile sheet
    z.load_immediate(0x72B3,r.c);
    z.load_a_direct(0x72B5);
    z.and_immediate(0x72B8);
    if (!z.relative_if(0x72BA,z.zero())) {                  // between tiles, moving left:
        z.copy(0x72BC,r.a,r.b);
        z.and_immediate(0x72BD);
        if (!z.relative_if(0x72BF,!z.zero())) z.load_immediate(0x72C1,r.c);   // one tile further
    }
    return continues(0x72C3,13);                            // ld a,(nn): the vertical shadow
}
template<class Z> Exit column_mirror(Z& z) {                 // 01:72C6-72DC
    auto& r=z.r;
    z.and_immediate(0x72C6);                                // the mirror address: row from the
                                                            // vertical shadow, column from the camera
    z.copy(0x72C8,r.l,r.a);
    z.load_immediate(0x72C9,r.h);
    z.add_to_hl(0x72CB,z.hl());
    z.add_to_hl(0x72CC,z.hl());
    z.add_to_hl(0x72CD,z.hl());
    z.load_a_direct(0x72CE);
    z.add_register(0x72D1,r.c);
    z.rotate_right_circular_a(0x72D2);
    z.rotate_right_circular_a(0x72D3);
    z.and_immediate(0x72D4);
    z.add_register(0x72D6,r.l);
    z.copy(0x72D7,r.e,r.a);
    z.copy(0x72D8,r.a,r.h);
    z.add_immediate(0x72D9);
    z.copy(0x72DB,r.d,r.a);
    return continues(0x72DC,7);                             // ld h,n: the window's first slot
}
template<class Z> Exit column_window(Z& z) {                 // 01:72DE-72FD
    auto& r=z.r;
    z.load_a_direct(0x72DE);                                // the window address of the first metatile
    z.and_immediate(0x72E1);
    z.copy(0x72E3,r.l,r.a);
    z.load_a_direct(0x72E4);
    z.add_register(0x72E7,r.c);
    if (!z.relative_if(0x72E8,!z.carry())) z.increment(0x72EA,r.h);
    z.rotate_right_circular_a(0x72EB);
    z.rotate_right_circular_a(0x72EC);
    z.rotate_right_circular_a(0x72ED);
    z.rotate_right_circular_a(0x72EE);
    z.and_immediate(0x72EF);
    z.add_register(0x72F1,r.l);
    z.copy(0x72F2,r.l,r.a);
    z.copy(0x72F3,r.a,r.b);
    z.and_immediate(0x72F4);
    if (!z.relative_if(0x72F6,z.zero())) z.increment(0x72F8,r.h);   // moving right: the slot to the right
    z.load_immediate(0x72F9,r.b);
    z.push(0x72FB,r.b,r.c);
    z.push(0x72FC,r.h,r.l);
    return continues(0x72FD,7);
}

// The row strip and the whole screen share their inner loop: 16 metatiles
// across the window.
template<class Z> void across_iteration(Z& z, std::uint16_t at) {  // 01:7397-73CC / 01:7488-74BD (pop bc excluded)
    auto& r=z.r;
    metatile_words(z,at);
    z.push(at+0x0D,r.d,r.e);
    next_mirror_row(z,at+0x0E);
    z.copy(at+0x14,r.d,r.a);
    bottom_words(z,at+0x15);
    z.pop(at+0x1D,r.d,r.e);
    z.copy(at+0x1E,r.a,r.e);
    z.and_immediate(at+0x1F);                               // the next pair of mirror columns,
    if (!z.relative_if(at+0x21,!z.zero())) {                // wrapping inside the row
        z.copy(at+0x23,r.a,r.e);
        z.subtract_immediate(at+0x24);
        z.copy(at+0x26,r.e,r.a);
    }
    z.pop(at+0x27,r.h,r.l);
    z.copy(at+0x28,r.a,r.l);                                // the next metatile across the window,
    z.and_immediate(at+0x29);
    z.copy(at+0x2B,r.b,r.a);
    z.increment(at+0x2C,r.l);
    z.copy(at+0x2D,r.a,r.l);
    z.and_immediate(at+0x2E);
    z.compare_register(at+0x30,r.b);
    if (!z.relative_if(at+0x31,z.zero())) {                 // into the slot to the right
        z.increment(at+0x33,r.h);
        z.copy(at+0x34,r.l,r.b);
    }
}
template<class Z> Exit row_iteration(Z& z) {                 // 01:7397-73CF
    auto& r=z.r;
    across_iteration(z,0x7397);
    z.pop(0x73CC,r.b,r.c);
    if (!z.decrement_jump(0x73CD)) return returns(0x73CF,mk::return_cycles);
    z.push(0x7394,r.b,r.c);
    z.push(0x7395,r.h,r.l);
    return continues(0x7396,7);                             // L 7396 after 0: one metatile a chunk
}
template<class Z> Exit row_start(Z& z) {                     // 01:733A-7396
    auto& r=z.r;
    z.load_a_direct(0x733A);
    z.and_immediate(0x733D);                                // a pending row (1 up, 2 down)?
    if (z.zero()) return returns(0x733F,taken_return);
    z.return_not_taken(0x733F);
    z.copy(0x7340,r.b,r.a);
    z.and_immediate(0x7341);
    z.load_a_direct(0x7343);
    z.store_a_direct(0x7346);                               // the bank of the metatile sheet
    z.load_a_direct(0x7349);
    z.load_immediate(0x734C,r.b);
    if (!z.relative_if(0x734E,!z.zero())) {                 // down: the row 192 lines below,
        z.compare_immediate(0x7350);                        // inside the 224 lines of the table
        if (!z.relative_if(0x7352,z.carry())) z.add_immediate(0x7354);
        z.load_immediate(0x7356,r.b);
        z.add_register(0x7358,r.b);
    }
    z.and_immediate(0x7359);
    z.copy(0x735B,r.l,r.a);
    z.load_immediate(0x735C,r.h);
    z.add_to_hl(0x735E,z.hl());
    z.add_to_hl(0x735F,z.hl());
    z.add_to_hl(0x7360,z.hl());
    z.load_a_direct(0x7361);
    z.rotate_right_circular_a(0x7364);
    z.rotate_right_circular_a(0x7365);
    z.and_immediate(0x7366);
    z.add_register(0x7368,r.l);
    z.copy(0x7369,r.e,r.a);
    z.copy(0x736A,r.a,r.h);
    z.add_immediate(0x736B);
    z.copy(0x736D,r.d,r.a);
    z.load_a_direct(0x736E);                                // the window address: the metatile row,
    z.and_immediate(0x7371);
    z.add_register(0x7373,r.b);                             // twelve rows lower going down
    z.copy(0x7374,r.l,r.a);
    z.add_with_carry_immediate(0x7375);
    z.subtract_register(0x7377,r.l);
    z.copy(0x7378,r.h,r.a);
    z.load_a_direct(0x7379);
    z.rotate_right_circular_a(0x737C);
    z.rotate_right_circular_a(0x737D);
    z.rotate_right_circular_a(0x737E);
    z.rotate_right_circular_a(0x737F);
    z.and_immediate(0x7380);
    z.add_register(0x7382,r.l);
    z.copy(0x7383,r.l,r.a);
    z.load_bc(0x7384);
    z.or_a(0x7387);
    z.subtract_hl_with_carry(0x7388,z.bc());               // past the slot: the slot below
    z.load_immediate(0x738A,r.a);
    if (!z.relative_if(0x738C,!z.carry())) {
        z.add_to_hl(0x738E,z.bc());
        z.load_immediate(0x738F,r.a);
    }
    z.copy(0x7391,r.h,r.a);
    z.load_immediate(0x7392,r.b);
    z.push(0x7394,r.b,r.c);
    z.push(0x7395,r.h,r.l);
    return continues(0x7396,7);
}

// The whole screen: 12 rows of 16 metatiles; a chunk ends at the inner loop
// head once 512 cycles have passed.
constexpr std::uint16_t screen_after=512;
// One metatile from 01:7488 (after the loop head's ld l,(hl)) up to the next
// arrival at the loop head 01:7487, or the final jump to the frame wait.
template<class Z> std::optional<Exit> screen_metatile(Z& z) {   // 01:7488-74E1
    auto& r=z.r;
    across_iteration(z,0x7488);
    z.pop(0x74BD,r.b,r.c);
    if (z.decrement_jump(0x74BE)) {
        z.push(0x7485,r.b,r.c);
        z.push(0x7486,r.h,r.l);
        return std::nullopt;
    }
    z.load_immediate(0x74C0,r.a);                           // the next row of the mirror, two tile rows down,
    z.add_register(0x74C2,r.e);
    z.copy(0x74C3,r.e,r.a);
    z.add_with_carry(0x74C4,r.d);
    z.subtract_register(0x74C5,r.e);
    z.subtract_immediate(0x74C6);                           // wrapping inside the 28 rows
    if (!z.relative_if(0x74C8,!z.carry())) z.add_immediate(0x74CA);
    z.add_immediate(0x74CC);
    z.copy(0x74CE,r.d,r.a);
    z.pop(0x74CF,r.h,r.l);
    z.load_immediate(0x74D0,r.a);                           // and the next row of the window
    z.add_register(0x74D2,r.l);
    z.compare_immediate(0x74D3);
    if (!z.relative_if(0x74D5,z.carry())) {
        z.subtract_immediate(0x74D7);
        z.increment(0x74D9,r.h);
        z.increment(0x74DA,r.h);
    }
    z.copy(0x74DB,r.l,r.a);
    z.pop(0x74DC,r.b,r.c);
    if (!z.decrement_jump(0x74DD)) {
        z.load_immediate(0x74DF,r.a);
        return continues(0x74E1,mk::jump_cycles);           // jp to the frame wait
    }
    z.push(0x7481,r.b,r.c);
    z.push(0x7482,r.h,r.l);
    z.load_immediate(0x7483,r.b);
    z.push(0x7485,r.b,r.c);
    z.push(0x7486,r.h,r.l);
    return std::nullopt;
}
template<class Z> Exit screen_loop(Z& z) {                   // from 01:7487
    for (;;) {
        if (z.loop_exit(screen_after)) return continues(0x7487,7);
        z.load_from_hl(0x7487,z.r.l);
        if (const auto exit=screen_metatile(z)) return *exit;
    }
}
template<class Z> Exit screen_start(Z& z) {                  // 01:744E-7487
    auto& r=z.r;
    z.load_a_direct(0x744E);
    z.store_a_direct(0x7451);
    z.load_a_direct(0x7454);
    z.and_immediate(0x7457);
    z.copy(0x7459,r.l,r.a);
    z.load_immediate(0x745A,r.h);
    z.add_to_hl(0x745C,z.hl());
    z.add_to_hl(0x745D,z.hl());
    z.add_to_hl(0x745E,z.hl());
    z.load_a_direct(0x745F);
    z.rotate_right_circular_a(0x7462);
    z.rotate_right_circular_a(0x7463);
    z.and_immediate(0x7464);
    z.add_register(0x7466,r.l);
    z.copy(0x7467,r.e,r.a);
    z.copy(0x7468,r.a,r.h);
    z.add_immediate(0x7469);
    z.copy(0x746B,r.d,r.a);
    z.load_a_direct(0x746C);
    z.and_immediate(0x746F);
    z.copy(0x7471,r.l,r.a);
    z.load_a_direct(0x7472);
    z.rotate_right_circular_a(0x7475);
    z.rotate_right_circular_a(0x7476);
    z.rotate_right_circular_a(0x7477);
    z.rotate_right_circular_a(0x7478);
    z.and_immediate(0x7479);
    z.add_register(0x747B,r.l);
    z.copy(0x747C,r.l,r.a);
    z.load_immediate(0x747D,r.h);
    z.load_immediate(0x747F,r.b);
    z.push(0x7481,r.b,r.c);
    z.push(0x7482,r.h,r.l);
    z.load_immediate(0x7483,r.b);
    z.push(0x7485,r.b,r.c);
    z.push(0x7486,r.h,r.l);
    return screen_loop(z);
}
template<class Z> Exit screen_resume(Z& z) {                 // 01:7488: after the core's ld l,(hl)
    if (const auto exit=screen_metatile(z)) return *exit;
    return screen_loop(z);
}
}

void register_map_scroll(RoutineRegistry& registry) {
    namespace m=map;
    const auto at=m::at1;
    const auto add=[&](RoutineEntry entry, RoutineEntry unit, std::vector<SegmentExit> exits, mk::Permissions permissions,
                       auto logic) {
        mk::add_segment(registry,entry,mk::segment(unit,std::move(exits),permissions),logic);
    };
    // The step: the wrapper holds BC, DE and HL over 01:714D. Each part of
    // 01:714D ends at a terrain check (taken or not), at the window refill of
    // a vertical move (taken or not, 01:71E3 then jumps natively to the row
    // strip), at the final JP NZ of a horizontal move (01:722A jumps natively
    // to the column strip) or at the return of a stop.
    const auto step=m::step_unit;
    add(step,step,{core_final_at(0x7146,-6)},{.scratch_stack=6},[](auto& z) -> Exit { return step_save(z); });
    add(at(0x7149),step,{returned_at(0x714C,6)},{.inherited_stack=6},[](auto& z) -> Exit { return step_restore(z); });
    const std::vector<SegmentExit> step_exits{core_final_at(0x718D),core_final_at(0x71B2),core_final_at(0x71F4),
        core_final_at(0x7209),core_final_at(0x71E0),conditional_at(0x7227),returned_at(0x723C)};
    add(at(0x714D),step,step_exits,{},[](auto& z) -> Exit { return step_frame(z); });
    add(at(0x7190),step,step_exits,{},[](auto& z) -> Exit { return step_after_up(z); });
    add(at(0x71B5),step,step_exits,{},[](auto& z) -> Exit { return step_after_down(z); });
    add(at(0x71F7),step,step_exits,{},[](auto& z) -> Exit { return step_after_left(z); });
    add(at(0x720C),step,step_exits,{},[](auto& z) -> Exit { return step_after_right(z); });

    // The window of metatiles: four screens, the first three through a call
    // of the decoder with the table pointer held, the fourth by falling into it.
    const auto window=m::screen_window_unit;
    add(window,window,{core_final_at(0x7261,-2)},{.slot2_mapper=true,.scratch_stack=2},
        [](auto& z) -> Exit { return window_start(z); });
    add(at(0x7264),window,{core_final_at(0x726E)},{.inherited_stack=2},[](auto& z) -> Exit { return window_second(z); });
    add(at(0x7271),window,{core_final_at(0x727E)},{.inherited_stack=2},[](auto& z) -> Exit { return window_third(z); });
    // With a chunk a run, 01:7281 and 01:728A always end at their first
    // arrival at the loop head; only 01:728D can meet the end of a screen.
    add(at(0x7281),window,{loop_at(0x728C,decoder_after,2)},{.inherited_stack=2},
        [](auto& z) -> Exit { return window_fourth(z); });
    add(at(0x728A),window,{loop_at(0x728C,decoder_after)},{},[](auto& z) -> Exit { return decode_screen(z); });
    add(at(0x728D),window,{loop_at(0x728C,decoder_after),returned_at(0x728E)},{},[](auto& z) -> Exit { return decode_tail(z); });

    // The strips: the head sets up and holds the loop's words; each resume
    // chunk is one metatile (the column and the row) or runs until 512 cycles
    // have passed (the whole screen).
    const auto column=m::column_unit;
    add(column,column,{returned_at(0x72AB),core_final_at(0x72C3)},{.slot2_mapper=true},
        [](auto& z) -> Exit { return column_start(z); });
    add(at(0x72C6),column,{core_final_at(0x72DC)},{},[](auto& z) -> Exit { return column_mirror(z); });
    add(at(0x72DE),column,{loop_at(0x72FD,0,-4)},{.scratch_stack=4},[](auto& z) -> Exit { return column_window(z); });
    add(at(0x72FE),column,{loop_at(0x72FD,0),returned_at(0x7339,4)},{.inherited_stack=4},
        [](auto& z) -> Exit { return column_iteration(z); });
    const auto row=m::row_unit;
    add(row,row,{returned_at(0x733F),loop_at(0x7396,0,-4)},{.slot2_mapper=true,.scratch_stack=4},
        [](auto& z) -> Exit { return row_start(z); });
    add(at(0x7397),row,{loop_at(0x7396,0),returned_at(0x73CF,4)},{.scratch_stack=2,.inherited_stack=4},
        [](auto& z) -> Exit { return row_iteration(z); });
    const auto screen=m::full_screen_unit;
    add(at(0x744E),screen,{loop_at(0x7487,screen_after,-8)},{.slot2_mapper=true,.scratch_stack=10},
        [](auto& z) -> Exit { return screen_start(z); });
    add(at(0x7488),screen,{loop_at(0x7487,screen_after),core_final_at(0x74E1,8)},{.scratch_stack=2,.inherited_stack=8},
        [](auto& z) -> Exit { return screen_resume(z); });
}
}
