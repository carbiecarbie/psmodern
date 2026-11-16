// SPDX-License-Identifier: GPL-3.0-or-later
// Menus and windows (milestone 12), part fades:
// Fading the screen: the fade waits 01:7AFD to 01:7B20, the fade and flash
// steps in the frame interrupt 01:7B40 and 01:7BEE, the flashes 01:7BAC and
// 01:7BC4, and the palette effects 01:7C85 to 01:7CF1.
// Planned segments: menu::fades_segments in game/menu.h, rendered with
// game/menu_kit.h.
//
// Every wait ends on its bare call of the frame wait (a core final: the wait
// itself stays native); the stretches between two waits, the two frame
// interrupt steps and the palette effects' loops are rendered. The per-colour
// steps 01:7B5F and 01:7B92 are written once (`colour_out`, `colour_in`): a
// body that names the return it reaches without executing it, so that the
// same code serves the segment of the step itself (its return is the exit)
// and the inline call inside 01:7B40 (the return is rendered).
#include "game/menu.h"
#include "game/menu_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace mk=menu_kit;
namespace menu=game::menu;
using mk::Exit;
using mk::continues;
using mk::returns;

constexpr unsigned wait_call=mk::call_cycles;

// The return a per-colour step reached: its address and whether it is a
// `ret cc` (taken: 11 cycles) or a plain `ret` (10).
struct Return { std::uint16_t at; bool conditional; };

// 01:7B5F: one colour of the fade-out: take the first non-zero channel
// (red, then green, then blue) down one step, leave the zero colour alone.
template<class Z> Return colour_out(Z& z) {
    z.load_from_hl(0x7B5F,z.r.a);
    z.or_a(0x7B60);
    if (z.zero()) return {0x7B61,true};
    z.return_not_taken(0x7B61);
    z.and_immediate(0x7B62);
    if (!z.relative_if(0x7B64,z.zero())) {
        z.decrement_at_hl(0x7B66);
        return {0x7B67,false};
    }
    z.load_from_hl(0x7B68,z.r.a);
    z.and_immediate(0x7B69);
    if (!z.relative_if(0x7B6B,z.zero())) {
        z.load_from_hl(0x7B6D,z.r.a);
        z.subtract_immediate(0x7B6E);
        z.store_register_to_hl(0x7B70,z.r.a);
        return {0x7B71,false};
    }
    z.load_from_hl(0x7B72,z.r.a);
    z.and_immediate(0x7B73);
    if (z.zero()) return {0x7B75,true};
    z.return_not_taken(0x7B75);
    z.subtract_immediate(0x7B76);
    z.store_register_to_hl(0x7B78,z.r.a);
    return {0x7B79,false};
}

// 01:7B92: one colour of the fade-in: (DE) one step towards the target (HL).
template<class Z> Return colour_in(Z& z) {
    z.load_a_from_pair(0x7B92,z.de());
    z.compare_from_hl(0x7B93);
    if (z.zero()) return {0x7B94,true};
    z.return_not_taken(0x7B94);
    z.add_immediate(0x7B95);
    z.compare_from_hl(0x7B97);
    bool store=z.relative_if(0x7B98,z.zero());
    if (!store) store=!z.relative_if(0x7B9A,!z.carry());
    if (store) {
        z.store_a_to_pair(0x7B9C,z.de());
        return {0x7B9D,false};
    }
    z.load_a_from_pair(0x7B9E,z.de());
    z.add_immediate(0x7B9F);
    z.compare_from_hl(0x7BA1);
    store=z.relative_if(0x7BA2,z.zero());
    if (!store) store=!z.relative_if(0x7BA4,!z.carry());
    if (store) {
        z.store_a_to_pair(0x7BA6,z.de());
        return {0x7BA7,false};
    }
    z.exchange_de_hl(0x7BA8);
    z.increment_at_hl(0x7BA9);
    z.exchange_de_hl(0x7BAA);
    return {0x7BAB,false};
}

template<class Body> auto inline_colour(Body body) {
    return [body](auto& z) -> std::uint16_t {
        const Return done=body(z);
        if (done.conditional) (void)z.return_if(done.at,true);
        else (void)z.return_from(done.at);
        return z.r.wz;
    };
}
template<class Body> auto colour_segment(Body body) {
    return [body](auto& z) -> Exit {
        const Return done=body(z);
        return returns(done.at,done.conditional ? mk::taken_conditional_return_cycles : mk::return_cycles);
    };
}

