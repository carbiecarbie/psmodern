// SPDX-License-Identifier: GPL-3.0-or-later
// Scenes (milestone 13), part script: the scripted scenes of bank 1. The
// opening and its first conversations 01:42AC (with the scroll step 01:43AA
// and the conversations at 01:43CF, 01:4414 and 01:4461), the boss scene
// 01:4497 with its transition 01:4517, the finale and credits route 01:454E,
// the portrait-conversation setup (01:467F), the portrait transition 01:46D1,
// and the Darkfalz scene with the final answer 01:5619. See
// docs/how-it-works/scenes.md ("The scripted finale", "Darkfalz and the final
// answer").
//
// How the code is cut. A scene is a long straight line of calls into shared
// routines: the fades (01:7B05, 01:7B20), the frame wait, the picture loader
// (00:3D47), the graphics and layout decoders, the text renderers (00:337D for
// the portrait conversations) and the portrait transition. Every call into
// them is where a segment ends: the segment renders the loads and stores that
// set up the call and the call is the segment's last instruction. The
// instruction after a call is the next entry (a one-instruction segment when
// the original goes on with another call or a jump). A few segments instead end
// on a load (the count of a block copy, so the shared block transfer starts at
// the copy), on the `di` or `ei` around a video transfer, or on a jump.
//
// Where a setup selects a ROM bank (a store to FFFF), the segment is declared
// with the slot-2 mapper permission. No picture, text, palette or script byte
// is written here: the kit reads every operand and cycle cost from the
// player's ROM at the original address passed to it.
//
// Bytes named below: C004 the sound request, C264 and C300-C307 the scroll
// state, C250 the palette, C29E the interaction picture, C2E6 the enemy group,
// C2F5 the credits latch, C309 the location, C800 and C900 the first object
// slot and its sprite.
#include "game/joins.h"
#include "game/scenes.h"
#include "game/menu_kit.h"
#include "game/routines.h"
#include <vector>

