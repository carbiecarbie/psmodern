// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 11, part scroll: steps, the camera, the screen window and the strips.
//   no arguments                       registration catalog (no ROM)
//   <rom> <us|br> [<recordings folder> [<reference folder>]]
//                                      state probes and scenario probes on
//                                      that ROM (tests/map_probe.h)
// A ROM run exits 77 without the ROM, and 77 at the end when the reference
// folder (the start image of later-town) is missing and its probes were skipped.
//
// The ROM probes:
//   1. power-on state probes of every entry: random work RAM and registers,
//      the paths' edges (step counter, directions, vehicles, scroll and camera
//      wraps), slot 2 variants, the stack cases and the refresh wrap; each
//      call must run in C++, match the original and cost what its bound said;
//      every declared exit must be taken by some probe;
//   2. chains from a real map frame (later-town): whole calls of 01:7143,
//      01:723D, 01:72A6, 01:733A and 01:744E with the camera at the edges and
//      wraps of the world (screen 7 to 0 and back, both axes), every segment
//      verified and the end state equal to the same call run natively;
//   3. scenario probes: steps across the world's wrap on foot and in a
//      vehicle, routines on beside routines off, compared every frame.
#include "game/map_kit.h"
#include "map_probe.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace psmodern;
using probe::check;
namespace map=game::map;
namespace mk=game::map_kit;
namespace {
constexpr map::Part part=map::Part::scroll;

// ---- Registration catalog --------------------------------------------------------
SegmentExit R(std::uint16_t address, std::int8_t stack=0) { return mk::returned_at(address,stack); }
SegmentExit F(std::uint16_t address, std::int8_t stack=0) { return mk::core_final_at(address,stack); }
SegmentExit X(std::uint16_t address) { return mk::conditional_at(address); }
SegmentExit L(std::uint16_t address, std::uint16_t after, std::int8_t stack=0) { return mk::loop_at(address,after,stack); }

struct Want {
    std::uint16_t entry, unit;
    std::vector<SegmentExit> exits;
    mk::Permissions permissions={};
};
// The milestone 11 segment design for this part, with the part's tuning (reported): the decoder's
// chunks end at every loop head (after 0, not 512), 01:733A holds four bytes,
// not six, and the column's set-up is split in three, with two extra entries
// 01:72C6 and 01:72DE on the column's unit.
std::vector<Want> wanted() {
    const std::vector<SegmentExit> step{F(0x718D),F(0x71B2),F(0x71F4),F(0x7209),F(0x71E0),X(0x7227),R(0x723C)};

    return {
        {0x7143,0x7143,{F(0x7146,-6)},{.scratch_stack=6}},
        {0x7149,0x7143,{R(0x714C,6)},{.inherited_stack=6}},
        {0x714D,0x7143,step},{0x7190,0x7143,step},{0x71B5,0x7143,step},{0x71F7,0x7143,step},{0x720C,0x7143,step},
        {0x723D,0x723D,{F(0x7261,-2)},{.slot2_mapper=true,.scratch_stack=2}},
        {0x7264,0x723D,{F(0x726E)},{.inherited_stack=2}},
        {0x7271,0x723D,{F(0x727E)},{.inherited_stack=2}},
        {0x7281,0x723D,{L(0x728C,0,2)},{.inherited_stack=2}},
        {0x728A,0x723D,{L(0x728C,0)}},{0x728D,0x723D,{L(0x728C,0),R(0x728E)}},
        {0x72A6,0x72A6,{R(0x72AB),F(0x72C3)},{.slot2_mapper=true}},
        {0x72C6,0x72A6,{F(0x72DC)}},
        {0x72DE,0x72A6,{L(0x72FD,0,-4)},{.scratch_stack=4}},
        {0x72FE,0x72A6,{L(0x72FD,0),R(0x7339,4)},{.inherited_stack=4}},
        {0x733A,0x733A,{R(0x733F),L(0x7396,0,-4)},{.slot2_mapper=true,.scratch_stack=4}},
        {0x7397,0x733A,{L(0x7396,0),R(0x73CF,4)},{.scratch_stack=2,.inherited_stack=4}},
        {0x744E,0x744B,{L(0x7487,512,-8)},{.slot2_mapper=true,.scratch_stack=10}},
        {0x7488,0x744B,{L(0x7487,512),F(0x74E1,8)},{.scratch_stack=2,.inherited_stack=8}},
    };
}
void registration() {
    const auto entries=map_probe::registered_entries(part,game::register_map_scroll);
    const auto want=wanted();
    check(entries.size()==want.size(),"the scroll part registers its 21 entries");
    for (const auto& planned:map::scroll_segments)
        check(std::count(entries.begin(),entries.end(),planned.entry)==1,"every planned scroll segment is registered");
    RoutineRegistry registry;
    game::register_map_scroll(registry);
    for (const auto& w:want) {
        const RoutineEntry entry{1,w.entry}, unit{1,w.unit};
        const auto listed=std::find_if(map::scroll_segments.begin(),map::scroll_segments.end(),[&](const auto& s) { return s.entry==entry; });
        check(listed!=map::scroll_segments.end() && listed->unit==unit,"the entry is planned with its unit");
        const auto* routine=registry.find(entry);
        check(routine!=nullptr,"the planned entry is registered");
        const auto& spec=routine->spec;
        check(spec.unit==unit && spec.slot2_mapper==w.permissions.slot2_mapper && spec.scratch_stack==w.permissions.scratch_stack &&
              spec.inherited_stack==w.permissions.inherited_stack && !spec.single_instruction && !spec.force_admit &&
              !spec.absolute_wram_store,"the entry has the unit and permissions of the scroll table");
        check(spec.exits.size()==w.exits.size(),"the entry declares the exits of the scroll table");
        for (const auto& exit:w.exits)
            check(std::count_if(spec.exits.begin(),spec.exits.end(),[&](const auto& e) { return e.same_declaration(exit); })==1,
                  "an exit of the scroll table is declared exactly as written");
    }
    std::cout<<map::scroll_segments.size()<<" planned scroll segments and 2 extra column entries registered with their exits and permissions.\n";
}

// ---- Probes on a ROM ------------------------------------------------------------------
constexpr RoutineEntry at(std::uint16_t address) { return {1,address}; }
// Work RAM the step reads or writes.
constexpr std::uint16_t pad_held=0xC204, sound_request=0xC004, strip_pending=0xC264, step_counter=0xC265, moved=0xC2D2;
constexpr std::uint16_t screen_table=0xC260, screen_bank=0xC262, sheet_bank=0xC263;

// The harness's verifier, wrapped to learn which exit each verified call took.
struct Spy final : RoutineVerifier {
    replay::CallVerifier inner{"probe"};
    std::optional<SegmentExit> last;
    std::map<RoutineEntry,std::map<std::uint16_t,std::uint64_t>> taken;   // entry -> exit -> calls
    void run(Machine& machine, RoutineEntry entry, const SegmentSpec& spec, bool forced, std::uint64_t bound,
             std::uint64_t call, const std::function<std::optional<SegmentExit>()>& body) override {
        inner.run(machine,entry,spec,forced,bound,call,[&] { last=body(); return last; });
        if (last) ++taken[entry][last->address];
    }
};

struct Rig {
    probe::Harness harness;
    Spy spy;
    const probe::Rom& rom;
    std::mt19937_64 random{0x5C2011};
    std::uint64_t seed=1, count=0, refusals=0, fallbacks=0;

