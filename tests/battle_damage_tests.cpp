// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/battle_damage.h"
#include "menu_probe.h"
#include <iostream>
using namespace psmodern;
namespace {
void registration() {
    RoutineRegistry registry;
    game::register_battle_damage(registry);
    probe::check(registry.entries().size() == game::battle_damage::segments.size(),
                 "damage registration count");
    for (const auto &s : game::battle_damage::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "damage ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::battle_damage::segments) {
        for (unsigned seed = 0; seed < 512; ++seed) {
            auto machine =
                harness.prepare(s.entry, {.r = static_cast<std::uint8_t>(seed), .seed = seed});
            for (unsigned address = 0xC000; address < 0xD000; ++address)
                machine.memory().write(address,
                                       static_cast<std::uint8_t>(seed * 31 + address * 19));
            machine.cpu().ix_iy[0].uint16_value = 0xC440;
            machine.cpu().ix_iy[1].uint16_value = 0xC400;
            harness.run_cpp(machine, s.entry);
            ++count;
        }
        auto boundary = harness.prepare(s.entry, {});
        menu_probe::place_before_boundary(boundary, 1);
        harness.run_fallback(boundary, s.entry, RoutineFallback::frame);
    }
    return count;
}
unsigned hit_points_and_death(probe::Harness &harness) {
    unsigned count = 0;
    for (unsigned hp = 0; hp < 256; ++hp)
        for (unsigned amount = 0; amount < 256; ++amount) {
            auto machine = harness.prepare({0, 0x1267}, {.seed = hp * 256 + amount});
            machine.cpu().ix_iy[0].uint16_value = 0xC440;
            machine.memory().write(0xC440, 0xC1);
            machine.memory().write(0xC441, hp);
            machine.memory().write(0xC44D, 7);
            // The amount and flags were held across the battle flash.
            machine.memory().write(0xDF00, 0xA5);
            machine.memory().write(0xDF01, amount);
            machine.memory().write(0xDF02, 0x80);
            machine.memory().write(0xDF03, 1);
            harness.run_cpp(machine, {0, 0x1267});
            const auto sum = hp + amount;
            const unsigned result = sum >= 256 ? sum % 256 : 0;
            probe::check(machine.peek(0xC441) == result, "damage HP subtraction/clamp");
            probe::check(machine.peek(0xC440) == (result ? 0xC1 : 0), "death activity flags");
            probe::check(machine.peek(0xC44D) == (result ? 7 : 0), "death clears blocking status");
            ++count;
        }
    return count;
}
unsigned minimum_damage(probe::Harness &harness) {
    unsigned count = 0;
    for (unsigned level = 0; level < 256; ++level)
        for (unsigned draw = 0; draw < 256; ++draw) {
            auto machine = harness.prepare({0, 0x1254}, {});
            machine.cpu().ix_iy[1].uint16_value = 0xC400;
            machine.cpu().af.uint8_values.at_1 = draw;
            machine.memory().write(0xC405, level);
            harness.run_cpp(machine, {0, 0x1254});
            const unsigned selected = draw % 32;
            if (selected > level)
                probe::check(machine.cpu().pc.uint16_value == 0x05B1,
                             "minimum damage rejection draws again");
            else {
                probe::check(machine.cpu().pc.uint16_value == 0x7BAC,
                             "accepted damage starts flash");
                probe::check(machine.peek(0xDEFF) == static_cast<std::uint8_t>(~selected),
                             "accepted minimum damage is roll plus one");
            }
            ++count;
        }
    return count;
}
unsigned attack_boost(probe::Harness &harness) {
    unsigned count = 0;
    for (unsigned stat = 0; stat < 256; ++stat)
        for (bool boost : {false, true}) {
            auto machine = harness.prepare({0, 0x121D}, {});
            machine.cpu().ix_iy[1].uint16_value = 0xC400;
            machine.memory().write(0xC400, boost ? 0x81 : 1);
            machine.memory().write(0xC408, stat);
            harness.run_cpp(machine, {0, 0x121D});
            const unsigned expected = boost ? std::min(255u, stat + stat / 2) : stat;
            probe::check(machine.cpu().af.uint8_values.at_1 == expected,
                         "attack boost saturates before variance");
            ++count;
        }
    return count;
}
unsigned complete_variances(probe::Harness &harness) {
    unsigned count = 0;
    for (unsigned stat = 0; stat < 256; ++stat)
        for (unsigned seed = 0; seed < 8; ++seed) {
            auto machine = harness.prepare(
                {0, 0x1279}, {.r = static_cast<std::uint8_t>(seed * 17), .seed = stat * 8 + seed});
            machine.cpu().af.uint8_values.at_1 = stat;
            Machine original = machine;
            // Capture the actual native draw, then apply an independent arithmetic
            // statement of the formula rather than reproducing its instructions.
            for (unsigned steps = 0; original.cpu().pc.uint16_value != 0x1284; ++steps) {
                probe::check(steps < 10000, "variance did not finish its random draw");
                original.step();
            }
            const unsigned draw = original.cpu().af.uint8_values.at_1;
            for (unsigned steps = 0; original.cpu().pc.uint16_value != 0x0180; ++steps) {
                probe::check(steps < 10000, "native variance failed to return");
                original.step();
            }
            for (unsigned steps = 0; machine.cpu().pc.uint16_value != 0x0180; ++steps) {
                probe::check(steps < 10000, "C++ variance failed to return");
                machine.step(harness.registry());
            }
            probe::check(original.differences(machine).empty(),
                         "complete variance machine and RNG");
            const unsigned expected = stat / 2 + stat / 4 + (draw * (stat / 4)) / 256;
            probe::check(machine.cpu().af.uint8_values.at_1 == expected, "stat variance formula");
            ++count;
        }
    return count;
}
unsigned incoming_damage(probe::Harness &harness) {
    unsigned count = 0;
    for (unsigned hp = 0; hp < 256; ++hp)
        for (unsigned amount = 0; amount < 256; ++amount) {
            auto m = harness.prepare({0, 0x15C2}, {});
            m.cpu().ix_iy[0].uint16_value = 0xC400;
            m.cpu().af.uint8_values.at_1 = amount;
            m.memory().write(0xC400, 0xC1);
            m.memory().write(0xC401, hp);
            m.memory().write(0xC40D, 7);
            harness.run_cpp(m, {0, 0x15C2});
            unsigned result = hp + amount >= 256 ? (hp + amount) % 256 : 0;
            probe::check(m.peek(0xC401) == result, "incoming HP clamp");
            probe::check(m.peek(0xC400) == (result ? 0xC1 : 0) &&
                             m.peek(0xC40D) == (result ? 7 : 0),
                         "incoming death clears activity/status");
            probe::check(m.peek(0xC2ED) == 0xFF, "incoming hit marker");
            ++count;
        }
    for (unsigned level = 0; level < 256; ++level)
        for (unsigned draw = 0; draw < 256; ++draw) {
            auto m = harness.prepare({0, 0x15B5}, {});
            m.cpu().ix_iy[0].uint16_value = 0xC400;
            m.cpu().af.uint8_values.at_1 = draw;
            m.memory().write(0xC405, level);
            m.memory().write(0xC401, 100);
            harness.run_cpp(m, {0, 0x15B5});
            if (draw % 32 > level)
                probe::check(m.cpu().pc.uint16_value == 0x05B1,
                             "incoming minimum rejects above level");
            else
                probe::check(m.peek(0xC401) == 99 - (draw % 32) / 2,
                             "incoming minimum is half draw plus one");
            ++count;
        }
    for (unsigned stat = 0; stat < 256; ++stat)
        for (unsigned flags : {0u, 0x40u, 0x80u, 0xC0u}) {
            auto m = harness.prepare({0, 0x1579}, {});
            m.cpu().ix_iy[1].uint16_value = 0xC440;
            m.memory().write(0xC440, flags | 1);
            m.memory().write(0xC448, stat);
            harness.run_cpp(m, {0, 0x1579});
            unsigned expected = (flags & 0x80) ? std::min(255u, stat + stat / 2) : stat;
            if (flags & 0x40)
                expected -= expected / 4;
            probe::check(m.cpu().af.uint8_values.at_1 == expected, "incoming boost then weaken");
            ++count;
        }
    for (unsigned seed = 0; seed < 128; ++seed) {
        auto m = harness.prepare({0, 0x1573}, {.seed = seed});
        m.cpu().ix_iy[0].uint16_value = 0xC400;
        m.cpu().ix_iy[1].uint16_value = 0xC440;
        m.memory().write(0xC2EF, seed % 4 == 0 ? 6 : 0);
        m.memory().write(0xC2ED, 0x55);
        m.memory().write(0xC400, 1);
        m.memory().write(0xC401, 100);
        m.memory().write(0xC405, 10);
        m.memory().write(0xC409, seed * 7);
        m.memory().write(0xC440, 1 | ((seed % 4) << 6));
        m.memory().write(0xC448, seed * 13);
        Machine original = m;
        for (unsigned steps = 0; original.cpu().pc.uint16_value != 0x0180; ++steps) {
            probe::check(steps < 100000, "incoming original returns");
            original.step();
        }
        for (unsigned steps = 0; m.cpu().pc.uint16_value != 0x0180; ++steps) {
            probe::check(steps < 100000, "incoming C++ returns");
            m.step(harness.registry());
        }
        probe::check(original.differences(m).empty(), "incoming full state/cycles/RNG");
        if (seed % 4 == 0)
            probe::check(m.peek(0xC401) == 100 && m.peek(0xC2ED) == 0,
                         "protection prevents incoming hit");
        ++count;
    }
    return count;
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
        unsigned count = state_probes(harness);
        count += hit_points_and_death(harness);
        count += minimum_damage(harness);
        count += attack_boost(harness);
        count += complete_variances(harness);
        count += incoming_damage(harness);
        std::cout << "Damage probes and complete variances: " << count << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
