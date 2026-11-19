// SPDX-License-Identifier: GPL-3.0-or-later
// Battle endings, field restoration, treasure and party unlock.
#include "game/joins.h"
#include "game/battle_outcomes.h"
#include "game/menu_kit.h"
#include "game/routines.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;
template <class Z> Exit prepare_field_objects(Z &);
template <class Z> Exit choose_field_restore(Z &);
template <class Z> Exit wait_field_restore(Z &);
template <class Z> Exit begin_defeat(Z &);
template <class Z> Exit report_defeat(Z &);
template <class Z> Exit report_game_over(Z &);
template <class Z> Exit request_title(Z &);
template <class Z> Exit begin_escape_restore(Z &);
template <class Z> Exit finish_escape_restore(Z &);
template <class Z> Exit begin_victory(Z &);
template <class Z> Exit report_victory(Z &);
template <class Z> Exit wait_late_victory(Z &);
template <class Z> Exit announce_victory(Z &);
template <class Z> Exit check_battle_treasure(Z &);
template <class Z> Exit select_treasure_graphics(Z &);
template <class Z> Exit prepare_treasure_palette(Z &);
template <class Z> Exit load_treasure_graphics(Z &);
template <class Z> Exit prepare_treasure_objects(Z &);
template <class Z> Exit create_treasure_object(Z &);
template <class Z> Exit wait_treasure_object(Z &);
template <class Z> Exit unlock_party_member(Z &);
template <class Z> Exit fill_unlocked_resources(Z &);
template <class Z> Exit prepare_field_objects(Z &z) {
    z.load_hl(0x15DC);
    z.load_de(0x15DF);
    z.load_bc(0x15E2);
    z.store_to_hl(0x15E5);
    return k::fall_through(0x15E5, 0x15E7);
}
template <class Z> Exit choose_field_restore(Z &z) {
    z.load_a_direct(0x15EC);
    z.or_a(0x15EF);
    return z.final_jump_if(0x15F0,k::Condition::nz);
}
template <class Z> Exit wait_field_restore(Z &z) {
    z.load_immediate(0x15FC, z.r.a);
    return z.final_call(0x15FE);
}
template <class Z> Exit begin_defeat(Z &z) {
    z.load_a_direct(0x1602);
    z.compare_immediate(0x1605);
    if (!z.relative_if(0x1607, z.zero())) {
        z.compare_immediate(0x1609);
        if (z.relative_if(0x160B, !z.zero()))
            return report_defeat(z);
    }
    z.load_immediate(0x160D, z.r.a);
    z.store_a_direct(0x160F);
    return z.final_return(0x1612);
}
template <class Z> Exit report_defeat(Z &z) {
    z.compare_immediate(0x1613);
    if (!z.relative_if(0x1615, !z.zero())) {
        z.load_hl(0x1617);
        z.load_immediate(0x161A, z.r.b);
        do {
            z.store_to_hl(0x161C);
            z.increment_hl(0x161E);
        } while (z.decrement_jump(0x161F));
    }
    z.load_immediate(0x1621, z.r.a);
    z.store_a_direct(0x1623);
    z.load_a_direct(0x1626);
    z.or_a(0x1629);
    z.load_hl(0x162A);
    return z.final_call_if(0x162D,k::Condition::nz);
}
template <class Z> Exit report_game_over(Z &z) {
    z.load_hl(0x1630);
    return z.final_call(0x1633);
}
template <class Z> Exit request_title(Z &z) {
    z.load_hl(0x1636);
    z.store_to_hl(0x1639);
    return z.final_jump(0x163B);
}
template <class Z> Exit begin_escape_restore(Z &z) {
    z.push(0x163E, z.r.a, z.r.f);
    return z.final_call(0x163F);
}
template <class Z> Exit finish_escape_restore(Z &z) {
    z.load_immediate(0x1645, z.r.a);
    z.store_a_direct(0x1647);
    z.pop(0x164A, z.r.a, z.r.f);
    z.compare_immediate(0x164B);
    if (auto exit=z.final_return_if(0x164D,k::Condition::nz)) return *exit;
    z.load_a_direct(0x164E);
    z.or_a(0x1651);
    if (auto exit=z.final_return_if(0x1652,k::Condition::nz)) return *exit;
    return z.final_jump(0x1653);
}
template <class Z> Exit begin_victory(Z &z) {
    z.load_a_direct(0x1656);
    z.compare_immediate(0x1659);
    if (z.relative_if(0x165B, !z.zero()))
        return report_victory(z);
    z.load_immediate(0x165D, z.r.a);
    z.store_a_direct(0x165F);
    return z.final_return(0x1662);
}
template <class Z> Exit report_victory(Z &z) {
    z.compare_immediate(0x1663);
    if (!z.relative_if(0x1665, !z.zero())) {
        z.load_hl(0x1667);
        z.load_immediate(0x166A, z.r.b);
        do {
            z.store_to_hl(0x166C);
            z.increment_hl(0x166E);
        } while (z.decrement_jump(0x166F));
    }
    z.load_immediate(0x1671, z.r.a);
    z.store_a_direct(0x1673);
    return z.final_call(0x1676);
}
template <class Z> Exit wait_late_victory(Z &z) {
    z.load_a_direct(0x1679);
    z.compare_immediate(0x167C);
    if (!z.relative_if(0x167E, z.zero())) {
        z.compare_immediate(0x1680);
        if (z.relative_if(0x1682, !z.zero()))
            return announce_victory(z);
    }
    z.load_immediate(0x1684, z.r.b);
    return z.final_call(0x1686);
}
template <class Z> Exit announce_victory(Z &z) {
    z.load_immediate(0x1689, z.r.a);
    z.store_a_direct(0x168B);
    z.load_hl(0x168E);
    return z.final_call(0x1691);
}
template <class Z> Exit check_battle_treasure(Z &z) {
    z.load_hl_direct(0x169A);
    z.load_a_direct(0x169D);
    z.or_register(0x16A0, z.r.l);
    z.or_register(0x16A1, z.r.h);
    if (auto exit=z.final_return_if(0x16A2,k::Condition::z)) return *exit;
    z.load_hl(0x16A3);
    return z.final_call(0x16A6);
}
template <class Z> Exit select_treasure_graphics(Z &z) {
    z.load_hl(0x16B2);
    z.store_to_hl(0x16B5);
    z.load_hl(0x16B7);
    z.load_de(0x16BA);
    z.load_bc(0x16BD);
    return k::fall_through(0x16BD, 0x16C0);
}
template <class Z> Exit prepare_treasure_palette(Z &z) {
    z.load_hl(0x16C2);
    z.load_de(0x16C5);
    z.load_bc(0x16C8);
    return k::fall_through(0x16C8, 0x16CB);
}
template <class Z> Exit load_treasure_graphics(Z &z) {
    z.load_hl(0x16CD);
    z.load_de(0x16D0);
    return z.final_call(0x16D3);
}
template <class Z> Exit prepare_treasure_objects(Z &z) {
    z.load_hl(0x16D6);
    z.load_de(0x16D9);
    z.load_bc(0x16DC);
    z.store_to_hl(0x16DF);
    return k::fall_through(0x16DF, 0x16E1);
}
template <class Z> Exit create_treasure_object(Z &z) {
    z.load_immediate(0x16E3, z.r.a);
    z.store_a_direct(0x16E5);
    return z.final_call(0x16E8);
}
template <class Z> Exit wait_treasure_object(Z &z) {
    z.load_immediate(0x16EB, z.r.a);
    return z.final_call(0x16ED);
}
template <class Z> Exit unlock_party_member(Z &z) {
    z.store_immediate_indexed(0x16F1, k::Index::iy);
    z.store_immediate_indexed(0x16F5, k::Index::iy);
    z.push_index(0x16F9, k::Index::iy);
    return z.final_call(0x16FB);
}
template <class Z> Exit fill_unlocked_resources(Z &z) {
    z.pop_index(0x16FE, k::Index::iy);
    z.load_from_indexed(0x1700, k::Index::iy, z.r.a);
    z.store_to_indexed(0x1703, k::Index::iy, z.r.a);
    z.load_from_indexed(0x1706, k::Index::iy, z.r.a);
    z.store_to_indexed(0x1709, k::Index::iy, z.r.a);
    return z.final_return(0x170C);
}
} // namespace
void register_battle_outcomes(RoutineRegistry &registry) {
    using k::conditional_at;
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto field_seed = core_final_at(0x15E5);
    field_seed.wram_seed = 0xC800;
    field_seed.seed_target = SeedTarget::sprite_shadow_zero;
    auto treasure_seed = core_final_at(0x16DF);
    treasure_seed.wram_seed = 0xC800;
    treasure_seed.seed_target = SeedTarget::sprite_shadow_zero;
    // The two work RAM clears (the field objects at C800 and the treasure
    // objects) are rendered only while the buffer is plain work RAM; the
    // mapping of C800 is fixed by the hardware, so it also holds across an
    // interrupt delivered inside the body.
    static const k::Guard work_ram{"C800 is plain work RAM", [](const Machine &m) {
        return m.memory().mapping(0xC800).kind == Memory::Kind::ram;
    }};
    auto add = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        auto spec = segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions);
        if (entry == 0x15DC || entry == 0x16D6)
            k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)}, spec, work_ram, logic);
        else
            k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)}, spec, logic);
    };
    // Instructions that ran between two pieces: each is a segment of its own.
    auto single = [&](unsigned entry, unsigned unit, SegmentExit exit, k::Permissions permissions, auto logic) {
        auto spec = segment({0, static_cast<std::uint16_t>(unit)}, {exit}, permissions);
        spec.single_instruction = true;
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)}, std::move(spec), logic);
    };
    add(0x15DC, 0x15D9, {field_seed}, {}, [](auto &z) { return prepare_field_objects(z); });
    add(0x15EC, 0x15D9, {conditional_at(0x15F0)}, {},
        [](auto &z) { return choose_field_restore(z); });
    add(0x15FC, 0x15D9, {core_final_at(0x15FE, 0)}, {},
        [](auto &z) { return wait_field_restore(z); });
    add(0x1602, 0x1602, {returned_at(0x1612, 0), core_final_at(0x162D, 0)}, {},
        [](auto &z) { return begin_defeat(z); });
    add(0x1613, 0x1602, {core_final_at(0x162D, 0)}, {}, [](auto &z) { return report_defeat(z); });
    add(0x1630, 0x1602, {core_final_at(0x1633, 0)}, {},
        [](auto &z) { return report_game_over(z); });
    add(0x1636, 0x1602, {core_final_at(0x163B, 0)}, {}, [](auto &z) { return request_title(z); });
    add(0x163E, 0x163E, {core_final_at(0x163F, -2)}, {.scratch_stack = 2},
        [](auto &z) { return begin_escape_restore(z); });
    add(0x1645, 0x163E, {returned_at(0x164D, 2), returned_at(0x1652, 2), core_final_at(0x1653, 2)},
        {.inherited_stack = 2}, [](auto &z) { return finish_escape_restore(z); });
    add(0x1656, 0x1656, {returned_at(0x1662, 0), core_final_at(0x1676, 0)}, {},
        [](auto &z) { return begin_victory(z); });
    add(0x1663, 0x1656, {core_final_at(0x1676, 0)}, {}, [](auto &z) { return report_victory(z); });
    add(0x1679, 0x1656, {core_final_at(0x1686, 0), core_final_at(0x1691, 0)}, {},
        [](auto &z) { return wait_late_victory(z); });
    add(0x1689, 0x1656, {core_final_at(0x1691, 0)}, {},
        [](auto &z) { return announce_victory(z); });
    add(0x169A, 0x1656, {returned_at(0x16A2, 0), core_final_at(0x16A6, 0)}, {},
        [](auto &z) { return check_battle_treasure(z); });
    add(0x16B2, 0x16B2, {core_final_at(0x16BD, 0)}, {.slot2_mapper = true},
        [](auto &z) { return select_treasure_graphics(z); });
    add(0x16C2, 0x16B2, {core_final_at(0x16C8, 0)}, {},
        [](auto &z) { return prepare_treasure_palette(z); });
    add(0x16CD, 0x16B2, {core_final_at(0x16D3, 0)}, {},
        [](auto &z) { return load_treasure_graphics(z); });
    add(0x16D6, 0x16B2, {treasure_seed}, {}, [](auto &z) { return prepare_treasure_objects(z); });
    add(0x16E3, 0x16B2, {core_final_at(0x16E8, 0)}, {},
        [](auto &z) { return create_treasure_object(z); });
    add(0x16EB, 0x16B2, {core_final_at(0x16ED, 0)}, {},
        [](auto &z) { return wait_treasure_object(z); });
    add(0x16F1, 0x16F1, {core_final_at(0x16FB, -2)}, {.scratch_stack = 2},
        [](auto &z) { return unlock_party_member(z); });
    add(0x16FE, 0x16F1, {returned_at(0x170C, 2)}, {.inherited_stack = 2},
        [](auto &z) { return fill_unlocked_resources(z); });
    // The field restore helper, and the calls and jump that follow the
    // object update and the two ways the field is redrawn.
    single(0x15D9, 0x15D9, core_final_at(0x15D9), {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x15D9); });
    single(0x15F3, 0x15D9, core_final_at(0x15F3), {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x15F3); });
    single(0x15F6, 0x15D9, core_final_at(0x15F6), {}, [](auto &z) { return z.final_jump(0x15F6); });
    single(0x15F9, 0x15D9, core_final_at(0x15F9), {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x15F9); });
    single(0x1601, 0x15D9, returned_at(0x1601), {}, [](auto &z) { return z.final_return(0x1601); });
    // Victory: after the dialogue, the experience award and the party stats.
    single(0x1694, 0x1656, core_final_at(0x1694), {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x1694); });
    single(0x1697, 0x1656, core_final_at(0x1697), {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x1697); });
    // Treasure: after the reward dialogue, the graphics setup, the wait for
    // a button, and the hand-off to the chest opening.
    single(0x16A9, 0x1656, core_final_at(0x16A9), {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x16A9); });
    single(0x16AC, 0x1656, core_final_at(0x16AC), {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x16AC); });
    single(0x16AF, 0x1656, core_final_at(0x16AF), {}, [](auto &z) { return z.final_jump(0x16AF); });
    single(0x16F0, 0x16B2, returned_at(0x16F0), {}, [](auto &z) { return z.final_return(0x16F0); });
    for (auto entry : battle_outcomes::copy_entries)
        register_block_transfer(registry, entry);
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_battle_outcomes_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{0,0x1642},{0,0x163E},{kit::core_final_at(0x1642)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x1642); });
}
} // namespace psmodern::game
