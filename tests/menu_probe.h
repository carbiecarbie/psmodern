// SPDX-License-Identifier: GPL-3.0-or-later
// Probes for the milestone 12 menu routines, on top of tests/probe.h and
// tests/map_probe.h (whose command line, start images, scenarios and
// prepare_at() the menu tests share unchanged).
//
// Command line of every menu ROM test:
//   <rom> <us|br> [<recordings folder> [<reference folder> [<work folder>]]]
// The work folder (a build subfolder) is for tests that save, such as the
// host hold below. A missing ROM makes load() return nothing (exit 77); a
// missing reference folder skips the parts that need it with a message.
//
// What this header adds:
//   - predicates on a machine at a frame end: a cursor loop is running, the
//     field menu or the item list is the one waiting, a dialogue window is
//     open;
//   - fills of video memory, colour memory and the window buffers
//     (D700h-DEAFh) from a seed, for the save, draw and restore probes;
//   - the window sizes of the game, read from the ROM at the call sites the
//     plan lists (menu::window_size_sites), so probes cover every real window;
//   - placement of a dispatch at a chosen distance before the frame boundary,
//     to start a chunk at every offset of its window (split-scan style);
//   - registered_entries(): the catalog rule every part test keeps;
//   - the host hold: two FrameDrivers, routines on and off, play a recording
//     to a frame, hold the native field menu as Options does, close it, and
//     compare the machines every frame.
#pragma once
#include "app/frame_driver.h"
#include "game/menu.h"
#include "map_probe.h"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace psmodern::menu_probe {
using probe::check;
namespace menu=game::menu;
using map_probe::Inputs;
using map_probe::Scenario;
using map_probe::StartImage;
using map_probe::start_image;
using map_probe::edited_start_image;
using map_probe::external_start_image;
using map_probe::prepare_at;
using map_probe::admitted;

// The command line above; the fifth argument is the work folder.
inline std::optional<Inputs> load(int argc, char** argv, std::filesystem::path* work=nullptr) {
    check(argc>=3 && argc<=6,"Expected <rom> <us|br> [<recordings folder> [<reference folder> [<work folder>]]].");
    if (work) *work=argc>5 ? std::filesystem::path(argv[5]) : std::filesystem::path{};
    return map_probe::load(std::min(argc,5),argv);
}

// ---- Predicates on a machine (at a frame end or anywhere) ---------------------------------
inline std::uint16_t word(const Machine& m, std::uint16_t address) {
    return static_cast<std::uint16_t>(m.peek(address)|(m.peek(static_cast<std::uint16_t>(address+1))<<8));
}
// The word a ROM instruction's operand holds (bank:address of the operand).
inline std::uint16_t rom_word(const probe::Rom& rom, RoutineEntry operand) {
    return static_cast<std::uint16_t>(rom.byte(operand.bank,operand.address)|
        (rom.byte(operand.bank,static_cast<std::uint16_t>(operand.address+1))<<8));
}
// The cursor positions the field menu (00:1BF4) and the item list (00:34FC) set.
inline constexpr RoutineEntry field_menu_cursor_operand{0,0x1BF5}, item_list_cursor_operand{0,0x34FD};
inline bool on_selection(const Machine& m) { return m.peek(menu::selection_active)!=0; }
inline bool on_field_menu(const probe::Rom& rom, const Machine& m) {
    return on_selection(m) && word(m,menu::cursor_address)==rom_word(rom,field_menu_cursor_operand);
}
inline bool on_item_list(const probe::Rom& rom, const Machine& m) {
    return on_selection(m) && word(m,menu::cursor_address)==rom_word(rom,item_list_cursor_operand);
}
inline bool on_dialogue(const Machine& m) { return m.peek(menu::dialogue_open)!=0; }
// The return address on top of the stack (the frame wait's caller while a loop waits).
inline std::uint16_t return_word(const Machine& m) { return word(m,m.cpu().sp.uint16_value); }

