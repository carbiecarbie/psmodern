// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/battle_magic.h"
#include "menu_probe.h"
#include "c4_sweep.h"
#include <bit>
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_battle_magic(registry);
    probe::check(registry.entries().size() == game::battle_magic::segments.size(),
                 "magic registration count");
    for (const auto &s : game::battle_magic::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "magic ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::battle_magic::segments) {
        for (unsigned seed = 0; seed < 1024; ++seed) {
            auto machine =
                harness.prepare(s.entry, {.r = static_cast<std::uint8_t>(seed), .seed = seed});
            for (unsigned address = 0xC000; address < 0xD000; ++address)
                machine.memory().write(address,
                                       static_cast<std::uint8_t>(seed * 17 + address * 23));
            machine.memory().write(0xC267, seed % 4);
            machine.cpu().hl.uint16_value = 0xC440 + 16 * (seed % 8);
            machine.cpu().de.uint16_value = 0xC402;
            machine.cpu().bc.uint8_values.at_1 = 1 + seed % 8;
            harness.run_cpp(machine, s.entry);
            ++count;
        }
        auto boundary = harness.prepare(s.entry, {});
        menu_probe::place_before_boundary(boundary, 1);
        harness.run_fallback(boundary, s.entry, RoutineFallback::frame);
    }
    return count;
}
void complete_effect(probe::Harness &harness, const Machine &base, unsigned kind, unsigned mask,
                     bool special) {
    constexpr std::uint16_t entries[]{0x1EA7, 0x1EE6, 0x1F25, 0x1EEE};
    auto machine =
        map_probe::prepare_at(base, {0, entries[kind]},
                              {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = mask});
    machine.memory().write(0xC267, 0);
    machine.memory().write(0xC2C2, 0);
    machine.memory().write(0xC402, 100);
    machine.memory().write(0xC2E6, special ? 0x49 : 1);
    machine.cpu().ix_iy[1].uint16_value = 0xC400;
    const unsigned spell =
        kind == 3 ? 0 : battle_effect_probe::spell_id(harness.rom(), entries[kind]);
    machine.cpu().bc.uint8_values.at_1 = spell;
    machine.cpu().de.uint16_value = 0xF010;
    for (unsigned enemy = 0; enemy < 8; ++enemy) {
        for (unsigned field = 0; field < 16; ++field)
            machine.memory().write(0xC440 + 16 * enemy + field, 0);
        machine.memory().write(0xC440 + 16 * enemy, (mask >> enemy) & 1);
        machine.memory().write(0xC441 + 16 * enemy, ((mask >> enemy) & 1) ? 100 : 0);
    }
    const std::string context = "offensive kind=" + std::to_string(kind) +
                                " mask=" + std::to_string(mask) +
                                " special=" + std::to_string(special);
    battle_effect_probe::compare_call(machine, harness.registry(), battle_effect_probe::press_both,
                                      context);
    probe::check(machine.peek(0xC402) ==
                     (kind == 3 ? 100 : 100 - battle_effect_probe::cost(harness.rom(), spell)),
                 "one MP debit per spell, none for weapon");
    unsigned changed = 0, total_damage = 0;
    for (unsigned enemy = 0; enemy < 8; ++enemy) {
        const auto hp = machine.peek(0xC441 + 16 * enemy);
        if ((mask >> enemy) & 1) {
            changed += hp != 100;
            total_damage += 100 - hp;
        } else
            probe::check(hp == 0, "inactive foe untouched");
    }
    if (!mask)
        probe::check(changed == 0, "no target means no damage");
    else if (kind == 0)
        probe::check(total_damage >= 14 && total_damage <= 20, "double strike total damage");
    else if (kind == 2)
        probe::check(total_damage >= 27 && total_damage <= 36, "triple strike total damage");
    else {
        const unsigned recipients = special ? 1 : std::popcount(mask);
        probe::check(changed == recipients, "group effect recipient count");
        if (kind == 3)
            probe::check(total_damage == 16 * recipients, "fixed weapon damage");
    }
}
int scenarios(probe::Harness &harness, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-battle"));
    const auto image = map_probe::start_image(inputs, "first-battle", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "magic-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from,
                            [](const Machine &m) {
                                return m.peek(0xC29D) != 0 && m.peek(0xC268) != 0 &&
                                       m.peek(0xC26E) == 4;
                            }),
                 "magic real battle frame");
    unsigned count = 0;
    for (unsigned kind = 0; kind < 4; ++kind)
        for (unsigned mask : {0u, 1u, 0xAAu, 0xFFu})
            for (bool special : {false, true}) {
                complete_effect(harness, scene.machine(), kind, mask, special);
                ++count;
            }
    return count;
}
} // namespace
int main(int argc, char **argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        registration();
        if (argc == 1)
            return 0;
        const auto inputs = map_probe::load(argc, argv);
        if (!inputs)
            return 77;
        probe::Harness harness(inputs->rom);
        {
            probe::Harness sweeping(inputs->rom);
            c4::sweep_file(sweeping,"battle_magic",game::battle_magic::segments);
        }
        const auto count = state_probes(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Offensive probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
