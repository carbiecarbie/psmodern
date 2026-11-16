// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 12, part field: the registration catalog and the probes.
//   no arguments   the part's registration against its plan (every entry
//                  planned with its unit, or inside the part's ranges with a
//                  unit of the part; never a native instruction), each spec's
//                  exits and permissions as the plan gives them; no ROM
//   <rom> <us|br> <recordings folder> [<reference folder> [<work folder>]]
//                  the part's state and scenario probes on that ROM (exit 77
//                  without the ROM; a missing reference folder skips what needs
//                  it with a message and returns 77 at the end)
//
// The probes. Every entry of the part is started in many machine states and
// dispatched once with the per-call verifier (the original runs first, the
// whole machines are compared, the cycles must be the original's), and a probe
// that falls back verifies nothing, so every probe must run in C++.
//   - Random states. The work-RAM bytes the part reads (found by scanning the
//     part's own code in the ROM for absolute operands and table bases), the
//     party and inventory records, the registers, the flags and the words the
//     entry inherits on the stack are set to values drawn from the constants
//     the code itself compares with, so that every comparison is met on both
//     sides. The original is also stepped on a copy to see which instructions
//     it ran: the part reports how many of its instructions the probes ran and
//     the test requires the ones the field-menu recording does not reach (the
//     plan's list of what the recordings miss).
//   - Directed states for paths a draw needs several coincidences for (the two
//     fixed search places, the chest reward).
//   - The usual variations: the slot-2 mapping, the stack placement (with the
//     fallback it must cause), the refresh counter around its wrap, interrupts
//     enabled, and a start at every distance from the frame boundary (the
//     segment falls back exactly when its bound does not fit).
//   - Real frames: the same random states on a machine taken from the
//     field-menu frame of a recording (a real party, items and windows), and
//     whole recordings played with the inputs the part reads changed between
//     frames, routines on beside routines off.
#include "game/menu_kit.h"
#include "menu_probe.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

