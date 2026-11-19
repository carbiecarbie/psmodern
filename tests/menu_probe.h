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
//   - the host hold: a FrameDriver plays a recording to a frame, holds the
//     native field menu as Options does, closes it, and is compared with the
//     oracle (the original on the pinned core) every frame.
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
        entries.push_back(entry);
    }
    return entries;
}

// ---- The host hold ----------------------------------------------------------------------------
// A FrameDriver (the program's) on the ROM, with its own save folder under
// `work`, plays `recording` (it must start from an empty cartridge) to frame
// `from`, then asks for the native field menu (as the Options screen does)
// until it is admitted, holds it `hold` frames, closes it and runs until it
// has returned, plus `after` frames. Milestone 14: there is no routines-off
// driver any more (the program has no core); every frame is compared with the
// oracle instead. At the start of a frame the oracle takes the engine's state
// (a top-level boundary) and runs the original, fed the input the driver
// feeds the machine, to the engine's frame observer, where the whole machines
// must be equal. The frames in which the host itself changes the machine (the
// request being admitted, the close) are host behaviour, not the original's,
// and are not compared; the app session tests cover them. The report gives
// the frames held and closing, the frames compared, the game mode at the
// request (9 map or town, 0Bh dungeon) and the C++ calls of menu segments
// (entries inside the six parts' ranges) from the request to the return.
struct HoldReport { std::uint64_t held=0, closing=0, menu_cpp=0, compared=0; std::uint8_t mode=0; };
inline HoldReport hold_compare(const probe::Rom& rom, const replay::Recording& recording, std::uint64_t from,
                               unsigned hold, unsigned after, const std::filesystem::path& work, const std::string& name) {
    check(sha1(StartImage{})==recording.cart_ram_sha1,name+": the hold needs a recording that starts from an empty cartridge");
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work/"on");
    app::FrameDriver on(rom.image,app::SaveStore(work/"on",rom.version));
    std::uint64_t frame=0;
    HoldReport report;
    bool requested=false;
    auto run=[&](const FrameInput& input, const std::string& when) {
        const bool active=on.menu_active(), closing=on.menu_closing(), request=requested;
        requested=false;
        // The input the driver feeds the machine (FrameDriver::run): none while
        // its menu is open.
        oracle::NativeMachine original(on.machine());
        original.set_input(active ? FrameInput{} : input);
        std::string mismatch;
        bool observed=false;
        on.set_frame_probe([&](const Machine& m) {
            observed=true;
            while (original.cycles()<m.cycles()) original.step();
            original.settle();
            if (original.cycles()!=m.cycles() || original.cpu().resume) { mismatch=" (the oracle is not at an instruction boundary)"; return; }
            if (oracle::equal(m,original,oracle::Scope::whole)) return;
            for (const auto& difference:oracle::differences(m,original,oracle::Scope::whole)) mismatch+=" "+difference;
        });
        const auto a=on.run(input);
        on.set_frame_probe({});
        check(!a.save_error,name+": a save failed");
        ++frame;
        check(observed,name+": the frame observer did not run");
        const bool host=request || closing || active!=on.menu_active();
        if (host) return;
        check(mismatch.empty(),name+": the game and the original differ "+when+" at frame "+std::to_string(frame)+":"+mismatch);
        ++report.compared;
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
    bool admitted=false;
    for (unsigned i=0;i<600 && !admitted;++i) {
        admitted=on.request_menu();
        requested=admitted;
        if (!admitted) run({},"before the hold");
    }
    check(admitted,name+": the field menu was never admitted");
    report.mode=on.machine().peek(menu::game_mode);
    const auto calls_before=menu_calls();
    run({},"opening the menu");
    check(on.menu_active(),name+": the native menu did not open");
    for (unsigned i=0;i<hold;++i) { run({},"during the hold"); ++report.held; check(on.menu_active(),name+": the menu closed by itself"); }
    on.close_menu();
    for (unsigned i=0;i<200 && on.menu_active();++i) { run({},"while closing"); ++report.closing; }
    check(!on.menu_active(),name+": the menu did not return");
    report.menu_cpp=menu_calls()-calls_before;
    check(report.menu_cpp>0,name+": the hold ran no C++ menu calls");
    check(report.compared>=report.held,name+": too few frames were compared with the original");
    for (unsigned i=0;i<after;++i) run({},"after the hold");
    return report;
}
}