    explicit Rig(const probe::Rom& r) : harness(r), rom(r) { harness.registry().verifier=&spy; }
    std::uint8_t byte() { return static_cast<std::uint8_t>(random()); }
    unsigned below(unsigned n) { return static_cast<unsigned>(random()%n); }
    template<class T, std::size_t N> T pick(const T (&values)[N]) { return values[below(N)]; }

    using Prepare=std::function<void(Machine&)>;
    // A power-on machine at the entry: work RAM below the stack random, then the probe's own state.
    Machine make(RoutineEntry entry, probe::Setup setup, const Prepare& prepare) {
        setup.seed=seed++;
        auto machine=harness.prepare(entry,setup);
        auto& memory=machine.memory();
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()&~0x10);
        for (std::uint16_t a=0xC000;a<0xDE00;++a) memory.write(a,byte());
        if (prepare) prepare(machine);
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
        return machine;
    }
    // One dispatch that runs in C++ against the original, at the cost its
    // bound announced; returns the exit taken. A state whose estimate is
    // uncertain (work RAM off) falls back with reason frame instead.
    std::uint16_t dispatch(Machine& machine, RoutineEntry entry, const std::string& what, bool ram_off=false) {
        const auto bound=harness.routine(entry).bound(machine);
        if (bound>Machine::frame_cycles) {
            check(ram_off,"a scroll segment refused a state with work RAM on: "+what);
            harness.run_fallback(machine,entry,RoutineFallback::frame);
            ++refusals;
            return 0;
        }
        spy.last.reset();
        probe::Harness::Result result{};
        try { result=harness.run_cpp(machine,entry); }
        catch (const std::exception& error) { throw std::runtime_error(std::string(error.what())+" ["+what+"]"); }
        ++count;
        check(spy.last.has_value(),"the probe's call took no exit: "+what);
        if (!ram_off) check(result.cycles==bound,what+": cost "+std::to_string(result.cycles)+" cycles, its bound said "+std::to_string(bound));
        return spy.last->address;
    }
    std::uint16_t run(RoutineEntry entry, const Prepare& prepare, const std::string& what) {
        probe::Setup setup;
        setup.iff1=setup.iff2=(seed&1);
        setup.r=byte();
        auto machine=make(entry,setup,prepare);
        return dispatch(machine,entry,what);
    }
};

