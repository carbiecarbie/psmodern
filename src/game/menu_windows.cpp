// SPDX-License-Identifier: GPL-3.0-or-later
// Menus and windows (milestone 12), part windows:
// Every window the game opens, field and battle: the short wrappers that save
// what lies under a window, draw it from its bank-27 table and close it again
// (00:2EAC to 00:3A56). Each wrapper is a handful of loads (the table, the
// screen position, the size) between bare calls to the window engine, so a
// segment renders the loads and ends on the next call, which the core runs.
// Planned segments: menu::windows_segments in game/menu.h, rendered with
// game/menu_kit.h. Every table address, position and size is read from the
// player's ROM at run time by the kit's loads.
#include "game/menu.h"
#include "game/menu_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace mk=menu_kit;

// One registration: the entry, its unit, the exits and permissions of the
// segment (segments.md section 4W2) and the rendering of its path.
struct Adder {
    RoutineRegistry& registry;
    template<class Logic> void operator()(RoutineEntry entry, RoutineEntry unit, std::vector<SegmentExit> exits,
                                          mk::Permissions permissions, Logic logic) const {
        mk::add_segment(registry,entry,mk::segment(unit,std::move(exits),permissions),std::move(logic));
    }
};
}

void register_menu_windows(RoutineRegistry& registry) {
    const Adder add{registry};

// ---- 00:2EAC Opening the battle name box: save what is under it, then draw it with the name of the acting character.
    add(menu::at0(0x2EAC),menu::battle_name_box_unit,{mk::core_final_at(0x2EB5)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2EAC);
        z.load_de(0x2EAF);
        z.load_bc(0x2EB2);
        return mk::continues(0x2EB5,mk::call_cycles);
    });
    add(menu::at0(0x2EB8),menu::battle_name_box_unit,{mk::core_final_at(0x2ECA)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_a_direct(0x2EB8);
        z.add_register(0x2EBB,r.a);
        z.add_register(0x2EBC,r.a);
        z.copy(0x2EBD,r.l,r.a);
        z.add_register(0x2EBE,r.a);
        z.add_register(0x2EBF,r.a);
        z.add_register(0x2EC0,r.a);
        z.add_register(0x2EC1,r.l);
        z.load_hl(0x2EC2);
        z.add_register(0x2EC5,r.l);
        z.copy(0x2EC6,r.l,r.a);
        z.add_with_carry(0x2EC7,r.h);
        z.subtract_register(0x2EC8,r.l);
        z.copy(0x2EC9,r.h,r.a);
        return mk::continues(0x2ECA,mk::jump_cycles);
    });

// ---- 00:2ECD Closing the battle name box: put back what was under it.
    add(menu::at0(0x2ECD),menu::battle_name_box_close_unit,{mk::core_final_at(0x2ED6)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2ECD);
        z.load_de(0x2ED0);
        z.load_bc(0x2ED3);
        return mk::continues(0x2ED6,mk::jump_cycles);
    });

// ---- 00:2ED9 Opening the party window in battle: save the area, draw each living member's box.
    add(menu::at0(0x2ED9),menu::party_window_unit,{mk::core_final_at(0x2EE2)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2ED9);
        z.load_de(0x2EDC);
        z.load_bc(0x2EDF);
        return mk::continues(0x2EE2,mk::call_cycles);
    });
    add(menu::at0(0x2EE5),menu::party_window_unit,{mk::core_final_at(0x2EEF)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2EE5);
        z.load_de(0x2EE8);
        z.load_index(0x2EEB,mk::Index::ix);
        return mk::continues(0x2EEF,mk::call_cycles);
    });
    add(menu::at0(0x2EF2),menu::party_window_unit,{mk::core_final_at(0x2EFC)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2EF2);
        z.load_de(0x2EF5);
        z.load_index(0x2EF8,mk::Index::ix);
        return mk::continues(0x2EFC,mk::call_cycles);
    });
    add(menu::at0(0x2EFF),menu::party_window_unit,{mk::core_final_at(0x2F09)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2EFF);
        z.load_de(0x2F02);
        z.load_index(0x2F05,mk::Index::ix);
        return mk::continues(0x2F09,mk::call_cycles);
    });
    add(menu::at0(0x2F0C),menu::party_window_unit,{mk::returned_at(0x2F1A),mk::core_final_at(0x2F1E)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2F0C);
        z.load_de(0x2F0F);
        z.load_index(0x2F12,mk::Index::ix);
        z.test_bit_indexed(0x2F16,mk::Index::ix,0);
        if (z.zero()) return mk::returns(0x2F1A,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x2F1A);
        z.load_bc(0x2F1B);
        return mk::continues(0x2F1E,mk::call_cycles);
    });

// ---- 00:2F16 One member's box (rendered into by 2ED9): frame, HP, MP and the label.
    add(menu::at0(0x2F16),menu::member_box_unit,{mk::returned_at(0x2F1A),mk::core_final_at(0x2F1E)},{},[](auto& z) -> mk::Exit {
        z.test_bit_indexed(0x2F16,mk::Index::ix,0);
        if (z.zero()) return mk::returns(0x2F1A,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x2F1A);
        z.load_bc(0x2F1B);
        return mk::continues(0x2F1E,mk::call_cycles);
    });
    add(menu::at0(0x2F21),menu::member_box_unit,{mk::core_final_at(0x2F27)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x2F21);
        z.load_from_indexed(0x2F24,mk::Index::ix,r.a);
        return mk::continues(0x2F27,mk::call_cycles);
    });
    add(menu::at0(0x2F2A),menu::member_box_unit,{mk::core_final_at(0x2F30)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x2F2A);
        z.load_from_indexed(0x2F2D,mk::Index::ix,r.a);
        return mk::continues(0x2F30,mk::call_cycles);
    });
    add(menu::at0(0x2F33),menu::member_box_unit,{mk::core_final_at(0x2F39)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2F33);
        z.load_bc(0x2F36);
        return mk::continues(0x2F39,mk::jump_cycles);
    });

