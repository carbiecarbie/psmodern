// SPDX-License-Identifier: GPL-3.0-or-later
// Menus and windows (milestone 12), part select:
// Choosing: the cursor loop 00:2D51 (its cancel point 00:2D93 is observed by
// the host), the paged item list 00:2DA5, yes or no 00:2D19, the waits 00:2D25
// to 00:2D49, the cursor blink 00:2E62 and the frame-interrupt handler bodies
// 00:015F, 00:01AD and 00:01D1.
// Planned segments: menu::select_segments in game/menu.h, rendered with
// game/menu_kit.h.
//
// What runs here is the polling between the frame waits. Every wait itself
// (the bare call of 00:0052) ends a segment, so each segment
// is the few instructions between two waits: read the pressed buttons, move
// the choice or the page, test the confirm buttons, and loop back to the next
// wait. The frame interrupt handlers (the three bodies) do the same on the
// other side: they write the scroll registers and call the shared uploads.
// The cancel point of the cursor loop (00:2D93, one instruction the host
// watches) is an observed entry. The host can release the cancel button
// before either the rewritten leave path or its original instruction runs.
#include "game/menu.h"
#include "game/menu_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace mk=game::menu_kit;
using mk::Exit;
using mk::call_cycles;
using mk::return_cycles;
using mk::taken_conditional_return_cycles;
using mk::core_final_at;
using mk::returned_at;
using mk::conditional_at;
namespace menu_ns=game::menu;

// ---- Frame interrupt handlers (00:015F, 00:01AD, 00:01D1) -------------------------------------
// The scroll registers: VDP register 8 takes the horizontal scroll (C300h),
// register 9 the vertical one (C304h); then the sprite table upload is called
// (the call is the final instruction, 18 bytes after the start).
template<class Z> Exit scroll_registers(Z& z, std::uint16_t at) {
    map_kit::write_scroll_registers(z,at);
    return z.final_call(static_cast<std::uint16_t>(at+18));
}
// 00:017A: after the fade steps, the palette (C220h, 32 bytes) goes to colour
// memory; the call of the unrolled upload is the final instruction.
template<class Z> Exit fade_palette(Z& z) {
    auto& r=z.r;
    z.load_hl(0x017A);
    z.load_de(0x017D);
    (void)z.restart_vdp_address(0x0180);
    z.load_immediate(0x0181,r.c);
    return z.final_call(0x0183);
}

// ---- Yes or no (00:2D19) ----------------------------------------------------------------------
template<class Z> Exit yes_no_open(Z& z) {                          // push bc, then the window call
    z.push(0x2D19,z.r.b,z.r.c);
    return z.final_call(0x2D1A);
}
template<class Z> Exit yes_no_between(Z& z) {                       // push af holds the answer across the close call
    z.push(0x2D1D,z.r.a,z.r.f);
    return z.final_call(0x2D1E);
}
template<class Z> Exit yes_no_close(Z& z) {                         // the answer comes back with Z set for "no"
    auto& r=z.r;
    z.pop(0x2D21,r.a,r.f);
    z.pop(0x2D22,r.b,r.c);
    z.or_a(0x2D23);
    return z.final_return(0x2D24);
}

// ---- The waits (00:2D25-00:2D50) --------------------------------------------------------------
// Wait for button 1 or 2: wait a frame, look at the pressed buttons.
template<class Z> Exit button_wait(Z& z, bool poll_first) {
    auto& r=z.r;
    if (!poll_first) {
        z.load_immediate(0x2D25,r.a);
        return z.final_call(0x2D27);
    }
    z.load_a_direct(0x2D2A);
    z.and_immediate(0x2D2D);
    if (z.jump_if(0x2D2F,z.zero())) {
        z.load_immediate(0x2D25,r.a);
        return z.final_call(0x2D27);
    }
    return z.final_return(0x2D32);
}
// 00:2D33 (B = 1Eh) and 00:2D37 (B = B4h) wait that many frames for a button;
// 00:2D3E is the poll after each frame wait.
template<class Z> Exit timed_wait_short(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x2D33,r.b);
    z.relative(0x2D35);
    z.load_immediate(0x2D39,r.a);
    return z.final_call(0x2D3B);
}
template<class Z> Exit timed_wait_long(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x2D37,r.b);
    z.load_immediate(0x2D39,r.a);
    return z.final_call(0x2D3B);
}
template<class Z> Exit timed_wait_poll(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x2D3E);
    z.and_immediate(0x2D41);
    if (auto exit=z.final_return_if(0x2D43,mk::Condition::nz)) return *exit;
    if (!z.decrement_jump(0x2D44)) return z.final_return(0x2D46);
    z.load_immediate(0x2D39,r.a);
    return z.final_call(0x2D3B);
}
// 00:2D47 (B = D0h) falls into 00:2D49: wait B frames, no button test.
template<class Z> Exit fixed_wait(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x2D47,r.b);
    z.load_immediate(0x2D49,r.a);
    return z.final_call(0x2D4B);
}
template<class Z> Exit frames_wait(Z& z) {
    z.load_immediate(0x2D49,z.r.a);
    return z.final_call(0x2D4B);
}
template<class Z> Exit frames_wait_next(Z& z) {
    if (!z.decrement_jump(0x2D4E)) return z.final_return(0x2D50);
    z.load_immediate(0x2D49,z.r.a);
    return z.final_call(0x2D4B);
}

