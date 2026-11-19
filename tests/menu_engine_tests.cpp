// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 12, part engine: the registration catalog and the probes.
//   no arguments   the part's registration against its plan (every entry
//                  planned with its unit, or inside the part's ranges with a
//                  unit of the part; never a native instruction) and the
//                  exits and permissions of segments.md table 4W1; no ROM
//   <rom> <us|br> <recordings folder> [<reference folder> [<work folder>]]
//                  the part's state and scenario probes on that ROM (exit 77
//                  without the ROM; a missing reference folder skips what needs
//                  it with a message and returns 77 at the end)
//
// The ROM run has these parts:
//   - the code shape the segments rely on (DI/EI and RST 08 positions, the
//     jumps into the shared bodies, the digit helper's calls), read from the
//     image;
//   - state probes from power-on: every one of the 30 entries with random
//     inputs biased towards each branch, random video memory and window
//     buffers, the VDP's read buffer and address left by a write or a read,
//     every stack placement and slot-2 mapping, the refresh counter around its
//     wrap; each call verified against the original and required to cost
//     exactly its announced bound, every declared exit taken somewhere;
//   - every window size of the game (the call sites' BC) through the three
//     row loops, and their chunks started at every distance from the frame
//     boundary (dispatch falls back exactly when the bound exceeds what is
//     left of the frame);
//   - ROM runs of the original: the number printers print every value as it
//     is, and the shop line of an empty item;
//   - state probes from real frames (an open dialogue on opening, the field
//     menu on field-menu, a choice in a dialogue on places; the field menu of
//     first-battle on the Brazilian ROM), and on field-menu a scenario with
//     edge money values poked (needs the reference folder).
#include "game/menu_kit.h"
#include "menu_probe.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace psmodern;
using probe::check;
namespace menu=game::menu;
namespace kit=game::kit;
namespace {
constexpr RoutineEntry at0(std::uint16_t address) { return {0,address}; }
std::string hex(unsigned value) { std::ostringstream text; text<<std::hex<<std::uppercase<<value; return text.str(); }

// ---- Registration catalog --------------------------------------------------------
SegmentExit R(std::uint16_t address, std::int8_t stack=0) { return kit::returned_at(address,stack); }
SegmentExit F(std::uint16_t address, std::int8_t stack=0) { return kit::core_final_at(address,stack); }
SegmentExit X(std::uint16_t address, std::int8_t stack=0) { return kit::conditional_at(address,stack); }
SegmentExit XL(std::uint16_t address, std::uint16_t after, std::int8_t stack=0) {
    return game::menu_kit::conditional_loop_at(address,after,stack);
}
struct Want {
    RoutineEntry entry, unit;
    std::vector<SegmentExit> exits;
    kit::Permissions permissions={};
};
// segments.md table 4W1 with this part's deviations (section 7): the loop
// exits 00:3186 after 128 cycles (planned 512) and 00:3AB4 after 96 (planned
// 768); 00:3A8D and 00:3A92 one byte each, ending on the JP NZ at 00:3A94
// (planned: an XL after 128 and the row's DJNZ); 00:3A97 not registered (its
// row end stays native); and the exits no state can reach from an entry left
// undeclared (00:3199 from 00:315D, 00:3ABE from 00:3AA7 and 00:3AAB).
const std::set<RoutineEntry> unregistered{at0(0x3A97)};   // not in the plan: menu::native_instructions
std::vector<Want> wanted() {
    return {
        {at0(0x2FD9),menu::number_wide_label_unit,{F(0x302E)},{.scratch_stack=6}},
        {at0(0x2FE2),menu::number_unit,{F(0x302E)},{.scratch_stack=6}},
        {at0(0x3037),menu::long_number_unit,{F(0x309F)},{.scratch_stack=6}},
        {at0(0x315D),menu::enemy_name_line_unit,{XL(0x3186,128,-6)},{.scratch_stack=8}},
        {at0(0x316F),menu::enemy_name_line_unit,{XL(0x3186,128),F(0x3199,+2)},{.scratch_stack=2,.inherited_stack=2}},
        {at0(0x3188),menu::enemy_name_line_unit,{F(0x3199,+2)},{.scratch_stack=2,.inherited_stack=2}},
        {at0(0x342C),menu::dialogue_scroll_two_unit,{F(0x342F,-6)},{.scratch_stack=6}},
        {at0(0x3435),menu::dialogue_scroll_two_unit,{R(0x3438,+6)},{.inherited_stack=6}},
        {at0(0x3439),menu::dialogue_scroll_unit,{F(0x3442)}},
        {at0(0x3445),menu::dialogue_scroll_unit,{F(0x344E)}},
        {at0(0x345A),menu::dialogue_scroll_unit,{F(0x345E)}},
        {at0(0x3461),menu::dialogue_scroll_unit,{F(0x345E),R(0x3463)}},
        {at0(0x356C),menu::item_name_line_unit,{F(0x35B2,-4)},{.scratch_stack=8}},
        {at0(0x35BD),menu::money_wide_label_unit,{F(0x3631)},{.scratch_stack=6}},
        {at0(0x35C5),menu::money_unit,{F(0x3631)},{.scratch_stack=6}},
        {at0(0x3645),menu::digit_unit,{R(0x3655)},{.scratch_stack=2}},
        {at0(0x393B),menu::shop_line_unit,{F(0x3631)},{.scratch_stack=6}},
        {at0(0x3A57),menu::draw_window_unit,{F(0x3A60,-2)},{.slot2_mapper=true,.scratch_stack=2}},
        {at0(0x3A63),menu::draw_window_unit,{R(0x3A67,+2)},{.slot2_mapper=true,.inherited_stack=2}},
        {at0(0x3A6A),menu::row_drawer_unit,{F(0x3A78)},{.scratch_stack=2}},
        {at0(0x3A7F),menu::row_drawer_unit,{X(0x3A80,+2)},{.inherited_stack=2}},
        {at0(0x3A83),menu::draw_window_at_once_unit,{F(0x3A87,-2)},{.scratch_stack=2}},
        {at0(0x3A8D),menu::draw_window_at_once_unit,{X(0x3A94,-2)},{.scratch_stack=4}},
        {at0(0x3A92),menu::draw_window_at_once_unit,{X(0x3A94)}},
        {at0(0x3AA2),menu::draw_window_at_once_unit,{R(0x3AA5)},{.slot2_mapper=true}},
        {at0(0x3AA7),menu::save_under_unit,{XL(0x3AB4,96,-6)},{.scratch_stack=8}},
        {at0(0x3AAB),menu::save_under_unit,{XL(0x3AB4,96,-2)},{.scratch_stack=4}},
        {at0(0x3AB0),menu::save_under_unit,{XL(0x3AB4,96),X(0x3ABE,+2)},{.scratch_stack=2,.inherited_stack=2}},
        {at0(0x3AB7),menu::save_under_unit,{X(0x3ABE,+2)},{.inherited_stack=2}},
    };
}
void catalog() {
    const auto entries=menu_probe::registered_entries(menu::Part::engine,game::register_menu_engine);
    RoutineRegistry registry;
    game::register_menu_engine(registry);
    const auto want=wanted();
    check(want.size()==menu::engine_segments.size() && entries.size()==want.size(),
          "every planned engine segment is registered, and nothing else");
    for (const auto e:unregistered) {
        check(registry.find(e)==nullptr,"engine entry "+hex(e.address)+" is left native");
        check(std::find(menu::native_instructions.begin(),menu::native_instructions.end(),e)!=menu::native_instructions.end(),
              "engine entry "+hex(e.address)+" is listed among the native instructions");
        check(std::none_of(menu::engine_segments.begin(),menu::engine_segments.end(),[&](const auto& s) { return s.entry==e; }),
              "engine entry "+hex(e.address)+" is not planned");
    }
    for (const auto& w:want) {
        const auto planned=std::find_if(menu::engine_segments.begin(),menu::engine_segments.end(),[&](const auto& s) { return s.entry==w.entry; });
        check(planned!=menu::engine_segments.end() && planned->unit==w.unit,"engine entry "+hex(w.entry.address)+" is planned with its unit");
        const auto* routine=registry.find(w.entry);
        check(routine!=nullptr,"planned engine entry "+hex(w.entry.address)+" is registered");
        const auto& spec=routine->spec;
        check(spec.unit==w.unit && spec.slot2_mapper==w.permissions.slot2_mapper && spec.scratch_stack==w.permissions.scratch_stack &&
              spec.inherited_stack==w.permissions.inherited_stack && !spec.single_instruction,
              "engine entry "+hex(w.entry.address)+" has the unit and permissions of table 4W1");
        check(spec.exits.size()==w.exits.size(),"engine entry "+hex(w.entry.address)+" declares the exits of table 4W1");
        for (const auto& exit:w.exits)
            check(std::count_if(spec.exits.begin(),spec.exits.end(),[&](const auto& e) { return e.same_declaration(exit); })==1,
                  "engine entry "+hex(w.entry.address)+": exit "+hex(exit.address)+" is declared exactly as in table 4W1");
    }
    std::cout<<entries.size()<<" of "<<menu::engine_segments.size()<<" planned engine segments registered (00:3A97, the row end, stays native), with the exits and permissions of table 4W1 and the part's deviations.\n";
}

// ---- The ROM's code shape ------------------------------------------------------------------
std::uint16_t word(const probe::Rom& rom, std::uint16_t address) {
    return static_cast<std::uint16_t>(rom.byte(0,address)|(rom.byte(0,static_cast<std::uint16_t>(address+1))<<8));
}
void code_shape(const probe::Rom& rom) {
    // DI before every interrupts-off entry, EI where the segments end.
    for (const std::uint16_t a:{0x2FD8,0x2FE1,0x3036,0x315C,0x356B,0x35BC,0x35C4,0x393A,0x3A69,0x3A8C,0x3AA6})
        check(rom.byte(0,a)==0xF3,"a DI at "+hex(a));
    for (const std::uint16_t a:{0x302F,0x30A0,0x319C,0x35B5,0x3632,0x3A79,0x3AA0,0x3AC2})
        check(rom.byte(0,a)==0xFB,"an EI at "+hex(a));
    // The plain finals the segments end on: ex de,hl, and ld a,n at 3A87.
    for (const std::uint16_t a:{0x302E,0x309F,0x3199,0x35B2,0x3631,0x3A78}) check(rom.byte(0,a)==0xEB,"an ex de,hl final at "+hex(a));
    check(rom.byte(0,0x3A87)==0x3E,"the ld a,n final at 3A87");
    // RST 08 rendered inline.
    for (const std::uint16_t a:{0x2FDB,0x2FE4,0x3039,0x3160,0x356F,0x35BE,0x35C6,0x393D,0x3A6A,0x3A8E,0x3AAC})
        check(rom.byte(0,a)==0xCF,"an RST 08 at "+hex(a));
    check(rom.byte(0,0x0008)==0x7B && rom.byte(0,0x0009)==0xD3 && rom.byte(0,0x000A)==0xBF && rom.byte(0,0x000B)==0x7A &&
          rom.byte(0,0x000C)==0xD3 && rom.byte(0,0x000D)==0xBF && rom.byte(0,0x000E)==0xC9,"RST 08 writes E then D to the VDP control port");
    // The jumps into the shared bodies, and the digit helper's calls.
    const std::pair<std::uint16_t,std::uint16_t> jumps[]{{0x2FDE,0x2FE7},{0x35C1,0x35C9},{0x3993,0x35D5},{0x3996,0x3620}};
    for (const auto& [at,target]:jumps) check(word(rom,static_cast<std::uint16_t>(at+1))==target,"the jump at "+hex(at)+" goes to "+hex(target));
    for (const std::uint16_t a:{0x2FFC,0x300B,0x301A,0x3051,0x305F,0x306D,0x307C,0x308B,0x35E3,0x35F1,0x35FF,0x360E,0x361D})
        check(rom.byte(0,a)==0xCD && word(rom,static_cast<std::uint16_t>(a+1))==0x3645,"a call of the digit helper at "+hex(a));
    // The bare calls the segments end on.
    const std::pair<std::uint16_t,std::uint16_t> calls[]{{0x342F,0x3439},{0x3432,0x3439},{0x3442,0x3AA6},{0x344E,0x3A83},
        {0x3457,0x3A83},{0x345E,0x0052},{0x3A60,0x3A68},{0x3A7C,0x0052}};
    for (const auto& [at,target]:calls) check(rom.byte(0,at)==0xCD && word(rom,static_cast<std::uint16_t>(at+1))==target,"the call at "+hex(at));
    // The loops the segments cut: djnz/jp nz and their targets.
    check(rom.byte(0,0x3186)==0x10 && rom.byte(0,0x3187)==0xE7,"the character loop's djnz at 3186 goes to 316F");
    check(rom.byte(0,0x3A94)==0xC2 && word(rom,0x3A95)==0x3A92,"the OUTI loop's jp nz at 3A94");
    check(rom.byte(0,0x3A9E)==0x10 && rom.byte(0,0x3A9F)==0xED,"the row loop's djnz at 3A9E goes to 3A8D");
    check(rom.byte(0,0x3AB4)==0xC2 && word(rom,0x3AB5)==0x3AB0,"the INI loop's jp nz at 3AB4");
    check(rom.byte(0,0x3ABE)==0x10 && rom.byte(0,0x3ABF)==0xEB,"the row loop's djnz at 3ABE goes to 3AAB");
    check(rom.byte(0,0x3AB0)==0xED && rom.byte(0,0x3AB1)==0xA2,"INI at 3AB0");
    check(rom.byte(0,0x3461)==0x10 && rom.byte(0,0x3462)==0xF9,"the scroll wait's djnz goes to 345C");
    std::cout<<"code shape: DI/EI, the finals, RST 08, the shared bodies, the digit calls and the cut loops are as the segments render them.\n";
}

// ---- State probes -------------------------------------------------------------------------
// The exit each call of the harness took (milestone 14: from the registry).
struct Spy { std::optional<SegmentExit> last; };
using Prepare=std::function<void(Machine&,std::mt19937_64&)>;
struct Rig {
    probe::Harness harness;
    Spy spy;
    std::mt19937_64 random{0x3AA6};
    std::uint64_t seed=1, count=0, refusals=0, unmapped=0, rom_stack_probes=0;
    std::map<RoutineEntry,std::set<std::uint16_t>> reached;
    std::map<RoutineEntry,std::uint64_t> per_entry;
    explicit Rig(const probe::Rom& rom) : harness(rom) { harness.exit_sink=&spy.last; }