// ---- 00:2F3C Redrawing the party window without saving, member by member.
    add(menu::at0(0x2F3C),menu::party_window_redraw_unit,{mk::core_final_at(0x2F46)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2F3C);
        z.load_de(0x2F3F);
        z.load_index(0x2F42,mk::Index::ix);
        return mk::continues(0x2F46,mk::call_cycles);
    });
    add(menu::at0(0x2F49),menu::party_window_redraw_unit,{mk::core_final_at(0x2F53)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2F49);
        z.load_de(0x2F4C);
        z.load_index(0x2F4F,mk::Index::ix);
        return mk::continues(0x2F53,mk::call_cycles);
    });
    add(menu::at0(0x2F56),menu::party_window_redraw_unit,{mk::core_final_at(0x2F60)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2F56);
        z.load_de(0x2F59);
        z.load_index(0x2F5C,mk::Index::ix);
        return mk::continues(0x2F60,mk::call_cycles);
    });
    add(menu::at0(0x2F63),menu::party_window_redraw_unit,{mk::returned_at(0x2F71),mk::core_final_at(0x2F75)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2F63);
        z.load_de(0x2F66);
        z.load_index(0x2F69,mk::Index::ix);
        z.test_bit_indexed(0x2F6D,mk::Index::ix,0);
        if (z.zero()) return mk::returns(0x2F71,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x2F71);
        z.load_bc(0x2F72);
        return mk::continues(0x2F75,mk::call_cycles);
    });

// ---- 00:2F6D One member's box for the redraw (like 2F16, drawing at once).
    add(menu::at0(0x2F6D),menu::member_box_redraw_unit,{mk::returned_at(0x2F71),mk::core_final_at(0x2F75)},{},[](auto& z) -> mk::Exit {
        z.test_bit_indexed(0x2F6D,mk::Index::ix,0);
        if (z.zero()) return mk::returns(0x2F71,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x2F71);
        z.load_bc(0x2F72);
        return mk::continues(0x2F75,mk::call_cycles);
    });
    add(menu::at0(0x2F78),menu::member_box_redraw_unit,{mk::core_final_at(0x2F7E)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x2F78);
        z.load_from_indexed(0x2F7B,mk::Index::ix,r.a);
        return mk::continues(0x2F7E,mk::call_cycles);
    });
    add(menu::at0(0x2F81),menu::member_box_redraw_unit,{mk::core_final_at(0x2F87)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x2F81);
        z.load_from_indexed(0x2F84,mk::Index::ix,r.a);
        return mk::continues(0x2F87,mk::call_cycles);
    });
    add(menu::at0(0x2F8A),menu::member_box_redraw_unit,{mk::core_final_at(0x2F90)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x2F8A);
        z.load_bc(0x2F8D);
        return mk::continues(0x2F90,mk::jump_cycles);
    });

// ---- 00:2F93 Refreshing a single member's box, the member number in A.
    add(menu::at0(0x2F93),menu::member_box_refresh_unit,{mk::core_final_at(0x2F9D,-2)},{.scratch_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.push(0x2F93,r.a,r.f);
        z.load_hl(0x2F94);
        z.load_de(0x2F97);
        z.load_bc(0x2F9A);
        return mk::continues(0x2F9D,mk::call_cycles);
    });
    add(menu::at0(0x2FA0),menu::member_box_refresh_unit,{mk::core_final_at(0x2F1E,2)},{.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x2FA0,r.a,r.f);
        z.load_hl(0x2FA1);
        z.load_de(0x2FA4);
        z.load_index(0x2FA7,mk::Index::ix);
        z.or_a(0x2FAB);
        if (z.jump_if(0x2FAC,z.zero())) {
            z.load_bc(0x2F1B);
            return mk::continues(0x2F1E,mk::call_cycles);
        }
        z.load_hl(0x2FAF);
        z.load_de(0x2FB2);
        z.load_index(0x2FB5,mk::Index::ix);
        z.decrement(0x2FB9,r.a);
        if (z.jump_if(0x2FBA,z.zero())) {
            z.load_bc(0x2F1B);
            return mk::continues(0x2F1E,mk::call_cycles);
        }
        z.load_hl(0x2FBD);
        z.load_de(0x2FC0);
        z.load_index(0x2FC3,mk::Index::ix);
        z.decrement(0x2FC7,r.a);
        if (z.jump_if(0x2FC8,z.zero())) {
            z.load_bc(0x2F1B);
            return mk::continues(0x2F1E,mk::call_cycles);
        }
        z.load_hl(0x2FCB);
        z.load_de(0x2FCE);
        z.load_index(0x2FD1,mk::Index::ix);
        z.jump(0x2FD5);
        z.load_bc(0x2F1B);
        return mk::continues(0x2F1E,mk::call_cycles);
    });

// ---- 00:30B7 Closing the party window.
    add(menu::at0(0x30B7),menu::party_window_close_unit,{mk::core_final_at(0x30C0)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x30B7);
        z.load_de(0x30BA);
        z.load_bc(0x30BD);
        return mk::continues(0x30C0,mk::jump_cycles);
    });

// ---- 00:30C3 Opening the battle command window.
    add(menu::at0(0x30C3),menu::battle_commands_unit,{mk::core_final_at(0x30CC)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x30C3);
        z.load_de(0x30C6);
        z.load_bc(0x30C9);
        return mk::continues(0x30CC,mk::call_cycles);
    });
    add(menu::at0(0x30CF),menu::battle_commands_unit,{mk::core_final_at(0x30D2)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x30CF);
        return mk::continues(0x30D2,mk::jump_cycles);
    });

// ---- 00:30D5 Redrawing the battle command window.
    add(menu::at0(0x30D5),menu::battle_commands_redraw_unit,{mk::core_final_at(0x30DE)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x30D5);
        z.load_de(0x30D8);
        z.load_bc(0x30DB);
        return mk::continues(0x30DE,mk::jump_cycles);
    });

// ---- 00:30E1 Closing the battle command window.
    add(menu::at0(0x30E1),menu::battle_commands_close_unit,{mk::core_final_at(0x30EA)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x30E1);
        z.load_de(0x30E4);
        z.load_bc(0x30E7);
        return mk::continues(0x30EA,mk::jump_cycles);
    });

// ---- 00:30ED Opening the two enemy windows (names, counts) in battle.
    add(menu::at0(0x30ED),menu::enemy_windows_unit,{mk::core_final_at(0x30F6)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x30ED);
        z.load_de(0x30F0);
        z.load_bc(0x30F3);
        return mk::continues(0x30F6,mk::call_cycles);
    });
    add(menu::at0(0x30F9),menu::enemy_windows_unit,{mk::core_final_at(0x3102)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x30F9);
        z.load_de(0x30FC);
        z.load_bc(0x30FF);
        return mk::continues(0x3102,mk::call_cycles);
    });