// ---- The cursor loop (00:2D51) ------------------------------------------------------------------
// Start: the selection becomes active (C268h = FFh), the choice 0 (C26Bh and
// C26Ch), the blink counter 0; then a frame wait. 00:2D60 is the wait the loop
// returns to.
template<class Z> Exit selection_start(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x2D51,r.a);
    z.store_a_direct(0x2D53);
    z.load_hl(0x2D56);
    z.store_hl_direct(0x2D59);
    z.xor_a(0x2D5C);
    z.store_a_direct(0x2D5D);
    z.load_immediate(0x2D60,r.a);
    return z.final_call(0x2D62);
}
template<class Z> Exit selection_wait(Z& z) {
    z.load_immediate(0x2D60,z.r.a);
    return z.final_call(0x2D62);
}
// After each frame wait: Up and Down move the choice (wrapping), then button 1
// or 2 leaves the loop (the jump at 00:2D90 is the segment's conditional exit;
// the cancel point 00:2D93 behind it is the next segment's entry).
template<class Z> Exit selection_poll(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x2D65);
    z.and_immediate(0x2D68);
    if (!z.jump_if(0x2D6A,z.zero())) {
        z.copy(0x2D6D,r.c,r.a);
        z.load_hl(0x2D6E);
        z.load_a_direct(0x2D71);
        z.test_bit(0x2D74,0,r.c);
        if (!z.relative_if(0x2D76,z.zero())) {
            z.subtract_immediate(0x2D78);
            if (!z.relative_if(0x2D7A,!z.carry())) z.load_from_hl(0x2D7C,r.a);
        }
        z.test_bit(0x2D7D,1,r.c);
        if (!z.relative_if(0x2D7F,z.zero())) {
            z.increment(0x2D81,r.a);
            z.compare_from_hl(0x2D82);
            if (!z.relative_if(0x2D83,z.carry()))
                if (!z.relative_if(0x2D85,z.zero())) z.xor_a(0x2D87);
        }
        z.store_a_direct(0x2D88);
    }
    z.load_a_direct(0x2D8B);
    z.and_immediate(0x2D8E);
    return z.final_jump_if(0x2D90,mk::Condition::z);
}
// After the cancel point: the blink counter is cleared, one more frame wait.
template<class Z> Exit selection_leave(Z& z) {
    auto& r=z.r;
    z.xor_a(0x2D94);
    z.store_a_direct(0x2D95);
    z.load_immediate(0x2D98,r.a);
    return z.final_call(0x2D9A);
}
// After that wait: the selection is no longer active; the choice is returned.
template<class Z> Exit selection_end(Z& z) {
    z.xor_a(0x2D9D);
    z.store_a_direct(0x2D9E);
    z.load_a_direct(0x2DA1);
    return z.final_return(0x2DA4);
}

