// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 11, part objects: the object engine and the map's object types.
//   no arguments                       registration catalog (no ROM)
//   <rom> <us|br> [<recordings folder> [<reference folder>]]
//                                      state probes from a power-on machine,
//                                      state and scenario probes from real
//                                      map frames (tests/map_probe.h)
// A ROM run exits 77 without the ROM (and after skipping a part that needs a
// start image that is not available).
//
// A state probe starts a power-on machine of the identified ROM at one entry,
// sets the state the callers leave (the slots, the move bits, the held words
// on the stack, the registers the caller passes) and dispatches the entry with
// the per-call verifier; it must run in C++ unless the machine's own rules
// refuse it (a stack on unmapped bytes or the mapper registers, a frame
// boundary inside the bound). Every declared exit that can be reached from an
// entry is taken at least once, and with work RAM on the cost equals the bound.
#include "game/map_kit.h"
#include "map_probe.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <set>

using namespace psmodern;
using probe::check;
namespace map=game::map;
namespace {
constexpr map::Part part=map::Part::objects;

// ---- Registration catalog --------------------------------------------------------
struct Expected { std::uint16_t entry, unit; std::vector<SegmentExit> exits; unsigned scratch, inherited; bool mapper; };
std::vector<Expected> expected_catalog() {
    using game::map_kit::returned_at; using game::map_kit::core_final_at; using game::map_kit::loop_at;
    return {
        {0x576A,0x576A,{core_final_at(0x577E)},0,0,false},
        {0x5781,0x576A,{core_final_at(0x579C,-2),core_final_at(0x5809,-4),returned_at(0x5817)},6,0,true},
        {0x579F,0x576A,{core_final_at(0x579C),core_final_at(0x5809,-2),returned_at(0x5817,2)},4,2,true},
        {0x580C,0x576A,{core_final_at(0x5809),returned_at(0x5817,4)},2,4,true},
        {0x5818,0x5818,{returned_at(0x5826)},2,0,false},
        {0x5853,0x5853,{loop_at(0x5868,128,-2),loop_at(0x587B,128),returned_at(0x588A)},2,0,false},
        {0x5869,0x5853,{loop_at(0x5868,128),loop_at(0x587B,128,2),returned_at(0x588A,2)},0,2,false},
        {0x587C,0x5853,{loop_at(0x587B,128),returned_at(0x588A)},0,0,false},
        {0x599F,0x599F,{core_final_at(0x59A2,-2)},2,0,false},
        {0x59A5,0x599F,{returned_at(0x59D3,2)},0,2,false},
        {0x59D4,0x59D4,{core_final_at(0x59DA)},0,0,false},
        {0x59DD,0x59D4,{returned_at(0x59DF)},0,0,false},
        {0x59E0,0x59E0,{core_final_at(0x59A2,-2)},2,0,false},
        {0x59E5,0x59E5,{core_final_at(0x59EB)},0,0,false},
        {0x59EE,0x59E5,{returned_at(0x59F0)},0,0,false},
        {0x59F1,0x59F1,{core_final_at(0x59A2,-2)},2,0,false},
        {0x59F6,0x59F6,{core_final_at(0x59FC)},0,0,false},
        {0x59FF,0x59F6,{returned_at(0x5A01)},0,0,false},
        {0x5A02,0x5A02,{core_final_at(0x59A2,-2)},2,0,false},
        {0x5A07,0x5A07,{core_final_at(0x5A0D)},0,0,false},
        {0x5A10,0x5A07,{returned_at(0x5A12)},0,0,false},
        {0x5A52,0x5A4F,{returned_at(0x5A6F)},0,0,false},
        {0x5A73,0x5A70,{returned_at(0x5A75)},0,0,false},
        {0x5A79,0x5A76,{returned_at(0x5A7E),returned_at(0x5A93)},0,0,false},
        {0x5A98,0x5A98,{core_final_at(0x5A9A)},0,0,false},
        {0x5A9D,0x5A98,{returned_at(0x5B2F),core_final_at(0x5B32),returned_at(0x5B3E),returned_at(0x5B5B)},0,0,false},
        {0x5B35,0x5A98,{returned_at(0x5B2F),returned_at(0x5B3E),returned_at(0x5B5B)},0,0,false},
        {0x5B5C,0x5B5C,{core_final_at(0x5B63)},0,0,false},
        {0x5B66,0x5B5C,{returned_at(0x5B7A),returned_at(0x5B86)},0,0,false},
        {0x5B6B,0x5B6B,{returned_at(0x5B7A),returned_at(0x5B86)},0,0,false},
    };
}
void registration() {
    const auto entries=map_probe::registered_entries(part,game::register_map_objects);
    // The planned list includes 01:5781 (after the cut prologue of 01:576A),
    // 01:5869 and 01:587C (loop resumes of 01:5853).
    check(entries.size()==30 && map::object_segments.size()==30,"the 30 planned objects segments are registered, and nothing else");
    for (const auto& planned:map::object_segments)
        check(std::find(entries.begin(),entries.end(),planned.entry)!=entries.end(),"a planned objects segment is not registered");
    RoutineRegistry registry;
    game::register_map_objects(registry);
    const auto catalog=expected_catalog();
    check(catalog.size()==30,"the expected catalog lists 30 entries");
    for (const auto& expected:catalog) {
        const RoutineEntry entry=map::at1(expected.entry);
        const auto* routine=registry.find(entry);
        check(routine!=nullptr,"a catalog entry is not registered");
        const auto& spec=routine->spec;
        const auto listed=std::find_if(map::object_segments.begin(),map::object_segments.end(),
                                       [&](const map::Segment& s) { return s.entry==entry; });
        if (listed!=map::object_segments.end()) check(spec.unit==listed->unit,"an entry belongs to the unit map.h names");
        check(spec.unit==map::at1(expected.unit),"an entry belongs to its unit");
        check(spec.slot2_mapper==expected.mapper && spec.scratch_stack==expected.scratch && spec.inherited_stack==expected.inherited &&
              !spec.single_instruction && !spec.force_admit && !spec.absolute_wram_store,"an entry declares the permissions of the design");
        check(spec.exits.size()==expected.exits.size(),"an entry declares the exits of the design");
        for (const auto& exit:expected.exits)
            check(std::any_of(spec.exits.begin(),spec.exits.end(),[&](const SegmentExit& e) { return e.same_declaration(exit); }),
                  "an entry's exit differs from the design (address, kind, stack, conditional or after)");
    }
    std::cout<<entries.size()<<" objects segments registered (all planned)."<<std::endl;
}

// ---- State probes ------------------------------------------------------------------
struct Stats {
    std::uint64_t calls=0, fallbacks=0;
    std::map<std::uint16_t,std::uint64_t> per_entry;
    std::map<std::uint16_t,std::set<std::uint16_t>> exits;   // addresses of the final instructions reached
};
using Builder=std::function<void(Machine&)>;
using map_probe::admitted;   // the machine dispatch's stack test, predicted (tests/map_probe.h)

// The probe counts below are for a full run; PSMODERN_PROBE_PERCENT scales them (a quick look while editing).
unsigned scaled(unsigned count) {
    static const unsigned percent=std::getenv("PSMODERN_PROBE_PERCENT") ? static_cast<unsigned>(std::atoi(std::getenv("PSMODERN_PROBE_PERCENT"))) : 100;
    return std::max(1u,count*percent/100);
}

class Lab {
public:
    Lab(probe::Rom rom, Stats& stats, std::uint64_t seed) : harness(std::move(rom)), stats_(stats), rng_(seed), seed_(seed) {}
    probe::Harness harness;

