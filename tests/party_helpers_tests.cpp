// SPDX-License-Identifier: GPL-3.0-or-later
#include "battle_effect_probe.h"
#include "game/party_helpers.h"
#include "menu_probe.h"
#include "c4_sweep.h"
#include <bit>
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
    game::register_party_helpers(registry);
    probe::check(registry.entries().size() == game::party_helpers::segments.size(),
                 "party helper registration count");
    for (const auto &s : game::party_helpers::segments)
        probe::check(registry.find(s.entry) && registry.find(s.entry)->spec.unit == s.unit,
                     "party helper ownership");
}
unsigned state_probes(probe::Harness &harness) {
    unsigned count = 0;
    for (const auto &s : game::party_helpers::segments) {
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
unsigned activity_lookup(probe::Harness &h) {
    unsigned count = 0;
    for (auto entry : {RoutineEntry{0, 0x187A}, RoutineEntry{0, 0x187D}})
        for (unsigned actor = 0; actor < 12; ++actor)
            for (unsigned flags = 0; flags < 256; ++flags) {
                auto m = h.prepare(entry, {});
                m.cpu().af.uint8_values.at_1 = actor;
                m.memory().write(0xC267, actor);
                m.memory().write(0xC400 + 16 * actor, flags);
                h.run_cpp(m, entry);
                probe::check(m.cpu().af.uint8_values.at_1 == actor &&
                                 m.cpu().hl.uint16_value == 0xC400 + 16 * actor,
                             "actor lookup preserves ID and returns record");
                probe::check(bool(m.cpu().af.uint8_values.at_0 & 0x40) == !(flags & 1),
                             "actor activity is exactly bit0");
                ++count;
            }
    return count;
}
unsigned complete_refreshes(probe::Harness &h) {
    unsigned count = 0;
    for (unsigned level = 1; level <= 30; ++level)
        for (unsigned equip = 0; equip < 64; ++equip) {
            auto m = h.prepare({0, 0x17BA}, {.seed = level * 64 + equip});
            std::array<std::array<unsigned, 16>, 4> expected{};
            constexpr unsigned operands[]{0x17C4, 0x17CE, 0x17D8, 0x17E2};
            for (unsigned actor = 0; actor < 4; ++actor) {
                auto &row = expected[actor];
                for (unsigned field = 0; field < 16; ++field)
                    row[field] = 0xA5;
                row[0] = (equip + actor) % 4 == 0 ? 0x80 : 0xC1;
                row[1] = 250;
                row[2] = 200;
                row[5] = (level + actor - 1) % 30 + 1;
                row[10] = equip;
                row[11] = (equip * 3) % 64;
                row[12] = (equip * 7) % 64;
                row[13] = 7;
                for (unsigned field = 0; field < 16; ++field)
                    m.memory().write(0xC400 + 16 * actor + field, row[field]);
                if (!(row[0] & 1))
                    continue;
                const auto base =
                    h.rom().byte(0, operands[actor]) | (h.rom().byte(0, operands[actor] + 1) << 8);
                const auto table = base + row[5] * 8;
                auto value = [&](unsigned offset) { return h.rom().byte(3, table + offset); };
                auto boost = [&](unsigned item) { return h.rom().byte(0, 0x183A + item); };
                row[0] = 1;
                row[13] = 0;
                row[6] = value(0);
                row[8] = (value(1) + boost(row[10])) % 256;
                row[9] = (value(2) + boost(row[11]) + boost(row[12])) % 256;
                row[7] = value(3);
                row[14] = value(6);
                row[15] = value(7);
            }
            auto original = m;
            for (unsigned steps = 0; original.cpu().pc.uint16_value != 0x0180; ++steps) {
                probe::check(steps < 10000, "native refresh returns");
                original.step();
            }
            for (unsigned steps = 0; m.cpu().pc.uint16_value != 0x0180; ++steps) {
                probe::check(steps < 10000, "C++ refresh returns");
                m.step(h.registry());
            }
            probe::check(same_state(m, original), "complete refresh state/cycles");
            for (unsigned actor = 0; actor < 4; ++actor)
                for (unsigned field = 0; field < 16; ++field)
                    probe::check(m.peek(0xC400 + 16 * actor + field) == expected[actor][field],
                                 "table-derived stats and preserved current resources/equipment");
            ++count;
        }
    return count;
}
void complete_warning(probe::Harness &h, const Machine &base, unsigned kind) {
    const unsigned actor = kind % 4;
    const bool active = kind < 4;
    auto m = map_probe::prepare_at(
        base, {0, 0x188E}, {.return_address = 0x0180, .iff1 = true, .iff2 = true, .seed = kind});
    m.cpu().af.uint8_values.at_1 = actor;
    m.cpu().hl.uint16_value = 0xC456;
    m.cpu().bc.uint16_value = 0x1234;
    m.cpu().de.uint16_value = 0x5678;
    m.memory().write(0xC400 + 16 * actor, active ? 1 : 0);
    battle_effect_probe::compare_call(m, h.registry(), battle_effect_probe::press_both,
                                      "inactive actor warning=" + std::to_string(kind));
    probe::check(m.cpu().af.uint8_values.at_1 == actor && m.cpu().hl.uint16_value == 0xC456 &&
                     m.cpu().bc.uint16_value == 0x1234 && m.cpu().de.uint16_value == 0x5678,
                 "warning preserves caller registers");
}
int scenarios(probe::Harness &harness, const map_probe::Inputs &inputs) {
    const auto recording = replay::read(inputs.recording("first-battle"));
    const auto image = map_probe::start_image(inputs, "first-battle", recording);
    if (!image)
        return -1;
    map_probe::Scenario scene(inputs.rom, *image, "party-helpers-base");
    std::uint64_t from = 0;
    probe::check(scene.play(recording, from,
                            [](const Machine &m) {
                                return m.peek(0xC29D) != 0 && m.peek(0xC268) != 0 &&
                                       m.peek(0xC26E) == 4;
                            }),
                 "enemy real battle frame");
    for (unsigned kind = 0; kind < 8; ++kind)
        complete_warning(harness, scene.machine(), kind);
    return 8;
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
            c4::sweep_file(sweeping,"party_helpers",game::party_helpers::segments);
        }
        const auto count =
            state_probes(harness) + activity_lookup(harness) + complete_refreshes(harness);
        const int chains = scenarios(harness, *inputs);
        if (chains < 0)
            return 77;
        std::cout << "Party helper probes: " << count << "; complete effects: " << chains << "\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";
        return 1;
    }
}