// ---- 00:3105 The enemy list: names, then the party's HP column.
    add(menu::at0(0x3105),menu::enemy_list_unit,{mk::core_final_at(0x310E)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3105);
        z.load_de(0x3108);
        z.load_bc(0x310B);
        return mk::continues(0x310E,mk::call_cycles);
    });
    add(menu::at0(0x3111),menu::enemy_list_unit,{mk::core_final_at(0x3116)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x3111);
        z.load_immediate(0x3114,r.c);
        return mk::continues(0x3116,mk::call_cycles);
    });
    add(menu::at0(0x3119),menu::enemy_list_unit,{mk::core_final_at(0x311B)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_immediate(0x3119,r.c);
        return mk::continues(0x311B,mk::call_cycles);
    });
    add(menu::at0(0x311E),menu::enemy_list_unit,{mk::core_final_at(0x3124)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x311E);
        z.load_bc(0x3121);
        return mk::continues(0x3124,mk::call_cycles);
    });
    add(menu::at0(0x3127),menu::enemy_list_unit,{mk::returned_at(0x312C),mk::core_final_at(0x3136)},{},[](auto& z) -> mk::Exit {
        z.load_a_direct(0x3127);
        z.compare_immediate(0x312A);
        if (z.zero()) return mk::returns(0x312C,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x312C);
        z.load_hl(0x312D);
        z.load_de(0x3130);
        z.load_bc(0x3133);
        return mk::continues(0x3136,mk::call_cycles);
    });
    add(menu::at0(0x3139),menu::enemy_list_unit,{mk::core_final_at(0x3148,-2)},{.scratch_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_index(0x3139,mk::Index::ix);
        z.load_a_direct(0x313D);
        z.copy(0x3140,r.b,r.a);
        z.push(0x3141,r.b,r.c);
        z.load_hl(0x3142);
        z.load_from_indexed(0x3145,mk::Index::ix,r.a);
        return mk::continues(0x3148,mk::call_cycles);
    });
    add(menu::at0(0x314B),menu::enemy_list_unit,{mk::core_final_at(0x3148),mk::core_final_at(0x3159,2)},{.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_bc(0x314B);
        z.add_to_index(0x314E,mk::Index::ix,z.bc());
        z.pop(0x3150,r.b,r.c);
        if (z.decrement_jump(0x3151)) {
            z.push(0x3141,r.b,r.c);
            z.load_hl(0x3142);
            z.load_from_indexed(0x3145,mk::Index::ix,r.a);
            return mk::continues(0x3148,mk::call_cycles);
        }
        z.load_hl(0x3153);
        z.load_bc(0x3156);
        return mk::continues(0x3159,mk::jump_cycles);
    });

// ---- 00:319E Closing the enemy windows.
    add(menu::at0(0x319E),menu::enemy_windows_close_unit,{mk::core_final_at(0x31AC)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x319E);
        z.load_de(0x31A1);
        z.load_bc(0x31A4);
        z.load_a_direct(0x31A7);
        z.compare_immediate(0x31AA);
        return mk::continues(0x31AC,!z.zero() ? mk::call_cycles : mk::untaken_call_cycles);
    });
    add(menu::at0(0x31AF),menu::enemy_windows_close_unit,{mk::core_final_at(0x31B8)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x31AF);
        z.load_de(0x31B2);
        z.load_bc(0x31B5);
        return mk::continues(0x31B8,mk::jump_cycles);
    });

// ---- 00:3464 Closing the dialogue window if it is open.
    add(menu::at0(0x3464),menu::dialogue_close_unit,{mk::returned_at(0x3469),mk::core_final_at(0x3475)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x3464);
        z.load_from_hl(0x3467,r.a);
        z.or_a(0x3468);
        if (z.zero()) return mk::returns(0x3469,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x3469);
        z.store_to_hl(0x346A);
        z.load_hl(0x346C);
        z.load_de(0x346F);
        z.load_bc(0x3472);
        return mk::continues(0x3475,mk::jump_cycles);
    });

// ---- 00:3478 The magic list window, with the part of it the caster knows.
    add(menu::at0(0x3478),menu::magic_list_unit,{mk::core_final_at(0x3483,-4)},{.scratch_stack=4},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.push(0x3478,r.a,r.f);
        z.push(0x3479,r.b,r.c);
        z.load_hl(0x347A);
        z.load_de(0x347D);
        z.load_bc(0x3480);
        return mk::continues(0x3483,mk::call_cycles);
    });
    add(menu::at0(0x3486),menu::magic_list_unit,{mk::core_final_at(0x34A7,2),mk::core_final_at(0x34C6,4)},{.inherited_stack=4},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x3486,r.b,r.c);
        z.pop(0x3487,r.a,r.f);
        z.add_register(0x3488,r.a);
        z.add_register(0x3489,r.a);
        z.add_register(0x348A,r.a);
        z.copy(0x348B,r.l,r.a);
        z.load_immediate(0x348C,r.h);
        z.copy(0x348E,r.e,r.l);
        z.copy(0x348F,r.d,r.h);
        z.add_to_hl(0x3490,z.hl());
        z.add_to_hl(0x3491,z.hl());
        z.add_to_hl(0x3492,z.hl());
        z.add_to_hl(0x3493,z.de());
        z.add_to_hl(0x3494,z.hl());
        z.load_de(0x3495);
        z.add_to_hl(0x3498,z.de());
        z.load_de(0x3499);
        z.copy(0x349C,r.a,r.b);
        z.or_a(0x349D);
        if (z.jump_if(0x349E,z.zero())) {
            z.load_hl(0x34C0);
            z.load_bc(0x34C3);
            return mk::continues(0x34C6,mk::jump_cycles);
        }
        z.add_register(0x34A1,r.a);
        z.increment(0x34A2,r.a);
        z.copy(0x34A3,r.b,r.a);
        z.load_immediate(0x34A4,r.c);
        z.push(0x34A6,r.b,r.c);
        return mk::continues(0x34A7,mk::call_cycles);
    });
    add(menu::at0(0x34AA),menu::magic_list_unit,{mk::core_final_at(0x34BD,2)},{.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x34AA,r.b,r.c);
        z.copy(0x34AB,r.a,r.b);
        z.add_register(0x34AC,r.a);
        z.copy(0x34AD,r.l,r.a);
        z.add_register(0x34AE,r.a);
        z.add_register(0x34AF,r.l);
        z.add_register(0x34B0,r.a);
        z.load_hl(0x34B1);
        z.add_register(0x34B4,r.l);
        z.copy(0x34B5,r.l,r.a);
        z.add_with_carry(0x34B6,r.h);
        z.subtract_register(0x34B7,r.l);
        z.copy(0x34B8,r.h,r.a);
        z.load_immediate(0x34B9,r.a);
        z.subtract_register(0x34BB,r.b);
        z.copy(0x34BC,r.b,r.a);
        return mk::continues(0x34BD,mk::jump_cycles);
    });

