// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/battle_enemy.h"
#include "menu_probe.h"
#include <bit>
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_battle_enemy(registry);
    probe::check(registry.entries().size() == game::battle_enemy::segments.size(),
                 "enemy registration count");
    for (const auto &s : game::battle_enemy::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "enemy ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::battle_enemy::segments) {
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
unsigned protection_states(probe::Harness &h) {
    unsigned count = 0;
    for (unsigned counter = 0; counter < 256; ++counter)
        for (unsigned draw = 0; draw < 256; ++draw) {
            auto m = h.prepare({0, 0x12DE}, {});
            m.memory().write(0xC2EF, counter);
            m.cpu().af.uint8_values.at_1 = draw;
            h.run_cpp(m, {0, 0x12DE});
            unsigned low = counter & 0x7F;
            unsigned reduced = low > draw % 4 ? low - draw % 4 : 0;
            unsigned expected = reduced ? (reduced | (counter & 0x80)) : 0;
            probe::check(m.peek(0xC2EF) == expected,
                         "protection consumes low two-bit draw, preserves variant until exhausted");
            ++count;
        }
    for (unsigned enemy = 0; enemy < 256; ++enemy)
        for (unsigned actor = 0; actor < 4; ++actor) {
            auto m = h.prepare({0, 0x1350}, {});
            m.memory().write(0xC2E6, enemy);
            m.memory().write(0xC2C2, actor);
            for (unsigned slot = 0; slot < 4; ++slot)
                m.memory().write(0xC400 + 16 * slot, 0x81);
            h.run_cpp(m, {0, 0x1350});
            for (unsigned slot = 0; slot < 4; ++slot)
                probe::check(m.peek(0xC400 + 16 * slot) == (enemy == 0x46 && actor == 1 ? 0 : 0x81),
                             "enemy46 and actor1 death clears all party activity");
            ++count;
        }
    return count;
}
void complete_action(probe::Harness &h, const Machine &base, unsigned kind) {
    auto m = map_probe::prepare_at(
        base, {0, 0x128C}, {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = kind});
    m.cpu().af.uint8_values.at_1 = 4;
    m.memory().write(0xC2E8, 0x10);
    m.memory().write(0xC2E6, 1);
    m.memory().write(0xC2EF, kind == 4 ? 1 : kind == 5 ? 6 : kind == 6 ? 0x86 : 0);
    m.memory().write(0xC440, kind == 0 ? 0 : 1);
    m.memory().write(0xC441, 100);
    m.memory().write(0xC448, kind == 7 ? 255 : 30);
    m.memory().write(0xC44D, kind == 1 ? 1 : kind == 2 ? 7 : 0);
    for (unsigned actor = 0; actor < 4; ++actor) {
        m.memory().write(0xC400 + 16 * actor, 1);
        m.memory().write(0xC401 + 16 * actor, kind == 7 ? 1 : 100);
        m.memory().write(0xC405 + 16 * actor, 10);
        m.memory().write(0xC409 + 16 * actor, kind == 3 ? 255 : 20);
    }
    battle_effect_probe::compare_call(m, h.registry(), battle_effect_probe::press_both,
                                      "enemy physical action=" + std::to_string(kind));
    if (kind == 1 || kind == 2)
        probe::check(m.peek(0xC44D) < (kind == 1 ? 1 : 7), "enemy blocking counter advances");
}
int scenarios(probe::Harness &harness, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-battle"));
    const auto image = map_probe::start_image(inputs, "first-battle", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "enemy-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from,
                            [](const Machine &m) {
                                return m.peek(0xC29D) != 0 && m.peek(0xC268) != 0 &&
                                       m.peek(0xC26E) == 4;
                            }),
                 "enemy real battle frame");
    for (unsigned kind = 0; kind < 8; ++kind)
        complete_action(harness, scene.machine(), kind);
    return 8;
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
        const auto count = state_probes(harness) + protection_states(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Enemy probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