    static bool admitted(const probe::Setup& s, unsigned scratch, unsigned inherited) {
        auto bad=[&](unsigned offset, bool strict) {
            const auto a=static_cast<std::uint16_t>(s.sp+offset);
            return (a>=0xC000 && s.work_ram_disabled) || (strict && a>=0xFFFC);
        };
        bool refused=bad(0,scratch>0) || bad(1,scratch>0);
        for (unsigned i=1;i<=scratch;++i) refused=refused || bad(0x10000-i,true);
        if (inherited) for (unsigned i=0;i<inherited+2;++i) refused=refused || bad(i,true);
        return !refused;
    }
    static bool stack_holds(const probe::Setup& s) {
        for (unsigned i=1;i<=8;++i) {
            const auto a=static_cast<std::uint16_t>(s.sp-i);
            if (a>=0xFFFC) return false;
            if (a>=0xC000) { if (s.work_ram_disabled) return false; }
            else if (a<0x8000 || !s.cartridge_ram) return false;
        }
        return true;
    }
    // One probe; returns the cycles of a C++ call (0 when it fell back).
    std::uint64_t run(RoutineEntry entry, probe::Setup setup, const Machine* base, const Prepare& prepare) {
        setup.seed=seed++;
        auto machine=base ? menu_probe::prepare_at(*base,entry,setup) : harness.prepare(entry,setup);
        auto& memory=machine.memory();
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()&~0x10);
        if (prepare) prepare(machine,random);
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
        const auto& routine=harness.routine(entry);
        const bool rom_stack=routine.spec.slot2_mapper && setup.sp>=0x8000 && setup.sp<0xC000 && !setup.cartridge_ram;
        if (!admitted(setup,routine.spec.scratch_stack,routine.spec.inherited_stack)) {
            harness.run_fallback(machine,entry,RoutineFallback::unmapped_stack);
            ++unmapped;
            return 0;
        }
        const auto bound=routine.bound(machine);
        if (bound>Machine::frame_cycles) {
            check(!stack_holds(setup) || setup.work_ram_disabled,"engine entry "+hex(entry.address)+" refused a state with a working stack");
            harness.run_fallback(machine,entry,RoutineFallback::frame);
            ++refusals;
            return 0;
        }
        spy.last.reset();
        probe::Harness::Result result{};
        const Machine pristine=machine;
        try {
            if (rom_stack) {
                oracle::NativeMachine original(machine);
                harness.verified=false;
                try { result=harness.run_cpp(machine,entry); } catch (...) { harness.verified=true; throw; }
                harness.verified=true;
                const auto stop=original.cycles()+result.cycles;
                while (original.cycles()<stop) original.step();
                check(original.cycles()==stop,"ROM-stack probe: the original does not stop at the C++ call's cycle count");
                std::string fields;
                for (const auto& difference:oracle::differences(machine,original,oracle::Scope::whole)) fields+=" "+difference;
                check(fields.empty(),"ROM-stack probe: C++ differs from the original at equal cycles:"+fields);
                ++rom_stack_probes;
            } else result=harness.run_cpp(machine,entry);
        } catch (const std::exception& error) {
            std::ostringstream text;
            // The lockstep reports only that the body diverged; run it again unverified for the reason.
            auto again=pristine;
            harness.verified=false;
            try { (void)harness.run_cpp(again,entry); text<<"(unverified run passed) "; }
            catch (const std::exception& reason) { text<<"("<<reason.what()<<") "; }
            harness.verified=true;
            text<<error.what()<<" [entry "<<std::hex<<entry.address<<" seed "<<std::dec<<setup.seed<<" sp "<<std::hex<<setup.sp
                <<" ram_off "<<setup.work_ram_disabled<<" cart "<<setup.cartridge_ram<<" bank2 "<<+setup.banks[2]<<"]";
            throw std::runtime_error(text.str());
        }
        if (!setup.work_ram_disabled)
            check(result.cycles==bound,"engine entry "+hex(entry.address)+" cost "+std::to_string(result.cycles)+
                  " cycles, its bound said "+std::to_string(bound));
        ++count; ++per_entry[entry];
        if (!rom_stack) {
            check(spy.last.has_value(),"an engine probe took no exit");
            reached[entry].insert(spy.last->address);
        }
        return result.cycles;
    }
};

