// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/battle_actor.h"
#include "menu_probe.h"
#include <bit>
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_battle_actor(registry);
    probe::check(registry.entries().size() == game::battle_actor::segments.size(),
                 "actor registration count");
    for (const auto &s : game::battle_actor::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "actor ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::battle_actor::segments) {
        for (unsigned seed = 0; seed < 1024; ++seed) {
            auto machine =
                harness.prepare(s.entry, {.r = static_cast<std::uint8_t>(seed), .seed = seed});
            for (unsigned address = 0xC000; address < 0xD000; ++address)
                machine.memory().write(address,
                                       static_cast<std::uint8_t>(seed * 17 + address * 23));
            machine.memory().write(0xC267, seed % 4);
            machine.cpu().hl.uint16_value = 0xC440 + 16 * (seed % 8);
            machine.cpu().de.uint16_value = 0xC402;
            machine.cpu().ix_iy[1].uint16_value = 0xC400;
            machine.cpu().bc.uint8_values.at_1 = 1 + seed % 8;
            try {
                harness.run_cpp(machine, s.entry);
            } catch (const std::exception &e) {
                throw std::runtime_error("entry=" + std::to_string(s.entry.address) +
                                         " seed=" + std::to_string(seed) + ": " + e.what());
            }
            ++count;
        }
        auto boundary = harness.prepare(s.entry, {});
        menu_probe::place_before_boundary(boundary, 1);
        harness.run_fallback(boundary, s.entry, RoutineFallback::frame);
    }
    return count;
}
void complete_action(probe::Harness &harness, const Machine &base, unsigned kind) {
    auto machine = map_probe::prepare_at(
        base, {0, 0x1148}, {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = kind});
    machine.cpu().af.uint8_values.at_1 = 0;
    machine.memory().write(0xC267, 0);
    machine.memory().write(0xC2C2, 0);
    machine.memory().write(0xC2D4, kind == 1 ? 1 : 0);
    machine.memory().write(0xC400, kind == 0 ? 0 : 1);
    machine.memory().write(0xC401, kind == 2 ? 0 : kind == 12 ? 70 : 100);
    machine.memory().write(0xC402, 100);
    machine.memory().write(0xC403, 120);
    if (kind == 12) {
        for (unsigned slot = 0; slot < 24; ++slot)
            machine.memory().write(0xC4C0 + slot, 0);
        machine.memory().write(0xC4C0, 0x24);
        machine.memory().write(0xC4E2, 1);
    }
    machine.memory().write(0xC408, 50);
    machine.memory().write(0xC40D, kind == 3 ? 1 : kind == 4 ? 7 : 0);
    machine.memory().write(0xC40A, kind == 6 ? 9 : kind == 7 ? 11 : kind == 8 ? 13 : 0);
    machine.memory().write(0xC2AC, kind == 9                    ? 3
                                   : (kind == 10 || kind == 12) ? 4
                                   : kind == 11                 ? 0
                                                                : 1);
    machine.memory().write(0xC2AD, kind == 9 ? battle_effect_probe::spell_id(harness.rom(), 0x1EA7)
                                   : kind == 12 ? 0x24
                                                : 0xFF);
    machine.memory().write(0xC2AE, 0);
    machine.memory().write(0xC2E6, 1);
    for (unsigned enemy = 0; enemy < 8; ++enemy) {
        for (unsigned field = 0; field < 16; ++field)
            machine.memory().write(0xC440 + 16 * enemy + field, 0);
        machine.memory().write(0xC440 + 16 * enemy, enemy == 3 ? 1 : 0);
        machine.memory().write(0xC441 + 16 * enemy, enemy == 3 ? 100 : 0);
        machine.memory().write(0xC449 + 16 * enemy, 10);
    }
    battle_effect_probe::compare_call(machine, harness.registry(), battle_effect_probe::press_both,
                                      "party action=" + std::to_string(kind));
    if (kind == 3 || kind == 4)
        probe::check(machine.peek(0xC40D) < (kind == 3 ? 1 : 7), "blocking counter advances");
    if (kind == 6 || kind == 7 || kind == 8)
        probe::check(machine.peek(0xC471) == 100 - (kind == 6   ? 5
                                                    : kind == 7 ? 10
                                                                : 20),
                     "group weapon damage");
    if (kind == 12)
        probe::check(machine.peek(0xC4C0) == 0, "queued food consumed");
    if (kind == 9)
        probe::check(machine.peek(0xC402) < 100, "queued caster pays MP");
}
int scenarios(probe::Harness &harness, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-battle"));
    const auto image = map_probe::start_image(inputs, "first-battle", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "actor-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from,
                            [](const Machine &m) {
                                return m.peek(0xC29D) != 0 && m.peek(0xC268) != 0 &&
                                       m.peek(0xC26E) == 4;
                            }),
                 "actor real battle frame");
    for (unsigned kind = 0; kind < 13; ++kind)
        complete_action(harness, scene.machine(), kind);
    return 13;
}
} // namespace
int main(int argc, char **argv) {
    try {
        registration();
        if (argc == 1)
            return 0;
        const auto inputs = map_probe::load(argc, argv);
        if (!inputs)
            return 77;
        probe::Harness harness(inputs->rom);
        const auto count = state_probes(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Actor probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