namespace psmodern::game {
namespace {
namespace mk=menu_kit;
using mk::Exit;

// The ways a segment ends: the final instruction, rendered like the others.
template<class Z> Exit call_at(Z& z, std::uint16_t at) { return z.final_call(at); }
template<class Z> Exit jump_at(Z& z, std::uint16_t at) { return z.final_jump(at); }
template<class Z> Exit relative_at(Z& z, std::uint16_t at) { return z.final_relative(at); }
template<class Z> Exit load_pair_at(Z& z, std::uint16_t at) {             // loads the count of the copy that follows
    z.load_bc(at);
    return mk::fall_through(at,static_cast<std::uint16_t>(at+3));
}
template<class Z> Exit return_at(Z& z, std::uint16_t at) { return z.final_return(at); }
std::uint16_t after(std::uint16_t at, unsigned length) { return static_cast<std::uint16_t>(at+length); }

// ---- Shared shapes ----------------------------------------------------------------------------
// A load of the accumulator at `at`, then a call (a frame count, a picture or portrait number).
template<class Z> Exit number_then_call(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    return call_at(z,after(at,2));
}
// A load and a store of the accumulator at `at`, then a call: a sound request (C004), an
// interaction picture (C29E) or an enemy group (C2E6).
template<class Z> Exit store_then_call(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    z.store_a_direct(after(at,2));
    return call_at(z,after(at,5));
}
// A load of the HL pair at `at`, then a call: a dialogue or a text of a conversation.
template<class Z> Exit text_then_call(Z& z, std::uint16_t at) {
    z.load_hl(at);
    return call_at(z,after(at,3));
}
// A store to the bank register (FFFF) at `at`: the bank of the data that follows is
// mapped into slot 2.
template<class Z> void select_bank(Z& z, std::uint16_t at) {
    z.load_hl(at);
    z.store_to_hl(after(at,3));
}
// A conversation starts: its sound request, then the first portrait number
// for the portrait transition (01:46D1).
template<class Z> Exit conversation_start(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    z.store_a_direct(after(at,2));                                   // C004
    z.load_immediate(after(at,5),z.r.a);
    return call_at(z,after(at,7));
}
// A conversation ends with its sound request and returns.
template<class Z> Exit conversation_end(Z& z, std::uint16_t at) {
    z.load_immediate(at,z.r.a);
    z.store_a_direct(after(at,2));                                   // C004
    return return_at(z,after(at,5));
}
// Bank, palette copy and graphics: the bank is selected, a palette is copied
// to its buffer by a block copy (`ldir`), and the graphics source and video
// destination are loaded for the decoder call that follows.
template<class Z> void palette_and_graphics(Z& z, std::uint16_t at) {
    select_bank(z,at);
    z.load_hl(after(at,5));                                          // the palette
    z.load_de(after(at,8));                                          // its buffer
    z.load_bc(after(at,11));                                         // its length
    z.block_copy(after(at,14));
    z.load_hl(after(at,16));                                         // the graphics
    z.load_de(after(at,19));                                         // the video address
}
// Bank and graphics only: the source and destination of the decoder call.
template<class Z> Exit bank_and_graphics(Z& z, std::uint16_t at) {
    select_bank(z,at);
    z.load_hl(after(at,5));
    z.load_de(after(at,8));
    return call_at(z,after(at,11));
}
// The source and destination of the decoder call, the bank already selected.
template<class Z> Exit graphics(Z& z, std::uint16_t at) {
    z.load_hl(at);
    z.load_de(after(at,3));
    return call_at(z,after(at,6));
}

// ---- Entry facades and interrupt switches -----------------------------------------------------
// The call at 00:074D (title initialization) and at 01:454E (the finale's
// first fade) is a one-instruction segment; the following segments own the
// bodies.
template<class Z> Exit facade(Z& z, std::uint16_t at) { return call_at(z,at); }

// ---- The opening: 01:42AC-43A9 ----------------------------------------------------------------
// 01:42AC: the opening's sound request, then the fade out.
template<class Z> Exit opening_sound(Z& z) { return store_then_call(z,0x42AC); }
// 01:42B4: the opening palette and graphics.
template<class Z> Exit opening_graphics(Z& z) {
    palette_and_graphics(z,0x42B4);
    return call_at(z,0x42CA);
}
// 01:42CD: the opening layout for the layout decoder (01:6B62).
template<class Z> Exit opening_layout(Z& z) {
    select_bank(z,0x42CD);
    z.load_hl(0x42D2);
    return call_at(z,0x42D5);
}
// 01:42D8: the screen copy at D000 is duplicated further down by two block
// copies (01:42E1, 01:42E9). The count loads end these segments, so
// each copy starts at its own entry.
template<class Z> Exit opening_screen_copy(Z& z) {
    z.load_hl(0x42D8);                                               // the source
    z.load_de(0x42DB);                                               // the destination
    return load_pair_at(z,0x42DE);                                     // the count
}
template<class Z> Exit opening_screen_second_copy(Z& z) {
    z.load_hl(0x42E3);
    return load_pair_at(z,0x42E6);
}
// 01:42EB: the scroll starts at its first values; the screen copy's source,
// destination and length are loaded for the video transfer, which runs with
// interrupts disabled: the `di` ends the segment (its exit carries the mask).
template<class Z> Exit opening_video_transfer(Z& z) {
    z.xor_a(0x42EB);
    z.store_a_direct(0x42EC);                                        // C300
    z.load_immediate(0x42EF,z.r.a);
    z.store_a_direct(0x42F1);                                        // C304
    z.load_hl(0x42F4);
    z.load_de(0x42F7);
    z.load_bc(0x42FA);
    return z.final_disable_interrupts(0x42FD);
}
// 01:4304: after the protected load that follows the `ei` (01:4302, a segment of
// its own), its sound request is stored and the picture fades in.
template<class Z> Exit opening_fade_in(Z& z) {
    z.store_a_direct(0x4304);                                        // C004
    return call_at(z,0x4307);
}
// 01:4314: the top of the scroll loop: the frame wait.
template<class Z> Exit opening_frame(Z& z) { return number_then_call(z,0x4314); }
// 01:430A: the scroll's two counters (C264, C307) start; then the loop.
template<class Z> Exit opening_scroll_start(Z& z) {
    z.load_immediate(0x430A,z.r.a);
    z.store_a_direct(0x430C);                                        // C264
    z.load_immediate(0x430F,z.r.a);
    z.store_a_direct(0x4311);                                        // C307
    return opening_frame(z);
}
// 01:4319: button 1 or 2 skips the rest of the scroll.
template<class Z> Exit opening_buttons(Z& z) {
    z.load_a_direct(0x4319);                                         // the pressed buttons
    z.and_immediate(0x431C);
    return z.final_relative_if(0x431E,mk::Condition::nz);
}
// 01:4323: after one scroll step (01:43AA). The scroll goes on until both
// C307 and C304 reach their final values; then a wait for a button.
template<class Z> Exit opening_scroll_done(Z& z) {
    z.load_a_direct(0x4323);                                         // C307
    z.compare_immediate(0x4326);
    bool again=z.relative_if(0x4328,!z.zero());
    if (!again) {
        z.load_a_direct(0x432A);                                     // C304
        z.compare_immediate(0x432D);
        again=z.relative_if(0x432F,!z.zero());
    }
    if (again) return opening_frame(z);
    return call_at(z,0x4331);
}
// 01:4334: the scroll stops (C264 and C307 cleared) and the picture fades out.
template<class Z> Exit opening_scroll_stop(Z& z) {
    z.xor_a(0x4334);
    z.store_a_direct(0x4335);                                        // C264
    z.store_a_direct(0x4338);                                        // C307
    return call_at(z,0x433B);
}
// 01:433E: the second picture of the opening (00:3D47).
template<class Z> Exit opening_picture(Z& z) { return store_then_call(z,0x433E); }
// 01:4346 and 01:4354: two graphics sets for the decoder.
template<class Z> Exit opening_text_graphics(Z& z) { return bank_and_graphics(z,0x4346); }
template<class Z> Exit opening_frame_graphics(Z& z) { return graphics(z,0x4354); }
// 01:435D: the scroll position (C304, C300) back to zero, then the frame wait.
template<class Z> Exit opening_scroll_reset(Z& z) {
    z.xor_a(0x435D);
    z.store_a_direct(0x435E);                                        // C304
    z.store_a_direct(0x4361);                                        // C300
    return number_then_call(z,0x4364);
}
// 01:436C: a block of rows for the row drawer (00:3A68): source, destination, size.
template<class Z> Exit opening_rows(Z& z) {
    select_bank(z,0x436C);
    z.load_hl(0x4371);
    z.load_de(0x4374);
    z.load_bc(0x4377);
    return call_at(z,0x437A);
}

// ---- The opening's scroll step: 01:43AA-43CE --------------------------------------------------
// One frame of the scroll. C304 advances by one; at the bottom of the name
// table it wraps (by the immediate added), and D becomes 1 so C307, the count
// of screens left, goes down. The step returns unless that count has just
// reached one with a wrap; then the next layout is loaded (by the jump to the
// layout decoder at 01:43CC).
template<class Z> Exit opening_scroll_step(Z& z) {
    auto& r=z.r;
    z.load_de(0x43AA);
    z.load_a_direct(0x43AD);                                         // C304
    z.add_register(0x43B0,r.e);
    z.compare_immediate(0x43B1);
    if (!z.relative_if(0x43B3,z.carry())) {
        z.load_immediate(0x43B5,r.d);
        z.add_immediate(0x43B7);
    }
    z.store_a_direct(0x43B9);                                        // C304
    z.load_a_direct(0x43BC);                                         // C307
    z.subtract_register(0x43BF,r.d);
    z.store_a_direct(0x43C0);
    z.compare_immediate(0x43C3);
    if (auto exit=z.final_return_if(0x43C5,mk::Condition::nz)) return *exit;
    z.copy(0x43C6,r.a,r.d);
    z.or_a(0x43C7);
    if (auto exit=z.final_return_if(0x43C8,mk::Condition::z)) return *exit;
    z.load_hl(0x43C9);
    return jump_at(z,0x43CC);
}

// ---- The conversations: 01:43CF-4496 ----------------------------------------------------------
// 01:4459: the end of the conversation at 01:4414: its sound request, then a
// jump to the fade in.
template<class Z> Exit conversation_fade(Z& z) {
    z.load_immediate(0x4459,z.r.a);
    z.store_a_direct(0x445B);                                        // C004
    return jump_at(z,0x445E);
}
// 01:4464: the conversation at 01:4461 opens with the portrait of the first
// member whose record says alive (bit 0): the first member's, else the third
// member's, else a third portrait.
template<class Z> Exit speaker_portrait(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x4464,r.a);
    z.store_a_direct(0x4466);                                        // C004
    z.load_immediate(0x4469,r.a);                                    // the first member's portrait
    z.load_hl(0x446B);                                               // the first member's record
    z.test_bit_at_hl(0x446E,0);
    bool chosen=z.relative_if(0x4470,!z.zero());
    if (!chosen) {
        z.load_immediate(0x4472,r.a);                                // the third member's portrait
        z.load_hl(0x4474);
        z.test_bit_at_hl(0x4477,0);
        chosen=z.relative_if(0x4479,!z.zero());
        if (!chosen) z.load_immediate(0x447B,r.a);
    }
    return call_at(z,0x447D);                                          // 01:46D1
}

// ---- The boss scene: 01:4497-4516 -------------------------------------------------------------
// 01:4497: in one location the conversation is skipped (01:44A1).
template<class Z> Exit boss_location(Z& z) {
    z.load_a_direct(0x4497);                                         // C309
    z.compare_immediate(0x449A);
    return z.final_relative_if(0x449C,mk::Condition::nz);
}
// 01:44A1: after the transition, that location's transfer record goes to the
// map transfer (the jump into 01:4509).
template<class Z> Exit boss_skip(Z& z) {
    z.load_hl(0x44A1);
    return jump_at(z,0x44A4);
}
// 01:44C8: the boss picture (00:3D47).
template<class Z> Exit boss_picture(Z& z) { return store_then_call(z,0x44C8); }
// 01:44D0: the frame wait.
template<class Z> Exit boss_frame(Z& z) { return number_then_call(z,0x44D0); }
// 01:44D5: the sixteen palette bytes at C250 are all set to one colour (B
// counts them), then the fade in.
template<class Z> Exit boss_flash(Z& z) {
    z.load_hl(0x44D5);                                               // C250
    z.load_immediate(0x44D8,z.r.b);
    do {
        z.store_to_hl(0x44DA);
        z.increment_hl(0x44DC);
    } while (z.decrement_jump(0x44DD));
    return call_at(z,0x44DF);
}
// 01:44E5: the boss palette is copied from its bank and its enemy group is
// stored before the encounter setup.
template<class Z> Exit boss_encounter(Z& z) {
    select_bank(z,0x44E5);
    z.load_hl(0x44EA);
    z.load_de(0x44ED);
    z.load_bc(0x44F0);
    z.block_copy(0x44F3);
    return store_then_call(z,0x44F5);                                // C2E6
}
// 01:4509: game mode 8 (map loading), then the transfer record in HL is applied.
template<class Z> Exit boss_transfer(Z& z) {
    z.load_immediate(0x4509,z.r.a);
    z.store_a_direct(0x450B);                                        // the game mode
    return jump_at(z,0x450E);
}
// 01:4500: after the battle. A defeat (game mode 2) returns; a victory moves
// the party by the transfer record that follows the routine.
template<class Z> Exit boss_after_battle(Z& z) {
    z.load_a_direct(0x4500);                                         // the game mode
    z.compare_immediate(0x4503);
    if (auto exit=z.final_return_if(0x4505,mk::Condition::z)) return *exit;
    z.load_hl(0x4506);
    return boss_transfer(z);
}
// 01:451A: the transition picture (00:3D47).
template<class Z> Exit transition_picture(Z& z) { return store_then_call(z,0x451A); }
// 01:4522: its palette and graphics.
template<class Z> Exit transition_graphics(Z& z) {
    palette_and_graphics(z,0x4522);
    return call_at(z,0x4538);
}
// 01:453B: the frame wait.
template<class Z> Exit transition_frame(Z& z) { return number_then_call(z,0x453B); }
// 01:4543: the first object slot's state (C800) for the animation that follows.
template<class Z> Exit transition_object(Z& z) { return store_then_call(z,0x4543); }

// ---- The finale: 01:454E-467B -----------------------------------------------------------------
// 01:4551: the party's sprite is hidden (C900), the sound request and the
// picture are set, then the picture loader (00:3D47).
template<class Z> Exit finale_picture(Z& z) {
    z.load_immediate(0x4551,z.r.a);
    z.store_a_direct(0x4553);                                        // C900
    z.load_immediate(0x4556,z.r.a);
    z.store_a_direct(0x4558);                                        // C004
    return store_then_call(z,0x455B);                                // C29E
}
// 01:4563 and 01:456B: the frame wait, then a wait of B frames.
template<class Z> Exit finale_frame(Z& z) { return number_then_call(z,0x4563); }
template<class Z> Exit finale_pause(Z& z) {
    z.load_immediate(0x456B,z.r.b);
    return call_at(z,0x456D);
}
// 01:45DD: after the conversation, the credits picture's palette and graphics.
template<class Z> Exit finale_graphics(Z& z) {
    palette_and_graphics(z,0x45DD);
    return call_at(z,0x45F3);
}
// 01:45F6: the screen copy at D000 is cleared (the seed store 01:4604 and the
// block copy 01:4606 run on their own). The source load ends the segment.
template<class Z> Exit finale_clear_bank(Z& z) {
    select_bank(z,0x45F6);
    z.load_hl(0x45FB);                                                 // D000
    return mk::fall_through(0x45FB,0x45FE);
}
template<class Z> Exit finale_clear_destination(Z& z) {
    z.load_de(0x45FE);
    return load_pair_at(z,0x4601);                                     // the count
}
// 01:4608: the credits layout: source, destination and size for its decoder.
template<class Z> Exit finale_layout(Z& z) {
    z.load_hl(0x4608);
    z.load_de(0x460B);
    z.load_bc(0x460E);
    return call_at(z,0x4611);
}
// 01:4614: the frame wait.
template<class Z> Exit finale_frame_two(Z& z) { return number_then_call(z,0x4614); }
// 01:461F: the credits dungeon position, facing direction zero; then the
// dungeon view is entered as by a pit.
template<class Z> Exit finale_dungeon(Z& z) {
    z.load_hl(0x461F);
    z.store_hl_direct(0x4622);                                       // the dungeon position
    z.xor_a(0x4625);
    z.store_a_direct(0x4626);                                        // the direction
    return call_at(z,0x4629);
}
// 01:462C: the credits graphics.
template<class Z> Exit finale_credits_graphics(Z& z) { return bank_and_graphics(z,0x462C); }
// One step of the credits route: the script's bank is selected again (the
// handler maps others), the next command read and an end marker tested.
template<class Z> Exit route_read(Z& z) {
    z.load_immediate(0x4647,z.r.a);
    z.store_a_direct(0x4649);                                        // FFFF
    z.load_from_hl(0x464C,z.r.a);
    z.compare_immediate(0x464D);
    return z.final_relative_if(0x464F,mk::Condition::z);
}
// 01:463A: the credits latch (C2F5) is set, the sound request made and HL
// pointed at the route script; then its first step.
template<class Z> Exit route_start(Z& z) {
    z.load_immediate(0x463A,z.r.a);
    z.store_a_direct(0x463C);                                        // C2F5
    z.load_immediate(0x463F,z.r.a);
    z.store_a_direct(0x4641);                                        // C004
    z.load_hl(0x4644);                                               // the script
    return route_read(z);
}
// 01:4651: anything but the wait command is a movement (01:465D).
template<class Z> Exit route_wait_command(Z& z) {
    z.compare_immediate(0x4651);
    return z.final_relative_if(0x4653,mk::Condition::nz);
}
// 01:4655: the wait: B frames.
template<class Z> Exit route_wait(Z& z) {
    z.load_immediate(0x4655,z.r.b);
    return call_at(z,0x4657);
}
// 01:465A: after the wait, the next command (a jump back to the step).
template<class Z> Exit route_waited(Z& z) {
    z.increment_hl(0x465A);
    return relative_at(z,0x465B);
}
// 01:465D: a movement. The script pointer is kept on the stack and the command
// becomes the held buttons for the ordinary dungeon handler.
template<class Z> Exit route_walk(Z& z) {
    z.push(0x465D,z.r.h,z.r.l);
    z.store_a_direct(0x465E);                                        // the held buttons
    return call_at(z,0x4661);
}
// 01:4664: the pointer back, the next command (a jump back to the step).
template<class Z> Exit route_walked(Z& z) {
    z.pop(0x4664,z.r.h,z.r.l);
    z.increment_hl(0x4665);
    return relative_at(z,0x4666);
}
// 01:4668: the end marker. The sound request stops the music, the credits
// latch is cleared and a long wait follows.
template<class Z> Exit route_end(Z& z) {
    z.load_immediate(0x4668,z.r.a);
    z.store_a_direct(0x466A);                                        // C004
    z.xor_a(0x466D);
    z.store_a_direct(0x466E);                                        // C2F5
    z.load_immediate(0x4671,z.r.b);
    return call_at(z,0x4673);
}
// 01:4676: game mode 2 (the title) and the finale returns.
template<class Z> Exit finale_done(Z& z) {
    z.load_immediate(0x4676,z.r.a);
    z.store_a_direct(0x4678);                                        // the game mode
    return return_at(z,0x467B);
}

// ---- The portrait-conversation setup: 01:467F-46D0 --------------------------------------------
// 01:467F and 01:468D: the conversation's two graphics sets.
template<class Z> Exit setup_first_graphics(Z& z) { return bank_and_graphics(z,0x467F); }
template<class Z> Exit setup_second_graphics(Z& z) { return graphics(z,0x468D); }
// 01:4696: the palette buffer C240 is cleared (a zero byte copied forward),
// the conversation palette copied after it, and the window graphics loaded.
template<class Z> Exit setup_palette(Z& z) {
    select_bank(z,0x4696);
    z.load_hl(0x469B);                                               // C240
    z.load_de(0x469E);
    z.store_to_hl(0x46A1);                                           // the zero seed
    z.load_bc(0x46A3);
    z.block_copy(0x46A6);
    z.load_hl(0x46A8);                                               // the palette, into the buffer after it
    z.load_bc(0x46AB);
    z.block_copy(0x46AE);
    return graphics(z,0x46B0);
}
// 01:46B9: the window layout for the layout decoder.
template<class Z> Exit setup_layout(Z& z) { return text_then_call(z,0x46B9); }
// 01:46BF: the scroll (C304, C300) and the saved-window flag (C2D3) cleared,
// then the frame wait.
template<class Z> Exit setup_reset(Z& z) {
    z.xor_a(0x46BF);
    z.store_a_direct(0x46C0);                                        // C304
    z.store_a_direct(0x46C3);                                        // C300
    z.store_a_direct(0x46C6);                                        // C2D3
    return number_then_call(z,0x46C9);
}

// ---- The portrait transition: 01:46D1-471D ----------------------------------------------------
// 01:46D1: the requested portrait (A) waits on the stack during the fade out.
template<class Z> Exit portrait_keep(Z& z) {
    z.push(0x46D1,z.r.a,z.r.f);
    return call_at(z,0x46D2);
}
// 01:46D5: the portrait's five-byte record (bank, palette, layout) is found at
// five times its number; the bank is selected, the palette copied to C240 and
// its graphics loaded. The pointer to the layout word stays on the stack.
template<class Z> Exit portrait_record(Z& z) {
    auto& r=z.r;
    z.pop(0x46D5,r.a,r.f);
    z.copy(0x46D6,r.l,r.a);
    z.add_register(0x46D7,r.a);
    z.add_register(0x46D8,r.a);
    z.add_register(0x46D9,r.l);                                      // number times five
    z.copy(0x46DA,r.l,r.a);
    z.load_immediate(0x46DB,r.h);
    z.load_de(0x46DD);                                               // the table
    z.add_to_hl(0x46E0,z.de());
    z.load_from_hl(0x46E1,r.a);
    z.store_a_direct(0x46E2);                                        // the bank, into FFFF
    z.increment_hl(0x46E5);
    z.load_from_hl(0x46E6,r.e);
    z.increment_hl(0x46E7);
    z.load_from_hl(0x46E8,r.d);                                      // the palette
    z.increment_hl(0x46E9);
    z.push(0x46EA,r.h,r.l);                                          // the layout word's address
    z.exchange_de_hl(0x46EB);
    z.load_de(0x46EC);                                               // C240
    z.load_bc(0x46EF);
    z.block_copy(0x46F2);
    z.load_de(0x46F4);                                               // the graphics' video address
    return call_at(z,0x46F7);
}
// 01:46FA: the layout bank selected again; the layout pointer read through the
// saved address; its destination loaded. The count load ends the segment before
// the `di` (01:470A) and the video transfer.
template<class Z> Exit portrait_layout(Z& z) {
    auto& r=z.r;
    select_bank(z,0x46FA);
    z.pop(0x46FF,r.h,r.l);
    z.load_from_hl(0x4700,r.a);
    z.increment_hl(0x4701);
    z.load_from_hl(0x4702,r.h);
    z.copy(0x4703,r.l,r.a);
    z.load_de(0x4704);
    return load_pair_at(z,0x4707);
}
// 01:470E: the lower panel is cleared: destination, count and fill word.
template<class Z> Exit portrait_clear_lower(Z& z) {
    z.load_de(0x470E);
    z.load_bc(0x4711);
    z.load_hl(0x4714);
    return call_at(z,0x4717);
}

// ---- Darkfalz and the final answer: 01:5619-569B ----------------------------------------------
// 01:5619 and 01:5621: the sound request, the wait, then the Darkfalz picture.
template<class Z> Exit darkfalz_sound(Z& z) { return store_then_call(z,0x5619); }
template<class Z> Exit darkfalz_picture(Z& z) { return store_then_call(z,0x5621); }
// 01:5629: sixteen palette bytes from their bank to C250; the count load ends the
// segment and the shared block transfer runs the copy (01:5637).
template<class Z> Exit darkfalz_palette(Z& z) {
    select_bank(z,0x5629);
    z.load_hl(0x562E);
    z.load_de(0x5631);                                               // C250
    return load_pair_at(z,0x5634);
}
// 01:5639: the frame wait.
template<class Z> Exit darkfalz_frame(Z& z) { return number_then_call(z,0x5639); }
// 01:5641: the enemy group (C2E6) before the encounter setup.
template<class Z> Exit darkfalz_encounter(Z& z) { return store_then_call(z,0x5641); }
// 01:564C: the next picture number, then the battle and its post-battle check.
template<class Z> Exit darkfalz_battle(Z& z) { return store_then_call(z,0x564C); }
// 01:5657: a living first member skips the extra text.
template<class Z> Exit darkfalz_survivor(Z& z) {
    z.load_a_direct(0x5657);                                         // the first member's status
    z.or_a(0x565A);
    return z.final_relative_if(0x565B,mk::Condition::nz);
}
// 01:565D: the extra text.
template<class Z> Exit darkfalz_extra_text(Z& z) { return text_then_call(z,0x565D); }
// 01:5669, 01:5671 and 01:5679: the final picture, the frame wait, then the
// scene object for the scene-object loader (01:617D).
template<class Z> Exit final_picture(Z& z) { return store_then_call(z,0x5669); }
template<class Z> Exit final_frame(Z& z) { return number_then_call(z,0x5671); }
template<class Z> Exit final_object(Z& z) { return number_then_call(z,0x5679); }
// 01:5681: the final question.
template<class Z> Exit final_question(Z& z) { return text_then_call(z,0x5681); }
// 01:568A: one of two replies by the answer (Z for yes).
template<class Z> Exit final_answer(Z& z) {
    z.load_hl(0x568A);
    if (!z.relative_if(0x568D,z.zero())) z.load_hl(0x568F);
    return call_at(z,0x5692);
}
// 01:5698: both answers drop the interaction's return address and jump into
// the finale (01:454E).
template<class Z> Exit final_unwind(Z& z) {
    z.pop(0x5698,z.r.h,z.r.l);
    return jump_at(z,0x5699);
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address, each checked against the original on every
// call: its unit (the entry itself unless named), the exits, the stack it may
// hold (scratch) or has inherited, and the slot-2 mapper permission.
void register_all(RoutineRegistry& registry) {
    auto F=[](std::uint16_t at, std::int8_t stack=0) { return mk::core_final_at(at,stack); };
    auto R=[](std::uint16_t at, std::int8_t stack=0) { return mk::returned_at(at,stack); };
    auto C=[](std::uint16_t at, std::int8_t stack=0) { return mk::conditional_at(at,stack); };
    auto add_in=[&](std::uint16_t entry, std::uint16_t unit, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        auto spec=mk::segment(RoutineEntry{1,unit},std::move(exits),permissions);
        spec.observe_entry=entry==0x45dd || entry==0x4676;
        mk::add_segment(registry,RoutineEntry{1,entry},std::move(spec),logic);
    };
    auto add=[&](std::uint16_t entry, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        add_in(entry,entry,std::move(exits),permissions,logic);
    };
    // A setup that ends at one call: room for the call's return address, and
    // the mapper permission when it selects a bank.
    auto add_call=[&](std::uint16_t entry, std::uint16_t end, auto logic) { add(entry,{F(end)},{.scratch_stack=2},logic); };
    auto add_mapped_call=[&](std::uint16_t entry, std::uint16_t end, auto logic) {
        add(entry,{F(end)},{.slot2_mapper=true,.scratch_stack=2},logic);
    };
    // A single `di` or `ei`, so its interrupt delay is the original's.
    auto add_interrupt_switch=[&](RoutineEntry entry, InterruptMask mask) {
        auto exit=mk::core_final_at(entry.address);
        exit.interrupt_mask=mask;
        auto spec=mk::segment(entry,{exit});
        spec.single_instruction=true;
        mk::add_segment(registry,entry,std::move(spec),[at=entry.address,mask](auto& z) -> Exit {
            return mask==InterruptMask::enable ? z.final_enable_interrupts(at) : z.final_disable_interrupts(at);
        });
    };
    // A joining instruction (milestone 14) that sits between two
    // segments: a call into a shared helper, or a jump, as a segment of its own.
    auto add_joining_call=[&](std::uint16_t entry) {
        auto spec=mk::segment(RoutineEntry{1,entry},{F(entry)},{.scratch_stack=2});
        spec.single_instruction=true;
        mk::add_segment(registry,RoutineEntry{1,entry},std::move(spec),[entry](auto& z) -> Exit { return z.final_call(entry); });
    };
    auto add_joining_jump=[&](std::uint16_t entry) {
        auto spec=mk::segment(RoutineEntry{1,entry},{F(entry)});
        spec.single_instruction=true;
        mk::add_segment(registry,RoutineEntry{1,entry},std::move(spec),[entry](auto& z) -> Exit { return z.final_jump(entry); });
    };
    auto add_joining_load=[&](std::uint16_t entry, auto logic) {
        auto spec=mk::segment(RoutineEntry{1,entry},{F(entry)});
        spec.single_instruction=true;
        mk::add_segment(registry,RoutineEntry{1,entry},std::move(spec),logic);
    };

    // ---- entry facades: the call keeps its return address ----
    for (const auto entry:{RoutineEntry{0,0x074D},RoutineEntry{1,0x454E}}) {
        auto spec=mk::segment(entry,{mk::core_final_at(entry.address)},{.scratch_stack=2});
        spec.single_instruction=true;
        mk::add_segment(registry,entry,spec,[at=entry.address](auto& z) -> Exit { return facade(z,at); });
    }

    // ---- the conversations' texts and portraits (lists in game/scenes.h) ----
    for (const auto at:scenes::script_dialogue_sites) add_call(at,after(at,3),[at](auto& z) -> Exit { return text_then_call(z,at); });
    for (const auto at:scenes::script_portrait_sites) add_call(at,after(at,2),[at](auto& z) -> Exit { return number_then_call(z,at); });
    // Each conversation (01:43CF, 01:4414, 01:4497) starts with a sound request
    // and its first portrait. The opening and the conversations at 01:43CF and
    // 01:4461 end with a sound request and a return.
    for (const auto at:{std::uint16_t{0x43D2},std::uint16_t{0x4417},std::uint16_t{0x44AA}})
        add_call(at,after(at,7),[at](auto& z) -> Exit { return conversation_start(z,at); });
    for (const auto at:{std::uint16_t{0x43A4},std::uint16_t{0x440E},std::uint16_t{0x4491}})
        add(at,{R(after(at,5))},{},[at](auto& z) -> Exit { return conversation_end(z,at); });

    // ---- the opening ----
    add_call(0x42AC,0x42B1,[](auto& z) -> Exit { return opening_sound(z); });
    add_mapped_call(0x42B4,0x42CA,[](auto& z) -> Exit { return opening_graphics(z); });                    // after the fade out
    add_mapped_call(0x42CD,0x42D5,[](auto& z) -> Exit { return opening_layout(z); });
    add(0x42D8,{F(0x42DE)},{},[](auto& z) -> Exit { return opening_screen_copy(z); });                      // after the layout decoder
    register_block_transfer(registry,{1,0x42E1});
    add_in(0x42E3,0x42D8,{F(0x42E6)},{},[](auto& z) -> Exit { return opening_screen_second_copy(z); });
    register_block_transfer(registry,{1,0x42E9});
    auto di=mk::core_final_at(0x42FD);
    di.interrupt_mask=InterruptMask::disable;
    add_in(0x42EB,0x42D8,{di},{},[](auto& z) -> Exit { return opening_video_transfer(z); });
    add_interrupt_switch({1,0x4301},InterruptMask::enable);
    add_call(0x4304,0x4307,[](auto& z) -> Exit { return opening_fade_in(z); });
    add_call(0x430A,0x4316,[](auto& z) -> Exit { return opening_scroll_start(z); });                        // after the fade in
    add_call(0x4314,0x4316,[](auto& z) -> Exit { return opening_frame(z); });
    add(0x4319,{C(0x431E)},{},[](auto& z) -> Exit { return opening_buttons(z); });                          // after the frame wait
    add(0x4323,{F(0x4316),F(0x4331)},{.scratch_stack=2},[](auto& z) -> Exit { return opening_scroll_done(z); }); // after the scroll step
    add_call(0x4334,0x433B,[](auto& z) -> Exit { return opening_scroll_stop(z); });
    add_call(0x433E,0x4343,[](auto& z) -> Exit { return opening_picture(z); });
    add_mapped_call(0x4346,0x4351,[](auto& z) -> Exit { return opening_text_graphics(z); });
    add_call(0x4354,0x435A,[](auto& z) -> Exit { return opening_frame_graphics(z); });
    add_call(0x435D,0x4366,[](auto& z) -> Exit { return opening_scroll_reset(z); });
    add_mapped_call(0x436C,0x437A,[](auto& z) -> Exit { return opening_rows(z); });                       // after the fade in
    add(0x43AA,{R(0x43C5),R(0x43C8),F(0x43CC)},{},[](auto& z) -> Exit { return opening_scroll_step(z); });

    // ---- the conversations ----
    add(0x4459,{F(0x445E)},{},[](auto& z) -> Exit { return conversation_fade(z); });                        // after the fade out
    add(0x4464,{F(0x447D)},{.scratch_stack=2},[](auto& z) -> Exit { return speaker_portrait(z); });         // after the conversation setup

    // ---- the boss scene ----
    add(0x4497,{C(0x449C)},{},[](auto& z) -> Exit { return boss_location(z); });
    add(0x44A1,{F(0x44A4)},{},[](auto& z) -> Exit { return boss_skip(z); });                                // after the transition (01:4517)
    add_call(0x44C8,0x44CD,[](auto& z) -> Exit { return boss_picture(z); });
    add_call(0x44D0,0x44D2,[](auto& z) -> Exit { return boss_frame(z); });
    add_call(0x44D5,0x44DF,[](auto& z) -> Exit { return boss_flash(z); });
    add_mapped_call(0x44E5,0x44FA,[](auto& z) -> Exit { return boss_encounter(z); });                     // after the button wait
    add(0x4500,{R(0x4505),F(0x450E)},{},[](auto& z) -> Exit { return boss_after_battle(z); });               // after the battle (00:100F)
    add(0x4509,{F(0x450E)},{},[](auto& z) -> Exit { return boss_transfer(z); });
    add_call(0x451A,0x451F,[](auto& z) -> Exit { return transition_picture(z); });
    add_mapped_call(0x4522,0x4538,[](auto& z) -> Exit { return transition_graphics(z); });
    add_call(0x453B,0x453D,[](auto& z) -> Exit { return transition_frame(z); });
    add_call(0x4543,0x4548,[](auto& z) -> Exit { return transition_object(z); });

    // ---- the finale ----
    add_call(0x4551,0x4560,[](auto& z) -> Exit { return finale_picture(z); });                              // after the fade out
    add_call(0x4563,0x4565,[](auto& z) -> Exit { return finale_frame(z); });
    add_call(0x456B,0x456D,[](auto& z) -> Exit { return finale_pause(z); });
    add_mapped_call(0x45DD,0x45F3,[](auto& z) -> Exit { return finale_graphics(z); });
    add(0x45F6,{F(0x45FB)},{.slot2_mapper=true},[](auto& z) -> Exit { return finale_clear_bank(z); });
    add_in(0x45FE,0x45F6,{F(0x4601)},{},[](auto& z) -> Exit { return finale_clear_destination(z); });
    register_block_transfer(registry,{1,0x4606});
    add_in(0x4608,0x45F6,{F(0x4611)},{.scratch_stack=2},[](auto& z) -> Exit { return finale_layout(z); });  // after the clear
    add_call(0x4614,0x4616,[](auto& z) -> Exit { return finale_frame_two(z); });
    add_call(0x461F,0x4629,[](auto& z) -> Exit { return finale_dungeon(z); });
    add_mapped_call(0x462C,0x4637,[](auto& z) -> Exit { return finale_credits_graphics(z); });
    add(0x463A,{C(0x464F)},{.slot2_mapper=true},[](auto& z) -> Exit { return route_start(z); });
    add(0x4647,{C(0x464F)},{.slot2_mapper=true},[](auto& z) -> Exit { return route_read(z); });
    add(0x4651,{C(0x4653)},{},[](auto& z) -> Exit { return route_wait_command(z); });
    add_call(0x4655,0x4657,[](auto& z) -> Exit { return route_wait(z); });
    add(0x465A,{F(0x465B)},{},[](auto& z) -> Exit { return route_waited(z); });                             // after the wait
    add(0x465D,{F(0x4661,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return route_walk(z); });
    add_in(0x4664,0x465D,{F(0x4666,2)},{.inherited_stack=2},[](auto& z) -> Exit { return route_walked(z); }); // after the dungeon handler
    add_call(0x4668,0x4673,[](auto& z) -> Exit { return route_end(z); });
    add(0x4676,{R(0x467B)},{},[](auto& z) -> Exit { return finale_done(z); });

    // ---- the portrait-conversation setup ----
    add_mapped_call(0x467F,0x468A,[](auto& z) -> Exit { return setup_first_graphics(z); });                // after the fade out
    add_call(0x468D,0x4693,[](auto& z) -> Exit { return setup_second_graphics(z); });
    add_mapped_call(0x4696,0x46B6,[](auto& z) -> Exit { return setup_palette(z); });
    add_call(0x46B9,0x46BC,[](auto& z) -> Exit { return setup_layout(z); });
    add_call(0x46BF,0x46CB,[](auto& z) -> Exit { return setup_reset(z); });

    // ---- the portrait transition (unit 01:46D1) ----
    add(0x46D1,{F(0x46D2,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return portrait_keep(z); });
    add_in(0x46D5,0x46D1,{F(0x46F7)},{.slot2_mapper=true,.scratch_stack=2,.inherited_stack=2},
           [](auto& z) -> Exit { return portrait_record(z); });                                              // after the fade out (01:7AFD)
    add_in(0x46FA,0x46D1,{F(0x4707,2)},{.slot2_mapper=true,.inherited_stack=2},[](auto& z) -> Exit { return portrait_layout(z); });
    add_interrupt_switch({1,0x470A},InterruptMask::disable);
    add_in(0x470E,0x46D1,{F(0x4717)},{.scratch_stack=2},[](auto& z) -> Exit { return portrait_clear_lower(z); }); // after the layout transfer
    add_interrupt_switch({1,0x471A},InterruptMask::enable);

    // ---- Darkfalz and the final answer ----
    add_call(0x5619,0x561E,[](auto& z) -> Exit { return darkfalz_sound(z); });
    add_call(0x5621,0x5626,[](auto& z) -> Exit { return darkfalz_picture(z); });
    add(0x5629,{F(0x5634)},{.slot2_mapper=true},[](auto& z) -> Exit { return darkfalz_palette(z); });
    register_block_transfer(registry,{1,0x5637});
    add_call(0x5639,0x563B,[](auto& z) -> Exit { return darkfalz_frame(z); });
    add_call(0x5641,0x5646,[](auto& z) -> Exit { return darkfalz_encounter(z); });
    add_call(0x564C,0x5651,[](auto& z) -> Exit { return darkfalz_battle(z); });
    add(0x5657,{C(0x565B)},{},[](auto& z) -> Exit { return darkfalz_survivor(z); });                        // after the window is closed (00:3464)
    add_call(0x565D,0x5660,[](auto& z) -> Exit { return darkfalz_extra_text(z); });
    add_call(0x5669,0x566E,[](auto& z) -> Exit { return final_picture(z); });
    add_call(0x5671,0x5673,[](auto& z) -> Exit { return final_frame(z); });
    add_call(0x5679,0x567B,[](auto& z) -> Exit { return final_object(z); });
    add_call(0x5681,0x5684,[](auto& z) -> Exit { return final_question(z); });
    add(0x568A,{F(0x5692)},{.scratch_stack=2},[](auto& z) -> Exit { return final_answer(z); });             // after the yes/no (00:2D19)
    add(0x5698,{F(0x5699,2)},{.inherited_stack=2},[](auto& z) -> Exit { return final_unwind(z); });         // after the window is closed (00:3464)

    // ---- joining stretches (milestone 14) ----
    // The calls after a fade, a frame wait, a button wait, a window close or a
    // screen transfer, the jumps back into a frame and the loads after an EI,
    // each a one-instruction segment of its own.
    for (const std::uint16_t entry:{0x42FE,0x4320,0x4369,0x437D,0x4380,0x4568,0x4570,0x4597,0x459A,0x45DA,0x4619,0x461C,0x467C,0x470B,
                                    0x54EF,0x563E,0x5649,0x5654,0x5666,0x5676,0x567E,0x5687})
        add_joining_call(entry);
    for (const std::uint16_t entry:{0x46CE,0x471B}) add_joining_jump(entry);
    add_joining_load(0x4302,[](auto& z) -> Exit { z.load_immediate(0x4302,z.r.a); return mk::fall_through(0x4302,0x4304); });     // the sound request, kept before any interrupt after the EI
    add_joining_load(0x4604,[](auto& z) -> Exit { z.store_to_hl(0x4604); return mk::fall_through(0x4604,0x4606); });            // the zero seed of the screen-copy clear
}
}

void register_scene_scripts(RoutineRegistry& registry) {
    register_all(registry);
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_scenes_script_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{1,0x4453},{1,0x444D},{kit::core_final_at(0x4453)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4453); });
    kit::add_joining(registry,{1,0x4456},{1,0x444D},{kit::core_final_at(0x4456)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4456); });
    kit::add_joining(registry,{1,0x449E},{1,0x4497},{kit::core_final_at(0x449E)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x449E); });
    kit::add_joining(registry,{1,0x44A7},{1,0x4497},{kit::core_final_at(0x44A7)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x44A7); });
    kit::add_joining(registry,{1,0x44C5},{1,0x44BF},{kit::core_final_at(0x44C5)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x44C5); });
    kit::add_joining(registry,{1,0x44E2},{1,0x44D5},{kit::core_final_at(0x44E2)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x44E2); });
    kit::add_joining(registry,{1,0x44FD},{1,0x44E5},{kit::core_final_at(0x44FD)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x44FD); });
    kit::add_joining(registry,{1,0x4517},{1,0x44BF},{kit::core_final_at(0x4517)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4517); });
    kit::add_joining(registry,{1,0x4540},{1,0x453B},{kit::core_final_at(0x4540)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x4540); });
    kit::add_joining(registry,{1,0x454B},{1,0x4543},{kit::core_final_at(0x454B)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x454B); });
    kit::add_joining(registry,{1,0x5663},{1,0x565D},{kit::core_final_at(0x5663)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x5663); });
}
}