std::uint8_t byte(std::mt19937_64& g) { return static_cast<std::uint8_t>(g()); }
bool one_in(std::mt19937_64& g, unsigned n) { return g()%n==0; }
std::uint16_t anywhere(std::mt19937_64& g) {
    switch (g()%4) { case 0: return static_cast<std::uint16_t>(0xC000+g()%0x2000); case 1: return static_cast<std::uint16_t>(g()%0x8000);
        case 2: return static_cast<std::uint16_t>(0x8000+g()%0x4000); default: return static_cast<std::uint16_t>(g()); }
}
// A window's columns or a row remainder: biased to 1, 2, the game's widths and 0 (256).
std::uint8_t columns(std::mt19937_64& g) {
    static constexpr std::uint8_t widths[]{1,2,3,0x10,0x18,0x1C,0x20,0x24,0x30,0x40};
    switch (g()%6) { case 0: return 0; case 1: case 2: case 3: return widths[g()%std::size(widths)]; default: return byte(g); }
}
std::uint8_t rows(std::mt19937_64& g) { return one_in(g,6) ? byte(g) : static_cast<std::uint8_t>(1+g()%8); }
void set_bc(Machine& m, std::uint8_t b, std::uint8_t c) { m.cpu().bc.uint8_values.at_1=b; m.cpu().bc.uint8_values.at_0=c; }
// A VRAM write address (7800h-7EFFh, the name table, or anywhere in VRAM), or
// a read address. Never a register write (codes 2 and 3 with bit 6 cleared by
// 00:3AA6): the game never gives one, and the registry refuses writes of
// registers 0, 1 and 10 at run time.
std::uint16_t vram_address(std::mt19937_64& g) {
    switch (g()%4) { case 0: case 1: return static_cast<std::uint16_t>(0x7800+g()%0x700); case 2: return static_cast<std::uint16_t>(0x4000|(g()&0x3FFF));
        default: return static_cast<std::uint16_t>(g()&0x7FFF); }
}
// The VDP left as by an earlier write or read (its address, code and read buffer).
void vdp_state(Machine& m, std::mt19937_64& g) {
    const auto at=m.cycles();
    switch (g()%4) {
        case 0: break;
        case 1: m.write_port(0xBF,byte(g),at); m.write_port(0xBF,static_cast<std::uint8_t>(byte(g)&0x3F),at); (void)m.read_port(0xBE,at); break;
        case 2: m.write_port(0xBF,byte(g),at); m.write_port(0xBF,static_cast<std::uint8_t>(0x40|(byte(g)&0x3F)),at); m.write_port(0xBE,byte(g),at); break;
        default: m.write_port(0xBF,byte(g),at); m.write_port(0xBF,static_cast<std::uint8_t>(byte(g)&0x3F),at); break;
    }
}
// Words above SP (held by an earlier segment of the unit), then the return word.
void stack_words(Machine& m, std::initializer_list<std::uint16_t> words) {
    auto sp=m.cpu().sp.uint16_value;
    for (const auto w:words) {
        for (const auto b:{static_cast<std::uint8_t>(w),static_cast<std::uint8_t>(w>>8)}) {
            if (sp>=0xC000 && sp<0xFFFC) m.memory().write(sp,b);
            else if (sp>=0x8000 && sp<0xC000) m.memory().write(sp,b);   // cartridge RAM when mapped; ROM ignores it
            sp=static_cast<std::uint16_t>(sp+1);
        }
    }
}
constexpr std::uint16_t return_word=0x0180;

