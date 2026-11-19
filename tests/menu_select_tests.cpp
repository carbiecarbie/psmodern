// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 12, part select: the registration catalog and the probes.
//   no arguments   the part's registration against its plan (every entry
//                  planned with its unit, or inside the part's ranges with a
//                  unit of the part; never a native instruction); the exits
//                  and permissions of the 4S table; no ROM
//   <rom> <us|br> <recordings folder> [<reference folder> [<work folder>]]
//                  the part's state and scenario probes on that ROM (exit 77
//                  without the ROM; a missing reference folder skips what needs
//                  it with a message and returns 77 at the end)
//
// The ROM run has four parts:
//   - the code shape the segments rely on (the confirm and direction masks,
//     the cancel point's instruction, every frame wait a bare call), read
//     from the image;
//   - state probes from power-on: every one of the 25 entries with inputs
//     biased towards each branch (buttons, page words, counts, the blink
//     state), every stack placement and slot-2 mapping, every call verified
//     against the original and required to cost exactly its bound; every
//     declared exit must be taken;
//   - scenario probes: the field menu, its item list and a yes-or-no window
//     of a real recording, driven by pad inputs that no recording presses
//     (wrapping the choice, paging, cancelling), routines on beside routines
//     off, the whole machine compared every frame;
//   - the same entries over the RAM and VRAM of those real frames.
#include "game/menu_kit.h"
#include "menu_probe.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
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

// ---- Registration catalog --------------------------------------------------------
SegmentExit R(std::uint16_t address, std::int8_t stack=0) { return kit::returned_at(address,stack); }
SegmentExit F(std::uint16_t address, std::int8_t stack=0) { return kit::core_final_at(address,stack); }
SegmentExit X(std::uint16_t address) { return kit::conditional_at(address); }