std::string hex(unsigned value) { std::ostringstream text; text<<std::hex<<value; return text.str(); }
void set_flags(Machine& m, std::uint8_t f) { m.cpu().af.uint8_values.at_0=f; }
void set_a(Machine& m, std::uint8_t a) { m.cpu().af.uint8_values.at_1=a; }
void set_bc(Machine& m, std::uint16_t v) { m.cpu().bc.uint16_value=v; }
void set_de(Machine& m, std::uint16_t v) { m.cpu().de.uint16_value=v; }
void set_hl(Machine& m, std::uint16_t v) { m.cpu().hl.uint16_value=v; }
void write_word(Machine& m, std::uint16_t address, std::uint16_t v) {
    m.memory().write(address,static_cast<std::uint8_t>(v)); m.memory().write(static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(v>>8));
}

// Camera and scroll values at the edges: pixel and screen wraps on both axes.
constexpr std::uint8_t edge_x_low[]{0x00,0x01,0x02,0x07,0x08,0x0F,0x10,0x7F,0x80,0xF0,0xF7,0xF8,0xFE,0xFF};
constexpr std::uint8_t edge_screen[]{0,1,6,7,8,0x0F,0xF8,0xFF};
constexpr std::uint8_t edge_y_low[]{0x00,0x01,0x02,0x0F,0x10,0xAF,0xB0,0xBE,0xBF,0xC0,0xC1,0xFF};
constexpr std::uint8_t edge_scroll[]{0x00,0x01,0x02,0x1F,0x20,0x21,0xC0,0xDE,0xDF,0xE0,0xE1,0xFF};

void camera(Rig& rig, Machine& m) {
    auto& memory=m.memory();
    memory.write(map::camera_x,rig.below(2) ? rig.pick(edge_x_low) : rig.byte());
    memory.write(map::camera_x+1,rig.below(2) ? rig.pick(edge_screen) : rig.byte());
    memory.write(map::camera_y,rig.below(2) ? rig.pick(edge_y_low) : rig.byte());
    memory.write(map::camera_y+1,rig.below(2) ? rig.pick(edge_screen) : rig.byte());
    memory.write(map::scroll_y,rig.below(2) ? rig.pick(edge_scroll) : rig.byte());
}