// The inputs each entry reads, randomized; resume entries get the invariants
// the code before them guarantees (C = the data port).
std::vector<std::pair<RoutineEntry,Prepare>> entry_inputs(const probe::Rom& rom) {
    std::vector<std::pair<RoutineEntry,Prepare>> list;
    auto add=[&](std::uint16_t e, Prepare p) { list.emplace_back(at0(e),std::move(p)); };
    // The label printers: a label (one of the game's or anywhere), A or BC the number.
    const std::uint16_t labels[]{word(rom,0x37F4),word(rom,0x37FD),word(rom,0x3812),0x3639};
    auto label_and_vram=[labels](Machine& m, std::mt19937_64& g) {
        vdp_state(m,g);
        m.cpu().hl.uint16_value=one_in(g,3) ? anywhere(g) : labels[g()%std::size(labels)];
        m.cpu().de.uint16_value=vram_address(g);
    };
    for (const std::uint16_t e:{0x2FD9,0x2FE2}) add(e,[label_and_vram](Machine& m, std::mt19937_64& g) {
        label_and_vram(m,g);
        static constexpr std::uint8_t edges[]{0,1,9,10,99,100,101,199,200,255};
        m.cpu().af.uint8_values.at_1=one_in(g,2) ? edges[g()%std::size(edges)] : byte(g);
    });
    add(0x3037,[label_and_vram](Machine& m, std::mt19937_64& g) {
        label_and_vram(m,g);
        static constexpr std::uint16_t edges[]{0,1,9,10,99,100,999,1000,1001,9999,10000,10001,12345,20000,65535};
        m.cpu().bc.uint16_value=one_in(g,2) ? edges[g()%std::size(edges)] : static_cast<std::uint16_t>(g());
    });
    // A name: 8 characters from HL (letters, blanks or anything), row C.
    auto name_text=[](Machine& m, std::mt19937_64& g) {
        const auto hl=static_cast<std::uint16_t>(one_in(g,4) ? anywhere(g) : 0xC000+g()%0x1F00);
        m.cpu().hl.uint16_value=hl;
        if (hl>=0xC000 && hl<0xDFF0)
            for (unsigned i=0;i<8;++i) m.memory().write(static_cast<std::uint16_t>(hl+i),one_in(g,3) ? byte(g) : static_cast<std::uint8_t>(0x20+g()%0x40));
        m.cpu().bc.uint8_values.at_0=one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(g()%2);
        m.cpu().de.uint16_value=vram_address(g);
    };
    add(0x315D,[name_text](Machine& m, std::mt19937_64& g) { vdp_state(m,g); name_text(m,g); });
    add(0x316F,[name_text](Machine& m, std::mt19937_64& g) {
        name_text(m,g); m.cpu().bc.uint8_values.at_1=one_in(g,3) ? byte(g) : static_cast<std::uint8_t>(1+g()%8);
        stack_words(m,{static_cast<std::uint16_t>(vram_address(g)),static_cast<std::uint16_t>(g()),static_cast<std::uint16_t>(g())});
    });
    add(0x3188,[](Machine& m, std::mt19937_64& g) { stack_words(m,{vram_address(g),static_cast<std::uint16_t>(g()),static_cast<std::uint16_t>(g())}); });
    // The dialogue scroll.
    add(0x342C,{});
    add(0x3435,[](Machine& m, std::mt19937_64& g) {
        stack_words(m,{static_cast<std::uint16_t>(g()),static_cast<std::uint16_t>(g()),static_cast<std::uint16_t>(g()),return_word});
    });
    add(0x3439,{}); add(0x3445,{}); add(0x345A,{});
    add(0x3461,[](Machine& m, std::mt19937_64& g) { m.cpu().bc.uint8_values.at_1=one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(1+g()%4); });
    // An item name: the item number at HL (0-63 or anything), row C.
    add(0x356C,[](Machine& m, std::mt19937_64& g) {
        vdp_state(m,g);
        const auto hl=static_cast<std::uint16_t>(one_in(g,4) ? anywhere(g) : 0xC4C0+g()%0x40);
        m.cpu().hl.uint16_value=hl;
        if (hl>=0xC000 && hl<0xDFFF) m.memory().write(hl,one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(g()%0x40));
        m.cpu().bc.uint8_values.at_0=one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(g()%2);
        m.cpu().de.uint16_value=vram_address(g);
    });
    // Money.
    auto money=[](Machine& m, std::mt19937_64& g) {
        vdp_state(m,g);
        static constexpr std::uint16_t edges[]{0,1,9,10,100,1000,9999,10000,10001,65535};
        const auto value=one_in(g,2) ? edges[g()%std::size(edges)] : static_cast<std::uint16_t>(g());
        m.memory().write(menu::money,static_cast<std::uint8_t>(value)); m.memory().write(menu::money+1,static_cast<std::uint8_t>(value>>8));
        m.cpu().de.uint16_value=vram_address(g);
    };
    add(0x35BD,money); add(0x35C5,money);
    // The digit helper: a digit (or anything) with the blank or the digit tile base.
    add(0x3645,[](Machine& m, std::mt19937_64& g) {
        m.cpu().af.uint8_values.at_1=one_in(g,2) ? static_cast<std::uint8_t>(g()%10) : byte(g);
        const std::uint16_t bases[]{0xC010,0xC110,static_cast<std::uint16_t>(g())};
        m.cpu().bc.uint16_value=bases[g()%3];
    });
    // A shop record at HL: item (0, a real one or anything) and price, row C.
    add(0x393B,[](Machine& m, std::mt19937_64& g) {
        vdp_state(m,g);
        const auto hl=static_cast<std::uint16_t>(one_in(g,5) ? anywhere(g) : 0xC000+g()%0x1F00);
        m.cpu().hl.uint16_value=hl;
        if (hl>=0xC000 && hl<0xDFF0) {
            m.memory().write(hl,one_in(g,4) ? 0 : one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(1+g()%0x3F));
            m.memory().write(static_cast<std::uint16_t>(hl+1),byte(g)); m.memory().write(static_cast<std::uint16_t>(hl+2),byte(g));
        }
        m.cpu().bc.uint8_values.at_0=one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(g()%2);
        m.cpu().de.uint16_value=vram_address(g);
    });
    // The window engine.
    add(0x3A57,{});
    add(0x3A63,[](Machine& m, std::mt19937_64& g) {
        const auto bank=one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(g()%32);
        stack_words(m,{static_cast<std::uint16_t>((bank<<8)|byte(g)),return_word});
    });
    auto window=[](Machine& m, std::mt19937_64& g) {
        vdp_state(m,g);
        m.cpu().hl.uint16_value=anywhere(g);
        m.cpu().de.uint16_value=vram_address(g);
        set_bc(m,rows(g),columns(g));
    };
    add(0x3A6A,window);
    add(0x3A7F,[](Machine& m, std::mt19937_64& g) { stack_words(m,{static_cast<std::uint16_t>((rows(g)<<8)|columns(g))}); });
    add(0x3A83,{});
    add(0x3A8D,window);
    auto inside_row=[](Machine& m, std::mt19937_64& g) {
        m.cpu().hl.uint16_value=anywhere(g);
        m.cpu().de.uint16_value=vram_address(g);
        set_bc(m,columns(g),0xBE);
        stack_words(m,{static_cast<std::uint16_t>((rows(g)<<8)|columns(g))});
    };
    add(0x3A92,inside_row);
    add(0x3AA2,[](Machine& m, std::mt19937_64& g) { m.cpu().af.uint8_values.at_1=one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(g()%32); });
    auto save=[](Machine& m, std::mt19937_64& g) {
        vdp_state(m,g);
        m.cpu().hl.uint16_value=one_in(g,3) ? anywhere(g) : static_cast<std::uint16_t>(0xD700+g()%0x700);
        m.cpu().de.uint16_value=vram_address(g);
        set_bc(m,rows(g),columns(g));
    };
    add(0x3AA7,save);
    add(0x3AAB,[save](Machine& m, std::mt19937_64& g) { save(m,g); stack_words(m,{static_cast<std::uint16_t>(g()),static_cast<std::uint16_t>(g())}); });
    add(0x3AB0,[](Machine& m, std::mt19937_64& g) {
        m.cpu().hl.uint16_value=one_in(g,3) ? anywhere(g) : static_cast<std::uint16_t>(0xD700+g()%0x700);
        m.cpu().de.uint16_value=vram_address(g);
        set_bc(m,columns(g),0xBE);
        stack_words(m,{static_cast<std::uint16_t>((rows(g)<<8)|columns(g)),static_cast<std::uint16_t>(g()),static_cast<std::uint16_t>(g())});
    });
    add(0x3AB7,[](Machine& m, std::mt19937_64& g) {
        m.cpu().de.uint16_value=vram_address(g);
        stack_words(m,{static_cast<std::uint16_t>((rows(g)<<8)|columns(g))});
    });
    return list;
}

