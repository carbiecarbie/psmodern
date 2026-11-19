// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/battle_commands.h"
#include "menu_probe.h"
#include "c4_sweep.h"
#include <iostream>
using namespace psmodern;
namespace {
// The documented equality of the engine and the oracle (milestone 14), plus the
// two host-side records the equality leaves out: the PSG writes waiting to be
// taken and the cartridge RAM dirty flag.
bool same_state(Machine& engine, Machine& original) {
    return oracle::equal(engine,original,oracle::Scope::whole) && engine.take_psg_writes()==original.take_psg_writes() &&
           engine.memory().cartridge_ram_dirty()==original.memory().cartridge_ram_dirty();
}
void registration() {
    RoutineRegistry registry;
    game::register_battle_commands(registry);
    probe::check(registry.entries().size() == game::battle_commands::segments.size(),
                 "command registration count");
    for (const auto &s : game::battle_commands::segments) {
        const auto *routine = registry.find(s.entry);
        probe::check(routine && routine->spec.unit == s.unit, "command segment ownership");
    }
}
unsigned state_probes(probe::Harness &h) {
    unsigned count = 0;
    for (const auto &s : game::battle_commands::segments) {
        for (unsigned seed = 0; seed < 512; ++seed) {
            auto m = h.prepare(s.entry, {.r = static_cast<std::uint8_t>(seed), .seed = seed});
            for (unsigned address = 0xC000; address < 0xD000; ++address)
                m.memory().write(address, static_cast<std::uint8_t>(seed * 29 + address * 13));
            m.memory().write(0xC267, seed % 4);
            m.memory().write(0xC2C2, seed % 4);
            m.cpu().hl.uint16_value = 0xC400 + (seed % 4) * 16;
            m.cpu().de.uint16_value = 0xC402;
            h.run_cpp(m, s.entry);
            ++count;
        }
        auto boundary = h.prepare(s.entry, {});
        menu_probe::place_before_boundary(boundary, 1);
        h.run_fallback(boundary, s.entry, RoutineFallback::frame);
    }
    return count;
}
void action_records(probe::Harness &h) {
    for (unsigned actor = 0; actor < 4; ++actor)
        for (unsigned value = 0; value < 256; ++value) {
            auto m = h.prepare({0, 0x1BB9}, {.seed = actor * 256 + value});
            m.memory().write(0xC267, actor);
            const auto base = static_cast<std::uint16_t>(0xC2AC + actor * 4);
            m.memory().write(base + 3, 0xA5);
            m.cpu().af.uint8_values.at_1 = value;
            m.cpu().bc.uint8_values.at_1 = value ^ 0x55;
            m.cpu().bc.uint8_values.at_0 = value ^ 0xAA;
            h.run_cpp(m, {0, 0x1BB9});
            probe::check(m.peek(base) == (value ^ 0xAA) && m.peek(base + 1) == (value ^ 0x55) &&
                             m.peek(base + 2) == value && m.peek(base + 3) == 0xA5,
                         "three action fields, fourth preserved");
            m.cpu().pc.uint16_value = 0x1BCE;
            m.cpu().sp.uint16_value = 0xDF00;
            h.run_cpp(m, {0, 0x1BCE});
            probe::check(m.cpu().af.uint8_values.at_1 == (value ^ 0xAA) &&
                             m.cpu().bc.uint8_values.at_1 == (value ^ 0x55) &&
                             m.cpu().bc.uint8_values.at_0 == value,
                         "read action fields");
        }
}
// Boundary values prove the strict comparison: equality refuses escape.
unsigned escape_thresholds(probe::Harness &harness) {
    unsigned count = 0;
    for (unsigned threshold = 0; threshold < 256; ++threshold) {
        for (unsigned draw = 0; draw < 256; ++draw) {
            auto machine = harness.prepare({0, 0x19AC}, {});
            machine.cpu().af.uint8_values.at_1 = draw;
            machine.cpu().bc.uint8_values.at_1 = threshold;
            machine.memory().write(0xC29E, 1);
            harness.run_cpp(machine, {0, 0x19AC});
            if (draw < threshold) {
                probe::check(machine.peek(0xC267) == 5, "passing escape result");
                probe::check(machine.cpu().pc.uint16_value == 0x0180, "passing escape returns");
            } else {
                probe::check(machine.cpu().pc.uint16_value == 0x31CF,
                             "failed escape shows message");
            }
            ++count;
        }
    }
    return count;
}
// Exercise a complete command using initialized game video/audio state. Every
// replacement is checked on each call, and the whole call matches the oracle.
void complete_command(probe::Harness &h, const Machine &base, unsigned kind, unsigned actor,
                      bool cancel) {
    constexpr std::uint16_t entries[]{0x191C, 0x1AD0, 0x1AFD, 0x1AB1, 0x1ADE, 0x192B, 0x199C};
    const auto entry = entries[kind];
    auto machine = map_probe::prepare_at(
        base, {0, entry}, {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = actor});
    machine.memory().write(0xC267, actor);
    machine.memory().write(0xC2C2, actor);
    machine.memory().write(0xC4F0, 1);
    machine.memory().write(0xC2E8, cancel ? 0 : 0x80);
    machine.memory().write(0xC2E7, cancel ? 0 : 255);
    machine.memory().write(0xC448, 10);
    machine.memory().write(0xC408, 100);
    for (unsigned member = 0; member < 4; ++member)
        machine.memory().write(0xC400 + 16 * member, 1);
    machine.cpu().bc.uint8_values.at_1 = 1;
    machine.cpu().bc.uint8_values.at_0 = 0;
    Machine original = machine;
    auto run = [&](Machine &m, bool enabled) {
        const auto begin = m.cycles();
        (void)m.take_psg_writes();
        (void)m.take_samples();
        decltype(m.take_psg_writes()) writes;
        for (unsigned steps = 0; m.cpu().pc.uint16_value != 0x0180; ++steps) {
            probe::check(steps < 20000000, "command failed to return kind " + std::to_string(kind));
            if (steps % 256 == 0) {
                auto block = m.take_psg_writes();
                writes.insert(writes.end(), block.begin(), block.end());
                (void)m.take_samples();
            }
            const bool press = ((m.cycles() - begin) / (Machine::frame_cycles * 3)) % 2;
            m.set_pad(0, Pad{.one = press && cancel, .two = press});
            if (enabled)
                m.step(h.registry());
            else
                m.step();
        }
        auto block = m.take_psg_writes();
        writes.insert(writes.end(), block.begin(), block.end());
        (void)m.take_samples();
        return writes;
    };
    const auto expected = run(original, false), actual = run(machine, true);
    probe::check(expected == actual, "command sound writes/cycles");
    probe::check(same_state(machine, original), "whole command differs from original");
    if (kind < 3 || ((kind == 3 || kind == 4) && !cancel))
        probe::check(machine.peek(0xC267) == actor + 1, "queued command advances actor");
    if ((kind == 3 || kind == 4) && cancel)
        probe::check(machine.peek(0xC267) == actor, "cancelled command retains actor");
}
int scenarios(probe::Harness &h, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("later-town"));
    const auto image = map_probe::start_image(inputs, "later-town", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "command-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from, [](const Machine &m) { return map_probe::on_map(m); }),
                 "command real map frame");
    unsigned count = 0;
    for (unsigned kind = 0; kind < 7; ++kind)
        for (unsigned actor = 0; actor < (kind < 3 ? 4u : 1u); ++actor)
            for (bool cancel : {false, true}) {
                complete_command(h, scene.machine(), kind, actor, cancel);
                ++count;
            }
    return static_cast<int>(count);
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
            c4::sweep_file(sweeping,"battle_commands",game::battle_commands::segments);
        }
        const auto count = state_probes(harness) + escape_thresholds(harness);
        action_records(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Command probes: " << count << "; complete calls: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