// -- 1. Power-on state probes ------------------------------------------------------------
void step_probes(Rig& rig) {
    for (unsigned i=0;i<400;++i) rig.run(map::step_unit,{},"7143");
    for (unsigned i=0;i<400;++i) rig.run(at(0x7149),[&](Machine& m) {    // HL, DE, BC held, then the return address
        const auto sp=m.cpu().sp.uint16_value;
        for (unsigned k=0;k<6;++k) m.memory().write(static_cast<std::uint16_t>(sp+k),rig.byte());
        write_word(m,static_cast<std::uint16_t>(sp+6),probe::Setup{}.return_address);
    },"7149");
    // 01:714D: the counter (running out or not), the pad, the transport, the camera.
    constexpr std::uint8_t counters[]{0x00,0x01,0x0F,0x7F,0x80,0x81,0xFF};
    constexpr std::uint8_t strips[]{0x00,0x01,0x02,0x04,0x08,0x03,0x0C,0x0F,0x80,0xFF};
    constexpr std::uint8_t transports[]{0x00,0x04,0x08,0x0C,0x10,0x11,0xFF};
    for (unsigned i=0;i<4000;++i) rig.run(at(0x714D),[&](Machine& m) {
        auto& memory=m.memory();
        memory.write(step_counter,rig.below(4) ? rig.pick(counters) : rig.byte());
        memory.write(strip_pending,rig.below(4) ? rig.pick(strips) : rig.byte());
        memory.write(pad_held,rig.byte());
        memory.write(map::transport,rig.pick(transports));
        camera(rig,m);
    },"714D");
    // After a terrain check: Z = free, NZ = blocked; C the directions (bit 7: a new step).
    for (const std::uint16_t entry:{0x7190,0x71B5,0x71F7,0x720C})
        for (unsigned i=0;i<3000;++i) rig.run(at(entry),[&](Machine& m) {
            camera(rig,m);
            set_flags(m,static_cast<std::uint8_t>((rig.byte()&~mk::zero_flag)|(rig.below(3) ? mk::zero_flag : 0)));
            const std::uint8_t c=rig.below(2) ? static_cast<std::uint8_t>(rig.byte()|0x80) : static_cast<std::uint8_t>(1u<<rig.below(4));
            const std::uint8_t e=rig.below(2) ? 1 : 2;
            if (entry<0x71F7) {              // vertical: D the scroll register, HL the camera's Y, B its screen
                const std::uint8_t d=rig.below(2) ? rig.pick(edge_scroll) : rig.byte();
                const std::uint8_t l=rig.below(2) ? rig.pick(edge_y_low) : rig.byte(), h=rig.below(2) ? rig.pick(edge_screen) : rig.byte();
                set_de(m,static_cast<std::uint16_t>((d<<8)|e)); set_hl(m,static_cast<std::uint16_t>((h<<8)|l));
                set_bc(m,static_cast<std::uint16_t>((rig.below(4) ? h : rig.byte())<<8|c));
            } else {                         // horizontal: DE the step, HL the camera's X
                const std::uint8_t l=rig.below(2) ? rig.pick(edge_x_low) : rig.byte(), h=rig.below(2) ? rig.pick(edge_screen) : rig.byte();
                set_de(m,e); set_hl(m,static_cast<std::uint16_t>((h<<8)|l));
                set_bc(m,static_cast<std::uint16_t>((rig.below(4) ? h : rig.byte())<<8|c));
            }
        },hex(entry));
}

