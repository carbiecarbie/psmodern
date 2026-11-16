// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/menu_kit.h"
#include "game/recovery.h"
#include "menu_probe.h"
#include "probe.h"
#include <iostream>
#include <set>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_recovery(registry);
    probe::check(registry.entries().size() == game::recovery::segments.size(),
                 "recovery registration count");
    for (const auto &s : game::recovery::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "recovery ownership");
}

unsigned probe_segment_states(probe::Harness &h, const probe::Rom &) {
    unsigned probes = 0;
    for (const auto &s : game::recovery::segments)
        for (unsigned seed = 0; seed < 128; ++seed) {
            auto m = h.prepare(s.entry, {.r = static_cast<std::uint8_t>(seed), .seed = seed});

            // Keep all potential inherited words writable and distinct from party records.
            for (unsigned at = 0xC000; at < 0xD000; ++at)
                m.memory().write(at, static_cast<std::uint8_t>(seed * 31 + at));
            m.memory().write(0xC267, seed % 4);
            m.memory().write(0xC2C2, seed % 4);
            m.cpu().hl.uint16_value = 0xC400 + (seed % 4) * 16;
            m.cpu().de.uint16_value = 0xC402;
            h.run_cpp(m, s.entry);
            ++probes;
        }
    return probes;
}

unsigned probe_hp_saturation(probe::Harness &h, const probe::Rom &) {
    unsigned probes = 0;
    // Exhaust the entire HP-byte and amount-byte domain, including overflow and
    // maximums below current HP. Expectations are separate from the instruction kit.
    for (unsigned hp = 0; hp < 256; ++hp)
        for (unsigned amount = 0; amount < 256; ++amount) {
            auto m = h.prepare({0, 0x1E72}, {.seed = hp * 256 + amount});
            m.cpu().hl.uint16_value = 0xC400;
            m.cpu().de.uint8_values.at_1 = amount;
            const unsigned maximum = (hp + amount * 7) % 256;
            m.memory().write(0xC401, hp);
            m.memory().write(0xC406, maximum);
            h.run_cpp(m, {0, 0x1E72});
            probe::check(m.peek(0xC401) == std::min(maximum, std::min(255u, hp + amount)),
                         "HP saturation");
            ++probes;
        }
    return probes;
}

unsigned probe_magic_balances(probe::Harness &h, const probe::Rom &rom) {
    unsigned probes = 0;
    // Valid spell indices, every possible MP balance, all caster slots. This
    // lookup reports insufficient MP through carry and never commits a debit.
    for (unsigned caster = 0; caster < 4; ++caster)
        for (unsigned spell = 0; spell < 19; ++spell)
            for (unsigned mp = 0; mp < 256; ++mp) {
                auto m = h.prepare({0, 0x1B87}, {});
                m.memory().write(0xC267, caster);
                m.cpu().af.uint8_values.at_1 = spell;
                m.memory().write(0xC402 + caster * 16, mp);
                h.run_cpp(m, {0, 0x1B87});
                const unsigned cost = rom.byte(0, 0x1DDC + spell);
                probe::check(m.peek(0xC402 + caster * 16) == mp, "cost lookup did not spend MP");
                probe::check(m.cpu().af.uint8_values.at_1 == static_cast<std::uint8_t>(mp - cost),
                             "MP remainder");
                probe::check(bool(m.cpu().af.uint8_values.at_0 & 1) == (mp < cost),
                             "insufficient MP carry");
                ++probes;
            }
    return probes;
}

void probe_food_cancellation(probe::Harness &h, const probe::Rom &rom) {
    (void)rom;
    // Cancellation leaves both HP and item count untouched and returns to the
    // original field/battle continuation without displaying the use message.
    for (unsigned battle = 0; battle < 2; ++battle) {
        auto m = h.prepare({0, 0x2346}, {});
        m.cpu().bc.uint8_values.at_0 = 0x10;
        m.memory().write(0xDF00, 0);
        m.memory().write(0xDF01, 10);
        m.memory().write(0xDF02, 0x80);
        m.memory().write(0xDF03, 1);
        m.memory().write(0xC29D, battle);
        m.memory().write(0xC4E2, 2);
        m.memory().write(0xC401, 7);
        h.run_cpp(m, {0, 0x2346});
        probe::check(m.peek(0xC4E2) == 2 && m.peek(0xC401) == 7, "cancelled food unchanged");
        probe::check(m.cpu().pc.uint16_value == (battle ? 0x0180 : 0x36BB),
                     "food cancel continuation");
    }
}

void probe_frame_admission(probe::Harness &h, const probe::Rom &rom) {
    (void)rom;
    // Registration admission still obeys the existing frame and interrupt rules.
    for (const auto &s : game::recovery::segments) {
        auto m = h.prepare(s.entry, {});
        menu_probe::place_before_boundary(m, 1);
        h.run_fallback(m, s.entry, RoutineFallback::frame);
    }
}

