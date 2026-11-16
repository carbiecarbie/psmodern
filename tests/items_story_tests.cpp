// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/dungeon_tables.h"
#include "game/items_story.h"
#include "menu_probe.h"
#include <bit>
#include <iostream>
#include <set>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_items_story(registry);
    probe::check(registry.entries().size() == game::items_story::segments.size(),
                 "story item registration count");
    for (const auto &s : game::items_story::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "story item ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::items_story::segments) {
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
unsigned semantic_probes(probe::Harness &h) {
    unsigned count = 0;
    for (unsigned alis : {0u, 1u, 0x80u, 0xFFu})
        for (unsigned odin : {0u, 1u, 0x80u, 0xFFu})
            for (unsigned noah : {0u, 1u, 0x80u, 0xFFu}) {
                auto m = h.prepare({0, 0x24BE}, {});
                m.memory().write(0xC400, alis);
                m.memory().write(0xC420, odin);
                m.memory().write(0xC430, noah);
                h.run_cpp(m, {0, 0x24BE});
                probe::check(m.cpu().af.uint8_values.at_1 == (alis | odin | noah) &&
                                 bool(m.cpu().af.uint8_values.at_0 & 0x40) == !(alis | odin | noah),
                             "story helper ORs full human record bytes");
                ++count;
            }
    for (unsigned cell = 0; cell < 256; ++cell)
        for (auto entry : {RoutineEntry{0, 0x2509}, RoutineEntry{0, 0x268E}}) {
            auto m = h.prepare(entry, {});
            m.cpu().hl.uint16_value = 0xCB78;
            m.cpu().af.uint8_values.at_1 = cell;
            m.memory().write(0xCB78, cell);
            m.memory().write(0xC2D8, 0);
            h.run_cpp(m, entry);
            const unsigned type = cell & 7;
            const unsigned expected = !(cell & 0x80) ? (type == 5                              ? 3
                                                        : entry.address == 0x268E && type == 6 ? 4
                                                                                               : 0)
                                                     : 0;
            probe::check(m.peek(0xC2D8) == expected,
                         "key gate distinguishes unopened normal/magic doors");
            ++count;
        }
    constexpr unsigned messages[]{0x2661, 0x2668, 0x266F, 0x2674};
    for (unsigned facing = 0; facing < 256; ++facing) {
        auto m = h.prepare({0, 0x265B}, {});
        m.memory().write(game::dungeon::facing, facing);
        h.run_cpp(m, {0, 0x265B});
        const unsigned at = messages[facing & 3];
        probe::check(m.cpu().hl.uint16_value ==
                         (h.rom().byte(0, at) | (h.rom().byte(0, at + 1) << 8)),
                     "compass uses low two facing bits");
        ++count;
    }
    for (unsigned scene = 0; scene < 256; ++scene)
        for (auto entry : {RoutineEntry{0, 0x2444}, RoutineEntry{0, 0x24A6}}) {
            auto m = h.prepare(entry, {});
            m.memory().write(0xC2DB, scene);
            h.run_cpp(m, entry);
            unsigned expected = entry.address == 0x2444 ? (scene == 0xA3 ? 0x245F : 0x244E)
                                                        : (scene == 0xA1 ? 0x24CF : 0x24B0);
            probe::check(menu_probe::word(m, m.cpu().sp.uint16_value) == expected,
                         "story scene selects exact party-check continuation");
            ++count;
        }
    unsigned slots = 0;
    for (unsigned id = 0; id < 64; ++id) {
        const unsigned at = 0x220A + 2 * id;
        const auto target = h.rom().byte(0, at) | (h.rom().byte(0, at + 1) << 8);
        if (target == 0x2416 || target == 0x2524) {
            probe::check(h.rom().byte(0, target) == 0xCD, "declared native item boundary is CALL");
            const auto callee = h.rom().byte(0, target + 1) | (h.rom().byte(0, target + 2) << 8);
            probe::check(callee == 0x277C &&
                             h.registry().find({0, static_cast<std::uint16_t>(callee)}),
                         "native item boundary calls owned selected-item removal");
        } else
            probe::check(h.registry().find({0, static_cast<std::uint16_t>(target)}),
                         "item effect table target has no owner");
        ++slots;
    }
    std::cout << "Item table slots accounted: " << slots << "; native removal boundaries: 2\n";
    return count;
}
bool battle_case(unsigned k) {
    return k == 0 || k == 4 || k == 13 || k == 14 || k == 16 || k == 21 || k == 27 || k == 29 ||
           k == 31 || k == 38 || k == 44 || k == 46;
}
bool dungeon_case(unsigned k) {
    for (unsigned v : {1u,  2u,  3u,  5u,  6u,  7u,  8u,  10u, 11u, 12u, 13u, 15u,
                       34u, 35u, 36u, 37u, 38u, 39u, 40u, 41u, 43u, 46u, 47u})
        if (k == v)
            return true;
    return false;
}
void complete_item(probe::Harness &h, const Machine &base, unsigned kind) {
    unsigned item = kind < 4     ? 0x2B
                    : kind < 9   ? 0x2C
                    : kind < 14  ? 0x2D
                    : kind < 16  ? 0x2E
                    : kind < 21  ? 0x2F
                    : kind < 25  ? 0x30
                    : kind < 31  ? 0x31
                    : kind < 33  ? 0x32
                    : kind < 39  ? 0x35
                    : kind < 44  ? 0x3E
                    : kind == 44 ? 0x33
                    : kind == 45 ? 0x3C
                    : kind == 46 ? 0x3E
                                 : 0x31;
    auto m = map_probe::prepare_at(
        base, {0, 0x2201}, {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = kind});
    m.memory().write(0xC29D, battle_case(kind));
    m.memory().write(0xC29E, dungeon_case(kind) ? 0 : 1);
    m.memory().write(0xC2D8, 0);
    m.memory().write(0xC2DB, 0);
    m.memory().write(0xC2DC, 0);
    m.memory().write(0xC309, 0);
    m.memory().write(0xC800, 0);
    if (kind == 0 || kind == 2 || kind == 3)
        m.memory().write(0xC2DB, 0xA3);
    if (kind == 4 || kind == 6 || kind == 7 || kind == 8)
        m.memory().write(0xC2DB, 0xA1);
    if (kind == 16 || kind == 18 || kind == 19 || kind == 20)
        m.memory().write(0xC2DB, 0xAF);
    if (kind == 21 || kind == 23 || kind == 24 || kind == 25 || kind == 28 || kind == 29 ||
        kind == 30)
        m.memory().write(0xC2DB, 0xB0);
    if (kind == 24 || kind == 25 || kind == 28 || kind == 29)
        m.memory().write(0xC2DC, 0xFF);
    if (kind == 26 || kind == 27 || kind == 47)
        m.memory().write(0xC309, 0x17);
    for (unsigned actor = 0; actor < 4; ++actor) {
        for (unsigned field = 0; field < 16; ++field)
            m.memory().write(0xC400 + 16 * actor + field, 0);
        m.memory().write(0xC401 + 16 * actor, 100);
        m.memory().write(0xC402 + 16 * actor, 100);
        m.memory().write(0xC405 + 16 * actor, 10);
        m.memory().write(0xC406 + 16 * actor, 100);
        m.memory().write(0xC407 + 16 * actor, 100);
    }
    m.memory().write(0xC400, kind == 2 || kind == 6 ? 0 : 1);
    m.memory().write(0xC410, kind == 25 ? 0 : 1);
    m.memory().write(0xC4F0, 1);
    m.memory().write(0xC267, 0);
    m.memory().write(0xC2C2, 0);
    m.memory().write(0xC2E8, 0);
    const game::DungeonTables tables(*h.rom().image);
    unsigned floor = 0;
    for (; floor < game::dungeon::floor_count; ++floor)
        if (!tables.object_at(floor, 0x78))
            break;
    probe::check(floor < game::dungeon::floor_count, "story cell fixture");
    m.memory().write(game::dungeon::floor, floor);
    m.memory().write(game::dungeon::position, 0x88);
    m.memory().write(game::dungeon::facing, kind >= 34 && kind <= 37 ? kind - 34 : 0);
    for (unsigned cell = 0; cell < 256; ++cell)
        m.memory().write(0xCB00 + cell, 0);
    unsigned cell = kind == 10 || kind == 13 || kind == 39   ? 5
                    : kind == 11                             ? 0x85
                    : kind == 12 || kind == 40 || kind == 46 ? 6
                    : kind == 41                             ? 0x86
                    : kind == 15                             ? 8
                                                             : 0;
    m.memory().write(0xCB78, cell);
    for (unsigned slot = 0; slot < 24; ++slot)
        m.memory().write(0xC4C0 + slot, 0);
    m.memory().write(0xC4C0, item);
    m.memory().write(0xC4C1, kind == 8 ? 0x32 : kind == 19 || kind == 20 ? 0x38 : 3);
    m.memory().write(0xC4E2, kind == 20 ? 3 : 2);
    if (kind == 20)
        m.memory().write(0xC4C2, 0x31);
    m.memory().write(0xC2C4, item);
    m.memory().write(0xC29B, 0xC0);
    m.memory().write(0xC29C, 0xC4);
    battle_effect_probe::compare_call(m, h.registry(), battle_effect_probe::press_both,
                                      "story item=" + std::to_string(kind));
    const unsigned expected_count = kind == 3 || kind == 8 || kind == 14 || kind == 15 ? 1 : 2;
    probe::check(m.peek(0xC4E2) == expected_count, "story item consumption and reward count");
    if (kind == 3)
        probe::check(m.peek(0xC420) == 1 && m.peek(0xC425) == 1 && m.peek(0xC4F0) == 2 &&
                         m.peek(0xC50A) == 0xFF && m.peek(0xC2D8) == 5,
                     "Alsulin restores Odin and defers scene update");
    auto contains = [&](unsigned wanted) {
        for (unsigned slot = 0; slot < m.peek(0xC4E2); ++slot)
            if (m.peek(0xC4C0 + slot) == wanted)
                return true;
        return false;
    };
    if (kind == 7 || kind == 8)
        probe::check(contains(0x32), "Polymaterial yields or retains Hapsby");
    if (kind == 19 || kind == 20)
        probe::check(contains(0x38) && contains(0x31) && !contains(0x2F),
                     "Torch spends itself, retains pot and adds Nuts once");
    if (kind == 10 || kind == 13 || kind == 39)
        probe::check(m.peek(0xC2D8) == 3, "normal door request including battle context");
    if (kind == 40 || kind == 46)
        probe::check(m.peek(0xC2D8) == 4, "magic door request including battle context");
    if (kind == 11 || kind == 12 || kind == 41 || kind == 43)
        probe::check(m.peek(0xC2D8) == 0, "refused door unchanged");
    if (kind == 15)
        probe::check(m.peek(0xCB78) == 8, "Sphere inspects without clearing pitfall");
    if (kind == 23)
        probe::check(m.peek(0xC2D8) == 6, "Prism defers palette sequence");
    if (kind == 26 || kind == 28 || kind == 29)
        probe::check(m.peek(0xC2D8) == 7, "Nuts request includes scene path without battle gate");
    if (kind == 25 || kind == 27 || kind == 30 || kind == 47)
        probe::check(m.peek(0xC2D8) == 0, "refused Nuts unchanged");
}
void all_item_dispatches(probe::Harness &h, const Machine &field) {
    std::set<std::uint16_t> targets;
    unsigned commands = 0, removals = 0;
    for (unsigned id = 0; id < 64; ++id) {
        auto m = map_probe::prepare_at(field, {0, 0x2201}, {.return_address = 0x0180, .seed = id});
        m.memory().write(0xC29D, 0);
        m.memory().write(0xC29E, 1);
        m.memory().write(0xC2DB, 0);
        m.memory().write(0xC2DC, 0);
        m.memory().write(0xC309, 0);
        m.memory().write(0xC308, 0);
        m.memory().write(0xC2D8, 0);
        m.memory().write(0xC267, 0);
        m.memory().write(0xC2C2, 0);
        for (unsigned slot = 0; slot < 24; ++slot)
            m.memory().write(0xC4C0 + slot, 0);
        // Preserve the initialized checkpoint party. Index zero is the
        // defined empty-item table boundary and uses an empty inventory.
        const unsigned initial_count = id ? 2 : 0;
        if (id) {
            m.memory().write(0xC4C0, id);
            m.memory().write(0xC4C1, 3);
        }
        m.memory().write(0xC4E2, initial_count);
        m.memory().write(0xC2C4, id);
        m.memory().write(0xC29B, 0xC0);
        m.memory().write(0xC29C, 0xC4);
        const auto at = static_cast<std::uint16_t>(0x220A + 2 * id);
        const auto target =
            static_cast<std::uint16_t>(h.rom().byte(0, at) | (h.rom().byte(0, at + 1) << 8));
        const std::string context = "real item dispatch id=" + std::to_string(id);
        battle_effect_probe::compare_call(m, h.registry(), battle_effect_probe::press_both, context,
                                          target);
        probe::check(m.cpu().pc.uint16_value == target && m.peek(0xC2C4) == id &&
                         m.peek(0xC4E2) == initial_count,
                     "dispatcher reaches table target with selected item intact");
        const bool remove_first = target == 0x2416 || target == 0x2524;
        if (remove_first) {
            const std::uint16_t after_removal = target == 0x2416 ? 0x2419 : 0x2527;
            battle_effect_probe::compare_call(m, h.registry(), battle_effect_probe::press_both,
                                              context + " selected-item removal", after_removal);
            probe::check(m.peek(0xC4E2) == initial_count - 1 && m.peek(0xC4C0) == 3,
                         "first effect removes exactly the selected item");
            ++removals;
        } else {
            h.run_cpp(m, {0, target});
            probe::check(m.peek(0xC4E2) == initial_count && (!id || m.peek(0xC4C0) == id),
                         "first effect preserves inventory until its documented gate");
        }
        targets.insert(target);
        ++commands;
    }
    probe::check(commands == 64 && targets.size() == 23 && removals == 2,
                 "all64 dispatch indices and distinct effect heads verified");
    std::cout << "Real item dispatches: " << commands << "; distinct targets: " << targets.size()
              << "; aliases: " << commands - targets.size()
              << "; immediate removal heads: " << removals << "\n";
}
int scenarios(probe::Harness &h, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-battle"));
    const auto image = map_probe::start_image(inputs, "first-battle", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "story-items-map");
    std::uint64_t from = 0;
    probe::check(
        scene.play(recording, from,
                   [](const Machine &m) { return map_probe::on_map(m) && m.peek(0xC29D) == 0; }),
        "real field frame");
    const auto field = scene.machine();
    all_item_dispatches(h, field);
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
    map_probe::Scenario dungeon(inputs.rom, *dungeon_image, "story-items-dungeon");
    from = 0;
    probe::check(dungeon.play(dungeon_recording, from,
                              [](const Machine &m) { return m.peek(0xC202) == 0x0B; }),
                 "real item dungeon frame");
    for (unsigned kind = 0; kind < 48; ++kind)
        complete_item(h,
                      battle_case(kind)    ? battle
                      : dungeon_case(kind) ? dungeon.machine()
                                           : field,
                      kind);
    return 48;
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
        const auto count = state_probes(harness) + semantic_probes(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Story item probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
