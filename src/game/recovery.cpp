// SPDX-License-Identifier: GPL-3.0-or-later
// HP recovery: selection and cancellation, MP debit, shared healing and food.
// Original operands (amounts, party fields and messages) come from the ROM.
#include "game/recovery.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace {
namespace k = menu_kit;
using k::Exit;

// The selected recipient and amount survive the existing character picker.
template <class Z> Exit pick_recipient(Z &z) {
    z.push(0x1E2D, z.r.b, z.r.c);
    z.push(0x1E2E, z.r.d, z.r.e);
    return z.final_call(0x1E2F);
}
template <class Z> Exit current_recipient(Z &z) {
    z.load_a_direct(0x1E4C);
    return z.final_call(0x1E4F);
}
template <class Z> Exit debit_magic(Z &z) {
    z.push(0x1E53, z.r.d, z.r.e);
    z.copy(0x1E54, z.r.a, z.r.b);
    return z.final_call(0x1E55);
}
template <class Z> Exit announce_healing(Z &z) {
    z.push(0x1E5F, z.r.d, z.r.e);
    z.load_hl(0x1E60);
    return z.final_call(0x1E63);
}
template <class Z> Exit food_return(Z &z) {
    z.load_a_direct(0x2361);
    z.or_a(0x2364);
    if (auto exit=z.final_return_if(0x2365,k::Condition::nz)) return *exit;
    return z.final_jump(0x2366);
}
template <class Z> Exit food_target(Z &z) {
    z.store_a_direct(0x234B);
    return z.final_call(0x234E);
}
template <class Z> Exit food_begin(Z &z) {
    z.load_a_direct(0x2339);
    z.or_a(0x233C);
    z.load_a_direct(0x233D);
    if (z.relative_if(0x2340, !z.zero()))
        return food_target(z);
    z.push(0x2342, z.r.d, z.r.e);
    return z.final_call(0x2343);
}
template <class Z> Exit remaining_magic_points(Z &z) {
    auto &r = z.r;
    z.load_hl(0x1B87);
    z.add_register(0x1B8A, r.l);
    z.copy(0x1B8B, r.l, r.a);
    z.add_with_carry(0x1B8C, r.h);
    z.subtract_register(0x1B8D, r.l);
    z.copy(0x1B8E, r.h, r.a);
    z.load_a_direct(0x1B8F);
    for (unsigned at = 0x1B92; at <= 0x1B95; ++at)
        z.add_register(at, r.a);
    z.load_de(0x1B96);
    z.add_register(0x1B99, r.e);
    z.copy(0x1B9A, r.e, r.a);
    z.load_a_from_pair(0x1B9B, z.de());
    z.bus().fetch(0x1B9C);
    r.a = kit::subtract_flags(r, r.a, z.bus().read(z.hl()), 0);
    z.bus().advance(7);
    return z.final_return(0x1B9D);
}

template <class Z> Exit choose_small_healing(Z &z) {
    z.load_immediate(0x1E27, z.r.d);
    z.relative(0x1E29);
    return pick_recipient(z);
}

template <class Z> Exit choose_large_healing(Z &z) {
    z.load_immediate(0x1E2B, z.r.d);
    return pick_recipient(z);
}

template <class Z> Exit accept_healing_recipient(Z &z) {
    z.pop(0x1E32, z.r.d, z.r.e);
    z.test_bit(0x1E33, 4, z.r.c);
    z.pop(0x1E35, z.r.b, z.r.c);
    if (z.relative_if(0x1E36, !z.zero()))
        return z.final_jump(0x1E43);
    z.store_a_direct(0x1E38);
    return z.final_call(0x1E3B);
}

template <class Z> Exit check_healing_recipient(Z &z) {
    if (z.relative_if(0x1E3E, z.zero()))
        return z.final_jump(0x1E43);
    return z.final_call(0x1E40);
}

template <class Z> Exit small_healing_on_current_recipient(Z &z) {
    z.load_immediate(0x1E46, z.r.d);
    z.relative(0x1E48);
    return current_recipient(z);
}