// A screen coded as runs: repeated bytes, literal runs and the end mark, at
// `data` in work RAM; long runs make the 512-cycle loop exit happen.
std::vector<std::uint8_t> coded_screen(Rig& rig) {
    std::vector<std::uint8_t> bytes;
    const unsigned runs=1+rig.below(rig.below(4) ? 6 : 14);
    for (unsigned i=0;i<runs;++i) {
        if (rig.below(2)) {
            const auto count=static_cast<std::uint8_t>(rig.below(3) ? 1+rig.below(16) : 1+rig.below(0x7F));
            bytes.push_back(count); bytes.push_back(rig.byte());
        } else {
            const auto count=static_cast<std::uint8_t>(rig.below(3) ? 1+rig.below(8) : 1+rig.below(0x7F));
            bytes.push_back(static_cast<std::uint8_t>(0x80|count));
            for (unsigned k=0;k<count;++k) bytes.push_back(rig.byte());
        }
    }
    bytes.push_back(0);
    return bytes;
}
void window_probes(Rig& rig) {
    // 01:723D: the screen row and column read from the 9 x 9 table, with every screen at the edge.
    for (unsigned i=0;i<1500;++i) rig.run(map::screen_window_unit,[&](Machine& m) {
        m.memory().write(map::camera_x+1,rig.below(2) ? rig.below(9) : rig.byte());
        m.memory().write(map::camera_y+1,rig.below(2) ? rig.below(9) : rig.byte());
        m.memory().write(screen_bank,rig.below(2) ? static_cast<std::uint8_t>(rig.below(32)) : rig.byte());
        write_word(m,screen_table,static_cast<std::uint16_t>(rig.below(2) ? 0x8000+rig.below(0x4000) : rig.below(0x10000)));
    },"723D");
    for (const std::uint16_t entry:{0x7264,0x7271})
        for (unsigned i=0;i<1000;++i) rig.run(at(entry),{},hex(entry));
    // The decoder: a coded screen in work RAM at C100h, decoded into a slot of the window.
    for (const std::uint16_t entry:{0x7281,0x728A,0x728D})
        for (unsigned i=0;i<1500;++i) rig.run(at(entry),[&](Machine& m) {
            const auto bytes=coded_screen(rig);
            for (std::size_t k=0;k<bytes.size();++k) m.memory().write(static_cast<std::uint16_t>(0xC100+k),bytes[k]);
            set_de(m,static_cast<std::uint16_t>(0xCC00+0x100*rig.below(4)+(rig.below(4) ? 0 : rig.byte())));
            if (entry==0x7281) {             // the held table pointer: the fourth word points at the screen
                const auto pointer=static_cast<std::uint16_t>(0xC000+rig.below(0x80));
                const auto sp=m.cpu().sp.uint16_value;
                write_word(m,sp,pointer);
                write_word(m,static_cast<std::uint16_t>(sp+2),probe::Setup{}.return_address);
                write_word(m,static_cast<std::uint16_t>(pointer+1),0xC100);
            } else {
                set_hl(m,0xC100);
                if (entry==0x728D) {         // after the loop head's ld a,(hl); B is 0 there except after an odd run
                    set_a(m,rig.below(4) ? bytes[0] : 0);   // 0: the end of the screen
                    set_bc(m,static_cast<std::uint16_t>((rig.below(8) || (bytes[0]&0x80) ? 0 : rig.byte())<<8|rig.byte()));
                }
            }
        },hex(entry));
}
void strip_probes(Rig& rig) {
    constexpr std::uint8_t strips[]{0x00,0x01,0x02,0x03,0x04,0x08,0x0C,0x05,0x0A,0x0F,0xF0,0xFF};
    for (const auto unit:{map::column_unit,map::row_unit})
        for (unsigned i=0;i<3000;++i) rig.run(unit,[&](Machine& m) {
            m.memory().write(strip_pending,rig.below(4) ? rig.pick(strips) : rig.byte());
            m.memory().write(sheet_bank,rig.below(2) ? static_cast<std::uint8_t>(rig.below(32)) : rig.byte());
            camera(rig,m);
        },hex(unit.address));
    // The column's second and third set-up pieces: B the pending column, C 0 or 8, A the vertical shadow.
    for (const std::uint16_t entry:{0x72C6,0x72DE})
        for (unsigned i=0;i<2000;++i) rig.run(at(entry),[&](Machine& m) {
            camera(rig,m);
            set_a(m,rig.below(2) ? rig.pick(edge_scroll) : rig.byte());
            set_bc(m,static_cast<std::uint16_t>((rig.below(4) ? rig.pick(strips)&0x0C : rig.byte())<<8|(rig.below(4) ? 8*rig.below(2) : rig.byte())));
        },hex(entry));
    // The resumes: one metatile each, from a window address and a mirror address at the edges.
    constexpr std::uint8_t window_low[]{0x00,0x01,0x0F,0x10,0xAF,0xB0,0xB1,0xBF,0xC0,0xFF};
    constexpr std::uint8_t mirror_low[]{0x00,0x02,0x3C,0x3E,0x40,0x7E,0x80,0xBE,0xC0,0xFE,0xFF};
    constexpr std::uint8_t mirror_high[]{0xD0,0xD1,0xD5,0xD6,0xD7,0xDF};
    for (const std::uint16_t entry:{0x72FE,0x7397,0x7488})
        for (unsigned i=0;i<3000;++i) rig.run(at(entry),[&](Machine& m) {
            const auto sp=m.cpu().sp.uint16_value;
            const auto window=static_cast<std::uint16_t>((0xCC+rig.below(4))<<8|(rig.below(2) ? rig.pick(window_low) : rig.byte()));
            const auto count=static_cast<std::uint8_t>(rig.below(2) ? 1+rig.below(2) : rig.byte());
            // The held words: HL (the window address), BC (the count); for the screen also the outer pair.
            write_word(m,sp,window);
            write_word(m,static_cast<std::uint16_t>(sp+2),static_cast<std::uint16_t>(count<<8|rig.byte()));
            if (entry==0x7488) {
                write_word(m,static_cast<std::uint16_t>(sp+4),static_cast<std::uint16_t>((0xCC+rig.below(4))<<8|rig.pick(window_low)));
                write_word(m,static_cast<std::uint16_t>(sp+6),static_cast<std::uint16_t>((rig.below(2) ? 1+rig.below(2) : rig.byte())<<8|rig.byte()));
            } else {
                write_word(m,static_cast<std::uint16_t>(sp+4),probe::Setup{}.return_address);
            }
            set_hl(m,static_cast<std::uint16_t>(rig.byte()|(rig.byte()<<8)));   // L: the metatile number the core loaded
            set_de(m,static_cast<std::uint16_t>(rig.pick(mirror_high)<<8|(rig.below(2) ? rig.pick(mirror_low) : rig.byte())));
        },hex(entry));
    for (unsigned i=0;i<1500;++i) rig.run(at(0x744E),[&](Machine& m) {
        m.memory().write(sheet_bank,rig.below(2) ? static_cast<std::uint8_t>(rig.below(32)) : rig.byte());
        camera(rig,m);
    },"744E");
}
// probe::StackCase::admitted with the inherited words too: every byte from SP
// to SP + inherited + 1 must be mapped and off the mapper registers
// (src/machine/machine.cpp). A helper to hoist into tests/probe.h.
bool admitted(const probe::StackCase& stack, const SegmentSpec& spec) {
    if (!stack.admitted(spec.scratch_stack)) return false;
    if (spec.inherited_stack) for (unsigned i=0;i<spec.inherited_stack+2;++i) {
        const auto address=static_cast<std::uint16_t>(stack.sp+i);
        if (address>=0xfffc || (address>=0xc000 && stack.work_ram_disabled)) return false;
    }
    return true;
}
// Slot 2 mappings, refresh wrap and stack placements on the segments that
// write $FFFF or push and pop.
void machine_probes(Rig& rig) {
    for (const auto entry:{map::step_unit,at(0x7149),at(0x714D),map::screen_window_unit,at(0x7264),map::column_unit,at(0x72FE),
                           at(0x72DE),map::row_unit,at(0x7397),at(0x744E),at(0x7488)}) {
        const auto& spec=rig.harness.routine(entry).spec;
        for (auto setup:probe::slot2_variants({})) {
            setup.seed=rig.seed++;
            auto m=rig.make(entry,setup,[&](Machine& m) { m.memory().write(strip_pending,static_cast<std::uint8_t>(1u<<rig.below(4))); });
            (void)rig.dispatch(m,entry,"slot2 "+hex(entry.address));
        }
        for (const auto r:probe::r_wrap_values(40)) {
            probe::Setup setup; setup.r=r;
            auto m=rig.make(entry,setup,{});
            (void)rig.dispatch(m,entry,"r "+hex(entry.address));
        }
        for (const auto& stack:probe::stack_cases()) {
            probe::Setup setup; setup.sp=stack.sp; setup.cartridge_ram=stack.cartridge_ram; setup.work_ram_disabled=stack.work_ram_disabled;
            auto m=rig.make(entry,setup,{});
            if (!admitted(stack,spec)) {
                rig.harness.run_fallback(m,entry,RoutineFallback::unmapped_stack);
                ++rig.fallbacks;
                continue;
            }
            (void)rig.dispatch(m,entry,std::string("stack ")+stack.name+" "+hex(entry.address),stack.work_ram_disabled);
        }
    }
}
// Every declared exit of every entry was taken by some probe.
void exits_covered(const Rig& rig) {
    for (const auto& w:wanted()) {
        const auto found=rig.spy.taken.find(at(w.entry));
        check(found!=rig.spy.taken.end(),"no probe ran entry "+hex(w.entry));
        // The step's parts share one exit set; each part reaches the exits after it in the code.
        static const std::map<std::uint16_t,std::set<std::uint16_t>> step_reach{
            {0x714D,{0x718D,0x71B2,0x71F4,0x7209,0x723C}},{0x7190,{0x71B2,0x71F4,0x7209,0x723C,0x71E0}},
            {0x71B5,{0x71F4,0x7209,0x723C,0x71E0}},{0x71F7,{0x7209,0x723C,0x7227}},{0x720C,{0x723C,0x7227}}};
        const auto reach=step_reach.find(w.entry);
        for (const auto& exit:w.exits) {
            const bool reachable=reach==step_reach.end() || reach->second.count(exit.address);
            check(found->second.count(exit.address)==(reachable ? 1u : 0u),
                  "the probes of "+hex(w.entry)+(reachable ? " never took" : " took")+" the exit "+hex(exit.address));
        }
    }
}

