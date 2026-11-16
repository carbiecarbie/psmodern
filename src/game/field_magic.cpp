// SPDX-License-Identifier: GPL-3.0-or-later
// Field spell effects: trap removal/inspection, dungeon exit, magic door and
// transfer request. Shared item entry points bypass the spell's MP debit.
#include "game/field_magic.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::continues;
using k::Exit;
using k::returns;

template <class Z> Exit spend_magic_points(Z &z, unsigned at) {
    z.copy(at, z.r.a, z.r.b);
    return continues(at + 1, k::call_cycles);
}
template <class Z> Exit disarm_chest_message(Z &z) {
    z.load_a_direct(0x204C);
    z.compare_immediate(0x204F);
    z.load_hl(0x2051);
    if (!z.relative_if(0x2054, z.zero()))
        z.load_hl(0x2056);
    return continues(0x2059, k::call_cycles);
}
template <class Z> Exit begin_disarm(Z &z) {
    z.store_a_to_pair(0x2020, z.de());
    z.load_immediate(0x2021, z.r.a);
    z.store_a_direct(0x2023);
    z.load_a_direct(0x2026);
    z.compare_immediate(0x2029);
    if (z.relative_if(0x202B, z.zero()))
        return disarm_chest_message(z);
    z.load_a_direct(0x202D);
    z.or_a(0x2030);
    z.load_hl(0x2031);
    if (z.relative_if(0x2034, !z.zero()))
        return continues(0x2046, k::call_cycles);
    return continues(0x2036, k::call_cycles);
}
// The dungeon helper returns C as the cell index and distinguishes a pitfall
// from an object record. Only a positive result clears the cell to floor.
template <class Z> Exit disarm_pitfall(Z &z) {
    z.load_hl(0x2039);
    if (z.relative_if(0x203C, z.zero()))
        return continues(0x2046, k::call_cycles);
    z.copy(0x203E, z.r.l, z.r.c);
    z.load_immediate(0x203F, z.r.h);
    z.store_to_hl(0x2041);
    z.load_hl(0x2043);
    return continues(0x2046, k::call_cycles);
}
template <class Z> Exit disarm_chest(Z &z) {
    z.load_immediate(0x205C, z.r.a);
    z.store_a_direct(0x205E);
    return continues(0x2061, k::jump_cycles);
}
template <class Z> Exit dungeon_exit_message(Z &z) {
    z.load_immediate(0x2078, z.r.a);
    z.store_a_direct(0x207A);
    z.load_hl(0x207D);
    return continues(0x2080, k::call_cycles);
}
template <class Z> Exit begin_dungeon_exit(Z &z) {
    z.store_a_to_pair(0x2068, z.de());
    z.load_a_direct(0x2069);
    z.or_a(0x206C);
    if (z.relative_if(0x206D, z.zero()))
        return dungeon_exit_message(z);
    z.load_hl(0x206F);
    return continues(0x2072, k::call_cycles);
}
template <class Z> Exit request_dungeon_exit(Z &z) {
    z.load_immediate(0x2086, z.r.a);
    z.store_a_direct(0x2088);
    z.load_hl(0x208B);
    z.store_to_hl(0x208E);
    return returns(0x2090, k::return_cycles);
}
template <class Z> Exit magic_door_failure(Z &z) {
    z.load_hl(0x20A1);
    return continues(0x20A4, k::call_cycles);
}
template <class Z> Exit begin_magic_door(Z &z) {
    z.store_a_to_pair(0x2095, z.de());
    z.load_immediate(0x2096, z.r.a);
    z.store_a_direct(0x2098);
    z.load_a_direct(0x209B);
    z.or_a(0x209E);
    if (!z.relative_if(0x209F, z.zero()))
        return magic_door_failure(z);
    z.load_immediate(0x20AA, z.r.b);
    return continues(0x20AC, k::call_cycles);
}
template <class Z> Exit accept_magic_door(Z &z) {
    z.and_immediate(0x20AF);
    z.compare_immediate(0x20B1);
    if (z.relative_if(0x20B3, !z.zero()))
        return magic_door_failure(z);
    z.test_bit_at_hl(0x20B5, 7);
    if (z.relative_if(0x20B7, !z.zero()))
        return magic_door_failure(z);
    z.load_immediate(0x20B9, z.r.a);
    z.store_a_direct(0x20BB);
    return returns(0x20BE, k::return_cycles);
}
template <class Z> Exit inspect_chest(Z &z) {
    z.load_a_direct(0x2128);
    z.compare_immediate(0x212B);
    z.load_hl(0x212D);
    if (!z.relative_if(0x2130, z.zero()))
        z.load_hl(0x2132);
    return continues(0x2135, k::call_cycles);
}
template <class Z> Exit inspect_target(Z &z) {
    z.load_a_direct(0x2102);
    z.compare_immediate(0x2105);
    if (z.relative_if(0x2107, z.zero()))
        return inspect_chest(z);
    z.load_a_direct(0x2109);
    z.or_a(0x210C);
    z.load_hl(0x210D);
    if (z.relative_if(0x2110, !z.zero()))
        return continues(0x211D, k::call_cycles);
    return continues(0x2112, k::call_cycles);
}
template <class Z> Exit begin_inspection(Z &z) {
    z.store_a_to_pair(0x20FC, z.de());
    z.load_immediate(0x20FD, z.r.a);
    z.store_a_direct(0x20FF);
    return inspect_target(z);
}
template <class Z> Exit inspect_pitfall(Z &z) {
    z.load_hl(0x2115);
    if (!z.relative_if(0x2118, z.zero()))
        z.load_hl(0x211A);
    return continues(0x211D, k::call_cycles);
}
template <class Z> Exit finish_inspection(Z &z) {
    z.load_immediate(0x2120, z.r.a);
    z.store_a_direct(0x2122);
    return continues(0x2125, k::jump_cycles);
}
template <class Z> Exit inspect_then_open_chest(Z &z) {
    z.load_immediate(0x2138, z.r.a);
    z.store_a_direct(0x213A);
    return continues(0x213D, k::jump_cycles);
}
template <class Z> Exit transfer_message(Z &z) {
    z.load_hl(0x2159);
    return continues(0x215C, k::call_cycles);
}
template <class Z> Exit begin_transfer(Z &z) {
    z.store_a_to_pair(0x2144, z.de());
    z.load_immediate(0x2145, z.r.a);
    z.store_a_direct(0x2147);
    z.load_a_direct(0x214A);
    z.or_a(0x214D);
    if (z.relative_if(0x214E, !z.zero()))
        return transfer_message(z);
    z.load_hl(0x2150);
    return continues(0x2153, k::call_cycles);
}
template <class Z> Exit request_transfer(Z &z) {
    z.load_immediate(0x2162, z.r.a);
    z.store_a_direct(0x2164);
    return returns(0x2167, k::return_cycles);
}
} // namespace
void register_field_magic(RoutineRegistry &registry) {
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions), logic);
    };
    add(0x201C, 0x201C, {core_final_at(0x201D)}, {},
        [](auto &z) { return spend_magic_points(z, 0x201C); });
    add(0x2020, 0x201C, {core_final_at(0x2059), core_final_at(0x2046), core_final_at(0x2036)}, {},
        [](auto &z) { return begin_disarm(z); });
    add(0x2039, 0x201C, {core_final_at(0x2046)}, {}, [](auto &z) { return disarm_pitfall(z); });
    add(0x205C, 0x201C, {core_final_at(0x2061)}, {}, [](auto &z) { return disarm_chest(z); });
    add(0x2064, 0x2064, {core_final_at(0x2065)}, {},
        [](auto &z) { return spend_magic_points(z, 0x2064); });
    add(0x2068, 0x2064, {core_final_at(0x2080), core_final_at(0x2072)}, {},
        [](auto &z) { return begin_dungeon_exit(z); });
    add(0x2078, 0x2078, {core_final_at(0x2080)}, {},
        [](auto &z) { return dungeon_exit_message(z); });
    add(0x2086, 0x2078, {returned_at(0x2090)}, {}, [](auto &z) { return request_dungeon_exit(z); });
    add(0x2091, 0x2091, {core_final_at(0x2092)}, {},
        [](auto &z) { return spend_magic_points(z, 0x2091); });
    add(0x2095, 0x2091, {core_final_at(0x20AC), core_final_at(0x20A4)}, {},
        [](auto &z) { return begin_magic_door(z); });
    add(0x20AF, 0x2091, {returned_at(0x20BE), core_final_at(0x20A4)}, {},
        [](auto &z) { return accept_magic_door(z); });
    add(0x20F8, 0x20F8, {core_final_at(0x20F9)}, {},
        [](auto &z) { return spend_magic_points(z, 0x20F8); });
    const auto inspect_exits = std::initializer_list<SegmentExit>{
        core_final_at(0x2135), core_final_at(0x211D), core_final_at(0x2112)};
    add(0x20FC, 0x20F8, inspect_exits, {}, [](auto &z) { return begin_inspection(z); });
    add(0x2102, 0x2102, inspect_exits, {}, [](auto &z) { return inspect_target(z); });
    add(0x2115, 0x2102, {core_final_at(0x211D)}, {}, [](auto &z) { return inspect_pitfall(z); });
    add(0x2120, 0x2102, {core_final_at(0x2125)}, {}, [](auto &z) { return finish_inspection(z); });
    add(0x2138, 0x2102, {core_final_at(0x213D)}, {},
        [](auto &z) { return inspect_then_open_chest(z); });
    add(0x2140, 0x2140, {core_final_at(0x2141)}, {},
        [](auto &z) { return spend_magic_points(z, 0x2140); });
    add(0x2144, 0x2140, {core_final_at(0x215C), core_final_at(0x2153)}, {},
        [](auto &z) { return begin_transfer(z); });
    add(0x2159, 0x2159, {core_final_at(0x215C)}, {}, [](auto &z) { return transfer_message(z); });
    add(0x2162, 0x2159, {returned_at(0x2167)}, {}, [](auto &z) { return request_transfer(z); });
}
} // namespace psmodern::game
