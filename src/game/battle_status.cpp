// SPDX-License-Identifier: GPL-3.0-or-later
// Protection, blocking/weakening/boost effects, shared escape and revival.
// Resource order is intentional: revival debits before its recipient picker.
#include "game/joins.h"
#include "game/battle_status.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;

template <class Z> Exit spend_magic_points(Z &z, unsigned at) {
    z.copy(at, z.r.a, z.r.b);
    return z.final_call(at + 1);
}
template <class Z> Exit begin_protection(Z &z, bool enhanced) {
    if (enhanced)
        z.load_immediate(0x1E8E, z.r.c);
    else {
        z.load_immediate(0x1E8A, z.r.c);
        z.relative(0x1E8C);
    }
    z.copy(0x1E90, z.r.a, z.r.b);
    return z.final_call(0x1E91);
}
template <class Z> Exit apply_protection(Z &z) {
    z.store_a_to_pair(0x1E94, z.de());
    z.load_immediate(0x1E95, z.r.a);
    z.store_a_direct(0x1E97);
    z.copy(0x1E9A, z.r.a, z.r.c);
    z.store_a_direct(0x1E9B);
    z.load_hl(0x1E9E);
    return z.final_call(0x1EA1);
}
template <class Z> Exit blocking_failure(Z &z) {
    z.load_hl(0x1F71);
    z.relative(0x1F74);
    return z.final_call(0x1F7A);
}
template <class Z> Exit blocking_candidate(Z &z) {
    z.copy(0x1F60, z.r.a, z.r.b);
    z.subtract_immediate(0x1F61);
    z.negate(0x1F63);
    return z.final_call(0x1F65);
}
template <class Z> Exit find_unblocked_enemy(Z &z) {
    z.load_de(0x1F5B);
    z.load_immediate(0x1F5E, z.r.b);
    return blocking_candidate(z);
}
// These strength gates use the first party/enemy attack fields, not the
// currently casting record. The weak-side draw chooses zero or 1-3 counter units.
template <class Z> Exit check_blocking_permission(Z &z) {
    z.store_a_to_pair(0x1F3A, z.de());
    z.load_immediate(0x1F3B, z.r.a);
    z.store_a_direct(0x1F3D);
    z.load_a_direct(0x1F40);
    z.and_immediate(0x1F43);
    if (z.relative_if(0x1F45, z.zero()))
        return blocking_failure(z);
    z.load_a_direct(0x1F47);
    z.copy(0x1F4A, z.r.b, z.r.a);
    z.load_a_direct(0x1F4B);
    z.compare_register(0x1F4E, z.r.b);
    z.load_immediate(0x1F4F, z.r.c);
    if (z.relative_if(0x1F51, !z.carry()))
        return find_unblocked_enemy(z);
    return z.final_call(0x1F53);
}
template <class Z> Exit choose_blocking_duration(Z &z) {
    z.and_immediate(0x1F56);
    if (z.relative_if(0x1F58, z.zero()))
        return blocking_failure(z);
    z.copy(0x1F5A, z.r.c, z.r.a);
    return find_unblocked_enemy(z);
}
template <class Z> Exit accept_blocking_recipient(Z &z) {
    if (!z.relative_if(0x1F68, z.zero())) {
        z.add_to_hl(0x1F6A, z.de());
        z.load_from_hl(0x1F6B, z.r.a);
        z.or_a(0x1F6C);
        if (z.relative_if(0x1F6D, z.zero())) {
            z.store_register_to_hl(0x1F76, z.r.c);
            z.load_hl(0x1F77);
            return z.final_call(0x1F7A);
        }
    }
    if (z.decrement_jump(0x1F6F))
        return blocking_candidate(z);
    return blocking_failure(z);
}
template <class Z> Exit escape_failure(Z &z) {
    z.load_a_direct(0x1F9A);
    z.or_a(0x1F9D);
    z.load_hl(0x1F9E);
    if (!z.relative_if(0x1FA1, z.zero()))
        z.load_hl(0x1FA3);
    return z.final_call(0x1FA6);
}
template <class Z> Exit escape_success(Z &z) {
    z.load_immediate(0x1FAC, z.r.a);
    z.store_a_direct(0x1FAE);
    z.load_hl(0x1FB1);
    return z.final_call(0x1FB4);
}
// Shared with item paths: the caller decides whether MP or an item was spent.
template <class Z> Exit check_escape_permission(Z &z) {
    z.load_a_direct(0x1F89);
    z.or_a(0x1F8C);
    if (z.relative_if(0x1F8D, z.zero()))
        return escape_failure(z);
    z.load_a_direct(0x1F8F);
    z.or_a(0x1F92);
    if (z.relative_if(0x1F93, !z.zero()))
        return escape_success(z);
    return z.final_call(0x1F95);
}
template <class Z> Exit begin_escape_spell(Z &z) {
    z.store_a_to_pair(0x1F84, z.de());
    z.xor_a(0x1F85);
    z.store_a_direct(0x1F86);
    return check_escape_permission(z);
}
template <class Z> Exit check_escape_space(Z &z) {
    if (z.relative_if(0x1F98, z.zero()))
        return escape_success(z);
    return escape_failure(z);
}
template <class Z> Exit finish_escape(Z &z) {
    z.load_immediate(0x1FBA, z.r.a);
    z.store_a_direct(0x1FBC);
    return z.final_return(0x1FBF);
}
template <class Z> Exit begin_attack_boost(Z &z) {
    z.store_a_to_pair(0x1FC4, z.de());
    z.load_immediate(0x1FC5, z.r.a);
    z.store_a_direct(0x1FC7);
    z.load_a_direct(0x1FCA);
    return z.final_call(0x1FCD);
}
template <class Z> Exit check_boost_recipient(Z &z) {
    if (auto exit=z.final_return_if(0x1FD0,k::Condition::z)) return *exit;
    return z.final_call(0x1FD1);
}
template <class Z> Exit apply_attack_boost(Z &z) {
    z.set_bit_at_hl(0x1FD4, 7);
    z.load_hl(0x1FD6);
    return z.final_call(0x1FD9);
}
template <class Z> Exit weakening_failure(Z &z) {
    z.load_hl(0x200C);
    z.relative(0x200F);
    return z.final_call(0x2016);
}
template <class Z> Exit weakening_candidate(Z &z) {
    z.copy(0x1FFC, z.r.a, z.r.b);
    z.subtract_immediate(0x1FFD);
    z.negate(0x1FFF);
    return z.final_call(0x2001);
}
template <class Z> Exit check_weakening_strength(Z &z) {
    z.store_a_to_pair(0x1FE3, z.de());
    z.load_immediate(0x1FE4, z.r.a);
    z.store_a_direct(0x1FE6);
    z.load_a_direct(0x1FE9);
    z.copy(0x1FEC, z.r.b, z.r.a);
    z.load_a_direct(0x1FED);
    z.compare_register(0x1FF0, z.r.b);
    if (z.relative_if(0x1FF1, z.carry()))
        return weakening_failure(z);
    return z.final_call(0x1FF3);
}
template <class Z> Exit accept_weakening_draw(Z &z) {
    z.compare_immediate(0x1FF6);
    if (z.relative_if(0x1FF8, !z.carry()))
        return weakening_failure(z);
    z.load_immediate(0x1FFA, z.r.b);
    return weakening_candidate(z);
}
template <class Z> Exit accept_weakening_recipient(Z &z) {
    if (!z.relative_if(0x2004, z.zero())) {
        z.test_bit_at_hl(0x2006, 6);
        if (z.relative_if(0x2008, z.zero())) {
            z.set_bit_at_hl(0x2011, 6);
            z.load_hl(0x2013);
            return z.final_call(0x2016);
        }
    }
    if (z.decrement_jump(0x200A))
        return weakening_candidate(z);
    return weakening_failure(z);
}
// The revival cost is committed before asking who should be restored.
template <class Z> Exit spend_revival_points(Z &z) {
    z.store_a_to_pair(0x20C3, z.de());
    return z.final_call(0x20C4);
}
template <class Z> Exit accept_revival_choice(Z &z) {
    z.test_bit(0x20C7, 4, z.r.c);
    if (z.relative_if(0x20C9, !z.zero()))
        return z.final_jump(0x20F5);
    z.push(0x20CB, z.r.a, z.r.f);
    z.load_immediate(0x20CC, z.r.a);
    z.store_a_direct(0x20CE);
    z.pop(0x20D1, z.r.a, z.r.f);
    z.store_a_direct(0x20D2);
    return z.final_call(0x20D5);
}
template <class Z> Exit restore_inactive_recipient(Z &z) {
    if (!z.relative_if(0x20D8, z.zero())) {
        z.load_hl(0x20DA);
        z.relative(0x20DD);
        return z.final_call(0x20EF);
    }
    z.store_to_hl(0x20DF);
    z.load_immediate(0x20E1, z.r.a);
    z.add_register(0x20E3, z.r.l);
    z.copy(0x20E4, z.r.e, z.r.a);
    z.copy(0x20E5, z.r.d, z.r.h);
    z.exchange_de_hl(0x20E6);
    z.increment_pair(0x20E7, z.r.d, z.r.e);
    z.load_increment(0x20E8);
    z.load_increment(0x20EA);
    z.load_hl(0x20EC);
    return z.final_call(0x20EF);
}
} // namespace
void register_battle_status(RoutineRegistry &registry) {
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions), logic);
    };
    add(0x1E8A, 0x1E8A, {core_final_at(0x1E91)}, {},
        [](auto &z) { return begin_protection(z, false); });
    add(0x1E8E, 0x1E8E, {core_final_at(0x1E91)}, {},
        [](auto &z) { return begin_protection(z, true); });
    add(0x1E94, 0x1E8A, {core_final_at(0x1EA1)}, {}, [](auto &z) { return apply_protection(z); });
    add(0x1F36, 0x1F36, {core_final_at(0x1F37)}, {},
        [](auto &z) { return spend_magic_points(z, 0x1F36); });
    add(0x1F3A, 0x1F36, {core_final_at(0x1F7A), core_final_at(0x1F53), core_final_at(0x1F65)}, {},
        [](auto &z) { return check_blocking_permission(z); });
    add(0x1F56, 0x1F36, {core_final_at(0x1F7A), core_final_at(0x1F65)}, {},
        [](auto &z) { return choose_blocking_duration(z); });
    add(0x1F68, 0x1F36, {core_final_at(0x1F7A), core_final_at(0x1F65)}, {},
        [](auto &z) { return accept_blocking_recipient(z); });
    add(0x1F80, 0x1F80, {core_final_at(0x1F81)}, {},
        [](auto &z) { return spend_magic_points(z, 0x1F80); });
    const auto escape_exits = std::initializer_list<SegmentExit>{
        core_final_at(0x1FA6), core_final_at(0x1FB4), core_final_at(0x1F95)};
    add(0x1F84, 0x1F80, escape_exits, {}, [](auto &z) { return begin_escape_spell(z); });
    add(0x1F89, 0x1F89, escape_exits, {}, [](auto &z) { return check_escape_permission(z); });
    add(0x1F98, 0x1F89, {core_final_at(0x1FA6), core_final_at(0x1FB4)}, {},
        [](auto &z) { return check_escape_space(z); });
    add(0x1FAC, 0x1F89, {core_final_at(0x1FB4)}, {}, [](auto &z) { return escape_success(z); });
    add(0x1FBA, 0x1F89, {returned_at(0x1FBF)}, {}, [](auto &z) { return finish_escape(z); });
    add(0x1FC0, 0x1FC0, {core_final_at(0x1FC1)}, {},
        [](auto &z) { return spend_magic_points(z, 0x1FC0); });
    add(0x1FC4, 0x1FC0, {core_final_at(0x1FCD)}, {}, [](auto &z) { return begin_attack_boost(z); });
    add(0x1FD0, 0x1FC0, {returned_at(0x1FD0), core_final_at(0x1FD1)}, {},
        [](auto &z) { return check_boost_recipient(z); });
    add(0x1FD4, 0x1FC0, {core_final_at(0x1FD9)}, {}, [](auto &z) { return apply_attack_boost(z); });
    add(0x1FDF, 0x1FDF, {core_final_at(0x1FE0)}, {},
        [](auto &z) { return spend_magic_points(z, 0x1FDF); });
    add(0x1FE3, 0x1FDF, {core_final_at(0x2016), core_final_at(0x1FF3)}, {},
        [](auto &z) { return check_weakening_strength(z); });
    add(0x1FF6, 0x1FDF, {core_final_at(0x2016), core_final_at(0x2001)}, {},
        [](auto &z) { return accept_weakening_draw(z); });
    add(0x2004, 0x1FDF, {core_final_at(0x2016), core_final_at(0x2001)}, {},
        [](auto &z) { return accept_weakening_recipient(z); });
    add(0x20BF, 0x20BF, {core_final_at(0x20C0)}, {},
        [](auto &z) { return spend_magic_points(z, 0x20BF); });
    add(0x20C3, 0x20BF, {core_final_at(0x20C4)}, {},
        [](auto &z) { return spend_revival_points(z); });
    add(0x20C7, 0x20BF, {core_final_at(0x20F5), core_final_at(0x20D5)}, {.scratch_stack = 2},
        [](auto &z) { return accept_revival_choice(z); });
    add(0x20D8, 0x20BF, {core_final_at(0x20EF)}, {},
        [](auto &z) { return restore_inactive_recipient(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_battle_status_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{0,0x1EA4},{0,0x1E8A},{kit::core_final_at(0x1EA4)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x1EA4); });
    kit::add_joining(registry,{0,0x1F7D},{0,0x1F36},{kit::core_final_at(0x1F7D)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x1F7D); });
    kit::add_joining(registry,{0,0x1FA9},{0,0x1F80},{kit::core_final_at(0x1FA9)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x1FA9); });
    kit::add_joining(registry,{0,0x1FB7},{0,0x1F80},{kit::core_final_at(0x1FB7)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x1FB7); });
    kit::add_joining(registry,{0,0x1FDC},{0,0x1FC0},{kit::core_final_at(0x1FDC)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x1FDC); });
    kit::add_joining(registry,{0,0x2019},{0,0x1FDF},{kit::core_final_at(0x2019)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x2019); });
    kit::add_joining(registry,{0,0x20F2},{0,0x20BF},{kit::core_final_at(0x20F2)},kit::call_cycles,2,[](auto& z) { return z.final_call(0x20F2); });
    kit::add_joining(registry,{0,0x20F5},{0,0x20BF},{kit::core_final_at(0x20F5)},kit::jump_cycles,0,[](auto& z) { return z.final_jump(0x20F5); });
}
} // namespace psmodern::game
