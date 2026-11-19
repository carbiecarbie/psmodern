// SPDX-License-Identifier: GPL-3.0-or-later
// World map and towns (milestone 11), part objects:
// The object engine 01:576A, 01:5818 and 01:5853, and the map's object types
// (01:599F to 01:5A70) with their walking helpers 01:5A76, 01:5A98, 01:5B5C
// and 01:5B6B; the battle types are other registered segments behind the engine's dispatcher.
// Planned segments: map::object_segments in game/map.h, rendered with game/map_kit.h.
//
// Each segment is written once as a sequence of original instructions over a
// bus (the milestone 11 kit); its bound is that sequence run on the estimating
// bus. A segment ends where the routine calls, jumps or returns into code that
// another segment renders (the type dispatcher, the party step 01:7143), and
// that last instruction is left to the segment at its target.
#include "game/map.h"
#include "game/map_kit.h"
#include "game/routines.h"
#include <stdexcept>
#include <type_traits>

namespace psmodern::game {
namespace {
namespace mk=map_kit;
using mk::Exit;
using mk::Index;
inline constexpr Index iy=Index::iy;

// A loop that reads its counter back from the stack (the sort's outer loop
// pops BC) never ends when the stack lies in ROM, in the original as in this
// rendering. The estimate then gives up: it marks itself uncertain (the
// segment's maximum, above a frame, makes the segment refuse the state at its entry, which stops the run) and stops. The
// live side never meets such a state, since it runs only after the estimate;
// if it did, the guard stops the game with its message. The guard reads only
// the cycle count of the body, so a delivery inside the body does not change it.
template<class Z> bool runaway(Z& z) {
    return !z.guard(z.elapsed()<=mk::uncertain_maximum,"a registered map segment ran past two frames");
}
// The exit an abandoned estimate returns; it is never run (the estimate is uncertain).
inline Exit gave_up() { return kit::refused(); }

// ---- 01:576A: the object engine ----------------------------------------------------
// Eight slots of 20h bytes at C800h. Pass one calls each used slot's type
// handler through the dispatcher (the call at 01:579C, with the slot counter
// held on the stack) and lists the slots that answer non-zero as (key, slot)
// pairs from C289h. Pass two sorts the pairs: three passes of a triangular
// compare, so the sort is only partial. Pass three draws the listed slots'
// sprites with 01:5853, switching slot 2 to the sprite data's bank first.
template<class Z> Exit engine_sort_and_draw(Z& z);

// 01:57B1-57B7 and the loop head 01:5791: to the next slot, or past the last.
template<class Z> Exit engine_scan(Z& z, bool step_first) {
    auto& r=z.r;
    bool step=step_first;
    for (;;) {
        if (step) {
            z.load_de(0x57B1);
            z.add_to_index(0x57B4,iy,z.de());
            z.increment(0x57B6,r.c);
            if (!z.decrement_jump(0x57B7)) break;
        }
        step=true;
        z.load_from_indexed(0x5791,iy,r.a);
        z.and_immediate(0x5794);
        if (z.relative_if(0x5796,z.zero())) continue;
        z.push(0x5798,r.b,r.c);
        z.load_hl(0x5799);
        return z.final_call(0x579C);
    }
    return engine_sort_and_draw(z);
}
// 01:579F: a handler came back with its answer in A.
template<class Z> Exit engine_after_handler(Z& z) {
    auto& r=z.r;
    z.pop(0x579F,r.b,r.c);
    z.or_a(0x57A0);
    if (!z.jump_if(0x57A1,z.zero())) {
        z.load_hl_direct(0x57A4);
        z.load_from_indexed(0x57A7,iy,r.a);
        z.store_register_to_hl(0x57AA,r.a);
        z.increment_hl(0x57AB);
        z.store_register_to_hl(0x57AC,r.c);
        z.increment_hl(0x57AD);
        z.store_hl_direct(0x57AE);
    }
    return engine_scan(z,true);
}
// The loop of pass three from 01:57E2; `step` starts at the step 01:580E.
template<class Z> Exit engine_draw_loop(Z& z, bool step_first) {
    auto& r=z.r;
    bool step=step_first;
    for (;;) {
        if (step) {
            z.increment_hl(0x580E);
            z.increment_hl(0x580F);
            if (!z.decrement_jump(0x5810)) break;
        }
        step=true;
        z.load_from_hl(0x57E2,r.a);
        z.compare_immediate(0x57E3);
        if (z.relative_if(0x57E5,z.zero())) continue;
        z.push(0x57E7,r.b,r.c);
        z.push(0x57E8,r.h,r.l);
        z.copy(0x57E9,r.l,r.a);
        z.load_immediate(0x57EA,r.h);
        for (std::uint16_t at=0x57EC;at<=0x57F0;++at) z.add_to_hl(at,z.hl());
        z.load_de(0x57F1);
        z.add_to_hl(0x57F4,z.de());
        z.push(0x57F5,r.h,r.l);
        z.pop_index(0x57F6,iy);
        z.compare_immediate(0x57F8);
        z.load_immediate(0x57FA,r.a);
        z.load_bc(0x57FC);
        if (!z.relative_if(0x57FF,z.carry())) {
            z.load_immediate(0x5801,r.a);
            z.load_bc(0x5803);
        }
        z.store_a_direct(0x5806);
        return z.final_call(0x5809);
    }
    z.load_hl_direct(0x5812);
    z.store_to_hl(0x5815);
    return z.final_return(0x5817);
}
template<class Z> Exit engine_sort_and_draw(Z& z) {
    auto& r=z.r;
    z.load_de(0x57B9);
    z.load_immediate(0x57BC,r.b);
    do {
        if (runaway(z)) return gave_up();
        z.push(0x57BE,r.b,r.c);
        z.copy(0x57BF,r.l,r.e);
        z.copy(0x57C0,r.h,r.d);
        z.increment_hl(0x57C1);
        z.increment_hl(0x57C2);
        do {
            if (runaway(z)) return gave_up();
            z.load_a_from_pair(0x57C3,z.de());
            z.compare_from_hl(0x57C4);
            if (!z.relative_if(0x57C5,!z.carry())) {
                z.copy(0x57C7,r.c,r.a);
                z.load_from_hl(0x57C8,r.a);
                z.store_register_to_hl(0x57C9,r.c);
                z.store_a_to_pair(0x57CA,z.de());
                z.increment_hl(0x57CB);
                z.increment_pair(0x57CC,r.d,r.e);
                z.load_a_from_pair(0x57CD,z.de());
                z.copy(0x57CE,r.c,r.a);
                z.load_from_hl(0x57CF,r.a);
                z.store_register_to_hl(0x57D0,r.c);
                z.store_a_to_pair(0x57D1,z.de());
                z.decrement_hl(0x57D2);
                z.decrement_pair(0x57D3,r.d,r.e);
            }
            z.increment_hl(0x57D4);
            z.increment_hl(0x57D5);
        } while (z.decrement_jump(0x57D6));
        z.increment_pair(0x57D8,r.d,r.e);
        z.increment_pair(0x57D9,r.d,r.e);
        z.pop(0x57DA,r.b,r.c);
    } while (z.decrement_jump(0x57DB));
    z.load_hl(0x57DD);
    z.load_immediate(0x57E0,r.b);
    return engine_draw_loop(z,false);
}
// The prologue is cut after the block copy, at the plain instruction 01:577E
// (the frame boundary falls near this call's start in some recordings, so a
// chunk stays short); 01:5781 resumes with the sprite table pointers.
template<class Z> Exit engine_start(Z& z) {                    // 01:576A-577D
    z.load_hl(0x576A);
    z.store_hl_direct(0x576D);
    z.load_de(0x5770);
    z.load_bc(0x5773);
    z.store_to_hl(0x5776);
    z.increment_hl(0x5778);
    z.store_to_hl(0x5779);
    z.decrement_hl(0x577B);
    z.block_copy(0x577C);
    z.load_hl(0x577E);
    return mk::fall_through(0x577E,0x5781);
}
template<class Z> Exit engine_second(Z& z) {                   // 01:5781-578E
    z.store_hl_direct(0x5781);
    z.load_hl(0x5784);
    z.store_hl_direct(0x5787);
    z.load_index(0x578A,iy);
    z.load_bc(0x578E);
    return engine_scan(z,false);
}
template<class Z> Exit engine_after_draw(Z& z) {              // 01:580C: after 01:5853 returned
    auto& r=z.r;
    z.pop(0x580C,r.h,r.l);
    z.pop(0x580D,r.b,r.c);
    return engine_draw_loop(z,true);
}

// ---- 01:5818: clear a slot -----------------------------------------------------------
// HL = the slot + 1, the 1Eh bytes after it are cleared by a copy of the zero.
template<class Z> Exit clear_slot(Z& z) {
    auto& r=z.r;
    z.push_index(0x5818,iy);
    z.pop(0x581A,r.h,r.l);
    z.increment_hl(0x581B);
    z.copy(0x581C,r.e,r.l);
    z.copy(0x581D,r.d,r.h);
    z.increment_pair(0x581E,r.d,r.e);
    z.xor_a(0x581F);
    z.store_register_to_hl(0x5820,r.a);
    z.load_bc(0x5821);
    z.block_copy(0x5824);
    return z.final_return(0x5826);
}

// ---- 01:5853: the slot's sprites ---------------------------------------------------------
// The slot's sprite id picks a list from the table at BC; its first byte is the
// count, then one Y offset per sprite and one (X offset, tile) pair per sprite
// are added to the slot's Y and X and appended to the two sprite tables.
// The two copy loops are cut at their first instruction (the loads at 01:5868
// and 01:587B) once 128 cycles have passed: the sprite lists of some objects
// are long (several thousand cycles) and would otherwise cross the frame
// boundary. 01:5869 and 01:587C resume after the load.
constexpr std::uint16_t draw_chunk=128;
template<class Z> Exit draw_second_loop(Z& z, bool resumed) {
    auto& r=z.r;
    for (;;) {
        if (!resumed) {
            const bool chunk_end=z.loop_exit(draw_chunk);
            z.load_from_hl(0x587B,r.a);
            if (chunk_end) return mk::fall_through(0x587B,0x587C);
        }
        resumed=false;
        z.add_register(0x587C,r.c);
        z.store_a_to_pair(0x587D,z.de());
        z.increment_pair(0x587E,r.d,r.e);
        z.increment_hl(0x587F);
        z.load_from_hl(0x5880,r.a);
        z.store_a_to_pair(0x5881,z.de());
        z.increment_hl(0x5882);
        z.increment_pair(0x5883,r.d,r.e);
        if (!z.decrement_jump(0x5884)) break;
    }
    z.store_pair_direct(0x5886,r.d,r.e);
    return z.final_return(0x588A);
}
template<class Z> Exit draw_first_loop(Z& z, bool resumed) {
    auto& r=z.r;
    for (;;) {
        if (!resumed) {
            const bool chunk_end=z.loop_exit(draw_chunk);
            z.load_from_hl(0x5868,r.a);
            if (chunk_end) return mk::fall_through(0x5868,0x5869);
        }
        resumed=false;
        z.add_register(0x5869,r.c);
        z.store_a_to_pair(0x586A,z.de());
        z.increment_pair(0x586B,r.d,r.e);
        z.increment_hl(0x586C);
        if (!z.decrement_jump(0x586D)) break;
    }
    z.store_pair_direct(0x586F,r.d,r.e);
    z.pop(0x5873,r.b,r.c);
    z.load_pair_direct(0x5874,r.d,r.e);
    z.load_from_indexed(0x5878,iy,r.c);
    return draw_second_loop(z,false);
}
template<class Z> Exit draw_slot(Z& z) {
    auto& r=z.r;
    z.load_from_indexed(0x5853,iy,r.l);
    z.load_immediate(0x5856,r.h);
    z.add_to_hl(0x5858,z.hl());
    z.add_to_hl(0x5859,z.bc());
    z.load_from_hl(0x585A,r.a);
    z.increment_hl(0x585B);
    z.load_from_hl(0x585C,r.h);
    z.copy(0x585D,r.l,r.a);
    z.load_from_hl(0x585E,r.b);
    z.push(0x585F,r.b,r.c);
    z.increment_hl(0x5860);
    z.load_pair_direct(0x5861,r.d,r.e);
    z.load_from_indexed(0x5865,iy,r.c);
    return draw_first_loop(z,false);
}

// ---- 01:599F-5A12: the map's object types ---------------------------------------------------
// The four spawn types (599F, 59E0, 59F1, 5A02) share 01:59A1: B = the type's
// sprite id; the slot is cleared with 01:5818, marked used and given its
// starting fields. Their walking twins (59D4, 59E5, 59F6, 5A07) only load the
// two direction tables and call the walker 01:5A98.
template<class Z> Exit spawn_head(Z& z, std::uint16_t at, bool with_jump) {
    auto& r=z.r;
    if (at) {
        z.load_immediate(at,r.b);
        if (with_jump) (void)z.jump(static_cast<std::uint16_t>(at+2));
    }
    z.push(0x59A1,r.b,r.c);
    return z.final_call(0x59A2);
}
template<class Z> Exit spawn_tail(Z& z) {                        // 01:59A5-59D3
    auto& r=z.r;
    z.increment_indexed(0x59A5,iy);
    z.pop(0x59A8,r.b,r.c);
    z.store_immediate_indexed(0x59A9,iy);
    z.load_a_direct(0x59AD);
    z.and_immediate(0x59B0);
    z.load_immediate(0x59B2,r.a);
    if (!z.relative_if(0x59B4,!z.zero())) z.load_immediate(0x59B6,r.a);
    z.store_to_indexed(0x59B8,iy,r.a);
    z.store_immediate_indexed(0x59BB,iy);
    z.store_to_indexed(0x59BF,iy,r.b);
    z.store_immediate_indexed(0x59C2,iy);
    z.copy(0x59C6,r.a,r.c);
    z.or_a(0x59C7);
    z.load_immediate(0x59C8,r.a);
    if (!z.relative_if(0x59CA,!z.zero())) z.load_immediate(0x59CC,r.a);
    z.store_to_indexed(0x59CE,iy,r.a);
    z.load_immediate(0x59D1,r.a);
    return z.final_return(0x59D3);
}
// A walking type: DE and HL = its two tables, then the call of 01:5A98 at `call_at`.
template<class Z> Exit walk_start(Z& z, std::uint16_t at) {
    z.load_de(at);
    z.load_hl(static_cast<std::uint16_t>(at+3));
    return z.final_call(static_cast<std::uint16_t>(at+6));
}
template<class Z> Exit walk_return(Z& z, std::uint16_t at) {     // ld a,FFh; ret
    z.load_immediate(at,z.r.a);
    return z.final_return(static_cast<std::uint16_t>(at+2));
}

// ---- 01:5A4F and 01:5A70: the party's vehicle on the map -------------------------------------
template<class Z> Exit vehicle_start(Z& z) {                    // 01:5A52-5A6F, after 01:5818
    auto& r=z.r;
    z.increment_indexed(0x5A52,iy);
    z.store_immediate_indexed(0x5A55,iy);
    z.store_immediate_indexed(0x5A59,iy);
    z.store_immediate_indexed(0x5A5D,iy);
    z.load_a_direct(0x5A61);
    z.subtract_immediate(0x5A64);
    z.store_to_indexed(0x5A66,iy,r.a);
    z.store_immediate_indexed(0x5A69,iy);
    z.load_immediate(0x5A6D,r.a);
    return z.final_return(0x5A6F);
}
template<class Z> Exit vehicle_answer(Z& z) {                   // 01:5A73: ld a,FFh; ret
    z.load_immediate(0x5A73,z.r.a);
    return z.final_return(0x5A75);
}
// 01:5A79, after the party's step 01:7143: with a direction held, the lowest
// set bit of the move bits C264h picks the vehicle's frame from a table of four.
template<class Z> Exit vehicle_step(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x5A79);
    z.and_immediate(0x5A7C);
    if (auto exit=z.final_return_if(0x5A7E,mk::Condition::z)) return *exit;
    z.load_immediate(0x5A7F,r.c);
    do {
        z.rotate_right_circular_a(0x5A81);
        z.increment(0x5A82,r.c);
    } while (z.jump_if(0x5A83,!z.carry()));
    z.load_hl(0x5A86);
    z.load_immediate(0x5A89,r.b);
    z.add_to_hl(0x5A8B,z.bc());
    z.load_a_direct(0x5A8C);
    z.add_from_hl(0x5A8F);
    z.store_to_indexed(0x5A90,iy,r.a);
    return z.final_return(0x5A93);
}

// ---- 01:5A98: the walker ----------------------------------------------------------------
// C = 0 for the party's leader slot (it follows the controller through the step
// routine 01:7143), else a follower. DE = the type's direction table, HL = its
// frame table.
template<class Z> Exit walker_frame(Z& z);                       // 01:5B30
template<class Z> Exit walker_direction(Z& z) {                  // 01:5B04-5B2F
    auto& r=z.r;
    z.load_from_indexed(0x5B04,iy,r.a);
    z.store_to_indexed(0x5B07,iy,r.a);
    z.copy(0x5B0A,r.a,r.c);
    z.or_a(0x5B0B);
    bool to_5B1E=z.relative_if(0x5B0C,!z.zero());
    if (!to_5B1E) {
        z.load_a_direct(0x5B0E);
        z.and_immediate(0x5B11);
        to_5B1E=z.relative_if(0x5B13,z.zero());
    }
    if (!to_5B1E) {
        z.load_immediate(0x5B15,r.l);
        do {
            z.rotate_right_circular_a(0x5B17);
            z.increment(0x5B18,r.l);
        } while (z.jump_if(0x5B19,!z.carry()));
        z.relative(0x5B1C);
    } else {
        z.copy(0x5B1E,r.a,r.c);
        z.or_a(0x5B1F);
        z.load_from_indexed(0x5B20,iy,r.l);
        if (!z.relative_if(0x5B23,z.zero())) z.load_from_indexed(0x5B25,iy,r.l);
    }
    z.load_immediate(0x5B28,r.h);
    z.add_to_hl(0x5B2A,z.de());
    z.load_from_hl(0x5B2B,r.a);
    z.store_to_indexed(0x5B2C,iy,r.a);
    return z.final_return(0x5B2F);
}
template<class Z> Exit walker_idle(Z& z) {                       // 01:5AFC
    z.load_a_direct(0x5AFC);
    z.and_immediate(0x5AFF);
    if (z.jump_if(0x5B01,!z.zero())) return walker_frame(z);
    return walker_direction(z);
}
template<class Z> Exit walker_follow(Z& z) {                     // 01:5AC9
    auto& r=z.r;
    z.test_bit_indexed(0x5AC9,iy,0);
    if (!z.relative_if(0x5ACD,z.zero())) z.set_bit_indexed(0x5ACF,iy,1);
    z.load_from_indexed(0x5AD3,iy,r.a);
    z.compare_indexed(0x5AD6,iy);
    bool to_5AE3=z.relative_if(0x5AD9,!z.zero());
    bool to_5AED=false;
    if (!to_5AE3) {
        z.load_from_indexed(0x5ADB,iy,r.a);
        z.compare_indexed(0x5ADE,iy);
        to_5AED=z.relative_if(0x5AE1,z.zero());
    }
    if (!to_5AED) {
        z.test_bit_indexed(0x5AE3,iy,1);
        if (!z.relative_if(0x5AE7,z.zero())) z.set_bit_indexed(0x5AE9,iy,0);
    }
    z.load_from_indexed(0x5AED,iy,r.a);
    z.store_to_indexed(0x5AF0,iy,r.a);
    z.load_from_indexed(0x5AF3,iy,r.a);
    z.store_to_indexed(0x5AF6,iy,r.a);
    (void)z.jump(0x5AF9);
    return walker_frame(z);
}
template<class Z> Exit walker_head(Z& z) {                       // 01:5A9D
    auto& r=z.r;
    z.load_a_direct(0x5A9D);
    z.or_a(0x5AA0);
    if (z.jump_if(0x5AA1,z.zero())) return walker_idle(z);
    z.compare_immediate(0x5AA4);
    if (z.jump_if(0x5AA6,!z.zero())) return walker_frame(z);
    z.copy(0x5AA9,r.a,r.c);
    z.or_a(0x5AAA);
    if (z.jump_if(0x5AAB,!z.zero())) return walker_follow(z);
    z.load_from_indexed(0x5AAE,iy,r.a);
    z.store_to_indexed(0x5AB1,iy,r.a);
    z.load_a_direct(0x5AB4);
    z.and_immediate(0x5AB7);
    if (z.jump_if(0x5AB9,z.zero())) return walker_idle(z);
    z.load_immediate(0x5ABC,r.b);
    do {
        z.rotate_right_circular_a(0x5ABE);
        z.increment(0x5ABF,r.b);
    } while (z.jump_if(0x5AC0,!z.carry()));
    z.store_to_indexed(0x5AC3,iy,r.b);
    (void)z.jump(0x5AC6);
    return walker_frame(z);
}
template<class Z> Exit walker_frame(Z& z) {                       // 01:5B30: a follower first calls 01:5B5C
    auto& r=z.r;
    z.copy(0x5B30,r.a,r.c);
    z.or_a(0x5B31);
    return z.final_call_if(0x5B32,mk::Condition::nz);
}
template<class Z> Exit walker_animate(Z& z) {                    // 01:5B35: the walking animation
    auto& r=z.r;
    z.load_a_direct(0x5B35);
    z.or_a(0x5B38);
    if (z.relative_if(0x5B39,!z.zero())) return walker_direction(z);
    z.decrement_indexed(0x5B3B,iy);
    if (auto exit=z.final_return_if(0x5B3E,mk::Condition::p)) return *exit;
    z.store_immediate_indexed(0x5B3F,iy);
    z.load_from_indexed(0x5B43,iy,r.a);
    z.add_register(0x5B46,r.a);
    z.add_register(0x5B47,r.a);
    z.copy(0x5B48,r.b,r.a);
    z.load_from_indexed(0x5B49,iy,r.a);
    z.increment(0x5B4C,r.a);
    z.and_immediate(0x5B4D);
    z.store_to_indexed(0x5B4F,iy,r.a);
    z.add_register(0x5B52,r.b);
    z.copy(0x5B53,r.e,r.a);
    z.load_immediate(0x5B54,r.d);
    z.add_to_hl(0x5B56,z.de());
    z.load_from_hl(0x5B57,r.a);
    z.store_to_indexed(0x5B58,iy,r.a);
    return z.final_return(0x5B5B);
}

// ---- 01:5B5C and 01:5B6B: the follower's nudge -------------------------------------------------
template<class Z> Exit nudge_move(Z& z) {                        // 01:5B6B-5B86 (A = a direction)
    auto& r=z.r;
    z.compare_immediate(0x5B6B);
    if (!z.jump_if(0x5B6D,!z.carry())) {
        z.or_a(0x5B70);
        if (!z.relative_if(0x5B71,!z.zero())) z.decrement(0x5B73,r.a);
        z.add_from_indexed(0x5B74,iy);
        z.store_to_indexed(0x5B77,iy,r.a);
        return z.final_return(0x5B7A);
    }
    z.subtract_immediate(0x5B7B);
    if (!z.relative_if(0x5B7D,!z.zero())) z.decrement(0x5B7F,r.a);
    z.add_from_indexed(0x5B80,iy);
    z.store_to_indexed(0x5B83,iy,r.a);
    return z.final_return(0x5B86);
}
template<class Z> Exit follow_start(Z& z) {                      // 01:5B5C
    auto& r=z.r;
    z.test_bit_indexed(0x5B5C,iy,0);
    z.load_from_indexed(0x5B60,iy,r.a);
    return z.final_call_if(0x5B63,mk::Condition::nz);
}
template<class Z> Exit follow_resume(Z& z) {                     // 01:5B66
    z.load_a_direct(0x5B66);
    z.xor_immediate(0x5B69);
    return nudge_move(z);
}
}