template <class Z> Exit large_healing_on_current_recipient(Z &z) {
    z.load_immediate(0x1E4A, z.r.d);
    return current_recipient(z);
}

template <class Z> Exit check_current_healing_recipient(Z &z) {
    if (auto exit=z.final_return_if(0x1E52,k::Condition::z)) return *exit;
    return debit_magic(z);
}

template <class Z> Exit spend_healing_points(Z &z) { return debit_magic(z); }

template <class Z> Exit commit_healing_points(Z &z) {
    z.store_a_to_pair(0x1E58, z.de());
    z.load_immediate(0x1E59, z.r.a);
    z.store_a_direct(0x1E5B);
    z.pop(0x1E5E, z.r.d, z.r.e);
    return announce_healing(z);
}

template <class Z> Exit show_healing_message(Z &z) { return announce_healing(z); }

template <class Z> Exit request_healing_sound(Z &z) {
    z.pop(0x1E66, z.r.d, z.r.e);
    z.load_immediate(0x1E67, z.r.a);
    z.store_a_direct(0x1E69);
    z.load_a_direct(0x1E6C);
    return z.final_call(0x1E6F);
}

template <class Z> Exit restore_hit_points(Z &z) {
    z.push(0x1E72, z.r.h, z.r.l);
    z.pop_index(0x1E73, k::Index::ix);
    z.load_from_indexed(0x1E75, k::Index::ix, z.r.b);
    z.load_from_indexed(0x1E78, k::Index::ix, z.r.a);
    z.add_register(0x1E7B, z.r.d);
    if (!z.relative_if(0x1E7C, !z.carry()))
        z.load_immediate(0x1E7E, z.r.a);
    z.compare_register(0x1E80, z.r.b);
    if (!z.relative_if(0x1E81, z.carry()))
        z.copy(0x1E83, z.r.a, z.r.b);
    z.store_to_indexed(0x1E84, k::Index::ix, z.r.a);
    return z.final_jump(0x1E87);
}

template <class Z> Exit accept_food_recipient(Z &z) {
    z.pop(0x2346, z.r.d, z.r.e);
    z.test_bit(0x2347, 4, z.r.c);
    if (z.relative_if(0x2349, !z.zero()))
        return food_return(z);
    return food_target(z);
}

template <class Z> Exit check_food_recipient(Z &z) {
    if (z.relative_if(0x2351, z.zero()))
        return food_return(z);
    z.push(0x2353, z.r.d, z.r.e);
    z.load_hl(0x2354);
    return z.final_call(0x2357);
}

template <class Z> Exit apply_food_healing(Z &z) {
    z.pop(0x235A, z.r.d, z.r.e);
    return z.final_call(0x235B);
}