// ---- Fills ---------------------------------------------------------------------------------
// Through the VDP ports at the machine's current cycle (no time passes): the
// VDP's address, code, latch and read buffer are left as after the last write.
inline void fill_vram(Machine& m, std::uint64_t seed) {
    std::mt19937_64 random(seed*0x2545f4914f6cdd1dull+1);
    const auto at=m.cycles();
    m.write_port(0xbf,0x00,at); m.write_port(0xbf,0x40,at);
    for (unsigned i=0;i<0x4000;++i) m.write_port(0xbe,static_cast<std::uint8_t>(random()),at);
}
inline void fill_cram(Machine& m, std::uint64_t seed) {
    std::mt19937_64 random(seed*0x9e3779b97f4a7c15ull+3);
    const auto at=m.cycles();
    m.write_port(0xbf,0x00,at); m.write_port(0xbf,0xc0,at);
    for (unsigned i=0;i<32;++i) m.write_port(0xbe,static_cast<std::uint8_t>(random()&0x3f),at);
}
inline void fill_window_buffers(Machine& m, std::uint64_t seed) {
    std::mt19937_64 random(seed*0xbf58476d1ce4e5b9ull+5);
    for (unsigned a=menu::window_buffers_first;a<=menu::window_buffers_last;++a)
        m.memory().write(static_cast<std::uint16_t>(a),static_cast<std::uint8_t>(random()));
}

// ---- Window sizes --------------------------------------------------------------------------
struct WindowSize {
    RoutineEntry site;
    std::uint8_t rows, columns;   // B and C of the site's ld bc,nn
    bool operator<(const WindowSize& other) const { return std::pair(rows,columns)<std::pair(other.rows,other.columns); }
};
// Every site's size, read from the ROM; throws when a listed site is not an
// ld bc,nn in this image.
inline std::vector<WindowSize> window_sizes(const probe::Rom& rom) {
    std::vector<WindowSize> sizes;
    for (const auto site:menu::window_size_sites) {
        check(rom.byte(site.bank,site.address)==0x01,"a window size site is not ld bc,nn");
        sizes.push_back({site,rom.byte(site.bank,static_cast<std::uint16_t>(site.address+2)),
                         rom.byte(site.bank,static_cast<std::uint16_t>(site.address+1))});
    }
    return sizes;
}
// The distinct sizes (rows, columns) among them.
inline std::vector<WindowSize> distinct_window_sizes(const probe::Rom& rom) {
    std::vector<WindowSize> sizes;
    std::set<std::pair<unsigned,unsigned>> seen;
    for (const auto& size:window_sizes(rom))
        if (seen.insert({size.rows,size.columns}).second) sizes.push_back(size);
    return sizes;
}

// ---- Frame-boundary placement --------------------------------------------------------------
// Advances the machine (no instruction runs) so that exactly `remaining`
// cycles are left before the next frame boundary; 1 <= remaining <=
// frame_cycles. Dispatch falls back (reason frame) when the segment's bound
// exceeds `remaining`, so a probe sweeps `remaining` across a chunk's window.
// Interrupts may become pending meanwhile: use IFF1 = 0 or expect irq.
inline void place_before_boundary(Machine& m, std::uint64_t remaining) {
    check(remaining>=1 && remaining<=Machine::frame_cycles,"remaining cycles must be within one frame");
    const auto position=m.cycles()%Machine::frame_cycles, target=Machine::frame_cycles-remaining;
    m.advance((target+Machine::frame_cycles-position)%Machine::frame_cycles);
}
// `count` remaining-cycle values spread over [1, window + margin], ends included.
inline std::vector<std::uint64_t> boundary_offsets(std::uint64_t window, unsigned count, std::uint64_t margin=64) {
    std::vector<std::uint64_t> offsets;
    const auto last=std::min<std::uint64_t>(window+margin,Machine::frame_cycles);
    for (unsigned i=0;i<count;++i) offsets.push_back(1+(last-1)*i/std::max(1u,count-1));
    return offsets;
}

// ---- Catalog checks shared by the part tests ---------------------------------------------------
// Registers one part alone and checks it against its planned list: every
// registered entry is planned with its planned unit, or lies in the part's
// ranges with a unit of the part; never a native instruction. The returned
// list says which entries are registered.
inline std::vector<RoutineEntry> registered_entries(menu::Part part, void (*registration)(RoutineRegistry&)) {
    RoutineRegistry registry;
    registration(registry);
    const auto planned=menu::planned_segments(part);
    std::vector<RoutineEntry> entries;
    for (const auto& [entry,routine]:registry.entries()) {
        bool listed=false, inside=false, own_unit=false;
        for (const auto& segment:planned) {
            listed=listed || (segment.entry==entry && segment.unit==routine.spec.unit);
            own_unit=own_unit || segment.unit==routine.spec.unit;
        }
        for (const auto& range:menu::part_ranges(part))
            inside=inside || (entry.bank==range.bank && entry.address>=range.first && entry.address<=range.last);
        const std::string name=menu::part_names[static_cast<unsigned>(part)];
        check(listed || (inside && own_unit),"menu part "+name+" registers an entry outside its plan, ranges or units");
        check(std::find(menu::native_instructions.begin(),menu::native_instructions.end(),entry)==menu::native_instructions.end(),
              "menu part "+name+" registers an entry at an instruction that stays native");
        check(!routine.spec.force_admit,"menu segments are never force-admitted");
        entries.push_back(entry);
    }
    return entries;
}