// ---- 00:34C9 Closing the magic list window.
    add(menu::at0(0x34C9),menu::magic_list_close_unit,{mk::core_final_at(0x34D2)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x34C9);
        z.load_de(0x34CC);
        z.load_bc(0x34CF);
        return mk::continues(0x34D2,mk::jump_cycles);
    });

// ---- 00:34D5 Choosing an item: open the list, run the selection, read the item chosen.
    add(menu::at0(0x34D5),menu::item_choice_unit,{mk::core_final_at(0x34E1)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_a_direct(0x34D5);
        z.decrement(0x34D8,r.a);
        z.and_immediate(0x34D9);
        z.copy(0x34DB,r.h,r.a);
        z.load_immediate(0x34DC,r.l);
        z.store_hl_direct(0x34DE);
        return mk::continues(0x34E1,mk::call_cycles);
    });
    add(menu::at0(0x34E4),menu::item_choice_unit,{mk::conditional_at(0x34E8)},{},[](auto& z) -> mk::Exit {
        z.load_a_direct(0x34E4);
        z.or_a(0x34E7);
        return mk::jump_if_exit(0x34E8);
    });
    add(menu::at0(0x34EB),menu::item_choice_unit,{mk::core_final_at(0x350C)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.decrement(0x34EB,r.a);
        z.compare_immediate(0x34EC);
        z.load_immediate(0x34EE,r.l);
        if (z.relative_if(0x34F0,z.carry())) {
            z.and_immediate(0x34F6);
            z.add_register(0x34F8,r.l);
            z.store_a_direct(0x34F9);
            z.load_hl(0x34FC);
            z.store_hl_direct(0x34FF);
            z.load_hl(0x3502);
            z.store_hl_direct(0x3505);
            z.xor_a(0x3508);
            z.store_a_direct(0x3509);
            return mk::continues(0x350C,mk::call_cycles);
        }
        z.load_immediate(0x34F2,r.l);
        z.load_immediate(0x34F4,r.a);
        z.and_immediate(0x34F6);
        z.add_register(0x34F8,r.l);
        z.store_a_direct(0x34F9);
        z.load_hl(0x34FC);
        z.store_hl_direct(0x34FF);
        z.load_hl(0x3502);
        z.store_hl_direct(0x3505);
        z.xor_a(0x3508);
        z.store_a_direct(0x3509);
        return mk::continues(0x350C,mk::call_cycles);
    });
    add(menu::at0(0x350F),menu::item_choice_unit,{mk::returned_at(0x3520)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.copy(0x350F,r.l,r.a);
        z.load_a_direct(0x3510);
        z.add_register(0x3513,r.l);
        z.load_hl(0x3514);
        z.add_register(0x3517,r.l);
        z.copy(0x3518,r.l,r.a);
        z.store_hl_direct(0x3519);
        z.load_from_hl(0x351C,r.a);
        z.store_a_direct(0x351D);
        return mk::returns(0x3520,mk::return_cycles);
    });

// ---- 00:3521 Opening the item list window (saving what is under it).
    add(menu::at0(0x3521),menu::item_list_open_unit,{mk::core_final_at(0x352A)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3521);
        z.load_de(0x3524);
        z.load_bc(0x3527);
        return mk::continues(0x352A,mk::call_cycles);
    });

// ---- 00:352D Drawing the item list: money, the page arrows and the eight item names.
    add(menu::at0(0x352D),menu::item_list_unit,{mk::core_final_at(0x3536)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x352D);
        z.load_de(0x3530);
        z.load_bc(0x3533);
        return mk::continues(0x3536,mk::call_cycles);
    });
    add(menu::at0(0x353C),menu::item_list_unit,{mk::core_final_at(0x3547)},{},[](auto& z) -> mk::Exit {
        z.load_a_direct(0x353C);
        z.compare_immediate(0x353F);
        z.load_hl(0x3541);
        z.load_bc(0x3544);
        return mk::continues(0x3547,!z.carry() ? mk::call_cycles : mk::untaken_call_cycles);
    });
    add(menu::at0(0x354A),menu::item_list_unit,{mk::core_final_at(0x3556)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_a_direct(0x354A);
        z.load_hl(0x354D);
        z.add_register(0x3550,r.l);
        z.copy(0x3551,r.l,r.a);
        z.load_immediate(0x3552,r.b);
        z.load_immediate(0x3554,r.c);
        return mk::continues(0x3556,mk::call_cycles);
    });
    add(menu::at0(0x3559),menu::item_list_unit,{mk::core_final_at(0x355B)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_immediate(0x3559,r.c);
        return mk::continues(0x355B,mk::call_cycles);
    });
    add(menu::at0(0x355E),menu::item_list_unit,{mk::core_final_at(0x3556),mk::core_final_at(0x3567)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.increment_pair(0x355E,r.h,r.l);
        if (z.decrement_jump(0x355F)) {
            z.load_immediate(0x3554,r.c);
            return mk::continues(0x3556,mk::call_cycles);
        }
        z.load_hl(0x3561);
        z.load_bc(0x3564);
        return mk::continues(0x3567,mk::call_cycles);
    });

// ---- 00:3656 Closing the item list window (the held BC kept).
    add(menu::at0(0x3656),menu::item_list_close_unit,{mk::core_final_at(0x3660,-2)},{.scratch_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.push(0x3656,r.b,r.c);
        z.load_hl(0x3657);
        z.load_de(0x365A);
        z.load_bc(0x365D);
        return mk::continues(0x3660,mk::call_cycles);
    });
    add(menu::at0(0x3663),menu::item_list_close_unit,{mk::returned_at(0x3664,+2)},{.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x3663,r.b,r.c);
        return mk::returns(0x3664,mk::return_cycles);
    });