// -- 2. Chains from a real map frame ---------------------------------------------------------
// A whole call run with routines on (every C++ segment verified) beside the
// same call run natively; both stop at `stop` and must be equal machines.
struct ChainCount { std::uint64_t cpp=0, fallbacks=0; };
ChainCount chain(Rig& rig, const Machine& base, RoutineEntry entry, std::uint16_t stop, const Rig::Prepare& prepare, const std::string& what) {
    probe::Setup setup;
    setup.seed=rig.seed++;
    setup.r=rig.byte();
    auto machine=map_probe::prepare_at(base,entry,setup);
    if (prepare) prepare(machine);
    Machine native=machine;
    auto before=[&] { std::uint64_t c=0, f=0; for (const auto& [e,r]:rig.harness.registry().entries()) { c+=r.counters.cpp_calls; f+=r.counters.fallback_calls; } return std::pair{c,f}; };
    const auto [c0,f0]=before();
    auto run=[&](Machine& m, RoutineRegistry* registry, const char* side) {
        for (unsigned steps=0;m.cpu().pc.uint16_value!=stop;++steps) {
            check(steps<2'000'000,std::string("a ")+side+" chain did not reach its stop: "+what+" (PC "+hex(m.cpu().pc.uint16_value)+
                  ", SP "+hex(m.cpu().sp.uint16_value)+")");
            if (steps%256==0) { (void)m.take_psg_writes(); (void)m.take_samples(); }
            try { if (registry) m.step(*registry); else m.step(); }
            catch (const std::exception& error) { throw std::runtime_error(std::string(error.what())+" ["+what+"]"); }
        }
    };
    run(native,nullptr,"native");
    run(machine,&rig.harness.registry(),"routines-on");
    std::string fields;
    // The sample queues were drained at different steps (a C++ segment is one step).
    (void)machine.take_psg_writes(); (void)machine.take_samples(); (void)native.take_psg_writes(); (void)native.take_samples();
    for (const auto& difference:native.differences(machine))
        if (difference.field.rfind("psg.samples",0)!=0) fields+=" "+difference.field;
    check(fields.empty(),"the chain differs from the native call ("+what+"):"+fields);
    const auto [c1,f1]=before();
    return {c1-c0,f1-f0};
}
// The camera values the chains use: the four edges of the world on each axis.
struct Camera { std::uint16_t x, y; std::uint8_t scroll; };
constexpr Camera chain_cameras[]{
    {0x0000,0x0000,0x00},{0x07FF,0x07BF,0xDF},{0x07F8,0x0010,0x20},{0x0007,0x07B0,0x1F},{0x0400,0x0300,0x60},
    {0x0001,0x00BF,0xC0},{0x07F0,0x0700,0xE0},{0x00F8,0x01BF,0x7F}};