// ---- The paged item list (00:2DA5) ---------------------------------------------------------------
// The wait at 00:2DAA/2DAC, the poll after it, the leave, the page change and
// the recount, as in the original's flow (see menus.md).
template<class Z> Exit item_wait(Z& z) {                            // ld a,8 ; call WaitForVInt
    z.load_immediate(0x2DAA,z.r.a);
    return z.final_call(0x2DAC);
}
template<class Z> Exit item_start(Z& z) {                           // 00:2DA5 and the jump back to it
    auto& r=z.r;
    z.load_immediate(0x2DA5,r.a);
    z.store_a_direct(0x2DA7);
    return item_wait(z);
}
// The page redraw: the selection is paused and the list redrawn (00:352D).
template<class Z> Exit item_redraw(Z& z) {
    z.xor_a(0x2E30);
    z.store_a_direct(0x2E31);
    return z.final_call(0x2E34);
}
// 00:2DFE: no page key; button 1 or 2 ends the selection, else wait again.
template<class Z> Exit item_confirm(Z& z) {
    auto& r=z.r;
    z.copy(0x2DFE,r.a,r.c);
    z.and_immediate(0x2DFF);
    if (z.relative_if(0x2E01,z.zero())) return item_wait(z);
    z.xor_a(0x2E03);
    z.store_a_direct(0x2E04);
    z.load_immediate(0x2E07,r.a);
    return z.final_call(0x2E09);
}
template<class Z> Exit item_poll(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x2DAF);
    z.copy(0x2DB2,r.c,r.a);
    z.and_immediate(0x2DB3);
    if (!z.jump_if(0x2DB5,z.zero())) {                              // Up or Down: move the choice
        z.copy(0x2DB8,r.c,r.a);
        z.load_hl(0x2DB9);
        z.load_a_direct(0x2DBC);
        z.test_bit(0x2DBF,0,r.c);
        if (!z.relative_if(0x2DC1,z.zero())) {
            z.subtract_immediate(0x2DC3);
            if (!z.relative_if(0x2DC5,!z.carry())) z.load_from_hl(0x2DC7,r.a);
        }
        z.test_bit(0x2DC8,1,r.c);
        if (!z.relative_if(0x2DCA,z.zero())) {
            z.increment(0x2DCC,r.a);
            z.compare_from_hl(0x2DCD);
            if (!z.relative_if(0x2DCE,z.carry()))
                if (!z.relative_if(0x2DD0,z.zero())) z.xor_a(0x2DD2);
        }
        z.store_a_direct(0x2DD3);
        z.jump(0x2DD6);
        return item_confirm(z);
    }
    z.load_hl_direct(0x2DD9);                                       // Left or Right: change the page
    z.copy(0x2DDC,r.a,r.h);
    z.or_a(0x2DDD);
    if (z.relative_if(0x2DDE,z.zero())) return item_confirm(z);
    z.test_bit(0x2DE0,2,r.c);
    if (!z.relative_if(0x2DE2,z.zero())) {
        z.copy(0x2DE4,r.a,r.l);
        z.subtract_immediate(0x2DE5);
        if (!z.relative_if(0x2DE7,!z.carry())) {
            z.copy(0x2DE9,r.a,r.h);
            z.relative(0x2DEA);
        }
    } else {
        z.test_bit(0x2DEC,3,r.c);
        if (z.relative_if(0x2DEE,z.zero())) return item_confirm(z);
        z.copy(0x2DF0,r.a,r.l);
        z.add_immediate(0x2DF1);
        z.compare_register(0x2DF3,r.h);
        if (!z.relative_if(0x2DF4,z.carry()))
            if (!z.relative_if(0x2DF6,z.zero())) z.xor_a(0x2DF8);
    }
    z.store_a_direct(0x2DF9);
    z.relative(0x2DFC);
    return item_redraw(z);
}
// After the wait that follows a confirm: the result (the choice, or the item
// index of the page) is returned, or the page wraps first.
template<class Z> Exit item_finish(Z& z) {
    auto& r=z.r;
    z.xor_a(0x2E0C);
    z.store_a_direct(0x2E0D);
    z.test_bit(0x2E10,4,r.c);
    if (auto exit=z.final_return_if(0x2E12,mk::Condition::nz)) return *exit;
    z.load_a_direct(0x2E13);
    z.compare_immediate(0x2E16);
    z.load_a_direct(0x2E18);
    if (auto exit=z.final_return_if(0x2E1B,mk::Condition::c)) return *exit;
    z.or_a(0x2E1C);
    if (!z.relative_if(0x2E1D,z.zero())) {
        z.decrement(0x2E1F,r.a);
        return z.final_return(0x2E20);
    }
    z.load_hl_direct(0x2E21);
    z.copy(0x2E24,r.a,r.l);
    z.add_immediate(0x2E25);
    z.compare_register(0x2E27,r.h);
    if (!z.relative_if(0x2E28,z.carry()))
        if (!z.relative_if(0x2E2A,z.zero())) z.xor_a(0x2E2C);
    z.store_a_direct(0x2E2D);
    return item_redraw(z);
}
// After the redraw: the option count is worked out from the page, the choice
// is clamped to it, and the list starts again.
template<class Z> Exit item_recount(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x2E37);
    z.copy(0x2E3A,r.l,r.a);
    z.load_a_direct(0x2E3B);
    z.decrement(0x2E3E,r.a);
    z.compare_immediate(0x2E3F);
    z.load_immediate(0x2E41,r.h);
    if (!z.relative_if(0x2E43,z.carry())) z.load_immediate(0x2E45,r.h);
    z.subtract_register(0x2E47,r.l);
    z.compare_immediate(0x2E48);
    if (!z.relative_if(0x2E4A,z.carry())) z.load_immediate(0x2E4C,r.a);
    z.and_immediate(0x2E4E);
    z.add_register(0x2E50,r.h);
    z.store_a_direct(0x2E51);
    z.load_hl_direct(0x2E54);
    z.compare_register(0x2E57,r.l);
    if (!z.relative_if(0x2E58,!z.carry())) z.copy(0x2E5A,r.l,r.a);
    z.copy(0x2E5B,r.h,r.l);
    z.store_hl_direct(0x2E5C);
    z.jump(0x2E5F);
    return item_start(z);
}

