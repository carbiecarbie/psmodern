// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 19: the stop sweep of 2026-11-23, re-verified (plan decision 6).
//
//   stop_sweep_tests <rom> <us|br> guards
//     Both sides of every explicit entry guard of the game's registry
//     (RegisteredRoutine::guard). Each guard family below lists states read
//     from its predicate: some it must admit and some it must refuse, at the
//     edges of every range it checks. For every guarded entry each state is
//     evaluated, and the engine itself is shown to refuse the entry with
//     reason `guard` in a refused state. A guard with no family here, or a
//     change in the number of guarded entries, fails the test.
//
//   stop_sweep_tests <rom> <us|br> control <recordings>
//     The positive control: the route that stopped every release from 1.0.0
//     to 1.3.0. From the dungeon of the first-dungeon recording (replayed to
//     the dungeon loop's wait), the party stands in front of the object of
//     dungeon 0 cell E3 and the object check (01:6963) runs under strict
//     lockstep with the original. The event it loads (A3) goes through the
//     interaction dispatcher (01:474B) to the item place at 01:5538, which must
//     call the field menu (00:1BE1) with its return address 01:553B.
//
//   stop_sweep_tests <rom> <us|br> route <recordings> <census.tsv> object <floor> <cell>
//   stop_sweep_tests <rom> <us|br> route <recordings> <census.tsv> interaction <event>
//     A prepared-state route (the mechanism for the playthrough group, label
//     playthrough, list tests/playthrough_routes.txt): from the same fixture,
//     a dungeon object (floor and cell, decimal and hex) or an interaction
//     event (hex, numbered from 1) runs under strict lockstep, buttons pressed
//     in a fixed rhythm, until the main loop (00:00AF) is reached at the top
//     level. A stop, a divergence or running out of frames fails; the engine
//     census of the route is written for the coverage categories.
// Addresses and RAM locations only; tables and records come from the ROM.
#include "probe.h"
#include "game/dungeon.h"
#include "game/dungeon_tables.h"
#include "oracle/native_machine.h"
#include "replay/recording.h"
#include <array>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>

using namespace psmodern;
namespace d = game::dungeon;

