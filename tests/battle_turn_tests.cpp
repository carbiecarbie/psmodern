// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/battle_turn.h"
#include "menu_probe.h"
#include <algorithm>
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_battle_turn(registry);
    probe::check(registry.entries().size() == game::battle_turn::segments.size(),
                 "turn registration count");
    for (const auto &s : game::battle_turn::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "turn ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::battle_turn::segments) {
        for (unsigned seed = 0; seed < 1024; ++seed) {
            auto machine =
                harness.prepare(s.entry, {.r = static_cast<std::uint8_t>(seed), .seed = seed});
            for (unsigned address = 0xC000; address < 0xD000; ++address)
                machine.memory().write(address,
                                       static_cast<std::uint8_t>(seed * 37 + address * 17));
            machine.cpu().hl.uint16_value = 0xC2A0 + seed % 12;
            machine.memory().write(0xC267, seed % 8);
            harness.run_cpp(machine, s.entry);
            ++count;
        }
        auto boundary = harness.prepare(s.entry, {});
        menu_probe::place_before_boundary(boundary, 1);
        harness.run_fallback(boundary, s.entry, RoutineFallback::frame);
    }
    return count;
}
void cleared_actions(probe::Harness &harness) {
    auto machine = harness.prepare({0, 0x1055}, {});
    for (unsigned offset = 0; offset < 16; ++offset)
        machine.memory().write(0xC2AC + offset, 0xA5);
    harness.run_cpp(machine, {0, 0x1055});
    for (unsigned offset = 0; offset < 16; ++offset)
        probe::check(machine.peek(0xC2AC + offset) == 0, "all action bytes cleared");
    probe::check(machine.peek(0xC29D) == 0xFF && machine.peek(0xC267) == 0 &&
                     machine.peek(0xC2D4) == 0,
                 "new command phase markers");
}
void battle_outcomes(probe::Harness &harness) {
    for (unsigned outcome = 0; outcome < 4; ++outcome) {
        auto machine = harness.prepare({0, 0x10E7}, {});
        machine.memory().write(0xDF00, 0xA0);
        machine.memory().write(0xDF01, 0xC2);
        machine.memory().write(0xDF02, 0);
        machine.memory().write(0xDF03, 1);
        for (unsigned enemy = 0; enemy < 8; ++enemy)
            machine.memory().write(0xC441 + 16 * enemy, outcome == 0 ? 0 : 1);
        for (unsigned member = 0; member < 4; ++member)
            machine.memory().write(0xC400 + 16 * member, outcome == 1 ? 0 : 1);
        machine.memory().write(0xC267, outcome == 2 ? 5 : 0);
        harness.run_cpp(machine, {0, 0x10E7});
        constexpr unsigned destinations[]{0x1656, 0x1602, 0x163E, 0x111C};
        probe::check(machine.cpu().pc.uint16_value == destinations[outcome],
                     "post-action battle outcome");
    }
}
void complete_shuffles(probe::Harness &harness) {
    for (unsigned seed = 0; seed < 128; ++seed) {
        auto machine = harness.prepare({0, 0x18F2}, {.seed = seed});
        for (unsigned actor = 0; actor < 12; ++actor)
            machine.memory().write(0xC2A0 + actor, actor);
        // Vary both bytes of the original random state across all seeds.
        const auto state = static_cast<std::uint16_t>(harness.rom().byte(0, 0x05B3) |
                                                      (harness.rom().byte(0, 0x05B4) << 8));
        machine.memory().write(state, seed);
        machine.memory().write(state + 1, seed == 0 ? 0 : seed ^ 0xA5);
        Machine original = machine;
        auto run = [&](Machine &m, bool enabled) {
            for (unsigned steps = 0; m.cpu().pc.uint16_value != 0x0180; ++steps) {
                probe::check(steps < 100000, "shuffle failed to return");
                if (enabled)
                    m.step(harness.registry());
                else
                    m.step();
            }
        };
        run(original, false);
        run(machine, true);
        probe::check(original.differences(machine).empty(),
                     "whole shuffle differs, including random state and cycles");
        std::array<unsigned, 12> actors{};
        for (unsigned i = 0; i < 12; ++i)
            actors[i] = machine.peek(0xC2A0 + i);
        std::sort(actors.begin(), actors.end());
        for (unsigned i = 0; i < 12; ++i)
            probe::check(actors[i] == i, "shuffle keeps all twelve actor IDs");
    }
}
} // namespace
int main(int argc, char **argv) {
    try {
        registration();
        if (argc == 1)
            return 0;
        probe::check(argc == 3, "Expected ROM and version");
        const auto rom = probe::load(argv[1], argv[2]);
        if (!rom)
            return 77;
        probe::Harness harness(*rom);
        const auto count = state_probes(harness);
        cleared_actions(harness);
        battle_outcomes(harness);
        complete_shuffles(harness);
        std::cout << "Turn probes: " << count << "; complete shuffles: 128\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