// Every declared exit taken somewhere.
void exits_reached(const Rig& g) {
    const std::set<std::pair<std::uint16_t,std::uint16_t>> never;
    for (const auto& w:wanted())
        for (const auto& exit:w.exits) {
            if (never.count({w.entry.address,exit.address})) continue;
            const auto found=g.reached.find(w.entry);
            check(found!=g.reached.end() && found->second.count(exit.address),
                  "engine entry "+hex(w.entry.address)+" never took its exit "+hex(exit.address));
        }
}

// A base machine with random VRAM, CRAM and window buffers.
Machine noisy_base(Rig& g, std::uint64_t seed) {
    probe::Setup setup;
    auto m=g.harness.prepare(at0(0x3A57),setup);
    menu_probe::fill_vram(m,seed);
    menu_probe::fill_cram(m,seed);
    menu_probe::fill_window_buffers(m,seed);
    return m;
}

void power_on_probes(const probe::Rom& rom) {
    Rig g(rom);
    const auto list=entry_inputs(rom);
    const auto base=noisy_base(g,7);
    for (const auto& [entry,prepare]:list) {
        for (unsigned i=0;i<120;++i) {
            probe::Setup setup;
            setup.iff1=setup.iff2=(i%4==3);
            setup.r=byte(g.random);
            setup.banks[2]=static_cast<std::uint8_t>(i%3==0 ? g.random()%32 : 27);
            g.run(entry,setup,i%2 ? &base : nullptr,prepare);
        }
        for (const auto& c:probe::stack_cases()) {
            probe::Setup setup;
            setup.sp=c.sp; setup.cartridge_ram=c.cartridge_ram; setup.work_ram_disabled=c.work_ram_disabled;
            g.run(entry,setup,nullptr,prepare);
        }
        for (const auto& setup:probe::slot2_variants({})) g.run(entry,setup,nullptr,prepare);
        if (g.harness.routine(entry).spec.slot2_mapper)
            for (const std::uint8_t bank:{0,2,3,5,15,27,31}) for (const std::uint16_t sp:{0x8000,0x9000,0xA123,0xBFF0})
                for (unsigned k=0;k<3;++k) {
                    probe::Setup setup;
                    setup.sp=sp; setup.banks[2]=bank; setup.r=byte(g.random); setup.iff1=setup.iff2=(k&1);
                    g.run(entry,setup,nullptr,prepare);
                }
        for (const auto r:probe::r_wrap_values(8)) { probe::Setup setup; setup.r=r; g.run(entry,setup,&base,prepare); }
    }
    exits_reached(g);
    // Interrupt sweeps (milestone 14): an IRQ and an NMI at every cycle of every segment (all have more than
    // one instruction); the longest are sampled so that a sweep stays near 300 runs per kind.
    for (const auto& [entry,prepare]:list) {
        for (unsigned variant=0;variant<2;++variant) {
            probe::Setup setup; setup.seed=9000+variant; setup.banks[2]=27;
            std::mt19937_64 random(0x3AA6u+variant*131u+entry.address);
            Machine probe_state=g.harness.prepare(entry,setup);
            if (prepare) prepare(probe_state,random);
            const auto length=g.harness.routine(entry).bound(probe_state);
            const unsigned stride=static_cast<unsigned>(std::max<std::uint64_t>(1,length/300));
            std::mt19937_64 again(0x3AA6u+variant*131u+entry.address);
            probe::sweep(g.harness,entry,setup,[&](Machine& m) { if (prepare) prepare(m,again); },stride);
        }
    }
    check(g.rom_stack_probes>0,"no slot-2 mapper segment was probed with its stack in slot-2 ROM");
    std::cout<<"power-on probes: "<<g.count<<" calls in C++, "<<g.refusals<<" refused states (frame), "<<g.unmapped<<" refused stacks; "
             <<g.rom_stack_probes<<" with the stack in slot-2 ROM (compared with the original at equal cycles).\n";
}

