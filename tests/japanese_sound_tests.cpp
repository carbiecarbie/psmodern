// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/japanese_sound.h"
#include "machine/machine.h"
#include "oracle/lockstep.h"
#include "rom/sha1.h"
#include <array>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace {
using namespace psmodern;

constexpr std::uint16_t initialize = 0x8000, silence = 0x801F, update = 0x8052, dispatch = 0x8466;
constexpr std::uint16_t call_return = 0xDFF0;
struct Rig {
    Machine engine;
    oracle::NativeMachine original;
    RoutineRegistry& routines;
    Console::PortTrace actual_ports, expected_ports;
    Console::WriteTrace actual_writes, expected_writes;
    unsigned boundaries = 0, fm_writes = 0;
    unsigned request = 0, tick = 0;

    Rig(std::shared_ptr<const Memory::Rom> rom, RoutineRegistry& registry)
        : engine(rom), original(rom), routines(registry), actual_ports(engine), expected_ports(original),
          actual_writes(engine), expected_writes(original) {
        for (auto* m : std::array<Console*, 2>{&engine, &original}) {
            m->memory().write(0xFFFF, 12);
            m->memory().write(0xC000, 1);
            m->cpu().iff1 = m->cpu().iff2 = 0;
        }
    }

    [[noreturn]] void fail(std::uint16_t pc, const char* detail) {
        std::ostringstream msg;
        msg << "JP FM request " << std::hex << request << " tick " << std::dec << tick
            << " at JP 0C:" << std::hex << std::setw(4) << std::setfill('0') << pc << ": " << detail;
        for (const auto& diff : oracle::differences(engine, original, oracle::Scope::whole)) msg << '\n' << diff;
        throw std::runtime_error(msg.str());
    }

    void submit(std::uint8_t command) {
        request = command;
        original.memory().write(0xC004, command);
        engine.memory().write(0xC004, command);
    }

    void call(std::uint16_t entry) {
        for (auto* m : std::array<Console*, 2>{&engine, &original}) {
            m->cpu().sp.uint16_value = call_return - 2;
            m->cpu().pc.uint16_value = entry;
            m->memory().write(call_return - 2, static_cast<std::uint8_t>(call_return));
            m->memory().write(call_return - 1, call_return >> 8);
            if (entry == dispatch) {
                // Same alternate registers the FM tick installs before dispatch.
                m->cpu().hl_.uint16_value = 0xC00C;
                m->cpu().af_.uint16_value = 0x0100;
            }
        }
        const auto start = engine.cycles();
        while (engine.cpu().pc.uint16_value != call_return) {
            const auto pc = engine.cpu().pc.uint16_value;
            if (engine.cycles() - start >= Machine::frame_cycles || pc < 0x8000 || pc >= 0xC000)
                fail(pc, "call exceeded its private execution window");
            engine.step(routines);
            while (original.cycles() < engine.cycles()) original.step();
            original.settle();
            if (original.cpu().resume || !oracle::equal(engine, original, oracle::Scope::boundary))
                fail(pc, "registers, clock or bus state diverged");
            if (actual_writes.writes() != expected_writes.writes()) fail(pc, "ordered memory writes diverged");
            if (actual_ports.accesses() != expected_ports.accesses()) fail(pc, "timestamped port accesses diverged");
            for (const auto& port : actual_ports.accesses()) {
                if (!port.write || (port.port != 0x7F && port.port != 0xF0 && port.port != 0xF1))
                    fail(pc, "unexpected port access");
                if (port.port == 0xF1) ++fm_writes;
            }
            for (const auto& write : actual_writes.writes()) {
                if (!((write.address >= 0xC000 && write.address <= 0xC1EE) ||
                      (write.address >= 0xDF00 && write.address < call_return)))
                    fail(pc, "unexpected work RAM or stack write");
            }
            actual_writes.writes().clear();
            expected_writes.writes().clear();
            actual_ports.accesses().clear();
            expected_ports.accesses().clear();
            ++boundaries;
        }
        if (engine.cpu().sp.uint16_value != call_return || engine.cycles() - start > Machine::frame_cycles)
            fail(entry, "unbalanced return or call exceeded one frame");
        if (!oracle::equal(engine, original, oracle::Scope::whole)) fail(entry, "full console mismatch");
        for (auto* m : std::array<Console*, 2>{&engine, &original}) {
            m->take_samples();
            m->take_psg_writes();
        }
        if (entry == update) ++tick;
    }
};