template <class Z> Exit finish_food_use(Z &z) { return food_return(z); }
} // namespace
void register_recovery(RoutineRegistry &registry) {
    using k::core_final_at;
    using k::returned_at;
    using k::segment;
    auto add = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                   k::Permissions permissions, auto logic) {
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)},
                       segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions), logic);
    };

    // Instructions that ran between the pieces below: each is a segment of its
    // own. Calls need room for the return address.
    auto single = [&](unsigned entry, unsigned unit, std::initializer_list<SegmentExit> exits,
                      k::Permissions permissions, auto logic) {
        auto spec = segment({0, static_cast<std::uint16_t>(unit)}, exits, permissions);
        spec.single_instruction = true;
        k::add_segment(registry, {0, static_cast<std::uint16_t>(entry)}, std::move(spec), logic);
    };
    // After the MP lookup returns to its callers, they draw the item window.
    single(0x1B9E, 0x1B87, {core_final_at(0x1B9E)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x1B9E); });
    single(0x1BA1, 0x1B87, {core_final_at(0x1BA1)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x1BA1); });
    // After the healing message, the window is closed by a jump.
    single(0x1E43, 0x1E27, {core_final_at(0x1E43)}, {}, [](auto &z) { return z.final_jump(0x1E43); });
    // After the food message, one more call closes the use.
    single(0x235E, 0x2333, {core_final_at(0x235E)}, {.scratch_stack = 2}, [](auto &z) { return z.final_call(0x235E); });

    // The MP lookup computes the remaining MP and its flags; the caller
    // decides whether to store it.
    add(0x1B87, 0x1B87, {returned_at(0x1B9D)}, {},
        [](auto &z) { return remaining_magic_points(z); });
    add(0x1E27, 0x1E27, {core_final_at(0x1E2F, -4)}, {.scratch_stack = 4},
        [](auto &z) { return choose_small_healing(z); });
    add(0x1E2B, 0x1E2B, {core_final_at(0x1E2F, -4)}, {.scratch_stack = 4},
        [](auto &z) { return choose_large_healing(z); });
    add(0x1E32, 0x1E27, {core_final_at(0x1E43, 4), core_final_at(0x1E3B, 4)},
        {.inherited_stack = 4}, [](auto &z) { return accept_healing_recipient(z); });
    add(0x1E3E, 0x1E27, {core_final_at(0x1E43), core_final_at(0x1E40)}, {},
        [](auto &z) { return check_healing_recipient(z); });
    add(0x1E46, 0x1E46, {core_final_at(0x1E4F)}, {},
        [](auto &z) { return small_healing_on_current_recipient(z); });
    add(0x1E4A, 0x1E4A, {core_final_at(0x1E4F)}, {},
        [](auto &z) { return large_healing_on_current_recipient(z); });
    add(0x1E52, 0x1E46, {returned_at(0x1E52), core_final_at(0x1E55, -2)}, {.scratch_stack = 2},
        [](auto &z) { return check_current_healing_recipient(z); });
    add(0x1E53, 0x1E53, {core_final_at(0x1E55, -2)}, {.scratch_stack = 2},
        [](auto &z) { return spend_healing_points(z); });
    add(0x1E58, 0x1E53, {core_final_at(0x1E63)}, {.scratch_stack = 2, .inherited_stack = 2},
        [](auto &z) { return commit_healing_points(z); });
    add(0x1E5F, 0x1E5F, {core_final_at(0x1E63, -2)}, {.scratch_stack = 2},
        [](auto &z) { return show_healing_message(z); });
    add(0x1E66, 0x1E5F, {core_final_at(0x1E6F, 2)}, {.inherited_stack = 2},
        [](auto &z) { return request_healing_sound(z); });

    // Clamp first on byte overflow, then on the recipient's maximum HP.
    add(0x1E72, 0x1E5F, {core_final_at(0x1E87)}, {.scratch_stack = 2},
        [](auto &z) { return restore_hit_points(z); });
    for (auto entry : {0x2333u, 0x2337u})
        add(entry, entry, {core_final_at(0x2343, -2), core_final_at(0x234E)}, {.scratch_stack = 2},
            [entry](auto &z) -> Exit {
                z.load_immediate(entry, z.r.d);
                if (entry == 0x2333)
                    z.relative(0x2335);
                return food_begin(z);
            });
    add(0x2346, 0x2333,
        {returned_at(0x2365, 2), core_final_at(0x2366, 2), core_final_at(0x234E, 2)},
        {.inherited_stack = 2}, [](auto &z) { return accept_food_recipient(z); });
    add(0x2351, 0x2333, {returned_at(0x2365), core_final_at(0x2366), core_final_at(0x2357, -2)},
        {.scratch_stack = 2}, [](auto &z) { return check_food_recipient(z); });
    add(0x235A, 0x2333, {core_final_at(0x235B, 2)}, {.inherited_stack = 2},
        [](auto &z) { return apply_food_healing(z); });
    add(0x2361, 0x2333, {returned_at(0x2365), core_final_at(0x2366)}, {},
        [](auto &z) { return finish_food_use(z); });
}
} // namespace psmodern::game