struct Want {
    RoutineEntry entry, unit;
    std::vector<SegmentExit> exits;
    kit::Permissions permissions={};
};
std::vector<Want> wanted() {
    return {
        {at0(0x015F),menu::fade_frame_handler_unit,{F(0x0171)}},
        {at0(0x017A),menu::fade_frame_handler_unit,{F(0x0183)},{.scratch_stack=2}},
        {at0(0x01AD),menu::menu_frame_handler_unit,{F(0x01BF)}},
        {at0(0x01D1),menu::text_frame_handler_unit,{F(0x01E3)}},
        {at0(0x2D19),menu::yes_no_unit,{F(0x2D1A,-2)},{.scratch_stack=2}},
        {at0(0x2D1D),menu::yes_no_unit,{F(0x2D1E,-2)},{.scratch_stack=2}},
        {at0(0x2D21),menu::yes_no_unit,{R(0x2D24,+4)},{.inherited_stack=4}},
        {at0(0x2D25),menu::wait_button_unit,{F(0x2D27)}},
        {at0(0x2D2A),menu::wait_button_unit,{F(0x2D27),R(0x2D32)}},
        {at0(0x2D33),menu::wait_short_unit,{F(0x2D3B)}},
        {at0(0x2D37),menu::wait_long_unit,{F(0x2D3B)}},
        {at0(0x2D3E),menu::wait_long_unit,{F(0x2D3B),R(0x2D43),R(0x2D46)}},
        {at0(0x2D47),menu::wait_fixed_unit,{F(0x2D4B)}},
        {at0(0x2D49),menu::wait_frames_unit,{F(0x2D4B)}},
        {at0(0x2D4E),menu::wait_frames_unit,{F(0x2D4B),R(0x2D50)}},
        {at0(0x2D51),menu::selection_unit,{F(0x2D62)}},
        {at0(0x2D60),menu::selection_unit,{F(0x2D62)}},
        {at0(0x2D65),menu::selection_unit,{X(0x2D90)}},
        {at0(0x2D94),menu::selection_unit,{F(0x2D9A)}},
        {at0(0x2D9D),menu::selection_unit,{R(0x2DA4)}},
        {at0(0x2DA5),menu::item_selection_unit,{F(0x2DAC)}},
        {at0(0x2DAF),menu::item_selection_unit,{F(0x2DAC),F(0x2E09),F(0x2E34)}},
        {at0(0x2E0C),menu::item_selection_unit,{R(0x2E12),R(0x2E1B),R(0x2E20),F(0x2E34)}},
        {at0(0x2E37),menu::item_selection_unit,{F(0x2DAC)}},
        {at0(0x2E62),menu::cursor_blink_unit,{R(0x2E66),R(0x2EAB)},{.scratch_stack=2}},
    };
}
void registration() {
    const auto entries=menu_probe::registered_entries(menu::Part::select,game::register_menu_select);
    RoutineRegistry registry;
    game::register_menu_select(registry);
    const auto want=wanted();
    check(want.size()==menu::select_segments.size() && entries.size()==want.size(),"the 25 planned select segments are registered");
    for (const auto& w:want) {
        const auto listed=std::find_if(menu::select_segments.begin(),menu::select_segments.end(),[&](const auto& s) { return s.entry==w.entry; });
        check(listed!=menu::select_segments.end() && listed->unit==w.unit,"a select entry is planned with its unit");
        const auto* routine=registry.find(w.entry);
        check(routine!=nullptr,"a planned select entry is registered");
        const auto& spec=routine->spec;
        check(spec.unit==w.unit && spec.slot2_mapper==w.permissions.slot2_mapper && spec.scratch_stack==w.permissions.scratch_stack &&
              spec.inherited_stack==w.permissions.inherited_stack && !spec.single_instruction,
              "select entry "+std::to_string(w.entry.address)+" has the unit and permissions of the 4S table");
        check(spec.exits.size()==w.exits.size(),"select entry "+std::to_string(w.entry.address)+" declares the exits of the 4S table");
        for (const auto& exit:w.exits)
            check(std::count_if(spec.exits.begin(),spec.exits.end(),[&](const auto& e) { return e.same_declaration(exit); })==1,
                  "an exit of the 4S table is declared exactly as written");
    }
    // The host-observed cancel point and the interrupt switches are never an entry or an exit.
    for (const auto& [entry,routine]:registry.entries()) {
        check(entry!=menu::menu_cancel_point && entry!=menu::save_observation_point,"no select entry at a host-observed instruction");
        for (const auto& exit:routine.spec.exits)
            check(!(exit.address==menu::menu_cancel_point.address),"no select exit at the host-observed cancel point");
    }
    std::cout<<entries.size()<<" select segments registered (all planned) with the exits and permissions of the 4S table.\n";
}

// ---- The ROM's code shape ------------------------------------------------------------------
void code_shape(const probe::Rom& rom) {
    auto op=[&](std::uint16_t a) { return rom.byte(0,a); };
    // Every frame wait the segments end on is a bare call of 00:0052.
    for (const std::uint16_t call:{0x2D27,0x2D3B,0x2D4B,0x2D62,0x2D9A,0x2DAC,0x2E09}) {
        check(op(call)==0xCD && op(call+1)==0x52 && op(call+2)==0x00,"a frame wait is a bare call of 00:0052");
        check(op(call-2)==0x3E && op(call-1)==0x08,"every frame wait is entered with A = 8");
    }
    // The buttons: up and down are bits 0 and 1, left and right 2 and 3, one and two 4 and 5.
    check(op(0x2D68)==0xE6 && op(0x2D69)==0x03,"the cursor loop masks up and down (bits 0 and 1)");
    check(op(0x2D8E)==0xE6 && op(0x2D8F)==0x30,"the cursor loop's confirm mask is bits 4 and 5");
    check(op(0x2D2D)==0xE6 && op(0x2D2E)==0x30 && op(0x2D41)==0xE6 && op(0x2D42)==0x30,"the waits' confirm mask is bits 4 and 5");
    check(op(0x2DB3)==0xE6 && op(0x2DB4)==0x03 && op(0x2DFF)==0xE6 && op(0x2E00)==0x30,"the item list's masks");
    check(op(0x2DE0)==0xCB && op(0x2DE1)==0x51 && op(0x2DEC)==0xCB && op(0x2DED)==0x59,"the item list pages on bits 2 and 3");
    // The cancel point is a one-byte LD C,A (the host watches it); the poll's last instruction is the JP Z in front of it.
    check(op(0x2D93)==0x4F,"the cancel point is ld c,a");
    check(op(0x2D90)==0xCA && op(0x2D91)==0x60 && op(0x2D92)==0x2D,"the poll ends on the jp z back to 00:2D60");
    // The blink: the game mode that uses the other tile pair, and the tile pairs.
    check(op(0x2E6A)==0xFE && op(0x2E6B)==0x03,"the blink looks for game mode 3");
    check(op(0x2E83)==0xCF && op(0x2E99)==0xCF && op(0x0180)==0xCF,"the blink and the fade palette use RST 08");
    check(op(0x2E6C)==0x01 && op(0x2E71)==0x01,"the blink loads its tile pairs with ld bc,nn");
    // The three handlers have the same shape: two scroll registers, then a bare call.
    for (const std::uint16_t base:{0x015F,0x01AD,0x01D1}) {
        check(op(base)==0x3A && op(base+3)==0xD3 && op(base+5)==0x3E && op(base+9)==0x3A && op(base+14)==0x3E && op(base+16)==0xD3 &&
              op(base+18)==0xCD,"a frame handler writes two scroll registers, then calls the sprite upload");
        check(op(base+4)==op(base+17),"the scroll registers go to one port");
    }
    std::cout<<"code shape: the waits, masks, cancel point, blink and handlers are as the segments render them.\n";
}