// 01:7B40: the fade step of the frame interrupt (every fourth call): fade-out
// (bit 7 of the timer clear) takes every colour of the palette buffer one
// step down, fade-in (bit 7 set) one step towards the target palette.
template<class Z> Exit fade_step(Z& z) {
    z.load_hl(0x7B40);
    z.decrement_at_hl(0x7B43);
    if (!z.sign()) return returns(0x7B44,mk::taken_conditional_return_cycles);
    z.return_not_taken(0x7B44);
    z.store_to_hl(0x7B45);
    z.load_hl(0x7B47);
    z.load_from_hl(0x7B4A,z.r.a);
    z.test_bit(0x7B4B,7,z.r.a);
    if (!z.jump_if(0x7B4D,!z.zero())) {
        z.or_a(0x7B50);
        if (z.zero()) return returns(0x7B51,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x7B51);
        z.decrement_at_hl(0x7B52);
        z.increment_hl(0x7B53);
        z.load_from_hl(0x7B54,z.r.b);
        z.load_hl(0x7B55);
        do {
            (void)z.inline_subroutine(0x7B58,inline_colour([](auto& y) { return colour_out(y); }));
            z.increment_hl(0x7B5B);
        } while (z.decrement_jump(0x7B5C));
        return returns(0x7B5E,mk::return_cycles);
    }
    z.compare_immediate(0x7B7A);
    if (!z.relative_if(0x7B7C,!z.zero())) {
        z.store_to_hl(0x7B7E);
        return returns(0x7B80,mk::return_cycles);
    }
    z.decrement_at_hl(0x7B81);
    z.increment_hl(0x7B82);
    z.load_from_hl(0x7B83,z.r.b);
    z.load_hl(0x7B84);
    z.load_de(0x7B87);
    do {
        (void)z.inline_subroutine(0x7B8A,inline_colour([](auto& y) { return colour_in(y); }));
        z.increment_hl(0x7B8D);
        z.increment_pair(0x7B8E,z.r.d,z.r.e);
    } while (z.decrement_jump(0x7B8F));
    return returns(0x7B91,mk::return_cycles);
}

// 01:7BEE: the flash step of the frame interrupt: counts the flash down and,
// on odd counts, paints a run of palette entries white; on even counts it
// restores the whole palette from the saved copy.
template<class Z> Exit flash_step(Z& z) {
    z.load_a_direct(0x7BEE);
    z.or_a(0x7BF1);
    if (z.zero()) return returns(0x7BF2,mk::taken_conditional_return_cycles);
    z.return_not_taken(0x7BF2);
    z.decrement(0x7BF3,z.r.a);
    z.store_a_direct(0x7BF4);
    z.rotate_right_circular_a(0x7BF7);
    if (!z.jump_if(0x7BF8,z.carry())) {
        z.load_hl(0x7BFB);
        z.load_de(0x7BFE);
        z.load_bc(0x7C01);
        z.block_copy(0x7C04);
        return returns(0x7C06,mk::return_cycles);
    }
    z.load_hl_direct(0x7C07);
    z.copy(0x7C0A,z.r.b,z.r.h);
    z.copy(0x7C0B,z.r.a,z.r.l);
    z.load_hl(0x7C0C);
    z.add_register(0x7C0F,z.r.l);
    z.copy(0x7C10,z.r.l,z.r.a);
    z.load_immediate(0x7C11,z.r.a);
    do {
        z.store_register_to_hl(0x7C13,z.r.a);
        z.increment_hl(0x7C14);
    } while (z.decrement_jump(0x7C15));
    return returns(0x7C17,mk::return_cycles);
}

