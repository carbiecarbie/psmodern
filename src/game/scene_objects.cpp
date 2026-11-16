// SPDX-License-Identifier: GPL-3.0-or-later
// Scene objects (milestone 13): the handlers of two object kinds that appear
// in scenes, the treasure chest (01:5C63 and 01:5C99) and a moving sprite
// (01:5D30 and 01:5D5B). Like the battle effect sprites, they are object-slot
// handlers reached through the object handler table, working on one object
// record through IY; their units and entry lists (chest, moving,
// chest_entries, moving_entries) are declared in game/battle_sprites.h next
// to those sprites, and this file registers them, so it shares the
// battle_sprites namespace.
//
// An object record holds, among others: +0 its state (zero is a free slot),
// +1 the sprite frame, +2 and +4 the Y and X position, +10 phase bits, +13 a
// step counter, +14 a countdown and +15 a second frame (the chest's outcome,
// the moving sprite's first frame).
//
// The routines of this file:
//   01:5C63  the chest's set-up: the slot is cleared (the shared slot clear,
//            a call the core runs), the state set, a fixed position, the
//            first frame and the countdown stored, and a random byte drawn.
//            The outcome frame is chosen against the threshold byte C2E0:
//            zero, or below the random byte (unless it is F0h or more),
//            gives the plain opened chest; otherwise the threshold's lowest
//            bit picks one of two other outcomes.
//   01:5C99  the chest each frame, in three phases kept as bits 0-2 of +10:
//            nothing until bit 0 is set; opening (the frame steps every
//            eighth frame up to the opened chest, then a pause and bit 1);
//            the reveal (the outcome frame is shown and bit 2 set: a plain
//            chest frees its slot, the others request a sound); and the
//            outcome's own animation, which flashes the screen once (the
//            shared flash routine) and ends on the opened chest with the
//            slot freed.
//   01:5D30  the moving sprite's set-up: position, first frame and a sound
//            request, chosen by whether the current location (C309) is one
//            particular location.
//   01:5D5B  the moving sprite each frame: every eighth frame its frame
//            cycles through four and it moves. In that location it moves
//            down and, every eighth row, one pixel left, and leaves at a
//            fixed row; elsewhere it moves up and left by one pixel a step
//            and leaves at a fixed column.
// Both per-frame handlers are a call of their body followed by `ld a,FFh;
// ret`, the answer the object update expects.
//
// The addresses passed to the kit are the original's, because the kit reads
// each instruction's operands and cycle cost from the player's ROM; no ROM
// value is written here.
#include "game/battle_sprites.h"
#include "game/map_kit.h"