// ---- State probes -------------------------------------------------------------------------
// The exit each call of the harness took (milestone 14: from the registry).
struct Spy { std::optional<SegmentExit> last; };

using Prepare=std::function<void(Machine&,std::mt19937_64&)>;
struct Rig {
    probe::Harness harness;
    Spy spy;
    std::mt19937_64 random{0x2D51};
    std::uint64_t seed=1, count=0, refusals=0, unmapped=0;
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
        for (unsigned i=1;i<=4;++i) {
            const auto a=static_cast<std::uint16_t>(s.sp-i);
            if (a>=0xFFFC) return false;
            if (a>=0xC000) { if (s.work_ram_disabled) return false; }
            else if (a<0x8000 || !s.cartridge_ram) return false;
        }
        return true;
    }
    // One probe: admitted states run in C++ with the cost the bound announced
    // and match the original in the whole machine; a refused stack falls back
    // with unmapped_stack; a state the estimate cannot follow (an inline RST 08
    // whose return address does not read back) falls back with frame.
    void run(RoutineEntry entry, probe::Setup setup, const Machine* base, const Prepare& prepare) {
        setup.seed=seed++;
        auto machine=base ? map_probe::prepare_at(*base,entry,setup) : harness.prepare(entry,setup);
        auto& memory=machine.memory();
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()&~0x10);
        if (prepare) prepare(machine,random);
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
        const auto& routine=harness.routine(entry);
        if (!admitted(setup,routine.spec.scratch_stack,routine.spec.inherited_stack)) {
            harness.run_fallback(machine,entry,RoutineFallback::unmapped_stack);
            ++unmapped;
            return;
        }
        const auto bound=routine.bound(machine);
        if (bound>Machine::frame_cycles) {
            check(!stack_holds(setup) || setup.work_ram_disabled,"select entry "+std::to_string(entry.address)+" refused a state with a working stack");
            harness.run_fallback(machine,entry,RoutineFallback::frame);
            ++refusals;
            return;
        }
        spy.last.reset();
        probe::Harness::Result result{};
        try { result=harness.run_cpp(machine,entry); }
        catch (const std::exception& error) {
            std::ostringstream text;
            text<<error.what()<<" [entry "<<std::hex<<entry.address<<" seed "<<std::dec<<setup.seed<<" sp "<<std::hex<<setup.sp
                <<" ram_off "<<setup.work_ram_disabled<<" cart "<<setup.cartridge_ram<<" bank2 "<<+setup.banks[2]<<"]";
            throw std::runtime_error(text.str());
        }
        check(spy.last.has_value(),"a select probe took no exit");
        if (!setup.work_ram_disabled)
            check(result.cycles==bound,"select entry "+std::to_string(entry.address)+" cost "+std::to_string(result.cycles)+
                  " cycles, its bound said "+std::to_string(bound));
        ++count; ++per_entry[entry];
        reached[entry].insert(spy.last->address);
    }
};