// ---- 00:3665 Choosing a party member in the field.
    add(menu::at0(0x3665),menu::member_choice_unit,{mk::returned_at(0x3669),mk::core_final_at(0x3673)},{},[](auto& z) -> mk::Exit {
        z.load_a_direct(0x3665);
        z.or_a(0x3668);
        if (z.zero()) return mk::returns(0x3669,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x3669);
        z.load_hl(0x366A);
        z.load_de(0x366D);
        z.load_bc(0x3670);
        return mk::continues(0x3673,mk::call_cycles);
    });
    add(menu::at0(0x3679),menu::member_choice_unit,{mk::core_final_at(0x367F)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3679);
        z.store_hl_direct(0x367C);
        return mk::continues(0x367F,mk::jump_cycles);
    });

// ---- 00:3682 Choosing a party member in battle.
    add(menu::at0(0x3682),menu::battle_member_choice_unit,{mk::returned_at(0x3686),mk::core_final_at(0x3690)},{},[](auto& z) -> mk::Exit {
        z.load_a_direct(0x3682);
        z.or_a(0x3685);
        if (z.zero()) return mk::returns(0x3686,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x3686);
        z.load_hl(0x3687);
        z.load_de(0x368A);
        z.load_bc(0x368D);
        return mk::continues(0x3690,mk::call_cycles);
    });
    add(menu::at0(0x3696),menu::battle_member_choice_unit,{mk::core_final_at(0x369C)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3696);
        z.store_hl_direct(0x3699);
        return mk::continues(0x369C,mk::jump_cycles);
    });

// ---- 00:369F Writing the members' names into the choice window.
    add(menu::at0(0x369F),menu::member_names_unit,{mk::returned_at(0x36A3),mk::core_final_at(0x36AF)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_a_direct(0x369F);
        z.or_a(0x36A2);
        if (z.zero()) return mk::returns(0x36A3,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x36A3);
        z.store_a_direct(0x36A4);
        z.increment(0x36A7,r.a);
        z.add_register(0x36A8,r.a);
        z.copy(0x36A9,r.b,r.a);
        z.load_immediate(0x36AA,r.c);
        z.load_hl(0x36AC);
        return mk::continues(0x36AF,mk::call_cycles);
    });
    add(menu::at0(0x36B2),menu::member_names_unit,{mk::core_final_at(0x36B8)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x36B2);
        z.load_bc(0x36B5);
        return mk::continues(0x36B8,mk::jump_cycles);
    });

// ---- 00:36BB Closing the member choice window.
    add(menu::at0(0x36BB),menu::member_choice_close_unit,{mk::returned_at(0x36BF),mk::core_final_at(0x36C9)},{},[](auto& z) -> mk::Exit {
        z.load_a_direct(0x36BB);
        z.or_a(0x36BE);
        if (z.zero()) return mk::returns(0x36BF,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x36BF);
        z.load_hl(0x36C0);
        z.load_de(0x36C3);
        z.load_bc(0x36C6);
        return mk::continues(0x36C9,mk::jump_cycles);
    });

// ---- 00:36CC Closing the battle member choice window.
    add(menu::at0(0x36CC),menu::battle_member_choice_close_unit,{mk::returned_at(0x36D0),mk::core_final_at(0x36DA)},{},[](auto& z) -> mk::Exit {
        z.load_a_direct(0x36CC);
        z.or_a(0x36CF);
        if (z.zero()) return mk::returns(0x36D0,mk::taken_conditional_return_cycles);
        z.return_not_taken(0x36D0);
        z.load_hl(0x36D1);
        z.load_de(0x36D4);
        z.load_bc(0x36D7);
        return mk::continues(0x36DA,mk::jump_cycles);
    });

// ---- 00:36DD The field menu window.
    add(menu::at0(0x36DD),menu::field_menu_window_unit,{mk::core_final_at(0x36E6)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x36DD);
        z.load_de(0x36E0);
        z.load_bc(0x36E3);
        return mk::continues(0x36E6,mk::call_cycles);
    });
    add(menu::at0(0x36E9),menu::field_menu_window_unit,{mk::core_final_at(0x36EC)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x36E9);
        return mk::continues(0x36EC,mk::jump_cycles);
    });

// ---- 00:36EF Redrawing the field menu window.
    add(menu::at0(0x36EF),menu::field_menu_window_redraw_unit,{mk::core_final_at(0x36F8)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x36EF);
        z.load_de(0x36F2);
        z.load_bc(0x36F5);
        return mk::continues(0x36F8,mk::jump_cycles);
    });

// ---- 00:36FB Closing the field menu window.
    add(menu::at0(0x36FB),menu::field_menu_window_close_unit,{mk::core_final_at(0x3704)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x36FB);
        z.load_de(0x36FE);
        z.load_bc(0x3701);
        return mk::continues(0x3704,mk::jump_cycles);
    });

// ---- 00:3707 The equipment window.
    add(menu::at0(0x3707),menu::equipment_window_unit,{mk::core_final_at(0x3711,-2)},{.scratch_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.push(0x3707,r.a,r.f);
        z.load_hl(0x3708);
        z.load_de(0x370B);
        z.load_bc(0x370E);
        return mk::continues(0x3711,mk::call_cycles);
    });
    add(menu::at0(0x3714),menu::equipment_window_unit,{mk::core_final_at(0x371D)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3714);
        z.load_de(0x3717);
        z.load_bc(0x371A);
        return mk::continues(0x371D,mk::call_cycles);
    });
    add(menu::at0(0x3720),menu::equipment_window_unit,{mk::core_final_at(0x3737)},{.slot2_mapper=true,.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x3720,r.a,r.f);
        z.push(0x3721,r.a,r.f);
        z.add_register(0x3722,r.a);
        z.add_register(0x3723,r.a);
        z.add_register(0x3724,r.a);
        z.add_register(0x3725,r.a);
        z.load_hl(0x3726);
        z.store_to_hl(0x3729);
        z.load_hl(0x372B);
        z.add_register(0x372E,r.l);
        z.copy(0x372F,r.l,r.a);
        z.add_with_carry(0x3730,r.h);
        z.subtract_register(0x3731,r.l);
        z.copy(0x3732,r.h,r.a);
        z.load_immediate(0x3733,r.b);
        z.load_immediate(0x3735,r.c);
        return mk::continues(0x3737,mk::call_cycles);
    });
    add(menu::at0(0x373A),menu::equipment_window_unit,{mk::core_final_at(0x373C)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_immediate(0x373A,r.c);
        return mk::continues(0x373C,mk::call_cycles);
    });
    add(menu::at0(0x373F),menu::equipment_window_unit,{mk::core_final_at(0x3737),mk::core_final_at(0x3748)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.increment_pair(0x373F,r.h,r.l);
        if (z.decrement_jump(0x3740)) {
            z.load_immediate(0x3735,r.c);
            return mk::continues(0x3737,mk::call_cycles);
        }
        z.load_hl(0x3742);
        z.load_bc(0x3745);
        return mk::continues(0x3748,mk::call_cycles);
    });
    add(menu::at0(0x374B),menu::equipment_window_unit,{mk::returned_at(0x374C,+2)},{.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x374B,r.a,r.f);
        return mk::returns(0x374C,mk::return_cycles);
    });