void set_camera(Machine& m, const Camera& c) {
    write_word(m,map::camera_x,c.x); write_word(m,map::camera_y,c.y);
    m.memory().write(map::scroll_x,static_cast<std::uint8_t>(-c.x));
    m.memory().write(map::scroll_y,c.scroll);
}
void chain_probes(Rig& rig, const Machine& base, const std::string& name) {
    ChainCount total;
    auto add=[&](ChainCount c) { total.cpp+=c.cpp; total.fallbacks+=c.fallbacks; };
    const std::uint16_t back=probe::Setup{}.return_address;
    unsigned chains=0;
    for (const auto& c:chain_cameras) {
        add(chain(rig,base,map::screen_window_unit,back,[&](Machine& m) { set_camera(m,c); },name+" 723D "+hex(c.x)+" "+hex(c.y))); ++chains;
    }
    for (const auto& c:chain_cameras) {
        // The three strips, whole calls.
        for (const std::uint8_t strip:{0x01,0x02,0x04,0x08,0x00}) {
            add(chain(rig,base,map::column_unit,back,[&](Machine& m) { set_camera(m,c); m.memory().write(strip_pending,strip); },name+" 72A6 "+hex(strip)));
            add(chain(rig,base,map::row_unit,back,[&](Machine& m) { set_camera(m,c); m.memory().write(strip_pending,strip); },name+" 733A "+hex(strip)));
            chains+=2;
        }
        // The whole screen, to its jump to the frame wait.
        const auto wait=static_cast<std::uint16_t>(rig.rom.byte(1,0x74E2)|(rig.rom.byte(1,0x74E3)<<8));
        add(chain(rig,base,at(0x744E),wait,[&](Machine& m) { set_camera(m,c); },name+" 744E")); ++chains;
        // A step under way in each direction, on foot and in a vehicle, with
        // the camera at the edge (a continuing step reads no pad and asks no terrain).
        for (const std::uint8_t direction:{0x01,0x02,0x04,0x08})
            for (const std::uint8_t transport:{0x00,0x04}) {
                add(chain(rig,base,map::step_unit,back,[&](Machine& m) {
                    set_camera(m,c);
                    m.memory().write(step_counter,0x05); m.memory().write(strip_pending,direction); m.memory().write(map::transport,transport);
                },name+" 7143 "+hex(direction)+" "+hex(transport)));
                ++chains;
            }
    }
    // Every 01:7143 call over 0..15 frames left of a step, every pad direction (new steps ask the terrain).
    for (unsigned i=0;i<64;++i) {
        add(chain(rig,base,map::step_unit,back,[&](Machine& m) {
            m.memory().write(step_counter,static_cast<std::uint8_t>(rig.below(2) ? 0 : rig.below(16)));
            m.memory().write(pad_held,rig.byte());
            m.memory().write(strip_pending,static_cast<std::uint8_t>(1u<<rig.below(4)));
        },name+" 7143 pad")); ++chains;
    }
    check(total.cpp>0,"the chains ran C++ segments");
    std::cout<<name<<": "<<chains<<" chains from a map frame equal to the native calls ("<<total.cpp<<" C++ calls, "
             <<total.fallbacks<<" fallbacks).\n";
}