// ---- The host hold ----------------------------------------------------------------------------
// Two FrameDrivers on the same ROM, routines on and off, each with its own
// save folder under `work`, play `recording` (it must start from an empty
// cartridge) to frame `from`, then ask for the native field menu (as the
// Options screen does) until both admit it, hold it `hold` frames, close it
// and run until both have returned, plus `after` frames. The machines must be
// equivalent after every frame and the menu state must agree. The report
// gives the frames held and closing, the game mode at the request (9 map or
// town, 0Bh dungeon) and the C++ calls of menu segments (entries inside the
// six parts' ranges, routines-on driver) from the request to the return.
struct HoldReport { std::uint64_t held=0, closing=0, menu_cpp=0; std::uint8_t mode=0; };
inline HoldReport hold_compare(const probe::Rom& rom, const replay::Recording& recording, std::uint64_t from,
                               unsigned hold, unsigned after, const std::filesystem::path& work, const std::string& name) {
    check(sha1(StartImage{})==recording.cart_ram_sha1,name+": the hold needs a recording that starts from an empty cartridge");
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work/"on"); std::filesystem::create_directories(work/"off");
    app::FrameDriver on(rom.image,app::SaveStore(work/"on",rom.version),true);
    app::FrameDriver off(rom.image,app::SaveStore(work/"off",rom.version),false);
    std::uint64_t frame=0;
    auto compare=[&](const std::string& when) {
        if (on.machine().equivalent(off.machine()) && on.menu_active()==off.menu_active()) return;
        std::string fields;
        for (const auto& difference:off.machine().differences(on.machine())) fields+=" "+difference.field;
        check(false,name+": routines on and off differ "+when+" at frame "+std::to_string(frame)+":"+fields+
              (on.menu_active()!=off.menu_active() ? " (menu state)" : ""));
    };
    auto run=[&](const FrameInput& input, const std::string& when) {
        const auto a=on.run(input); const auto b=off.run(input);
        check(!a.save_error && !b.save_error,name+": a save failed");
        ++frame; compare(when);
    };
    for (;frame<from && frame<recording.frames;) run(recording.input_at(frame),"while playing");
    auto menu_calls=[&] {
        std::uint64_t calls=0;
        for (const auto& [entry,routine]:on.routines().entries())
            for (unsigned p=0;p<static_cast<unsigned>(menu::Part::count);++p)
                for (const auto& range:menu::part_ranges(static_cast<menu::Part>(p)))
                    if (entry.bank==range.bank && entry.address>=range.first && entry.address<=range.last) calls+=routine.counters.cpp_calls;
        return calls;
    };
    HoldReport report;
    bool admitted=false;
    for (unsigned i=0;i<600 && !admitted;++i) {
        const bool a=on.request_menu(), b=off.request_menu();
        check(a==b,name+": the menu request was admitted with routines "+(a ? "on" : "off")+" only");
        admitted=a;
        if (!admitted) run({},"before the hold");
    }
    check(admitted,name+": the field menu was never admitted");
    report.mode=on.machine().peek(menu::game_mode);
    const auto calls_before=menu_calls();
    run({},"opening the menu");
    check(on.menu_active(),name+": the native menu did not open");
    for (unsigned i=0;i<hold;++i) { run({},"during the hold"); ++report.held; check(on.menu_active(),name+": the menu closed by itself"); }
    on.close_menu(); off.close_menu();
    for (unsigned i=0;i<200 && on.menu_active();++i) { run({},"while closing"); ++report.closing; }
    check(!on.menu_active(),name+": the menu did not return");
    report.menu_cpp=menu_calls()-calls_before;
    check(report.menu_cpp>0,name+": the hold ran no C++ menu calls with routines on (it would pass with the routines silently off)");
    for (unsigned i=0;i<after;++i) run({},"after the hold");
    return report;
}
}