void register_map_objects(RoutineRegistry& registry) {
    using mk::Permissions;
    const auto unit=[](std::uint16_t address) { return map::at1(address); };
    const auto add=[&](std::uint16_t entry, std::uint16_t of_unit, std::vector<SegmentExit> exits, Permissions permissions, auto logic) {
        mk::add_segment(registry,map::at1(entry),mk::segment(unit(of_unit),std::move(exits),permissions),logic);
    };
    using mk::core_final_at; using mk::returned_at;
    // The engine
    add(0x576A,0x576A,{core_final_at(0x577E)},{},[](auto& z) { return engine_start(z); });
    add(0x5781,0x576A,{core_final_at(0x579C,-2),core_final_at(0x5809,-4),returned_at(0x5817)},
        {.slot2_mapper=true,.scratch_stack=6},[](auto& z) { return engine_second(z); });
    add(0x579F,0x576A,{core_final_at(0x579C),core_final_at(0x5809,-2),returned_at(0x5817,+2)},
        {.slot2_mapper=true,.scratch_stack=4,.inherited_stack=2},[](auto& z) { return engine_after_handler(z); });
    add(0x580C,0x576A,{core_final_at(0x5809),returned_at(0x5817,+4)},
        {.slot2_mapper=true,.scratch_stack=2,.inherited_stack=4},[](auto& z) { return engine_after_draw(z); });
    add(0x5818,0x5818,{returned_at(0x5826)},{.scratch_stack=2},[](auto& z) { return clear_slot(z); });
    add(0x5853,0x5853,{mk::loop_at(0x5868,draw_chunk,-2),mk::loop_at(0x587B,draw_chunk),returned_at(0x588A)},{.scratch_stack=2},
        [](auto& z) { return draw_slot(z); });
    add(0x5869,0x5853,{mk::loop_at(0x5868,draw_chunk),mk::loop_at(0x587B,draw_chunk,+2),returned_at(0x588A,+2)},{.inherited_stack=2},
        [](auto& z) { return draw_first_loop(z,true); });
    add(0x587C,0x5853,{mk::loop_at(0x587B,draw_chunk),returned_at(0x588A)},{},[](auto& z) { return draw_second_loop(z,true); });
    // The spawn types and their walking twins
    add(0x599F,0x599F,{core_final_at(0x59A2,-2)},{.scratch_stack=2},[](auto& z) { return spawn_head(z,0x599F,false); });
    add(0x59A5,0x599F,{returned_at(0x59D3,+2)},{.inherited_stack=2},[](auto& z) { return spawn_tail(z); });
    for (const std::uint16_t at:{0x59E0,0x59F1,0x5A02})
        add(at,at,{core_final_at(0x59A2,-2)},{.scratch_stack=2},[at](auto& z) { return spawn_head(z,at,true); });
    for (const std::uint16_t at:{0x59D4,0x59E5,0x59F6,0x5A07}) {
        add(at,at,{core_final_at(static_cast<std::uint16_t>(at+6))},{},[at](auto& z) { return walk_start(z,at); });
        const auto back=static_cast<std::uint16_t>(at+9);
        add(back,at,{returned_at(static_cast<std::uint16_t>(back+2))},{},[back](auto& z) { return walk_return(z,back); });
    }
    // The vehicle
    // The vehicle types' first instructions: the call that clears the slot, the
    // call into the vehicle step, and the party step it makes.
    for (const std::uint16_t entry : {0x5A4F,0x5A70,0x5A76}) {
        auto spec=mk::segment(unit(entry),{core_final_at(entry)},{.scratch_stack=2});
        spec.single_instruction=true;
        mk::add_segment(registry,map::at1(entry),std::move(spec),[entry](auto& z) -> Exit { return z.final_call(entry); });
    }
    add(0x5A52,0x5A4F,{returned_at(0x5A6F)},{},[](auto& z) { return vehicle_start(z); });
    add(0x5A73,0x5A70,{returned_at(0x5A75)},{},[](auto& z) { return vehicle_answer(z); });
    add(0x5A79,0x5A76,{returned_at(0x5A7E),returned_at(0x5A93)},{},[](auto& z) { return vehicle_step(z); });
    // The walker and the follower
    add(0x5A98,0x5A98,{core_final_at(0x5A9A)},{},[](auto& z) {
        z.copy(0x5A98,z.r.a,z.r.c); z.or_a(0x5A99);
        return z.final_call_if(0x5A9A,mk::Condition::z); });
    add(0x5A9D,0x5A98,{returned_at(0x5B2F),core_final_at(0x5B32),returned_at(0x5B3E),returned_at(0x5B5B)},{},
        [](auto& z) { return walker_head(z); });
    add(0x5B35,0x5A98,{returned_at(0x5B2F),returned_at(0x5B3E),returned_at(0x5B5B)},{},[](auto& z) { return walker_animate(z); });
    add(0x5B5C,0x5B5C,{core_final_at(0x5B63)},{},[](auto& z) { return follow_start(z); });
    add(0x5B66,0x5B5C,{returned_at(0x5B7A),returned_at(0x5B86)},{},[](auto& z) { return follow_resume(z); });
    add(0x5B6B,0x5B6B,{returned_at(0x5B7A),returned_at(0x5B86)},{},[](auto& z) { return nudge_move(z); });
}
}
