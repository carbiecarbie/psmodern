// SPDX-License-Identifier: GPL-3.0-or-later
// Scenes, part name entry (milestone 13): naming a saved game. The screen is
// built by 01:4034, and the mode 00:3EB9 runs once per frame until the name
// is confirmed and written into cartridge RAM.
//
// The working state. The name buffer C700-C737 holds one character per text
// position; the five characters of the name are positions 21h-25h, and the
// text position C781 starts at 21h. Each position is drawn as a two-tile cell
// in the screen copy (01:4117 finds it, 01:413A writes it, 01:414F also sends
// it to video memory). The keyboard cursor is a five-byte record from C784:
// its sprite X (C784) and Y (C785), its address in the screen copy (C786-
// C787), the key under it (C788: a character, or the three action keys 5Ch
// next, 5Dh back and 5Eh end, each two keys wide) and the repeat counter
// C789 of a held direction.
//
// The routines of this file:
//   00:3EB9  the frame: pause, frame wait, the cursor sprites (01:40C5), then
//            the buttons. Button 1 erases the character at the text position
//            and steps back; button 2 types the key under the cursor (a
//            character, then the position steps forward), or acts on an
//            action key: next and back move the position, end confirms the
//            name (00:3F26). No button: the directions (00:3F73).
//   00:3F26  the confirmation: the name is copied into the slot's two name
//            rows in cartridge RAM, and the next game mode is chosen.
//   00:3F73  the directions: a new press moves at once and starts a 24-frame
//            delay; a held direction moves every fifth frame (01:402C). The
//            move (00:3FF2) skips empty cells and the second half of a wide
//            key.
//   01:4034  the screen: name table cleared, working state cleared, the
//            layout stream (01:4166) and the keyboard (01:41DB) drawn, the
//            palette and the cursor's tiles loaded, the cursor placed.
//   01:40C5  the cursor sprites; 01:4100 redraws every character of the name
//            buffer; 01:4166-41FB the layout stream, borders, rows and keys.
//
// How the code is cut, as in scenes.cpp: a segment ends at a call into
// another routine, at a DI or EI, at a store to the cartridge control
// register ($FFFC) and before a long copy (LDIR, block_transfer.cpp); the
// core runs that instruction. The addresses passed to the kit are the
// original's, because the kit reads each instruction's operands and cycle
// cost from the player's ROM.
#include "game/scenes.h"
#include "game/menu_kit.h"
#include <tuple>
namespace psmodern::game {
void register_block_transfer(RoutineRegistry&,RoutineEntry);
namespace {
namespace mk=menu_kit;
using mk::Exit;

// The ways a segment ends: the core runs the named instruction.
Exit call_at(std::uint16_t at) { return mk::continues(at,mk::call_cycles); }
Exit return_at(std::uint16_t at) { return mk::returns(at,mk::return_cycles); }
Exit return_taken_at(std::uint16_t at) { return mk::returns(at,mk::taken_conditional_return_cycles); }
Exit relative_jump_at(std::uint16_t at) { return mk::continues(at,12); }       // `jr e`
Exit jump_at(std::uint16_t at) { return mk::continues(at,mk::jump_cycles); }     // `jp cc,nn`: 10 either way
Exit disable_interrupts_at(std::uint16_t at) { return mk::continues(at,4); }     // `di`
Exit video_address_at(std::uint16_t at) { return mk::continues(at,11); }         // `rst 08h`
Exit cartridge_store_at(std::uint16_t at) { return mk::continues(at,13); }       // `ld ($FFFC),a`

// rra: A rotated right through the carry; S, Z and P/V kept, H and N cleared,
// the undocumented bits from the result (as the core).
template<class Z> void rotate_a_right(Z& z, std::uint16_t at) {
    z.one_byte(at,4);
    const auto before=z.r.a;
    z.r.a=static_cast<std::uint8_t>((before>>1)|((z.r.f&mk::carry_flag)<<7));
    z.r.f=static_cast<std::uint8_t>((z.r.f&(mk::sign_flag|mk::zero_flag|mk::parity_flag))|
        (z.r.a&(mk::y_flag|mk::x_flag))|(before&1));
}

// ---- The frame: 00:3EB9-3F25 ----------------------------------------------------------------------
// 00:3EB9: wait out a pause (CALL NZ 00:00F1).
template<class Z> Exit frame_pause_test(Z& z) {
    z.load_a_direct(0x3EB9);                                         // the pause flag (C212)
    z.or_a(0x3EBC);
    return call_at(0x3EBD);
}
// 00:3EC0: IX = the cursor record (C784), then the frame wait 00:0052; the
// core then calls the cursor sprites 01:40C5 at 3EC9.
template<class Z> Exit frame_wait(Z& z) {
    z.load_index(0x3EC0,mk::Index::ix);
    z.load_immediate(0x3EC4,z.r.a);
    return call_at(0x3EC6);
}
// 00:3ECC: no button 1 or 2 just pressed: the directions (JP Z 00:3F73).
template<class Z> Exit button_test(Z& z) {
    z.load_a_direct(0x3ECC);                                         // the pressed buttons (C205)
    z.and_immediate(0x3ECF);
    return jump_at(0x3ED1);
}
// 00:3ED4: button 1 erases (JP NZ 00:3F11); button 2 goes on to the key.
template<class Z> Exit button_one_test(Z& z) {
    z.and_immediate(0x3ED4);
    return jump_at(0x3ED6);
}
// 00:3EF5: the text position C781 (HL) steps forward, up to 25h; returns.
template<class Z> Exit advance_position(Z& z) {
    z.load_from_hl(0x3EF5,z.r.a);
    z.compare_immediate(0x3EF6);                                     // 25h, the last name character
    if (z.zero()) return return_taken_at(0x3EF8);
    z.return_not_taken(0x3EF8);
    z.increment_at_hl(0x3EF9);
    return return_at(0x3EFA);
}
// 00:3ED9: button 2 on the key C788. The next key (5Ch) steps the position
// forward: the original jumps to the load at 3EF2, which is rendered here with
// the step; the EI before that load (3EF1) belongs to the typing path, whose
// load the core runs before the entry 3EF5. The back and end keys take the
// JR NC at 3EE0; a character falls through to 3EE2.
template<class Z> Exit key_pressed(Z& z) {
    z.load_a_direct(0x3ED9);
    z.compare_immediate(0x3EDC);                                     // 5Ch
    if (z.relative_if(0x3EDE,z.zero())) {
        z.load_hl(0x3EF2);                                           // the text position (C781)
        return advance_position(z);
    }
    return mk::relative_if_exit(0x3EE0,!z.carry());
}
// 00:3EE2 (a character) and 00:3F11 (button 1, the erase): the character in A
// (the key, or 20h, a space, at 3F17) is stored at the text position of the
// name buffer, C700 + C781, then the cell is found (01:4117).
template<class Z> Exit type_character(Z& z) {
    z.load_pair_direct(0x3EE2,z.r.d,z.r.e);                          // E = the text position
    z.load_immediate(0x3EE6,z.r.d);                                  // D = C7h
    z.store_a_to_pair(0x3EE8,z.de());
    return call_at(0x3EE9);
}
template<class Z> Exit erase_character(Z& z) {
    z.load_pair_direct(0x3F11,z.r.d,z.r.e);
    z.load_immediate(0x3F15,z.r.d);
    z.load_immediate(0x3F17,z.r.a);                                  // a space
    z.store_a_to_pair(0x3F19,z.de());
    return call_at(0x3F1A);
}
// 00:3EEC and 00:3F1D: the character back in A, then the DI before the glyph
// upload 01:414F (the EI follows at 3EF1 or 3F22).
template<class Z> Exit reload_character(Z& z, std::uint16_t at) {
    z.load_a_from_pair(at,z.de());
    return disable_interrupts_at(static_cast<std::uint16_t>(at+1));
}
// 00:3EFB: the end key (5Eh) confirms (JR Z 00:3F26); the back key (5Dh)
// falls through to 3EFF.
template<class Z> Exit end_key_test(Z& z) {
    z.compare_immediate(0x3EFB);                                     // 5Eh
    return mk::relative_if_exit(0x3EFD,z.zero());
}
// 00:3EFF: the text position steps back (also after an erase, by the jump at
// 3F23), down to 21h, the first name character; with bit 0 of C780 set the
// limit is 0 instead.
template<class Z> Exit back_position(Z& z) {
    z.load_a_direct(0x3EFF);
    z.load_immediate(0x3F02,z.r.c);                                  // 0
    rotate_a_right(z,0x3F04);
    if (!z.relative_if(0x3F05,z.carry())) z.load_immediate(0x3F07,z.r.c);   // 21h
    z.load_hl(0x3F09);
    z.load_from_hl(0x3F0C,z.r.a);
    z.compare_register(0x3F0D,z.r.c);
    if (z.zero()) return return_taken_at(0x3F0E);
    z.return_not_taken(0x3F0E);
    z.decrement_at_hl(0x3F0F);
    return return_at(0x3F10);
}

// ---- The confirmation: 00:3F26-3F68 ----------------------------------------------------------------
// 00:3F26: the confirmed name is copied into the slot. First the five
// characters (C721-C725) go to C778. Then the slot number (the dialogue number
// C2C5, 1-5) chooses the slot's name address in cartridge RAM from the word
// table at 3F69h (DE). The name as drawn is the 10-byte row of tile words at
// D19A in the screen copy and the row below it; the first is copied to the
// slot by the LDIR at 3F47, after cartridge RAM is switched on (stored at
// 3F44).
template<class Z> Exit confirm_name(Z& z) {
    z.load_hl(0x3F26);
    z.load_de(0x3F29);
    z.load_bc(0x3F2C);
    z.block_copy(0x3F2F);                                            // the five characters to C778
    z.load_hl_direct(0x3F31);                                        // the slot number
    z.add_to_hl(0x3F34,z.hl());
    z.load_de(0x3F35);
    z.add_to_hl(0x3F38,z.de());
    z.load_from_hl(0x3F39,z.r.e);                                    // DE = the slot's name address
    z.increment_hl(0x3F3A);
    z.load_from_hl(0x3F3B,z.r.d);
    z.load_hl(0x3F3C);                                               // D19A, the drawn name
    z.load_bc(0x3F3F);                                               // 10 bytes
    z.load_immediate(0x3F42,z.r.a);                                  // 08h: cartridge RAM on
    return cartridge_store_at(0x3F44);
}
// 00:3F49: the second row. In cartridge RAM the slot window's rows are 18
// bytes apart (8 more after the 10 copied); in the screen copy 64 bytes apart
// (36h more). The core loads the count (10) at 3F51; the LDIR at 3F53 is an
// entry of its own.
template<class Z> Exit name_second_row(Z& z) {
    z.load_immediate(0x3F49,z.r.c);
    z.exchange_de_hl(0x3F4B);
    z.add_to_hl(0x3F4C,z.bc());
    z.exchange_de_hl(0x3F4D);
    z.load_immediate(0x3F4E,z.r.c);
    z.add_to_hl(0x3F50,z.bc());
    return mk::continues(0x3F51,7);                                  // `ld c,n`
}
// 00:3F55: cartridge RAM off (80h, stored at 3F57).
template<class Z> Exit confirm_cartridge_off(Z& z) {
    z.load_immediate(0x3F55,z.r.a);
    return cartridge_store_at(0x3F57);
}
// 00:3F5A: the next game mode: 0Ah for a game saved in a dungeon (saved mode
// C316 = 0Bh), 0Ch otherwise; then return.
template<class Z> Exit next_mode(Z& z) {
    z.load_a_direct(0x3F5A);
    z.compare_immediate(0x3F5D);                                     // 0Bh
    z.load_immediate(0x3F5F,z.r.a);                                  // 0Ah
    if (!z.relative_if(0x3F61,z.zero())) z.load_immediate(0x3F63,z.r.a);   // 0Ch
    z.store_a_direct(0x3F65);                                        // the game mode (C202)
    return return_at(0x3F68);
}

// ---- The directions: 00:3F73-01:402B ----------------------------------------------------------------
// 00:3F73: the four directions in the order of the pressed bits (up, down,
// left, right). A new press sets the repeat counter C789 to 18h (24 frames)
// and jumps to that direction's move. Without a new press, a held direction
// (bits of C204) calls the repeat countdown 01:402C first; nothing held
// returns. Each exit is the instruction the core runs next: the JR to the
// move after a press, or the countdown's call.
template<class Z> Exit read_directions(Z& z) {
    auto pressed=[&](std::uint16_t load, std::uint16_t store, std::uint16_t jump) -> Exit {
        z.load_immediate(load,z.r.a);                                // 18h
        z.store_a_direct(store);                                     // C789
        return relative_jump_at(jump);
    };
    z.load_a_direct(0x3F73);                                         // the pressed buttons (C205)
    rotate_a_right(z,0x3F76);
    if (z.relative_if(0x3F77,z.carry())) return pressed(0x3FB7,0x3FB9,0x3FBC);   // up
    rotate_a_right(z,0x3F79);
    if (z.relative_if(0x3F7A,z.carry())) return pressed(0x3FCE,0x3FD0,0x3FD3);   // down
    rotate_a_right(z,0x3F7C);
    if (z.relative_if(0x3F7D,z.carry())) return pressed(0x3FEB,0x3FED,0x3FF0);   // left
    rotate_a_right(z,0x3F7F);
    if (z.relative_if(0x3F80,z.carry())) return pressed(0x3FA0,0x3FA2,0x3FA5);   // right
    z.load_a_direct(0x3F82);                                         // the held buttons (C204)
    rotate_a_right(z,0x3F85);
    if (z.relative_if(0x3F86,z.carry())) return call_at(0x3FA7);    // up
    rotate_a_right(z,0x3F88);
    if (z.relative_if(0x3F89,z.carry())) return call_at(0x3FBE);    // down
    rotate_a_right(z,0x3F8B);
    if (z.relative_if(0x3F8C,z.carry())) return call_at(0x3FD5);    // left
    rotate_a_right(z,0x3F8E);
    if (!z.carry()) return return_taken_at(0x3F8F);
    z.return_not_taken(0x3F8F);
    return call_at(0x3F90);                                          // right
}
// 00:3F94 (right), 00:3FAB (up), 00:3FC2 (down): a move's parameters, then the
// JR to the move 00:3FF2. B is the coordinate the cursor stops at and C the
// step of that coordinate in pixels; DE the step of the cursor's screen-copy
// address (two bytes a key, 128 bytes, two screen rows, a keyboard row); IY
// the coordinate (C784 X, C785 Y).
template<class Z> Exit move_parameters(Z& z, std::uint16_t at) {
    z.load_bc(at);
    z.load_de(static_cast<std::uint16_t>(at+3));
    z.load_index(static_cast<std::uint16_t>(at+6),mk::Index::iy);
    return relative_jump_at(static_cast<std::uint16_t>(at+10));
}
// 00:3F93, 00:3FAA, 00:3FC1: after the repeat countdown; not yet (NZ): return.
template<class Z> Exit held_move(Z& z, std::uint16_t at) {
    if (!z.zero()) return return_taken_at(at);
    z.return_not_taken(at);
    return move_parameters(z,static_cast<std::uint16_t>(at+1));
}
// 00:3FD9: left, whose parameters are loaded around one more rule: while the
// cursor is on the next key (5Ch) it does not move left.
template<class Z> Exit move_left(Z& z) {
    z.load_bc(0x3FD9);
    z.load_a_direct(0x3FDC);                                         // the key (C788)
    z.compare_immediate(0x3FDF);                                     // 5Ch
    if (z.zero()) return return_taken_at(0x3FE1);
    z.return_not_taken(0x3FE1);
    z.load_de(0x3FE2);
    z.load_index(0x3FE5,mk::Index::iy);
    return relative_jump_at(0x3FE9);
}
// 00:3FD8: left after the repeat countdown.
template<class Z> Exit held_move_left(Z& z) {
    if (!z.zero()) return return_taken_at(0x3FD8);
    z.return_not_taken(0x3FD8);
    return move_left(z);
}
// 00:3FF2: one step. At the stop coordinate: return. Otherwise the coordinate
// and the screen-copy address C786 take their steps and the cell there is
// read; an empty cell takes the JR Z at 01:4004 back for another step. The
// routine runs across the end of the first fixed bank (00:3FFF, 01:4002), so
// its bound needs ROM bank 1 at 4000h (see the registration).
template<class Z> Exit move_cursor(Z& z) {
    z.load_from_indexed(0x3FF2,mk::Index::iy,z.r.a);
    z.compare_register(0x3FF5,z.r.b);
    if (z.zero()) return return_taken_at(0x3FF6);
    z.return_not_taken(0x3FF6);
    z.add_register(0x3FF7,z.r.c);
    z.store_to_indexed(0x3FF8,mk::Index::iy,z.r.a);
    z.load_hl_direct(0x3FFB);
    z.add_to_hl(0x3FFE,z.de());
    z.store_hl_direct(0x3FFF);
    z.load_from_hl(0x4002,z.r.a);
    z.or_a(0x4003);
    return mk::relative_if_exit(0x4004,z.zero());
}
// 01:4006: the same key as before (the second half of a wide key): another
// step (JR Z at 4009).
template<class Z> Exit same_key_test(Z& z) {
    z.compare_indexed(0x4006,mk::Index::ix);                         // IX + 4 = C788
    return mk::relative_if_exit(0x4009,z.zero());
}
// 01:400B: the new key goes to C788. A character returns. An action key puts
// the cursor at that key's fixed X (C784) and screen-copy address (C786),
// whichever of its two halves was reached: next (5Ch), back (5Dh), end.
template<class Z> Exit cursor_on_key(Z& z) {
    z.store_a_direct(0x400B);
    z.compare_immediate(0x400E);                                     // 5Ch
    if (z.carry()) return return_taken_at(0x4010);
    z.return_not_taken(0x4010);
    z.load_immediate(0x4011,z.r.c);
    z.load_hl(0x4013);
    bool chosen=z.relative_if(0x4016,z.zero());                      // next
    if (!chosen) {
        z.compare_immediate(0x4018);                                 // 5Dh
        z.load_immediate(0x401A,z.r.l);
        z.load_immediate(0x401C,z.r.c);
        chosen=z.relative_if(0x401E,z.zero());                       // back
        if (!chosen) {                                               // end
            z.load_immediate(0x4020,z.r.c);
            z.load_immediate(0x4022,z.r.l);
        }
    }
    z.store_hl_direct(0x4024);
    z.copy(0x4027,z.r.a,z.r.c);
    z.store_a_direct(0x4028);
    return return_at(0x402B);
}
// 01:402C: the repeat countdown C789: NZ until it reaches zero, then it
// restarts at 5 and returns Z (move now).
template<class Z> Exit repeat_countdown(Z& z) {
    z.load_hl(0x402C);
    z.decrement_at_hl(0x402F);
    if (!z.zero()) return return_taken_at(0x4030);
    z.return_not_taken(0x4030);
    z.store_to_hl(0x4031);
    return return_at(0x4033);
}

// ---- The screen: 01:4034-40BF -----------------------------------------------------------------------
// 01:4037: the operands of the name-table clear 00:0363 (DE = 7800h, a
// write at video address 3800h; BC = 300h; HL = 0), then the DI before it.
template<class Z> Exit clear_name_table(Z& z) {
    z.load_de(0x4037);
    z.load_bc(0x403A);
    z.load_hl(0x403D);
    return disable_interrupts_at(0x4040);
}
// 01:4048: after the EI at 4044 and the load the core runs at 4045: the next
// game mode, then C781-C800 cleared (the zero at C781 copied forward; the core
// loads the count at 4051 and the LDIR at 4054 is an entry of its own).
template<class Z> Exit clear_working_state(Z& z) {
    z.increment_at_hl(0x4048);                                       // game mode + 1
    z.load_hl(0x4049);
    z.store_to_hl(0x404C);
    z.load_de(0x404E);
    return mk::continues(0x4051,10);                                 // `ld bc,nn`
}
// 01:4056: the screen copy D000-D600 cleared: the core loads the count at
// 405C, the zero seed at 405F stays native and the LDIR at 4061 is an entry.
template<class Z> Exit clear_screen_copy(Z& z) {
    z.load_hl(0x4056);
    z.load_de(0x4059);
    return mk::continues(0x405C,10);                                 // `ld bc,nn`
}
// 01:4066: after the layout stream (01:4166): the name buffer C700-C737
// cleared, the text position at 21h, then the pointers of the transfer of
// the screen copy from D0C0 (540h bytes to video address 38C0h, 00:0346) and
// the DI before it.
template<class Z> Exit name_buffer_and_upload(Z& z) {
    z.load_hl(0x4066);
    z.load_de(0x4069);
    z.load_bc(0x406C);
    z.store_to_hl(0x406F);
    z.block_copy(0x4071);
    z.load_immediate(0x4073,z.r.a);
    z.store_a_direct(0x4075);                                        // C781 = 21h
    z.load_de(0x4078);
    z.load_hl(0x407B);
    z.load_bc(0x407E);
    return disable_interrupts_at(0x4081);
}
// 01:4089: after the keyboard (01:41DB): the palette (20h bytes from 41FCh)
// to the target palette C240, then the pointers of one tile (20h bytes from
// 421Ch to video address 2000h, the sprites' tile 0 that the cursor sprites
// use) and the DI before the transfer.
template<class Z> Exit palette_and_cursor_tiles(Z& z) {
    z.load_hl(0x4089);
    z.load_de(0x408C);
    z.load_bc(0x408F);
    z.block_copy(0x4092);
    z.load_de(0x4094);
    z.load_hl(0x4097);
    z.load_bc(0x409A);
    return disable_interrupts_at(0x409D);
}
// 01:40A5: after the EI at 40A1 and the load the core runs at 40A2 (DE =
// C784): the cursor record's five starting bytes (from 40C0h); both scroll
// shadows (C304, C300) and the dialogue-window flag C2D3 to zero; DE = 8006h
// for the video register write (RST 08h at 40BB), after the DI at 40BA.
template<class Z> Exit place_cursor(Z& z) {
    z.load_hl(0x40A5);
    z.load_bc(0x40A8);
    z.block_copy(0x40AB);
    z.xor_a(0x40AD);
    z.store_a_direct(0x40AE);
    z.store_a_direct(0x40B1);
    z.store_a_direct(0x40B4);
    z.load_de(0x40B7);
    return disable_interrupts_at(0x40BA);
}

// ---- The cursor sprites: 01:40C5-40FF --------------------------------------------------------------
// 01:40C8: after the cell of the text position is found (01:4117, HL). The
// cell's address gives the position marker's sprite: (HL + 3040h) x 4 leaves
// the screen row plus one in H (times 8: its Y) and the column times 8 in L
// (its X). The sprite Y list from C900: the marker, then the keyboard cursor
// at C785 (three sprites for an action key, one for a character), then the
// end mark D0h. The X and tile list from C980: the marker, then the three
// cursor sprites from C784 leftwards, 8 pixels apart.
template<class Z> Exit cursor_sprites(Z& z) {
    z.load_de(0x40C8);
    z.add_to_hl(0x40CB,z.de());
    z.add_to_hl(0x40CC,z.hl());
    z.add_to_hl(0x40CD,z.hl());
    z.load_de(0x40CE);                                               // C900
    z.copy(0x40D1,z.r.a,z.r.h);
    z.add_register(0x40D2,z.r.a);
    z.add_register(0x40D3,z.r.a);
    z.add_register(0x40D4,z.r.a);
    z.store_a_to_pair(0x40D5,z.de());                                // the marker's Y
    z.load_a_direct(0x40D6);                                         // the key (C788)
    z.compare_immediate(0x40D9);                                     // 5Ch
    z.load_a_direct(0x40DB);                                         // the cursor's Y (C785)
    z.increment_pair(0x40DE,z.r.d,z.r.e);
    z.store_a_to_pair(0x40DF,z.de());
    if (!z.relative_if(0x40E0,z.carry())) {                          // an action key: two more
        z.increment(0x40E2,z.r.e);
        z.store_a_to_pair(0x40E3,z.de());
        z.increment(0x40E4,z.r.e);
        z.store_a_to_pair(0x40E5,z.de());
    }
    z.increment(0x40E6,z.r.e);
    z.load_immediate(0x40E7,z.r.a);
    z.store_a_to_pair(0x40E9,z.de());                                // the end mark
    z.load_immediate(0x40EA,z.r.e);                                  // C980
    z.exchange_de_hl(0x40EC);
    z.copy(0x40ED,z.r.a,z.r.e);
    z.load_bc(0x40EE);                                               // B = 3, C = tile 0
    z.store_register_to_hl(0x40F1,z.r.a);                            // the marker's X
    z.increment(0x40F2,z.r.l);
    z.store_register_to_hl(0x40F3,z.r.c);
    z.load_a_direct(0x40F4);                                         // the cursor's X (C784)
    do {
        z.increment(0x40F7,z.r.l);
        z.store_register_to_hl(0x40F8,z.r.a);
        z.increment(0x40F9,z.r.l);
        z.store_register_to_hl(0x40FA,z.r.c);
        z.subtract_immediate(0x40FB);                                // 8 pixels left
    } while (z.decrement_jump(0x40FD));
    return return_at(0x40FF);
}

// ---- Redrawing the name buffer: 01:4100-4116 -------------------------------------------------------
// 01:4100: the text position C781 serves as the counter, from 38h down; each
// position's cell is found (01:4117) and its character drawn (01:413A). HL
// (the counter's address) waits on the stack across both calls, DE (the
// buffer address) across the second.
template<class Z> Exit redraw_next(Z& z) {
    z.decrement_at_hl(0x4107);
    if (z.sign()) return return_taken_at(0x4108);                    // all drawn
    z.return_not_taken(0x4108);
    z.load_from_hl(0x4109,z.r.e);                                    // DE = C700 + position
    z.push(0x410A,z.r.h,z.r.l);
    return call_at(0x410B);
}
template<class Z> Exit redraw_name(Z& z) {
    z.load_hl(0x4100);
    z.store_to_hl(0x4103);                                           // C781 = 38h
    z.load_immediate(0x4105,z.r.d);                                  // D = C7h
    return redraw_next(z);
}
// 01:410E: the character, then the glyph (01:413A) with DE kept.
template<class Z> Exit redraw_glyph(Z& z) {
    z.load_a_from_pair(0x410E,z.de());
    z.push(0x410F,z.r.d,z.r.e);
    return call_at(0x4110);
}
// 01:4113: both words back, then the JR to the next position (4107).
template<class Z> Exit redraw_glyph_done(Z& z) {
    z.pop(0x4113,z.r.d,z.r.e);
    z.pop(0x4114,z.r.h,z.r.l);
    return relative_jump_at(0x4115);
}

// ---- The cell of a text position: 01:4117-4139 -----------------------------------------------------
// 01:4117: HL = the cell of text position C781 in the screen copy. Positions
// 0-17h are the line at D146, 18h-2Fh the line at D1C6, from 30h the line at
// D246 (lines two screen rows apart). In its line, column c is at 2c bytes,
// plus 2 bytes (one cell of space) after every eight characters: the two RRA
// and the mask 06h give 2 x (c / 8).
template<class Z> Exit character_cell(Z& z) {
    z.load_a_direct(0x4117);
    z.load_hl(0x411A);                                               // D146
    z.subtract_immediate(0x411D);                                    // 18h characters a line
    bool line_found=z.relative_if(0x411F,z.carry());
    if (!line_found) {
        z.load_immediate(0x4121,z.r.l);                              // D1C6
        z.subtract_immediate(0x4123);
        line_found=z.relative_if(0x4125,z.carry());
        if (!line_found) {
            z.load_hl(0x4127);                                       // D246
            z.subtract_immediate(0x412A);
        }
    }
    z.add_immediate(0x412C);                                         // back to the column c
    z.copy(0x412E,z.r.c,z.r.a);
    z.add_register(0x412F,z.r.a);
    z.add_register(0x4130,z.r.l);
    z.copy(0x4131,z.r.l,z.r.a);                                      // + 2c
    z.copy(0x4132,z.r.a,z.r.c);
    rotate_a_right(z,0x4133);
    rotate_a_right(z,0x4134);
    z.and_immediate(0x4135);
    z.add_register(0x4137,z.r.l);
    z.copy(0x4138,z.r.l,z.r.a);                                      // + 2 x (c / 8)
    return return_at(0x4139);
}

// ---- The glyph: 01:413A-4165 -----------------------------------------------------------------------
// 01:413A: draw character A at the cell HL (HL is kept on the stack). A
// character is two tiles from the table at 01:7CD7 (two bytes a character):
// the second byte goes in the cell, the first in the cell one screen row above
// it (64 bytes before). Returns with A = the cell's tile, C = the tile above
// and DE = the cell.
template<class Z> Exit write_glyph(Z& z) {
    z.push(0x413A,z.r.h,z.r.l);
    z.exchange_de_hl(0x413B);                                        // DE = the cell
    z.load_hl(0x413C);
    z.copy(0x413F,z.r.c,z.r.a);
    z.load_immediate(0x4140,z.r.b);
    z.add_to_hl(0x4142,z.bc());
    z.add_to_hl(0x4143,z.bc());                                      // + 2 x the character
    z.load_from_hl(0x4144,z.r.c);
    z.increment_hl(0x4145);
    z.load_from_hl(0x4146,z.r.a);
    z.store_a_to_pair(0x4147,z.de());                                // the cell's tile
    z.load_hl(0x4148);
    z.add_to_hl(0x414B,z.de());
    z.store_register_to_hl(0x414C,z.r.c);                            // the tile above
    z.pop(0x414D,z.r.h,z.r.l);
    return return_at(0x414E);
}
// 01:4152: after the glyph (01:414F calls 01:413A first), the same two tiles
// go to video memory: the cell's name-table address is its screen-copy
// address minus 5800h (D000 becomes 7800h, a write at 3800h), set by RST 08h.
template<class Z> Exit glyph_video_address(Z& z) {
    z.copy(0x4152,z.r.b,z.r.a);                                      // B = the cell's tile
    z.copy(0x4153,z.r.a,z.r.h);
    z.subtract_immediate(0x4154);
    z.copy(0x4156,z.r.h,z.r.a);
    z.exchange_de_hl(0x4157);
    return video_address_at(0x4158);
}
// 01:4159: the cell's tile out, then the address one row above (- 40h).
template<class Z> Exit glyph_video_upper(Z& z) {
    z.copy(0x4159,z.r.a,z.r.b);
    z.output(0x415A);
    z.load_hl(0x415C);
    z.add_to_hl(0x415F,z.de());
    z.exchange_de_hl(0x4160);
    return video_address_at(0x4161);
}
// 01:4162: the tile above out; return.
template<class Z> Exit glyph_video_upper_tile(Z& z) {
    z.copy(0x4162,z.r.a,z.r.c);
    z.output(0x4163);
    return return_at(0x4165);
}

// ---- The layout: 01:4166-41FB ----------------------------------------------------------------------
// 01:4166: the screen's layout stream (from 423Ch) drawn into the screen
// copy from D100. Only the tile byte of each two-byte cell is written. A
// control byte of zero ends the stream; otherwise its low six bits (or the
// whole byte below 80h) are a count n:
//   below 80h        n cells of the next byte;
//   80h-BFh          n cells counting up from the next byte, each followed by
//                    a cell of one fixed tile, the byte stored at 01:4180
//                    (C0h in the US release, 20h in the Brazilian one);
//   C0h and above    the next n bytes, one a cell.
// Then the frame around the name area (screen rows 4-18, columns 3-27): the
// left column from D146 first (01:41C8).
template<class Z> Exit layout_stream(Z& z) {
    z.load_hl(0x4166);
    z.load_de(0x4169);
    for (;;) {
        z.load_from_hl(0x416C,z.r.a);
        z.increment_hl(0x416D);
        z.or_a(0x416E);
        if (z.relative_if(0x416F,z.zero())) break;                   // the end
        if (z.jump_if(0x4171,!z.sign())) {                           // a repeated cell
            z.copy(0x4198,z.r.b,z.r.a);
            z.load_from_hl(0x4199,z.r.a);
            do {
                z.store_a_to_pair(0x419A,z.de());
                z.increment_pair(0x419B,z.r.d,z.r.e);
                z.increment_pair(0x419C,z.r.d,z.r.e);
            } while (z.decrement_jump(0x419D));
            z.increment_hl(0x419F);
            z.relative(0x41A0);
        } else {
            z.test_bit(0x4174,6,z.r.a);
            if (z.relative_if(0x4176,!z.zero())) {                   // literal cells
                z.and_immediate(0x418B);
                z.copy(0x418D,z.r.c,z.r.a);
                z.load_immediate(0x418E,z.r.b);
                do {
                    z.load_increment(0x4190);
                    z.increment_pair(0x4192,z.r.d,z.r.e);
                } while (z.jump_if(0x4193,z.parity_even()));
                z.relative(0x4196);
            } else {                                                 // counting cells
                z.and_immediate(0x4178);
                z.copy(0x417A,z.r.b,z.r.a);
                z.load_from_hl(0x417B,z.r.a);
                do {
                    z.store_a_to_pair(0x417C,z.de());
                    z.increment_pair(0x417D,z.r.d,z.r.e);
                    z.increment_pair(0x417E,z.r.d,z.r.e);
                    z.exchange_de_hl(0x417F);
                    z.store_to_hl(0x4180);                           // the regional fixed tile
                    z.exchange_de_hl(0x4182);
                    z.increment_pair(0x4183,z.r.d,z.r.e);
                    z.increment_pair(0x4184,z.r.d,z.r.e);
                    z.increment(0x4185,z.r.a);
                } while (z.decrement_jump(0x4186));
                z.increment_hl(0x4188);
                z.relative(0x4189);
            }
        }
    }
    z.load_hl(0x41A2);
    z.load_de(0x41A5);
    return call_at(0x41A8);
}
// 01:41AB: the bottom row of the frame (row 18): HL is just past the left
// column, on the attribute byte of its first cell; 24 attribute bytes there
// get one value (01:41D5), which mirrors the stream's tiles vertically.
template<class Z> Exit layout_bottom_attributes(Z& z) {
    z.increment_hl(0x41AB);
    z.load_bc(0x41AC);
    return call_at(0x41AF);
}
// 01:41B2: the attribute bytes of the two right corners (the end of that
// row, and D137 on row 4), then the 24 of the top row from D107 (01:41D5).
template<class Z> Exit layout_corner_attributes(Z& z) {
    z.store_to_hl(0x41B2);
    z.load_hl(0x41B4);
    z.store_to_hl(0x41B7);
    z.load_hl(0x41B9);
    z.load_bc(0x41BC);
    return call_at(0x41BF);
}
// 01:41C8: a column of the frame: 13 cells (tile D, attribute E), one screen
// row apart (1 + 3Fh bytes); return. 01:41C2 is the right column (from D176,
// with an attribute that mirrors the tile horizontally), falling into it.
template<class Z> Exit border_column(Z& z) {
    z.load_immediate(0x41C8,z.r.a);                                  // 13 rows
    z.load_bc(0x41CA);
    do {
        z.store_register_to_hl(0x41CD,z.r.d);
        z.increment(0x41CE,z.r.l);
        z.store_register_to_hl(0x41CF,z.r.e);
        z.add_to_hl(0x41D0,z.bc());
        z.decrement(0x41D1,z.r.a);
    } while (z.relative_if(0x41D2,!z.zero()));
    return return_at(0x41D4);
}
template<class Z> Exit right_border(Z& z) {
    z.load_hl(0x41C2);
    z.load_de(0x41C5);
    return border_column(z);
}
// 01:41D5: C into every other byte from HL, B times (one byte a cell); return.
template<class Z> Exit every_other_byte(Z& z) {
    do {
        z.store_register_to_hl(0x41D5,z.r.c);
        z.increment(0x41D6,z.r.l);
        z.increment(0x41D7,z.r.l);
    } while (z.decrement_jump(0x41D8));
    return return_at(0x41DA);
}
// 01:41DB: the keyboard: four rows of eleven keys from the table at 4280h,
// drawn from D30A; after each key a blank cell (tile 0); a row's 44 bytes
// plus 54h start the next row two screen rows down.
template<class Z> Exit keyboard(Z& z) {
    z.load_hl(0x41DB);
    z.load_de(0x41DE);
    z.load_immediate(0x41E1,z.r.a);                                  // 4 rows
    do {
        z.load_bc(0x41E3);                                           // 11 keys
        do {
            z.load_increment(0x41E6);                                // the key
            z.increment_pair(0x41E8,z.r.d,z.r.e);
            z.exchange_de_hl(0x41E9);
            z.store_to_hl(0x41EA);                                   // the blank cell
            z.exchange_de_hl(0x41EC);
            z.increment_pair(0x41ED,z.r.d,z.r.e);
            z.increment_pair(0x41EE,z.r.d,z.r.e);
        } while (z.jump_if(0x41EF,z.parity_even()));
        z.exchange_de_hl(0x41F2);
        z.load_bc(0x41F3);
        z.add_to_hl(0x41F6,z.bc());
        z.exchange_de_hl(0x41F7);
        z.decrement(0x41F8,z.r.a);
    } while (z.relative_if(0x41F9,!z.zero()));
    return return_at(0x41FB);
}
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address, with its unit, exits and permissions. A note
// names the call or jump the entry follows.
void register_scene_names(RoutineRegistry& registry) {
    auto F=[](std::uint16_t at, std::int8_t stack=0) { return mk::core_final_at(at,stack); };
    auto R=[](std::uint16_t at) { return mk::returned_at(at); };
    auto C=[](std::uint16_t at) { return mk::conditional_at(at); };
    // A final `ld ($FFFC),a`: the core remaps cartridge RAM at that moment.
    auto cartridge=[](std::uint16_t at) { auto exit=mk::core_final_at(at); exit.cartridge_control=true; return exit; };
    // A final DI, run by the core.
    auto di=[](std::uint16_t at) { auto exit=mk::core_final_at(at); exit.interrupt_mask=InterruptMask::disable; return exit; };
    auto add=[&](RoutineEntry entry, RoutineEntry unit, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        mk::add_segment(registry,entry,mk::segment(unit,std::move(exits),permissions),logic);
    };
    // A lone EI, run by the core (four cycles).
    auto enable_interrupts=[&](RoutineEntry entry) {
        auto exit=mk::core_final_at(entry.address);
        exit.interrupt_mask=InterruptMask::enable;
        auto spec=mk::segment(entry,{exit});
        spec.single_instruction=true;
        registry.add(entry,spec,[](const Machine&) { return 4; },[=](RoutineContext& c) { c.finish_core(entry.address); });
    };
    auto copy=[&](RoutineEntry entry) { register_block_transfer(registry,entry); };
    constexpr RoutineEntry e3EB9{0,0x3EB9}, e3EC0{0,0x3EC0}, e3ECC{0,0x3ECC}, e3ED4{0,0x3ED4}, e3ED9{0,0x3ED9};
    constexpr RoutineEntry e3EE2{0,0x3EE2}, e3EEC{0,0x3EEC}, e3EF5{0,0x3EF5}, e3EFB{0,0x3EFB}, e3EFF{0,0x3EFF};
    constexpr RoutineEntry e3F11{0,0x3F11}, e3F1D{0,0x3F1D}, e3F26{0,0x3F26}, e3F49{0,0x3F49}, e3F55{0,0x3F55};
    constexpr RoutineEntry e3F5A{0,0x3F5A}, e3F73{0,0x3F73}, e3FD8{0,0x3FD8}, e3FD9{0,0x3FD9};
    constexpr mk::Permissions none{}, calls{.scratch_stack=2};

    // ---- the frame ----
    add(e3EB9,e3EB9,{F(0x3EBD)},calls,[](auto& z) -> Exit { return frame_pause_test(z); });
    add(e3EC0,e3EC0,{F(0x3EC6)},calls,[](auto& z) -> Exit { return frame_wait(z); });                          // after the pause test
    add(e3ECC,e3ECC,{C(0x3ED1)},none,[](auto& z) -> Exit { return button_test(z); });                          // after 01:40C5
    add(e3ED4,e3ED4,{C(0x3ED6)},none,[](auto& z) -> Exit { return button_one_test(z); });
    add(e3ED9,e3ED9,{R(0x3EF8),R(0x3EFA),C(0x3EE0)},none,[](auto& z) -> Exit { return key_pressed(z); });
    add(e3EE2,e3EE2,{F(0x3EE9)},calls,[](auto& z) -> Exit { return type_character(z); });
    add(e3EEC,e3EEC,{di(0x3EED)},none,[](auto& z) -> Exit { return reload_character(z,0x3EEC); });            // after 01:4117
    enable_interrupts({0,0x3EF1});
    add(e3EF5,e3EF5,{R(0x3EF8),R(0x3EFA)},none,[](auto& z) -> Exit { return advance_position(z); });          // after the EI-delayed load at 3EF2
    add(e3EFB,e3EFB,{C(0x3EFD)},none,[](auto& z) -> Exit { return end_key_test(z); });                        // the JR NC at 3EE0
    add(e3EFF,e3EFF,{R(0x3F0E),R(0x3F10)},none,[](auto& z) -> Exit { return back_position(z); });
    add(e3F11,e3F11,{F(0x3F1A)},calls,[](auto& z) -> Exit { return erase_character(z); });                    // the JP NZ at 3ED6
    add(e3F1D,e3F1D,{di(0x3F1E)},none,[](auto& z) -> Exit { return reload_character(z,0x3F1D); });            // after 01:4117
    enable_interrupts({0,0x3F22});

    // ---- the confirmation ----
    add(e3F26,e3F26,{cartridge(0x3F44)},none,[](auto& z) -> Exit { return confirm_name(z); });                // the JR Z at 3EFD
    copy({0,0x3F47});
    add(e3F49,e3F49,{F(0x3F51)},calls,[](auto& z) -> Exit { return name_second_row(z); });
    copy({0,0x3F53});
    add(e3F55,e3F55,{cartridge(0x3F57)},none,[](auto& z) -> Exit { return confirm_cartridge_off(z); });
    add(e3F5A,e3F5A,{R(0x3F68)},none,[](auto& z) -> Exit { return next_mode(z); });

    // ---- the directions ----
    add(e3F73,e3F73,{R(0x3F8F),F(0x3F90),F(0x3FA7),F(0x3FBE),F(0x3FD5),F(0x3FA5),F(0x3FBC),F(0x3FD3),F(0x3FF0)},calls,
        [](auto& z) -> Exit { return read_directions(z); });                                                    // the JP Z at 3ED1
    for (const auto [at,move,jump]:{std::tuple{std::uint16_t{0x3F93},std::uint16_t{0x3F94},std::uint16_t{0x3F9E}},
                                     std::tuple{std::uint16_t{0x3FAA},std::uint16_t{0x3FAB},std::uint16_t{0x3FB5}},
                                     std::tuple{std::uint16_t{0x3FC1},std::uint16_t{0x3FC2},std::uint16_t{0x3FCC}}}) {
        const RoutineEntry held{0,at}, moved{0,move};
        add(held,held,{R(at),F(jump)},none,[=](auto& z) -> Exit { return held_move(z,at); });                    // after 01:402C
        add(moved,moved,{F(jump)},calls,[=](auto& z) -> Exit { return move_parameters(z,move); });              // the JR after a press
    }
    add(e3FD8,e3FD8,{R(0x3FD8),R(0x3FE1),F(0x3FE9)},none,[](auto& z) -> Exit { return held_move_left(z); });  // after 01:402C
    add(e3FD9,e3FD9,{R(0x3FE1),F(0x3FE9)},none,[](auto& z) -> Exit { return move_left(z); });                 // the JR after a press
    // The move runs from 00:3FF2 into 01:4002-4004 (slot 1), so its path is
    // only known with ROM bank 1 mapped at 4000h; otherwise the bound is the
    // uncertain maximum and the segment falls back.
    const RoutineEntry move{0,0x3FF2};
    const auto move_logic=[](auto& z) -> Exit { return move_cursor(z); };
    registry.add(move,mk::segment(move,{R(0x3FF6),C(0x4004)}),[=](const Machine& m) {
        if (m.memory().mapping(0x4000)!=Memory::Mapping{Memory::Kind::rom,1}) return mk::uncertain_maximum;
        return mk::cost(m,move_logic,mk::uncertain_maximum);
    },[=](RoutineContext& c) { mk::run(c,move_logic); });
    add({1,0x4006},{1,0x4006},{C(0x4009)},none,[](auto& z) -> Exit { return same_key_test(z); });
    add({1,0x400B},{1,0x400B},{R(0x4010),R(0x402B)},none,[](auto& z) -> Exit { return cursor_on_key(z); });
    add({1,0x402C},{1,0x402C},{R(0x4030),R(0x4033)},none,[](auto& z) -> Exit { return repeat_countdown(z); });

    // ---- the screen ----
    add({1,0x4037},{1,0x4037},{di(0x4040)},none,[](auto& z) -> Exit { return clear_name_table(z); });         // after 01:7B05
    enable_interrupts({1,0x4044});
    add({1,0x4048},{1,0x4048},{F(0x4051)},none,[](auto& z) -> Exit { return clear_working_state(z); });       // after the EI-delayed load at 4045
    copy({1,0x4054});
    add({1,0x4056},{1,0x4048},{F(0x405C)},none,[](auto& z) -> Exit { return clear_screen_copy(z); });
    copy({1,0x4061});
    add({1,0x4066},{1,0x4066},{di(0x4081)},none,[](auto& z) -> Exit { return name_buffer_and_upload(z); });
    enable_interrupts({1,0x4085});
    add({1,0x4089},{1,0x4089},{di(0x409D)},none,[](auto& z) -> Exit { return palette_and_cursor_tiles(z); });
    enable_interrupts({1,0x40A1});
    add({1,0x40A5},{1,0x40A5},{di(0x40BA)},none,[](auto& z) -> Exit { return place_cursor(z); });             // after the EI-delayed load at 40A2
    enable_interrupts({1,0x40BC});

    // ---- the cursor sprites, the redraw, the cell, the glyph ----
    add({1,0x40C8},{1,0x40C8},{R(0x40FF)},none,[](auto& z) -> Exit { return cursor_sprites(z); });             // after 01:4117
    add({1,0x4100},{1,0x4100},{R(0x4108),F(0x410B,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return redraw_name(z); });
    add({1,0x4107},{1,0x4107},{R(0x4108),F(0x410B,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return redraw_next(z); });  // the JR at 4115
    add({1,0x410E},{1,0x410E},{F(0x4110,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return redraw_glyph(z); });           // after 01:4117
    add({1,0x4113},{1,0x410E},{F(0x4115,4)},{.inherited_stack=4},[](auto& z) -> Exit { return redraw_glyph_done(z); });     // after 01:413A
    add({1,0x4117},{1,0x4117},{R(0x4139)},none,[](auto& z) -> Exit { return character_cell(z); });
    add({1,0x413A},{1,0x413A},{R(0x414E)},calls,[](auto& z) -> Exit { return write_glyph(z); });
    add({1,0x4152},{1,0x4152},{F(0x4158)},calls,[](auto& z) -> Exit { return glyph_video_address(z); });      // after 01:413A
    add({1,0x4159},{1,0x4159},{F(0x4161)},calls,[](auto& z) -> Exit { return glyph_video_upper(z); });        // after the RST 08h
    add({1,0x4162},{1,0x4162},{R(0x4165)},none,[](auto& z) -> Exit { return glyph_video_upper_tile(z); });    // after the RST 08h

    // ---- the layout ----
    add({1,0x4166},{1,0x4166},{F(0x41A8)},calls,[](auto& z) -> Exit { return layout_stream(z); });
    add({1,0x41AB},{1,0x41AB},{F(0x41AF)},calls,[](auto& z) -> Exit { return layout_bottom_attributes(z); });        // after 01:41C8
    add({1,0x41B2},{1,0x41B2},{F(0x41BF)},calls,[](auto& z) -> Exit { return layout_corner_attributes(z); });           // after 01:41D5
    add({1,0x41C2},{1,0x41C2},{R(0x41D4)},none,[](auto& z) -> Exit { return right_border(z); });              // after 01:41D5
    add({1,0x41C8},{1,0x41C8},{R(0x41D4)},none,[](auto& z) -> Exit { return border_column(z); });
    add({1,0x41D5},{1,0x41D5},{R(0x41DA)},none,[](auto& z) -> Exit { return every_other_byte(z); });
    add({1,0x41DB},{1,0x41DB},{R(0x41FB)},none,[](auto& z) -> Exit { return keyboard(z); });
}
}