namespace {
std::string hex(unsigned value, int width) {
    std::ostringstream text;
    text << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
    return text.str();
}
std::string location(RoutineEntry entry) { return hex(entry.bank, 2) + ':' + hex(entry.address, 4); }

// ---- guards ------------------------------------------------------------------
// One state for a guard: what it sets on a prepared machine (work RAM on, ROM
// banks 0, 1 and the entry's own, SP DF00) and whether the guard must hold.
struct State {
    const char* what;
    std::function<void(Machine&)> set;
    bool holds;
};
void set_c(Machine& m, std::uint8_t c) { m.cpu().bc.uint8_values.at_0 = c; }
void set_de(Machine& m, std::uint16_t v) { m.cpu().de.uint16_value = v; }
void set_hl(Machine& m, std::uint16_t v) { m.cpu().hl.uint16_value = v; }
void set_ix(Machine& m, std::uint16_t v) { m.cpu().ix_iy[0].uint16_value = v; }
void set_sp(Machine& m, std::uint16_t v) { m.cpu().sp.uint16_value = v; }
void work_ram_off(Machine& m) { m.memory().set_memory_control(m.memory().memory_control() | 0x10); }

std::vector<State> exact_hl(std::uint16_t value) {
    return {{"HL at the record", [=](Machine& m) { set_hl(m, value); }, true},
            {"HL one below", [=](Machine& m) { set_hl(m, static_cast<std::uint16_t>(value - 1)); }, false},
            {"HL one above", [=](Machine& m) { set_hl(m, static_cast<std::uint16_t>(value + 1)); }, false}};
}
// IX within [low, high]; a register-only guard.
std::vector<State> ix_range(std::uint16_t low, std::uint16_t high) {
    return {{"IX lowest", [=](Machine& m) { set_ix(m, low); }, true},
            {"IX highest", [=](Machine& m) { set_ix(m, high); }, true},
            {"IX below", [=](Machine& m) { set_ix(m, static_cast<std::uint16_t>(low - 1)); }, false},
            {"IX above", [=](Machine& m) { set_ix(m, static_cast<std::uint16_t>(high + 1)); }, false}};
}
// A register in [low, high] (set by `reg`, others valid) and SP in
// [sp_low, sp_high] with the word `below` bytes under SP in work RAM.
std::vector<State> register_and_stack(void (*reg)(Machine&, std::uint16_t), const char* name, std::uint16_t low,
                                      std::uint16_t high, std::uint16_t sp_low, std::uint16_t sp_high) {
    auto both = [=](std::uint16_t r, std::uint16_t sp) {
        return [=](Machine& m) { reg(m, r); set_sp(m, sp); };
    };
    const std::string n = name;
    // Each refused state fails exactly one of the predicate's conditions
    // (review 2026-11-25: conditions tested only together). "SP below" alone
    // does not do that where the word under SP then leaves work RAM too (SP
    // C001 with the word at SP-2), so "SP 0001" fails the lower bound alone:
    // the word under it wraps to FFFD-FFFF, which is work RAM.
    return {{"register lowest, SP lowest", both(low, sp_low), true},
            {"register highest, SP highest", both(high, sp_high), true},
            {"register below", both(static_cast<std::uint16_t>(low - 1), sp_low), false},
            {"register above", both(static_cast<std::uint16_t>(high + 1), sp_low), false},
            {"SP below", both(low, static_cast<std::uint16_t>(sp_low - 1)), false},
            {"SP 0001, the word under it in work RAM", both(low, 0x0001), false},
            {"SP above", both(low, static_cast<std::uint16_t>(sp_high + 1)), false},
            {"work RAM off", [=](Machine& m) { both(low, sp_high)(m); work_ram_off(m); }, false}};
}

// The open bus holds the last byte the processor read or wrote; with the
// cartridge disabled (memory control bit 6, 0000h-BFFFh unmapped) a guard that
// reads its operands from the ROM reads that byte instead. The state sets it
// as the processor does: LD A,(C100) run from work RAM by the oracle's core
// (no rewritten code takes part), the registers and the bytes used restored.
void set_open_bus(Machine& m, std::uint8_t value) {
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    const auto cpu = m.cpu();
    const std::array<std::uint16_t, 4> used{0xC000, 0xC001, 0xC002, 0xC100};
    std::array<std::uint8_t, 4> kept{};
    for (std::size_t i = 0; i < used.size(); ++i) kept[i] = m.peek(used[i]);
    m.memory().write(0xC000, 0x3A);
    m.memory().write(0xC001, 0x00);
    m.memory().write(0xC002, 0xC1);
    m.memory().write(0xC100, value);
    m.cpu().pc.uint16_value = 0xC000;
    m.cpu().iff1 = m.cpu().iff2 = 0;
    (void)m.step();
    probe::check(m.open_bus() == value && m.cpu().pc.uint16_value == 0xC003, "the open bus was not set");
    for (std::size_t i = 0; i < used.size(); ++i) m.memory().write(used[i], kept[i]);
    m.cpu() = cpu;
}
void cartridge_off(Machine& m) { m.memory().set_memory_control(m.memory().memory_control() | 0x40); }
// The money addition's guard: its two variables (operands of the routine,
// read through the bus) and the word at SP are mapped. Each refused state
// fails one condition group alone: the variables (work RAM off, the stack in
// ROM), the stack word (the cartridge off with the variables still read as
// C4C4h, in work RAM), each byte of the stack word, and the variables' high
// byte. The variables' own two operands are always read through the same
// mapping (the ROM, or the one open bus byte), so no state separates them.
std::vector<State> money_states() {
    auto stack_off = [](std::uint16_t sp) {
        return [=](Machine& m) { set_open_bus(m, 0xC4); cartridge_off(m); set_sp(m, sp); };
    };
    return {{"work RAM on", [](Machine&) {}, true},
            {"work RAM off", work_ram_off, false},
            {"the stack in slot 2 ROM", [](Machine& m) { set_sp(m, 0x9000); }, true},
            {"work RAM off, the stack in slot 2 ROM", [](Machine& m) { work_ram_off(m); set_sp(m, 0x9000); }, false},
            {"cartridge off, variables C4C4, stack in work RAM", stack_off(0xDF00), true},
            {"cartridge off, variables C4C4, stack word unmapped", stack_off(0x9000), false},
            {"cartridge off, variables C4C4, SP BFFF (only its low byte unmapped)", stack_off(0xBFFF), false},
            {"cartridge off, variables C4C4, SP FFFF (only its high byte, at 0000, unmapped)", stack_off(0xFFFF), false},
            {"cartridge off, variables FFFF (their high byte at 0000, unmapped), stack in work RAM",
             [](Machine& m) { set_open_bus(m, 0xFF); cartridge_off(m); set_sp(m, 0xDF00); }, false}};
}

struct Family {
    std::string_view name;
    // Entries this family covers; empty: every entry with the name not
    // claimed by another family.
    std::vector<RoutineEntry> entries;
    std::vector<State> states;
};
std::vector<Family> families() {
    const std::vector<State> work_ram{{"work RAM on", [](Machine&) {}, true}, {"work RAM off", work_ram_off, false}};
    return {
        {"C selects the video data port", {},
         {{"C=BE", [](Machine& m) { set_c(m, 0xBE); }, true}, {"C=BF", [](Machine& m) { set_c(m, 0xBF); }, false},
          {"C=7F", [](Machine& m) { set_c(m, 0x7F); }, false}}},
        {"the port register holds the PSG port (C=7F)", {},
         {{"C=7F", [](Machine& m) { set_c(m, 0x7F); }, true}, {"C=7E", [](Machine& m) { set_c(m, 0x7E); }, false},
          {"C=BE", [](Machine& m) { set_c(m, 0xBE); }, false}}},
        {"C800 is plain work RAM", {}, work_ram},
        {"the seed address is work RAM", {}, work_ram},
        {"D selects a VRAM write address", {},
         {{"D=40", [](Machine& m) { set_de(m, 0x4000); }, true}, {"D=60", [](Machine& m) { set_de(m, 0x60FF); }, true},
          {"D=3F", [](Machine& m) { set_de(m, 0x3FFF); }, false}, {"D=41", [](Machine& m) { set_de(m, 0x4100); }, false},
          {"D=C0", [](Machine& m) { set_de(m, 0xC000); }, false}}},
        {"DE is below the cartridge control registers", {},
         {{"DE=0000", [](Machine& m) { set_de(m, 0x0000); }, true}, {"DE=FFFB", [](Machine& m) { set_de(m, 0xFFFB); }, true},
          {"DE=FFFC", [](Machine& m) { set_de(m, 0xFFFC); }, false}, {"DE=FFFF", [](Machine& m) { set_de(m, 0xFFFF); }, false}}},
        {"DE in the voice records, SP in work RAM", {}, register_and_stack(set_de, "DE", 0xC000, 0xC1F0, 0xC400, 0xDFFC)},
        {"IX in the voice records, SP in work RAM", {{12, 0x85CF}}, register_and_stack(set_ix, "IX", 0xC000, 0xDFE0, 0xC002, 0xDFFF)},
        {"IX in the voice records, SP in work RAM", {}, register_and_stack(set_ix, "IX", 0xC001, 0xC1FF, 0xC400, 0xDFFC)},
        {"IX points into the voice records in work RAM", {}, ix_range(0xC000, 0xDFE8)},
        {"HL points at the voice record C12E", {}, exact_hl(0xC12E)},
        {"HL points at the shared voice record C18E", {}, exact_hl(0xC18E)},
        {"HL points at the shared voice record C1AE", {}, exact_hl(0xC1AE)},
        {"HL points at the driver byte C00C", {}, exact_hl(0xC00C)},
        {"HL points at the voice records C08E or C1AE", {},
         {{"HL=C08E", [](Machine& m) { set_hl(m, 0xC08E); }, true}, {"HL=C1AE", [](Machine& m) { set_hl(m, 0xC1AE); }, true},
          {"HL=C08F", [](Machine& m) { set_hl(m, 0xC08F); }, false}, {"HL=C18E", [](Machine& m) { set_hl(m, 0xC18E); }, false}}},
        {"HL is a save slot flag in selected cartridge RAM", {},
         {{"HL=8201, cartridge RAM", [](Machine& m) { set_hl(m, 0x8201); m.memory().write(0xFFFC, 0x08); }, true},
          {"HL=8205, cartridge RAM", [](Machine& m) { set_hl(m, 0x8205); m.memory().write(0xFFFC, 0x08); }, true},
          {"HL=8200, cartridge RAM", [](Machine& m) { set_hl(m, 0x8200); m.memory().write(0xFFFC, 0x08); }, false},
          {"HL=8206, cartridge RAM", [](Machine& m) { set_hl(m, 0x8206); m.memory().write(0xFFFC, 0x08); }, false},
          {"HL=8201, ROM", [](Machine& m) { set_hl(m, 0x8201); m.memory().write(0xFFFC, 0x00); }, false}}},
        {"cartridge RAM is mapped at 8000h", {},
         {{"cartridge RAM", [](Machine& m) { m.memory().write(0xFFFC, 0x08); }, true},
          {"ROM", [](Machine& m) { m.memory().write(0xFFFC, 0x00); }, false}}},
        {"ROM bank 1 is mapped at 4000h", {},
         {{"bank 1", [](Machine& m) { m.memory().write(0xFFFE, 1); }, true},
          {"bank 2", [](Machine& m) { m.memory().write(0xFFFE, 2); }, false}}},
        // The money addition's two variables (operands read from the ROM by
        // the guard) and the stack word, each refused alone (money_states).
        {"the money variable and the stack are mapped memory", {}, money_states()},
    };
}

int guards(probe::Harness& h) {
    const auto table = families();
    unsigned entries = 0, evaluated = 0, refused = 0;
    std::set<std::string_view> used;
    for (const auto& [entry, routine] : h.registry().entries()) {
        if (!routine.guard) continue;
        ++entries;
        const auto name = routine.guard->name;
        const Family* family = nullptr;
        for (const auto& f : table)
            if (f.name == name && std::find(f.entries.begin(), f.entries.end(), entry) != f.entries.end()) family = &f;
        if (!family)
            for (const auto& f : table)
                if (f.name == name && f.entries.empty()) { family = &f; break; }
        probe::check(family != nullptr, "A guard this test has no states for: " + location(entry) + " \"" +
                                            std::string(name) + "\" (add its family here)");
        used.insert(name);
        bool admitted = false, rejected = false;
        for (const auto& state : family->states) {
            probe::Setup setup;
            setup.seed = entry.address;
            auto m = h.prepare(entry, setup);
            state.set(m);
            const bool holds = routine.guard->holds(m);
            probe::check(holds == state.holds, location(entry) + " \"" + std::string(name) + "\": " + state.what +
                                                   (state.holds ? " must be admitted" : " must be refused"));
            ++evaluated;
            admitted = admitted || holds;
            // The engine refuses the entry itself (reason guard), changing nothing.
            if (!holds && !rejected) {
                h.run_fallback(m, entry, RoutineFallback::guard);
                rejected = true;
                ++refused;
            }
        }
        probe::check(admitted && rejected, location(entry) + ": both sides of the guard were not shown");
    }
    for (const auto& f : table)
        probe::check(used.contains(f.name), "A guard family no entry uses any more: " + std::string(f.name));
    probe::check(entries == 71, "The number of guarded entries changed from 71 to " + std::to_string(entries));
    std::cout << entries << " guarded entries: " << evaluated << " states evaluated, both sides of every guard, "
              << refused << " entries refused by the engine with reason guard.\n";
    return 0;
}

// ---- the fixture and the routes ---------------------------------------------
// The first-dungeon recording (it starts from the empty cartridge RAM) replayed
// until the town's map wait (when `field` is asked for) and the dungeon loop's
// wait have been reached at the top level; the machine at each.
struct Fixture {
    std::optional<Machine> dungeon, field;
};
Fixture fixture(probe::Harness& h, const std::filesystem::path& recordings, const std::string& region, bool field) {
    const auto recording = replay::read(recordings / region / "first-dungeon.psrec");
    probe::check(recording.rom_sha1 == h.rom().sha1, "first-dungeon was recorded on another cartridge");
    Memory::CartridgeRam empty{};
    probe::check(recording.cart_ram_sha1 == sha1(empty), "first-dungeon no longer starts from the empty image");
    Machine m(h.rom().image, empty);
    Fixture f;
    constexpr RoutineEntry map_wait{0, 0x0B14};
    const Machine::Hook watch = [&](std::uint16_t pc, Memory::Mapping mapping) -> std::optional<std::uint16_t> {
        const RoutineEntry at{mapping.bank, pc};
        if (m.depth() == 0 && at == d::sample && !f.dungeon) f.dungeon = m;
        if (m.depth() == 0 && at == map_wait && !f.field) f.field = m;
        return std::nullopt;
    };
    for (std::uint64_t i = 0; i < recording.frames && !(f.dungeon && (f.field || !field)); ++i) {
        m.run_frame(recording.input_at(i), h.registry(), watch);
        (void)m.take_psg_writes();
        (void)m.take_samples();
    }
    probe::check(f.dungeon && (f.field || !field), "first-dungeon did not reach the map and dungeon waits");
    return f;
}

// The machine of `base` with PC at `entry`, SP at DF00 and a return to the
// main loop on the stack.
void place(Machine& m, RoutineEntry entry) {
    m.cpu().pc.uint16_value = entry.address;
    m.cpu().sp.uint16_value = 0xDF00;
    m.memory().write(0xDF00, static_cast<std::uint8_t>(d::pass_end.address));
    m.memory().write(0xDF01, static_cast<std::uint8_t>(d::pass_end.address >> 8));
    if (entry.address >= 0x8000) m.memory().write(0xFFFF, static_cast<std::uint8_t>(entry.bank));
}
// The party in front of a dungeon object: its floor laid out in work RAM, the
// party on the first walkable cell from which the object is straight ahead,
// the object's done flag clear, then the object check entered as its caller
// does (B = the index of the cell ahead).
void face_object(Machine& m, const game::DungeonTables& tables, std::uint8_t floor, std::uint8_t cell) {
    const auto record = tables.object_at(floor, cell);
    probe::check(record.has_value(), "no dungeon object at floor " + std::to_string(floor) + " cell " + hex(cell, 2));
    const auto cells = tables.layout(floor);
    probe::check(d::object_cell(cells.at(cell)), "the object's cell is not an object cell in the layout");
    std::optional<unsigned> facing;
    for (unsigned f = 0; f < 4 && !facing; ++f)
        if (d::walkable(cells[static_cast<std::uint8_t>(cell - tables.offset(f, d::ahead))])) facing = f;
    probe::check(facing.has_value(), "no walkable cell in front of the object");
    for (unsigned i = 0; i < cells.size(); ++i) m.memory().write(static_cast<std::uint16_t>(d::layout + i), cells[i]);
    m.memory().write(d::game_mode, d::dungeon_mode);
    m.memory().write(d::floor, floor);
    m.memory().write(d::position, static_cast<std::uint8_t>(cell - tables.offset(*facing, d::ahead)));
    m.memory().write(d::facing, static_cast<std::uint8_t>(*facing));
    m.memory().write(record->flag, 0);
    m.cpu().bc.uint8_values.at_1 = static_cast<std::uint8_t>(d::ahead);
    place(m, d::object_check_unit);
}

constexpr RoutineEntry interaction_entry{1, 0x474B};
constexpr std::uint16_t interaction_event = 0xC2DB;   // the event number the dispatcher reads (from 1)
constexpr std::uint16_t interaction_kind = 0xC29E;

int control(probe::Harness& h, const std::filesystem::path& recordings, const std::string& region) {
    const auto f = fixture(h, recordings, region, false);
    const game::DungeonTables tables(*h.rom().image);
    auto m = *f.dungeon;
    face_object(m, tables, 0, 0xE3);
    constexpr RoutineEntry field_menu{0, 0x1BE1};
    oracle::Lockstep run(m, "positive control " + region, oracle::Mode::instructions);
    bool dispatched = false;
    for (unsigned steps = 0; steps < 100000; ++steps) {
        const RoutineEntry at{m.memory().mapping(m.cpu().pc.uint16_value).bank, m.cpu().pc.uint16_value};
        if (at == interaction_entry) {
            probe::check(m.peek(interaction_event) == 0xA3, "the object did not load event A3");
            dispatched = true;
        }
        if (at == field_menu) {
            const auto sp = m.cpu().sp.uint16_value;
            const auto back = static_cast<std::uint16_t>(m.peek(sp) | m.peek(static_cast<std::uint16_t>(sp + 1)) << 8);
            probe::check(dispatched && back == 0x553B, "the field menu was not called from 01:5538");
            std::cout << region << ": dungeon 0 cell E3, event A3, reaches 00:1BE1 from 01:5538 after " << steps
                      << " steps under strict lockstep.\n";
            return 0;
        }
        run.step(m, &h.registry());
        if (steps % 256 == 0) {
            (void)m.take_psg_writes(); (void)m.take_samples();
            (void)run.oracle().take_psg_writes(); (void)run.oracle().take_samples();
        }
    }
    throw std::runtime_error("the control did not reach the field menu in 100000 steps");
}

struct MainLoop {};
int route(probe::Harness& h, const std::filesystem::path& recordings, const std::string& region,
          const std::filesystem::path& census, const std::vector<std::string>& spec) {
    std::filesystem::remove(census);
    // CTest hands a path under the build folder whose route-census directory may not exist yet.
    if (census.has_parent_path()) std::filesystem::create_directories(census.parent_path());
    const auto f = fixture(h, recordings, region, !spec.empty() && spec[0] == "interaction");
    // The registry counts from here on: the fixture's replay is not the route.
    probe::Harness counted(h.rom());
    std::optional<Machine> start;
    std::string label;
    if (spec.size() == 3 && spec[0] == "object") {
        const game::DungeonTables tables(*h.rom().image);
        start = *f.dungeon;
        face_object(*start, tables, static_cast<std::uint8_t>(std::stoul(spec[1])),
                    static_cast<std::uint8_t>(std::stoul(spec[2], nullptr, 16)));
        label = "object " + spec[1] + " " + spec[2];
    } else if (spec.size() == 2 && spec[0] == "interaction") {
        const auto event = std::stoul(spec[1], nullptr, 16);
        probe::check(event >= 1 && event <= 182, "an interaction event is 01 to B6");
        start = *f.field;
        place(*start, interaction_entry);
        start->memory().write(interaction_event, static_cast<std::uint8_t>(event));
        start->memory().write(interaction_event + 1, 0);
        start->memory().write(interaction_kind, 0);
        label = "interaction " + spec[1];
    } else throw std::runtime_error("a route is: object <floor> <cell hex> | interaction <event hex>");
    auto& m = *start;
    oracle::Lockstep run(m, region + " route " + label, oracle::Mode::instructions);
    const Machine::Hook stop_at_main_loop = [&](std::uint16_t pc, Memory::Mapping mapping) -> std::optional<std::uint16_t> {
        if (m.depth() == 0 && RoutineEntry{mapping.bank, pc} == d::pass_end) throw MainLoop{};
        return std::nullopt;
    };
    constexpr unsigned frames = 20000;
    for (unsigned frame = 0; frame < frames; ++frame) {
        FrameInput input;
        const bool press = frame % 8 >= 4;            // a press every eighth frame, button 1 then 2
        input.pads[0].one = press && frame % 32 < 16;
        input.pads[0].two = press && frame % 32 >= 16;
        try {
            run.run_frame(m, input, &counted.registry(), {}, stop_at_main_loop);
        } catch (const MainLoop&) {
            run.synchronize(m);
            std::ofstream out(census, std::ios::binary);
            replay::write_census(out, counted.registry());
            out.close();
            probe::check(bool(out), "cannot write the route census");
            std::cout << region << " route " << label << ": back at the main loop after " << frame + 1
                      << " frames under strict lockstep.\n";
            return 0;
        }
    }
    throw std::runtime_error(region + " route " + label + ": the main loop was not reached in " +
                             std::to_string(frames) + " frames");
}
}

int main(int argc, char** argv) {
    try {
        if (argc < 4) throw std::runtime_error("usage: stop_sweep_tests <rom> <us|br> guards | control <recordings> | "
                                               "route <recordings> <census> <route...>");
        const auto rom = probe::load(argv[1], argv[2]);
        const std::string mode = argv[3];
        const bool required = mode == "route";      // the playthrough group never skips
        if (!rom) {
            if (required) throw std::runtime_error("the ROM is missing: " + std::string(argv[1]));
            return 77;
        }
        probe::Harness h(*rom);
        if (mode == "guards" && argc == 4) return guards(h);
        if (mode == "control" && argc == 5) return control(h, argv[4], argv[2]);
        if (mode == "route" && argc >= 7)
            return route(h, argv[4], argv[2], argv[5], std::vector<std::string>(argv + 6, argv + argc));
        throw std::runtime_error("invalid arguments for mode " + mode);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