// ---- 00:374D Closing the equipment window.
    add(menu::at0(0x374D),menu::equipment_window_close_unit,{mk::core_final_at(0x3756)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x374D);
        z.load_de(0x3750);
        z.load_bc(0x3753);
        return mk::continues(0x3756,mk::jump_cycles);
    });

// ---- 00:3759 The item actions window.
    add(menu::at0(0x3759),menu::item_actions_unit,{mk::core_final_at(0x3762)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3759);
        z.load_de(0x375C);
        z.load_bc(0x375F);
        return mk::continues(0x3762,mk::call_cycles);
    });
    add(menu::at0(0x3765),menu::item_actions_unit,{mk::core_final_at(0x3768)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3765);
        return mk::continues(0x3768,mk::jump_cycles);
    });

// ---- 00:376B Closing the item actions window.
    add(menu::at0(0x376B),menu::item_actions_close_unit,{mk::core_final_at(0x3774)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x376B);
        z.load_de(0x376E);
        z.load_bc(0x3771);
        return mk::continues(0x3774,mk::jump_cycles);
    });

// ---- 00:3777 The buy-or-sell window.
    add(menu::at0(0x3777),menu::buy_or_sell_unit,{mk::core_final_at(0x3780)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3777);
        z.load_de(0x377A);
        z.load_bc(0x377D);
        return mk::continues(0x3780,mk::call_cycles);
    });
    add(menu::at0(0x3783),menu::buy_or_sell_unit,{mk::core_final_at(0x3786)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3783);
        return mk::continues(0x3786,mk::call_cycles);
    });
    add(menu::at0(0x3789),menu::buy_or_sell_unit,{mk::core_final_at(0x3794)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x3789);
        z.store_hl_direct(0x378C);
        z.load_immediate(0x378F,r.a);
        z.store_a_direct(0x3791);
        return mk::continues(0x3794,mk::jump_cycles);
    });

// ---- 00:3797 Closing the buy-or-sell window.
    add(menu::at0(0x3797),menu::buy_or_sell_close_unit,{mk::core_final_at(0x37A0)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3797);
        z.load_de(0x379A);
        z.load_bc(0x379D);
        return mk::continues(0x37A0,mk::jump_cycles);
    });

// ---- 00:37A3 The yes-or-no window.
    add(menu::at0(0x37A3),menu::yes_no_window_unit,{mk::core_final_at(0x37AC)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x37A3);
        z.load_de(0x37A6);
        z.load_bc(0x37A9);
        return mk::continues(0x37AC,mk::call_cycles);
    });
    add(menu::at0(0x37AF),menu::yes_no_window_unit,{mk::core_final_at(0x37B2)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x37AF);
        return mk::continues(0x37B2,mk::call_cycles);
    });
    add(menu::at0(0x37B5),menu::yes_no_window_unit,{mk::core_final_at(0x37C0)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x37B5);
        z.store_hl_direct(0x37B8);
        z.load_immediate(0x37BB,r.a);
        z.store_a_direct(0x37BD);
        return mk::continues(0x37C0,mk::jump_cycles);
    });

// ---- 00:37C3 Closing the yes-or-no window.
    add(menu::at0(0x37C3),menu::yes_no_window_close_unit,{mk::core_final_at(0x37CC)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x37C3);
        z.load_de(0x37C6);
        z.load_bc(0x37C9);
        return mk::continues(0x37CC,mk::jump_cycles);
    });