// A map frame well after the map load (mode 9 starts with the load itself):
// the walking mode for 150 frames in a row.
std::function<bool(const Machine&)> settled_on_map() {
    return [run=0u](const Machine& machine) mutable { run=map_probe::on_map(machine) ? run+1 : 0; return run>=150; };
}

// -- 3. Scenario probes: steps across the wrap of the world ----------------------------------
// From a map frame, the camera is put next to the wrap of the world (screen 7
// to 0 across or down, or 0 to 7) with a step under way in that direction;
// the step continues over several frames on both machines, not to its end.
unsigned wrap_scenario(const probe::Rom& rom, const map_probe::StartImage& image, const replay::Recording& recording,
                       const std::string& name, std::uint8_t transport) {
    map_probe::Scenario s(rom,image,name+" wrap");
    std::uint64_t from=0;
    check(s.play(recording,from,settled_on_map()),name+" never settles on the map");
    s.frames({},4);
    struct Case { std::uint8_t direction; std::uint16_t x, y; std::uint8_t scroll; };
    const Case cases[]{{0x08,0x07F8,0x0300,0x60},{0x04,0x0006,0x0300,0x60},{0x02,0x0400,0x07B8,0xD8},{0x01,0x0400,0x0004,0x04}};
    unsigned wraps=0;
    for (const auto& c:cases) {
        s.poke_word(map::camera_x,c.x); s.poke_word(map::camera_y,c.y);
        s.poke(map::scroll_x,static_cast<std::uint8_t>(-c.x)); s.poke(map::scroll_y,c.scroll);
        s.poke(map::transport,transport);
        s.poke(strip_pending,c.direction); s.poke(step_counter,transport ? 7 : 15);
        const auto x0=s.peek(map::camera_x+1), y0=s.peek(map::camera_y+1);
        s.frames({},transport ? 6 : 12);    // not to the step's end, which looks for entrances
        check(map_probe::on_map(s.machine()),"the wrap scenario stays on the map");
        if (s.peek(map::camera_x+1)!=x0 || s.peek(map::camera_y+1)!=y0) ++wraps;
    }
    const auto [cpp,fallbacks]=s.calls(part);
    check(cpp>0,"the wrap scenario ran the scroll segments in C++");
    std::cout<<name<<" transport "<<+transport<<": "<<wraps<<" of 4 steps crossed the world's wrap; "<<cpp<<" C++ calls, "
             <<fallbacks<<" fallbacks, every frame equal to the routines-off run.\n";
    return wraps;
}

void probes(const map_probe::Inputs& inputs, bool& skipped) {
    Rig rig(inputs.rom);
    step_probes(rig);
    window_probes(rig);
    strip_probes(rig);
    machine_probes(rig);
    exits_covered(rig);
    std::cout<<rig.count<<" power-on state probes ran in C++ ("<<rig.refusals<<" refused with work RAM off, "<<rig.fallbacks
             <<" stack fallbacks); every declared exit was taken.\n";
    if (inputs.recordings.empty()) { std::cout<<"No recordings folder: the map-frame probes are skipped.\n"; skipped=true; return; }
    const std::string stem="later-town";
    const auto recording=replay::read(inputs.recording(stem));
    check(recording.rom_sha1==inputs.rom.sha1,stem+" was recorded with another ROM");
    const auto image=map_probe::start_image(inputs,stem,recording);
    if (!image) { std::cout<<stem<<": start image missing (reference folder not configured), map-frame probes skipped.\n"; skipped=true; return; }
    {
        map_probe::Scenario s(inputs.rom,*image,stem);
        std::uint64_t from=0;
        check(s.play(recording,from,settled_on_map()),stem+" never settles on the map");
        s.frames({},8);
        check(s.peek(screen_table+1)>=0x80 && s.peek(screen_table+1)<0xC0,"the area's screen table lies in slot 2");
        chain_probes(rig,s.machine(),stem);
    }
    unsigned wraps=0;
    for (const std::uint8_t transport:{0x00,0x04}) wraps+=wrap_scenario(inputs.rom,*image,recording,stem,transport);
    check(wraps==8,"every scenario step crossed the world's wrap");
}
}

int main(int argc, char** argv) {
    try {
        registration();
        if (argc==1) return 0;
        const auto inputs=map_probe::load(argc,argv);
        if (!inputs) return 77;
        bool skipped=false;
        probes(*inputs,skipped);
        return skipped ? 77 : 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
