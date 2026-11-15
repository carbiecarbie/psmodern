// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 11, part terrain: what the ground allows, vehicles and ice.
//   no arguments                       registration catalog (no ROM)
//   <rom> <us|br> [<recordings folder> [<reference folder>]]
//                                      state probes, whole-flow probes and
//                                      scenario probes on that ROM
//                                      (tests/map_probe.h)
// A ROM run exits 77 without the ROM; the scenario probes need the
// recordings and the reference folder and are skipped (exit 77 at the end)
// without them. PSMODERN_TERRAIN_COVERAGE=<file> writes the code addresses
// the original ran in every probe (for the unreached-paths check).
#include "map_probe.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>

using namespace psmodern;
using probe::check;
namespace map=game::map;
namespace {
constexpr map::Part part=map::Part::terrain;
constexpr RoutineEntry at(std::uint16_t address) { return {1,address}; }
constexpr std::uint16_t pending_return=0x0180;
constexpr std::uint16_t facing_ram=0xC2D7, tile_memo=0xC2E5, camera_copy_y=0xC311, camera_copy_x=0xC313, sound_request=0xC004;

// ---- Registration catalog --------------------------------------------------------
struct ExitSpec { RoutineExit kind; std::uint16_t address; int stack; };
ExitSpec F(std::uint16_t address, int stack=0) { return {RoutineExit::core_final,address,stack}; }
ExitSpec R(std::uint16_t address, int stack=0) { return {RoutineExit::returned,address,stack}; }
struct Expect {
    std::uint16_t entry; std::vector<ExitSpec> exits; bool slot2; unsigned scratch, inherited;
};
const std::vector<Expect>& expectations() {
    static const std::vector<Expect> table{
        {0x7764,{R(0x77AB)},true,0,0},
        {0x74E4,{F(0x74FA,-4),F(0x7513,-6),F(0x7532,-6),F(0x754F,-6),R(0x7505)},false,6,0},
        {0x74FD,{R(0x7501,4)},false,0,4},
        {0x7516,{F(0x751C),R(0x7524,6)},false,0,6},
        {0x751F,{R(0x7524,6)},false,0,6},
        {0x7535,{F(0x753B),R(0x7543,6)},false,0,6},
        {0x753E,{R(0x7543,6)},false,0,6},
        {0x7552,{F(0x7557)},false,0,6},
        {0x755A,{R(0x756B,6)},false,2,6},
        {0x756C,{F(0x75C9,-2),R(0x7601)},true,2,0},
        {0x75E5,{R(0x7601)},true,0,0},
        {0x7602,{R(0x7607),R(0x760D),F(0x7610)},false,0,0},
        {0x7613,{F(0x7615)},false,0,0},
        {0x7618,{F(0x761A)},false,0,0},
        {0x761D,{R(0x764F),R(0x7652),R(0x7655)},false,0,0},
        {0x761F,{R(0x764F),R(0x7652),R(0x7655)},false,0,0},
        {0x7656,{F(0x7662,-2)},false,2,0},
        {0x7665,{F(0x766B),F(0x7662),R(0x768E,2)},false,0,2},
        {0x766E,{F(0x7674),F(0x7662),R(0x768E,2)},false,0,2},
        {0x7677,{F(0x767D),F(0x7662),R(0x768E,2)},false,0,2},
        {0x7680,{F(0x7662),R(0x768E,2),R(0x7692,2),R(0x76C0,2)},false,0,2},
        {0x76C1,{F(0x76CD,-2)},false,2,0},
        {0x76D0,{F(0x76D6),F(0x76CD),R(0x76F9,2)},false,0,2},
        {0x76D9,{F(0x76DF),F(0x76CD),R(0x76F9,2)},false,0,2},
        {0x76E2,{F(0x76E8),F(0x76CD),R(0x76F9,2)},false,0,2},
        {0x76EB,{F(0x76CD),R(0x76F9,2),R(0x7731,2)},false,0,2},
        {0x7732,{F(0x7746,-2)},false,2,0},
        {0x7749,{F(0x7746),R(0x775D,2),R(0x7731,2)},false,0,2},
    };
    return table;
}

void registration() {
    const auto entries=map_probe::registered_entries(part,game::register_map_terrain);
    const auto planned=map::planned_segments(part);
    std::cout<<entries.size()<<" of "<<planned.size()<<" planned terrain segments registered.\n";
    check(entries.size()==planned.size(),"every planned terrain segment is registered");
    RoutineRegistry registry;
    game::register_map_terrain(registry);
    check(expectations().size()==planned.size(),"the catalog lists every planned terrain segment");
    for (const auto& expect:expectations()) {
        const auto* routine=registry.find(at(expect.entry));
        check(routine!=nullptr,"a planned terrain entry is not registered");
        const auto& spec=routine->spec;
        check(std::any_of(planned.begin(),planned.end(),[&](const auto& s) { return s.entry==at(expect.entry) && s.unit==spec.unit; }),
              "a terrain segment names another unit than planned");
        check(spec.exits.size()==expect.exits.size(),"a terrain segment declares another number of exits");
        for (std::size_t i=0;i<expect.exits.size();++i) {
            const auto& exit=spec.exits[i];
            check(exit.kind==expect.exits[i].kind && exit.address==expect.exits[i].address &&
                  exit.stack==expect.exits[i].stack && !exit.conditional && exit.after==0,"a terrain exit differs from the plan");
        }
        check(spec.slot2_mapper==expect.slot2 && spec.scratch_stack==expect.scratch && spec.inherited_stack==expect.inherited &&
              !spec.single_instruction && !spec.force_admit,"a terrain segment declares other permissions");
    }
    // None of the native DI stretch or the options boundary is an entry.
    for (const auto& [entry,routine]:registry.entries()) {
        (void)routine;
        for (const auto& range:map::native_stretches)
            check(!(entry.bank==range.bank && entry.address>=range.first && entry.address<=range.last),
                  "a terrain segment starts inside a native stretch");
    }
}

// ---- The world a probe poses ----------------------------------------------------------
struct World {
    std::uint8_t road, transport, area, scroll_y, facing;
    std::uint16_t camera_y, camera_x;
    std::vector<std::uint8_t> window;      // CC00h-CFFFh, the four decoded screens
};

struct Rig {
    probe::Harness harness;
    std::mt19937_64 rng{0x7e44u};
    std::uint64_t seed=1;
    unsigned count=0, flows=0, fallbacks=0;
    std::map<RoutineEntry,unsigned> per_entry;
    std::set<unsigned> covered;
    std::map<std::uint16_t,std::set<std::uint16_t>> pcs_after;
    explicit Rig(const probe::Rom& rom) : harness(rom) {}