// Exercise rare branches independently of the performance the shipped scores
// happen to play. Compare each instruction in the native piece, including
// the alternate registers, undocumented flags and scratch stack accesses.
void check_each_segment(const std::shared_ptr<const Memory::Rom>& rom, RoutineRegistry& routines) {
    unsigned cases = 0;
    for (const auto& [entry, routine] : routines.entries()) {
        (void)routine;
        for (const std::uint8_t value : std::array<std::uint8_t, 8>{0, 1, 2, 0x10, 0x7F, 0x80, 0x83, 0xFF}) {
            Machine engine(rom);
            engine.memory().write(0xFFFF, 12);
            for (unsigned a = 0xC001; a <= 0xC1EE; ++a)
                engine.memory().write(static_cast<std::uint16_t>(a), value);
            engine.memory().write(0xC000, 1);
            auto& r = engine.cpu();
            r.pc.uint16_value = entry.address;
            r.sp.uint16_value = 0xDFD0;
            r.ix_iy[0].uint16_value = 0xC04E;
            r.af.uint16_value = static_cast<std::uint16_t>((value << 8) | value);
            r.af_.uint16_value = 0x0100;
            r.hl.uint16_value = 0xC080;
            r.hl_.uint16_value = 0xC00C;
            r.de.uint16_value = 0xC0C0;
            r.bc.uint16_value = 0x04F1;
            r.r = value;
            r.iff1 = r.iff2 = 0;
            oracle::Lockstep comparison(engine, "Japanese FM segment", oracle::Mode::instructions);
            comparison.step(engine, &routines);
            ++cases;
        }
    }
    std::cout << cases << " seeded native pieces matched at instruction boundaries.\n";
}

void check_mode_guard(const std::shared_ptr<const Memory::Rom>& rom, RoutineRegistry& routines) {
    for (const auto entry : {initialize, silence, update, dispatch}) {
        Machine engine(rom);
        engine.memory().write(0xFFFF, 12);
        engine.memory().write(0xC000, 0);
        engine.cpu().pc.uint16_value = entry;
        try {
            engine.step(routines);
        } catch (const UnhandledState&) {
            if (engine.cycles() != 0) throw std::runtime_error("Wrong-mode entry advanced the clock");
            continue;
        }
        throw std::runtime_error("Japanese PSG mode entered the private FM registry");
    }
    Machine engine(rom);
    engine.memory().write(0xFFFF, 12);
    engine.memory().write(0xC000, 1);
    engine.cpu().pc.uint16_value = dispatch;
    engine.cpu().af_.uint16_value = 0x0040;
    try {
        engine.step(routines);
    } catch (const UnhandledState&) {
        return;
    }
    throw std::runtime_error("Direct dispatch accepted the alternate PSG condition");
}

void check_all_requests(const std::shared_ptr<const Memory::Rom>& rom, RoutineRegistry& routines) {
    unsigned long long comparisons = 0;
    for (unsigned cmd = 0x80; cmd <= 0xFF; ++cmd) {
        Rig rig(rom, routines);
        rig.call(initialize);
        rig.call(update);
        rig.call(update);
        rig.submit(static_cast<std::uint8_t>(cmd));
        const auto prior = rig.fm_writes;
        const bool song = cmd >= 0x81 && cmd <= 0x94;
        // Long songs exercise repeated score calls and loop counters. Effects
        // normally finish much sooner; leave time for their music restoration.
        const bool effect = cmd >= 0xA0 && cmd <= 0xD4;
        unsigned duration = 24;
        if (song) duration = cmd == 0x81 || cmd == 0x8B ? 7200 : 2400;
        if (effect) duration = 360;
        for (unsigned i = 0; i < duration; ++i) rig.call(update);
        if ((song || effect) && rig.fm_writes == prior)
            rig.fail(update, "playable request never touched the FM data port");
        comparisons += rig.boundaries;
    }
    std::cout << "All request bytes, " << comparisons << " native segment boundaries.\n";
}

void check_collisions(const std::shared_ptr<const Memory::Rom>& rom, RoutineRegistry& routines) {
    Rig rig(rom, routines);
    rig.call(initialize);
    rig.call(silence);
    rig.call(initialize);
    for (unsigned first = 0x81; first <= 0x94; ++first) {
        rig.submit(static_cast<std::uint8_t>(first));
        rig.call(update);
        for (const auto command : {0xA5, 0xA5, 0xD0, 0xD5, 0xD6, 0xD6, 0xD7, 0xD8, 0xD9}) {
            rig.submit(static_cast<std::uint8_t>(command));
            rig.call(dispatch);
            rig.call(update);
            if (command == 0xD6 && rig.engine.peek(0xC004) != 0xD6)
                rig.fail(dispatch, "D6 lost its pending repeat");
            const unsigned delay = command == 0xD8 ? 160 : 12;
            for (unsigned i = 0; i < delay; ++i) rig.call(update);
        }
    }
    rig.submit(0x85);
    rig.call(update);
    for (unsigned i = 0; i < 100; ++i) rig.call(update);
    rig.call(silence);
    rig.call(update);
    rig.call(initialize);
    rig.call(update);
    std::cout << "Priority, direct dispatch, repeated effects, five controls, silence and reset matched.\n";
}
}

int main(int argc, char** argv) {
    if (argc != 2 || !*argv[1]) return 77;
    try {
        std::ifstream input(argv[1], std::ios::binary);
        if (!input) throw std::runtime_error("Cannot read configured Japanese sound ROM");
        auto rom = std::make_shared<const psmodern::Memory::Rom>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>{});
        if (psmodern::sha1(*rom) != "c9a40ddd217c58dddcd6b5c0fe66c3a50d3e68e4")
            throw std::runtime_error("Japanese sound test requires the verified whole Japanese cartridge");
        psmodern::RoutineRegistry registry;
        psmodern::game::register_japanese_sound(registry);
        check_mode_guard(rom, registry);
        check_each_segment(rom, registry);
        check_all_requests(rom, registry);
        check_collisions(rom, registry);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