// Every window size of the game through the three row loops: whole rows
// (3A6A, 3A8D, 3AAB/3AA7) and the chunk resumes (3A92, 3AB0) at every
// remainder of a row, then each started at distances from the frame boundary
// across its window: it runs in C++ exactly when the frame has room for its
// bound, and falls back (frame) otherwise.
void window_size_probes(const probe::Rom& rom) {
    Rig g(rom);
    const auto sizes=menu_probe::distinct_window_sizes(rom);
    const auto base=noisy_base(g,11);
    std::uint64_t boundary_cpp=0, boundary_fallbacks=0;
    for (const auto& size:sizes) {
        auto whole=[size](std::uint16_t hl) {
            return [size,hl](Machine& m, std::mt19937_64& g) {
                vdp_state(m,g);
                m.cpu().hl.uint16_value=hl;
                m.cpu().de.uint16_value=vram_address(g);
                set_bc(m,size.rows,size.columns);
            };
        };
        for (unsigned k=0;k<3;++k) {
            probe::Setup setup;
            setup.r=byte(g.random); setup.banks[2]=27;
            g.run(at0(0x3A6A),setup,&base,whole(static_cast<std::uint16_t>(0x8000+g.random()%0x3000)));
            g.run(at0(0x3A8D),setup,&base,whole(static_cast<std::uint16_t>(0x8000+g.random()%0x3000)));
            g.run(at0(0x3AA7),setup,&base,whole(static_cast<std::uint16_t>(0xD700+g.random()%0x400)));
            g.run(at0(0x3AAB),setup,&base,[&](Machine& m, std::mt19937_64& r) {
                whole(static_cast<std::uint16_t>(0xD700+r()%0x400))(m,r); stack_words(m,{static_cast<std::uint16_t>(r()),static_cast<std::uint16_t>(r())});
            });
        }
        for (unsigned left=1;left<=size.columns;++left) {
            auto resume=[size,left](Machine& m, std::mt19937_64& g) {
                m.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xD700+g()%0x400);
                m.cpu().de.uint16_value=vram_address(g);
                set_bc(m,static_cast<std::uint8_t>(left),0xBE);
                stack_words(m,{static_cast<std::uint16_t>((size.rows<<8)|size.columns),static_cast<std::uint16_t>(g()),static_cast<std::uint16_t>(g())});
            };
            probe::Setup setup;
            setup.r=byte(g.random); setup.banks[2]=27;
            g.run(at0(0x3A92),setup,&base,resume);
            g.run(at0(0x3AB0),setup,&base,resume);
        }
        // The frame boundary across each whole-row chunk.
        for (const std::uint16_t entry:{0x3A6A,0x3A8D,0x3AA7}) {
            probe::Setup setup;
            setup.banks[2]=27;
            const auto hl=static_cast<std::uint16_t>(entry==0x3AA7 ? 0xD800 : 0x8100);
            auto m=menu_probe::prepare_at(base,at0(entry),setup);
            whole(hl)(m,g.random);
            const auto bound=g.harness.routine(at0(entry)).bound(m);
            for (const auto remaining:menu_probe::boundary_offsets(bound,9,8)) {
                auto copy=m;
                menu_probe::place_before_boundary(copy,remaining);
                if (remaining>=bound) {
                    const auto cycles=g.harness.run_cpp(copy,at0(entry)).cycles;
                    check(cycles==bound,"a boundary probe cost differs from its bound");
                    ++boundary_cpp;
                } else { g.harness.run_fallback(copy,at0(entry),RoutineFallback::frame); ++boundary_fallbacks; }
            }
        }
    }
    std::cout<<sizes.size()<<" window sizes: "<<g.count<<" row calls in C++; at the frame boundary "<<boundary_cpp<<" in C++ and "
             <<boundary_fallbacks<<" fallbacks (frame), as the bounds said.\n";
}