// ---- 00:37CF The status window, field by field.
    add(menu::at0(0x37CF),menu::status_window_unit,{mk::core_final_at(0x37E7)},{.scratch_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.add_register(0x37CF,r.a);
        z.add_register(0x37D0,r.a);
        z.add_register(0x37D1,r.a);
        z.add_register(0x37D2,r.a);
        z.load_hl(0x37D3);
        z.add_register(0x37D6,r.l);
        z.copy(0x37D7,r.l,r.a);
        z.add_with_carry(0x37D8,r.h);
        z.subtract_register(0x37D9,r.l);
        z.copy(0x37DA,r.h,r.a);
        z.push(0x37DB,r.h,r.l);
        z.pop_index(0x37DC,mk::Index::ix);
        z.load_hl(0x37DE);
        z.load_de(0x37E1);
        z.load_bc(0x37E4);
        return mk::continues(0x37E7,mk::call_cycles);
    });
    add(menu::at0(0x37EA),menu::status_window_unit,{mk::core_final_at(0x37F0)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x37EA);
        z.load_bc(0x37ED);
        return mk::continues(0x37F0,mk::call_cycles);
    });
    add(menu::at0(0x37F3),menu::status_window_unit,{mk::core_final_at(0x37F9)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x37F3);
        z.load_from_indexed(0x37F6,mk::Index::ix,r.a);
        return mk::continues(0x37F9,mk::call_cycles);
    });
    add(menu::at0(0x37FC),menu::status_window_unit,{mk::core_final_at(0x3805)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x37FC);
        z.load_from_indexed(0x37FF,mk::Index::ix,r.c);
        z.load_from_indexed(0x3802,mk::Index::ix,r.b);
        return mk::continues(0x3805,mk::call_cycles);
    });
    add(menu::at0(0x3808),menu::status_window_unit,{mk::core_final_at(0x380E)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3808);
        z.load_bc(0x380B);
        return mk::continues(0x380E,mk::call_cycles);
    });
    add(menu::at0(0x3811),menu::status_window_unit,{mk::core_final_at(0x3817)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x3811);
        z.load_from_indexed(0x3814,mk::Index::ix,r.a);
        return mk::continues(0x3817,mk::call_cycles);
    });
    add(menu::at0(0x381A),menu::status_window_unit,{mk::core_final_at(0x3820)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x381A);
        z.load_bc(0x381D);
        return mk::continues(0x3820,mk::call_cycles);
    });
    add(menu::at0(0x3823),menu::status_window_unit,{mk::core_final_at(0x3829)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x3823);
        z.load_from_indexed(0x3826,mk::Index::ix,r.a);
        return mk::continues(0x3829,mk::call_cycles);
    });
    add(menu::at0(0x382C),menu::status_window_unit,{mk::core_final_at(0x3832)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x382C);
        z.load_bc(0x382F);
        return mk::continues(0x3832,mk::call_cycles);
    });
    add(menu::at0(0x3835),menu::status_window_unit,{mk::core_final_at(0x383B)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x3835);
        z.load_from_indexed(0x3838,mk::Index::ix,r.a);
        return mk::continues(0x383B,mk::call_cycles);
    });
    add(menu::at0(0x383E),menu::status_window_unit,{mk::core_final_at(0x3844)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x383E);
        z.load_bc(0x3841);
        return mk::continues(0x3844,mk::call_cycles);
    });
    add(menu::at0(0x3847),menu::status_window_unit,{mk::core_final_at(0x384D)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x3847);
        z.load_from_indexed(0x384A,mk::Index::ix,r.a);
        return mk::continues(0x384D,mk::call_cycles);
    });
    add(menu::at0(0x3850),menu::status_window_unit,{mk::core_final_at(0x3856)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3850);
        z.load_bc(0x3853);
        return mk::continues(0x3856,mk::call_cycles);
    });
    add(menu::at0(0x385C),menu::status_window_unit,{mk::core_final_at(0x3862)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x385C);
        z.load_bc(0x385F);
        return mk::continues(0x3862,mk::jump_cycles);
    });

// ---- 00:38C1 Closing the status window.
    add(menu::at0(0x38C1),menu::status_window_close_unit,{mk::core_final_at(0x38CA)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x38C1);
        z.load_de(0x38C4);
        z.load_bc(0x38C7);
        return mk::continues(0x38CA,mk::jump_cycles);
    });

// ---- 00:38CD Opening the shop list window.
    add(menu::at0(0x38CD),menu::shop_list_open_unit,{mk::core_final_at(0x38D6)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x38CD);
        z.load_de(0x38D0);
        z.load_bc(0x38D3);
        return mk::continues(0x38D6,mk::jump_cycles);
    });

// ---- 00:38D9 Drawing the shop list.
    add(menu::at0(0x38D9),menu::shop_list_unit,{mk::core_final_at(0x38E2)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x38D9);
        z.load_de(0x38DC);
        z.load_bc(0x38DF);
        return mk::continues(0x38E2,mk::call_cycles);
    });
    add(menu::at0(0x38E5),menu::shop_list_unit,{mk::core_final_at(0x3908,-6)},{.slot2_mapper=true,.scratch_stack=6},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x38E5);
        z.store_to_hl(0x38E8);
        z.load_a_direct(0x38EA);
        z.and_immediate(0x38ED);
        z.copy(0x38EF,r.l,r.a);
        z.load_immediate(0x38F0,r.h);
        z.add_to_hl(0x38F2,z.hl());
        z.copy(0x38F3,r.c,r.l);
        z.copy(0x38F4,r.b,r.h);
        z.add_to_hl(0x38F5,z.hl());
        z.add_to_hl(0x38F6,z.hl());
        z.add_to_hl(0x38F7,z.bc());
        z.load_bc(0x38F8);
        z.add_to_hl(0x38FB,z.bc());
        z.load_from_hl(0x38FC,r.a);
        z.store_a_direct(0x38FD);
        z.increment_pair(0x3900,r.h,r.l);
        z.push(0x3901,r.h,r.l);
        z.load_immediate(0x3902,r.b);
        z.push(0x3904,r.b,r.c);
        z.load_immediate(0x3905,r.c);
        z.push(0x3907,r.h,r.l);
        return mk::continues(0x3908,mk::call_cycles);
    });
    add(menu::at0(0x390B),menu::shop_list_unit,{mk::core_final_at(0x390F)},{.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x390B,r.h,r.l);
        z.load_immediate(0x390C,r.c);
        z.push(0x390E,r.h,r.l);
        return mk::continues(0x390F,mk::call_cycles);
    });
    add(menu::at0(0x3912),menu::shop_list_unit,{mk::core_final_at(0x3908),mk::core_final_at(0x391F,4)},{.inherited_stack=4},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x3912,r.h,r.l);
        z.increment_pair(0x3913,r.h,r.l);
        z.increment_pair(0x3914,r.h,r.l);
        z.increment_pair(0x3915,r.h,r.l);
        z.pop(0x3916,r.b,r.c);
        if (z.decrement_jump(0x3917)) {
            z.push(0x3904,r.b,r.c);
            z.load_immediate(0x3905,r.c);
            z.push(0x3907,r.h,r.l);
            return mk::continues(0x3908,mk::call_cycles);
        }
        z.load_hl(0x3919);
        z.load_bc(0x391C);
        return mk::continues(0x391F,mk::call_cycles);
    });
    add(menu::at0(0x3922),menu::shop_list_unit,{mk::core_final_at(0x3928)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3922);
        z.store_hl_direct(0x3925);
        return mk::continues(0x3928,mk::call_cycles);
    });
    add(menu::at0(0x392B),menu::shop_list_unit,{mk::returned_at(0x3939,+2)},{.slot2_mapper=true,.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x392B);
        z.store_to_hl(0x392E);
        z.pop(0x3930,r.h,r.l);
        z.copy(0x3931,r.b,r.a);
        z.add_register(0x3932,r.a);
        z.add_register(0x3933,r.b);
        z.add_register(0x3934,r.l);
        z.copy(0x3935,r.l,r.a);
        z.add_with_carry(0x3936,r.h);
        z.subtract_register(0x3937,r.l);
        z.copy(0x3938,r.h,r.a);
        return mk::returns(0x3939,mk::return_cycles);
    });