    unsigned pick(unsigned n) { return static_cast<unsigned>(rng()%n); }
    std::uint8_t byte() { return static_cast<std::uint8_t>(rng()); }
    std::uint8_t attribute(unsigned tile, unsigned area) const {
        return harness.rom().byte(3,static_cast<std::uint16_t>(0xBC6F+tile+(area>=4 ? 0x100 : 0)));
    }
    // A random world. `mask` is the blocking mask the probe cares about: the
    // window mixes tiles clear of it (so that a try succeeds) and blocked
    // ones, with ice (D8h-DFh) and broken ice (D7h) in some worlds.
    World world(unsigned mask, int transport=-1, int area=-1) {
        World w{};
        const unsigned roads[]={0,0,0,0,0,1,6};
        w.road=static_cast<std::uint8_t>(roads[pick(7)]);
        const unsigned kinds[]={0,4,8,0x0C,0x10,0x11,0x0C,0x04};
        w.transport=static_cast<std::uint8_t>(transport>=0 ? transport : (pick(8)==7 ? byte() : kinds[pick(8)]));
        const unsigned areas[]={0,1,2,3,4,5,6,0x20,2,2,3};
        w.area=static_cast<std::uint8_t>(area>=0 ? area : (pick(12)==11 ? byte() : areas[pick(11)]));
        w.camera_y=static_cast<std::uint16_t>((pick(4)==0 ? byte() : pick(0xC0))|(pick(8)<<8));
        w.camera_x=static_cast<std::uint16_t>(byte()|(pick(8)<<8));
        if (pick(5)==0) w.camera_x=static_cast<std::uint16_t>((w.camera_x&0xFF00)|(pick(2) ? 0xFF-pick(4) : pick(4)));
        w.scroll_y=byte(); w.facing=byte();
        std::vector<std::uint8_t> clear, blocked;
        for (unsigned tile=0;tile<256;++tile)
            (attribute(tile,w.area)&mask ? blocked : clear).push_back(static_cast<std::uint8_t>(tile));
        if (clear.empty()) clear.push_back(0);
        if (blocked.empty()) blocked.push_back(0);
        const unsigned blocks[]={0,0,5,20,50,100}, ices[]={0,0,10,60};
        const unsigned p_block=blocks[pick(6)], p_ice=ices[pick(4)];
        w.window.resize(0x400);
        for (auto& tile:w.window) {
            const unsigned roll=pick(100);
            tile=roll<p_block ? blocked[pick(static_cast<unsigned>(blocked.size()))] : clear[pick(static_cast<unsigned>(clear.size()))];
            if (pick(100)<p_ice) tile=static_cast<std::uint8_t>(0xD8+pick(8));
            else if (pick(100)<3) tile=0xD7;
            else if (pick(100)<2) tile=byte();
        }
        return w;
    }
    static void apply(Machine& machine, const World& w) {
        auto& memory=machine.memory();
        auto word=[&](std::uint16_t address, std::uint16_t value) {
            memory.write(address,static_cast<std::uint8_t>(value)); memory.write(static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
        };
        memory.write(map::road_index,w.road); memory.write(map::transport,w.transport); memory.write(map::area,w.area);
        memory.write(map::scroll_y,w.scroll_y); memory.write(facing_ram,w.facing);
        word(map::camera_y,w.camera_y); word(map::camera_x,w.camera_x);
        for (unsigned i=0;i<w.window.size();++i) memory.write(static_cast<std::uint16_t>(map::screen_window+i),w.window[i]);
    }

    // The words held on the stack above the return address (top first).
    using Held=std::vector<std::uint16_t>;
    using Regs=std::function<void(Machine&)>;
    Machine make(RoutineEntry entry, probe::Setup setup, const World& w, const Held& held, const Regs& regs) {
        setup.seed=seed++;
        auto machine=harness.prepare(entry,setup);
        auto& memory=machine.memory();
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()&~0x10);
        apply(machine,w);
        auto sp=setup.sp;
        auto write_word=[&](std::uint16_t value) {
            memory.write(sp,static_cast<std::uint8_t>(value)); memory.write(static_cast<std::uint16_t>(sp+1),static_cast<std::uint8_t>(value>>8));
            sp=static_cast<std::uint16_t>(sp+2);
        };
        if (setup.sp<0xfffc-1-2*held.size()) {
            for (const auto value:held) write_word(value);
            write_word(pending_return);
        }
        if (regs) regs(machine);
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
        return machine;
    }
    // The code the original runs from this state until it returns to the
    // pending return address.
    void cover(const Machine& machine) {
        Machine original=machine;
        std::set<unsigned> seen;
        for (unsigned i=0;i<6000;++i) {
            const auto pc=original.cpu().pc.uint16_value;
            if (pc==pending_return) { covered.insert(seen.begin(),seen.end()); return; }
            seen.insert(pc);
            original.step();
        }
    }
    // One probe: a segment dispatch that runs in C++ with exactly the cycles
    // its bound announced, verified against the original.
    probe::Harness::Result run(RoutineEntry entry, probe::Setup setup, const World& w, const Held& held, const Regs& regs={}) {
        auto machine=make(entry,setup,w,held,regs);
        const auto bound=harness.routine(entry).bound(machine);
        check(bound<=Machine::frame_cycles || setup.work_ram_disabled,"a terrain segment's bound is above a frame without cause");
        cover(machine);
        probe::Harness::Result result{};
        try { result=harness.run_cpp(machine,entry); }
        catch (const std::exception& error) {
            std::ostringstream text;
            text<<error.what()<<std::hex<<" [entry "<<entry.address<<" road "<<+w.road<<" transport "<<+w.transport<<" area "<<+w.area
                <<" camera y "<<w.camera_y<<" x "<<w.camera_x<<" sp "<<setup.sp<<" seed "<<std::dec<<seed<<"]";
            throw std::runtime_error(text.str());
        }
        ++count; ++per_entry[entry];
        check(result.cycles==bound,"entry "+std::to_string(entry.address)+" cost "+std::to_string(result.cycles)+" cycles, its bound said "+std::to_string(bound));
        pcs_after[entry.address].insert(machine.cpu().pc.uint16_value);
        return result;
    }
    probe::Setup setup() {
        probe::Setup s; s.iff1=s.iff2=(seed&1); s.r=byte(); return s;
    }
    probe::Harness::Result run(RoutineEntry entry, const World& w, const Held& held, const Regs& regs={}) {
        return run(entry,setup(),w,held,regs);
    }