namespace psmodern::game::battle_sprites {
namespace {
namespace k=map_kit;
using k::Exit;
constexpr auto obj=k::Index::iy;                                      // the object record

// The ways a segment ends. Each names the original's final instruction and
// charges its cycles; the core runs that instruction.
Exit call_at(std::uint16_t at) { return k::continues(at,k::call_cycles); }
Exit return_at(std::uint16_t at) { return k::returns(at,k::return_cycles); }
Exit return_taken_at(std::uint16_t at) { return k::returns(at,k::taken_conditional_return_cycles); }

// A call at the very start of a handler (the slot clear, or the call of the
// per-frame body) is a one-instruction entry the core runs.
void register_handler_call(RoutineRegistry& r, RoutineEntry unit, std::uint16_t pc) {
    auto spec=k::segment(unit,{k::core_final_at(pc)},{.scratch_stack=2});
    spec.single_instruction=true;
    r.add({1,pc},std::move(spec),[](const Machine&){return k::call_cycles;},
          [pc](RoutineContext& c){c.finish_core(pc);});
}
// After the per-frame body returns: `ld a,FFh` at pc, then the return.
template<class Z> Exit handler_answer(Z& z, std::uint16_t pc) {
    z.load_immediate(pc,z.r.a);
    return return_at(static_cast<std::uint16_t>(pc+2));
}

// ---- The chest's set-up: 01:5C63 -----------------------------------------------------------------
// 01:5C66: after the slot clear. The state goes up (the slot is in use),
// the position, first frame and countdown are stored, and the random number
// generator is called.
template<class Z> Exit chest_setup(Z& z) {
    z.increment_indexed(0x5C66,obj);                                  // +0, the state
    for (auto pc:{0x5C69,0x5C6D,0x5C71,0x5C75}) z.store_immediate_indexed(pc,obj);   // +2, +4, +1, +14
    return call_at(0x5C79);
}
// 01:5C7C: the random byte in B and the plain outcome in C. A nonzero
// threshold that is F0h or more, or not below the random byte, selects one
// of the other two outcomes by its lowest bit (rotated into the carry). C becomes
// the outcome frame (+15).
template<class Z> Exit chest_outcome(Z& z) {
    z.copy(0x5C7C,z.r.b,z.r.a);                                       // the random byte
    z.load_immediate(0x5C7D,z.r.c);                                   // the plain opened chest
    z.load_a_direct(0x5C7F);                                          // C2E0, the threshold
    z.or_a(0x5C82);
    if (!z.relative_if(0x5C83,z.zero())) {
        z.compare_immediate(0x5C85);
        const bool guaranteed=z.relative_if(0x5C87,!z.carry());
        bool selected=guaranteed;
        if (!guaranteed) {
            z.compare_register(0x5C89,z.r.b);
            selected=!z.relative_if(0x5C8A,z.carry());
        }
        if (selected) {
            z.rotate_right_circular_a(0x5C8C);                        // the lowest bit into the carry
            z.load_immediate(0x5C8D,z.r.c);
            if (!z.relative_if(0x5C8F,!z.carry())) z.load_immediate(0x5C91,z.r.c);
        }
    }
    z.store_to_indexed(0x5C93,obj,z.r.c);                             // +15
    z.load_immediate(0x5C96,z.r.a);                                   // the handler's answer, FFh
    return return_at(0x5C98);
}

// ---- The chest each frame: 01:5C99 ----------------------------------------------------------------
// 01:5C9F: the phase bits. Bit 0 clear: nothing yet. Bit 1 set: past the
// opening (the jump goes to the reveal test); otherwise the opening.
template<class Z> Exit chest_phase(Z& z) {
    z.test_bit_indexed(0x5C9F,obj,0);
    if (z.zero()) return return_taken_at(0x5CA3);
    z.return_not_taken(0x5CA3);
    z.test_bit_indexed(0x5CA4,obj,1);
    return k::relative_if_exit(0x5CA8,!z.zero());
}
// 01:5CAA: the opening. Every eighth frame the frame steps; on reaching the
// opened chest the countdown is set for a pause and bit 1 marks the opening
// done.
template<class Z> Exit chest_opening(Z& z) {
    z.decrement_indexed(0x5CAA,obj);                                  // +14, the countdown
    if (!z.sign()) return return_taken_at(0x5CAD);
    z.return_not_taken(0x5CAD);
    z.store_immediate_indexed(0x5CAE,obj);                            // reload it
    z.load_from_indexed(0x5CB2,obj,z.r.a);                            // +1, the frame
    z.increment(0x5CB5,z.r.a);
    z.store_to_indexed(0x5CB6,obj,z.r.a);
    z.compare_immediate(0x5CB9);                                      // the opened chest yet?
    if (!z.zero()) return return_taken_at(0x5CBB);
    z.return_not_taken(0x5CBB);
    z.store_immediate_indexed(0x5CBC,obj);                            // the pause
    z.set_bit_indexed(0x5CC0,obj,1);
    return return_at(0x5CC4);
}
// 01:5CC5: bit 2 set: past the reveal (the jump goes to the outcome's
// animation).
template<class Z> Exit chest_reveal_test(Z& z) {
    z.test_bit_indexed(0x5CC5,obj,2);
    return k::relative_if_exit(0x5CC9,!z.zero());
}
// 01:5CCB: the reveal, after the pause. The outcome frame is shown, bit 2
// set, and the outcome compared with the plain chest (the jump goes to the
// sound request).
template<class Z> Exit chest_reveal(Z& z) {
    z.decrement_indexed(0x5CCB,obj);
    if (!z.sign()) return return_taken_at(0x5CCE);
    z.return_not_taken(0x5CCE);
    z.store_immediate_indexed(0x5CCF,obj);                            // the countdown
    z.load_from_indexed(0x5CD3,obj,z.r.a);                            // +15, the outcome
    z.store_to_indexed(0x5CD6,obj,z.r.a);                             // shown as the frame
    z.set_bit_indexed(0x5CD9,obj,2);
    z.compare_immediate(0x5CDD);
    return k::relative_if_exit(0x5CDF,!z.zero());
}
// 01:5CE1: a plain chest frees its slot.
template<class Z> Exit chest_empty(Z& z) {
    z.store_immediate_indexed(0x5CE1,obj);                            // +0 = 0
    return return_at(0x5CE5);
}
// 01:5CE6: the other outcomes request a sound, one number for the first
// outcome and the next number for the second.
template<class Z> Exit chest_reveal_sound(Z& z) {
    z.compare_immediate(0x5CE6);
    z.load_immediate(0x5CE8,z.r.a);
    if (!z.relative_if(0x5CEA,z.zero())) z.increment(0x5CEC,z.r.a);
    z.store_a_direct(0x5CED);                                         // the sound request byte
    return return_at(0x5CF0);
}
// 01:5CF1: the outcome's animation, a step every fourth frame; the jump
// goes to the second outcome's version.
template<class Z> Exit chest_animation_step(Z& z) {
    z.decrement_indexed(0x5CF1,obj);
    if (!z.sign()) return return_taken_at(0x5CF4);
    z.return_not_taken(0x5CF4);
    z.store_immediate_indexed(0x5CF5,obj);
    z.load_from_indexed(0x5CF9,obj,z.r.a);                            // +15, the outcome
    z.compare_immediate(0x5CFC);
    return k::relative_if_exit(0x5CFE,!z.zero());
}
// The end of either outcome's animation: back to the opened-chest frame and
// the slot freed. `at` is the first of its three instructions, because the
// original has this tail twice, at 01:5D11 and at 01:5D27.
template<class Z> Exit finish_chest(Z& z, std::uint16_t at) {
    z.store_immediate_indexed(at,obj);                                // +1, the opened chest
    z.store_immediate_indexed(static_cast<std::uint16_t>(at+4),obj);  // +0 = 0
    return return_at(static_cast<std::uint16_t>(at+8));
}
// 01:5D00: the first outcome. The frame steps; one particular frame calls
// the screen flash (a conditional call), the flags waiting on the stack.
template<class Z> Exit chest_first_outcome_step(Z& z) {
    z.load_from_indexed(0x5D00,obj,z.r.a);
    z.increment(0x5D03,z.r.a);
    z.store_to_indexed(0x5D04,obj,z.r.a);
    z.push(0x5D07,z.r.a,z.r.f);
    z.compare_immediate(0x5D08);
    return k::continues(0x5D0A,z.zero() ? k::call_cycles : k::untaken_call_cycles);
}
// 01:5D0D: after the flash call; the animation ends at its last frame.
template<class Z> Exit chest_first_outcome_end(Z& z) {
    z.pop(0x5D0D,z.r.a,z.r.f);
    z.compare_immediate(0x5D0E);
    if (z.carry()) return return_taken_at(0x5D10);
    z.return_not_taken(0x5D10);
    return finish_chest(z,0x5D11);
}
// 01:5D1A: the second outcome. The frame steps up to its last one, which
// calls the screen flash.
template<class Z> Exit chest_second_outcome_step(Z& z) {
    z.load_from_indexed(0x5D1A,obj,z.r.a);
    z.increment(0x5D1D,z.r.a);
    z.store_to_indexed(0x5D1E,obj,z.r.a);
    z.compare_immediate(0x5D21);
    if (z.carry()) return return_taken_at(0x5D23);
    z.return_not_taken(0x5D23);
    return call_at(0x5D24);
}
// 01:5D27: after the flash, the end.
template<class Z> Exit chest_second_outcome_end(Z& z) { return finish_chest(z,0x5D27); }

// ---- The moving sprite: 01:5D30 and 01:5D5B ------------------------------------------------------
// 01:5D33: after the slot clear. The state goes up; the first frame and the
// position (D = Y, E = X) depend on the location; the frame is also the
// base of the four-frame cycle (+15); a sound is requested.
template<class Z> Exit moving_setup(Z& z) {
    z.increment_indexed(0x5D33,obj);                                  // +0, the state
    z.load_a_direct(0x5D36);                                          // C309, the location
    z.compare_immediate(0x5D39);
    z.load_immediate(0x5D3B,z.r.a);                                   // elsewhere
    z.load_de(0x5D3D);
    if (!z.relative_if(0x5D40,!z.zero())) {                           // in that location
        z.load_immediate(0x5D42,z.r.a);
        z.load_de(0x5D44);
    }
    z.store_to_indexed(0x5D47,obj,z.r.d);                             // +2, Y
    z.store_to_indexed(0x5D4A,obj,z.r.e);                             // +4, X
    z.store_to_indexed(0x5D4D,obj,z.r.a);                             // +1, the frame
    z.store_to_indexed(0x5D50,obj,z.r.a);                             // +15, the base frame
    z.load_immediate(0x5D53,z.r.a);
    z.store_a_direct(0x5D55);                                         // the sound request byte
    z.load_immediate(0x5D58,z.r.a);                                   // the handler's answer, FFh
    return return_at(0x5D5A);
}
// 01:5D61: a step every eighth frame. The frame is the base plus the step
// counter modulo 4 (the counter goes up); the jump goes to the movement in
// that location.
template<class Z> Exit moving_step(Z& z) {
    z.decrement_indexed(0x5D61,obj);                                  // +14, the countdown
    if (!z.sign()) return return_taken_at(0x5D64);
    z.return_not_taken(0x5D64);
    z.store_immediate_indexed(0x5D65,obj);
    z.load_from_indexed(0x5D69,obj,z.r.a);                            // +13, the step counter
    z.increment_indexed(0x5D6C,obj);
    z.and_immediate(0x5D6F);
    z.add_from_indexed(0x5D71,obj);                                   // + the base frame
    z.store_to_indexed(0x5D74,obj,z.r.a);
    z.load_a_direct(0x5D77);                                          // C309, the location
    z.compare_immediate(0x5D7A);
    return k::relative_if_exit(0x5D7C,z.zero());
}
// 01:5D7E: elsewhere, up and left; at a fixed column the slot is freed.
template<class Z> Exit moving_up_left(Z& z) {
    z.decrement_indexed(0x5D7E,obj);                                  // X
    z.decrement_indexed(0x5D81,obj);                                  // Y
    z.load_from_indexed(0x5D84,obj,z.r.a);
    z.compare_immediate(0x5D87);
    if (!z.zero()) return return_taken_at(0x5D89);
    z.return_not_taken(0x5D89);
    z.store_immediate_indexed(0x5D8A,obj);                            // +0 = 0
    return return_at(0x5D8E);
}
// 01:5D8F: in that location, down; at a fixed row the slot is freed (the
// jump back to 01:5D8A, rendered here); every eighth row one pixel left.
template<class Z> Exit moving_down(Z& z) {
    z.increment_indexed(0x5D8F,obj);                                  // Y
    z.load_from_indexed(0x5D92,obj,z.r.a);
    z.compare_immediate(0x5D95);
    if (z.relative_if(0x5D97,z.zero())) {
        z.store_immediate_indexed(0x5D8A,obj);                        // +0 = 0
        return return_at(0x5D8E);
    }
    z.and_immediate(0x5D99);
    if (!z.zero()) return return_taken_at(0x5D9B);
    z.return_not_taken(0x5D9B);
    z.decrement_indexed(0x5D9C,obj);                                  // X
    return return_at(0x5D9F);
}
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address, with its unit, exits and permissions. A note
// names the call or jump the entry follows.
void register_scene_objects(RoutineRegistry& r) {
    auto F=[](std::uint16_t at, std::int8_t stack=0) { return k::core_final_at(at,stack); };
    auto R=[](std::uint16_t at, std::int8_t stack=0) { return k::returned_at(at,stack); };
    auto C=[](std::uint16_t at, std::int8_t stack=0) { return k::conditional_at(at,stack); };
    auto add=[&](RoutineEntry unit,std::uint16_t pc,std::vector<SegmentExit> exits,k::Permissions perms,auto logic) {
        k::add_segment(r,{1,pc},k::segment(unit,std::move(exits),perms),logic);
    };
    // The calls that open the handlers: the slot clear (set-up) and the call
    // of the per-frame body.
    register_handler_call(r,chest,0x5C63);
    register_handler_call(r,chest,0x5C99);
    register_handler_call(r,moving,0x5D30);
    register_handler_call(r,moving,0x5D5B);
    // The per-frame handlers' answer, after the body returns.
    add(chest,0x5C9C,{R(0x5C9E)},{},[](auto& z) -> Exit { return handler_answer(z,0x5C9C); });
    add(moving,0x5D5E,{R(0x5D60)},{},[](auto& z) -> Exit { return handler_answer(z,0x5D5E); });

    // ---- the chest ----
    add(chest,0x5C66,{F(0x5C79)},{.scratch_stack=2},[](auto& z) -> Exit { return chest_setup(z); });                     // after the slot clear
    add(chest,0x5C7C,{R(0x5C98)},{},[](auto& z) -> Exit { return chest_outcome(z); });                                   // after the random byte
    add(chest,0x5C9F,{R(0x5CA3),C(0x5CA8)},{},[](auto& z) -> Exit { return chest_phase(z); });                           // the body, called at 01:5C99
    add(chest,0x5CAA,{R(0x5CAD),R(0x5CBB),R(0x5CC4)},{},[](auto& z) -> Exit { return chest_opening(z); });               // the jr nz at 01:5CA8 was not taken
    add(chest,0x5CC5,{C(0x5CC9)},{},[](auto& z) -> Exit { return chest_reveal_test(z); });                               // the jr nz at 01:5CA8 was taken
    add(chest,0x5CCB,{R(0x5CCE),C(0x5CDF)},{},[](auto& z) -> Exit { return chest_reveal(z); });                          // the jr nz at 01:5CC9 was not taken
    add(chest,0x5CE1,{R(0x5CE5)},{},[](auto& z) -> Exit { return chest_empty(z); });                                     // the jr nz at 01:5CDF was not taken
    add(chest,0x5CE6,{R(0x5CF0)},{},[](auto& z) -> Exit { return chest_reveal_sound(z); });                              // the jr nz at 01:5CDF was taken
    add(chest,0x5CF1,{R(0x5CF4),C(0x5CFE)},{},[](auto& z) -> Exit { return chest_animation_step(z); });                  // the jr nz at 01:5CC9 was taken
    add(chest,0x5D00,{F(0x5D0A,-2)},{.scratch_stack=4},[](auto& z) -> Exit { return chest_first_outcome_step(z); });     // the jr nz at 01:5CFE was not taken
    add(chest,0x5D0D,{R(0x5D10,2),R(0x5D19,2)},{.inherited_stack=2},[](auto& z) -> Exit { return chest_first_outcome_end(z); });  // after the flash call
    add(chest,0x5D1A,{R(0x5D23),F(0x5D24)},{.scratch_stack=2},[](auto& z) -> Exit { return chest_second_outcome_step(z); });     // the jr nz at 01:5CFE was taken
    add(chest,0x5D27,{R(0x5D2F)},{},[](auto& z) -> Exit { return chest_second_outcome_end(z); });                        // after the flash

    // ---- the moving sprite ----
    add(moving,0x5D33,{R(0x5D5A)},{},[](auto& z) -> Exit { return moving_setup(z); });                                   // after the slot clear
    add(moving,0x5D61,{R(0x5D64),C(0x5D7C)},{},[](auto& z) -> Exit { return moving_step(z); });                          // the body, called at 01:5D5B
    add(moving,0x5D7E,{R(0x5D89),R(0x5D8E)},{},[](auto& z) -> Exit { return moving_up_left(z); });                       // the jr z at 01:5D7C was not taken
    add(moving,0x5D8F,{R(0x5D8E),R(0x5D9B),R(0x5D9F)},{},[](auto& z) -> Exit { return moving_down(z); });                // the jr z at 01:5D7C was taken
}
}