using namespace psmodern;
using probe::check;
namespace menu=game::menu;
namespace {
// ---- Registration catalog ------------------------------------------------------------
struct Expected { std::uint16_t entry, unit; std::vector<SegmentExit> exits; unsigned scratch, inherited; bool mapper; };
std::vector<Expected> expected_catalog() {
    using game::menu_kit::returned_at; using game::menu_kit::core_final_at; using game::menu_kit::conditional_at;
    return {
        {0x1BE1,0x1BE1,{core_final_at(0x1BE8)},0,0,false},
        {0x1BEE,0x1BE1,{core_final_at(0x1BFF),core_final_at(0x1C23,-2)},2,0,false},
        {0x1C02,0x1BE1,{core_final_at(0x1C0A),core_final_at(0x1C23,-2)},2,0,false},
        {0x1C29,0x1BE1,{returned_at(0x1C2C,2),returned_at(0x1C2F,2),core_final_at(0x1C35,2),core_final_at(0x1C3C,2),core_final_at(0x1C4B,2),conditional_at(0x1C5B,2)},0,2,false},
        {0x1C51,0x1BE1,{core_final_at(0x1BE8)},0,0,false},
        {0x1C5E,0x1BE1,{core_final_at(0x1C79)},0,0,false},
        {0x1CA4,0x1CA1,{core_final_at(0x1CA8),core_final_at(0x1CDC)},0,0,false},
        {0x1CAB,0x1CA1,{core_final_at(0x1CAE,-2),core_final_at(0x1CDC)},2,0,false},
        {0x1CB7,0x1CA1,{core_final_at(0x1CCD,2),core_final_at(0x1CD6,2)},0,2,false},
        {0x1CDF,0x1CDF,{core_final_at(0x1CE2)},0,0,false},
        {0x1CE8,0x1CDF,{core_final_at(0x1CEB)},0,0,false},
        {0x1CF1,0x1CDF,{core_final_at(0x1CFC),core_final_at(0x1D3B)},0,0,false},
        {0x1CFF,0x1CDF,{core_final_at(0x1D0B,-2)},2,0,false},
        {0x1D33,0x1CDF,{core_final_at(0x1D38),returned_at(0x1D4C,4)},0,4,false},
        {0x1D50,0x1D4D,{core_final_at(0x1D55),core_final_at(0x1DBA)},0,0,false},
        {0x1D58,0x1D4D,{core_final_at(0x1D81,-4),core_final_at(0x1DBA),core_final_at(0x1DC8)},4,0,false},
        {0x1D84,0x1D4D,{core_final_at(0x1D90,2)},0,2,false},
        {0x1D93,0x1D4D,{core_final_at(0x1DAA,2),core_final_at(0x1DB7,2)},0,2,false},
        {0x1DAD,0x1D4D,{core_final_at(0x1DB4),core_final_at(0x1DD4)},0,0,false},
        {0x1DDA,0x1D4D,{core_final_at(0x1DB7)},0,0,false},
        {0x2168,0x2168,{core_final_at(0x216F),core_final_at(0x2175)},0,0,false},
        {0x2178,0x2168,{core_final_at(0x219D,-2),core_final_at(0x21A4,-2),core_final_at(0x21C8),core_final_at(0x21F5)},2,0,false},
        {0x21A0,0x2168,{core_final_at(0x21B8,2)},0,2,false},
        {0x21A7,0x2168,{core_final_at(0x21B8,2)},0,2,false},
        {0x21CB,0x2168,{core_final_at(0x21C8),core_final_at(0x21D6),core_final_at(0x21F5)},0,0,false},
        {0x21D9,0x2168,{core_final_at(0x21E4)},0,0,false},
        {0x21E7,0x2168,{core_final_at(0x21EF),core_final_at(0x21F2)},0,0,false},
        {0x2201,0x2201,{core_final_at(0x2207)},0,0,false},
        {0x26C8,0x26C8,{core_final_at(0x26E5),core_final_at(0x26EE,-4)},4,0,true},
        {0x26F1,0x26C8,{core_final_at(0x26F8,4),core_final_at(0x2741,4)},0,4,false},
        {0x26FB,0x26C8,{core_final_at(0x272A,-2),core_final_at(0x2741),core_final_at(0x274A)},2,0,false},
        {0x272D,0x26C8,{core_final_at(0x2730)},0,0,false},
        {0x273C,0x26C8,{core_final_at(0x273E,2)},0,2,false},
        {0x2750,0x26C8,{core_final_at(0x2741)},0,0,false},
        {0x2752,0x2752,{core_final_at(0x276A),core_final_at(0x2773)},0,0,true},
        {0x277C,0x277C,{returned_at(0x279E)},2,0,false},
        {0x277F,0x277F,{returned_at(0x279E)},2,0,false},
        {0x279F,0x279F,{returned_at(0x27BB),core_final_at(0x27BF)},0,0,false},
        {0x27C5,0x279F,{core_final_at(0x27CA),core_final_at(0x27DF,-2)},2,0,false},
        {0x27D0,0x279F,{core_final_at(0x27D5),core_final_at(0x27DF,-2)},2,0,false},
        {0x27E8,0x279F,{core_final_at(0x27BF,2),core_final_at(0x2804),core_final_at(0x2811)},0,2,true},
        {0x2807,0x279F,{core_final_at(0x27DF)},0,2,false},
        {0x2814,0x279F,{core_final_at(0x2824,2)},0,2,false},
        {0x282E,0x282E,{returned_at(0x2834),returned_at(0x2838)},0,0,false},
        {0x2839,0x2839,{core_final_at(0x2840),core_final_at(0x28B4),conditional_at(0x28CA)},0,0,false},
        {0x2843,0x2839,{core_final_at(0x2846)},0,0,false},
        {0x28B7,0x2839,{core_final_at(0x28BC),conditional_at(0x28CA)},0,0,false},
        {0x28CD,0x2839,{conditional_at(0x28CF)},0,0,false},
        {0x28D2,0x2839,{core_final_at(0x28D5)},0,0,false},
        {0x28DB,0x28DB,{core_final_at(0x28DE)},0,0,false},
        {0x28E4,0x28DB,{core_final_at(0x28E5,-2)},2,0,false},
        {0x28EB,0x28DB,{returned_at(0x28ED,2),core_final_at(0x28FE)},0,2,false},
        {0x2901,0x28DB,{core_final_at(0x2907,2)},0,2,false},
        {0x290A,0x28DB,{core_final_at(0x291A),core_final_at(0x292B)},0,0,false},
        {0x291D,0x28DB,{core_final_at(0x2922)},0,0,false},
        {0x292E,0x28DB,{core_final_at(0x2933)},0,0,false},
        {0x2936,0x28DB,{core_final_at(0x2942),core_final_at(0x294D)},0,0,false},
        {0x2948,0x28DB,{core_final_at(0x294D)},0,0,false},
        {0x2950,0x2950,{core_final_at(0x295B),core_final_at(0x2961)},0,0,false},
        {0x295E,0x2950,{core_final_at(0x295B),returned_at(0x2960)},0,0,false},
        {0x2964,0x2950,{core_final_at(0x2966)},0,0,false},
        {0x2969,0x2966,{returned_at(0x2969),core_final_at(0x2971,-2)},2,0,false},
        {0x2974,0x2966,{returned_at(0x2979,2)},0,2,false},
    };
}
void catalog() {
    const auto entries=menu_probe::registered_entries(menu::Part::field,game::register_menu_field);
    std::cout<<entries.size()<<" of "<<menu::field_segments.size()<<" planned field segments registered.\n";
    check(entries.size()==menu::field_segments.size() && entries.size()==63,"all 63 planned field segments are registered, and nothing else");
    for (const auto& segment:menu::field_segments)
        check(std::find(entries.begin(),entries.end(),segment.entry)!=entries.end(),"a planned field segment is not registered");
    RoutineRegistry registry;
    game::register_menu_field(registry);
    const auto catalog=expected_catalog();
    check(catalog.size()==63,"the expected catalog lists 63 entries");
    for (const auto& expected:catalog) {
        const RoutineEntry entry{0,expected.entry};
        const auto* routine=registry.find(entry);
        check(routine!=nullptr,"a catalog entry is not registered");
        const auto& spec=routine->spec;
        check(spec.unit==RoutineEntry{0,expected.unit},"unit of a registered entry");
        check(spec.exits.size()==expected.exits.size(),"exit count of a registered entry");
        for (std::size_t i=0;i<expected.exits.size();++i)
            check(spec.exits[i].same_declaration(expected.exits[i]),"an exit of a registered entry is declared as planned");
        check(spec.scratch_stack==expected.scratch && spec.inherited_stack==expected.inherited && spec.slot2_mapper==expected.mapper,
              "the permissions of a registered entry");
        check(!spec.single_instruction && !spec.force_admit,"no field segment is single-instruction or forced");
    }
}

// ---- The lab ---------------------------------------------------------------------------------
// Work RAM the part names (the plan's RAM table); everything else the probes touch is found in the ROM.
constexpr std::uint16_t ram_item_slot=0xC29B, ram_done_flag=0xC2E1, ram_inventory=0xC4C0, ram_inventory_last=0xC4D7,
    ram_post_action=0xC2D8, ram_camera_x=0xC301, ram_camera_y=0xC305, ram_location=0xC309, ram_spot_a=0xC507,
    ram_spot_b=0xC509, ram_scene_argument=0xC2DB;
constexpr std::uint16_t sentinel=0x0180;   // the word under an entry's return address

// What the field-menu recording does not reach (m12-plan partR/unreached-after.txt): the first and last
// instruction of each missing run. The probes must run every one of them.
constexpr std::uint16_t must_run[]={0x1C34,0x1C35,0x1C3C,0x1C49,0x1C56,0x1CCC,0x1D7B,0x1DC0,0x1DC3,0x216F,
    0x21CE,0x21D0,0x27D2,0x27D5,0x285B,0x285D,0x2877,0x2892,0x289D,0x2917,0x2922,0x2957,0x2960};

void poke(Machine& m, std::uint16_t address, std::uint8_t value) { if (address<0xfffc) m.memory().write(address,value); }
void poke_word(Machine& m, std::uint16_t address, std::uint16_t value) {
    poke(m,address,static_cast<std::uint8_t>(value)); poke(m,static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
}
void set_a(Machine& m, std::uint8_t v) { m.cpu().af.uint8_values.at_1=v; }
void set_f(Machine& m, std::uint8_t v) { m.cpu().af.uint8_values.at_0=v; }
void set_b(Machine& m, std::uint8_t v) { m.cpu().bc.uint8_values.at_1=v; }
void set_c(Machine& m, std::uint8_t v) { m.cpu().bc.uint8_values.at_0=v; }
void set_d(Machine& m, std::uint8_t v) { m.cpu().de.uint8_values.at_1=v; }
void set_e(Machine& m, std::uint8_t v) { m.cpu().de.uint8_values.at_0=v; }
void set_hl(Machine& m, std::uint16_t v) { m.cpu().hl.uint16_value=v; }

struct Stats {
    std::uint64_t probes=0, fallbacks_checked=0;
    std::map<std::uint16_t,std::uint64_t> per_entry;
    std::set<std::uint16_t> visited;
};

class Lab {
public:
    using Directed=std::function<void(Machine&,std::mt19937_64&)>;
    Lab(const probe::Rom& rom, Stats& stats, std::uint64_t seed, const Machine* base=nullptr)
        : harness(rom), rom_(rom), stats_(stats), seed_(seed), base_(base) { scan(); }
    probe::Harness harness;
    std::uint64_t fallbacks_checked=0;

    // The state of one probe, deterministic in `seed`. `directed` runs last (it may set up a specific path).
    Machine build(RoutineEntry entry, probe::Setup setup, std::uint64_t seed, bool shaken, const Directed& directed={}) {
        std::mt19937_64 rng(seed*0x9e3779b97f4a7c15ull+seed_);
        setup.seed=seed;
        Machine m=base_ ? menu_probe::prepare_at(*base_,entry,setup) : harness.prepare(entry,setup);
        if (!shaken) return m;
        shake(m,rng);
        const auto& spec=harness.routine(entry).spec;
        const auto sp=m.cpu().sp.uint16_value;
        for (unsigned i=0;i<spec.inherited_stack;i+=2) {
            // The inherited words are registers an earlier segment pushed: pointers into the party records
            // (the one that is stored through must be one) or a value and flags.
            const bool pointer=entry.address==0x2974 || (rng()&1);
            poke_word(m,static_cast<std::uint16_t>(sp+i),pointer ? static_cast<std::uint16_t>(0xC400+rng()%0x100)
                      : static_cast<std::uint16_t>((pick(rng)<<8)|(rng()&0xFF)));
        }
        if (spec.inherited_stack) poke_word(m,static_cast<std::uint16_t>(sp+spec.inherited_stack),sentinel);
        registers(m,entry,rng);
        if (directed) directed(m,rng);
        return m;
    }
    // Runs the original on a copy up to the entry's first exit and records the instructions it ran.
    void trace(const Machine& start, RoutineEntry entry) {
        const auto& spec=harness.routine(entry).spec;
        Machine original=start;
        for (unsigned n=0;n<200000;++n) {
            const auto pc=original.cpu().pc.uint16_value;
            bool exit=false;
            if (n>0 || (rom_.byte(0,pc)&0xC7)==0xC0) for (const auto& e:spec.exits) exit=exit || e.address==pc;
            stats_.visited.insert(pc);
            if (exit) {
                const auto op=rom_.byte(0,pc);
                if ((op&0xC7)!=0xC0) return;                    // an exit that is not a conditional return
                const auto sp=original.cpu().sp.uint16_value;
                original.step();
                if (original.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp+2)) return;   // the return was taken
                continue;
            }
            original.step();
        }
        throw std::runtime_error("the original never left an entry of the field menu part");
    }
    // One probe: trace, then dispatch with the verifier; it must run in C++.
    probe::Harness::Result probe_state(Machine& m, RoutineEntry entry) {
        trace(m,entry);
        const auto result=harness.run_cpp(m,entry);
        ++stats_.probes; ++stats_.per_entry[entry.address];
        return result;
    }
    void random_states(RoutineEntry entry, unsigned count) {
        for (unsigned i=0;i<count;++i) {
            auto m=build(entry,{},i,true);
            try { probe_state(m,entry); }
            catch (const std::exception& error) {
                throw std::runtime_error("entry "+std::to_string(entry.address)+" random state "+std::to_string(i)+": "+error.what());
            }
        }
    }
    // The planned entries (or any entry given).
    std::vector<RoutineEntry> entries() const {
        std::vector<RoutineEntry> list;
        for (const auto& segment:menu::field_segments) list.push_back(segment.entry);
        return list;
    }
    std::uint8_t pick(std::mt19937_64& rng) const {
        if (rng()%10<7) return palette_[rng()%palette_.size()];
        return static_cast<std::uint8_t>(rng());
    }
    const std::vector<std::uint16_t>& inputs() const { return inputs_; }
    const std::vector<std::uint16_t>& bases() const { return bases_; }
    void shake(Machine& m, std::mt19937_64& rng) {
        for (const auto a:inputs_) if (rng()%100<55) poke(m,a,pick(rng));
        for (const auto base:bases_) for (unsigned k=0;k<64;++k) if (rng()%100<40) poke(m,static_cast<std::uint16_t>(base+k),pick(rng));
        poke_word(m,ram_item_slot,static_cast<std::uint16_t>(ram_inventory+rng()%24));
        poke_word(m,ram_done_flag,static_cast<std::uint16_t>(ram_inventory+rng()%24));
    }
    void registers(Machine& m, RoutineEntry entry, std::mt19937_64& rng) {
        set_a(m,pick(rng)); set_f(m,static_cast<std::uint8_t>(rng())); set_b(m,static_cast<std::uint8_t>(rng()%6));
        set_c(m,pick(rng)); set_d(m,pick(rng)); set_e(m,pick(rng));
        // 00:277F removes the item HL points at; no other entry stores through a register it is given.
        if (entry.address==0x277F || entry.address==0x277C) set_hl(m,static_cast<std::uint16_t>(ram_inventory+rng()%24));
        else set_hl(m,static_cast<std::uint16_t>(rng()));
    }
    const probe::Rom& rom() const { return rom_; }
private:
    // The absolute RAM addresses and table bases the part's own code names, and the constants it compares with.
    void scan() {
        std::set<std::uint16_t> inputs, bases; std::set<std::uint8_t> palette{0,1,2,3,4,5,0x80,0xFF};
        auto operand=[&](unsigned at) { return static_cast<std::uint16_t>(rom_.byte(0,at)|(rom_.byte(0,at+1)<<8)); };
        auto ram=[](std::uint16_t a) { return a>=0xC000 && a<0xDB00; };
        for (unsigned a=0x1BE1;a<0x297A;++a) {
            const auto op=rom_.byte(0,a);
            if ((op==0x3A || op==0x32 || op==0x2A || op==0x22) && ram(operand(a+1))) {
                inputs.insert(operand(a+1));
                if (op==0x2A || op==0x22) inputs.insert(static_cast<std::uint16_t>(operand(a+1)+1));
            }
            if (op==0xED && ((rom_.byte(0,a+1)&0xCF)==0x4B || (rom_.byte(0,a+1)&0xCF)==0x43) && ram(operand(a+2))) inputs.insert(operand(a+2));
            if ((op==0x01 || op==0x11 || op==0x21) && ram(operand(a+1))) bases.insert(operand(a+1));
            if (op==0xFE || op==0xE6 || op==0xD6 || op==0xC6 || op==0xF6 || op==0x3E || (op&0xC7)==0x06) {
                const auto v=rom_.byte(0,a+1);
                palette.insert(v); palette.insert(static_cast<std::uint8_t>(v+1)); palette.insert(static_cast<std::uint8_t>(v-1));
            }
        }
        inputs_.assign(inputs.begin(),inputs.end()); bases_.assign(bases.begin(),bases.end());
        palette_.assign(palette.begin(),palette.end());
    }
    probe::Rom rom_;
    Stats& stats_;
    std::uint64_t seed_;
    const Machine* base_;
    std::vector<std::uint16_t> inputs_, bases_;
    std::vector<std::uint8_t> palette_;
};

// ---- Probe families -----------------------------------------------------------------------------
// The variations every probe family is asked for: slot-2 mappings, stack placements, the refresh counter
// around its wrap, interrupts enabled, and every distance from the frame boundary.
void variations(Lab& lab, RoutineEntry entry, std::uint64_t seed) {
    auto& harness=lab.harness;
    const auto& spec=harness.routine(entry).spec;
    // Slot-2 mappings (the original's own banks, other ROM banks, cartridge RAM).
    for (const auto& setup:probe::slot2_variants({})) {
        auto m=lab.build(entry,setup,seed,true);
        lab.probe_state(m,entry);
    }
    // Stack placements; the ones the mechanism refuses must fall back for that reason.
    for (const auto& stack:probe::stack_cases()) {
        probe::Setup setup; setup.sp=stack.sp; setup.cartridge_ram=stack.cartridge_ram; setup.work_ram_disabled=stack.work_ram_disabled;
        auto m=lab.build(entry,setup,seed,false);
        if (!map_probe::admitted(m,spec)) { harness.run_fallback(m,entry,RoutineFallback::unmapped_stack); ++lab.fallbacks_checked; }
        else if (harness.routine(entry).bound(m)>Machine::frame_cycles) {
            // A state the estimate cannot follow (work RAM off, a stack that does not give back what was pushed)
            // runs natively: the bound is the two-frame maximum, so the frame window refuses it.
            check(stack.work_ram_disabled || stack.sp>=0xFFFC-4 || stack.sp<0xC000,"a field segment refused a state with a working stack");
            harness.run_fallback(m,entry,RoutineFallback::frame); ++lab.fallbacks_checked;
        } else lab.probe_state(m,entry);
    }
    // The refresh counter around its wrap, and interrupts enabled with none pending.
    auto pilot=lab.build(entry,{},seed,true);
    const auto fetches=lab.probe_state(pilot,entry).fetches;
    for (const auto r:probe::r_wrap_values(static_cast<unsigned>(fetches))) {
        probe::Setup setup; setup.r=r;
        auto m=lab.build(entry,setup,seed,true);
        lab.probe_state(m,entry);
    }
    for (const bool iff:{false,true}) {
        probe::Setup setup; setup.iff1=iff; setup.iff2=iff;
        auto m=lab.build(entry,setup,seed+1,true);
        lab.probe_state(m,entry);
    }
    // Every distance from the frame boundary near the segment's bound: it falls back (frame) exactly when the
    // bound does not fit in what is left.
    auto measure=lab.build(entry,{},seed+2,true);
    const auto cycles=lab.probe_state(measure,entry).cycles;
    for (const std::uint64_t remaining:{cycles-1,cycles,cycles+1,cycles+17}) {
        auto m=lab.build(entry,{},seed+2,true);
        menu_probe::place_before_boundary(m,remaining);
        if (remaining<cycles) { harness.run_fallback(m,entry,RoutineFallback::frame); ++lab.fallbacks_checked; }
        else lab.probe_state(m,entry);
    }
}

// ---- Entry ------------------------------------------------------------------------------------------
void report(const Stats& stats, const Lab& lab) {
    std::cout<<stats.probes<<" probes ("<<lab.fallbacks_checked<<" intended fallbacks checked), "
             <<stats.visited.size()<<" distinct instructions run by the original on the way\n";
    if (const char* dump=std::getenv("PSMODERN_FIELD_VISITED")) {
        std::ofstream out(dump);
        for (const auto a:stats.visited) out<<std::hex<<a<<'\n';
    }
    std::vector<std::uint16_t> missing;
    for (const auto a:must_run) if (!stats.visited.count(a)) missing.push_back(a);
    for (const auto a:missing) std::cout<<"  not run: "<<std::hex<<a<<std::dec<<'\n';
    check(missing.empty(),"the probes ran every instruction the recordings miss");
    // 561 instructions are rendered (the part's whole control-flow graph, exits included): the probes ran all of them.
    check(stats.visited.size()>=561,"the probes ran every instruction of the part");
}

// SEARCH (00:2839) gives an item at two fixed places of two locations; a draw rarely meets the location, both
// camera words and the flag together, so these states set them (the camera words are the ones the code turns into
// a map position; the values were found by running the arithmetic and are checked by the probe that runs them).
void search_places(Lab& lab) {
    const RoutineEntry entry{0,0x2839};
    const auto odin_shield=menu_probe::rom_word(lab.rom(),{0,0x28A9});
    for (unsigned variant=0;variant<48;++variant) {
        auto m=lab.build(entry,{},500+variant,true,[&](Machine& machine, std::mt19937_64& rng) {
            const bool second=variant&1;
            const unsigned k=variant>>1;
            poke(machine,0xC800,(rng()&1) ? 0x00 : 0x40);                    // the scene marker is not 0Eh
            if (!second) {
                poke(machine,ram_location,7); poke_word(machine,ram_camera_y,0x0220); poke_word(machine,ram_camera_x,0x0160);
                poke(machine,ram_spot_a,k%3==0 ? 1 : k%3==1 ? 0 : 0xFF);
            } else {
                poke(machine,ram_location,1); poke_word(machine,ram_camera_y,0x0260); poke_word(machine,ram_camera_x,0x0400);
                poke(machine,ram_spot_b,(k&1) ? 1 : 0); poke(machine,odin_shield,(k&2) ? 0x1F : 0x05);
            }
            if (k>=8) poke_word(machine,(k&4) ? ram_camera_x : ram_camera_y,static_cast<std::uint16_t>(rng()));   // near misses
        });
        lab.probe_state(m,entry);
    }
}

// ITEM (00:2168): using a vehicle item while that vehicle is the one the party is in (item 21h-23h, transport byte
// equal to 4 times the vehicle plus 4) takes the vehicle-parking branches; a draw needs the item, the transport byte
// and the button to agree.
void vehicle_items(Lab& lab) {
    const RoutineEntry entry{0,0x2178};
    for (unsigned variant=0;variant<36;++variant) {
        auto m=lab.build(entry,{},700+variant,true,[&](Machine& machine, std::mt19937_64& rng) {
            const unsigned vehicle=variant%3;                                  // item 21h + vehicle
            poke(machine,0xC2C4,static_cast<std::uint8_t>(0x21+vehicle));
            const auto wanted=static_cast<std::uint8_t>(vehicle*4+4);
            poke(machine,0xC30E,(variant/3)%4==3 ? static_cast<std::uint8_t>(rng()) : (variant/3)%4==2 ? 0 : wanted);
            set_c(machine,static_cast<std::uint8_t>(rng()&~0x10));          // button 4 not held: the item list was not cancelled
        });
        lab.probe_state(m,entry);
    }
}

// ---- Real frames ----------------------------------------------------------------------------------------
// The recordings that open the field menu (the new field-menu one, and the older ones that save or use items).
const char* const stems[]={"field-menu","first-dungeon"};

// The same random states on a machine taken from the field menu of a recording (a real party, items and windows),
// and the whole recording played from there with the party's records changed at every opening of the menu, routines
// on beside routines off.
void real_frames(const menu_probe::Inputs& inputs, Stats& stats, bool& skipped) {
    if (inputs.recordings.empty() || inputs.reference.empty()) {
        std::cout<<"real frames skipped: no recordings or reference folder given\n"; skipped=true; return;
    }
    unsigned used=0;
    for (const std::string stem:stems) {
        if (!std::filesystem::exists(inputs.recording(stem))) continue;
        const auto recording=replay::read(inputs.recording(stem));
        if (recording.rom_sha1!=inputs.rom.sha1) continue;                       // a recording of the other version
        const auto image=menu_probe::start_image(inputs,stem,recording);
        if (!image) { std::cout<<stem<<": start image not available, skipped\n"; skipped=true; continue; }
        const auto in_menu=[&](const Machine& m) { return menu_probe::on_field_menu(inputs.rom,m); };
        menu_probe::Scenario scenario(inputs.rom,*image,stem);
        std::uint64_t from=0;
        if (!scenario.play(recording,from,in_menu)) { std::cout<<stem<<": never opens the field menu\n"; continue; }
        ++used;
        std::cout<<stem<<": field menu at frame "<<from<<'\n';
        {   // states on the real machine
            Lab real(inputs.rom,stats,from+inputs.rom.sha1.size(),&scenario.machine());
            for (const auto entry:real.entries()) real.random_states(entry,120);
        }
        // the recording played on with changed records at every opening of the menu
        std::mt19937_64 rng(from*31+7);
        const auto party=std::vector<std::uint16_t>{0xC4E0,0xC4E1,0xC4E2};
        unsigned openings=1;
        const std::uint64_t end=from+1500;                       // frames of the recording played on with the changed records
        for (;;) {
            for (const auto a:party) scenario.poke(a,static_cast<std::uint8_t>(rng()%3==0 ? rng() : rng()%4));
            for (unsigned k=0;k<24;++k) scenario.poke(static_cast<std::uint16_t>(0xC4C0+k),static_cast<std::uint8_t>(rng()%5==0 ? 0 : rng()%0x40));
            for (unsigned member=0;member<4;++member)
                for (unsigned field=0;field<16;++field)
                    if (rng()%6==0) scenario.poke(static_cast<std::uint16_t>(0xC400+16*member+field),static_cast<std::uint8_t>(rng()%4==0 ? rng() : rng()%4));
            if (!scenario.play(recording,from,[&](const Machine& m) { return from>=end || !in_menu(m); }) || from>=end) break;
            if (!scenario.play(recording,from,[&](const Machine& m) { return from>=end || in_menu(m); }) || from>=end) break;
            ++openings;
        }
        std::uint64_t cpp=0, fallbacks=0;
        for (const auto& segment:menu::field_segments) { const auto c=scenario.counters(segment.entry); cpp+=c.cpp_calls; fallbacks+=c.fallback_calls; }
        std::cout<<stem<<": "<<openings<<" openings of the menu with changed records, "<<cpp<<" C++ and "<<fallbacks<<" fallback calls of the part\n";
    }
    if (!used) { std::cout<<"real frames skipped: no recording opens the field menu\n"; skipped=true; }
}

void probes(const menu_probe::Inputs& inputs, bool& skipped) {
    Stats stats;
    Lab lab(inputs.rom,stats,inputs.version=="us" ? 11 : 17);
    const unsigned randoms=1500;
    for (const auto entry:lab.entries()) lab.random_states(entry,randoms);
    for (const auto entry:lab.entries()) variations(lab,entry,1000+entry.address);
    search_places(lab);
    vehicle_items(lab);
    real_frames(inputs,stats,skipped);
    report(stats,lab);
}
}

int main(int argc, char** argv) {
    try {
        catalog();
        if (argc==1) return 0;
        std::filesystem::path work;
        const auto inputs=menu_probe::load(argc,argv,&work);
        if (!inputs) return 77;
        bool skipped=false;
        probes(*inputs,skipped);
        return skipped ? 77 : 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
