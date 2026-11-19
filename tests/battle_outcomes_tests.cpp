// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/battle_outcomes.h"
#include "game/dungeon_tables.h"
#include "menu_probe.h"
#include "c4_sweep.h"
#include <algorithm>
#include <bit>
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_battle_outcomes(registry);
    probe::check(registry.entries().size() == game::battle_outcomes::segments.size(),
                 "outcome registration count");
    for (const auto &s : game::battle_outcomes::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "outcome ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::battle_outcomes::segments) {
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
            if (s.entry.address == 0x16FE) {
                machine.memory().write(0xDF00, 0x00);
                machine.memory().write(0xDF01, 0xC4);
                machine.memory().write(0xDF02, 0x80);
                machine.memory().write(0xDF03, 0x01);
            }
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
Machine prepare(probe::Harness &harness, RoutineEntry entry, unsigned count, std::uint16_t source,
                std::uint16_t dest) {
    auto m = harness.prepare(entry, {.cartridge_ram = true, .seed = count});
    for (unsigned i = 0; i < 32; ++i)
        m.memory().write(static_cast<std::uint16_t>(0xc400 + i),
                         static_cast<std::uint8_t>(7 * i + 1));
    m.cpu().hl.uint16_value = source;
    m.cpu().de.uint16_value = dest;
    m.cpu().bc.uint16_value = static_cast<std::uint16_t>(count);
    return m;
}
void check_slices(probe::Harness &h, RoutineEntry entry) {
    constexpr std::pair<std::uint16_t, std::uint16_t> ranges[]{
        {0xc400, 0xc401}, {0xc401, 0xc400}, {0xc400, 0xe401}, {0xc400, 0x8000},
        {0x8000, 0xc401}, {0x0100, 0xdeff}, {0xfffc, 0xc400}};
    for (auto [source, dest] : ranges) {
        for (unsigned remaining : {0u, 1u, 2u, 15u, 16u, 17u, 31u, 255u, 1024u, 65535u}) {
            auto machine = prepare(h, entry, remaining, source, dest);
            const unsigned copied = remaining == 0 || remaining > 16 ? 16 : remaining;
            const auto result = h.run_cpp(machine, entry);
            probe::check(result.cycles == 21 * copied - (remaining == copied ? 5 : 0),
                         "wrong copy slice duration");
            probe::check(machine.cpu().bc.uint16_value ==
                             static_cast<std::uint16_t>(remaining - copied),
                         "wrong number of remaining bytes");
        }
    }
    for (unsigned count : {1u, 2u, 15u, 16u, 17u}) {
        const auto cost = count <= 16 ? 21 * count - 5 : 336;
        for (unsigned offset = 1; offset <= cost + 1; ++offset) {
            auto frame = prepare(h, entry, count, 0xc400, 0x8000);
            auto nmi = frame;
            auto irq = frame;
            frame.advance(Machine::frame_cycles - frame.cycles() % Machine::frame_cycles - offset);
            nmi.schedule_nmi(nmi.cycles() + offset);
            irq.cpu().iff1 = irq.cpu().iff2 = 1;
            const auto next = irq.next_irq();
            probe::check(next && *next > irq.cycles() + cost + 1,
                         "expected an upcoming frame interrupt");
            irq.advance(*next - irq.cycles() - offset);
            // Milestone 14: the copy is an exact segment. Its slice is the next
            // sixteen transfers (or the rest) whatever the window: a frame end,
            // an NMI or an interrupt falling inside it is taken between two
            // transfers, as natively (the lockstep compares every boundary).
            const unsigned copied = count < 16 ? count : 16;
            for (auto *machine : {&frame, &nmi, &irq}) {
                const auto before = h.routine(entry).counters;
                h.run_cpp(*machine, entry);
                const auto &after = h.routine(entry).counters;
                probe::check(machine->cpu().bc.uint16_value == count - copied,
                             "copy must make its whole slice");
                // An event sampled at the end of a transfer before the last
                // one (cycle 21*i-1 of transfer i) is delivered inside the body.
                const bool inside = machine != &frame && offset < 21 * (copied - 1);
                probe::check(after.irq_deliveries + after.nmi_deliveries ==
                                 before.irq_deliveries + before.nmi_deliveries + (inside ? 1 : 0),
                             "an event inside the slice must be delivered inside it, and only then");
            }
        }
    }
    for (const std::uint16_t dest : {0xffed, 0xfffb, 0xfffc, 0xffff}) {
        auto m = prepare(h, entry, 16, 0xc400, dest);
        h.run_fallback(m, entry, RoutineFallback::mapper_write);
    }
}
void seed_windows(probe::Harness &h) {
    for (auto entry : {RoutineEntry{0, 0x15DC}, RoutineEntry{0, 0x16D6}})
        for (unsigned offset = 1; offset <= 41; ++offset) {
            auto frame = h.prepare(entry, {});
            auto nmi = frame;
            auto irq = frame;
            menu_probe::place_before_boundary(frame, offset);
            nmi.schedule_nmi(nmi.cycles() + offset);
            irq.cpu().iff1 = irq.cpu().iff2 = 1;
            const auto next = irq.next_irq();
            probe::check(next && *next > irq.cycles() + 41, "seed IRQ fixture");
            irq.advance(*next - irq.cycles() - offset);
            if (offset < 40) {
                h.run_fallback(frame, entry, RoutineFallback::frame);
                h.run_fallback(nmi, entry, RoutineFallback::nmi);
                h.run_fallback(irq, entry, RoutineFallback::irq);
            } else
                for (auto *machine : {&frame, &nmi, &irq}) {
                    const auto result = h.run_cpp(*machine, entry);
                    probe::check(result.cycles == 40 && machine->peek(0xC800) == 0,
                                 "seed window full state and zero write");
                }
        }
}
unsigned ending_branches(probe::Harness &h) {
    unsigned count = 0;
    for (auto entry : {RoutineEntry{0, 0x1602}, RoutineEntry{0, 0x1656}})
        for (unsigned enemy = 0; enemy < 256; ++enemy) {
            auto m = h.prepare(entry, {});
            m.memory().write(0xC2E6, enemy);
            m.memory().write(0xC4F0, 1);
            for (unsigned color = 0; color < 16; ++color)
                m.memory().write(0xC230 + color, 0xA5);
            h.run_cpp(m, entry);
            const bool special = enemy == 0x31 || (entry.address == 0x1602 && enemy == 0x4A);
            probe::check(m.peek(0xC004) == (special                   ? 0xD8
                                            : entry.address == 0x1602 ? 0x94
                                                                      : 0xAF),
                         "ending branch sound");
            for (unsigned color = 0; color < 16; ++color)
                probe::check(m.peek(0xC230 + color) == (enemy == 0x46 ? 0x30 : 0xA5),
                             "enemy46 ending palette reset");
            ++count;
        }
    for (unsigned enemy = 0; enemy < 256; ++enemy) {
        auto m = h.prepare({0, 0x1679}, {});
        m.memory().write(0xC2E6, enemy);
        h.run_cpp(m, {0, 0x1679});
        if (enemy == 0x48 || enemy == 0x49)
            probe::check(m.cpu().bc.uint8_values.at_1 == 180, "late victory wait is180 frames");
        else
            probe::check(m.peek(0xC004) == 0xD8, "ordinary victory announces immediately");
        ++count;
    }
    return count;
}
unsigned escape_restoration_gates(probe::Harness &h) {
    unsigned count = 0;
    for (unsigned action = 0; action < 256; ++action)
        for (unsigned interaction : {0u, 1u, 255u}) {
            auto m = h.prepare({0, 0x1645}, {});
            m.memory().write(0xDF00, 0xA5);
            m.memory().write(0xDF01, action);
            m.memory().write(0xDF02, 0x80);
            m.memory().write(0xDF03, 1);
            m.memory().write(0xC29E, interaction);
            h.run_cpp(m, {0, 0x1645});
            probe::check(m.cpu().pc.uint16_value == (action == 5 && !interaction ? 0x688C : 0x0180),
                         "only action5 in dungeon applies backstep restoration");
            ++count;
        }
    return count;
}
void complete_outcome(probe::Harness &h, const Machine &base, unsigned kind) {
    const std::uint16_t entry = kind < 5     ? 0x1602
                                : kind < 9   ? 0x1656
                                : kind == 9  ? 0x163E
                                : kind == 10 ? 0x15D9
                                : kind >= 18 ? 0x163E
                                : kind >= 16 ? 0x1656
                                : kind == 11 ? 0x16B2
                                             : 0x16F1;
    auto m = map_probe::prepare_at(
        base, {0, entry}, {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = kind});
    const unsigned enemy = kind == 0 || kind == 5   ? 0x31
                           : kind == 1              ? 0x4A
                           : kind == 2 || kind == 6 ? 0x46
                           : kind == 7              ? 0x48
                           : kind == 8              ? 0x49
                                                    : 1;
    m.memory().write(0xC2E6, enemy);
    m.memory().write(0xC2D0, 0);
    m.memory().write(0xC2D1, 0);
    m.memory().write(0xC2DD, kind == 16 ? 10 : 0);
    m.memory().write(0xC2DE, 0);
    m.memory().write(0xC2DF, kind == 17 ? 0x24 : 0);
    if (kind >= 16) {
        m.memory().write(0xC4E0, 100);
        m.memory().write(0xC4E1, 0);
        m.memory().write(0xC4E2, 0);
        m.memory().write(0xC2E1, 0);
        m.memory().write(0xC2E2, 0xC5);
        for (unsigned slot = 0; slot < 24; ++slot)
            m.memory().write(0xC4C0 + slot, 0);
    }
    m.memory().write(0xC4F0, kind == 4 ? 0 : 1);
    for (unsigned color = 0; color < 16; ++color)
        m.memory().write(0xC230 + color, 0xA5);
    if (kind == 9 || kind >= 18)
        m.cpu().af.uint8_values.at_1 = kind == 9 ? 0 : 5;
    if (kind >= 18)
        m.memory().write(0xC29E, kind == 18 ? 0 : 1);
    if (kind == 18) {
        m.memory().write(game::dungeon::position, 0x88);
        m.memory().write(game::dungeon::facing, 0);
        for (unsigned cell = 0; cell < 256; ++cell)
            m.memory().write(0xCB00 + cell, 0);
        const game::DungeonTables tables(*h.rom().image);
        for (unsigned floor = 0; floor < game::dungeon::floor_count; ++floor)
            if (!tables.object_at(floor, 0x88) && !tables.object_at(floor, 0xA8)) {
                m.memory().write(game::dungeon::floor, floor);
                break;
            }
    }
    if (kind >= 12 && kind < 16) {
        m.cpu().ix_iy[1].uint16_value = 0xC400 + 16 * (kind - 12);
        for (unsigned field = 0; field < 16; ++field)
            m.memory().write(0xC400 + 16 * (kind - 12) + field, 0);
    }
    auto controls = [&](const Machine &machine) {
        auto pad = battle_effect_probe::press_both(machine);
        if (kind >= 16)
            pad.one = false;
        return pad;
    };
    battle_effect_probe::compare_call(m, h.registry(), controls, "outcome=" + std::to_string(kind));
    if (kind >= 2 && kind <= 4)
        probe::check(m.peek(0xC202) == 2, "ordinary defeat requests title");
    if (kind == 18)
        probe::check(m.peek(game::dungeon::position) == 0x98,
                     "dungeon escape5 executes owned backstep follow-up");
    if (kind == 16)
        probe::check(menu_probe::word(m, 0xC4E0) == 110, "victory chest awards money once");
    if (kind == 17)
        probe::check(m.peek(0xC4E2) == 1 && m.peek(0xC4C0) == 0x24,
                     "victory chest awards item once");
    if (kind == 11)
        probe::check(m.peek(0xC800) == 0x0E,
                     "treasure object resolves to chest, actual=" + std::to_string(m.peek(0xC800)));
    if (kind >= 12 && kind < 16) {
        auto at = 0xC400 + 16 * (kind - 12);
        probe::check(m.peek(at) == 1 && m.peek(at + 5) == 1 && m.peek(at + 1) == m.peek(at + 6) &&
                         m.peek(at + 2) == m.peek(at + 7),
                     "unlock initializes level/status and fills resources");
    }
}
int scenarios(probe::Harness &harness, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-battle"));
    const auto image = map_probe::start_image(inputs, "first-battle", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "outcome-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from,
                            [](const Machine &m) {
                                return m.peek(0xC29D) != 0 && m.peek(0xC268) != 0 &&
                                       m.peek(0xC26E) == 4;
                            }),
                 "enemy real battle frame");
    for (unsigned kind = 0; kind < 18; ++kind)
        complete_outcome(harness, scene.machine(), kind);
    const auto dungeon_recording = replay::read(inputs.recording("first-dungeon"));
    const auto dungeon_image = map_probe::start_image(inputs, "first-dungeon", dungeon_recording);
    if (!dungeon_image)
        return -1;
    map_probe::Scenario dungeon(inputs.rom, *dungeon_image, "escape-dungeon");
    from = 0;
    probe::check(dungeon.play(dungeon_recording, from,
                              [](const Machine &m) { return m.peek(0xC202) == 0x0B; }),
                 "initialized escape dungeon");
    complete_outcome(harness, dungeon.machine(), 18);
    complete_outcome(harness, scene.machine(), 19);
    return 20;
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
            // The four block-copy slices belong to the block transfer wrapper (Part C1).
            std::vector<game::battle_outcomes::Segment> own;
            for (const auto& segment:game::battle_outcomes::segments)
                if (std::find(game::battle_outcomes::copy_entries.begin(),game::battle_outcomes::copy_entries.end(),segment.entry)==
                    game::battle_outcomes::copy_entries.end()) own.push_back(segment);
            c4::sweep_file(sweeping,"battle_outcomes",own);
        }
        const auto count =
            state_probes(harness) + ending_branches(harness) + escape_restoration_gates(harness);
        seed_windows(harness);
        for (auto entry : game::battle_outcomes::copy_entries)
            check_slices(harness, entry);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Outcome probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