// 01:7BB7 and 01:7BE1: the load before the wait of the two flashes.
template<class Z> Exit flash_wait(Z& z, std::uint16_t load_at) {
    z.load_immediate(load_at,z.r.a);
    return continues(static_cast<std::uint16_t>(load_at+2),wait_call);
}
// 01:7BBC and 01:7BE6: the wait's condition (the flash counter at C2BE).
template<class Z> Exit flash_again(Z& z, std::uint16_t at, std::uint16_t load_at, std::uint16_t done) {
    z.load_a_direct(at);
    z.or_a(static_cast<std::uint16_t>(at+3));
    if (z.jump_if(static_cast<std::uint16_t>(at+4),!z.zero())) return flash_wait(z,load_at);
    return returns(done,mk::return_cycles);
}

// 01:7CF7: one step of a palette sequence: the first byte of the pair goes to
// colour 0 and to six colours from C22A, the second to colour 7, then a wait
// of C frames; the pair counter is B (pushed with the frame count).
template<class Z> Exit sequence_step(Z& z) {
    z.push(0x7CF7,z.r.b,z.r.c);
    z.load_from_hl(0x7CF8,z.r.a);
    z.store_a_direct(0x7CF9);
    z.load_immediate(0x7CFC,z.r.b);
    z.load_de(0x7CFE);
    do {
        z.store_a_to_pair(0x7D01,z.de());
        z.increment_pair(0x7D02,z.r.d,z.r.e);
    } while (z.decrement_jump(0x7D03));
    z.increment_hl(0x7D05);
    z.load_from_hl(0x7D06,z.r.a);
    z.store_a_direct(0x7D07);
    z.increment_hl(0x7D0A);
    z.copy(0x7D0B,z.r.b,z.r.c);
    z.load_immediate(0x7D0C,z.r.a);
    return continues(0x7D0E,wait_call);
}

// 01:7C8F: one step of the palette cycle (a copy of eight colours every
// sixteenth step, a wait each).
template<class Z> Exit cycle_step(Z& z) {
    z.push(0x7C8F,z.r.a,z.r.f);
    z.and_immediate(0x7C90);
    z.load_de(0x7C92);
    z.load_bc(0x7C95);
    if (!z.relative_if(0x7C98,!z.zero())) z.block_copy(0x7C9A);
    z.load_immediate(0x7C9C,z.r.a);
    return continues(0x7C9E,wait_call);
}

// 01:7CCE: one pass of the sequence of 01:7CBB (six colours from C22A, then
// eight waits).
template<class Z> Exit sequence_row(Z& z) {
    z.push(0x7CCE,z.r.b,z.r.c);
    z.load_de(0x7CCF);
    z.load_bc(0x7CD2);
    z.block_copy(0x7CD5);
    z.load_immediate(0x7CD7,z.r.b);
    z.load_immediate(0x7CD9,z.r.a);
    return continues(0x7CDB,wait_call);
}
}

