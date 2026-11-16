// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/battle_enemy_magic.h"
#include "menu_probe.h"
#include <bit>
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_battle_enemy_magic(registry);
    probe::check(registry.entries().size() == game::battle_enemy_magic::segments.size(),
                 "enemy magic registration count");
    for (const auto &s : game::battle_enemy_magic::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "enemy magic ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::battle_enemy_magic::segments) {
        for (unsigned seed = 0; seed < 1024; ++seed) {
            auto machine =
                harness.prepare(s.entry, {.r = static_cast<std::uint8_t>(seed), .seed = seed});
            for (unsigned address = 0xC000; address < 0xD000; ++address)
                machine.memory().write(address,
                                       static_cast<std::uint8_t>(seed * 17 + address * 23));
            machine.memory().write(0xC267, seed % 4);
            machine.cpu().hl.uint16_value = 0xC440 + 16 * (seed % 8);
            machine.cpu().de.uint16_value = 0xC402;
            machine.cpu().ix_iy[1].uint16_value = 0xC440;
            machine.cpu().ix_iy[0].uint16_value = 0xC400;
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
unsigned healing_and_shield(probe::Harness &h) {
    unsigned count = 0;
    for (unsigned hp = 0; hp < 256; ++hp)
        for (unsigned maximum = 0; maximum < 256; ++maximum) {
            auto m = h.prepare({0, 0x13DB}, {});
            m.cpu().ix_iy[1].uint16_value = 0xC440;
            m.memory().write(0xC441, hp);
            m.memory().write(0xC446, maximum);
            h.run_cpp(m, {0, 0x13DB});
            probe::check(m.peek(0xC441) == std::min(maximum, std::min(255u, hp + 80)),
                         "enemy healing saturates then clamps to maximum");
            ++count;
        }
    for (unsigned flags : {0u, 1u, 0x80u, 0xFFu})
        for (unsigned shield = 0; shield < 256; ++shield) {
            auto m = h.prepare({0, 0x151A}, {});
            m.memory().write(0xC420, flags);
            m.memory().write(0xC42C, shield);
            m.memory().write(0xC2EF, 0);
            h.run_cpp(m, {0, 0x151A});
            if (flags)
                probe::check(m.cpu().pc.uint16_value == (shield == 0x1F ? 0x1305 : 0x1528),
                             "Odin mirror shield redirects lethal action");
            else
                probe::check(m.cpu().pc.uint16_value == 0x152F, "absent Odin skips shield lookup");
            ++count;
        }
    return count;
}
void complete_action(probe::Harness &h, const Machine &base, unsigned kind) {
    std::uint16_t entry = kind == 0                                  ? 0x13BC
                          : kind < 4                                 ? 0x13DB
                          : kind == 4                                ? 0x140F
                          : kind == 5                                ? 0x1429
                          : kind < 11                                ? 0x14A5
                          : (kind == 11 || kind == 12 || kind == 16) ? 0x151A
                          : kind == 15                               ? 0x14A7
                                                                     : 0x1561;
    auto m = map_probe::prepare_at(
        base, {0, entry}, {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = kind});
    m.cpu().ix_iy[1].uint16_value = 0xC440;
    m.cpu().hl.uint16_value = 0xC40D;
    m.cpu().bc.uint8_values.at_0 = 0xFF;
    m.memory().write(0xC2C2, 0);
    m.memory().write(0xC2E8, 0x10);
    m.memory().write(0xC2E6, 1);
    m.memory().write(0xC2EF, (kind == 10 || kind == 16) ? 0xFF : 0);
    m.memory().write(0xC440, 0x41);
    m.memory().write(0xC441, kind == 2 ? 250 : 100);
    m.memory().write(0xC446, kind == 1 ? 200 : kind == 2 ? 255 : 80);
    m.memory().write(0xC448, 30);
    m.memory().write(0xC44D, 0);
    const unsigned mask = kind == 6 ? 0 : kind == 7 ? 1 : kind == 8 ? 5 : 15;
    for (unsigned actor = 0; actor < 4; ++actor) {
        m.memory().write(0xC400 + 16 * actor, (mask >> actor) & 1);
        m.memory().write(0xC401 + 16 * actor, kind == 17 ? 250 : 100);
        m.memory().write(0xC405 + 16 * actor, 10);
        m.memory().write(0xC409 + 16 * actor, 20);
        m.memory().write(0xC40D + 16 * actor, 0);
    }
    m.memory().write(0xC42C, kind == 12 ? 0x1F : 0);
    for (unsigned slot = 0; slot < 24; ++slot)
        m.memory().write(0xC4C0 + slot, 0);
    if (kind == 14)
        m.memory().write(0xC4C0, 0x3C);
    std::array<unsigned, 4> amounts{};
    auto controls = [&](const Machine &machine) {
        if (kind == 17 && machine.cpu().pc.uint16_value == 0x1514)
            amounts[machine.peek(0xC2C2)] = 1 + (machine.cpu().af.uint8_values.at_1 & 15);
        return battle_effect_probe::press_both(machine);
    };
    battle_effect_probe::compare_call(m, h.registry(), controls,
                                      "enemy special=" + std::to_string(kind));
    if (kind == 0)
        probe::check(m.peek(0xC40D) == 3, "enemy blocks target for three units");
    if (kind > 0 && kind < 4)
        probe::check(m.peek(0xC441) == (kind == 1   ? 180
                                        : kind == 2 ? 255
                                                    : 80),
                     "enemy complete heal");
    if (kind == 4)
        probe::check(m.peek(0xC440) == 0xC1, "boost retains existing weak flag");
    unsigned damage = 0, dead = 0;
    for (unsigned actor = 0; actor < 4; ++actor) {
        damage += 100 - m.peek(0xC401 + 16 * actor);
        dead += m.peek(0xC400 + 16 * actor) == 0;
    }
    if (kind == 5)
        probe::check(damage >= 14 && damage <= 20, "double strike falls through for second hit");
    if (kind >= 7 && kind <= 9)
        probe::check(damage >= 25u * std::popcount(mask) && damage <= 40u * std::popcount(mask),
                     "group spell fixed base with per-target variance");
    if (kind == 10)
        probe::check(damage == 0, "high protection blocks group spell");
    if (kind == 11 || kind == 16)
        probe::check(dead == 1, "lethal action kills one despite protection");
    if (kind == 12)
        probe::check(dead == 0, "mirror shield redirects lethal action");
    if (kind == 17)
        for (unsigned actor = 0; actor < 4; ++actor) {
            probe::check(amounts[actor] >= 1 && amounts[actor] <= 16,
                         "crystal absence captured each independent draw");
            const unsigned sum = 250 + amounts[actor];
            probe::check(m.peek(0xC401 + 16 * actor) == (sum >= 256 ? sum % 256 : 0),
                         "crystal absence preserves byte overflow survival");
        }
    if (kind == 13)
        probe::check(dead == 4, "missing crystal group action kills all active recipients");
    if (kind == 14 || kind == 15)
        probe::check(dead == 0, "physical group path leaves prepared party alive");
}
int scenarios(probe::Harness &harness, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-battle"));
    const auto image = map_probe::start_image(inputs, "first-battle", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "enemy-magic-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from,
                            [](const Machine &m) {
                                return m.peek(0xC29D) != 0 && m.peek(0xC268) != 0 &&
                                       m.peek(0xC26E) == 4;
                            }),
                 "enemy real battle frame");
    for (unsigned kind = 0; kind < 18; ++kind)
        complete_action(harness, scene.machine(), kind);
    return 18;
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
        const auto count = state_probes(harness) + healing_and_shield(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Enemy special probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
