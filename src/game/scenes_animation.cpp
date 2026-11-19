// SPDX-License-Identifier: GPL-3.0-or-later
// Scene animation (milestone 13): the tile and colour animations that run once
// per frame while an interaction scene (a conversation picture) is on screen.
// The frame tables, the tile graphics and the colours stay in the cartridge.
//
// The routines of this file:
//   01:63A5  the dispatcher. It does nothing unless the scene armed its
//            animation (C2D6 nonzero, set by 00:3C52) and the interaction
//            kind is between 1 and 0Bh; it then jumps through the cartridge's
//            handler table by that kind. One table entry is a bare return
//            (01:63CE, "this scene has no animation").
//   01:63CF  tile sequence A: the first colour cycle, then every twelfth
//            frame the next of nine frames, each four tile strips.
//   01:63F6  tile sequence B: the same with fourteen frames of three strips,
//            every sixteenth frame. Both fall into the strip uploader 01:641B.
//   01:641B  the strip uploader: for each strip, one byte names the tile to
//            overwrite and one byte the 128-byte source picture in bank 16.
//   01:6442  tile sequence C: six frames, each four 128-byte and two 64-byte
//            pieces from bank 17, written in one run from tile 1 onwards.
//   01:64A3  colour cycle A: every 32nd frame, colours 13-15 take the next
//            of four positions in a cartridge colour run.
//   01:64CF  colour cycle B: every eighth frame, colours 8-15 step one
//            position back through two cartridge colour runs.
//
// How the code is cut. Each animation keeps a countdown and a frame index in
// a two-byte record (C2BC/C2BD for the tile sequences, C213/C214 for the
// colours). A countdown that is still not negative after its decrement ends
// the handler with a conditional return; that return is the segment's first
// exit. A frame that runs out reloads the countdown and steps the index,
// which wraps to zero at the sequence length (so an index already past the
// end also restarts at zero). The video writes are output loops on the VDP
// data port; the kit replays them write by write at their original cycles.
// The VDP address is set by the restart at 00:0008, which the kit renders in
// place except in the strip uploader, where the restart ends the segment.
//
// The addresses passed to the kit are the original's, because the kit reads
// each instruction's operands and cycle cost from the player's ROM; no ROM
// value is written here.
#include "game/scenes.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace mk=menu_kit;
namespace {
using mk::Exit;

// The ways a segment ends: the final instruction, rendered like the others.
template<class Z> Exit jump_at(Z& z, std::uint16_t at) { return z.final_jump(at); }
template<class Z> Exit return_at(Z& z, std::uint16_t at) { return z.final_return(at); }
template<class Z> Exit restart_at(Z& z, std::uint16_t at) { return z.final_restart(at,0x08); }

// ---- The dispatcher: 01:63A5 ---------------------------------------------------------------------
// Three early returns: the animation is not armed (C2D6 zero), there is no
// interaction kind, or the kind is 0Ch or more (no table entry). Otherwise
// HL points just before the handler table and the shared table jump runs.
template<class Z> Exit animation_dispatch(Z& z) {
    z.load_a_direct(0x63A5);                                          // C2D6, the "animation armed" byte
    z.or_a(0x63A8);
    if (auto exit=z.final_return_if(0x63A9,mk::Condition::z)) return *exit;
    z.load_a_direct(0x63AA);                                          // the interaction kind
    z.or_a(0x63AD);
    if (auto exit=z.final_return_if(0x63AE,mk::Condition::z)) return *exit;
    z.compare_immediate(0x63AF);                                      // kinds 0Ch and up have no handler
    if (auto exit=z.final_return_if(0x63B1,mk::Condition::nc)) return *exit;
    z.load_hl(0x63B2);                                                // the handler table, one entry early (kind 1 is first)
    return jump_at(z,0x63B5);                                           // the table jump
}
// 01:63CE: the table's "no animation" entry is a lone return.
template<class Z> Exit no_animation(Z& z) { return return_at(z,0x63CE); }

// ---- Tile sequences A and B: 01:63CF and 01:63F6 ------------------------------------------------
// Both first call colour cycle A (the call is a one-instruction segment of its
// own; these entries start after it), then count down C2BC and step the frame C2BD.

// 01:63D2: sequence A. A new frame every twelve frames (the countdown reloads
// with 0Bh), nine frames. Bank 16 goes into slot 2 (a mapper store), the
// frame's row of the frame table is index * 8 bytes, and B = 4 strips; the
// jump into the strip uploader ends the segment.
template<class Z> Exit tile_sequence_a(Z& z) {
    z.load_hl(0x63D2);                                                // C2BC, the countdown
    z.decrement_at_hl(0x63D5);
    if (auto exit=z.final_return_if(0x63D6,mk::Condition::p)) return *exit;                     // not yet: nothing to draw this frame
    z.store_to_hl(0x63D7);                                            // reload the countdown
    z.increment_hl(0x63D9);                                           // C2BD, the frame index
    z.load_from_hl(0x63DA,z.r.a);
    z.increment(0x63DB,z.r.a);
    z.compare_immediate(0x63DC);                                      // at or past the last frame: back to the first
    if(!z.relative_if(0x63DE,z.carry())) z.xor_a(0x63E0);
    z.store_register_to_hl(0x63E1,z.r.a);
    z.load_hl(0x63E2);                                                // the slot-2 bank register
    z.store_to_hl(0x63E5);                                            // bank 16
    z.load_hl(0x63E7);                                                // the frame table
    z.add_register(0x63EA,z.r.a);                                     // index * 8
    z.add_register(0x63EB,z.r.a);
    z.add_register(0x63EC,z.r.a);
    z.copy(0x63ED,z.r.e,z.r.a);
    z.load_immediate(0x63EE,z.r.d);
    z.add_to_hl(0x63F0,z.de());
    z.load_immediate(0x63F1,z.r.b);                                   // four strips
    return jump_at(z,0x63F3);                                           // into the strip uploader, 01:641B
}

// ---- The strip uploader: 01:641B ----------------------------------------------------------------
// One strip per turn of the outer loop (B strips, B kept on the stack). The
// first byte of the strip's two is a tile number: 0200h plus it, times 32, is
// that tile's VRAM write address, which the restart at 00:0008 sends to the
// VDP. The restart ends this segment (the strip counter is already pushed).
template<class Z> Exit strip_start(Z& z) {
    z.push(0x641B,z.r.b,z.r.c);                                       // the strip counter
    z.load_from_hl(0x641C,z.r.e);                                     // the tile to overwrite
    z.load_immediate(0x641D,z.r.d);
    z.exchange_de_hl(0x641F);
    z.add_to_hl(0x6420,z.hl());                                       // times 32 (the bytes of a tile)
    z.add_to_hl(0x6421,z.hl());
    z.add_to_hl(0x6422,z.hl());
    z.add_to_hl(0x6423,z.hl());
    z.add_to_hl(0x6424,z.hl());
    z.exchange_de_hl(0x6425);
    return restart_at(z,0x6426);                                        // the VDP address (00:0008)
}
// The end of a strip: the frame pointer and the strip counter come back from
// the stack and the djnz decides whether another strip follows.
template<class Z> Exit strip_end(Z& z) {
    z.pop(0x643D,z.r.h,z.r.l);
    z.pop(0x643E,z.r.b,z.r.c);
    return z.final_decrement_jump(0x643F);
}
// The 128 bytes of a strip go to the VDP data port one by one. A long stream
// is cut at the loop's own jump once 128 cycles have passed, so a frame edge
// never falls inside a segment; the entry at 01:6438 resumes it with the same
// pending byte and VRAM address.
template<class Z> Exit strip_stream(Z& z) {
    for(;;) {
        z.output_increment(0x6438);
        if(z.loop_exit(128)) return z.final_jump_if(0x643A,mk::Condition::nz);
        if(!z.jump_if(0x643A,!z.zero())) return strip_end(z);
    }
}
// 01:6427: after the restart. The second byte picks the source: its value
// times 128 (shifted into DE) is added to the start of the strip pictures in
// bank 16. The frame pointer is kept on the stack across the stream, and
// BC = 80h bytes for the data port.
template<class Z> Exit strip_source(Z& z) {
    z.increment_hl(0x6427);
    z.load_from_hl(0x6428,z.r.d);                                     // the source picture number
    z.increment_hl(0x6429);
    z.push(0x642A,z.r.h,z.r.l);                                       // the frame pointer, for the next strip
    z.load_immediate(0x642B,z.r.e);
    z.shift_right_logical(0x642D,z.r.d);                              // DE = number * 128
    z.rotate_right_through_carry(0x642F,z.r.e);
    z.load_hl(0x6431);                                                // the strip pictures
    z.add_to_hl(0x6434,z.de());
    z.load_bc(0x6435);                                                // 128 bytes to the data port
    return strip_stream(z);
}

// 01:63F9: sequence B. A new frame every sixteen frames (reload 0Fh),
// fourteen frames, a row of the frame table is index * 6 bytes and B = 3
// strips. The original falls straight into the strip uploader, so this
// segment carries on through its first strip address.
template<class Z> Exit tile_sequence_b(Z& z) {
    z.load_hl(0x63F9);                                                // C2BC, the countdown
    z.decrement_at_hl(0x63FC);
    if (auto exit=z.final_return_if(0x63FD,mk::Condition::p)) return *exit;
    z.store_to_hl(0x63FE);                                            // reload the countdown
    z.increment_hl(0x6400);                                           // C2BD, the frame index
    z.load_from_hl(0x6401,z.r.a);
    z.increment(0x6402,z.r.a);
    z.compare_immediate(0x6403);                                      // wrap past the last frame
    if(!z.relative_if(0x6405,z.carry())) z.xor_a(0x6407);
    z.store_register_to_hl(0x6408,z.r.a);
    z.load_hl(0x6409);                                                // the slot-2 bank register
    z.store_to_hl(0x640C);                                            // bank 16
    z.load_hl(0x640E);                                                // the frame table
    z.add_register(0x6411,z.r.a);                                     // index * 2, kept in E
    z.copy(0x6412,z.r.e,z.r.a);
    z.add_register(0x6413,z.r.a);                                     // index * 4 + index * 2 = index * 6
    z.add_register(0x6414,z.r.e);
    z.copy(0x6415,z.r.e,z.r.a);
    z.load_immediate(0x6416,z.r.d);
    z.add_to_hl(0x6418,z.de());
    z.load_immediate(0x6419,z.r.b);                                   // three strips
    return strip_start(z);
}

// ---- Tile sequence C: 01:6442 -------------------------------------------------------------------
// A new frame every sixteen frames (reload 0Fh), six frames; bank 17 in
// slot 2. A frame row is six bytes (index * 6): four source numbers for
// 128-byte pieces, then two for 64-byte pieces (the second shift halves the
// step). The VRAM address is set once, at tile 1, and all six pieces follow
// one another in VRAM. The whole frame is one segment.
template<class Z> Exit tile_sequence_c(Z& z) {
    z.load_hl(0x6442);                                                // C2BC, the countdown
    z.decrement_at_hl(0x6445);
    if (auto exit=z.final_return_if(0x6446,mk::Condition::p)) return *exit;
    z.store_to_hl(0x6447);                                            // reload the countdown
    z.increment_hl(0x6449);                                           // C2BD, the frame index
    z.load_from_hl(0x644A,z.r.a);
    z.increment(0x644B,z.r.a);
    z.compare_immediate(0x644C);                                      // wrap past the last frame
    if(!z.relative_if(0x644E,z.carry())) z.xor_a(0x6450);
    z.store_register_to_hl(0x6451,z.r.a);
    z.load_hl(0x6452);                                                // the slot-2 bank register
    z.store_to_hl(0x6455);                                            // bank 17
    z.add_register(0x6457,z.r.a);                                     // index * 6, as in sequence B
    z.copy(0x6458,z.r.b,z.r.a);
    z.add_register(0x6459,z.r.a);
    z.add_register(0x645A,z.r.b);
    z.copy(0x645B,z.r.e,z.r.a);
    z.load_immediate(0x645C,z.r.d);
    z.load_hl(0x645E);                                                // the frame table
    z.add_to_hl(0x6461,z.de());
    z.load_de(0x6462);                                                // VRAM tile 1
    z.restart_vdp_address(0x6465);
    z.load_immediate(0x6466,z.r.b);                                   // four large pieces
    do {
        z.push(0x6468,z.r.b,z.r.c);
        z.load_from_hl(0x6469,z.r.d);                                 // the source number
        z.increment_hl(0x646A);
        z.load_immediate(0x646B,z.r.e);
        z.shift_right_logical(0x646D,z.r.d);                          // times 128
        z.rotate_right_through_carry(0x646F,z.r.e);
        z.load_bc(0x6471);                                            // the start of bank 17's pictures
        z.exchange_de_hl(0x6474);                                     // the frame pointer waits in DE
        z.add_to_hl(0x6475,z.bc());
        z.load_bc(0x6476);                                            // 128 bytes to the data port
        do { z.output_increment(0x6479); } while(z.jump_if(0x647B,!z.zero()));
        z.pop(0x647E,z.r.b,z.r.c);
        z.exchange_de_hl(0x647F);
    } while(z.decrement_jump(0x6480));
    z.load_immediate(0x6482,z.r.b);                                   // two small pieces
    do {
        z.push(0x6484,z.r.b,z.r.c);
        z.load_from_hl(0x6485,z.r.d);
        z.increment_hl(0x6486);
        z.load_immediate(0x6487,z.r.e);
        z.shift_right_logical(0x6489,z.r.d);                          // times 64
        z.rotate_right_through_carry(0x648B,z.r.e);
        z.shift_right_logical(0x648D,z.r.d);
        z.rotate_right_through_carry(0x648F,z.r.e);
        z.load_bc(0x6491);
        z.exchange_de_hl(0x6494);
        z.add_to_hl(0x6495,z.bc());
        z.load_bc(0x6496);                                            // 64 bytes to the data port
        do { z.output_increment(0x6499); } while(z.jump_if(0x649B,!z.zero()));
        z.pop(0x649E,z.r.b,z.r.c);
        z.exchange_de_hl(0x649F);
    } while(z.decrement_jump(0x64A0));
    return return_at(z,0x64A2);
}

// ---- Colour cycles A and B: 01:64A3 and 01:64CF --------------------------------------------------
// Both wait while a fade runs (the fade timer nonzero) and outside the
// interaction game mode (0Dh), then count down C213 and step C214.

// 01:64A3: colour cycle A. Every 32nd frame (reload 1Fh) the position steps
// forward and wraps at four; colours 13-15 receive the three cartridge bytes
// from that position on.
template<class Z> Exit colour_cycle_a(Z& z) {
    z.load_a_direct(0x64A3);                                          // the fade timer
    z.or_a(0x64A6);
    if (auto exit=z.final_return_if(0x64A7,mk::Condition::nz)) return *exit;
    z.load_a_direct(0x64A8);                                          // the game mode
    z.compare_immediate(0x64AB);
    if (auto exit=z.final_return_if(0x64AD,mk::Condition::nz)) return *exit;
    z.load_hl(0x64AE);                                                // C213, the countdown
    z.decrement_at_hl(0x64B1);
    if (auto exit=z.final_return_if(0x64B2,mk::Condition::p)) return *exit;
    z.store_to_hl(0x64B3);                                            // reload the countdown
    z.load_de(0x64B5);                                                // CRAM colour 13
    z.restart_vdp_address(0x64B8);
    z.increment_hl(0x64B9);                                           // C214, the position
    z.load_from_hl(0x64BA,z.r.a);
    z.increment(0x64BB,z.r.a);                                        // one forward, modulo 4
    z.and_immediate(0x64BC);
    z.store_register_to_hl(0x64BE,z.r.a);
    z.copy(0x64BF,z.r.e,z.r.a);
    z.load_immediate(0x64C0,z.r.d);
    z.load_hl(0x64C2);                                                // the colour run
    z.add_to_hl(0x64C5,z.de());
    z.load_bc(0x64C6);                                                // three colours to the data port
    do { z.output_increment(0x64C9); } while(z.jump_if(0x64CB,!z.zero()));
    return return_at(z,0x64CE);
}
// 01:64CF: colour cycle B. Every eighth frame (reload 07h) the position steps
// back and wraps at sixteen; colours 8-11 come from the first run and 12-15
// from the second, both from that position on.
template<class Z> Exit colour_cycle_b(Z& z) {
    z.load_a_direct(0x64CF);                                          // the fade timer
    z.or_a(0x64D2);
    if (auto exit=z.final_return_if(0x64D3,mk::Condition::nz)) return *exit;
    z.load_a_direct(0x64D4);                                          // the game mode
    z.compare_immediate(0x64D7);
    if (auto exit=z.final_return_if(0x64D9,mk::Condition::nz)) return *exit;
    z.load_hl(0x64DA);                                                // C213, the countdown
    z.decrement_at_hl(0x64DD);
    if (auto exit=z.final_return_if(0x64DE,mk::Condition::p)) return *exit;
    z.store_to_hl(0x64DF);                                            // reload the countdown
    z.load_de(0x64E1);                                                // CRAM colour 8
    z.restart_vdp_address(0x64E4);
    z.increment_hl(0x64E5);                                           // C214, the position
    z.load_from_hl(0x64E6,z.r.a);
    z.decrement(0x64E7,z.r.a);                                        // one back, modulo 16
    z.and_immediate(0x64E8);
    z.store_register_to_hl(0x64EA,z.r.a);
    z.copy(0x64EB,z.r.e,z.r.a);
    z.load_immediate(0x64EC,z.r.d);
    z.load_hl(0x64EE);                                                // the first colour run
    z.add_to_hl(0x64F1,z.de());
    z.load_bc(0x64F2);                                                // four colours
    do { z.output_increment(0x64F5); } while(z.jump_if(0x64F7,!z.zero()));
    z.load_hl(0x64FA);                                                // the second colour run
    z.add_to_hl(0x64FD,z.de());
    z.load_immediate(0x64FE,z.r.b);                                   // four more
    do { z.output_increment(0x6500); } while(z.jump_if(0x6502,!z.zero()));
    return return_at(z,0x6505);
}
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address, with its unit, exits and permissions. The
// strip uploader's three resume entries belong to the unit 01:641B and
// declare the stack they inherit: the strip counter (2 bytes) and, inside a
// strip, the frame pointer (2 more).
void register_scene_animation(RoutineRegistry& registry) {
    auto F=[](std::uint16_t at, std::int8_t stack=0) { return mk::core_final_at(at,stack); };
    auto R=[](std::uint16_t at, std::int8_t stack=0) { return mk::returned_at(at,stack); };
    auto C=[](std::uint16_t at, std::int8_t stack=0) { return mk::conditional_at(at,stack); };
    auto L=[](std::uint16_t at, std::uint16_t after, std::int8_t stack=0) { return mk::conditional_loop_at(at,after,stack); };
    auto add=[&](std::uint16_t entry, RoutineEntry unit, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        mk::add_segment(registry,RoutineEntry{1,entry},mk::segment(unit,std::move(exits),permissions),logic);
    };
    const RoutineEntry dispatch{1,0x63A5}, none{1,0x63CE}, colour_a{1,0x64A3}, colour_b{1,0x64CF};
    const RoutineEntry strips{1,0x641B}, sequence_a{1,0x63D2}, sequence_b{1,0x63F9}, sequence_c{1,0x6442};

    add(0x63A5,dispatch,{R(0x63A9),R(0x63AE),R(0x63B1),F(0x63B5)},{},[](auto& z) -> Exit { return animation_dispatch(z); });
    {   // a lone RET: the entry is also its final
        auto spec=mk::segment(none,{R(0x63CE)},{});
        spec.single_instruction=true;
        mk::add_segment(registry,RoutineEntry{1,0x63CE},std::move(spec),[](auto& z) -> Exit { return no_animation(z); });
    }
    add(0x64A3,colour_a,{R(0x64A7),R(0x64AD),R(0x64B2),R(0x64CE)},{.scratch_stack=2},[](auto& z) -> Exit { return colour_cycle_a(z); });
    add(0x64CF,colour_b,{R(0x64D3),R(0x64D9),R(0x64DE),R(0x6505)},{.scratch_stack=2},[](auto& z) -> Exit { return colour_cycle_b(z); });
    add(0x641B,strips,{F(0x6426,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return strip_start(z); });
    add(0x63D2,sequence_a,{R(0x63D6),F(0x63F3)},{.slot2_mapper=true},[](auto& z) -> Exit { return tile_sequence_a(z); });        // after the call of colour cycle A
    add(0x63F9,sequence_b,{R(0x63FD),F(0x6426,-2)},{.slot2_mapper=true,.scratch_stack=4},
        [](auto& z) -> Exit { return tile_sequence_b(z); });                                                                // after the call of colour cycle A
    add(0x6442,sequence_c,{R(0x6446),R(0x64A2)},{.slot2_mapper=true,.scratch_stack=2},[](auto& z) -> Exit { return tile_sequence_c(z); });
    add(0x6427,strips,{L(0x643A,128,-2),C(0x643F,2)},{.scratch_stack=2,.inherited_stack=2},
        [](auto& z) -> Exit { return strip_source(z); });                                                                   // after the VDP address restart
    add(0x6438,strips,{L(0x643A,128),C(0x643F,4)},{.inherited_stack=4},[](auto& z) -> Exit { return strip_stream(z); });     // a stream cut at its jump
    add(0x643D,strips,{C(0x643F,4)},{.inherited_stack=4},[](auto& z) -> Exit { return strip_end(z); });                      // the stream's last byte was sent

    // The joining stretches (milestone 14): the calls of colour cycle A after
    // a sequence's table jump, and the return that follows the last strip.
    for (const std::uint16_t at:{0x63CF,0x63F6}) {
        auto spec=mk::segment(RoutineEntry{1,at},{F(at)},{.scratch_stack=2});
        spec.single_instruction=true;
        mk::add_segment(registry,RoutineEntry{1,at},std::move(spec),[at](auto& z) -> Exit { return z.final_call(at); });
    }
    auto leave=mk::segment(RoutineEntry{1,0x6441},{R(0x6441)});
    leave.single_instruction=true;
    mk::add_segment(registry,RoutineEntry{1,0x6441},std::move(leave),[](auto& z) -> Exit { return z.final_return(0x6441); });
}
}
