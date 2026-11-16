// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/items_field.h"
#include "menu_probe.h"
#include <bit>
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_items_field(registry);
    probe::check(registry.entries().size() == game::items_field::segments.size(),
                 "field item registration count");
    for (const auto &s : game::items_field::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "field item ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::items_field::segments) {
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
unsigned vehicle_gates(probe::Harness &h) {
    unsigned count = 0;
    auto target = [&](unsigned at) {
        return h.rom().byte(0, at + 1) | (h.rom().byte(0, at + 2) << 8);
    };
    for (unsigned world = 0; world < 256; ++world)
        for (unsigned interaction : {0u, 1u, 255u})
            for (auto entry : {RoutineEntry{0, 0x22B7}, RoutineEntry{0, 0x22EB}}) {
                auto m = h.prepare(entry, {});
                m.memory().write(0xC308, world);
                m.memory().write(0xC29E, interaction);
                h.run_cpp(m, entry);
                const bool accepted = world < 4 && interaction;
                unsigned final = entry.address == 0x22B7 ? (accepted ? 0x22C9 : 0x22DF)
                                                         : (accepted ? 0x22FD : 0x2314);
                probe::check(m.cpu().pc.uint16_value == target(final),
                             "vehicle world/interaction gate");
                ++count;
            }
    for (unsigned world = 0; world < 256; ++world) {
        auto m = h.prepare({0, 0x2320}, {});
        m.memory().write(0xC308, world);
        h.run_cpp(m, {0, 0x2320});
        probe::check(m.cpu().pc.uint16_value == (world == 2 ? 0x22B7 : 0x232A) &&
                         m.cpu().de.uint8_values.at_0 == 0x0C,
                     "ice digger world gate and mode");
        ++count;
    }
    return count;
}
void complete_item(probe::Harness &h, const Machine &base, unsigned kind) {
    constexpr unsigned ids[]{0,    4,    4,    0x21, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
                             0x26, 0x26, 0x26, 0x27, 0x27, 0x27, 0x27, 0x28, 0x28, 0x29,
                             0x29, 0x29, 0x2A, 0x2A, 0x10, 0x21, 0x22, 0x28, 4};
    const auto item = ids[kind];
    auto m = map_probe::prepare_at(
        base, {0, 0x2201}, {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = kind});
    const bool battle = kind == 1 || kind == 10 || kind == 13 || kind == 17 || kind == 19 ||
                        kind == 22 || kind == 27 || kind == 28;
    const bool dungeon = kind == 5 || kind == 11 || kind == 14 || kind == 15 || kind == 21;
    m.memory().write(0xC29D, battle);
    m.memory().write(0xC29E, dungeon ? 0 : 1);
    m.memory().write(0xC2D4, 0);
    m.memory().write(0xC2E7, 1);
    m.memory().write(0xC2D8, 0);
    m.memory().write(0xC315, kind == 15 ? 0xFF : 0);
    m.memory().write(0xC308, kind == 4 || kind == 7 ? 4 : kind == 8 ? 2 : 0);
    m.memory().write(0xC30E, 0);
    m.memory().write(0xC2C4, item);
    m.memory().write(0xC267, 0);
    m.memory().write(0xC2C2, 0);
    for (unsigned slot = 0; slot < 24; ++slot)
        m.memory().write(0xC4C0 + slot, 0);
    m.memory().write(0xC4C0, item);
    m.memory().write(0xC4C1, 3);
    m.memory().write(0xC4E2, 2);
    m.memory().write(0xC29B, 0xC0);
    m.memory().write(0xC29C, 0xC4);
    if (kind == 3 || kind == 6 || kind == 8 || kind == 25 || kind == 26) {
        const unsigned mask = item == 0x22 ? 0x0A : 0x0D;
        unsigned tile = 0;
        for (; tile < 256; ++tile)
            if (bool(h.rom().byte(3, 0xBC6F + tile) & mask) == (kind >= 25))
                break;
        probe::check(tile < 256, "vehicle terrain byte fixture");
        for (unsigned at = 0xCC00; at < 0xD000; ++at)
            m.memory().write(at, tile);
    }
    battle_effect_probe::compare_call(m, h.registry(), battle_effect_probe::press_both,
                                      "field/battle item=" + std::to_string(kind));
    const bool consumed =
        kind == 14 || kind == 17 || kind == 20 || kind == 22 || kind == 23 || kind == 27;
    probe::check(m.peek(0xC4E2) == (consumed ? 1 : 2), "item consumption follows actual branch");
    if (kind == 3 || kind == 6 || kind == 8)
        probe::check(m.peek(0xC30E) == (kind == 3   ? 4
                                        : kind == 6 ? 8
                                                    : 12) &&
                         m.peek(0xC2D8) == 0xFF,
                     "vehicle request accepted");
    if (kind == 4 || kind == 5 || kind == 7 || kind == 9 || kind == 25 || kind == 26)
        probe::check(m.peek(0xC30E) == 0 && m.peek(0xC2D8) == 0, "vehicle request refused");
    if (kind == 11)
        probe::check(m.peek(0xC2D8) == 0xFF && m.peek(0xC202) == 8,
                     "flute shares dungeon exit request");
    if (kind == 14)
        probe::check(m.peek(0xC315) == 0xFF && m.peek(0xC2D8) == 0xFF, "flash illuminates dungeon");
    if (kind == 20)
        probe::check(m.peek(0xC2D8) == 8, "transfer item requests map transfer");
    if (kind == 1 || kind == 17)
        probe::check(m.peek(0xC267) == 5, "wand/escaper share escape effect");
}
int scenarios(probe::Harness &h, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-battle"));
    const auto image = map_probe::start_image(inputs, "first-battle", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "field-items-map");
    std::uint64_t from = 0;
    probe::check(
        scene.play(recording, from,
                   [](const Machine &m) { return map_probe::on_map(m) && m.peek(0xC29D) == 0; }),
        "real field frame");
    const auto field = scene.machine();
    probe::check(scene.play(recording, from,
                            [](const Machine &m) {
                                return m.peek(0xC29D) != 0 && m.peek(0xC268) != 0 &&
                                       m.peek(0xC26E) == 4;
                            }),
                 "real item battle frame");
    const auto battle = scene.machine();
    const auto dungeon_recording = replay::read(inputs.recording("first-dungeon"));
    const auto dungeon_image = map_probe::start_image(inputs, "first-dungeon", dungeon_recording);
    if (!dungeon_image)
        return -1;
    map_probe::Scenario dungeon(inputs.rom, *dungeon_image, "field-items-dungeon");
    from = 0;
    probe::check(dungeon.play(dungeon_recording, from,
                              [](const Machine &m) { return m.peek(0xC202) == 0x0B; }),
                 "real item dungeon frame");
    for (unsigned kind = 0; kind < 29; ++kind) {
        const bool in_battle = kind == 1 || kind == 10 || kind == 13 || kind == 17 || kind == 19 ||
                               kind == 22 || kind == 27 || kind == 28;
        const bool in_dungeon = kind == 5 || kind == 11 || kind == 14 || kind == 15 || kind == 21;
        complete_item(h, in_battle ? battle : in_dungeon ? dungeon.machine() : field, kind);
    }
    return 29;
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
        const auto count = state_probes(harness) + vehicle_gates(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Field item probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