void register_menu_fades(RoutineRegistry& registry) {
    using mk::core_final_at; using mk::returned_at; using mk::segment;
    // ---- The fade waits.
    mk::add_segment(registry,menu::fade_out_short_unit,
        segment(menu::fade_out_short_unit,{core_final_at(0x7B0D)}),
        [](auto& z) -> Exit {
            z.load_hl(0x7AFD);
            z.store_hl_direct(0x7B00);
            z.relative(0x7B03);
            z.load_immediate(0x7B0B,z.r.a);
            return continues(0x7B0D,wait_call);
        });
    mk::add_segment(registry,menu::at1(0x7B05),
        segment(menu::fade_out_unit,{core_final_at(0x7B0D)}),
        [](auto& z) -> Exit {
            z.load_hl(0x7B05);
            z.store_hl_direct(0x7B08);
            z.load_immediate(0x7B0B,z.r.a);
            return continues(0x7B0D,wait_call);
        });
    mk::add_segment(registry,menu::at1(0x7B10),
        segment(menu::fade_out_unit,{core_final_at(0x7B0D),returned_at(0x7B17)}),
        [](auto& z) -> Exit { return flash_again(z,0x7B10,0x7B0B,0x7B17); });
    mk::add_segment(registry,menu::fade_in_short_unit,
        segment(menu::fade_in_short_unit,{core_final_at(0x7B35)}),
        [](auto& z) -> Exit {
            z.load_hl(0x7B18);
            z.store_hl_direct(0x7B1B);
            z.relative(0x7B1E);
            z.load_immediate(0x7B33,z.r.a);
            return continues(0x7B35,wait_call);
        });
    mk::add_segment(registry,menu::at1(0x7B20),
        segment(menu::fade_in_unit,{core_final_at(0x7B35)}),
        [](auto& z) -> Exit {
            z.load_hl(0x7B20);
            z.store_hl_direct(0x7B23);
            z.load_hl(0x7B26);
            z.load_de(0x7B29);
            z.load_bc(0x7B2C);
            z.store_to_hl(0x7B2F);
            z.block_copy(0x7B31);
            z.load_immediate(0x7B33,z.r.a);
            return continues(0x7B35,wait_call);
        });
    mk::add_segment(registry,menu::at1(0x7B38),
        segment(menu::fade_in_unit,{core_final_at(0x7B35),returned_at(0x7B3F)}),
        [](auto& z) -> Exit { return flash_again(z,0x7B38,0x7B33,0x7B3F); });

    // ---- The frame interrupt steps and the per-colour steps.
    mk::add_segment(registry,menu::fade_step_unit,
        segment(menu::fade_step_unit,{returned_at(0x7B44),returned_at(0x7B51),returned_at(0x7B5E),
                                      returned_at(0x7B80),returned_at(0x7B91)},{.scratch_stack=2}),
        [](auto& z) -> Exit { return fade_step(z); });
    mk::add_segment(registry,menu::fade_out_colour_unit,
        segment(menu::fade_out_colour_unit,{returned_at(0x7B61),returned_at(0x7B67),returned_at(0x7B71),
                                            returned_at(0x7B75),returned_at(0x7B79)}),
        colour_segment([](auto& z) { return colour_out(z); }));
    mk::add_segment(registry,menu::fade_in_colour_unit,
        segment(menu::fade_in_colour_unit,{returned_at(0x7B94),returned_at(0x7B9D),returned_at(0x7BA7),
                                           returned_at(0x7BAB)}),
        colour_segment([](auto& z) { return colour_in(z); }));

    // ---- The flashes.
    mk::add_segment(registry,menu::battle_flash_unit,
        segment(menu::battle_flash_unit,{core_final_at(0x7BB9)}),
        [](auto& z) -> Exit {
            z.load_immediate(0x7BAC,z.r.a);
            z.store_a_direct(0x7BAE);
            z.load_hl(0x7BB1);
            z.store_hl_direct(0x7BB4);
            return flash_wait(z,0x7BB7);
        });
    mk::add_segment(registry,menu::at1(0x7BBC),
        segment(menu::battle_flash_unit,{core_final_at(0x7BB9),returned_at(0x7BC3)}),
        [](auto& z) -> Exit { return flash_again(z,0x7BBC,0x7BB7,0x7BC3); });
    mk::add_segment(registry,menu::magic_flash_unit,
        segment(menu::magic_flash_unit,{core_final_at(0x7BE3)}),
        [](auto& z) -> Exit {
            z.load_immediate(0x7BC4,z.r.a);
            z.store_a_direct(0x7BC6);
            z.load_hl(0x7BC9);
            z.load_a_direct(0x7BCC);
            z.compare_immediate(0x7BCF);
            if (!z.relative_if(0x7BD1,z.zero())) {
                z.compare_immediate(0x7BD3);
                if (!z.relative_if(0x7BD5,z.zero())) {
                    z.compare_immediate(0x7BD7);
                    if (!z.relative_if(0x7BD9,z.zero())) z.load_hl(0x7BDB);
                }
            }
            z.store_hl_direct(0x7BDE);
            return flash_wait(z,0x7BE1);
        });
    mk::add_segment(registry,menu::at1(0x7BE6),
        segment(menu::magic_flash_unit,{core_final_at(0x7BE3),returned_at(0x7BED)}),
        [](auto& z) -> Exit { return flash_again(z,0x7BE6,0x7BE1,0x7BED); });
    mk::add_segment(registry,menu::flash_step_unit,
        segment(menu::flash_step_unit,{returned_at(0x7BF2),returned_at(0x7C06),returned_at(0x7C17)}),
        [](auto& z) -> Exit { return flash_step(z); });

    // ---- The palette effects (bank 3 into slot 2 for their tables).
    mk::add_segment(registry,menu::palette_cycle_unit,
        segment(menu::palette_cycle_unit,{core_final_at(0x7C9E,-2)},{.slot2_mapper=true,.scratch_stack=2}),
        [](auto& z) -> Exit {
            z.load_hl(0x7C85);
            z.store_to_hl(0x7C88);
            z.load_hl(0x7C8A);
            z.load_immediate(0x7C8D,z.r.a);
            return cycle_step(z);
        });
    mk::add_segment(registry,menu::at1(0x7CA1),
        segment(menu::palette_cycle_unit,{core_final_at(0x7C9E),returned_at(0x7CA5,+2)},{.inherited_stack=2}),
        [](auto& z) -> Exit {
            z.pop(0x7CA1,z.r.a,z.r.f);
            z.decrement(0x7CA2,z.r.a);
            if (z.relative_if(0x7CA3,!z.zero())) return cycle_step(z);
            return returns(0x7CA5,mk::return_cycles);
        });
    mk::add_segment(registry,menu::palette_load_unit,
        segment(menu::palette_load_unit,{core_final_at(0x7CB8)},{.slot2_mapper=true}),
        [](auto& z) -> Exit {
            z.load_hl(0x7CA6);
            z.store_to_hl(0x7CA9);
            z.load_hl(0x7CAB);
            z.load_de(0x7CAE);
            z.load_bc(0x7CB1);
            z.block_copy(0x7CB4);
            z.load_immediate(0x7CB6,z.r.a);
            return continues(0x7CB8,mk::jump_cycles);
        });
    mk::add_segment(registry,menu::palette_sequence_unit,
        segment(menu::palette_sequence_unit,{core_final_at(0x7CC0)},{.slot2_mapper=true}),
        [](auto& z) -> Exit {
            z.load_hl(0x7CBB);
            z.store_to_hl(0x7CBE);
            return continues(0x7CC0,wait_call);
        });
    mk::add_segment(registry,menu::at1(0x7CC9),
        segment(menu::palette_sequence_unit,{core_final_at(0x7CDB,-2)},{.scratch_stack=2}),
        [](auto& z) -> Exit {
            z.load_hl(0x7CC9);
            z.load_immediate(0x7CCC,z.r.b);
            return sequence_row(z);
        });
    mk::add_segment(registry,menu::at1(0x7CDE),
        segment(menu::palette_sequence_unit,{core_final_at(0x7CDB),returned_at(0x7CE3,+2)},{.inherited_stack=2}),
        [](auto& z) -> Exit {
            if (z.decrement_jump(0x7CDE)) {
                z.load_immediate(0x7CD9,z.r.a);
                return continues(0x7CDB,wait_call);
            }
            z.pop(0x7CE0,z.r.b,z.r.c);
            if (z.decrement_jump(0x7CE1)) return sequence_row(z);
            return returns(0x7CE3,mk::return_cycles);
        });
    mk::add_segment(registry,menu::scene_palette_sequence_unit,
        segment(menu::scene_palette_sequence_unit,{core_final_at(0x7D0E,-2)},{.slot2_mapper=true,.scratch_stack=2}),
        [](auto& z) -> Exit {
            z.load_hl(0x7CE4);
            z.store_to_hl(0x7CE7);
            z.load_hl(0x7CE9);
            z.load_bc(0x7CEC);
            z.relative(0x7CEF);
            return sequence_step(z);
        });
    mk::add_segment(registry,menu::palette_sequence_step_unit,
        segment(menu::palette_sequence_step_unit,{core_final_at(0x7D0E,-2)},{.scratch_stack=2}),
        [](auto& z) -> Exit {
            z.load_hl(0x7CF1);
            z.load_bc(0x7CF4);
            return sequence_step(z);
        });
    mk::add_segment(registry,menu::at1(0x7D11),
        segment(menu::palette_sequence_step_unit,{core_final_at(0x7D0E),returned_at(0x7D16,+2)},{.inherited_stack=2}),
        [](auto& z) -> Exit {
            if (z.decrement_jump(0x7D11)) {
                z.load_immediate(0x7D0C,z.r.a);
                return continues(0x7D0E,wait_call);
            }
            z.pop(0x7D13,z.r.b,z.r.c);
            if (z.decrement_jump(0x7D14)) return sequence_step(z);
            return returns(0x7D16,mk::return_cycles);
        });
}
}
