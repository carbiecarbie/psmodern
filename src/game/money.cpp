// SPDX-License-Identifier: GPL-3.0-or-later
// Shared party money addition. This routine also serves nonbattle callers;
// the optional battle multiplier therefore never belongs here.
// Milestone 14: both bodies render every instruction of their segment on the
// menu kit (the addition's return at 00:2988 and the battle's reward store at
// 01:60BC); every instruction of both is rendered. With the experience option on
// the reward store renders the same instruction with the scaled value.
#include "game/money.h"
#include "game/menu_kit.h"
#include <algorithm>
#include <cstdint>
#include <utility>

namespace psmodern::game {
namespace {
namespace k = menu_kit;
// Add HL to the party's money, saturating at the 16-bit maximum.
template <class Z> k::Exit add_money(Z &z) {
    z.exchange_de_hl(0x297A);
    z.load_hl_direct(0x297B);
    z.add_to_hl(0x297E, z.de());
    if (!z.relative_if(0x297F, k::Condition::nc))
        z.load_hl(0x2981);
    z.store_hl_direct(0x2984);
    z.exchange_de_hl(0x2987);
    return z.final_return(0x2988);
}
// The money variable, the saved amount and the stack are plain mapped memory
// (the original reads them; an unmapped address returns open-bus values the
// body does not model). The addresses are operands of the routine in ROM and
// the stack pointer is the caller's; none of them is changed by an interrupt
// handler, so the guard also holds across a delivery inside the body.
const k::Guard add_money_memory{"the money variable and the stack are mapped memory", [](const Machine &m) {
    auto word = [&](std::uint16_t at) { return static_cast<std::uint16_t>(m.peek(at) | (m.peek(at + 1) << 8)); };
    auto mapped = [&](unsigned at) {
        return m.memory().mapping(static_cast<std::uint16_t>(at)).kind != Memory::Kind::unmapped;
    };
    const auto source = word(0x297C), destination = word(0x2985);
    const auto sp = m.cpu().sp.uint16_value;
    return mapped(source) && mapped(source + 1u) && mapped(destination) && mapped(destination + 1u) &&
           mapped(sp) && mapped(sp + 1u);
}};
// The reward HL is stored at its work RAM address. With the experience
// option on, the stored value is the scaled reward and HL keeps its own.
template <class Z> k::Exit store_reward(Z &z, unsigned percent) {
    const auto at = battle_money_store.address;
    const auto h = z.r.h, l = z.r.l;
    if (percent != experience_percent_choices.original) {
        const auto value = std::min<unsigned long long>(0xffff, static_cast<unsigned long long>(z.hl()) * percent / 100);
        z.r.h = static_cast<std::uint8_t>(value >> 8);
        z.r.l = static_cast<std::uint8_t>(value);
    }
    z.store_hl_direct(at);
    z.r.h = h;
    z.r.l = l;
    return k::fall_through(at, static_cast<std::uint16_t>(at + 3));
}
} // namespace
void register_money_add(RoutineRegistry &registry) {
    k::add_segment(registry, money_add_unit, k::segment(money_add_unit, {k::returned_at(0x2988)}),
                   add_money_memory, [](auto &z) { return add_money(z); });
}
void register_battle_money_store(RoutineRegistry &registry, const Options &options) {
    const auto percent = options.experience_percent;
    auto spec = k::segment(battle_setup_unit, {k::core_final_at(battle_money_store.address)});
    spec.single_instruction = true;
    k::add_segment(registry, battle_money_store, std::move(spec), [percent](auto &z) { return store_reward(z, percent); });
}
} // namespace psmodern::game