    static void poke(Machine& m, std::uint16_t address, std::uint8_t value) { if (address<0xfffc) m.memory().write(address,value); }
    static void poke_word(Machine& m, std::uint16_t address, std::uint16_t value) {
        poke(m,address,static_cast<std::uint8_t>(value)); poke(m,static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
    }
    static void lay_stack(Machine& m, std::initializer_list<std::uint16_t> words) {
        auto sp=m.cpu().sp.uint16_value;
        for (const auto word:words) { poke_word(m,sp,word); sp=static_cast<std::uint16_t>(sp+2); }
    }
    std::uint8_t byte() { return static_cast<std::uint8_t>(rng_()); }
    unsigned below(unsigned n) { return static_cast<unsigned>(rng_()%n); }
    static void set_iy(Machine& m, std::uint16_t v) { m.cpu().ix_iy[1].uint16_value=v; }
    static void set_bc(Machine& m, std::uint8_t b, std::uint8_t c) { m.cpu().bc.uint8_values.at_1=b; m.cpu().bc.uint8_values.at_0=c; }
    static void set_de(Machine& m, std::uint16_t v) { m.cpu().de.uint16_value=v; }
    static void set_hl(Machine& m, std::uint16_t v) { m.cpu().hl.uint16_value=v; }
    static void set_a(Machine& m, std::uint8_t v) { m.cpu().af.uint8_values.at_1=v; }

    // The next setup of a rotation through slot-2 mappings, refresh-counter
    // wraps, interrupt flags and stack places.
    probe::Setup next_setup() {
        const auto n=++serial_;
        probe::Setup s;
        s.seed=n*7919+seed_;
        s.iff1=(n&1); s.iff2=(n&2);
        switch (n%8) {
            case 4: s.banks[2]=0; break;
            case 5: { constexpr std::uint8_t other[3]{2,15,31}; s.banks[2]=other[(n/8)%3]; break; }
            case 6: s.banks[2]=3; s.cartridge_ram=true; break;
            default: s.banks[2]=3;
        }
        static const auto wraps=probe::r_wrap_values(70);
        s.r=(n%3==0) ? wraps[(n/3)%wraps.size()] : byte();
        s.sp=static_cast<std::uint16_t>(0xCC10+rng_()%0x1300);
        if (n%11==0 || forced_) {
            static const auto cases=probe::stack_cases();
            const auto& c=cases[forced_ ? *forced_ : (n/11)%cases.size()];
            s.sp=c.sp; s.cartridge_ram=c.cartridge_ram; s.work_ram_disabled=c.work_ram_disabled;
            if (s.cartridge_ram) s.banks[2]=3;
        }
        return s;
    }
    void force_stack(std::optional<std::size_t> placement, unsigned quota) { forced_=placement; quota_=quota; used_.clear(); }
    static std::size_t stack_placements() { return probe::stack_cases().size(); }

    // One call that must run in C++ against the original, with the address of
    // the original's last instruction (the exit it took) recorded.
    bool step(Machine& m, RoutineEntry entry) {
        const auto& routine=harness.routine(entry);
        if (!admitted(m,routine.spec)) { harness.run_fallback(m,entry,RoutineFallback::unmapped_stack); ++stats_.fallbacks; return false; }
        const bool trace=std::getenv("PSMODERN_PROBE_TRACE")!=nullptr;
        const auto bound=routine.bound(m);
        if (trace) std::cerr<<"  bound "<<bound<<std::endl;
        if (Machine::frame_cycles-m.cycles()%Machine::frame_cycles<bound) {
            harness.run_fallback(m,entry,RoutineFallback::frame); ++stats_.fallbacks; return false;
        }
        Machine original=m;
        const auto start=m.cycles();
        const bool ram_on=(m.memory().memory_control()&0x10)==0;
        const auto result=harness.run_cpp(m,entry);
        if (ram_on) check(result.cycles==bound,"entry "+std::to_string(entry.address)+" cost "+std::to_string(result.cycles)+" cycles, its bound said "+std::to_string(bound));
        std::uint16_t last=0;
        unsigned guard=0;
        while (original.cycles()-start<result.cycles && ++guard<4000) { last=original.cpu().pc.uint16_value; original.step(); }
        check(original.cycles()-start==result.cycles,"the original does not end on the C++ call's last cycle");
        ++stats_.calls; ++stats_.per_entry[entry.address]; stats_.exits[entry.address].insert(last);
        return true;
    }
    void run(std::uint16_t address, const Builder& build) {
        const auto entry=map::at1(address);
        if (forced_ && used_[address]++>=quota_) return;
        auto m=harness.prepare(entry,next_setup());
        build(m);
        if (std::getenv("PSMODERN_PROBE_TRACE")) std::cerr<<"probe 01:"<<std::hex<<address<<" sp "<<m.cpu().sp.uint16_value<<" slot2 "<<m.memory().mapping(0x8000).bank<<std::dec<<std::endl;
        step(m,entry);
    }

    // ---- The state the engine and the types read ----
    // Eight slots of random bytes; `used` of them with a type; the list
    // C289h-C298h of (key, slot) pairs with slots 0-7 or FFh.
    void slots(Machine& m, unsigned used_mask) {
        for (unsigned k=0;k<scaled(40);++k) {
            for (unsigned j=0;j<0x20;++j) poke(m,static_cast<std::uint16_t>(map::object_slots+0x20*k+j),byte());
            const bool used=used_mask&(1u<<k);
            poke(m,static_cast<std::uint16_t>(map::object_slots+0x20*k),used ? static_cast<std::uint8_t>((1+below(0x14))|(byte()&0x80)) : static_cast<std::uint8_t>(byte()&0x80));
            poke(m,static_cast<std::uint16_t>(map::object_slots+0x20*k+1),static_cast<std::uint8_t>(below(40)));
        }
    }
    // The list as the cut prologue leaves it: eight places with no slot.
    static void empty_list(Machine& m) {
        for (unsigned i=0;i<8;++i) { poke(m,static_cast<std::uint16_t>(0xC289+2*i),0); poke(m,static_cast<std::uint16_t>(0xC28A+2*i),0xFF); }
    }
    void list(Machine& m) {
        for (unsigned i=0;i<8;++i) {
            poke(m,static_cast<std::uint16_t>(0xC289+2*i),byte());
            poke(m,static_cast<std::uint16_t>(0xC28A+2*i),below(3)==0 ? 0xFF : static_cast<std::uint8_t>(below(8)));
        }
    }
    unsigned used_mask() {
        const auto kind=below(8);
        if (kind==0) return 0;
        if (kind==1) return 0xFF;
        return byte();
    }
    void variables(Machine& m) {
        poke(m,0xC264,below(3)==0 ? 0 : (byte()&0x0F)); poke(m,0xC265,below(3)==0 ? 0 : below(2) ? 0x0F : byte());
        poke(m,0xC2E9,below(2) ? 0 : byte()); poke(m,0xC2EA,byte()); poke(m,0xC204,byte()); poke(m,0xC812,byte());
        static constexpr std::uint8_t transports[]{0,4,8,0x0C,0x10,0x11,0xFF};
        poke(m,map::transport,below(4)==0 ? byte() : transports[below(7)]);
        poke_word(m,0xC217,static_cast<std::uint16_t>(0xC900+below(0x7F))); poke_word(m,0xC219,static_cast<std::uint16_t>(0xC980+below(0x7F)));
        poke_word(m,0xC287,static_cast<std::uint16_t>(0xC289+2*below(9)));
    }
    std::uint16_t some_slot(Machine& m) {
        const auto address=static_cast<std::uint16_t>(map::object_slots+0x20*below(8));
        set_iy(m,address);
        return address;
    }
    // The state of a slot being walked: the neighbours the follower reads (the slot before it).
    void walker(Machine& m, std::uint8_t c) {
        variables(m); slots(m,used_mask()); some_slot(m);
        const auto iy=m.cpu().ix_iy[1].uint16_value;
        if (below(2)) { poke(m,static_cast<std::uint16_t>(iy-0x1C),byte()); poke(m,static_cast<std::uint16_t>(iy-0x1E),byte()); }
        if (below(3)==0) { poke(m,static_cast<std::uint16_t>(iy-0x1C),m.peek(static_cast<std::uint16_t>(iy+4))); if (below(2)) poke(m,static_cast<std::uint16_t>(iy-0x1E),m.peek(static_cast<std::uint16_t>(iy+2))); }
        poke(m,static_cast<std::uint16_t>(iy+10),byte()&3); poke(m,static_cast<std::uint16_t>(iy-22),byte()&3);
        poke(m,static_cast<std::uint16_t>(iy+18),byte()&3); poke(m,static_cast<std::uint16_t>(iy-13),byte()&3);
        poke(m,static_cast<std::uint16_t>(iy+14),below(4)==0 ? 0 : static_cast<std::uint8_t>(below(9)));
        poke(m,static_cast<std::uint16_t>(iy+13),byte());
        set_bc(m,byte(),c);
        static constexpr std::uint16_t tables[]{0x5A13,0x5A17,0x5A27,0x5A2B,0x5A3B,0x5A3F};
        set_de(m,below(5)==0 ? static_cast<std::uint16_t>(0x5A13+below(0x40)) : tables[below(6)]);
        set_hl(m,tables[below(6)]);
    }

    // ---- The probes, one family per entry ----
    void engine() {
        for (unsigned k=0;k<scaled(500);++k) run(0x576A,[&](Machine& m) { variables(m); slots(m,used_mask()); list(m); });
        for (unsigned k=0;k<scaled(2500);++k)
            run(0x5781,[&](Machine& m) {
                variables(m); slots(m,used_mask()); list(m); set_hl(m,0xC900);
                if (below(2)) empty_list(m);
            });
        for (unsigned k=0;k<scaled(4000);++k)
            run(0x579F,[&](Machine& m) {
                variables(m); slots(m,used_mask()); list(m);
                if (below(3)==0) empty_list(m);
                const unsigned slot=below(8);
                const std::uint16_t iy=static_cast<std::uint16_t>(map::object_slots+0x20*slot);
                set_iy(m,iy); set_a(m,below(3)==0 ? 0 : byte());
                poke(m,static_cast<std::uint16_t>(iy+2),byte());
                lay_stack(m,{static_cast<std::uint16_t>(((8-slot)<<8)|slot),0x0180});
            });
        for (unsigned k=0;k<scaled(4000);++k)
            run(0x580C,[&](Machine& m) {
                variables(m); slots(m,used_mask()); list(m);
                if (below(3)==0) empty_list(m);
                const unsigned pair=below(8);
                const auto hl=static_cast<std::uint16_t>(0xC28A+2*pair);
                lay_stack(m,{hl,static_cast<std::uint16_t>(((8-pair)<<8)|byte()),0x0180});
                set_iy(m,static_cast<std::uint16_t>(map::object_slots+0x20*below(8)));
            });
        for (unsigned k=0;k<scaled(1000);++k)
            run(0x5818,[&](Machine& m) { slots(m,used_mask()); set_iy(m,below(3) ? static_cast<std::uint16_t>(map::object_slots+0x20*below(8)) : static_cast<std::uint16_t>(0xC400+below(0x300))); });
        for (unsigned k=0;k<scaled(2500);++k)
            run(0x5853,[&](Machine& m) {
                variables(m); slots(m,used_mask()); some_slot(m);
                m.cpu().bc.uint16_value=below(2) ? 0x96F4 : (below(2) ? 0x8000 : static_cast<std::uint16_t>(0x8000+below(0x4000)));
            });
        // The copy loops mid-way: the cut after the first load of each loop (HL in the sprite data, DE in a table, C the offset).
        for (unsigned k=0;k<scaled(2500);++k)
            run(0x5869,[&](Machine& m) {
                variables(m); slots(m,used_mask()); some_slot(m);
                set_hl(m,static_cast<std::uint16_t>(0x9000+below(0x800))); set_de(m,static_cast<std::uint16_t>(0xC900+below(0x70)));
                set_bc(m,static_cast<std::uint8_t>(below(3)==0 ? 1 : 1+below(60)),byte()); set_a(m,byte());
                lay_stack(m,{static_cast<std::uint16_t>(((below(2) ? 1+below(2) : byte())<<8)|byte()),0x0180});
            });
        for (unsigned k=0;k<scaled(2500);++k)
            run(0x587C,[&](Machine& m) {
                variables(m); slots(m,used_mask()); some_slot(m);
                set_hl(m,static_cast<std::uint16_t>(0x9000+below(0x800))); set_de(m,static_cast<std::uint16_t>(0xCA00+below(0x70)));
                set_bc(m,static_cast<std::uint8_t>(1+below(60)),byte()); set_a(m,byte());
            });
    }
    void types() {
        for (const std::uint16_t entry:{0x599F,0x59E0,0x59F1,0x5A02})
            for (unsigned k=0;k<scaled(400);++k)
                run(entry,[&](Machine& m) { variables(m); slots(m,used_mask()); some_slot(m); set_bc(m,byte(),below(3)==0 ? 0 : byte()); });
        for (unsigned k=0;k<scaled(1500);++k)
            run(0x59A5,[&](Machine& m) {
                variables(m); slots(m,used_mask()); some_slot(m);
                lay_stack(m,{static_cast<std::uint16_t>((byte()<<8)|(below(3)==0 ? 0 : byte())),0x0180});
            });
        for (const std::uint16_t entry:{0x59D4,0x59E5,0x59F6,0x5A07,0x59DD,0x59EE,0x59FF,0x5A10})
            for (unsigned k=0;k<scaled(100);++k) run(entry,[&](Machine&) {});
        for (unsigned k=0;k<scaled(400);++k) run(0x5A52,[&](Machine& m) { variables(m); slots(m,used_mask()); some_slot(m); });
        for (unsigned k=0;k<scaled(100);++k) run(0x5A73,[&](Machine&) {});
        for (unsigned k=0;k<scaled(1500);++k) run(0x5A79,[&](Machine& m) { variables(m); slots(m,used_mask()); some_slot(m); });
        for (unsigned k=0;k<scaled(1000);++k) run(0x5A98,[&](Machine& m) { set_bc(m,byte(),below(2) ? 0 : byte()); });
        for (unsigned k=0;k<scaled(15000);++k) run(0x5A9D,[&](Machine& m) { walker(m,below(2) ? 0 : static_cast<std::uint8_t>(1+below(7))); });
        for (unsigned k=0;k<scaled(7500);++k) run(0x5B35,[&](Machine& m) { walker(m,below(2) ? 0 : static_cast<std::uint8_t>(1+below(7))); });
        for (unsigned k=0;k<scaled(1000);++k) run(0x5B5C,[&](Machine& m) { walker(m,static_cast<std::uint8_t>(1+below(7))); });
        for (unsigned k=0;k<scaled(1000);++k) run(0x5B66,[&](Machine& m) { walker(m,static_cast<std::uint8_t>(1+below(7))); });
        for (unsigned k=0;k<scaled(1500);++k)
            run(0x5B6B,[&](Machine& m) { walker(m,static_cast<std::uint8_t>(1+below(7))); set_a(m,below(2) ? static_cast<std::uint8_t>(below(5)) : byte()); });
    }
    void everything() { engine(); types(); }
    Stats& stats() { return stats_; }
private:
    Stats& stats_;
    std::mt19937_64 rng_;
    std::uint64_t seed_;
    unsigned serial_=0;
    std::optional<std::size_t> forced_;
    unsigned quota_=0;
    std::map<std::uint16_t,unsigned> used_;
};

// Entry points of the design that no state from a power-on machine can finish
// through the exit: 01:5809 from 01:576A (the list starts empty, and only a
// handler's answer fills it).
// and 01:5B3E / 01:5B5B from 01:5A9D (every path there meets the call at 01:5B32 first, which ends the segment),
// and 01:587B and 01:588A from 01:5853 (its setup alone takes more than the chunk, so the first loop's exit is always taken).
bool reachable(std::uint16_t entry, std::uint16_t exit) {
    return !(entry==0x5781 && exit==0x5809) && !(entry==0x5A9D && (exit==0x5B3E || exit==0x5B5B)) && !(entry==0x5853 && (exit==0x587B || exit==0x588A));
}

// The two things of the engine worth keeping on record, checked on a ROM run: the
// sort orders only the first four places of the list, and the drawing leaves
// slot 2 on the bank of the sprite data.
void quirk_probes(const probe::Rom& rom) {
    probe::Harness harness(rom);
    const auto entry=map::at1(0x579F);
    for (unsigned seed=0;seed<40;++seed) {
        std::mt19937_64 random(seed*977+5);
        std::uint8_t keys[8];
        for (auto& key:keys) key=static_cast<std::uint8_t>(random());
        probe::Setup setup;
        setup.seed=seed+1; setup.sp=0xDF00; setup.banks[2]=3;
        auto m=harness.prepare(entry,setup);
        for (unsigned i=0;i<8;++i) {
            m.memory().write(static_cast<std::uint16_t>(0xC289+2*i),keys[i]);
            m.memory().write(static_cast<std::uint16_t>(0xC28A+2*i),static_cast<std::uint8_t>(i));
        }
        for (unsigned k=0;k<8;++k) m.memory().write(static_cast<std::uint16_t>(map::object_slots+0x20*k),0);
        // The last slot's handler has just answered zero: the sort and the first draw call follow.
        m.cpu().ix_iy[1].uint16_value=map::object_slots+0xE0;
        m.cpu().af.uint8_values.at_1=0;
        Lab::lay_stack(m,{0x0107,0x0180});
        harness.run_cpp(m,entry);
        std::uint8_t first[4];
        for (unsigned i=0;i<4;++i) first[i]=keys[i];
        std::sort(first,first+4,std::greater<>());
        for (unsigned i=0;i<4;++i)
            check(m.peek(static_cast<std::uint16_t>(0xC289+2*i))==first[i],"the first four places of the list are ordered by descending key");
        for (unsigned i=4;i<8;++i)
            check(m.peek(static_cast<std::uint16_t>(0xC289+2*i))==keys[i] && m.peek(static_cast<std::uint16_t>(0xC28A+2*i))==i,
                  "the places after the fourth keep their order");
        // The first listed slot is one of 0-3, so the sprite data of bank 3 is left in slot 2.
        check(m.memory().mapping(0x8000).bank==3,"the drawing leaves slot 2 on the bank of the sprite data");
    }
}

void power_on_probes(const probe::Rom& rom) {
    quirk_probes(rom);
    const auto started=std::chrono::steady_clock::now();
    Stats stats;
    Lab lab(rom,stats,0x576A);
    // The operands the design names are the ones the ROM holds.
    auto operand=[&](std::uint16_t at) { return static_cast<std::uint16_t>(rom.byte(1,at)|(rom.byte(1,static_cast<std::uint16_t>(at+1))<<8)); };
    check(operand(0x577F)==map::sprite_y && operand(0x5785)==map::sprite_xn &&
          operand(0x578C)==map::object_slots,"the engine reads the slots and sprite tables map.h names");
    auto phase=[&](const char* name, const std::function<void()>& work) {
        const auto before=stats.calls;
        const auto t=std::chrono::steady_clock::now();
        work();
        std::cout<<"  "<<name<<": "<<stats.calls-before<<" calls, "
                 <<std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count()<<" s"<<std::endl;
    };
    phase("engine",[&] { lab.engine(); });
    phase("types",[&] { lab.types(); });
    for (std::size_t placement=0;placement<Lab::stack_placements();++placement) {
        lab.force_stack(placement,6);
        lab.everything();
    }
    lab.force_stack(std::nullopt,0);
    RoutineRegistry mine;
    game::register_map_objects(mine);
    for (const auto& [entry,routine]:mine.entries()) {
        check(stats.per_entry[entry.address]>0,"a segment was never called in C++: "+std::to_string(entry.address));
        for (const auto& exit:routine.spec.exits) {
            if (!reachable(entry.address,exit.address)) continue;
            check(stats.exits[entry.address].count(exit.address)>0,
                  "an exit was never taken: entry "+std::to_string(entry.address)+", exit "+std::to_string(exit.address));
        }
    }
    std::cout<<stats.calls<<" calls in C++ against the original ("<<stats.fallbacks<<" fallbacks as expected), all 30 entries and every reachable declared exit, in "
             <<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" s.\n";
}

// ---- Probes from real map frames -----------------------------------------------------
// A machine at a frame end on the map (or in a battle, where the engine also
// runs), stopped at an entry with the state the real objects hold; the
// per-call verifier runs the original beside the C++ call.
void frame_probes(const map_probe::Inputs& inputs, const std::string& stem, bool& skipped, const std::function<bool(const Machine&)>& stop) {
    if (inputs.recordings.empty()) { std::cout<<stem<<": no recordings folder, skipped.\n"; skipped=true; return; }
    const auto path=inputs.recording(stem);
    if (!std::filesystem::exists(path)) { std::cout<<stem<<": recording absent for "<<inputs.version<<", skipped.\n"; return; }
    const auto recording=replay::read(path);
    check(recording.rom_sha1==inputs.rom.sha1,stem+" was recorded with another ROM");
    const auto image=map_probe::start_image(inputs,stem,recording);
    if (!image) { std::cout<<stem<<": start image missing (reference folder not configured), skipped.\n"; skipped=true; return; }
    map_probe::Scenario scenario(inputs.rom,*image,stem);
    std::uint64_t from=0;
    if (!scenario.play(recording,from,stop)) { std::cout<<stem<<": the recording never reaches the wanted state, skipped.\n"; return; }
    // State probes on a real frame: the engine from its entry, the draw of a slot.
    probe::Harness harness(inputs.rom);
    std::uint64_t cpp=0;
    for (unsigned k=0;k<scaled(60);++k) {
        probe::Setup setup;
        setup.seed=k+1; setup.sp=0xDF00; setup.banks[2]=3; setup.iff1=(k&1);
        auto m=map_probe::prepare_at(scenario.machine(),map::object_engine_unit,setup);
        harness.run_cpp(m,map::object_engine_unit); ++cpp;
    }
    // The real run: routines on (every C++ call verified) beside routines off, compared every frame.
    const auto before=scenario.calls(part);
    for (unsigned n=0;n<240 && from<recording.frames;++n) scenario.frame(recording.input_at(from++));
    const auto after=scenario.calls(part);
    check(after.first>before.first,stem+": the objects part ran no C++ call");
    std::cout<<stem<<": "<<cpp<<" state probes from a real frame; 240 frames with "<<after.first-before.first<<" C++ calls and "
             <<after.second-before.second<<" fallbacks, routines on and off compared every frame.\n";
}
// PSMODERN_OBJECTS_CENSUS=<recording>: plays that recording from its start with
// routines on and off compared and prints each registered entry's calls with
// the fallback reasons (a diagnostic for the census gate).
void census(const map_probe::Inputs& inputs, const std::string& stem) {
    const auto recording=replay::read(inputs.recording(stem));
    const auto image=map_probe::start_image(inputs,stem,recording);
    check(image.has_value(),"census: start image missing");
    map_probe::Scenario scenario(inputs.rom,*image,stem);
    std::vector<std::pair<std::uint64_t,std::uint64_t>> phases;   // the cycle in the frame and the bound of each 01:576A call
    const char* watched=std::getenv("PSMODERN_OBJECTS_ENTRY");
    const auto watched_entry=map::at1(watched ? static_cast<std::uint16_t>(std::stoul(watched,nullptr,16)) : 0x576A);
    if (auto* routine=scenario.routines().find(watched_entry)) {
        auto inner=routine->bound;
        routine->bound=[&phases,inner](const Machine& machine) {
            const auto bound=inner(machine);
            phases.emplace_back(machine.cycles()%Machine::frame_cycles,bound);
            return bound;
        };
    }
    for (std::uint64_t from=0;from<recording.frames;) scenario.frame(recording.input_at(from++));
    std::map<std::uint64_t,unsigned> histogram;
    for (const auto& [phase,bound]:phases) { (void)bound; ++histogram[phase/1000]; }
    std::cout<<"watched entry phase in the frame (thousands of cycles: calls):";
    for (const auto& [bucket,count]:histogram) std::cout<<" "<<bucket<<":"<<count;
    std::cout<<std::endl;
    std::map<std::uint64_t,unsigned> lengths;
    for (const auto& [phase,bound]:phases) { (void)phase; ++lengths[bound/250]; }
    std::cout<<"watched entry bound (quarter thousands of cycles: calls):";
    for (const auto& [bucket,count]:lengths) std::cout<<" "<<bucket<<":"<<count;
    std::cout<<std::endl;
    std::cout<<"watched entry calls with the frame boundary within a window of W cycles:";
    for (const std::uint64_t window:{100,150,200,250,300,400,500,600,800,1000,1500}) {
        unsigned hit=0;
        for (const auto& [phase,bound]:phases) { (void)bound; if (Machine::frame_cycles-phase<window) ++hit; }
        std::cout<<" W"<<window<<"="<<hit;
    }
    std::cout<<" of "<<phases.size()<<std::endl;
    for (const auto& [entry,routine]:scenario.routines().entries()) {
        if (entry.bank!=1 || entry.address<0x576A || entry.address>0x5B86) continue;
        const auto c=scenario.counters(entry);
        (void)routine;
        std::cout<<"01:"<<std::hex<<entry.address<<std::dec<<" cpp "<<c.cpp_calls<<" fallback "<<c.fallback_calls;
        for (unsigned i=0;i<routine_fallback_names.size();++i) if (c.reasons[i]) std::cout<<" "<<routine_fallback_names[i]<<"="<<c.reasons[i];
        std::cout<<"\n";
    }
}
void probes(const map_probe::Inputs& inputs) {
    if (const char* stem=std::getenv("PSMODERN_OBJECTS_CENSUS")) { census(inputs,stem); return; }
    power_on_probes(inputs.rom);
    bool skipped=false;
    frame_probes(inputs,"later-town",skipped,map_probe::on_map);
    frame_probes(inputs,"first-battle",skipped,[n=0u](const Machine&) mutable { return ++n>=2500; });
    frame_probes(inputs,"vehicles",skipped,map_probe::on_map);
    if (skipped) std::exit(77);
}
}

int main(int argc, char** argv) {
    try {
        registration();
        if (argc==1) return 0;
        const auto inputs=map_probe::load(argc,argv);
        if (!inputs) return 77;
        probes(*inputs);
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