std::uint8_t byte(std::mt19937_64& g) { return static_cast<std::uint8_t>(g()); }
bool one_in(std::mt19937_64& g, unsigned n) { return g()%n==0; }
void poke(Machine& m, std::uint16_t address, std::uint8_t value) { m.memory().write(address,value); }
void poke_word(Machine& m, std::uint16_t address, std::uint16_t value) {
    poke(m,address,static_cast<std::uint8_t>(value)); poke(m,static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
}
// Pressed buttons: none, each direction, each page key, confirm, cancel, mixes, anything.
std::uint8_t buttons(std::mt19937_64& g) {
    static constexpr std::uint8_t set[]{0,0,1,2,3,4,8,0x0C,0x10,0x20,0x30,0x11,0x22,0x14,0x18,0x05,0x0A,0x31};
    return one_in(g,5) ? byte(g) : set[g()%std::size(set)];
}
// A small count biased to its edges: 0, 1, the page size, one over, anything.
std::uint8_t small(std::mt19937_64& g) {
    static constexpr std::uint8_t set[]{0,1,2,6,7,8,9,10,16,17,0xFF};
    return one_in(g,4) ? byte(g) : set[g()%std::size(set)];
}

// The inputs each entry reads, randomized; registers the code sets itself are
// left to the seed.
std::vector<std::pair<RoutineEntry,Prepare>> entry_inputs() {
    std::vector<std::pair<RoutineEntry,Prepare>> list;
    auto add=[&](RoutineEntry e, Prepare p) { list.emplace_back(e,std::move(p)); };
    auto scroll=[](Machine& m, std::mt19937_64& g) { for (unsigned a=0xC300;a<=0xC306;++a) poke(m,static_cast<std::uint16_t>(a),byte(g)); };
    for (const std::uint16_t handler:{0x015F,0x01AD,0x01D1}) add(at0(handler),scroll);
    add(at0(0x017A),[](Machine& m, std::mt19937_64& g) { for (unsigned a=0xC220;a<=0xC23F;++a) poke(m,static_cast<std::uint16_t>(a),byte(g)); });
    for (const std::uint16_t entry:{0x2D19,0x2D1D,0x2D21,0x2D25,0x2D33,0x2D37,0x2D47,0x2D49,0x2D60,0x2D51,0x2DA5}) add(at0(entry),{});
    add(at0(0x2D2A),[](Machine& m, std::mt19937_64& g) { poke(m,menu::pressed_buttons,buttons(g)); });
    add(at0(0x2D3E),[](Machine& m, std::mt19937_64& g) {
        poke(m,menu::pressed_buttons,buttons(g));
        m.cpu().bc.uint8_values.at_1=one_in(g,3) ? static_cast<std::uint8_t>(1+g()%2) : byte(g);
    });
    add(at0(0x2D4E),[](Machine& m, std::mt19937_64& g) { m.cpu().bc.uint8_values.at_1=one_in(g,3) ? static_cast<std::uint8_t>(1+g()%2) : byte(g); });
    add(at0(0x2D65),[](Machine& m, std::mt19937_64& g) {
        poke(m,menu::pressed_buttons,buttons(g));
        poke(m,menu::option_count,small(g)); poke(m,menu::choice,one_in(g,3) ? byte(g) : static_cast<std::uint8_t>(g()%4));
    });
    for (const std::uint16_t entry:{0x2D94,0x2D9D})
        add(at0(entry),[](Machine& m, std::mt19937_64& g) { poke(m,menu::choice,byte(g)); poke(m,menu::selection_active,byte(g)); });
    add(at0(0x2DAF),[](Machine& m, std::mt19937_64& g) {
        poke(m,menu::pressed_buttons,buttons(g));
        poke(m,menu::option_count,small(g)); poke(m,menu::choice,one_in(g,3) ? byte(g) : static_cast<std::uint8_t>(g()%9));
        poke(m,menu::item_page,one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(8*(g()%3)));
        poke(m,menu::item_page+1,one_in(g,3) ? 0 : one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(8*(g()%4)));
    });
    add(at0(0x2E0C),[](Machine& m, std::mt19937_64& g) {
        m.cpu().bc.uint8_values.at_0=buttons(g);
        poke(m,menu::inventory_count,small(g)); poke(m,menu::choice,one_in(g,3) ? byte(g) : static_cast<std::uint8_t>(g()%3));
        poke(m,menu::item_page,one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(8*(g()%3)));
        poke(m,menu::item_page+1,one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(8*(g()%4)));
    });
    add(at0(0x2E37),[](Machine& m, std::mt19937_64& g) {
        poke(m,menu::inventory_count,small(g)); poke(m,menu::choice,one_in(g,3) ? byte(g) : static_cast<std::uint8_t>(g()%9));
        poke(m,static_cast<std::uint16_t>(menu::choice+1),one_in(g,2) ? byte(g) : 0);
        poke(m,menu::item_page,one_in(g,4) ? byte(g) : static_cast<std::uint8_t>(8*(g()%3)));
        poke(m,menu::item_page+1,byte(g));
    });
    add(at0(0x2E62),[](Machine& m, std::mt19937_64& g) {
        poke(m,menu::selection_active,one_in(g,4) ? 0 : one_in(g,2) ? 0xFF : byte(g));
        poke(m,menu::game_mode,one_in(g,2) ? 3 : one_in(g,2) ? 9 : byte(g));
        poke_word(m,menu::cursor_address,static_cast<std::uint16_t>(0x3800+(g()%0x200)*2));
        poke(m,menu::choice,static_cast<std::uint8_t>(g()%16)); poke(m,static_cast<std::uint16_t>(menu::choice+1),static_cast<std::uint8_t>(g()%16));
        poke(m,menu::blink_phase,byte(g));
    });
    return list;
}

// Every declared exit taken somewhere (the state probes can reach all of them).
void exits_reached(const Rig& g) {
    for (const auto& w:wanted())
        for (const auto& exit:w.exits) {
            const auto found=g.reached.find(w.entry);
            check(found!=g.reached.end() && found->second.count(exit.address),
                  "select entry "+std::to_string(w.entry.address)+" never took its exit "+std::to_string(exit.address));
        }
    for (const auto& w:wanted()) check(g.per_entry.count(w.entry)!=0,"a select entry was never run in C++");
}

void power_on_probes(const probe::Rom& rom) {
    Rig g(rom);
    const auto list=entry_inputs();
    for (const auto& [entry,prepare]:list) {
        for (unsigned i=0;i<300;++i) {
            probe::Setup setup;
            setup.iff1=setup.iff2=(i&1);
            setup.r=byte(g.random);
            setup.banks[2]=static_cast<std::uint8_t>(i%3==0 ? g.random()%32 : 3);
            g.run(entry,setup,nullptr,prepare);
        }
        for (const auto& c:probe::stack_cases()) {
            probe::Setup setup;
            setup.sp=c.sp; setup.cartridge_ram=c.cartridge_ram; setup.work_ram_disabled=c.work_ram_disabled;
            g.run(entry,setup,nullptr,prepare);
        }
        for (const auto& setup:probe::slot2_variants({})) g.run(entry,setup,nullptr,prepare);
        for (const auto r:probe::r_wrap_values(40)) { probe::Setup setup; setup.r=r; g.run(entry,setup,nullptr,prepare); }
    }
    exits_reached(g);
    // Interrupt sweeps (milestone 14): an IRQ and an NMI at every cycle of every segment (all have more
    // than one instruction), from inputs biased to the paths the probes above found.
    for (const auto& [entry,prepare]:list)
        for (unsigned variant=0;variant<2;++variant) {
            probe::Setup setup; setup.seed=7000+variant;
            std::mt19937_64 random(0x2D51u+variant*977u+entry.address);
            probe::sweep(g.harness,entry,setup,[&](Machine& m) { if (prepare) prepare(m,random); },variant ? 3 : 1);
        }
    std::cout<<"power-on probes: "<<g.count<<" calls in C++, "<<g.refusals<<" refused states (frame), "<<g.unmapped
             <<" refused stacks; every declared exit taken.\n";
}

// ---- Scenario probes ---------------------------------------------------------------------------
FrameInput pad(bool up=false, bool down=false, bool left=false, bool right=false, bool one=false, bool two=false) {
    FrameInput input;
    input.pads[0]={up,down,left,right,one,two};
    return input;
}
// The first recording (field-menu first) whose play reaches a frame where
// `stop` holds; the Scenario is built on its start image.
struct Found {
    std::string stem;
    std::unique_ptr<menu_probe::Scenario> scenario;
    std::uint64_t from=0;
    bool missing_image=false;
};
Found reach(const menu_probe::Inputs& inputs, const std::string& name, const std::function<bool(const Machine&)>& stop,
            std::vector<std::string> stems={"field-menu","places","later-town","first-dungeon","first-battle","boss","opening","deep-dungeon"}) {
    Found found;
    for (const auto& stem:stems) {
        if (!std::filesystem::exists(inputs.recording(stem))) continue;
        const auto recording=replay::read(inputs.recording(stem));
        check(recording.rom_sha1==inputs.rom.sha1,stem+" was recorded with another ROM");
        const auto image=menu_probe::start_image(inputs,stem,recording);
        if (!image) { found.missing_image=true; return found; }
        auto scenario=std::make_unique<menu_probe::Scenario>(inputs.rom,*image,name+"-"+stem);
        std::uint64_t from=0;
        if (scenario->play(recording,from,stop)) { found.stem=stem; found.scenario=std::move(scenario); found.from=from; return found; }
    }
    return found;
}

// A running selection of a recording (the field menu where there is one), then
// pad inputs no recording presses: down past the last option (wrap), up past
// the first (wrap), both at once, a cancel, with routines on beside routines off.
bool selection_scenarios(Found& found) {
    if (found.missing_image) { std::cout<<"selection scenarios: start image missing (reference folder not configured), skipped.\n"; return false; }
    if (!found.scenario) { std::cout<<"selection scenarios: no recording reaches a selection, skipped.\n"; return false; }
    auto& s=*found.scenario;
    s.frames(pad(),4);
    const unsigned count=s.peek(menu::option_count);
    check(s.peek(menu::selection_active)!=0,"the selection is still running");
    const unsigned first=s.peek(menu::choice);
    for (unsigned i=0;i<count+3;++i) { s.frame(pad(false,true)); s.frame(pad()); check(s.peek(menu::choice)==(first+i+1)%(count+1u),"Down moves the choice and wraps"); }
    for (unsigned i=0;i<count+3;++i) { s.frame(pad(true)); s.frame(pad()); }
    check(s.peek(menu::choice)==first,"Up undoes Down, wrapping");
    s.frame(pad(true,true)); s.frame(pad());
    check(s.peek(menu::choice)==first,"Up and Down together cancel out");
    s.frames(pad(),40);
    const auto blink=s.counters(at0(0x2E62)), handler=s.counters(at0(0x01AD)), poll=s.counters(at0(0x2D65));
    check(blink.cpp_calls>0 && handler.cpp_calls>0 && poll.cpp_calls>0,"the selection ran the blink, the handler and the poll in C++");
    std::cout<<"selection scenarios ("<<found.stem<<", "<<count+1<<" options): choice wrapped both ways; blink "<<blink.cpp_calls<<"/"<<blink.cpp_calls+blink.fallback_calls
             <<", handler "<<handler.cpp_calls<<"/"<<handler.cpp_calls+handler.fallback_calls<<", poll "<<poll.cpp_calls<<"/"<<poll.cpp_calls+poll.fallback_calls
             <<" in C++ (machine equal with routines on and off every frame).\n";
    s.frame(pad(false,false,false,false,false,true)); s.frames(pad(),12);   // cancel (button 2)
    const auto cancel=s.counters(menu::menu_cancel_point),end=s.counters(at0(0x2D9D));
    check(cancel.cpp_calls>0 && end.cpp_calls>0,"cancel did not execute the observed selection exit and its return in C++");
    return true;
}

// The item list: more items than a page, paging both ways, a confirm. Only
// recordings that open the item list (the US field-menu one).
bool item_list_scenarios(const menu_probe::Inputs& inputs) {
    auto found=reach(inputs,"select-items",[&](const Machine& m) { return menu_probe::on_item_list(inputs.rom,m); },{"field-menu"});
    if (found.missing_image) return false;
    if (!found.scenario) { std::cout<<"item list scenarios: no recording of this ROM opens the item list (the state probes and the checkpoint runs cover it).\n"; return true; }
    auto& s=*found.scenario;
    s.poke(menu::inventory_count,20);
    s.poke_word(menu::item_page,static_cast<std::uint16_t>(0x1000));
    s.frames(pad(),3);
    for (unsigned i=0;i<4;++i) { s.frame(pad(false,false,false,true)); s.frames(pad(),4); }   // Right: 8, 16, wrap to 0
    for (unsigned i=0;i<4;++i) { s.frame(pad(false,false,true)); s.frames(pad(),4); }         // Left: 16, 8, 0, wrap
    for (unsigned i=0;i<3;++i) { s.frame(pad(false,true)); s.frames(pad(),2); }               // Down
    s.frame(pad(true)); s.frames(pad(),2);
    const auto paged=s.counters(at0(0x2E37));
    check(paged.cpp_calls+paged.fallback_calls>0,"the page change recounts the options (00:2E37)");
    s.frame(pad(false,false,false,false,true)); s.frames(pad(),12);   // button 1 confirms
    const auto finish=s.counters(at0(0x2E0C));
    check(finish.cpp_calls+finish.fallback_calls>0,"the confirm reaches 00:2E0C");
    std::cout<<"item list scenarios: pages 0-8-16 both ways, 00:2E37 "<<paged.cpp_calls<<"/"<<paged.cpp_calls+paged.fallback_calls
             <<", 00:2E0C "<<finish.cpp_calls<<"/"<<finish.cpp_calls+finish.fallback_calls<<" in C++.\n";
    return true;
}

// The same entries over the RAM and VRAM of a real frame with a selection running.
bool real_frame_probes(const menu_probe::Inputs& inputs, Found& found) {
    if (!found.scenario) { std::cout<<"real frame probes: no selection frame available, skipped.\n"; return false; }
    auto& s=*found.scenario;
    s.frames(pad(),5);
    Rig g(inputs.rom);
    const auto list=entry_inputs();
    for (const auto& [entry,prepare]:list)
        for (unsigned i=0;i<40;++i) {
            probe::Setup setup;
            setup.banks={0,1,static_cast<std::uint8_t>(s.machine().memory().mapping(0x8000).bank)};
            g.run(entry,setup,&s.machine(),i%2 ? prepare : Prepare{});
        }
    std::cout<<"real frame probes ("<<found.stem<<", frame "<<s.frame_count()<<"): "<<g.count<<" calls in C++.\n";
    return true;
}
}

int main(int argc, char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        registration();
        if (argc==1) return 0;
        std::filesystem::path work;
        const auto inputs=menu_probe::load(argc,argv,&work);
        if (!inputs) return 77;
        const auto start=std::chrono::steady_clock::now();
        auto seconds=[&] { return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(); };
        code_shape(inputs->rom);
        power_on_probes(inputs->rom);
        std::cout<<"  ("<<seconds()<<" s)\n";
        bool skipped=false;
        if (inputs->recordings.empty()) { std::cout<<"scenario probes: no recordings folder, skipped.\n"; skipped=true; }
        else {
            auto found=reach(*inputs,"select",menu_probe::on_selection);
            if (found.missing_image) { std::cout<<"scenario probes: start image missing (reference folder not configured), skipped.\n"; skipped=true; }
            else {
                skipped=!selection_scenarios(found) || skipped;
                skipped=!real_frame_probes(*inputs,found) || skipped;
                skipped=!item_list_scenarios(*inputs) || skipped;
            }
        }
        std::cout<<"  ("<<seconds()<<" s)\n";
        return skipped ? 77 : 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