    // The whole flow from a top entry to its return, stepping through the
    // registry: every segment verified against the original, and the end
    // state and cycle count equal to the original's on a copy.
    void flow(RoutineEntry entry, const World& w, const Regs& regs) {
        auto s=setup(); s.iff1=s.iff2=false;
        auto machine=make(entry,s,w,{},regs);
        cover(machine);
        Machine original=machine;
        unsigned steps=0;
        while (original.cpu().pc.uint16_value!=pending_return) { original.step(); check(++steps<20000,"the original did not return"); }
        steps=0;
        std::map<RoutineEntry,RoutineCounters> before;
        for (const auto& [e,routine]:harness.registry().entries()) before[e]=routine.counters;
        while (machine.cpu().pc.uint16_value!=pending_return) { machine.step(harness.registry()); check(++steps<20000,"the C++ flow did not return"); }
        for (const auto& [e,routine]:harness.registry().entries()) {
            const auto& old=before[e];
            fallbacks+=static_cast<unsigned>(routine.counters.fallback_calls-old.fallback_calls);
            per_entry[e]+=static_cast<unsigned>(routine.counters.cpp_calls-old.cpp_calls);
        }
        std::string fields;
        for (const auto& difference:original.differences(machine)) fields+=" "+difference.field;
        check(fields.empty(),"a terrain flow differs from the original:"+fields);
        check(machine.cycles()==original.cycles(),"a terrain flow took other cycles than the original");
        ++flows;
    }
};

// ---- State probes of every entry -------------------------------------------------------------
constexpr std::uint8_t carry_edges[]={0x00,0x01,0x0F,0x10,0x7F,0x80,0xBF,0xC0,0xC1,0xEF,0xF0,0xFF};
void set_a(Machine& m, std::uint8_t value) { m.cpu().af.uint8_values.at_1=value; }
void set_bc(Machine& m, std::uint8_t b, std::uint8_t c) { m.cpu().bc.uint8_values.at_1=b; m.cpu().bc.uint8_values.at_0=c; }
void set_de(Machine& m, std::uint16_t value) { m.cpu().de.uint16_value=value; }

// Probe indexes the game uses (the tables at 01:7A21-7A75 hold them) and the
// whole range around them, since the lookup accepts any byte.
unsigned probe_index(Rig& g) { return g.pick(3)==0 ? g.byte() : g.pick(0x16); }

void tile_attribute_probes(Rig& g) {
    for (unsigned a=0;a<256;++a)
        for (unsigned k=0;k<2;++k) {
            const auto w=g.world(1);
            g.run(at(0x7764),w,{},[a](Machine& m) { set_a(m,static_cast<std::uint8_t>(a)); });
        }
    // Page and column edges: every combination of camera Y and X around the wraps.
    for (const auto y:carry_edges)
        for (const auto x:carry_edges)
            for (unsigned area:{0u,3u,4u,9u}) {
                auto w=g.world(1,-1,static_cast<int>(area));
                w.camera_y=static_cast<std::uint16_t>(y|(g.pick(8)<<8)); w.camera_x=static_cast<std::uint16_t>(x|(g.pick(8)<<8));
                g.run(at(0x7764),w,{},[&](Machine& m) { set_a(m,static_cast<std::uint8_t>(g.pick(0x16)&~1u)); });
            }
}

void blocked_probes(Rig& g) {
    // The head, every transport kind, the road, on foot, every index.
    for (const unsigned transport:{0u,4u,8u,0x0Cu,0x10u,0x11u,0xFFu})
        for (const unsigned road:{0u,1u,6u})
            for (unsigned a=0;a<12;++a)
                for (unsigned k=0;k<2;++k) {
                    auto w=g.world(k&1 ? 0x0A : 1,static_cast<int>(transport));
                    w.road=static_cast<std::uint8_t>(road);
                    g.run(at(0x74E4),w,{},[a](Machine& m) { set_a(m,static_cast<std::uint8_t>(a)); });
                }
    for (unsigned k=0;k<300;++k) {
        const auto w=g.world(0x0F);
        const auto a=static_cast<std::uint8_t>(g.pick(8)==0 ? g.byte() : g.pick(10));
        g.run(at(0x74E4),w,{},[a](Machine& m) { set_a(m,a); });
    }
    const auto result=[&](std::uint16_t entry, Rig::Held held, unsigned count) {
        for (unsigned k=0;k<count;++k) {
            const auto w=g.world(0x0F);
            const auto a=g.byte(), b=g.byte(), c=g.byte();
            const auto de=static_cast<std::uint16_t>(0x7A2B+g.pick(10)), pushed=static_cast<std::uint16_t>(g.rng());
            auto stack=held; for (auto& word:stack) word=static_cast<std::uint16_t>(g.rng()^pushed);
            g.run(at(entry),w,stack,[=,&g](Machine& m) { set_a(m,a); set_bc(m,b,c); set_de(m,de); });
        }
    };
    result(0x74FD,{0,0},64);
    for (const auto entry:{0x7516,0x751F,0x7535,0x753E}) result(static_cast<std::uint16_t>(entry),{0,0,0},200);
    result(0x7552,{0,0,0},200);
    // The second ice probe: E set and clear, every combination of the two results.
    for (unsigned k=0;k<256;++k) {
        const auto w=g.world(0x0F);
        const auto a=static_cast<std::uint8_t>(k&1 ? 0x01 : 0x00), d=static_cast<std::uint8_t>((k&2) ? 1 : 0), e=static_cast<std::uint8_t>((k&4) ? g.byte()|1 : 0);
        g.run(at(0x755A),w,{0,0,0},[=,&g](Machine& m) { set_a(m,static_cast<std::uint8_t>(a|(g.byte()&0xFE))); m.cpu().de.uint8_values.at_1=d; m.cpu().de.uint8_values.at_0=e; });
    }
}

void ice_probes(Rig& g) {
    for (unsigned k=0;k<500;++k) {
        auto w=g.world(1,static_cast<int>(g.pick(2) ? 0x0C : -1));
        const auto a=static_cast<std::uint8_t>(probe_index(g));
        g.run(at(0x756C),w,{},[=,&g](Machine& m) { set_a(m,a); m.cpu().de.uint16_value=static_cast<std::uint16_t>(g.rng()); });
    }
    // Every carry combination of the name-table address arithmetic: camera Y low byte
    // and scroll Y (the picture's row) around their wraps, with ice under every cell.
    for (const auto y:carry_edges)
        for (const auto s:carry_edges)
            for (const auto x:{0x00,0x37,0x80,0xFF})
                for (const unsigned a:{0u,2u,0x0Au,0x0Cu,0x12u,0x14u}) {
                    auto w=g.world(1,-1,static_cast<int>(g.pick(6)));
                    std::fill(w.window.begin(),w.window.end(),static_cast<std::uint8_t>(0xD8+g.pick(8)));
                    w.camera_y=static_cast<std::uint16_t>(y|(g.pick(8)<<8)); w.scroll_y=s; w.camera_x=static_cast<std::uint16_t>(x|(g.pick(8)<<8));
                    g.run(at(0x756C),w,{},[=,&g](Machine& m) { set_a(m,static_cast<std::uint8_t>(a)); });
                }
    // After the name-table update: A, E and the area (the attribute table in use).
    for (unsigned k=0;k<200;++k) {
        const auto w=g.world(1,-1,static_cast<int>(k%8));
        g.run(at(0x75E5),w,{},[](Machine& m) { set_a(m,0x00); });
    }
    // The preset: road area and the vehicle that digs, and every near miss of them.
    for (unsigned k=0;k<400;++k) {
        auto w=g.world(1,static_cast<int>(k%3==0 ? 0x0C : g.pick(2) ? 0x0C : 0x08),static_cast<int>(k%5==0 ? g.pick(8) : 2));
        if (k%7==0) w.transport=0x0C;
        g.run(at(0x7602),w,{});
    }
    for (const auto entry:{0x7613,0x7618,0x761D})
        for (unsigned k=0;k<100;++k) {
            auto w=g.world(1,-1,static_cast<int>(g.pick(6)));
            if (k%2) for (auto& t:w.window) if (g.pick(2)) t=static_cast<std::uint8_t>(0xD8+g.pick(8));
            g.run(at(static_cast<std::uint16_t>(entry)),w,{});
        }
    for (unsigned k=0;k<400;++k) {
        auto w=g.world(1,-1,static_cast<int>(g.pick(8)));
        std::fill(w.window.begin(),w.window.end(),static_cast<std::uint8_t>(k&1 ? 0xD8+g.pick(8) : 0xD0+g.pick(16)));
        g.run(at(0x761F),w,{},[&](Machine& m) { set_a(m,static_cast<std::uint8_t>(probe_index(g))); });
    }
}

void vehicle_probes(Rig& g) {
    // The tries: B tries left and the try offset C, the probe's result in A.
    struct Chain { std::uint16_t table; unsigned stride, tries; std::vector<std::uint16_t> after; std::uint16_t last; };
    const Chain landrover{0x7A35,4,4,{0x7665,0x766E,0x7677},0x7680}, hovercraft{0x7A45,6,8,{0x76D0,0x76D9,0x76E2},0x76EB};
    for (const auto& chain:{landrover,hovercraft}) {
        std::vector<std::uint16_t> entries=chain.after; entries.push_back(chain.last);
        for (const auto entry:entries)
            for (unsigned k=0;k<300;++k) {
                const auto w=g.world(chain.stride==4 ? 0x0D : 0x0A);
                const unsigned tries_left=1+g.pick(chain.tries);
                const auto c=static_cast<std::uint8_t>(chain.stride*(chain.tries-tries_left));
                const auto a=static_cast<std::uint8_t>(g.pick(3)==0 ? g.byte() : (g.pick(2) ? 0 : (chain.stride==4 ? 0x0D : 0x0A))&g.byte());
                const auto de=static_cast<std::uint16_t>(chain.table+chain.stride*(chain.tries-tries_left)+g.pick(chain.stride));
                g.run(at(entry),w,{static_cast<std::uint16_t>((tries_left<<8)|c)},[=,&g](Machine& m) { set_a(m,a); set_de(m,de); set_bc(m,static_cast<std::uint8_t>(g.byte()),static_cast<std::uint8_t>(g.byte())); });
            }
    }
    // The leaving routine's tries.
    for (unsigned k=0;k<300;++k) {
        const auto w=g.world(0x0D);
        const unsigned tries_left=1+g.pick(8);
        const auto c=static_cast<std::uint8_t>((g.pick(24)));
        const auto a=static_cast<std::uint8_t>(g.pick(3)==0 ? g.byte() : (g.pick(2) ? 0 : 0x0D)&g.byte());
        g.run(at(0x7749),w,{static_cast<std::uint16_t>((tries_left<<8)|c)},[=,&g](Machine& m) { set_a(m,a); });
    }
    // The heads: any state of the registers.
    for (const auto entry:{0x7656,0x76C1,0x7732})
        for (unsigned k=0;k<200;++k) g.run(at(static_cast<std::uint16_t>(entry)),g.world(0x0D),{});
}

// ---- Whole flows ------------------------------------------------------------------------------
void flow_probes(Rig& g) {
    for (unsigned k=0;k<250;++k) {
        const auto w=g.world(0x0F);
        const auto a=static_cast<std::uint8_t>(probe_index(g)%16);
        g.flow(at(0x7764),w,[a](Machine& m) { set_a(m,a); });
    }
    for (unsigned k=0;k<700;++k) {
        const auto w=g.world(k&1 ? 0x0A : 1,k%3==0 ? static_cast<int>(0x0C) : k%3==1 ? static_cast<int>(0x10+(k&1)) : -1);
        const auto a=static_cast<std::uint8_t>(g.pick(10));
        g.flow(at(0x74E4),w,[a](Machine& m) { set_a(m,a); });
    }
    for (unsigned k=0;k<400;++k) {
        const auto w=g.world(0x0D,-1,static_cast<int>(g.pick(2) ? 2 : -1));
        g.flow(at(0x7656),w,{});
    }
    for (unsigned k=0;k<400;++k) g.flow(at(0x76C1),g.world(0x0A),{});
    for (unsigned k=0;k<400;++k) g.flow(at(0x7732),g.world(0x0D),{});
    for (unsigned k=0;k<200;++k) {
        auto w=g.world(1,0x0C,2);
        g.flow(at(0x7602),w,{});
    }
    for (unsigned k=0;k<300;++k) {
        auto w=g.world(1,-1,static_cast<int>(g.pick(6)));
        const auto a=static_cast<std::uint8_t>(g.pick(3)==0 ? g.pick(0x16) : (g.pick(4)*2+0x0A));
        g.flow(at(0x756C),w,[a](Machine& m) { set_a(m,a); });
    }
}

// ---- Environment: slot 2, refresh counter, stack placements ---------------------------------
// Every entry kind with the usual variations. A scratch stack that touches an
// unmapped address or the mapper registers falls back (unmapped_stack); every
// other placement runs in C++.
struct Frame { std::uint16_t entry; Rig::Held held; };
void environment_probes(Rig& g) {
    const std::vector<Frame> frames{
        {0x7764,{}},{0x74E4,{}},{0x74FD,{0,0}},{0x7516,{0,0,0}},{0x755A,{0,0,0}},{0x756C,{}},{0x75E5,{}},{0x7602,{}},{0x761F,{}},
        {0x7656,{}},{0x7665,{0x0300}},{0x7680,{0x0100}},{0x76C1,{}},{0x76D0,{0x0700}},{0x76EB,{0x0100}},{0x7732,{}},{0x7749,{0x0800}}};
    unsigned skip=0;
    for (const auto& frame:frames) {
        const auto entry=at(frame.entry);
        const auto scratch=g.harness.routine(entry).spec.scratch_stack;
        const auto make_world=[&] { return g.world(0x0F); };
        const Rig::Regs regs=[&](Machine& m) { set_a(m,static_cast<std::uint8_t>(g.pick(12))); };
        for (auto setup:probe::slot2_variants({}))
            for (const auto r:probe::r_wrap_values(40)) {
                if (++skip%3) continue;      // every third value: the counter's wrap is met all the same
                setup.r=r; setup.iff1=setup.iff2=(g.seed&1);
                g.run(entry,setup,make_world(),frame.held,regs);
            }
        if (!frame.held.empty()) continue;      // the held words sit in work RAM above the placements below
        for (const auto& placement:probe::stack_cases())
            for (unsigned k=0;k<3;++k) {
                probe::Setup setup; setup.sp=placement.sp; setup.cartridge_ram=placement.cartridge_ram;
                setup.work_ram_disabled=placement.work_ram_disabled; setup.iff1=setup.iff2=(g.seed&1); setup.r=g.byte();
                if (setup.cartridge_ram) setup.banks[2]=3;
                auto machine=g.make(entry,setup,make_world(),{},regs);
                if (!placement.admitted(scratch)) { g.harness.run_fallback(machine,entry,RoutineFallback::unmapped_stack); ++g.fallbacks; continue; }
                const auto bound=g.harness.routine(entry).bound(machine);
                if (bound>Machine::frame_cycles) {
                    check(setup.work_ram_disabled,"a terrain segment refused an admitted stack placement");
                    g.harness.run_fallback(machine,entry,RoutineFallback::frame); ++g.fallbacks; continue;
                }
                const auto result=g.harness.run_cpp(machine,entry);
                if (!setup.work_ram_disabled) check(result.cycles==bound,"a terrain segment cost other than its bound on a placed stack");
                ++g.count;
            }
        // The pushes land on the variables the routine reads afterwards.
        for (const std::uint16_t sp:std::vector<std::uint16_t>{static_cast<std::uint16_t>(map::camera_y+2),static_cast<std::uint16_t>(map::camera_x+4),
                                     static_cast<std::uint16_t>(map::transport+3),static_cast<std::uint16_t>(tile_memo+2),0xC005,0xCC10})
        {
            probe::Setup setup; setup.sp=sp; setup.r=g.byte();
            g.run(entry,setup,make_world(),{},regs);
        }
    }
}

// ---- Scenario probes: the real map ---------------------------------------------------------------
// Routines on beside routines off, every frame compared. The recording leaves
// the party on the map; the transport kind, the tile numbers around the party,
// the area and the camera are then edited and the party walks in every
// direction.
bool scenario_probes(const map_probe::Inputs& inputs) {
    const std::string stem=inputs.version=="us" ? "vehicles" : "later-town";
    const auto recording_path=inputs.recording(stem);
    if (inputs.recordings.empty() || !std::filesystem::exists(recording_path)) {
        std::cout<<"skipped: the scenario probes need the recordings folder\n"; return false;
    }
    const auto recording=replay::read(recording_path);
    const auto image=map_probe::start_image(inputs,stem,recording);
    if (!image) { std::cout<<"skipped: the scenario probes need the reference folder\n"; return false; }
    std::mt19937_64 rng(11);
    unsigned total_cpp=0, total_fallback=0;
    for (const unsigned kind:{0u,4u,8u,0x0Cu,0x10u,0x11u}) {
        map_probe::Scenario s(inputs.rom,*image,"terrain-transport",{});
        std::uint64_t from=0;
        check(s.play(recording,from,map_probe::on_map),"the recording never reaches the map");
        s.frames({},30);
        s.poke(map::transport,static_cast<std::uint8_t>(kind));
        for (const unsigned area:{2u,5u}) {
            s.poke(map::area,static_cast<std::uint8_t>(area));
            // Tiles around the party: ice, broken ice, open ground, a mixture.
            for (const unsigned fill:{0u,1u,2u,3u}) {
                for (unsigned i=0;i<0x400;++i) {
                    const std::uint8_t tile=fill==0 ? static_cast<std::uint8_t>(0xD8+(i&7)) : fill==1 ? 0xD7 :
                        fill==2 ? 0x00 : static_cast<std::uint8_t>(rng()%4==0 ? 0xD8+(rng()&7) : rng());
                    s.poke(static_cast<std::uint16_t>(map::screen_window+i),tile);
                }
                for (unsigned step=0;step<4;++step) {
                    Pad pad;
                    switch (rng()%4) { case 0: pad.up=true; break; case 1: pad.down=true; break; case 2: pad.left=true; break; default: pad.right=true; }
                    FrameInput input; input.pads[0]=pad;
                    s.frames(input,24);
                    s.frames({},4);
                }
            }
        }
        const auto [cpp,fallbacks]=s.calls(part);
        total_cpp+=static_cast<unsigned>(cpp); total_fallback+=static_cast<unsigned>(fallbacks);
    }
    std::cout<<"scenario probes: "<<total_cpp<<" C++ calls, "<<total_fallback<<" fallbacks in terrain segments\n";
    check(total_cpp>0,"the scenario probes reached no terrain segment");
    return true;
}

void probes(const map_probe::Inputs& inputs) {
    Rig rig(inputs.rom);
    tile_attribute_probes(rig);
    blocked_probes(rig);
    ice_probes(rig);
    vehicle_probes(rig);
    const auto isolated=rig.count;
    flow_probes(rig);
    environment_probes(rig);
    std::cout<<isolated<<" state probes, "<<rig.flows<<" whole flows, "<<rig.count-isolated<<" environment probes; "<<rig.fallbacks<<" intended fallbacks\n";
    // Every planned entry must have run.
    for (const auto& segment:map::terrain_segments)
        check(rig.per_entry[segment.entry]>0,"a planned terrain entry was never run in C++ by the probes");
    if (const char* file=std::getenv("PSMODERN_TERRAIN_COVERAGE")) {
        std::ofstream out(file);
        for (const auto address:rig.covered) out<<std::hex<<address<<'\n';
    }
}
}

int main(int argc, char** argv) {
    try {
        registration();
        if (argc==1) return 0;
        const auto inputs=map_probe::load(argc,argv);
        if (!inputs) return 77;
        probes(*inputs);
        if (!scenario_probes(*inputs)) return 77;
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