// The chunks of the printers and the enemy name at the frame boundary too.
void printer_boundary_probes(const probe::Rom& rom) {
    Rig g(rom);
    const auto base=noisy_base(g,13);
    const auto list=entry_inputs(rom);
    std::uint64_t cpp=0, fallbacks=0;
    for (const auto& [entry,prepare]:list) {
        probe::Setup setup;
        setup.banks[2]=27;
        auto m=menu_probe::prepare_at(base,entry,setup);
        if (prepare) prepare(m,g.random);
        const auto bound=g.harness.routine(entry).bound(m);
        if (bound>Machine::frame_cycles) continue;
        for (const auto remaining:menu_probe::boundary_offsets(bound,5,4)) {
            auto copy=m;
            menu_probe::place_before_boundary(copy,remaining);
            if (remaining>=bound) { (void)g.harness.run_cpp(copy,entry); ++cpp; }
            else { g.harness.run_fallback(copy,entry,RoutineFallback::frame); ++fallbacks; }
        }
    }
    std::cout<<"every entry at the frame boundary: "<<cpp<<" in C++, "<<fallbacks<<" fallbacks (frame).\n";
}

// ---- ROM runs of the original ------------------------------------------------------------------
// Runs a printer natively (routines off) from power-on with VRAM write address
// 7800h up to its EI (the frame wait after it needs the frame interrupt) and
// returns the name-table bytes written (tile, attribute pairs).
std::vector<std::uint8_t> native_print(probe::Harness& h, std::uint16_t unit, std::uint16_t ei, const std::function<void(Machine&)>& set,
                                       unsigned bytes) {
    probe::Setup setup;
    setup.banks[2]=27;
    auto m=h.prepare(at0(unit),setup);
    m.cpu().de.uint16_value=0x7800;
    set(m);
    // The original on the oracle's native machine (the engine has no core).
    oracle::NativeMachine original(m);
    for (unsigned i=0;i<200000 && original.cpu().pc.uint16_value!=ei;++i) original.step();
    check(original.cpu().pc.uint16_value==ei,"the printer reached its EI");
    const auto& vram=original.vdp().vram();
    return {vram.begin()+0x3800,vram.begin()+0x3800+bytes};
}
// The digits of a printed number: tile C0h a blank, C1h-CAh the digits 0-9.
std::string shown(const std::vector<std::uint8_t>& written, unsigned from, unsigned digits) {
    std::string text;
    for (unsigned i=0;i<digits;++i) {
        const auto tile=written.at(from+2*i);
        text+=tile==0xC0 ? ' ' : tile>=0xC1 && tile<=0xCA ? static_cast<char>('0'+tile-0xC1) : '?';
    }
    return text;
}
// 00:3036 (the five-digit number of the status window, experience) and the
// money printer 00:35C4 print every value as it is. In 00:3036 the
// thousands and hundreds counts start from `ld a,FFh` without clearing carry
// (the money printer clears it with xor a), but the digit helper's `and 0Fh`
// before them always has: observed on the original.
void number_printers(const probe::Rom& rom) {
    probe::Harness h(rom);
    const auto label=word(rom,0x37FD);
    auto print=[&](std::uint16_t value) {
        return shown(native_print(h,0x3036,0x30A0,[&](Machine& m) { m.cpu().hl.uint16_value=label; m.cpu().bc.uint16_value=value; },12+10),12,5);
    };
    auto money=[&](std::uint16_t value) {
        return shown(native_print(h,0x35C4,0x3632,[&](Machine& m) {
            m.memory().write(menu::money,static_cast<std::uint8_t>(value)); m.memory().write(menu::money+1,static_cast<std::uint8_t>(value>>8));
        },8+10),8,5);
    };
    std::ostringstream report;
    for (const std::uint16_t value:{std::uint16_t{5},std::uint16_t{345},std::uint16_t{2345},std::uint16_t{12345},std::uint16_t{10000},std::uint16_t{65535}})
        report<<" "<<value<<"->'"<<print(value)<<"'/'"<<money(value)<<"'";
    std::cout<<"ROM run, the five-digit printer 00:3036 / the money printer 00:35C4:"<<report.str()<<'\n';
    for (const std::uint16_t value:{std::uint16_t{0},std::uint16_t{7},std::uint16_t{345},std::uint16_t{1000},std::uint16_t{2345},
                                    std::uint16_t{10000},std::uint16_t{12345},std::uint16_t{40009},std::uint16_t{65535}}) {
        std::string want=std::to_string(value);
        want=std::string(5-want.size(),' ')+want;
        check(print(value)==want && money(value)==want,"00:3036 and 00:35C4 print "+std::to_string(value)+" as it is");
    }
}
// 00:393A with an empty item (0): row C is forced to 0, so the bottom row of
// an empty shop line shows the top half of the blank name and no price.
void empty_shop_line(const probe::Rom& rom) {
    probe::Harness h(rom);
    auto line=[&](std::uint8_t item, std::uint8_t row) {
        return native_print(h,0x393A,0x3632,[&](Machine& m) {
            m.memory().write(0xC800,item); m.memory().write(0xC801,0x34); m.memory().write(0xC802,0x12);
            m.cpu().hl.uint16_value=0xC800; m.cpu().bc.uint8_values.at_0=row;
        },2*16);
    };
    const auto full=line(1,1), empty=line(0,1);
    check(shown(full,2*(1+8+1),5)==" 4660","row 1 of a real item ends with its price (1234h = 4660)");
    check(empty==line(0,0),"an empty item draws row 1 exactly as row 0");
    std::cout<<"ROM run: a shop line with item 0 draws the same tiles on both rows (no price).\n";
}

