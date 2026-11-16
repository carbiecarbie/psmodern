// SPDX-License-Identifier: GPL-3.0-or-later
// Offensive spell effects and the shared multi-target weapon effect. MP lookup,
// messages, damage application and window updates retain their existing owners.
#include "game/battle_magic.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::continues;
using k::Exit;
using k::returns;

template <class Z> Exit spend_offensive_magic(Z &z, unsigned at) {
    z.copy(at, z.r.a, z.r.b);
    return continues(at + 1, k::call_cycles);
}
// This spell deliberately calls the shared strike once and then falls into
// it a second time. Keeping the original return address preserves both hits.
template <class Z> Exit begin_double_strike(Z &z) {
    z.store_a_to_pair(0x1EAB, z.de());
    z.load_de(0x1EAC);
    return continues(0x1EAF, k::call_cycles);
}
template <class Z> Exit find_enemy_candidate(Z &z) {
    z.copy(0x1EB4, z.r.a, z.r.b);
    z.subtract_immediate(0x1EB5);
    z.negate(0x1EB7);
    return continues(0x1EB9, k::call_cycles);
}
template <class Z> Exit begin_single_strike(Z &z) {
    z.load_immediate(0x1EB2, z.r.b);
    return find_enemy_candidate(z);
}
template <class Z> Exit accept_enemy_candidate(Z &z) {
    if (z.relative_if(0x1EBC, !z.zero())) {
        z.push(0x1EC1, z.r.d, z.r.e);
        z.copy(0x1EC2, z.r.a, z.r.e);
        return continues(0x1EC3, k::call_cycles);
    }
    if (z.decrement_jump(0x1EBE))
        return find_enemy_candidate(z);
    return returns(0x1EC0, k::return_cycles);
}
template <class Z> Exit choose_random_recipient(Z &z) {
    z.and_immediate(0x1EC9);
    z.add_immediate(0x1ECB);
    return continues(0x1ECD, k::call_cycles);
}
// Inactive recipients cause a new draw. The amount/effect pair survives the
// recipient check and is saved again while the damage-variation draw runs.
template <class Z> Exit accept_random_recipient(Z &z) {
    if (z.jump_if(0x1ED0, z.zero()))
        return continues(0x1EC6, k::call_cycles);
    z.push(0x1ED3, z.r.h, z.r.l);
    z.pop_index(0x1ED4, k::Index::ix);
    z.pop(0x1ED6, z.r.d, z.r.e);
    z.push(0x1ED7, z.r.d, z.r.e);
    return continues(0x1ED8, k::call_cycles);
}
template <class Z> Exit vary_single_strike(Z &z) {
    z.and_immediate(0x1EDB);
    z.add_register(0x1EDD, z.r.d);
    return continues(0x1EDE, k::call_cycles);
}
template <class Z> Exit finish_single_strike(Z &z) {
    z.pop(0x1EE4, z.r.d, z.r.e);
    return returns(0x1EE5, k::return_cycles);
}
template <class Z> Exit next_group_candidate(Z &z) {
    z.push(0x1EF0, z.r.b, z.r.c);
    z.copy(0x1EF1, z.r.a, z.r.b);
    z.subtract_immediate(0x1EF2);
    z.negate(0x1EF4);
    return continues(0x1EF6, k::call_cycles);
}
template <class Z> Exit begin_group_strike(Z &z) {
    z.load_immediate(0x1EEE, z.r.b);
    return next_group_candidate(z);
}
template <class Z> Exit begin_group_magic(Z &z) {
    z.store_a_to_pair(0x1EEA, z.de());
    z.load_de(0x1EEB);
    return begin_group_strike(z);
}
template <class Z> Exit finish_group_candidate(Z &z) {
    z.pop(0x1F1F, z.r.b, z.r.c);
    if (z.decrement_jump(0x1F20))
        return next_group_candidate(z);
    return returns(0x1F22, k::return_cycles);
}
template <class Z> Exit accept_group_recipient(Z &z) {
    if (z.jump_if(0x1EF9, z.zero()))
        return finish_group_candidate(z);
    z.push(0x1EFC, z.r.h, z.r.l);
    z.pop_index(0x1EFD, k::Index::ix);
    z.push(0x1EFF, z.r.d, z.r.e);
    z.copy(0x1F00, z.r.a, z.r.e);
    return continues(0x1F01, k::call_cycles);
}
// The shared weapon path has a fixed amount; the group spell varies it.
template <class Z> Exit choose_group_variation(Z &z) {
    z.pop(0x1F04, z.r.d, z.r.e);
    z.push(0x1F05, z.r.d, z.r.e);
    z.copy(0x1F06, z.r.a, z.r.d);
    z.compare_immediate(0x1F07);
    if (z.relative_if(0x1F09, !z.zero()))
        return continues(0x1F11, k::call_cycles);
    return continues(0x1F0B, k::call_cycles);
}
template <class Z> Exit vary_group_strike(Z &z) {
    z.and_immediate(0x1F0E);
    z.add_register(0x1F10, z.r.d);
    return continues(0x1F11, k::call_cycles);
}
// Encounter49 stops the group effect after this recipient. Yield the original
// conditional branch so both outcomes keep the same interpreter boundary.
template <class Z> Exit check_group_stop(Z &z) {
    z.pop(0x1F17, z.r.d, z.r.e);
    z.load_a_direct(0x1F18);
    z.compare_immediate(0x1F1B);
    return continues(0x1F1D, z.zero() ? 12 : 7);
}
template <class Z> Exit finish_special_group(Z &z) {
    z.pop(0x1F23, z.r.b, z.r.c);
    return returns(0x1F24, k::return_cycles);
}
template <class Z> Exit begin_triple_strike(Z &z) {
    z.store_a_to_pair(0x1F29, z.de());
    z.load_de(0x1F2A);
    return continues(0x1F2D, k::call_cycles);
}
} // namespace
void register_battle_magic(RoutineRegistry &registry) {
    using k::conditional_at;
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions), logic);
    };
    add(0x1EA7, 0x1EA7, {core_final_at(0x1EA8)}, {},
        [](auto &z) { return spend_offensive_magic(z, 0x1EA7); });
    add(0x1EAB, 0x1EA7, {core_final_at(0x1EAF)}, {},
        [](auto &z) { return begin_double_strike(z); });
    add(0x1EB2, 0x1EB2, {core_final_at(0x1EB9)}, {},
        [](auto &z) { return begin_single_strike(z); });
    add(0x1EBC, 0x1EB2, {core_final_at(0x1EC3, -2), core_final_at(0x1EB9), returned_at(0x1EC0)},
        {.scratch_stack = 2}, [](auto &z) { return accept_enemy_candidate(z); });
    add(0x1EC9, 0x1EB2, {core_final_at(0x1ECD)}, {.inherited_stack = 2},
        [](auto &z) { return choose_random_recipient(z); });
    add(0x1ED0, 0x1EB2, {core_final_at(0x1EC6), core_final_at(0x1ED8)},
        {.scratch_stack = 2, .inherited_stack = 2},
        [](auto &z) { return accept_random_recipient(z); });
    add(0x1EDB, 0x1EB2, {core_final_at(0x1EDE)}, {.inherited_stack = 2},
        [](auto &z) { return vary_single_strike(z); });
    add(0x1EE4, 0x1EB2, {returned_at(0x1EE5, 2)}, {.inherited_stack = 2},
        [](auto &z) { return finish_single_strike(z); });
    add(0x1EE6, 0x1EE6, {core_final_at(0x1EE7)}, {},
        [](auto &z) { return spend_offensive_magic(z, 0x1EE6); });
    add(0x1EEA, 0x1EE6, {core_final_at(0x1EF6, -2)}, {.scratch_stack = 2},
        [](auto &z) { return begin_group_magic(z); });
    add(0x1EEE, 0x1EEE, {core_final_at(0x1EF6, -2)}, {.scratch_stack = 2},
        [](auto &z) { return begin_group_strike(z); });
    add(0x1EF0, 0x1EEE, {core_final_at(0x1EF6, -2)}, {.scratch_stack = 2},
        [](auto &z) { return next_group_candidate(z); });
    add(0x1EF9, 0x1EEE, {core_final_at(0x1EF6), core_final_at(0x1F01, -2), returned_at(0x1F22, 2)},
        {.scratch_stack = 2, .inherited_stack = 2},
        [](auto &z) { return accept_group_recipient(z); });
    add(0x1F04, 0x1EEE, {core_final_at(0x1F11), core_final_at(0x1F0B)},
        {.scratch_stack = 2, .inherited_stack = 4},
        [](auto &z) { return choose_group_variation(z); });
    add(0x1F0E, 0x1EEE, {core_final_at(0x1F11)}, {.inherited_stack = 4},
        [](auto &z) { return vary_group_strike(z); });
    add(0x1F17, 0x1EEE, {conditional_at(0x1F1D, 2)}, {.inherited_stack = 4},
        [](auto &z) { return check_group_stop(z); });
    add(0x1F1F, 0x1EEE, {core_final_at(0x1EF6), returned_at(0x1F22, 2)},
        {.scratch_stack = 2, .inherited_stack = 2},
        [](auto &z) { return finish_group_candidate(z); });
    add(0x1F23, 0x1EEE, {returned_at(0x1F24, 2)}, {.inherited_stack = 2},
        [](auto &z) { return finish_special_group(z); });
    add(0x1F25, 0x1F25, {core_final_at(0x1F26)}, {},
        [](auto &z) { return spend_offensive_magic(z, 0x1F25); });
    add(0x1F29, 0x1F25, {core_final_at(0x1F2D)}, {},
        [](auto &z) { return begin_triple_strike(z); });
}
} // namespace psmodern::game