// ---- The cursor blink (00:2E62) ------------------------------------------------------------------
// Called from the menu frame handler with interrupts off. While a selection is
// active it erases the cursor cell last drawn, draws it where the choice is now
// (one character cell per 32 bytes: choice * 40h bytes in the name table), and
// every 8 calls alternates the cell's tile between two values (BC).
template<class Z> Exit cursor_blink(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x2E62);
    z.or_a(0x2E65);
    if (auto exit=z.final_return_if(0x2E66,mk::Condition::z)) return *exit;
    z.load_a_direct(0x2E67);
    z.compare_immediate(0x2E6A);
    z.load_bc(0x2E6C);
    if (!z.relative_if(0x2E6F,!z.zero())) z.load_bc(0x2E71);
    z.load_hl_direct(0x2E74);
    z.load_a_direct(0x2E77);
    z.shift_right_logical(0x2E7A,r.a);
    z.load_immediate(0x2E7C,r.e);
    z.rotate_right_through_carry(0x2E7E,r.e);
    z.copy(0x2E80,r.d,r.a);
    z.add_to_hl(0x2E81,z.de());
    z.exchange_de_hl(0x2E82);
    (void)z.restart_vdp_address(0x2E83);
    z.copy(0x2E84,r.a,r.c);
    z.output(0x2E85);
    z.load_hl_direct(0x2E87);
    z.load_a_direct(0x2E8A);
    z.store_a_direct(0x2E8D);
    z.shift_right_logical(0x2E90,r.a);
    z.load_immediate(0x2E92,r.e);
    z.rotate_right_through_carry(0x2E94,r.e);
    z.copy(0x2E96,r.d,r.a);
    z.add_to_hl(0x2E97,z.de());
    z.exchange_de_hl(0x2E98);
    (void)z.restart_vdp_address(0x2E99);
    z.load_a_direct(0x2E9A);
    z.decrement(0x2E9D,r.a);
    z.and_immediate(0x2E9E);
    z.store_a_direct(0x2EA0);
    z.test_bit(0x2EA3,3,r.a);
    z.copy(0x2EA5,r.a,r.b);
    if (!z.relative_if(0x2EA6,!z.zero())) z.copy(0x2EA8,r.a,r.c);
    z.output(0x2EA9);
    return z.final_return(0x2EAB);
}

template<class Logic> void add(RoutineRegistry& registry, std::uint16_t address, RoutineEntry unit,
                               std::vector<SegmentExit> exits, mk::Permissions permissions, Logic logic) {
    mk::add_segment(registry,{0,address},mk::segment(unit,std::move(exits),permissions),logic);
}
}

