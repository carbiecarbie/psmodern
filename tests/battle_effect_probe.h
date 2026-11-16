// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "probe.h"
#include <string>

namespace psmodern::battle_effect_probe {
// Tables contain addresses and costs supplied by the player's image. Match
// the effect rather than assigning an unrelated spell's cost to a scenario.
inline unsigned spell_id(const probe::Rom &rom, std::uint16_t effect) {
    for (const auto [table, count] : {std::pair{0x1A8Au, 18u}, std::pair{0x1DFEu, 19u}})
        for (unsigned id = 0; id < count; ++id) {
            const auto at = static_cast<std::uint16_t>(table + 2 * id);
            const auto target =
                static_cast<std::uint16_t>(rom.byte(0, at) | (rom.byte(0, at + 1) << 8));
            if (target == effect)
                return id;
        }
    throw std::runtime_error("effect absent from both spell tables");
}
inline unsigned cost(const probe::Rom &rom, unsigned spell) {
    probe::check(spell < 19, "spell cost index");
    return rom.byte(0, static_cast<std::uint16_t>(0x1DDC + spell));
}

// Compare a whole effect while feeding identical input at each replacement
// boundary. The independent interpreter advances to the exact same cycle.
// Ordered sound writes are checked at every sample point; full state is
// checked at completed ordinary instructions and always at the final return.
template <class Controls>
void compare_call(Machine &machine, RoutineRegistry &registry, Controls controls,
                  const std::string &context, std::uint16_t stop = 0x0180) {
    Machine original = machine;
    (void)machine.take_psg_writes();
    (void)machine.take_samples();
    (void)original.take_psg_writes();
    (void)original.take_samples();
    auto compare = [&](bool final) {
        const auto expected = original.take_psg_writes(), actual = machine.take_psg_writes();
        probe::check(expected == actual, "effect ordered sound writes/cycles: " + context);
        (void)original.take_samples();
        (void)machine.take_samples();
        // Cpu::differences documents decoder scratch which is overwritten by
        // the next decode. Prefix/HALT continuations are not stable snapshot
        // points; the per-call verifier still checks every declared segment.
        const auto opcode = machine.cpu().data.uint8_array[0];
        const bool stable = machine.cpu().resume == 0 && opcode != 0xCB && opcode != 0xED &&
                            (opcode & 0xDF) != 0xDD;
        if (final || stable) {
            std::string fields;
            for (const auto &difference : original.differences(machine))
                fields += " " + difference.field;
            probe::check(fields.empty(), "effect state differs at cycle " +
                                             std::to_string(machine.cycles()) + ": " + context +
                                             fields);
        }
    };
    for (unsigned steps = 0; machine.cpu().pc.uint16_value != stop; ++steps) {
        probe::check(steps < 30000000, "effect failed to return: " + context);
        const Pad input = controls(machine);
        machine.set_pad(0, input);
        original.set_pad(0, input);
        try {
            machine.step(registry);
        } catch (const std::exception &error) {
            throw std::runtime_error(std::string(error.what()) + " [" + context + "]");
        }
        while (original.cycles() < machine.cycles())
            original.step();
        probe::check(original.cycles() == machine.cycles(),
                     "effect instruction boundary differs: " + context);
        if (steps % 256 == 0)
            compare(false);
    }
    compare(true);
}
inline Pad press_both(const Machine &machine) {
    const bool press = ((machine.cycles() / Machine::frame_cycles) / 3) % 2;
    return Pad{.one = press, .two = press};
}
} // namespace psmodern::battle_effect_probe