int probe_complete_recovery_calls(probe::Harness &h, const map_probe::Inputs &inputs) {
    // Real initialized display/sound state, replayed on/off from a pinned save.
    const auto recording = replay::read(inputs.recording("later-town"));
    auto image = map_probe::start_image(inputs, "later-town", recording);
    if (!image) {
        std::cerr << "Missing later-town reference image\n";
        return -1;
    }
    map_probe::Scenario scene(inputs.rom, *image, "recovery-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from, [](const Machine &m) { return map_probe::on_map(m); }),
                 "map frame");
    const Machine base = scene.machine();

    unsigned chains = 0;
    for (unsigned kind = 0; kind < 8; ++kind)
        for (unsigned member = 0; member < (kind < 4 ? 4u : 1u); ++member)
            for (unsigned outcome = 0; outcome < 2; ++outcome) {
                const auto entry =
                    static_cast<std::uint16_t>(kind < 2   ? (kind == 0 ? 0x1E46 : 0x1E4A)
                                               : kind < 4 ? (kind == 2 ? 0x2333 : 0x2337)
                                               : kind < 6 ? (kind == 4 ? 0x1E27 : 0x1E2B)
                                                          : (kind == 6 ? 0x2333 : 0x2337));
                auto m = map_probe::prepare_at(
                    base, {0, entry},
                    {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = member});
                m.memory().write(0xC2C2, member);
                m.memory().write(0xC4F0, 1);
                m.memory().write(0xC267, member);
                m.memory().write(0xC29D, kind < 6 ? 1 : 0);
                m.memory().write(0xC400 + member * 16, (kind >= 4 || outcome == 0) ? 1 : 0);
                m.memory().write(0xC401 + member * 16, 7);
                m.memory().write(0xC406 + member * 16, 100);
                m.memory().write(0xC402 + member * 16, 100);
                m.cpu().bc.uint8_values.at_1 = 1;

                // Food removes exactly the chosen item through the existing inventory owner.
                m.memory().write(0xC4E2, 2);
                m.memory().write(0xC4C0, 0x1B);
                m.memory().write(0xC4C1, 0x1C);
                m.memory().write(0xC2C4, 0x1B);
                m.memory().write(0xC29B, 0xC0);
                m.memory().write(0xC29C, 0xC4);
                Machine native = m;
                auto run = [&](Machine &machine, bool enabled) {
                    const auto begin = machine.cycles();
                    (void)machine.take_samples();
                    (void)machine.take_psg_writes();
                    decltype(machine.take_psg_writes()) writes;
                    for (unsigned steps = 0; machine.cpu().pc.uint16_value != 0x0180; ++steps) {
                        probe::check(steps < 20000000, "recovery chain failed to return");
                        if (steps % 256 == 0) {
                            auto block = machine.take_psg_writes();
                            writes.insert(writes.end(), block.begin(), block.end());
                            (void)machine.take_samples();
                        }
                        const bool press =
                            ((machine.cycles() - begin) / (Machine::frame_cycles * 3)) % 2;
                        machine.set_pad(
                            0, Pad{.one = press && (kind < 4 || outcome == 1), .two = press});
                        if (enabled)
                            machine.step(h.registry());
                        else
                            machine.step();
                    }
                    auto block = machine.take_psg_writes();
                    writes.insert(writes.end(), block.begin(), block.end());
                    (void)machine.take_samples();
                    return writes;
                };
                const auto expected_writes = run(native, false), actual_writes = run(m, true);
                probe::check(expected_writes == actual_writes, "ordered PSG writes and cycles");
                probe::check(native.differences(m).empty(),
                             "whole recovery call differs from original");
                const unsigned amount = kind % 4 == 0   ? 20
                                        : kind % 4 == 1 ? 80
                                        : kind % 4 == 2 ? 10
                                                        : 40;
                probe::check(m.peek(0xC401 + member * 16) == (outcome == 0 ? 7 + amount : 7),
                             "whole-call HP reward kind=" + std::to_string(kind) + " member=" +
                                 std::to_string(member) + " outcome=" + std::to_string(outcome) +
                                 " hp=" + std::to_string(m.peek(0xC401 + member * 16)) +
                                 " target=" + std::to_string(m.peek(0xC2C2)));
                if (kind % 4 >= 2)
                    probe::check(m.peek(0xC4E2) == (outcome == 0 ? 1 : 2),
                                 "food consumed exactly once on success");
                else
                    probe::check(m.peek(0xC402 + member * 16) ==
                                     (outcome == 0 ? 100 - inputs.rom.byte(0, 0x1DDD) : 100),
                                 "MP debited only for living target");
                ++chains;
            }
    return static_cast<int>(chains);
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
        unsigned probes = probe_segment_states(harness, inputs->rom);
        probes += probe_hp_saturation(harness, inputs->rom);
        probes += probe_magic_balances(harness, inputs->rom);
        probe_food_cancellation(harness, inputs->rom);
        probe_frame_admission(harness, inputs->rom);
        const int chains = probe_complete_recovery_calls(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Whole recovery chains: " << chains << "\nRecovery probes: " << probes << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
