// SPDX-License-Identifier: GPL-3.0-or-later
// Scenes, part title (milestone 13): the title screen and its two choices,
// the saved-game conversation, the opening loader and the spaceship flight.
// The other scene parts register themselves from register_scenes below
// (scenes_name.cpp, scenes_script.cpp, scenes_animation.cpp,
// scenes_persistence.cpp, scenes_interaction.cpp, scenes_visual.cpp,
// scenes_quests.cpp); the planned entries are scenes::segments in
// game/scenes.h.
//
// The routines of this file:
//   00:05D0  title setup: game mode 2 (the title screen).
//   00:05D6  the title screen: a two-choice cursor. The first choice starts a
//            new game (00:05E8): work RAM C300-C6FF cleared, the first
//            character equipped and unlocked, the world's starting values
//            set, then the opening scene (01:42AC) and game mode 8. The
//            second goes to Continue (00:0634).
//   00:0634  Continue: when all five slot flags in cartridge RAM are clear it
//            starts a new game instead; otherwise it loads the window
//            graphics and holds a conversation of yes/no questions. Loading
//            leads to a slot choice (an empty slot asks again), then the
//            slot's 1 KiB is copied from cartridge RAM to C300 and play
//            resumes in the saved mode. Erasing leads to a slot choice and a
//            confirmation, then the slot's flag is cleared and its name
//            blanked (00:06FA), and the title screen comes back.
//   00:073A  the slot-flag check used by both choices: Z set when the slot
//            chosen (the dialogue number C2C5) is empty.
//   00:074D  the opening loader: palette, temporary state, graphics and
//            layout, sound and picture.
//   00:086F  the spaceship flight between planets: take-off over the departure map, the flight through space,
//            landing over the destination map; with its scroll step 00:0998,
//            descent step 00:09E9, route colours 00:0A31 and the map loader
//            00:0A5C / 00:0A74.
//
// How the code is cut. A segment ends where the original calls or jumps into
// a routine of another part (a dialogue, the cursor loop, a frame wait, a
// graphics loader); that call is rendered as the segment's last instruction
// and the instruction after a call is the next entry. Every store to the
// cartridge control register ($FFFC), every DI or EI and every long copy
// (LDIR, block_transfer.cpp) also ends a segment, so that memory is remapped,
// interrupts change and copies can pause at their original instruction
// boundaries; since milestone 14 every such final instruction is rendered
// and the instructions that used to run between two segments are one-
// instruction segments of their own (the last block of register_scenes). The
// addresses passed to the kit are the original's, because the kit reads each
// instruction's operands and cycle cost from the player's ROM; no ROM value
// is written here.
#include "game/joins.h"
#include "game/scenes.h"
#include "game/routines.h"
#include "game/menu_kit.h"
namespace psmodern::game {
void register_scene_names(RoutineRegistry&);
void register_scene_scripts(RoutineRegistry&);
void register_scene_animation(RoutineRegistry&);
void register_scene_persistence(RoutineRegistry&);
void register_scene_interactions(RoutineRegistry&);
void register_scene_visuals(RoutineRegistry&);
void register_scene_quests(RoutineRegistry&);
namespace {
namespace mk=menu_kit;
using mk::Exit;

// The ways a segment ends: the final instruction, rendered like the others.
template<class Z> Exit call_at(Z& z, std::uint16_t at) { return z.final_call(at); }
template<class Z> Exit jump_at(Z& z, std::uint16_t at) { return z.final_jump(at); }
template<class Z> Exit return_at(Z& z, std::uint16_t at) { return z.final_return(at); }
template<class Z> Exit cartridge_store_at(Z& z, std::uint16_t at) {   // `ld ($FFFC),a`
    z.store_a_direct(at);
    return mk::fall_through(at,static_cast<std::uint16_t>(at+3));
}
template<class Z> Exit seed_store_at(Z& z, std::uint16_t at) {   // `ld (hl),l`
    z.store_register_to_hl(at,z.r.l);
    return mk::fall_through(at,static_cast<std::uint16_t>(at+1));
}
template<class Z> Exit word_load_at(Z& z, std::uint16_t at) {            // `ld bc,nn` before a copy
    z.load_bc(at);
    return mk::fall_through(at,static_cast<std::uint16_t>(at+3));
}
template<class Z> Exit video_address_at(Z& z, std::uint16_t at) { return z.final_restart(at,0x08); }   // `rst 08h`
template<class Z> Exit disable_interrupts_at(Z& z, std::uint16_t at) { return z.final_disable_interrupts(at); }

// ---- Title: 00:05D0-0633 ---------------------------------------------------------------------------
// 00:05D0: game mode 2, the title screen; return.
template<class Z> Exit title_setup(Z& z) {
    z.load_hl(0x05D0);                                               // the game mode (C202)
    z.store_to_hl(0x05D3);
    return return_at(z,0x05D5);
}
// 00:05D6: the title screen's cursor: its position (C269) and two choices
// (the count minus one at C26E), then the cursor loop 00:2D51.
template<class Z> Exit title_menu(Z& z) {
    z.load_hl(0x05D6);
    z.store_hl_direct(0x05D9);
    z.load_immediate(0x05DC,z.r.a);
    z.store_a_direct(0x05DE);
    return call_at(z,0x05E1);
}
// 00:05E8: a new game. Work RAM from C300 is cleared (1,024 bytes): the zero
// seed is stored at 05F1 (L of C300h) and the LDIR at 05F2 copies it forward;
// the copy is a separate resumable entry.
template<class Z> Exit new_game_clear(Z& z) {
    z.load_hl(0x05E8);
    z.load_de(0x05EB);
    z.load_bc(0x05EE);
    return seed_store_at(z,0x05F1);
}
// 00:05E4: after the cursor loop, A holds the choice. Continue (nonzero) takes
// the JP NZ to 00:0634, rendered here up to its cartridge-RAM switch;
// a new game falls through to the clear above.
template<class Z> Exit title_choice(Z& z) {
    z.or_a(0x05E4);
    if (z.jump_if(0x05E5,!z.zero())) {
        z.load_immediate(0x0634,z.r.a);                              // 08h: cartridge RAM on
        return cartridge_store_at(z,0x0636);
    }
    return new_game_clear(z);
}
// 00:05F4: after the clear, the first character's record (C400): her weapon
// and armour, then 00:16F1 enables her at level one.
template<class Z> Exit new_game_first_character(Z& z) {
    z.load_index(0x05F4,mk::Index::iy);
    z.store_immediate_indexed(0x05F8,mk::Index::iy);                 // weapon
    z.store_immediate_indexed(0x05FC,mk::Index::iy);                 // armour
    return call_at(z,0x0600);
}
// 00:0603: the world's starting values: two dungeon-object flags (C600, C604)
// set to done, the starting area word (C308), the camera X (C301 and its
// saved copy C313), the camera Y (C305 and its copy C311) and no money; then
// the opening scene 01:42AC.
template<class Z> Exit new_game_world(Z& z) {
    z.load_hl(0x0603);
    z.store_to_hl(0x0606);                                           // C600
    z.load_hl(0x0608);
    z.store_to_hl(0x060B);                                           // C604
    z.load_hl(0x060D);
    z.store_hl_direct(0x0610);                                       // C308
    z.load_hl(0x0613);
    z.store_hl_direct(0x0616);                                       // C301
    z.store_hl_direct(0x0619);                                       // C313
    z.load_hl(0x061C);
    z.store_hl_direct(0x061F);                                       // C305
    z.store_hl_direct(0x0622);                                       // C311
    z.load_hl(0x0625);
    z.store_hl_direct(0x0628);                                       // the money
    return call_at(z,0x062B);
}
// 00:062E: game mode 8, then return.
template<class Z> Exit new_game_mode(Z& z) {
    z.load_hl(0x062E);
    z.store_to_hl(0x0631);
    return return_at(z,0x0633);
}

// ---- Continue: the slot scan and the window graphics, 00:0634-0678 ---------------------------------
// 00:0634 (also 00:0645, 00:064D, 00:069D, 00:06B4, 00:06F5, 00:073A): one
// load of the cartridge control value (08h on, 80h off) and the store that
// ends the segment.
template<class Z> Exit cartridge_switch(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    return cartridge_store_at(z,static_cast<std::uint16_t>(at+2));
}
// 00:0639: look for an occupied slot among the five flags at 8201h-8205h.
// The first nonzero flag goes on to the conversation (cartridge RAM off at
// 064F); five clear flags go to a new game (cartridge RAM off at 0647, then
// the jump to 00:05E8).
template<class Z> Exit find_occupied_slot(Z& z) {
    z.load_hl(0x0639);
    z.load_immediate(0x063C,z.r.b);                                  // five slots
    for (;;) {
        z.load_from_hl(0x063E,z.r.a);
        z.or_a(0x063F);
        if (z.relative_if(0x0640,!z.zero())) {
            z.load_immediate(0x064D,z.r.a);                          // 80h
            return cartridge_store_at(z,0x064F);
        }
        z.increment_hl(0x0642);
        if (!z.decrement_jump(0x0643)) {
            z.load_immediate(0x0645,z.r.a);                          // 80h
            return cartridge_store_at(z,0x0647);
        }
    }
}
// 00:065D: after the EI at 0659 and the game-mode load at 065A (its own
// segment: the one-instruction interrupt delay), game mode 8; bank 16 into slot 2; the
// first block of window graphics to video address 1800h through the loader
// 00:03FA.
template<class Z> Exit continue_window_graphics(Z& z) {
    z.store_to_hl(0x065D);                                           // game mode 8
    z.load_hl(0x065F);
    z.store_to_hl(0x0662);                                           // slot-2 bank ($FFFF)
    z.load_hl(0x0664);
    z.load_de(0x0667);
    return call_at(z,0x066A);
}
// 00:0664, 00:066D, 00:08BA: the source and video address of one graphics
// block, then the loader 00:03FA. 0664 is also an entry of its own.
template<class Z> Exit graphics_block(Z& z, std::uint16_t at) {
    z.load_hl(at);
    z.load_de(static_cast<std::uint16_t>(at+3));
    return call_at(z,static_cast<std::uint16_t>(at+6));
}

// ---- Continue: the conversation, 00:0679-0729 ------------------------------------------------------
// Every text is shown by 00:31CF with its address in HL; the answer of a
// yes/no (00:2D19) is Z for yes.
// 00:0679 and the other text sites: the text's address, then 00:31CF.
template<class Z> Exit show_text(Z& z, std::uint16_t at) {
    z.load_hl(at);
    return call_at(z,static_cast<std::uint16_t>(at+3));
}
// 00:0682, 00:06CE, 00:06ED: after a yes/no. No (NZ) takes the JR NZ to the
// text at `no_text`, yes falls through to the text at `yes_text`; either
// text's address is loaded here and its call ends the segment.
//   0682, the load question: no goes to the erase question (06C5), yes to the slot choice (0684).
//   06CE, the erase question: no goes back to the load question (0679), yes to the slot choice (06D0).
//   06ED, the erase confirmation: no chooses again (06D0), yes erases (06EF).
template<class Z> Exit answer_branch(Z& z, std::uint16_t at, std::uint16_t no_text, std::uint16_t yes_text) {
    const bool no=z.relative_if(at,!z.zero());
    return show_text(z,no ? no_text : yes_text);
}
// 00:068A: choose the slot to load. BC waits on the stack across the slot
// window 00:39B1, which leaves the slot number in the dialogue number C2C5.
template<class Z> Exit load_choose_slot(Z& z) {
    z.push(0x068A,z.r.b,z.r.c);
    return call_at(z,0x068B);
}
// 00:068E: BC back, then the slot-flag check 00:073A.
template<class Z> Exit load_slot_chosen(Z& z) {
    z.pop(0x068E,z.r.b,z.r.c);
    return call_at(z,0x068F);
}
// 00:0692: an empty slot (Z) chooses again; an occupied one shows the text
// before loading.
template<class Z> Exit load_slot_checked(Z& z) {
    if (z.relative_if(0x0692,z.zero())) return load_choose_slot(z);
    return show_text(z,0x0694);
}
// 00:06A2: after cartridge RAM is switched on (069D): the saved game of slot
// n (1-5) is the 1,024 bytes at 8000h + n x 400h. HL = n x 100h x 4 with bit
// 15 set, DE = C300; the count is loaded at 06AF and the LDIR at 06B2 is a
// separate resumable entry.
template<class Z> Exit load_slot_address(Z& z) {
    z.load_a_direct(0x06A2);                                         // the slot number (C2C5)
    z.copy(0x06A5,z.r.h,z.r.a);
    z.load_immediate(0x06A6,z.r.l);
    z.add_to_hl(0x06A8,z.hl());
    z.add_to_hl(0x06A9,z.hl());
    z.set_bit(0x06AA,7,z.r.h);
    z.load_de(0x06AC);
    return word_load_at(z,0x06AF);
}
// 00:06B9: after the copy and cartridge RAM off (06B4): a game saved in a
// dungeon (saved mode C316 = 0Bh) resumes in mode 0Ah, which reloads the floor;
// any other saved mode stays as copied.
template<class Z> Exit load_resume_mode(Z& z) {
    z.load_a_direct(0x06B9);
    z.compare_immediate(0x06BC);                                     // 0Bh
    if (auto exit=z.final_return_if(0x06BE,mk::Condition::nz)) return *exit;
    z.load_hl(0x06BF);
    z.store_to_hl(0x06C2);                                           // game mode 0Ah
    return return_at(z,0x06C4);
}
// 00:06D6: choose the slot to erase, as for loading.
template<class Z> Exit erase_choose_slot(Z& z) {
    z.push(0x06D6,z.r.b,z.r.c);
    return call_at(z,0x06D7);
}
// 00:06DA: button 1 (bit 4 of the pressed buttons in C) cancels back to the
// erase question (06C5); otherwise BC back and the slot-flag check 00:073A.
template<class Z> Exit erase_slot_chosen(Z& z) {
    z.test_bit(0x06DA,4,z.r.c);
    z.pop(0x06DC,z.r.b,z.r.c);
    if (z.relative_if(0x06DD,!z.zero())) return show_text(z,0x06C5);
    return call_at(z,0x06DF);
}
// 00:06E2: an empty slot chooses again; an occupied one asks for confirmation.
template<class Z> Exit erase_slot_checked(Z& z) {
    if (z.relative_if(0x06E2,z.zero())) return erase_choose_slot(z);
    return show_text(z,0x06E4);
}
// 00:06FA: erase slot n (1-5), with cartridge RAM on (06F5). Its flag at
// 8200h + n is cleared. Its name lives in the saved-game window image at 8100h
// (rows of 18 bytes, as the slot window draws them) as two 10-byte rows, the
// first at 8118h + 36 x (n - 1) and the second 18 bytes after it; both are
// overwritten with the blank 10-byte row kept in the ROM at 0730h. Then
// cartridge RAM off (stored at 0727).
template<class Z> Exit erase_slot(Z& z) {
    z.load_a_direct(0x06FA);                                         // the slot number (C2C5)
    z.load_immediate(0x06FD,z.r.h);
    z.copy(0x06FF,z.r.l,z.r.a);
    z.store_to_hl(0x0700);                                           // its flag cleared
    // DE = 8100h + 18h + 36 x (n - 1): A = 2(n - 1), kept in E, times 8 plus E
    // is 18(n - 1), doubled, plus 18h.
    z.decrement(0x0702,z.r.a);
    z.add_register(0x0703,z.r.a);
    z.copy(0x0704,z.r.e,z.r.a);
    z.add_register(0x0705,z.r.a);
    z.add_register(0x0706,z.r.a);
    z.add_register(0x0707,z.r.a);
    z.add_register(0x0708,z.r.e);
    z.add_register(0x0709,z.r.a);
    z.add_immediate(0x070A);
    z.copy(0x070C,z.r.e,z.r.a);
    z.load_immediate(0x070D,z.r.d);
    z.load_hl(0x070F);                                               // the blank row
    z.load_bc(0x0712);                                               // 10 bytes
    z.block_copy(0x0715);                                            // the first name row
    z.exchange_de_hl(0x0717);
    z.load_bc(0x0718);
    z.add_to_hl(0x071B,z.bc());                                      // 8 more: the next window row
    z.exchange_de_hl(0x071C);
    z.load_hl(0x071D);
    z.load_bc(0x0720);
    z.block_copy(0x0723);                                            // the second name row
    z.load_immediate(0x0725,z.r.a);                                  // 80h
    return cartridge_store_at(z,0x0727);
}
// 00:072A: back to the title screen (game mode 2), then return.
template<class Z> Exit erase_done(Z& z) {
    z.load_hl(0x072A);
    z.store_to_hl(0x072D);
    return return_at(z,0x072F);
}

// ---- The slot-flag check: 00:073A-074C -------------------------------------------------------------
// 00:073F: with cartridge RAM on (073A), the flag of the chosen slot at
// 8200h + n is tested with OR A; the load of 80h (cartridge RAM off, stored
// at 0749) does not touch the flags, so the caller still sees Z for an empty
// slot after the return at 074C.
template<class Z> Exit slot_flag_test(Z& z) {
    z.load_a_direct(0x073F);                                         // the slot number (C2C5)
    z.copy(0x0742,z.r.l,z.r.a);
    z.load_immediate(0x0743,z.r.h);
    z.load_from_hl(0x0745,z.r.a);
    z.or_a(0x0746);
    z.load_immediate(0x0747,z.r.a);
    return cartridge_store_at(z,0x0749);
}
// 00:074C: the return itself, an entry after the cartridge store.
template<class Z> Exit slot_flag_return(Z& z) { return return_at(z,0x074C); }

// ---- The opening loader: 00:074D-07B7 --------------------------------------------------------------
// 00:075A: after the DI at 0750 and its three calls (00:003E, the sound
// initialization, the name-table clear 00:035A): the next game mode, the word
// C20E (258h), and
// the opening palette (20h bytes from the ROM at 07BAh) to the target
// palette C240; the count is loaded at 076A and the LDIR at 076D is an entry
// of its own.
template<class Z> Exit opening_palette(Z& z) {
    z.load_hl(0x075A);
    z.increment_at_hl(0x075D);                                       // game mode + 1
    z.load_hl(0x075E);
    z.store_hl_direct(0x0761);
    z.load_hl(0x0764);
    z.load_de(0x0767);
    return word_load_at(z,0x076A);
}
// 00:076F and 00:077C: the pointers of a fill (C260-C2FF, then the eight
// object slots C800-C8FF); the count is loaded at the end of the segment, the
// zero seed (0778, 0785) is a one-instruction segment of its own and the LDIR
// (077A, 0786) is an entry of its own.
template<class Z> Exit opening_fill_pointers(Z& z, std::uint16_t at) {
    z.load_hl(at);
    z.load_de(static_cast<std::uint16_t>(at+3));
    return word_load_at(z,static_cast<std::uint16_t>(at+6));
}
// 00:0788: bank 31 into slot 2, then its graphics to video address 0 through
// the four-plane loader 00:03CD.
template<class Z> Exit opening_graphics(Z& z) {
    z.load_hl(0x0788);
    z.store_to_hl(0x078B);                                           // slot-2 bank ($FFFF)
    z.load_hl(0x078D);
    z.load_de(0x0790);
    return call_at(z,0x0793);
}
// 00:0796: bank 14 into slot 2, then its layout through 01:6B62.
template<class Z> Exit opening_layout(Z& z) {
    z.load_hl(0x0796);
    z.store_to_hl(0x0799);                                           // slot-2 bank ($FFFF)
    z.load_hl(0x079B);
    return call_at(z,0x079E);
}
// 00:07A1: both scroll shadows (C304, C300) to zero, the sound request C004
// (81h), and DE = 8006h for the video register write at 07B0 (RST 08h); the
// EI at 07B1 follows.
template<class Z> Exit opening_scroll_and_sound(Z& z) {
    z.xor_a(0x07A1);
    z.store_a_direct(0x07A2);
    z.store_a_direct(0x07A5);
    z.load_immediate(0x07A8,z.r.a);
    z.store_a_direct(0x07AA);
    z.load_de(0x07AD);
    return video_address_at(z,0x07B0);
}

// ---- The spaceship flight: take-off, 00:086F-08A5 ---------------------------------------------------
// The flight moves the background with a 16-bit speed C2F2 (whole lines in
// the high byte C2F3, 1/256 lines in the low byte), against a position whose
// whole lines are the vertical scroll C304 and whose fraction is C307. Each
// frame of the three loops below waits out a pause (00:00F1), waits for the
// frame (00:0052), and leaves early when button 1 or 2 is pressed.
//
// 00:086F: the fade timer C21B (2009h), then the pause test of the first loop.
template<class Z> Exit takeoff_start(Z& z) {
    z.load_hl(0x086F);
    z.store_hl_direct(0x0872);
    z.load_a_direct(0x0875);                                         // the pause flag (C212)
    z.or_a(0x0878);
    return z.final_call_if(0x0879,mk::Condition::nz);
}
// 00:0875, 00:091A, 00:0944: the top of a loop: CALL NZ to the pause loop.
template<class Z> Exit flight_pause_test(Z& z, std::uint16_t at) {
    z.load_a_direct(at);
    z.or_a(static_cast<std::uint16_t>(at+3));
    return z.final_call_if(static_cast<std::uint16_t>(at+4),mk::Condition::nz);
}
// 00:087C and the other frame waits: the frame handler's number in A, then
// the frame wait 00:0052.
template<class Z> Exit frame_wait(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    return call_at(z,static_cast<std::uint16_t>(at+2));
}
// 00:0881, 00:0926, 00:0950: button 1 or 2 just pressed takes the loop's exit
// (the JR NZ).
template<class Z> Exit flight_skip_test(Z& z, std::uint16_t at) {
    z.load_a_direct(at);                                             // the pressed buttons (C205)
    z.and_immediate(static_cast<std::uint16_t>(at+3));
    return z.final_relative_if(static_cast<std::uint16_t>(at+5),mk::Condition::nz);
}
// 00:088B: after the scroll step, the take-off speeds up by 8/256 of a line
// per frame; the loop goes on (JR C) until the speed reaches eight lines.
template<class Z> Exit takeoff_accelerate(Z& z) {
    z.load_hl_direct(0x088B);
    z.load_de(0x088E);
    z.add_to_hl(0x0891,z.de());
    z.store_hl_direct(0x0892);
    z.copy(0x0895,z.r.a,z.r.h);
    z.compare_immediate(0x0896);
    return z.final_relative_if(0x0898,mk::Condition::c);
}
// 00:089F: wait (frame by frame, from 089A) until the fade timer runs out.
template<class Z> Exit takeoff_fade_wait(Z& z) {
    z.load_a_direct(0x089F);                                         // the fade timer (C21B)
    z.or_a(0x08A2);
    return z.final_relative_if(0x08A3,mk::Condition::nz);
}

// ---- The spaceship flight: space, 00:08A7-0935 -----------------------------------------------------
// 00:08A7: bank 23 into slot 2; the space palette (11h bytes) to the target
// palette C240; then the route's colours (00:0A31).
template<class Z> Exit space_palette(Z& z) {
    z.load_hl(0x08A7);
    z.store_to_hl(0x08AA);                                           // slot-2 bank ($FFFF)
    z.load_hl(0x08AC);
    z.load_de(0x08AF);
    z.load_bc(0x08B2);
    z.block_copy(0x08B5);
    return call_at(z,0x08B7);
}
// 00:08C3: bank 28 into slot 2, then the space layout through 01:6B62.
template<class Z> Exit space_layout(Z& z) {
    z.load_hl(0x08C3);
    z.store_to_hl(0x08C6);                                           // slot-2 bank ($FFFF)
    z.load_hl(0x08C8);
    return call_at(z,0x08CB);
}
// 00:08CE: the screen copy D000 (the decoded layout) is repeated below
// itself: 300h bytes to D300 (LDIR at 08D7), then 100h more to D600 (08D9,
// LDIR at 08DF), the copy continuing where the first one ended. Each count is
// loaded at the end of its segment.
template<class Z> Exit space_screen_first_copy(Z& z) {
    z.load_hl(0x08CE);
    z.load_de(0x08D1);
    return word_load_at(z,0x08D4);
}
template<class Z> Exit space_screen_second_copy(Z& z) {
    z.load_hl(0x08D9);
    return word_load_at(z,0x08DC);
}
// 00:08E1: a second layout over it (01:6B62).
template<class Z> Exit space_overlay(Z& z) {
    z.load_hl(0x08E1);
    return call_at(z,0x08E4);
}
// 00:08E7: both scroll shadows to zero, then the pointers of the 700h-byte
// transfer of the screen copy to the name table (video address 3800h); the
// DI at 08F7 ends the segment before the transfer 00:0346.
template<class Z> Exit space_screen_upload(Z& z) {
    z.xor_a(0x08E7);
    z.store_a_direct(0x08E8);
    z.store_a_direct(0x08EB);
    z.load_hl(0x08EE);
    z.load_de(0x08F1);
    z.load_bc(0x08F4);
    return disable_interrupts_at(z,0x08F7);
}
// 00:08FF: after the EI at 08FB and the load at 08FC (its own segment): the
// 300h bytes of D000 to D300 again (the count is loaded at 0902, the LDIR at
// 0905 is an entry of its own).
template<class Z> Exit space_screen_refresh(Z& z) {
    z.load_de(0x08FF);
    return word_load_at(z,0x0902);
}
// 00:0907: the sound request C004 (8Fh), then the fade-in 01:7B20.
template<class Z> Exit space_sound(Z& z) {
    z.load_immediate(0x0907,z.r.a);
    z.store_a_direct(0x0909);
    return call_at(z,0x090C);
}
// 00:090F: speed zero; C307 now counts the screens left to cross (8); then
// the pause test of the space loop.
template<class Z> Exit space_start(Z& z) {
    z.load_hl(0x090F);
    z.store_hl_direct(0x0912);
    z.load_immediate(0x0915,z.r.a);
    z.store_a_direct(0x0917);
    z.load_a_direct(0x091A);                                         // the pause flag (C212)
    z.or_a(0x091D);
    return z.final_call_if(0x091E,mk::Condition::nz);
}
// 00:0930: after the descent step 00:09E9, the loop goes on while screens are
// left (C307 not zero).
template<class Z> Exit space_loop_test(Z& z) {
    z.load_a_direct(0x0930);
    z.or_a(0x0933);
    return z.final_relative_if(0x0934,mk::Condition::nz);
}

// ---- The spaceship flight: landing, 00:0936-0997 ---------------------------------------------------
// 00:0936: game mode 4, then the destination map is loaded (00:0A74).
template<class Z> Exit landing_mode(Z& z) {
    z.load_hl(0x0936);
    z.store_to_hl(0x0939);
    return call_at(z,0x093B);
}
// 00:093E: the landing starts at eight lines per frame; then the pause test
// of the landing loop.
template<class Z> Exit landing_start(Z& z) {
    z.load_hl(0x093E);
    z.store_hl_direct(0x0941);
    z.load_a_direct(0x0944);                                         // the pause flag (C212)
    z.or_a(0x0947);
    return z.final_call_if(0x0948,mk::Condition::nz);
}
// 00:095A: after the scroll step, the landing slows down by 8/256 of a line
// per frame; the loop goes on (JR NC) until the speed would drop below zero.
template<class Z> Exit landing_decelerate(Z& z) {
    z.load_hl_direct(0x095A);
    z.load_de(0x095D);
    z.or_a(0x0960);
    z.subtract_hl_with_carry(0x0961,z.de());
    z.store_hl_direct(0x0963);
    return z.final_relative_if(0x0966,mk::Condition::nc);
}
// 00:0968: arrived. Speed zero, game mode 8, and the route's arrival record:
// the route C2E9 (low seven bits, from 1) chooses a 9-byte record of the table
// at 0ABFh, whose last three bytes are the arrival (HL = table - 3 + 9 x
// route). The step direction C264, the encounter gate C265, the route, the
// transport C30E and C307 are cleared; then 01:787B applies the arrival.
template<class Z> Exit landing_arrival(Z& z) {
    z.load_hl(0x0968);
    z.store_hl_direct(0x096B);
    z.load_hl(0x096E);
    z.store_to_hl(0x0971);                                           // game mode 8
    z.load_a_direct(0x0973);                                         // the route (C2E9)
    z.and_immediate(0x0976);
    z.copy(0x0978,z.r.l,z.r.a);                                      // A x 9: x 8 plus itself
    z.add_register(0x0979,z.r.a);
    z.add_register(0x097A,z.r.a);
    z.add_register(0x097B,z.r.a);
    z.add_register(0x097C,z.r.l);
    z.copy(0x097D,z.r.l,z.r.a);
    z.load_immediate(0x097E,z.r.h);
    z.load_de(0x0980);
    z.add_to_hl(0x0983,z.de());
    z.xor_a(0x0984);
    z.store_a_direct(0x0985);                                        // C265
    z.store_a_direct(0x0988);                                        // C264
    z.store_a_direct(0x098B);                                        // C2E9
    z.store_a_direct(0x098E);                                        // C30E
    z.store_a_direct(0x0991);                                        // C307
    return call_at(z,0x0994);
}

// ---- The scroll step: 00:0998-09E8 ----------------------------------------------------------------
// 00:0998: one frame of take-off or landing movement.
//  1. The position (C304 lines, C307 fraction) goes back by the speed. The
//     vertical scroll runs over the 224 lines of the name table (0-DFh), so a
//     result of E0h or more has wrapped below zero and loses 20h.
//  2. The whole lines moved are the old line minus the new one; the mask 0Fh
//     drops the 20h of a wrap (the speed stays under 16 lines). None moved:
//     return (09BB).
//  3. The camera Y moves up as many lines: its row C305 runs over the 192
//     lines of a screen (0-BFh), so a result of C0h or more has wrapped, loses
//     40h and moves the screen row C306 (0-7) up by one.
//  4. A new screen row needs the screen window loaded again: CALL NZ
//     01:723D at 09D5. Then the call of the row-strip builder 01:733A at
//     09D8 (a segment of its own) and the next entry is 09DB.
template<class Z> Exit scroll_step(Z& z) {
    z.load_pair_direct(0x0998,z.r.d,z.r.e);                          // DE = the speed (C2F2)
    z.load_a_direct(0x099C);
    z.copy(0x099F,z.r.h,z.r.a);
    z.copy(0x09A0,z.r.b,z.r.a);                                      // B = the old line
    z.load_a_direct(0x09A1);
    z.copy(0x09A4,z.r.l,z.r.a);
    z.or_a(0x09A5);
    z.subtract_hl_with_carry(0x09A6,z.de());
    z.copy(0x09A8,z.r.a,z.r.h);
    z.compare_immediate(0x09A9);                                     // E0h
    if (!z.relative_if(0x09AB,z.carry())) z.subtract_immediate(0x09AD);   // wrapped: - 20h
    z.copy(0x09AF,z.r.h,z.r.a);
    z.store_a_direct(0x09B0);                                        // C304
    z.copy(0x09B3,z.r.a,z.r.l);
    z.store_a_direct(0x09B4);                                        // C307
    z.copy(0x09B7,z.r.a,z.r.b);
    z.subtract_register(0x09B8,z.r.h);
    z.and_immediate(0x09B9);                                         // the lines moved
    if (auto exit=z.final_return_if(0x09BB,mk::Condition::z)) return *exit;
    z.copy(0x09BC,z.r.e,z.r.a);
    z.load_immediate(0x09BD,z.r.d);
    z.load_hl_direct(0x09BF);                                        // the camera Y (C305-C306)
    z.copy(0x09C2,z.r.b,z.r.h);                                      // B = the old screen row
    z.copy(0x09C3,z.r.a,z.r.l);
    z.subtract_register(0x09C4,z.r.e);
    z.compare_immediate(0x09C5);                                     // C0h
    if (!z.relative_if(0x09C7,z.carry())) {                          // wrapped: - 40h, a row up
        z.subtract_immediate(0x09C9);
        z.decrement(0x09CB,z.r.h);
    }
    z.copy(0x09CC,z.r.l,z.r.a);
    z.copy(0x09CD,z.r.a,z.r.h);
    z.and_immediate(0x09CE);                                         // rows 0-7
    z.copy(0x09D0,z.r.h,z.r.a);
    z.store_hl_direct(0x09D1);
    z.compare_register(0x09D4,z.r.b);
    return z.final_call_if(0x09D5,mk::Condition::nz);
}
// 00:09DB: while the speed's whole lines are 7 and the fade timer runs, one
// fade step (CALL NZ 01:7B40 at 09E5); at any other speed, return.
template<class Z> Exit scroll_fade(Z& z) {
    z.load_a_direct(0x09DB);                                         // C2F3
    z.compare_immediate(0x09DE);
    if (auto exit=z.final_return_if(0x09E0,mk::Condition::nz)) return *exit;
    z.load_a_direct(0x09E1);                                         // the fade timer (C21B)
    z.or_a(0x09E4);
    return z.final_call_if(0x09E5,mk::Condition::nz);
}
// 00:09E8: the return after the fade step.
template<class Z> Exit scroll_return(Z& z) { return return_at(z,0x09E8); }

// ---- The descent step: 00:09E9-0A49 ---------------------------------------------------------------
// 00:09E9: one frame of the space flight. The picture moves two lines (D
// becomes 1 when the scroll wraps below zero, losing 20h as in the scroll
// step), and each wrap counts C307 down by one. The step returns unless the
// count has just reached 1 on a wrap (0A04, 0A07); then the destination's
// area byte (the fourth byte of the route record, HL = 0ABFh - 6 + 9 x
// route) goes to C308 and the route colours are chosen for it (00:0A31).
template<class Z> Exit descent_step(Z& z) {
    z.load_de(0x09E9);                                               // E = 2 lines, D = 0
    z.load_a_direct(0x09EC);                                         // C304
    z.subtract_register(0x09EF,z.r.e);
    z.compare_immediate(0x09F0);                                     // E0h
    if (!z.relative_if(0x09F2,z.carry())) {                          // wrapped
        z.load_immediate(0x09F4,z.r.d);                              // one screen crossed
        z.subtract_immediate(0x09F6);
    }
    z.store_a_direct(0x09F8);
    z.load_a_direct(0x09FB);                                         // the screens left (C307)
    z.subtract_register(0x09FE,z.r.d);
    z.store_a_direct(0x09FF);
    z.compare_immediate(0x0A02);                                     // 1
    if (auto exit=z.final_return_if(0x0A04,mk::Condition::nz)) return *exit;
    z.copy(0x0A05,z.r.a,z.r.d);
    z.or_a(0x0A06);                                                  // not on a wrap: already done
    if (auto exit=z.final_return_if(0x0A07,mk::Condition::z)) return *exit;
    z.load_a_direct(0x0A08);                                         // the route (C2E9)
    z.and_immediate(0x0A0B);
    z.copy(0x0A0D,z.r.l,z.r.a);                                      // A x 9
    z.add_register(0x0A0E,z.r.a);
    z.add_register(0x0A0F,z.r.a);
    z.add_register(0x0A10,z.r.a);
    z.add_register(0x0A11,z.r.l);
    z.copy(0x0A12,z.r.l,z.r.a);
    z.load_immediate(0x0A13,z.r.h);
    z.load_de(0x0A15);
    z.add_to_hl(0x0A18,z.de());
    z.load_from_hl(0x0A19,z.r.a);
    z.store_a_direct(0x0A1A);                                        // C308
    return call_at(z,0x0A1D);
}
// 00:0A20: the new colours are shown at once (the target palette C240 copied
// over the working palette C220, 20h bytes), then the destination's layout:
// a tail jump to 01:6B62 at 0A2E, whose return ends the descent step.
template<class Z> Exit descent_destination(Z& z) {
    z.load_hl(0x0A20);
    z.load_de(0x0A23);
    z.load_bc(0x0A26);
    z.block_copy(0x0A29);
    z.load_hl(0x0A2B);
    return jump_at(z,0x0A2E);
}
// 00:0A31: the route colours: C308's low two bits choose one of four 6-byte
// entries of the table at 0A4Ah, copied to the target palette from C242.
template<class Z> Exit route_colours(Z& z) {
    z.load_a_direct(0x0A31);
    z.and_immediate(0x0A34);
    z.add_register(0x0A36,z.r.a);                                    // A x 6: x 2, kept in L,
    z.copy(0x0A37,z.r.l,z.r.a);                                      // doubled, plus L
    z.add_register(0x0A38,z.r.a);
    z.add_register(0x0A39,z.r.l);
    z.load_immediate(0x0A3A,z.r.d);
    z.copy(0x0A3C,z.r.e,z.r.a);
    z.load_hl(0x0A3D);
    z.add_to_hl(0x0A40,z.de());
    z.load_de(0x0A41);
    z.load_bc(0x0A44);
    z.block_copy(0x0A47);
    return return_at(z,0x0A49);
}

// ---- The flight's map loader: 00:0A5C-0ABE --------------------------------------------------------
// 00:0A5C (the departure map, before the take-off) and 00:0A74 (the
// destination map, called at 093B): the route's record, the first or the second three
// bytes (HL = 0ABFh - 9 or - 6 + 9 x route), is applied by 01:787B. 00:0A8C
// is pushed first: 01:787B drops its own return address and returns to the
// address below it, so both go on at 0A8C.
template<class Z> Exit flight_map_record(Z& z, std::uint16_t at) {
    z.load_a_direct(at);                                             // the route (C2E9)
    z.and_immediate(static_cast<std::uint16_t>(at+3));
    z.copy(static_cast<std::uint16_t>(at+5),z.r.l,z.r.a);            // A x 9
    z.add_register(static_cast<std::uint16_t>(at+6),z.r.a);
    z.add_register(static_cast<std::uint16_t>(at+7),z.r.a);
    z.add_register(static_cast<std::uint16_t>(at+8),z.r.a);
    z.add_register(static_cast<std::uint16_t>(at+9),z.r.l);
    z.copy(static_cast<std::uint16_t>(at+10),z.r.l,z.r.a);
    z.load_immediate(static_cast<std::uint16_t>(at+11),z.r.h);
    z.load_de(static_cast<std::uint16_t>(at+13));
    z.add_to_hl(static_cast<std::uint16_t>(at+16),z.de());
    z.load_de(static_cast<std::uint16_t>(at+17));
    z.push(static_cast<std::uint16_t>(at+20),z.r.d,z.r.e);           // 00:0A8C
    return call_at(z,static_cast<std::uint16_t>(at+21));
}
// 00:0A8C: the held buttons C204 and the step direction C264 cleared; the
// transport C30E is the ship (10h), or 11h for a route of 83h or above; speed
// zero; then the map is loaded (00:0B6A).
template<class Z> Exit flight_map_transport(Z& z) {
    z.xor_a(0x0A8C);
    z.store_a_direct(0x0A8D);
    z.store_a_direct(0x0A90);
    z.load_a_direct(0x0A93);                                         // the route (C2E9)
    z.compare_immediate(0x0A96);                                     // 83h
    z.load_immediate(0x0A98,z.r.a);
    if (!z.relative_if(0x0A9A,z.carry())) z.increment(0x0A9C,z.r.a);
    z.store_a_direct(0x0A9D);
    z.load_hl(0x0AA0);
    z.store_hl_direct(0x0AA3);
    return call_at(z,0x0AA6);
}
// 00:0AA9: the six tile animators C26F-C286 cleared (a zero byte copied
// forward), then the object engine 01:576A.
template<class Z> Exit flight_map_animators(Z& z) {
    z.load_hl(0x0AA9);
    z.load_de(0x0AAC);
    z.load_bc(0x0AAF);
    z.store_to_hl(0x0AB2);
    z.block_copy(0x0AB4);
    return call_at(z,0x0AB6);
}
// 00:0AB9: the step direction C264 = 1 (up), then return.
template<class Z> Exit flight_map_direction(Z& z) {
    z.load_immediate(0x0AB9,z.r.a);
    z.store_a_direct(0x0ABB);
    return return_at(z,0x0ABE);
}
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address, with the unit it belongs to, its exits and its
// permissions; each is checked against the original on every call. A note
// names the call or jump the entry follows.
void register_scenes(RoutineRegistry& registry) {
    register_scene_names(registry);
    register_scene_scripts(registry);
    register_scene_animation(registry);
    register_scene_persistence(registry);
    register_scene_interactions(registry);
    register_scene_visuals(registry);
    register_scene_quests(registry);

    auto F=[](std::uint16_t at, std::int8_t stack=0) { return mk::core_final_at(at,stack); };
    auto R=[](std::uint16_t at) { return mk::returned_at(at); };
    auto C=[](std::uint16_t at) { return mk::conditional_at(at); };
    // A final `ld ($FFFC),a`: cartridge RAM is remapped at that moment.
    auto cartridge=[](std::uint16_t at) { auto exit=mk::core_final_at(at); exit.cartridge_control=true; return exit; };
    // A final `ld (hl),l` that seeds the 1,024-byte clear of C300.
    auto seed=[](std::uint16_t at) { auto exit=mk::core_final_at(at); exit.wram_seed=0xC300; return exit; };
    auto masked=[](std::uint16_t at, InterruptMask mask) { auto exit=mk::core_final_at(at); exit.interrupt_mask=mask; return exit; };
    // `observed`: the host watches this entry (game/common.cpp hooks); see
    // the two uses below.
    auto add=[&](std::uint16_t at, std::uint16_t unit, std::vector<SegmentExit> exits, mk::Permissions permissions,
                 auto logic, bool observed=false) {
        auto spec=mk::segment(RoutineEntry{0,unit},std::move(exits),permissions);
        spec.observe_entry=observed;
        mk::add_segment(registry,RoutineEntry{0,at},std::move(spec),logic);
    };
    // A lone DI or EI (four cycles).
    auto interrupt_switch=[&](std::uint16_t at, InterruptMask mask) {
        const RoutineEntry entry{0,at};
        auto spec=mk::segment(entry,{masked(at,mask)});
        spec.single_instruction=true;
        mk::add_segment(registry,entry,std::move(spec),[at,mask](auto& z) -> Exit {
            return mask==InterruptMask::enable ? z.final_enable_interrupts(at) : z.final_disable_interrupts(at);
        });
    };
    auto copy=[&](std::uint16_t at) { register_block_transfer(registry,{0,at}); };
    constexpr mk::Permissions none{}, calls{.scratch_stack=2};
    constexpr mk::Permissions mapper_calls{.slot2_mapper=true,.scratch_stack=2};
    // The joining stretches of milestone 14: the instructions between
    // two segments, each a one-instruction segment of its own.
    auto single=[&](std::uint16_t at, SegmentExit exit, mk::Permissions permissions, auto logic) {
        const RoutineEntry entry{0,at};
        auto spec=mk::segment(entry,{exit},permissions);
        spec.single_instruction=true;
        mk::add_segment(registry,entry,std::move(spec),logic);
    };
    auto call_single=[&](std::uint16_t at) {
        single(at,F(at),calls,[at](auto& z) -> Exit { return z.final_call(at); });
    };

    // ---- title and new game ----
    add(0x05D0,0x05D0,{R(0x05D5)},none,[](auto& z) -> Exit { return title_setup(z); });
    add(0x05D6,0x05D6,{F(0x05E1)},calls,[](auto& z) -> Exit { return title_menu(z); });
    add(0x05E4,0x05E4,{cartridge(0x0636),seed(0x05F1)},none,[](auto& z) -> Exit { return title_choice(z); });  // after the cursor loop (00:2D51)
    add(0x05E8,0x05E8,{seed(0x05F1)},none,[](auto& z) -> Exit { return new_game_clear(z); });                  // the jump at 064A
    copy(0x05F2);
    // The host observes 05F4 to see the 1,024-byte clear complete at its
    // original boundary.
    add(0x05F4,0x05F4,{F(0x0600)},calls,[](auto& z) -> Exit { return new_game_first_character(z); },true);
    add(0x0603,0x0603,{F(0x062B)},calls,[](auto& z) -> Exit { return new_game_world(z); });                    // after 00:16F1
    add(0x062E,0x062E,{R(0x0633)},none,[](auto& z) -> Exit { return new_game_mode(z); });                      // after 01:42AC

    // ---- Continue: slot scan, window graphics ----
    add(0x0634,0x0634,{cartridge(0x0636)},none,[](auto& z) -> Exit { return cartridge_switch(z,0x0634); });
    add(0x0639,0x0639,{cartridge(0x0647),cartridge(0x064F)},none,[](auto& z) -> Exit { return find_occupied_slot(z); });
    add(0x0645,0x0645,{cartridge(0x0647)},none,[](auto& z) -> Exit { return cartridge_switch(z,0x0645); });
    add(0x064D,0x064D,{cartridge(0x064F)},none,[](auto& z) -> Exit { return cartridge_switch(z,0x064D); });
    interrupt_switch(0x0655,InterruptMask::disable);
    interrupt_switch(0x0659,InterruptMask::enable);
    add(0x065D,0x065D,{F(0x066A)},mapper_calls,[](auto& z) -> Exit { return continue_window_graphics(z); });  // after the EI-delayed load at 065A
    add(0x0664,0x0664,{F(0x066A)},calls,[](auto& z) -> Exit { return graphics_block(z,0x0664); });
    add(0x066D,0x066D,{F(0x0673)},calls,[](auto& z) -> Exit { return graphics_block(z,0x066D); });            // after 00:03FA

    // ---- Continue: the conversation ----
    add(0x0679,0x0679,{F(0x067C)},calls,[](auto& z) -> Exit { return show_text(z,0x0679); });
    add(0x0682,0x0682,{F(0x06C8),F(0x0687)},calls,[](auto& z) -> Exit { return answer_branch(z,0x0682,0x06C5,0x0684); });  // after the yes/no
    add(0x0684,0x0684,{F(0x0687)},calls,[](auto& z) -> Exit { return show_text(z,0x0684); });
    add(0x068A,0x068A,{F(0x068B,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return load_choose_slot(z); });
    add(0x068E,0x068A,{F(0x068F,2)},{.inherited_stack=2},[](auto& z) -> Exit { return load_slot_chosen(z); });  // after the slot window (00:39B1)
    add(0x0692,0x0692,{F(0x068B,-2),F(0x0697)},{.scratch_stack=4},[](auto& z) -> Exit { return load_slot_checked(z); });  // after the flag check (00:073A)
    add(0x0694,0x0694,{F(0x0697)},calls,[](auto& z) -> Exit { return show_text(z,0x0694); });
    add(0x069D,0x069D,{cartridge(0x069F)},none,[](auto& z) -> Exit { return cartridge_switch(z,0x069D); });   // after the window is closed (00:3464)
    add(0x06A2,0x06A2,{F(0x06AF)},none,[](auto& z) -> Exit { return load_slot_address(z); });
    copy(0x06B2);
    // The host observes 06B4 to see the saved-game copy complete at its
    // original boundary.
    add(0x06B4,0x06B4,{cartridge(0x06B6)},none,[](auto& z) -> Exit { return cartridge_switch(z,0x06B4); },true);
    add(0x06B9,0x06B9,{R(0x06BE),R(0x06C4)},none,[](auto& z) -> Exit { return load_resume_mode(z); });
    add(0x06C5,0x06C5,{F(0x06C8)},calls,[](auto& z) -> Exit { return show_text(z,0x06C5); });
    add(0x06CE,0x06CE,{F(0x067C),F(0x06D3)},calls,[](auto& z) -> Exit { return answer_branch(z,0x06CE,0x0679,0x06D0); });  // after the yes/no
    add(0x06D0,0x06D0,{F(0x06D3)},calls,[](auto& z) -> Exit { return show_text(z,0x06D0); });
    add(0x06D6,0x06D6,{F(0x06D7,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return erase_choose_slot(z); });
    add(0x06DA,0x06D6,{F(0x06C8,2),F(0x06DF,2)},{.inherited_stack=2},[](auto& z) -> Exit { return erase_slot_chosen(z); });  // after the slot window (00:39B1)
    add(0x06E2,0x06E2,{F(0x06D7,-2),F(0x06E7)},{.scratch_stack=4},[](auto& z) -> Exit { return erase_slot_checked(z); });  // after the flag check (00:073A)
    add(0x06E4,0x06E4,{F(0x06E7)},calls,[](auto& z) -> Exit { return show_text(z,0x06E4); });
    add(0x06ED,0x06ED,{F(0x06D3),F(0x06F2)},calls,[](auto& z) -> Exit { return answer_branch(z,0x06ED,0x06D0,0x06EF); });  // after the yes/no
    add(0x06EF,0x06EF,{F(0x06F2)},calls,[](auto& z) -> Exit { return show_text(z,0x06EF); });
    add(0x06F5,0x06F5,{cartridge(0x06F7)},none,[](auto& z) -> Exit { return cartridge_switch(z,0x06F5); });   // after the text
    add(0x06FA,0x06FA,{cartridge(0x0727)},none,[](auto& z) -> Exit { return erase_slot(z); });
    add(0x072A,0x072A,{R(0x072F)},none,[](auto& z) -> Exit { return erase_done(z); });

    // ---- the slot-flag check ----
    add(0x073A,0x073A,{cartridge(0x073C)},none,[](auto& z) -> Exit { return cartridge_switch(z,0x073A); });
    add(0x073F,0x073F,{cartridge(0x0749)},none,[](auto& z) -> Exit { return slot_flag_test(z); });
    single(0x074C,R(0x074C),none,[](auto& z) -> Exit { return slot_flag_return(z); });

    // ---- the opening loader ----
    interrupt_switch(0x0750,InterruptMask::disable);
    add(0x075A,0x075A,{F(0x076A)},none,[](auto& z) -> Exit { return opening_palette(z); });                    // after 00:035A
    copy(0x076D);
    add(0x076F,0x075A,{F(0x0775)},none,[](auto& z) -> Exit { return opening_fill_pointers(z,0x076F); });
    copy(0x077A);
    add(0x077C,0x075A,{F(0x0782)},none,[](auto& z) -> Exit { return opening_fill_pointers(z,0x077C); });
    copy(0x0786);
    add(0x0788,0x075A,{F(0x0793)},mapper_calls,[](auto& z) -> Exit { return opening_graphics(z); });
    add(0x0796,0x0796,{F(0x079E)},mapper_calls,[](auto& z) -> Exit { return opening_layout(z); });             // after 00:03CD
    add(0x07A1,0x07A1,{F(0x07B0)},calls,[](auto& z) -> Exit { return opening_scroll_and_sound(z); });         // after 01:6B62
    interrupt_switch(0x07B1,InterruptMask::enable);

    // ---- the spaceship flight: take-off ----
    add(0x086F,0x086F,{F(0x0879)},calls,[](auto& z) -> Exit { return takeoff_start(z); });
    add(0x0875,0x0875,{F(0x0879)},calls,[](auto& z) -> Exit { return flight_pause_test(z,0x0875); });         // the loop's JR C at 0898
    add(0x087C,0x087C,{F(0x087E)},calls,[](auto& z) -> Exit { return frame_wait(z,0x087C); });                // after the pause test
    add(0x0881,0x0881,{C(0x0886)},none,[](auto& z) -> Exit { return flight_skip_test(z,0x0881); });           // after the frame wait
    add(0x088B,0x088B,{C(0x0898)},none,[](auto& z) -> Exit { return takeoff_accelerate(z); });                // after the scroll step
    add(0x089A,0x089A,{F(0x089C)},calls,[](auto& z) -> Exit { return frame_wait(z,0x089A); });
    add(0x089F,0x089F,{C(0x08A3)},none,[](auto& z) -> Exit { return takeoff_fade_wait(z); });                 // after the frame wait

    // ---- the spaceship flight: space ----
    add(0x08A7,0x08A7,{F(0x08B7)},mapper_calls,[](auto& z) -> Exit { return space_palette(z); });
    add(0x08BA,0x08BA,{F(0x08C0)},calls,[](auto& z) -> Exit { return graphics_block(z,0x08BA); });            // after the route colours
    add(0x08C3,0x08C3,{F(0x08CB)},mapper_calls,[](auto& z) -> Exit { return space_layout(z); });              // after 00:03FA
    add(0x08CE,0x08CE,{F(0x08D4)},none,[](auto& z) -> Exit { return space_screen_first_copy(z); });           // after 01:6B62
    copy(0x08D7);
    add(0x08D9,0x08CE,{F(0x08DC)},none,[](auto& z) -> Exit { return space_screen_second_copy(z); });
    copy(0x08DF);
    add(0x08E1,0x08CE,{F(0x08E4)},calls,[](auto& z) -> Exit { return space_overlay(z); });
    add(0x08E7,0x08E7,{masked(0x08F7,InterruptMask::disable)},none,[](auto& z) -> Exit { return space_screen_upload(z); });  // after 01:6B62
    interrupt_switch(0x08FB,InterruptMask::enable);
    add(0x08FF,0x08FF,{F(0x0902)},none,[](auto& z) -> Exit { return space_screen_refresh(z); });             // after the EI-delayed load at 08FC
    copy(0x0905);
    add(0x0907,0x08FF,{F(0x090C)},calls,[](auto& z) -> Exit { return space_sound(z); });
    add(0x090F,0x090F,{F(0x091E)},calls,[](auto& z) -> Exit { return space_start(z); });                      // after the fade-in
    add(0x091A,0x091A,{F(0x091E)},calls,[](auto& z) -> Exit { return flight_pause_test(z,0x091A); });         // the loop's JR NZ at 0934
    add(0x0921,0x0921,{F(0x0923)},calls,[](auto& z) -> Exit { return frame_wait(z,0x0921); });
    add(0x0926,0x0926,{C(0x092B)},none,[](auto& z) -> Exit { return flight_skip_test(z,0x0926); });
    add(0x0930,0x0930,{C(0x0934)},none,[](auto& z) -> Exit { return space_loop_test(z); });                   // after the descent step

    // ---- the spaceship flight: landing ----
    add(0x0936,0x0936,{F(0x093B)},calls,[](auto& z) -> Exit { return landing_mode(z); });
    add(0x093E,0x093E,{F(0x0948)},calls,[](auto& z) -> Exit { return landing_start(z); });                    // after the destination map
    add(0x0944,0x0944,{F(0x0948)},calls,[](auto& z) -> Exit { return flight_pause_test(z,0x0944); });         // the loop's JR NC at 0966
    add(0x094B,0x094B,{F(0x094D)},calls,[](auto& z) -> Exit { return frame_wait(z,0x094B); });
    add(0x0950,0x0950,{C(0x0955)},none,[](auto& z) -> Exit { return flight_skip_test(z,0x0950); });
    add(0x095A,0x095A,{C(0x0966)},none,[](auto& z) -> Exit { return landing_decelerate(z); });                // after the scroll step
    add(0x0968,0x0968,{F(0x0994)},calls,[](auto& z) -> Exit { return landing_arrival(z); });

    // ---- the scroll and descent steps ----
    add(0x0998,0x0998,{R(0x09BB),F(0x09D5)},calls,[](auto& z) -> Exit { return scroll_step(z); });
    add(0x09DB,0x09DB,{R(0x09E0),F(0x09E5)},calls,[](auto& z) -> Exit { return scroll_fade(z); });            // after 01:733A
    single(0x09E8,R(0x09E8),none,[](auto& z) -> Exit { return scroll_return(z); });                     // after the fade step
    add(0x09E9,0x09E9,{R(0x0A04),R(0x0A07),F(0x0A1D)},calls,[](auto& z) -> Exit { return descent_step(z); });
    add(0x0A20,0x0A20,{F(0x0A2E)},calls,[](auto& z) -> Exit { return descent_destination(z); });              // after the route colours
    add(0x0A31,0x0A31,{R(0x0A49)},none,[](auto& z) -> Exit { return route_colours(z); });

    // ---- the flight's map loader ----
    add(0x0A5C,0x0A5C,{F(0x0A71,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return flight_map_record(z,0x0A5C); });
    add(0x0A74,0x0A74,{F(0x0A89,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return flight_map_record(z,0x0A74); });
    add(0x0A8C,0x0A8C,{F(0x0AA6)},calls,[](auto& z) -> Exit { return flight_map_transport(z); });             // the return of 01:787B
    add(0x0AA9,0x0AA9,{F(0x0AB6)},calls,[](auto& z) -> Exit { return flight_map_animators(z); });             // after 00:0B6A
    add(0x0AB9,0x0AB9,{R(0x0ABE)},none,[](auto& z) -> Exit { return flight_map_direction(z); });              // after 01:576A

    // ---- joining stretches (milestone 14) ----
    // Calls that follow a cartridge-RAM switch, an interrupt switch or another
    // call, each a segment of its own: the pause loop (00:00F1), the fade
    // helper (01:7B05), the sound and screen set-up of the opening, the yes/no
    // window, the window close, the scroll step and the frame waits.
    for (std::uint16_t at:{0x00F1,0x0652,0x0656,0x0676,0x067F,0x069A,0x0751,0x0754,0x0757,0x07B4,0x0888,0x08F8,0x092D,0x0957,0x09D8})
        call_single(at);
    // The instruction after an EI, which the interrupt delay keeps in front
    // of any interrupt: the game-mode pointer of the Continue graphics, the
    // frame-wait count of the opening, the ship's VDP pointer.
    single(0x065A,F(0x065A),none,[](auto& z) -> Exit { z.load_hl(0x065A); return mk::fall_through(0x065A,0x065D); });
    single(0x07B2,F(0x07B2),none,[](auto& z) -> Exit { z.load_immediate(0x07B2,z.r.a); return mk::fall_through(0x07B2,0x07B4); });
    single(0x08FC,F(0x08FC),none,[](auto& z) -> Exit { z.load_hl(0x08FC); return mk::fall_through(0x08FC,0x08FF); });
    // The two seed bytes of the opening loader's fills (a zero and L), which
    // the fill's copy then spreads.
    single(0x0778,F(0x0778),none,[](auto& z) -> Exit { z.store_to_hl(0x0778); return mk::fall_through(0x0778,0x077A); });
    single(0x0785,F(0x0785),none,[](auto& z) -> Exit { z.store_register_to_hl(0x0785,z.r.l); return mk::fall_through(0x0785,0x0786); });
    // The jump that ends the opening loader after its frame wait, and the
    // short jump inside the take-off's fade wait.
    single(0x07B7,F(0x07B7),none,[](auto& z) -> Exit { return z.final_jump(0x07B7); });
    single(0x08A5,F(0x08A5),none,[](auto& z) -> Exit { return z.final_relative(0x08A5); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_scenes_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{0,0x064A},{0,0x0639},{kit::core_final_at(0x064A)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x064A); });
    kit::add_joining(registry,{0,0x06CB},{0,0x0682},{kit::core_final_at(0x06CB)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x06CB); });
    kit::add_joining(registry,{0,0x06EA},{0,0x06E2},{kit::core_final_at(0x06EA)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x06EA); });
    kit::add_joining(registry,{0,0x0997},{0,0x0968},{kit::returned_at(0x0997)},kit::return_cycles,0,[](auto& z) { return z.final_return(0x0997); });
    kit::add_joining(registry,{0,0x0B07},{0,0x0B07},{kit::returned_at(0x0B07)},kit::return_cycles,0,[](auto& z) { return z.final_return(0x0B07); });
}
}
