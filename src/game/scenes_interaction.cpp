// SPDX-License-Identifier: GPL-3.0-or-later
// Interaction scenes (milestone 13): entering, running and leaving the
// cartridge's interaction scenes (a conversation picture with its script).
// The scripts, pictures, palettes, layouts and music numbers stay in the
// cartridge.
//
// The routines of this file:
//   00:3C52  loading an interaction (its game mode). A sound request and a
//            fade out; the environment may substitute another interaction
//            kind for the requested one; a missing kind becomes 1; the
//            picture is loaded (00:3D47 below), two fixed tile sets go to
//            VRAM, the scene's work bytes are reset and the animation armed
//            (01:63A5 runs once with interrupts off), the scene's music is
//            requested, the game mode advances to the interaction itself,
//            the first VDP mode register is rewritten and, after a frame
//            wait, the fade in (00:02FD) follows.
//   00:3B9C  the interaction (its game mode), once per frame: the pause,
//            the frame wait, a pending encounter or the scene object
//            (01:617D) and the script through the cartridge's dispatch
//            table; when the script returns, the clean-up and the choice of
//            the next game mode.
//   00:3D36  the picture loader, three ways in: 00:3D36 a background chosen
//            by the caller's argument, 00:3D47 the current interaction's
//            picture, 00:3D4E a blank screen. A picture is a palette, its
//            graphics (the loader 00:03FA) and a layout (the decoder
//            01:6B62); kinds 20h and up, and arguments 20h and up, are blank.
//
// How the code is cut. Most stretches here end at a call into another part
// (the pause loop, the frame wait, the graphics loader, the table jump); the
// core runs the call and the instruction after it is the next entry. The
// interrupt switches around the animation's first run and the VDP register
// write are also left to the core, as their own one-instruction entries, so the
// one-instruction delay of EI is the original's. The addresses passed to the
// kit are the original's, because the kit reads each instruction's operands
// and cycle cost from the player's ROM; no ROM value is written here.
#include "game/scenes.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace mk=menu_kit;
void register_block_transfer(RoutineRegistry&,RoutineEntry);
namespace {
using mk::Exit;

// The ways a segment ends. Each names the original's final instruction and
// charges its cycles; the core runs that instruction. The cost is an upper
// bound, so a conditional call is charged as taken.
Exit call_at(std::uint16_t at) { return mk::continues(at,mk::call_cycles); }
Exit jump_at(std::uint16_t at) { return mk::continues(at,mk::jump_cycles); }
Exit return_at(std::uint16_t at) { return mk::returns(at,mk::return_cycles); }
Exit return_taken_at(std::uint16_t at) { return mk::returns(at,mk::taken_conditional_return_cycles); }
Exit restart_at(std::uint16_t at) { return mk::continues(at,mk::restart_cycles); }
// The instructions the core runs at the end of some segments, by cost.
Exit interrupts_off_at(std::uint16_t at) { return mk::continues(at,4); }    // DI
Exit decrement_pair_at(std::uint16_t at) { return mk::continues(at,6); }    // DEC HL

// ---- The interaction: 00:3B9C-3C29 ----------------------------------------------------------------
// 00:3B9C: a paused game runs the pause loop first (a conditional call).
template<class Z> Exit interaction_pause(Z& z) {
    z.load_a_direct(0x3B9C);                                          // the pause flag
    z.or_a(0x3B9F);
    return call_at(0x3BA0);                                           // the pause loop, if paused
}
// 00:3BA3: the frame wait, with its request number in A.
template<class Z> Exit interaction_frame_wait(Z& z) {
    z.load_immediate(0x3BA3,z.r.a);
    return call_at(0x3BA5);
}
// 00:3BA8: a pending encounter (C29D nonzero) jumps to the battle path at
// 00:3C1A; the core runs the jump.
template<class Z> Exit interaction_pending_encounter(Z& z) {
    z.load_a_direct(0x3BA8);                                          // C29D
    z.or_a(0x3BAB);
    return mk::jump_if_exit(0x3BAC);
}
// 00:3BAF: the scene object named by C2DC is installed (01:617D; zero means
// none).
template<class Z> Exit interaction_scene_object(Z& z) {
    z.load_a_direct(0x3BAF);                                          // C2DC
    return call_at(0x3BB2);
}
// 00:3BB5: the script. The kind minus 10h (zero below 10h), modulo 16,
// picks the entry of the dispatch table; the table jump runs the script.
template<class Z> Exit interaction_script(Z& z) {
    z.load_a_direct(0x3BB5);                                          // the interaction kind
    z.subtract_immediate(0x3BB8);
    if(!z.relative_if(0x3BBA,!z.carry())) z.xor_a(0x3BBC);
    z.and_immediate(0x3BBD);
    z.load_hl(0x3BBF);                                                // the dispatch table
    return call_at(0x3BC2);
}
// 00:3BC5: after the script (or the battle path). A scene with its own music
// (a nonzero entry, indexed by the kind minus 0Fh, in the music table)
// requests a closing sound. The encounter flag, the kind, C2D5 and the word
// C2DB are cleared, and the 256 bytes of the object slots (C800) are cleared
// with one zero byte copied forward. Back in the interaction game mode
// (0Dh), the vehicle byte C2E9 picks the next mode: bit 7 set gives mode 4,
// otherwise zero gives mode 8 and anything else mode 0Eh.
template<class Z> Exit interaction_finish(Z& z) {
    z.load_a_direct(0x3BC5);                                          // the interaction kind
    z.subtract_immediate(0x3BC8);
    if(!z.relative_if(0x3BCA,!z.carry())) z.xor_a(0x3BCC);
    z.and_immediate(0x3BCD);
    z.copy(0x3BCF,z.r.l,z.r.a);
    z.load_immediate(0x3BD0,z.r.h);
    z.load_de(0x3BD2);                                                // the music table
    z.add_to_hl(0x3BD5,z.de());
    z.load_from_hl(0x3BD6,z.r.a);
    z.or_a(0x3BD7);
    if(!z.relative_if(0x3BD8,z.zero())) {                             // the scene had music: the closing sound
        z.load_immediate(0x3BDA,z.r.a);
        z.store_a_direct(0x3BDC);
    }
    z.xor_a(0x3BDF);
    z.store_a_direct(0x3BE0);                                         // C29D
    z.store_a_direct(0x3BE3);                                         // the interaction kind
    z.store_a_direct(0x3BE6);                                         // C2D5
    z.load_hl(0x3BE9);
    z.store_hl_direct(0x3BEC);                                        // C2DB
    z.load_hl(0x3BEF);                                                // clear the object slots
    z.load_de(0x3BF2);
    z.load_bc(0x3BF5);
    z.store_to_hl(0x3BF8);
    z.block_copy(0x3BFA);
    z.load_a_direct(0x3BFC);                                          // the game mode
    z.compare_immediate(0x3BFF);
    if(!z.zero()) return return_taken_at(0x3C01);                     // not the interaction mode: keep it
    z.return_not_taken(0x3C01);
    z.load_a_direct(0x3C02);                                          // C2E9
    z.test_bit(0x3C05,7,z.r.a);
    if(!z.relative_if(0x3C07,z.zero())) {
        z.load_immediate(0x3C09,z.r.a);
        z.store_a_direct(0x3C0B);
        return return_at(0x3C0E);
    }
    z.or_a(0x3C0F);
    z.load_immediate(0x3C10,z.r.a);                                   // the load keeps the flags of the test
    if(!z.relative_if(0x3C12,z.zero())) z.load_immediate(0x3C14,z.r.a);
    z.store_a_direct(0x3C16);
    return return_at(0x3C19);
}
// 00:3C20: on the battle path, after the battle. A nonzero first object slot
// opens the field menu (a conditional call), then 00:3BC5 follows.
template<class Z> Exit interaction_after_battle(Z& z) {
    z.load_a_direct(0x3C20);                                          // C800
    z.or_a(0x3C23);
    return call_at(0x3C24);
}

// ---- Loading an interaction: 00:3C52-3D1C ----------------------------------------------------------
// 00:3C52: a sound request, then the fade out.
template<class Z> Exit load_interaction_start(Z& z) {
    z.load_immediate(0x3C52,z.r.a);
    z.store_a_direct(0x3C54);                                         // the sound request byte
    return call_at(0x3C57);
}
// The environment can substitute the requested scene. By the environment
// byte C308: zero turns kind 5 into 4; 1 and 7 turn kind 1 into 5; 8 makes
// any kind 6; 0Ah turns kind 9 into 8. Every comparison is kept, because its
// flags survive to the picture loader's call.
template<class Z> void substitute_interaction_kind(Z& z) {
    z.load_a_direct(0x3C5A);                                          // C308
    z.or_a(0x3C5D);
    if(!z.relative_if(0x3C5E,!z.zero())) {
        z.load_a_direct(0x3C60);
        z.compare_immediate(0x3C63);
        if(z.relative_if(0x3C65,!z.zero())) return;
        z.load_immediate(0x3C67,z.r.a);
        z.store_a_direct(0x3C69);
        z.relative(0x3C6C);
        return;
    }
    z.compare_immediate(0x3C6E);
    if(!z.relative_if(0x3C70,!z.zero())) {
        z.load_a_direct(0x3C72);
        z.compare_immediate(0x3C75);
        if(z.relative_if(0x3C77,!z.zero())) return;
        z.load_immediate(0x3C79,z.r.a);
        z.store_a_direct(0x3C7B);
        z.relative(0x3C7E);
        return;
    }
    z.compare_immediate(0x3C80);
    if(!z.relative_if(0x3C82,!z.zero())) {
        z.load_a_direct(0x3C84);
        z.compare_immediate(0x3C87);
        if(z.relative_if(0x3C89,!z.zero())) return;
        z.load_immediate(0x3C8B,z.r.a);
        z.store_a_direct(0x3C8D);
        z.relative(0x3C90);
        return;
    }
    z.compare_immediate(0x3C92);
    if(!z.relative_if(0x3C94,!z.zero())) {
        z.load_immediate(0x3C96,z.r.a);
        z.store_a_direct(0x3C98);
        z.relative(0x3C9B);
        return;
    }
    z.compare_immediate(0x3C9D);
    if(z.relative_if(0x3C9F,!z.zero())) return;
    z.load_a_direct(0x3CA1);
    z.compare_immediate(0x3CA4);
    if(z.relative_if(0x3CA6,!z.zero())) return;
    z.load_immediate(0x3CA8,z.r.a);
    z.store_a_direct(0x3CAA);
}
// 00:3CAD: kind zero becomes kind 1.
template<class Z> void default_interaction_kind(Z& z) {
    z.load_a_direct(0x3CAD);                                          // the interaction kind
    z.or_a(0x3CB0);
    if(!z.relative_if(0x3CB1,!z.zero())) {
        z.increment(0x3CB3,z.r.a);
        z.store_a_direct(0x3CB4);
    }
}
// 00:3C5A: after the fade out, the substitution and the default; the call of
// the picture loader 00:3D47 ends the segment.
template<class Z> Exit load_interaction_kind(Z& z) {
    substitute_interaction_kind(z);
    default_interaction_kind(z);
    return call_at(0x3CB7);
}
// 00:3CAD as an entry: the substitution's jumps land here.
template<class Z> Exit load_interaction_default(Z& z) {
    default_interaction_kind(z);
    return call_at(0x3CB7);
}
// 00:3CBA: after the picture. Bank 16 goes into slot 2 (a mapper store) and
// the first fixed tile set is loaded to VRAM by the graphics loader.
template<class Z> Exit load_interaction_tiles(Z& z) {
    z.load_hl(0x3CBA);                                                // the slot-2 bank register
    z.store_to_hl(0x3CBD);
    z.load_hl(0x3CBF);                                                // the tile set
    z.load_de(0x3CC2);                                                // its VRAM address
    return call_at(0x3CC5);
}
// 00:3CC8: the second fixed tile set.
template<class Z> Exit load_interaction_more_tiles(Z& z) {
    z.load_hl(0x3CC8);
    z.load_de(0x3CCB);
    return call_at(0x3CCE);
}
// 00:3CD1: the scene's work bytes. C304, C300, the first object slot and the
// vehicle byte C2E9 are cleared; C2D6 = FFh arms the animation; the colour
// countdown word C213 is zeroed; C2BC = FF00h starts the tile animation's
// countdown at zero and its frame at FFh, so its first step shows frame
// zero. Interrupts then go off (the core runs the DI) for the animation's
// first run.
template<class Z> Exit load_interaction_reset(Z& z) {
    z.xor_a(0x3CD1);
    z.store_a_direct(0x3CD2);                                         // C304
    z.store_a_direct(0x3CD5);                                         // C300
    z.store_a_direct(0x3CD8);                                         // C800
    z.store_a_direct(0x3CDB);                                         // C2E9
    z.decrement(0x3CDE,z.r.a);
    z.store_a_direct(0x3CDF);                                         // C2D6 = FFh
    z.load_hl(0x3CE2);
    z.store_hl_direct(0x3CE5);                                        // C213
    z.load_hl(0x3CE8);
    z.store_hl_direct(0x3CEB);                                        // C2BC
    return interrupts_off_at(0x3CEE);
}
// 00:3CF6: the scene's music, after the protected load of the kind that
// follows the EI (game/scenes.h, ei_delay_scaffolding). The kind minus 0Fh
// (zero below), modulo 16, indexes the music table; a nonzero entry is the
// sound request. The game mode then advances to the interaction itself, and
// interrupts go off again for the VDP register write.
template<class Z> Exit load_interaction_music(Z& z) {
    z.subtract_immediate(0x3CF6);
    if(!z.relative_if(0x3CF8,!z.carry())) z.xor_a(0x3CFA);
    z.and_immediate(0x3CFB);
    z.copy(0x3CFD,z.r.l,z.r.a);
    z.load_immediate(0x3CFE,z.r.h);
    z.load_de(0x3D00);                                                // the music table
    z.add_to_hl(0x3D03,z.de());
    z.load_from_hl(0x3D04,z.r.a);
    z.or_a(0x3D05);
    if(!z.relative_if(0x3D06,z.zero())) z.store_a_direct(0x3D08);     // the sound request byte
    z.load_hl(0x3D0B);                                                // the game mode
    z.increment_at_hl(0x3D0E);
    return interrupts_off_at(0x3D0F);
}
// 00:3D10: DE holds a VDP register write (register 0, the first mode
// register); the restart at 00:0008 sends it.
template<class Z> Exit load_interaction_mode_register(Z& z) {
    z.load_de(0x3D10);
    return restart_at(0x3D13);
}

// ---- The picture loader: 00:3D36-3DA5 --------------------------------------------------------------
// 00:3D36: a background by argument. 20h and up jump to the blank screen.
template<class Z> Exit background_by_argument(Z& z) {
    z.compare_immediate(0x3D36);
    return mk::relative_if_exit(0x3D38,!z.carry());
}
// 00:3D3A: otherwise the argument times 8 indexes the picture table (three
// bytes into the row, at the layout), and the jump to 00:3D96 draws only the
// layout.
template<class Z> Exit background_layout_record(Z& z) {
    z.add_register(0x3D3A,z.r.a);
    z.add_register(0x3D3B,z.r.a);
    z.add_register(0x3D3C,z.r.a);
    z.copy(0x3D3D,z.r.l,z.r.a);
    z.load_immediate(0x3D3E,z.r.h);
    z.load_de(0x3D40);
    z.add_to_hl(0x3D43,z.de());
    return jump_at(0x3D44);
}
// 00:3D47: the current interaction's picture. Kinds below 20h have one;
// the others fall into the blank screen.
template<class Z> Exit interaction_picture(Z& z) {
    z.load_a_direct(0x3D47);                                          // the interaction kind
    z.compare_immediate(0x3D4A);
    return mk::relative_if_exit(0x3D4C,z.carry());
}
// 00:3D4E: the blank screen. The first two bytes of the screen copy at D000
// get the blank cell's two bytes; the copy forward of 5FEh bytes, two
// apart, repeats them across the screen. The segment ends at the DEC HL that
// rewinds to the copy's source; the copy itself (00:3D5D) is the shared
// resumable block transfer.
template<class Z> Exit blank_screen(Z& z) {
    z.load_hl(0x3D4E);
    z.load_de(0x3D51);
    z.load_bc(0x3D54);
    z.store_to_hl(0x3D57);
    z.increment_hl(0x3D59);
    z.store_to_hl(0x3D5A);
    return decrement_pair_at(0x3D5C);
}
// 00:3D5F: after the copy, C2D3 (a dialogue window's saved background) is
// cleared, since the screen under it is gone.
template<class Z> Exit blank_screen_done(Z& z) {
    z.xor_a(0x3D5F);
    z.store_a_direct(0x3D60);
    return return_at(0x3D63);
}
// 00:3D64: a picture. The kind times 8 indexes the picture table. Its row
// gives the bank for slot 2 (a mapper store), the palette (16 bytes to the
// target palette at C240, followed by 8 fixed bytes), then the graphics,
// sent to VRAM 0 by the graphics loader. The row pointer waits on the stack
// across that call.
template<class Z> Exit picture_palette_and_graphics(Z& z) {
    z.add_register(0x3D64,z.r.a);                                     // kind * 8
    z.add_register(0x3D65,z.r.a);
    z.add_register(0x3D66,z.r.a);
    z.copy(0x3D67,z.r.l,z.r.a);
    z.load_immediate(0x3D68,z.r.h);
    z.load_de(0x3D6A);                                                // the picture table
    z.add_to_hl(0x3D6D,z.de());
    z.load_from_hl(0x3D6E,z.r.a);
    z.store_a_direct(0x3D6F);                                         // the bank, into slot 2
    z.increment_hl(0x3D72);
    z.load_from_hl(0x3D73,z.r.a);                                     // the palette pointer
    z.increment_hl(0x3D74);
    z.push(0x3D75,z.r.h,z.r.l);
    z.load_from_hl(0x3D76,z.r.h);
    z.copy(0x3D77,z.r.l,z.r.a);
    z.load_de(0x3D78);                                                // C240
    z.load_bc(0x3D7B);
    z.block_copy(0x3D7E);
    z.load_hl(0x3D80);                                                // the fixed second half
    z.load_immediate(0x3D83,z.r.c);
    z.block_copy(0x3D85);
    z.pop(0x3D87,z.r.h,z.r.l);
    z.increment_hl(0x3D88);
    z.load_from_hl(0x3D89,z.r.a);                                     // the graphics pointer
    z.increment_hl(0x3D8A);
    z.push(0x3D8B,z.r.h,z.r.l);                                       // the row, kept for the layout
    z.load_from_hl(0x3D8C,z.r.h);
    z.copy(0x3D8D,z.r.l,z.r.a);
    z.load_de(0x3D8E);                                                // VRAM 0
    return call_at(0x3D91);                                           // the graphics loader, 00:03FA
}
// 00:3D96: the layout. C2D3 is cleared, the layout's bank goes into slot 2
// (a mapper store) and its pointer into HL; the jump to the layout decoder
// 01:6B62 ends the segment.
template<class Z> void picture_layout(Z& z) {
    z.xor_a(0x3D96);
    z.store_a_direct(0x3D97);                                         // C2D3
    z.load_from_hl(0x3D9A,z.r.a);
    z.store_a_direct(0x3D9B);                                         // the bank, into slot 2
    z.increment_hl(0x3D9E);
    z.load_from_hl(0x3D9F,z.r.a);
    z.increment_hl(0x3DA0);
    z.load_from_hl(0x3DA1,z.r.h);
    z.copy(0x3DA2,z.r.l,z.r.a);
}
template<class Z> Exit picture_layout_only(Z& z) {
    picture_layout(z);
    return jump_at(0x3DA3);
}
// 00:3D94: after the graphics loader, the row pointer comes back and steps
// to the layout bytes.
template<class Z> Exit picture_layout_after_graphics(Z& z) {
    z.pop(0x3D94,z.r.h,z.r.l);
    z.increment_hl(0x3D95);
    picture_layout(z);
    return jump_at(0x3DA3);
}

// 00:3CF2 and 00:3D14: an EI as a one-instruction entry the core runs, so
// the instruction after it still sees the original's interrupt delay.
void register_interrupts_on(RoutineRegistry& registry, std::uint16_t at) {
    const RoutineEntry entry{0,at};
    auto exit=mk::core_final_at(at);
    exit.interrupt_mask=InterruptMask::enable;
    auto spec=mk::segment(entry,{exit});
    spec.single_instruction=true;
    registry.add(entry,spec,[](const Machine&) {return 4;},[=](RoutineContext& c) {c.finish_core(at);});
}
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address, with its unit, exits and permissions. An
// entry that ends at a call holds the call's return address (scratch 2). A
// note names the call or jump the entry follows.
void register_scene_interactions(RoutineRegistry& registry) {
    auto F=[](std::uint16_t at, std::int8_t stack=0) { return mk::core_final_at(at,stack); };
    auto R=[](std::uint16_t at, std::int8_t stack=0) { return mk::returned_at(at,stack); };
    auto C=[](std::uint16_t at, std::int8_t stack=0) { return mk::conditional_at(at,stack); };
    // The DI that ends a segment declares the interrupts it switches off.
    auto F_di=[](std::uint16_t at) { auto exit=mk::core_final_at(at); exit.interrupt_mask=InterruptMask::disable; return exit; };
    auto add=[&](RoutineEntry entry, RoutineEntry unit, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        mk::add_segment(registry,entry,mk::segment(unit,std::move(exits),permissions),logic);
    };
    const auto at=[](std::uint16_t address) { return RoutineEntry{0,address}; };
    const RoutineEntry picture=at(0x3D47), blank=at(0x3D4E), background=at(0x3D36);

    // ---- the interaction ----
    add(at(0x3B9C),at(0x3B9C),{F(0x3BA0)},{.scratch_stack=2},[](auto& z) -> Exit { return interaction_pause(z); });
    add(at(0x3BA3),at(0x3BA3),{F(0x3BA5)},{.scratch_stack=2},[](auto& z) -> Exit { return interaction_frame_wait(z); });         // after the pause loop
    add(at(0x3BA8),at(0x3BA8),{C(0x3BAC)},{},[](auto& z) -> Exit { return interaction_pending_encounter(z); });                   // after the frame wait
    add(at(0x3BAF),at(0x3BAF),{F(0x3BB2)},{.scratch_stack=2},[](auto& z) -> Exit { return interaction_scene_object(z); });     // the jp nz at 00:3BAC was not taken
    add(at(0x3BB5),at(0x3BB5),{F(0x3BC2)},{.scratch_stack=2},[](auto& z) -> Exit { return interaction_script(z); });             // after the scene object (01:617D)
    add(at(0x3BC5),at(0x3BC5),{R(0x3C01),R(0x3C0E),R(0x3C19)},{},[](auto& z) -> Exit { return interaction_finish(z); });         // after the script
    add(at(0x3C20),at(0x3C20),{F(0x3C24)},{.scratch_stack=2},[](auto& z) -> Exit { return interaction_after_battle(z); });       // after the battle (00:100F)

    // ---- loading an interaction and the picture loader ----
    add(at(0x3C52),at(0x3C52),{F(0x3C57)},{.scratch_stack=2},[](auto& z) -> Exit { return load_interaction_start(z); });
    add(at(0x3CBA),at(0x3CBA),{F(0x3CC5)},{.slot2_mapper=true,.scratch_stack=2},[](auto& z) -> Exit { return load_interaction_tiles(z); });  // after the picture (00:3D47)
    add(at(0x3CC8),at(0x3CC8),{F(0x3CCE)},{.scratch_stack=2},[](auto& z) -> Exit { return load_interaction_more_tiles(z); });    // after the first tile set (00:03FA)
    add(picture,picture,{C(0x3D4C)},{},[](auto& z) -> Exit { return interaction_picture(z); });
    add(at(0x3D64),at(0x3D64),{F(0x3D91,-2)},{.slot2_mapper=true,.scratch_stack=4},[](auto& z) -> Exit { return picture_palette_and_graphics(z); });
    add(at(0x3D94),picture,{F(0x3DA3,2)},{.slot2_mapper=true,.inherited_stack=2},
        [](auto& z) -> Exit { return picture_layout_after_graphics(z); });                                                  // after the graphics loader (00:03FA)
    add(at(0x3D96),at(0x3D96),{F(0x3DA3)},{.slot2_mapper=true},[](auto& z) -> Exit { return picture_layout_only(z); });
    add(blank,blank,{F(0x3D5C)},{},[](auto& z) -> Exit { return blank_screen(z); });
    register_block_transfer(registry,at(0x3D5D));                                                                           // the blank screen's copy
    add(at(0x3D5F),blank,{R(0x3D63)},{},[](auto& z) -> Exit { return blank_screen_done(z); });                                 // after the copy
    add(background,background,{C(0x3D38)},{},[](auto& z) -> Exit { return background_by_argument(z); });
    add(at(0x3D3A),background,{F(0x3D44)},{},[](auto& z) -> Exit { return background_layout_record(z); });                     // the jr nc at 00:3D38 was not taken
    add(at(0x3CAD),at(0x3CAD),{F(0x3CB7)},{.scratch_stack=2},[](auto& z) -> Exit { return load_interaction_default(z); });
    add(at(0x3C5A),at(0x3C5A),{F(0x3CB7)},{.scratch_stack=2},[](auto& z) -> Exit { return load_interaction_kind(z); });          // after the fade out
    add(at(0x3CD1),at(0x3CD1),{F_di(0x3CEE)},{},[](auto& z) -> Exit { return load_interaction_reset(z); });                    // after the second tile set (00:03FA)
    add(at(0x3CF6),at(0x3CF6),{F_di(0x3D0F)},{},[](auto& z) -> Exit { return load_interaction_music(z); });                    // after the protected load at 00:3CF3
    add(at(0x3D10),at(0x3D10),{F(0x3D13)},{.scratch_stack=2},[](auto& z) -> Exit { return load_interaction_mode_register(z); });      // after the DI
    register_interrupts_on(registry,0x3CF2);                                                                               // after the animation's first run (01:63A5)
    register_interrupts_on(registry,0x3D14);                                                                               // after the VDP register write
}
}