void register_menu_select(RoutineRegistry& registry) {
    namespace m=menu_ns;
    // Frame interrupt handlers.
    add(registry,0x015F,m::fade_frame_handler_unit,{core_final_at(0x0171)},{},[](auto& z) { return scroll_registers(z,0x015F); });
    add(registry,0x017A,m::fade_frame_handler_unit,{core_final_at(0x0183)},{.scratch_stack=2},[](auto& z) { return fade_palette(z); });
    add(registry,0x01AD,m::menu_frame_handler_unit,{core_final_at(0x01BF)},{},[](auto& z) { return scroll_registers(z,0x01AD); });
    add(registry,0x01D1,m::text_frame_handler_unit,{core_final_at(0x01E3)},{},[](auto& z) { return scroll_registers(z,0x01D1); });
    // Yes or no.
    add(registry,0x2D19,m::yes_no_unit,{core_final_at(0x2D1A,-2)},{.scratch_stack=2},[](auto& z) { return yes_no_open(z); });
    add(registry,0x2D1D,m::yes_no_unit,{core_final_at(0x2D1E,-2)},{.scratch_stack=2},[](auto& z) { return yes_no_between(z); });
    add(registry,0x2D21,m::yes_no_unit,{returned_at(0x2D24,+4)},{.inherited_stack=4},[](auto& z) { return yes_no_close(z); });
    // The waits.
    add(registry,0x2D25,m::wait_button_unit,{core_final_at(0x2D27)},{},[](auto& z) { return button_wait(z,false); });
    add(registry,0x2D2A,m::wait_button_unit,{core_final_at(0x2D27),returned_at(0x2D32)},{},[](auto& z) { return button_wait(z,true); });
    add(registry,0x2D33,m::wait_short_unit,{core_final_at(0x2D3B)},{},[](auto& z) { return timed_wait_short(z); });
    add(registry,0x2D37,m::wait_long_unit,{core_final_at(0x2D3B)},{},[](auto& z) { return timed_wait_long(z); });
    add(registry,0x2D3E,m::wait_long_unit,{core_final_at(0x2D3B),returned_at(0x2D43),returned_at(0x2D46)},{},
        [](auto& z) { return timed_wait_poll(z); });
    add(registry,0x2D47,m::wait_fixed_unit,{core_final_at(0x2D4B)},{},[](auto& z) { return fixed_wait(z); });
    add(registry,0x2D49,m::wait_frames_unit,{core_final_at(0x2D4B)},{},[](auto& z) { return frames_wait(z); });
    add(registry,0x2D4E,m::wait_frames_unit,{core_final_at(0x2D4B),returned_at(0x2D50)},{},[](auto& z) { return frames_wait_next(z); });
    // The cursor loop.
    add(registry,0x2D51,m::selection_unit,{core_final_at(0x2D62)},{},[](auto& z) { return selection_start(z); });
    add(registry,0x2D60,m::selection_unit,{core_final_at(0x2D62)},{},[](auto& z) { return selection_wait(z); });
    add(registry,0x2D65,m::selection_unit,{conditional_at(0x2D90)},{},[](auto& z) { return selection_poll(z); });
    add(registry,0x2D94,m::selection_unit,{core_final_at(0x2D9A)},{},[](auto& z) { return selection_leave(z); });
    add(registry,0x2D9D,m::selection_unit,{returned_at(0x2DA4)},{},[](auto& z) { return selection_end(z); });
    // The paged item list.
    add(registry,0x2DA5,m::item_selection_unit,{core_final_at(0x2DAC)},{},[](auto& z) { return item_start(z); });
    add(registry,0x2DAF,m::item_selection_unit,{core_final_at(0x2DAC),core_final_at(0x2E09),core_final_at(0x2E34)},{},
        [](auto& z) { return item_poll(z); });
    add(registry,0x2E0C,m::item_selection_unit,
        {returned_at(0x2E12),returned_at(0x2E1B),returned_at(0x2E20),core_final_at(0x2E34)},{},[](auto& z) { return item_finish(z); });
    add(registry,0x2E37,m::item_selection_unit,{core_final_at(0x2DAC)},{},[](auto& z) { return item_recount(z); });
    // The cursor blink.
    add(registry,0x2E62,m::cursor_blink_unit,{returned_at(0x2E66),returned_at(0x2EAB)},{.scratch_stack=2},
        [](auto& z) { return cursor_blink(z); });
}
void register_observed_menu_cancel(RoutineRegistry& registry) {
    auto spec=mk::segment(menu::selection_unit,{mk::core_final_at(0x2D9A)},{.scratch_stack=2});
    spec.observe_entry=true;
    mk::add_segment(registry,menu::menu_cancel_point,std::move(spec),[](auto& z) {
        z.copy(0x2D93,z.r.c,z.r.a);
        return selection_leave(z);
    });
}

}