// ---- Probes from real frames -----------------------------------------------------------------
std::optional<std::pair<replay::Recording,menu_probe::StartImage>> recording(const menu_probe::Inputs& inputs, const std::string& stem) {
    if (inputs.recordings.empty()) return std::nullopt;
    auto rec=replay::read(inputs.recording(stem));
    check(rec.rom_sha1==inputs.rom.sha1,stem+" was recorded with another ROM");
    auto image=menu_probe::start_image(inputs,stem,rec);
    if (!image) return std::nullopt;
    return std::pair{std::move(rec),std::move(*image)};
}
// The engine's entries over the RAM and VRAM of a real frame.
std::uint64_t frame_probes(const menu_probe::Inputs& inputs, const Machine& frame) {
    Rig g(inputs.rom);
    const auto list=entry_inputs(inputs.rom);
    for (const auto& [entry,prepare]:list)
        for (unsigned i=0;i<16;++i) {
            probe::Setup setup;
            setup.banks={0,1,static_cast<std::uint8_t>(frame.memory().mapping(0x8000).bank)};
            g.run(entry,setup,&frame,prepare);
        }
    return g.count;
}
// Engine calls (C++, native) of a scenario's routines-on registry.
std::pair<std::uint64_t,std::uint64_t> engine_calls(menu_probe::Scenario& s) {
    std::uint64_t cpp=0, fallbacks=0;
    for (const auto& [entry,routine]:s.routines().entries())
        for (const auto& range:menu::engine_ranges)
            if (entry.bank==range.bank && entry.address>=range.first && entry.address<=range.last) {
                cpp+=routine.counters.cpp_calls; fallbacks+=routine.counters.fallback_calls;
            }
    return {cpp,fallbacks};
}
// Probes at a real frame of three recordings. On field-menu the scenario then
// goes on with edge money values poked (0, 10000, 65535, 300 recording frames
// each), routines on beside routines off compared every frame: the money and
// status windows the recording opens are drawn with them.
bool real_frames(const menu_probe::Inputs& inputs) {
    struct Stop { std::string stem, what; std::function<bool(const Machine&)> stop; };
    const auto& rom=inputs.rom;
    std::vector<Stop> stops{{"opening","an open dialogue",menu_probe::on_dialogue}};
    if (rom.version==RomVersion::us_rev_a) {
        stops.push_back({"field-menu","the field menu",[&rom](const Machine& m) { return menu_probe::on_field_menu(rom,m); }});
        stops.push_back({"places","a choice in a dialogue",[](const Machine& m) { return menu_probe::on_selection(m) && menu_probe::on_dialogue(m); }});
    } else stops.push_back({"first-battle","the field menu",[&rom](const Machine& m) { return menu_probe::on_field_menu(rom,m); }});
    bool all=true;
    for (const auto& stop:stops) {
        const auto rec=recording(inputs,stop.stem);
        if (!rec) { std::cout<<"real-frame probes on "<<stop.stem<<": start image or recordings missing, skipped.\n"; all=false; continue; }
        menu_probe::Scenario s(rom,rec->second,stop.stem);
        std::uint64_t from=0;
        check(s.play(rec->first,from,stop.stop),stop.stem+" reaches "+stop.what);
        const auto count=frame_probes(inputs,s.machine());
        std::cout<<"real-frame probes on "<<stop.stem<<" at "<<stop.what<<" (frame "<<s.frame_count()<<"): "<<count<<" calls in C++.\n";
        if (stop.stem!="field-menu") continue;
        const auto before=engine_calls(s);
        const auto long_before=s.counters(at0(0x3037)).cpp_calls;
        for (const std::uint16_t value:{std::uint16_t{0},std::uint16_t{10000},std::uint16_t{65535}}) {
            s.poke_word(menu::money,value);
            std::uint64_t frames=0;
            s.play(rec->first,from,[&](const Machine&) { return ++frames>=300; });
        }
        const auto after=engine_calls(s);
        std::cout<<"money scenario on field-menu (0, 10000, 65535 poked, to frame "<<s.frame_count()<<"): routines on and off equal every frame; "
                 <<"engine calls "<<after.first-before.first<<" in C++ (00:3037 "<<s.counters(at0(0x3037)).cpp_calls-long_before<<"), "
                 <<after.second-before.second<<" native.\n";
    }
    return all;
}
}

int main(int argc, char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        catalog();
        if (argc==1) return 0;
        std::filesystem::path work;
        const auto inputs=menu_probe::load(argc,argv,&work);
        if (!inputs) return 77;
        const auto start=std::chrono::steady_clock::now();
        auto seconds=[&] { return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(); };
        code_shape(inputs->rom);
        number_printers(inputs->rom);
        empty_shop_line(inputs->rom);
        power_on_probes(inputs->rom);
        std::cout<<"  ("<<seconds()<<" s)\n";
        window_size_probes(inputs->rom);
        printer_boundary_probes(inputs->rom);
        std::cout<<"  ("<<seconds()<<" s)\n";
        bool skipped=!real_frames(*inputs);
        std::cout<<"  ("<<seconds()<<" s)\n";
        return skipped ? 77 : 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