// ---- 00:3999 Closing the shop list window.
    add(menu::at0(0x3999),menu::shop_list_close_unit,{mk::core_final_at(0x39A2)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3999);
        z.load_de(0x399C);
        z.load_bc(0x399F);
        return mk::continues(0x39A2,mk::jump_cycles);
    });

// ---- 00:39A5 Opening the save slot window.
    add(menu::at0(0x39A5),menu::save_slots_open_unit,{mk::core_final_at(0x39AE)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x39A5);
        z.load_de(0x39A8);
        z.load_bc(0x39AB);
        return mk::continues(0x39AE,mk::call_cycles);
    });

// ---- 00:39B1 Drawing the save slot names from cartridge RAM.
    add(menu::at0(0x39B6),menu::save_slots_unit,{mk::core_final_at(0x39BF)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x39B6);
        z.load_de(0x39B9);
        z.load_bc(0x39BC);
        return mk::continues(0x39BF,mk::call_cycles);
    });
    add(menu::at0(0x39C7),menu::save_slots_unit,{mk::core_final_at(0x39D2)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x39C7);
        z.store_hl_direct(0x39CA);
        z.load_immediate(0x39CD,r.a);
        z.store_a_direct(0x39CF);
        return mk::continues(0x39D2,mk::call_cycles);
    });
    add(menu::at0(0x39D5),menu::save_slots_unit,{mk::returned_at(0x39DC)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.copy(0x39D5,r.l,r.a);
        z.increment(0x39D6,r.l);
        z.load_immediate(0x39D7,r.h);
        z.store_hl_direct(0x39D9);
        return mk::returns(0x39DC,mk::return_cycles);
    });

// ---- 00:39DD Closing the save slot window.
    add(menu::at0(0x39DD),menu::save_slots_close_unit,{mk::core_final_at(0x39E6)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x39DD);
        z.load_de(0x39E0);
        z.load_bc(0x39E3);
        return mk::continues(0x39E6,mk::jump_cycles);
    });

// ---- 00:39E9 Opening the money window.
    add(menu::at0(0x39E9),menu::money_window_open_unit,{mk::core_final_at(0x39F3,-2)},{.scratch_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.push(0x39E9,r.b,r.c);
        z.load_hl(0x39EA);
        z.load_de(0x39ED);
        z.load_bc(0x39F0);
        return mk::continues(0x39F3,mk::call_cycles);
    });
    add(menu::at0(0x39F6),menu::money_window_open_unit,{mk::core_final_at(0x3A01)},{.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x39F6,r.b,r.c);
        z.push(0x39F7,r.b,r.c);
        z.load_hl(0x39F8);
        z.load_de(0x39FB);
        z.load_bc(0x39FE);
        return mk::continues(0x3A01,mk::call_cycles);
    });

// ---- 00:39F7 Drawing the money window.
    add(menu::at0(0x39F7),menu::money_window_unit,{mk::core_final_at(0x3A01,-2)},{.scratch_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.push(0x39F7,r.b,r.c);
        z.load_hl(0x39F8);
        z.load_de(0x39FB);
        z.load_bc(0x39FE);
        return mk::continues(0x3A01,mk::call_cycles);
    });
    add(menu::at0(0x3A07),menu::money_window_unit,{mk::core_final_at(0x3A0D)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3A07);
        z.load_bc(0x3A0A);
        return mk::continues(0x3A0D,mk::call_cycles);
    });
    add(menu::at0(0x3A10),menu::money_window_unit,{mk::returned_at(0x3A11,+2)},{.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x3A10,r.b,r.c);
        return mk::returns(0x3A11,mk::return_cycles);
    });

// ---- 00:3A12 Closing the money window.
    add(menu::at0(0x3A12),menu::money_window_close_unit,{mk::core_final_at(0x3A1C,-2)},{.scratch_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.push(0x3A12,r.b,r.c);
        z.load_hl(0x3A13);
        z.load_de(0x3A16);
        z.load_bc(0x3A19);
        return mk::continues(0x3A1C,mk::call_cycles);
    });
    add(menu::at0(0x3A1F),menu::money_window_close_unit,{mk::returned_at(0x3A20,+2)},{.inherited_stack=2},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x3A1F,r.b,r.c);
        return mk::returns(0x3A20,mk::return_cycles);
    });

// ---- 00:3A21 The two-choice window.
    add(menu::at0(0x3A21),menu::two_choice_unit,{mk::core_final_at(0x3A2A)},{},[](auto& z) -> mk::Exit {
        z.load_hl(0x3A21);
        z.load_de(0x3A24);
        z.load_bc(0x3A27);
        return mk::continues(0x3A2A,mk::call_cycles);
    });
    add(menu::at0(0x3A2D),menu::two_choice_unit,{mk::core_final_at(0x3A35)},{.slot2_mapper=true},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_immediate(0x3A2D,r.a);
        z.store_a_direct(0x3A2F);
        z.load_hl(0x3A32);
        return mk::continues(0x3A35,mk::call_cycles);
    });
    add(menu::at0(0x3A38),menu::two_choice_unit,{mk::core_final_at(0x3A43)},{},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.load_hl(0x3A38);
        z.store_hl_direct(0x3A3B);
        z.load_immediate(0x3A3E,r.a);
        z.store_a_direct(0x3A40);
        return mk::continues(0x3A43,mk::call_cycles);
    });
    add(menu::at0(0x3A46),menu::two_choice_unit,{mk::core_final_at(0x3A51,-4)},{.scratch_stack=4},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.push(0x3A46,r.a,r.f);
        z.push(0x3A47,r.b,r.c);
        z.load_hl(0x3A48);
        z.load_de(0x3A4B);
        z.load_bc(0x3A4E);
        return mk::continues(0x3A51,mk::call_cycles);
    });
    add(menu::at0(0x3A54),menu::two_choice_unit,{mk::returned_at(0x3A56,+4)},{.inherited_stack=4},[](auto& z) -> mk::Exit {
        auto& r=z.r;
        z.pop(0x3A54,r.b,r.c);
        z.pop(0x3A55,r.a,r.f);
        return mk::returns(0x3A56,mk::return_cycles);
    });
}
}
