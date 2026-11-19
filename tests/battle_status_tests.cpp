// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/battle_status.h"
#include "menu_probe.h"
#include "c4_sweep.h"
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_battle_status(registry);
    probe::check(registry.entries().size() == game::battle_status::segments.size(),
                 "status registration count");
    for (const auto &s : game::battle_status::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "status ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::battle_status::segments) {
        for (unsigned seed = 0; seed < 512; ++seed) {
            auto machine =
                harness.prepare(s.entry, {.r = static_cast<std::uint8_t>(seed), .seed = seed});
            for (unsigned address = 0xC000; address < 0xD000; ++address)
                machine.memory().write(address,
                                       static_cast<std::uint8_t>(seed * 23 + address * 11));
            machine.memory().write(0xC267, seed % 4);
            machine.cpu().hl.uint16_value = 0xC400 + 16 * (seed % 4);
            machine.cpu().de.uint16_value = 0xC412;
            harness.run_cpp(machine, s.entry);
            ++count;
        }
        auto boundary = harness.prepare(s.entry, {});
        menu_probe::place_before_boundary(boundary, 1);
        harness.run_fallback(boundary, s.entry, RoutineFallback::frame);
    }
    return count;
}
unsigned strength_gates(probe::Harness &harness) {
    unsigned count = 0;
    for (bool blocking : {false, true})
        for (unsigned leader = 0; leader < 256; ++leader)
            for (unsigned enemy = 0; enemy < 256; ++enemy) {
                const RoutineEntry entry{0, static_cast<std::uint16_t>(blocking ? 0x1F3A : 0x1FE3)};
                auto machine = harness.prepare(entry, {});
                machine.cpu().de.uint16_value = 0xC432;
                machine.cpu().af.uint8_values.at_1 = 90;
                machine.memory().write(0xC267, 3);
                machine.memory().write(0xC408, leader);
                machine.memory().write(0xC438, 255 - leader);
                machine.memory().write(0xC448, enemy);
                machine.memory().write(0xC2E8, 0x20);
                harness.run_cpp(machine, entry);
                const unsigned expected = blocking ? (leader >= enemy ? 0x187D : 0x05B1)
                                                   : (leader >= enemy ? 0x05B1 : 0x31CF);
                probe::check(machine.cpu().pc.uint16_value == expected,
                             "strength gate uses first party/enemy records, not caster");
                probe::check(machine.peek(0xC432) == 90, "points committed before strength gate");
                ++count;
            }
    return count;
}
unsigned restore_fields(probe::Harness &harness) {
    unsigned count = 0;
    for (unsigned member = 0; member < 4; ++member)
        for (unsigned hp = 0; hp < 256; ++hp)
            for (unsigned mp : {0u, 1u, 127u, 255u}) {
                auto machine = harness.prepare({0, 0x20D8}, {});
                const unsigned record = 0xC400 + 16 * member;
                machine.cpu().hl.uint16_value = record;
                machine.cpu().af.uint8_values.at_0 |= 0x40;
                machine.memory().write(record, 0xC0);
                machine.memory().write(record + 1, 0);
                machine.memory().write(record + 2, 0);
                machine.memory().write(record + 6, hp);
                machine.memory().write(record + 7, mp);
                harness.run_cpp(machine, {0, 0x20D8});
                probe::check(machine.peek(record) == 1, "revival resets flags to active");
                probe::check(machine.peek(record + 1) == hp && machine.peek(record + 2) == mp,
                             "revival restores maximum HP and MP");
                ++count;
            }
    return count;
}
unsigned draw_and_flag_cases(probe::Harness &harness) {
    unsigned count = 0;
    for (unsigned draw = 0; draw < 256; ++draw) {
        auto blocking = harness.prepare({0, 0x1F56}, {});
        blocking.cpu().af.uint8_values.at_1 = draw;
        harness.run_cpp(blocking, {0, 0x1F56});
        probe::check(blocking.cpu().pc.uint16_value == (draw % 4 ? 0x187D : 0x31CF),
                     "zero blocking duration fails");
        if (draw % 4)
            probe::check(blocking.cpu().bc.uint8_values.at_0 == draw % 4, "blocking duration1-3");
        auto weakening = harness.prepare({0, 0x1FF6}, {});
        weakening.cpu().af.uint8_values.at_1 = draw;
        harness.run_cpp(weakening, {0, 0x1FF6});
        probe::check(weakening.cpu().pc.uint16_value == (draw < 0xB2 ? 0x187D : 0x31CF),
                     "weakening draw threshold");
        auto boost = harness.prepare({0, 0x1FD4}, {});
        boost.cpu().hl.uint16_value = 0xC400;
        boost.memory().write(0xC400, draw);
        harness.run_cpp(boost, {0, 0x1FD4});
        probe::check(boost.peek(0xC400) == (draw | 0x80), "boost sets only flag7");
        count += 3;
    }
    return count;
}
void complete_effect(probe::Harness &harness, const Machine &base, unsigned kind) {
    constexpr std::uint16_t entries[]{0x1E8A, 0x1E8E, 0x1F36, 0x1F36, 0x1F36, 0x1F36,
                                      0x1FC0, 0x1FC0, 0x1FDF, 0x1FDF, 0x1FDF, 0x1F80,
                                      0x1F80, 0x1F89, 0x1F80, 0x20BF, 0x20BF, 0x20BF};
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
        machine.memory().write(record + 8, 150);
        machine.memory().write(record + 9, 20);
    }
    machine.memory().write(0xC401, 7);
    machine.memory().write(0xC402, 2);
    machine.memory().write(0xC406, 30);
    machine.memory().write(0xC407, 12);
    if (kind == 7 || kind == 15 || kind == 17) {
        machine.memory().write(0xC400, 0);
        machine.memory().write(0xC401, 0);
        machine.memory().write(0xC402, 0);
    }
    for (unsigned enemy = 0; enemy < 8; ++enemy) {
        const unsigned record = 0xC440 + 16 * enemy;
        for (unsigned field = 0; field < 16; ++field)
            machine.memory().write(record + field, 0);
        machine.memory().write(record, kind == 10 ? 0x41 : 1);
        machine.memory().write(record + 1, 100);
        machine.memory().write(record + 8, 10);
        machine.memory().write(record + 13, kind == 5 ? 3 : 0);
    }
    machine.memory().write(0xC408, (kind == 4 || kind == 9) ? 0 : 100);
    machine.memory().write(0xC267, 3);
    machine.memory().write(0xC2C2, 0);
    machine.memory().write(0xC4F0, 3);
    machine.memory().write(0xC2E8, kind == 3 ? 0 : 0x20);
    machine.memory().write(0xC2EF, 0);
    machine.memory().write(0xC2E7, (kind == 12 || kind == 13) ? 0 : 200);
    machine.memory().write(0xC29E, kind == 14 ? 0 : 1);
    machine.memory().write(0xC2C4, kind == 13 ? 1 : 0);
    machine.memory().write(0xC204, 0);
    machine.memory().write(0xC205, 0);
    machine.cpu().ix_iy[1].uint16_value = 0xC430;
    const unsigned spell = kind == 13 ? 0 : battle_effect_probe::spell_id(harness.rom(), entry);
    machine.cpu().bc.uint8_values.at_1 = spell;
    if (kind != 13)
        probe::check(battle_effect_probe::cost(harness.rom(), spell) > 0,
                     "tested spell has a nonzero ROM cost");
    const bool cancel = kind == 17;
    const bool picker = kind >= 15;
    battle_effect_probe::compare_call(
        machine, harness.registry(),
        [=](const Machine &m) {
            const bool press = ((m.cycles() / Machine::frame_cycles) / 3) % 2;
            return Pad{.one = press && (!picker || cancel), .two = press};
        },
        "status kind=" + std::to_string(kind));
    probe::check(machine.peek(0xC432) ==
                     (kind == 13 ? 100 : 100 - battle_effect_probe::cost(harness.rom(), spell)),
                 "spell spends actual ROM cost once, including revival cancellation");
    if (kind < 2)
        probe::check(machine.peek(0xC2EF) == (kind ? 0x86 : 6), "protection kind and duration");
    if (kind == 2)
        probe::check(machine.peek(0xC44D) == 3, "strong blocking counter starts at three");
    if (kind == 3)
        probe::check(machine.peek(0xC44D) == 0, "permission blocks effect");
    if (kind == 4)
        probe::check(machine.peek(0xC44D) <= 3, "weak blocking duration");
    if (kind == 5)
        for (unsigned enemy = 0; enemy < 8; ++enemy)
            probe::check(machine.peek(0xC44D + 16 * enemy) == 3, "already blocked enemy unchanged");
    if (kind == 6)
        probe::check(machine.peek(0xC400) == 0x81, "active recipient gets boost");
    if (kind == 7)
        probe::check(machine.peek(0xC400) == 0, "inactive boost recipient unchanged");
    if (kind == 9)
        probe::check(machine.peek(0xC440) == 1, "weak leader cannot weaken enemy");
    if (kind == 10)
        for (unsigned enemy = 0; enemy < 8; ++enemy)
            probe::check(machine.peek(0xC440 + 16 * enemy) == 0x41,
                         "already weakened enemy unchanged");
    if (kind == 11)
        probe::check(machine.peek(0xC267) == 5, "escape spell succeeds");
    if (kind == 12 || kind == 13)
        probe::check(machine.peek(0xC267) == 3, "refused escape keeps actor");
    if (kind == 15)
        probe::check(machine.peek(0xC400) == 1 && machine.peek(0xC401) == 30 &&
                         machine.peek(0xC402) == 12,
                     "revival restores inactive target completely");
    if (kind == 16)
        probe::check(machine.peek(0xC400) == 1 && machine.peek(0xC401) == 7 &&
                         machine.peek(0xC402) == 2,
                     "active target refuses revival");
    if (kind == 17)
        probe::check(machine.peek(0xC400) == 0 && machine.peek(0xC401) == 0 &&
                         machine.peek(0xC402) == 0,
                     "cancelled revival changes no recipient fields");
}
int scenarios(probe::Harness &harness, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-battle"));
    const auto image = map_probe::start_image(inputs, "first-battle", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "status-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from,
                            [](const Machine &m) {
                                return m.peek(0xC29D) != 0 && m.peek(0xC268) != 0 &&
                                       m.peek(0xC26E) == 4;
                            }),
                 "status real battle frame");
    for (unsigned kind = 0; kind < 18; ++kind)
        complete_effect(harness, scene.machine(), kind);
    return 18;
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
            c4::sweep_file(sweeping,"battle_status",game::battle_status::segments,[](RoutineEntry entry,Machine& m) {
                // 1FD0 returns at once when Z is set: make it take the call too.
                if (entry.address==0x1FD0) m.cpu().af.uint8_values.at_0&=static_cast<std::uint8_t>(~0x40);
            });
        }
        unsigned count = state_probes(harness);
        count += strength_gates(harness);
        count += restore_fields(harness);
        count += draw_and_flag_cases(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Status probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
