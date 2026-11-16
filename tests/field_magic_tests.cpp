// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/dungeon_tables.h"
#include "game/field_magic.h"
#include "menu_probe.h"
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_field_magic(registry);
    probe::check(registry.entries().size() == game::field_magic::segments.size(),
                 "field magic registration count");
    for (const auto &s : game::field_magic::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "field magic ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::field_magic::segments) {
        for (unsigned seed = 0; seed < 512; ++seed) {
            auto machine =
                harness.prepare(s.entry, {.r = static_cast<std::uint8_t>(seed), .seed = seed});
            for (unsigned address = 0xC000; address < 0xD000; ++address)
                machine.memory().write(address,
                                       static_cast<std::uint8_t>(seed * 29 + address * 31));
            machine.memory().write(0xC267, seed % 4);
            machine.cpu().hl.uint16_value = 0xCB00 + (seed % 256);
            machine.cpu().de.uint16_value = 0xC432;
            harness.run_cpp(machine, s.entry);
            ++count;
        }
        auto boundary = harness.prepare(s.entry, {});
        menu_probe::place_before_boundary(boundary, 1);
        harness.run_fallback(boundary, s.entry, RoutineFallback::frame);
    }
    return count;
}
unsigned cell_rules(probe::Harness &harness) {
    unsigned count = 0;
    for (unsigned value = 0; value < 256; ++value) {
        auto machine = harness.prepare({0, 0x20AF}, {});
        machine.cpu().hl.uint16_value = 0xCB78;
        machine.cpu().af.uint8_values.at_1 = value;
        machine.memory().write(0xCB78, value);
        machine.memory().write(0xC2D8, 0xA5);
        harness.run_cpp(machine, {0, 0x20AF});
        const bool opens = (value & 7) == 6 && !(value & 0x80);
        probe::check(machine.peek(0xC2D8) == (opens ? 4 : 0xA5),
                     "only unopened magic doors request opening");
        probe::check(machine.peek(0xCB78) == value, "opening is delegated, not performed here");
        ++count;
    }
    for (unsigned cell = 0; cell < 256; ++cell)
        for (bool pitfall : {false, true}) {
            auto machine = harness.prepare({0, 0x2039}, {});
            machine.cpu().bc.uint8_values.at_0 = cell;
            machine.cpu().af.uint8_values.at_1 = pitfall ? 0xFF : 0;
            machine.cpu().af.uint8_values.at_0 = pitfall ? 0 : 0x40;
            machine.memory().write(0xCB00 + cell, 8);
            machine.memory().write(0xCB00 + ((cell + 1) % 256), 0xA5);
            harness.run_cpp(machine, {0, 0x2039});
            probe::check(machine.peek(0xCB00 + cell) == (pitfall ? 0 : 8),
                         "pitfall removal follows helper result");
            probe::check(machine.peek(0xCB00 + ((cell + 1) % 256)) == 0xA5,
                         "adjacent cell preserved");
            ++count;
        }
    return count;
}
unsigned dispatch_ownership(probe::Harness &harness) {
    unsigned slots = 0, placeholders = 0;
    for (const auto [table, count] :
         {std::pair{0x1A66u, 18u}, std::pair{0x1A8Au, 18u}, std::pair{0x1DFEu, 19u}}) {
        for (unsigned index = 0; index < count; ++index) {
            const auto at = static_cast<std::uint16_t>(table + 2 * index);
            const auto target = static_cast<std::uint16_t>(harness.rom().byte(0, at) |
                                                           (harness.rom().byte(0, at + 1) << 8));
            if (target == 0x1AAE || target == 0x1E24) {
                const auto jump = static_cast<std::uint16_t>(
                    harness.rom().byte(0, target + 1) | (harness.rom().byte(0, target + 2) << 8));
                probe::check(harness.rom().byte(0, target) == 0xC3 && jump == target,
                             "declared spell placeholder is a self loop");
                ++placeholders;
            } else
                probe::check(harness.registry().find({0, target}) != nullptr,
                             "spell target has no registered family: " + std::to_string(target));
            ++slots;
        }
    }
    std::cout << "Spell dispatch slots accounted: " << slots
              << "; explicit placeholder slots: " << placeholders << "\n";
    return slots;
}
struct FloorCell {
    unsigned floor, cell;
};
FloorCell no_object_cell(const game::DungeonTables &tables) {
    for (unsigned floor = 0; floor < game::dungeon::floor_count; ++floor)
        if (!tables.object_at(floor, 0x78))
            return {floor, 0x78};
    throw std::runtime_error("no pitfall fixture cell found");
}
void complete_effect(probe::Harness &harness, const Machine &base,
                     const game::DungeonTables &tables, unsigned kind) {
    // Spell entries, followed by the three shared item paths without a debit.
    constexpr std::uint16_t entries[]{0x201C, 0x201C, 0x201C, 0x201C, 0x2064, 0x2064,
                                      0x2091, 0x2091, 0x2091, 0x20F8, 0x20F8, 0x20F8,
                                      0x2140, 0x2140, 0x2078, 0x2102, 0x2159};
    const unsigned entry = entries[kind];
    auto machine =
        map_probe::prepare_at(base, {0, static_cast<std::uint16_t>(entry)},
                              {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = kind});
    for (unsigned member = 0; member < 4; ++member) {
        const unsigned record = 0xC400 + 16 * member;
        for (unsigned field = 0; field < 16; ++field)
            machine.memory().write(record + field, 0);
        machine.memory().write(record, 1);
        machine.memory().write(record + 1, 100);
        machine.memory().write(record + 2, 100);
        machine.memory().write(record + 5, 40);
        machine.memory().write(record + 6, 100);
        machine.memory().write(record + 7, 100);
    }
    const auto selected = no_object_cell(tables);
    machine.memory().write(game::dungeon::floor, selected.floor);
    machine.memory().write(game::dungeon::position, 0x88);
    machine.memory().write(game::dungeon::facing, 0);
    for (unsigned cell = 0; cell < 256; ++cell)
        machine.memory().write(0xCB00 + cell, 0);
    const unsigned ahead = static_cast<std::uint8_t>(0x88 + tables.offset(0, game::dungeon::ahead));
    probe::check(ahead == selected.cell, "chosen pitfall fixture is ahead");
    const bool pitfall = kind == 0 || kind == 9 || kind == 15;
    const bool chest = kind == 2 || kind == 3 || kind == 11;
    machine.memory().write(0xCB00 + ahead, pitfall ? 8 : kind == 6 ? 6 : kind == 7 ? 0x86 : 0);
    machine.memory().write(0xC800, chest ? 0x0E : 0);
    machine.memory().write(0xC80F, kind == 2 ? 0x3E : 0x3D);
    machine.memory().write(0xC2DD, 0);
    machine.memory().write(0xC2DE, 0);
    machine.memory().write(0xC2DF, 0);
    machine.memory().write(0xC29D, 0);
    machine.memory().write(
        0xC29E, (kind == 5 || kind == 8 || kind == 10 || kind == 12 || kind == 16) ? 1 : 0);
    machine.memory().write(0xC2D8, 0);
    machine.memory().write(0xC267, 3);
    machine.memory().write(0xC2C2, 3);
    machine.memory().write(0xC4F0, 3);
    machine.memory().write(0xC204, 0);
    machine.memory().write(0xC205, 0);
    machine.cpu().ix_iy[1].uint16_value = 0xC430;
    const bool shared = kind >= 14;
    const unsigned spell = shared ? 0 : battle_effect_probe::spell_id(harness.rom(), entry);
    machine.cpu().bc.uint8_values.at_1 = spell;
    const auto controls = [](const Machine &m) {
        const bool press = ((m.cycles() / Machine::frame_cycles) / 3) % 2;
        return Pad{.two = press};
    };
    const std::string context = "field effect kind=" + std::to_string(kind);
    if (kind == 11) {
        // Inspection hands the unchanged safe chest to M12's ordinary prompt.
        battle_effect_probe::compare_call(machine, harness.registry(), controls, context, 0x28DB);
        probe::check(machine.peek(0xC80F) == 0x3D, "inspection leaves chest trap state unchanged");
    }
    battle_effect_probe::compare_call(machine, harness.registry(), controls, context);
    probe::check(machine.peek(0xC432) ==
                     (shared ? 100 : 100 - battle_effect_probe::cost(harness.rom(), spell)),
                 "field spell debits actual cost; shared item entry does not");
    if (kind == 0)
        probe::check(machine.peek(0xCB00 + ahead) == 0, "pitfall disarmed");
    if (kind == 1)
        probe::check(machine.peek(0xCB00 + ahead) == 0, "ordinary floor preserved");
    if (kind == 2 || kind == 3)
        probe::check(machine.peek(0xC80F) == 0x3D, "chest made safe");
    if (kind == 4 || kind == 14)
        probe::check(machine.peek(0xC2D8) == 0xFF && machine.peek(0xC202) == 8,
                     "dungeon exit request");
    if (kind == 5 || kind == 8 || kind == 13)
        probe::check(machine.peek(0xC2D8) == 0, "refused request leaves field menu active");
    if (kind == 6)
        probe::check(machine.peek(0xC2D8) == 4, "unopened magic door request");
    if (kind == 7)
        probe::check(machine.peek(0xC2D8) == 0, "already open magic door refused");
    if (kind == 9 || kind == 15)
        probe::check(machine.peek(0xCB00 + ahead) == 8, "inspection keeps pitfall");
    if (kind == 12 || kind == 16)
        probe::check(machine.peek(0xC2D8) == 8, "map transfer request");
}
int scenarios(probe::Harness &harness, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-dungeon"));
    const auto image = map_probe::start_image(inputs, "first-dungeon", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "field-magic-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from,
                            [](const Machine &m) {
                                return m.peek(game::dungeon::game_mode) ==
                                       game::dungeon::dungeon_mode;
                            }),
                 "real dungeon frame");
    const game::DungeonTables tables(*inputs.rom.image);
    for (unsigned kind = 0; kind < 17; ++kind)
        complete_effect(harness, scene.machine(), tables, kind);
    return 17;
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
        const unsigned count = state_probes(harness) + cell_rules(harness);
        (void)dispatch_ownership(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Field magic probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
