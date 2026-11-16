// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 11, part modes: the map loop, map loading and the road.
//   no arguments                       registration catalog (no ROM)
//   <rom> <us|br> [<recordings folder> [<reference folder>]]
//                                      state probes and scenario probes on
//                                      that ROM (tests/map_probe.h)
// A ROM run exits 77 without the ROM, or when the scenario part needed the
// later-town start image and could not find it.
//
// What the ROM run checks, every probe dispatched once with the per-call
// verifier (the original runs first and the whole machine is compared):
//   - every registered entry, on each path through it, runs in C++ at exactly
//     the cost its bound announced, with the banks of slot 2, the refresh
//     counter and the stack placements the milestone's review asked for varied
//     (a placement the scratch bytes do not fit makes the entry fall back);
//   - map loading at all 24 locations, on foot and in each vehicle, and the six
//     legs of the road, as scenarios: routines on (every C++ call verified)
//     beside routines off, the seven manifest components compared every frame.
#include "game/map_kit.h"
#include "map_probe.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <random>
#include <set>

using namespace psmodern;
using probe::check;
namespace map=game::map;
namespace mk=game::map_kit;
namespace {
constexpr map::Part part=map::Part::modes;
constexpr RoutineEntry at(std::uint16_t address) { return {0,address}; }

// ---- Registration catalog --------------------------------------------------------
SegmentExit R(std::uint16_t address) { return mk::returned_at(address); }
SegmentExit F(std::uint16_t address, std::int8_t stack=0) { return mk::core_final_at(address,stack); }

struct Want {
    std::uint16_t entry, unit;
    std::vector<SegmentExit> exits;
    mk::Permissions permissions={};
};
std::vector<Want> wanted() {
    return {
        {0x0B08,0x0B08,{F(0x0B0C)}},{0x0B0F,0x0B08,{F(0x0B11)}},
        {0x0B1A,0x0B08,{F(0x0B2A),R(0x0B3A),R(0x0B3F),R(0x0B69)}},{0x0B2D,0x0B08,{R(0x0B3A),R(0x0B3F),R(0x0B69)}},
        {0x0B75,0x0B6A,{F(0x0B93),F(0x0BA3)},{.slot2_mapper=true}},{0x0B96,0x0B6A,{F(0x0BA6)}},
        {0x0BAC,0x0B6A,{F(0x0BB1,-2)},{.scratch_stack=2}},{0x0BBA,0x0B6A,{F(0x0BB1,-2),F(0x0C5D)},{.scratch_stack=4}},
        {0x0C63,0x0B6A,{F(0x0C65)}},{0x0C68,0x0B6A,{F(0x0C8B)}},
        {0x0C97,0x0C97,{R(0x0C9F),R(0x0CA5)},{.scratch_stack=2}},
        {0x0CA6,0x0CA6,{F(0x0CAC)}},{0x0CB3,0x0CA6,{R(0x0CBF),F(0x0CCB)}},{0x0CCE,0x0CA6,{F(0x0CD3)}},{0x0CD6,0x0CA6,{F(0x0CDB)}},
        {0x0CDE,0x0CA6,{R(0x0CE5),R(0x0CEA)}},{0x0CE3,0x0CA6,{R(0x0CE5),R(0x0CEA)}},
        {0x0306,0x02FD,{F(0x030A)}},                       // planned as 00:02FD ending at 00:0312, see map_modes.cpp
        {0x0E8B,0x0E8B,{F(0x0E8F)}},{0x0E92,0x0E8B,{F(0x0E94)}},
        {0x0E97,0x0E8B,{F(0x0EA4),F(0x0ED3)}},             // the button press leaves the road at once
        {0x0EA7,0x0E8B,{R(0x0EAB),R(0x0EB3),F(0x0ED3)}},
        {0x0ED7,0x0ED7,{F(0x0EF2,-2)},{.scratch_stack=2}},{0x0EF8,0x0ED7,{R(0x0F0B)}},
    };
}
void registration() {
    const auto entries=map_probe::registered_entries(part,game::register_map_modes);
    const auto planned=map::planned_segments(part);
    RoutineRegistry registry;
    game::register_map_modes(registry);
    const auto want=wanted();
    check(want.size()==registry.entries().size() && entries.size()==want.size(),"24 modes segments are registered");
    // Every planned entry is registered (the sprite tables start at 00:0306, not 00:02FD).
    for (const auto& segment:planned)
        check(registry.find(segment.entry)!=nullptr,"a planned modes entry is registered");
    for (const auto& w:want) {
        const RoutineEntry entry=at(w.entry), unit=at(w.unit);
        const auto* routine=registry.find(entry);
        check(routine!=nullptr,"the entry is registered");
        const auto& spec=routine->spec;
        check(spec.unit==unit && spec.slot2_mapper==w.permissions.slot2_mapper && spec.scratch_stack==w.permissions.scratch_stack &&
              spec.inherited_stack==0 && !spec.single_instruction && !spec.force_admit && !spec.absolute_wram_store,
              "the entry has the unit and permissions of the table");
        check(spec.exits.size()==w.exits.size(),"the entry declares the exits of the table");
        for (const auto& exit:w.exits)
            check(std::count_if(spec.exits.begin(),spec.exits.end(),[&](const auto& e) { return e.same_declaration(exit); })==1,
                  "an exit of the table is declared exactly as written");
        // 00:0B14 is never an entry or an exit, and no entry lies in a stretch that stays native.
        check(entry!=map::options_boundary,"00:0B14 stays native");
        for (const auto& exit:spec.exits) check(exit.address!=map::options_boundary.address,"no exit at 00:0B14");
    }
    std::cout<<entries.size()<<" of "<<planned.size()<<" planned modes segments registered; "
             <<"every entry has the exits and permissions of the table.\n";
}

// ---- Probes on a ROM ------------------------------------------------------------------
// The game's work RAM the segments read or write.
constexpr std::uint16_t paused=0xC212, held=0xC204, pressed=0xC205, busy=0xC265, battle_due=0xC2D2,
    area=0xC308, location=0xC309, transport=0xC30E, tune=0xC2F4, road_index=0xC2E9, road_buttons=0xC2EA,
    road_count=0xC2EB;
constexpr std::array<std::uint16_t,4> party_records{0xC400,0xC430,0xC420,0xC410};   // Alis, Noah, Odin, Myau

using Prepare=std::function<void(Machine&)>;
void poke(Machine& m, std::uint16_t address, std::uint8_t value) { m.memory().write(address,value); }
void set_a(Machine& m, std::uint8_t v) { m.cpu().af.uint8_values.at_1=v; }
void set_b(Machine& m, std::uint8_t v) { m.cpu().bc.uint8_values.at_1=v; }
void set_hl(Machine& m, std::uint16_t v) { m.cpu().hl.uint16_value=v; }
void set_de(Machine& m, std::uint16_t v) { m.cpu().de.uint16_value=v; }
void set_bc(Machine& m, std::uint16_t v) { m.cpu().bc.uint16_value=v; }

struct Rig {
    probe::Harness harness;
    std::mt19937_64 random{0x40DE5};
    std::uint64_t seed=1, count=0, fallbacks=0;
    std::map<RoutineEntry,std::uint64_t> per_entry;
    explicit Rig(const probe::Rom& rom) : harness(rom) {}

    std::uint8_t byte() { return static_cast<std::uint8_t>(random()); }
    // The entry's machine: the work RAM the game keeps in C200h-C6FFh filled
    // with noise (so nothing hides behind a zero), then the probe's own state.
    Machine make(RoutineEntry entry, probe::Setup setup, const Prepare& prepare) {
        setup.seed=seed++;
        auto machine=harness.prepare(entry,setup);
        if (!setup.work_ram_disabled)
            for (unsigned address=0xC200;address<0xC700;++address) machine.memory().write(static_cast<std::uint16_t>(address),byte());
        if (prepare) prepare(machine);
        // The stack word the entry returns to (the fill above may have covered it).
        return machine;
    }
    std::uint64_t run(RoutineEntry entry, probe::Setup setup, const Prepare& prepare={}) {
        auto machine=make(entry,setup,prepare);
        const auto bound=harness.routine(entry).bound(machine);
        if (bound>Machine::frame_cycles) {
            // A state the estimate cannot follow: work RAM off (an open-bus read) or a stack that does
            // not give back what was pushed. The call then runs natively.
            check(setup.work_ram_disabled || setup.sp>=0xFFFC-4 || setup.sp<0xC000,"entry "+std::to_string(entry.address)+" refused a state with a working stack (bound "+std::to_string(bound)+")");
            harness.run_fallback(machine,entry,RoutineFallback::frame);
            ++fallbacks;
            return 0;
        }
        const auto result=harness.run_cpp(machine,entry);
        ++count; ++per_entry[entry];
        if (!setup.work_ram_disabled)
            check(result.cycles==bound,"entry "+std::to_string(entry.address)+" cost "+std::to_string(result.cycles)+
                  " cycles, its bound said "+std::to_string(bound));
        return result.fetches;
    }
    std::uint64_t run(RoutineEntry entry, const Prepare& prepare={}) {
        probe::Setup setup;
        setup.iff1=setup.iff2=(seed&1);
        setup.r=byte();
        return run(entry,setup,prepare);
    }
    void fallback(RoutineEntry entry, probe::Setup setup, const Prepare& prepare) {
        auto machine=make(entry,setup,prepare);
        harness.run_fallback(machine,entry,RoutineFallback::unmapped_stack);
        ++fallbacks;
    }
};

// The paths of each entry: a list of RAM and register states.
std::vector<Prepare> paths(Rig& g, RoutineEntry entry, const probe::Rom& rom) {
    std::vector<Prepare> list;
    auto add=[&](Prepare p) { list.push_back(std::move(p)); };
    const auto a=entry.address;
    switch (a) {
    case 0x0B08: case 0x0E8B:
        for (const std::uint8_t v:{0,1,0x80}) add([=](Machine& m) { poke(m,paused,v); });
        break;
    case 0x0B0F: case 0x0B96: case 0x0BAC: case 0x0C63: case 0x0E92: case 0x0EF8: case 0x0306:
        // The two fills run with the count the code before them loaded; random BC would copy for seconds.
        if (a!=0x0EF8 && a!=0x0306) add({});
        if (a==0x0306) add([&](Machine& m) { set_hl(m,0xC900); set_de(m,0xC901); set_bc(m,0x40); });
        if (a==0x0EF8) add([&](Machine& m) { set_hl(m,0xC26F); set_de(m,0xC270); set_bc(m,0x17); });
        break;
    case 0x0B1A:   // busy, battle due, buttons held
        for (const std::uint8_t b:{0,1,0x80}) for (const std::uint8_t due:{0,1,0xFF}) for (const std::uint8_t h:{0,0x10,0x20,0x30,0x0F,0xCF})
            add([=](Machine& m) { poke(m,busy,b); poke(m,battle_due,due); poke(m,held,h); });
        break;
    case 0x0B2D:   // the battle check's result, busy, buttons held
        for (const std::uint8_t result:{0,1,0xFF}) for (const std::uint8_t b:{0,1}) for (const std::uint8_t h:{0,0x10,0x20,0x30,0x0F}) {
            add([=](Machine& m) { set_a(m,result); poke(m,busy,b); poke(m,held,h); });
            add([=](Machine& m) { set_a(m,result); poke(m,busy,b); poke(m,held,h); m.cpu().af.uint8_values.at_0=0xFF; });
        }
        break;
    case 0x0B75:   // the area picks the tile set
        for (const std::uint8_t ar:{0,1,3,4,5,9,0xFF}) {
            add([=](Machine& m) { set_hl(m,map::game_mode); poke(m,area,ar); });
            add([=](Machine& m) { set_hl(m,0xC2D0); poke(m,area,ar); });
        }
        break;
    case 0x0BBA:   // the frame counter, the location and the transport
        for (const std::uint8_t b:{0,2,4,0xFF}) add([=](Machine& m) { set_b(m,b); });
        for (unsigned loc=0;loc<24;++loc) for (const std::uint8_t t:{0,4,8,0x0C,0x10,0x11})
            add([=](Machine& m) { set_b(m,1); poke(m,location,static_cast<std::uint8_t>(loc)); poke(m,transport,t); });
        for (unsigned k=0;k<24;++k) {   // beyond the table: whatever the ROM holds there
            const auto loc=static_cast<std::uint8_t>(24+g.byte()%232);
            add([=](Machine& m) { set_b(m,1); poke(m,location,loc); poke(m,transport,0); });
        }
        break;
    case 0x0C68:   // the tile's interaction type, the transport and the location
        for (const std::uint8_t t:{0,1,4,8,0x0C,0x0F,0x10,0x11,0xFF})
            for (unsigned loc=0;loc<24;++loc)
                add([=](Machine& m) { set_a(m,static_cast<std::uint8_t>(0x35*loc+t)); poke(m,transport,t); poke(m,location,static_cast<std::uint8_t>(loc)); });
        break;
    case 0x0C97:   // the tune: the same one again, or a new one
        for (const std::uint8_t cur:{0,0x8F,0xB8,0x55}) for (const std::uint8_t req:{0,0x8F,0xB8,0x55,0xFF})
            add([=](Machine& m) { set_a(m,req); poke(m,tune,cur); poke(m,0xC004,0x11); set_hl(m,0xC300); });
        break;
    case 0x0CA6:
        add({});
        break;
    case 0x0CB3:   // on foot: which of the four characters are in the party; on a vehicle
        for (unsigned bits=0;bits<16;++bits)
            add([=](Machine& m) {
                poke(m,transport,0);
                for (unsigned i=0;i<4;++i) poke(m,party_records[i],static_cast<std::uint8_t>((bits>>i&1) ? 0xFF : 0xFE));
            });
        for (const std::uint8_t t:{4,8,0x0C,0x10,0x11}) add([=](Machine& m) { poke(m,transport,t); });
        break;
    case 0x0CCE: case 0x0CD6: case 0x0CDE: case 0x0CE3:
        for (const std::uint8_t record:{0,1,0xFE,0xFF,0x80}) for (const std::uint8_t slot:{0,1,0x55}) {
            const std::uint16_t hl=(a==0x0CCE) ? party_records[1] : (a==0x0CD6) ? party_records[2] : party_records[3];
            add([=](Machine& m) {
                set_hl(m,hl); poke(m,hl,record); set_de(m,static_cast<std::uint16_t>(0xC800+0x40*(slot&3))); set_bc(m,0x20);
                set_a(m,slot); m.cpu().af.uint8_values.at_0=slot;
            });
        }
        break;
    case 0x0E97:   // the new buttons, and the scripted ones
        for (const std::uint8_t p:{0,0x10,0x20,0x30,0x0F,0xCF}) add([=](Machine& m) { poke(m,pressed,p); poke(m,road_buttons,0x2A); });
        break;
    case 0x0EA7:   // busy, the leg's countdown, the road index
        for (const std::uint8_t b:{0,1}) for (const std::uint8_t n:{0,1,2,0xFF}) for (unsigned idx=0;idx<8;++idx)
            add([=](Machine& m) { poke(m,busy,b); poke(m,road_count,n); poke(m,road_index,static_cast<std::uint8_t>(idx)); });
        break;
    case 0x0ED7:   // the road index picks the leg
        for (unsigned idx=0;idx<8;++idx) add([=](Machine& m) { poke(m,road_index,static_cast<std::uint8_t>(idx)); });
        add([=](Machine& m) { poke(m,road_index,0xFF); });
        add([=](Machine& m) { poke(m,road_index,0x20); });
        break;
    default: check(false,"entry without probe paths"); break;
    }
    (void)rom;
    return list;
}

// Every path, then the paths under each slot-2 bank and refresh-counter placement and under each stack placement.
void probe_entry(Rig& g, RoutineEntry entry, const probe::Rom& rom) {
    const auto list=paths(g,entry,rom);
    const auto scratch=g.harness.routine(entry).spec.scratch_stack;
    unsigned fetches=0;
    for (const auto& prepare:list) fetches=std::max<unsigned>(fetches,static_cast<unsigned>(g.run(entry,prepare)));
    // Every path again with its own random registers and interrupt flag.
    for (const auto& prepare:list) g.run(entry,prepare);
    // Slot 2 on a few of the paths, and the refresh counter around its 7-bit wrap.
    const auto few=std::min<std::size_t>(list.size(),3);
    for (unsigned n=0;n<few;++n) {
        const auto& prepare=list[n*list.size()/few];
        for (auto setup:probe::slot2_variants({}))
            for (const std::uint8_t r:{0x00,0x7F,0xFE,0x85}) {
                setup.r=r; setup.iff1=setup.iff2=(g.seed&1);
                g.run(entry,setup,prepare);
            }
        if (n>=2) continue;
        for (const auto r:probe::r_wrap_values(fetches)) {
            probe::Setup setup; setup.r=r; setup.iff1=setup.iff2=(g.seed&1);
            g.run(entry,setup,prepare);
        }
    }
    // The stack placements.
    for (const auto& placement:probe::stack_cases())
        for (unsigned k=0;k<3;++k) {
            probe::Setup setup; setup.sp=placement.sp; setup.cartridge_ram=placement.cartridge_ram;
            setup.work_ram_disabled=placement.work_ram_disabled; setup.iff1=setup.iff2=(g.seed&1);
            setup.r=g.byte();
            if (setup.cartridge_ram) setup.banks[2]=3;
            const auto& prepare=list[g.byte()%list.size()];
            if (!placement.admitted(scratch)) { g.fallback(entry,setup,prepare); continue; }
            g.run(entry,setup,prepare);
        }
}

void state_probes(const map_probe::Inputs& inputs) {
    Rig g(inputs.rom);
    for (unsigned moving:{0u,1u,255u}) for (unsigned remaining:{1u,64u,1000u,59000u}) {
        auto actual=g.harness.prepare(at(0x0B3B),{.seed=moving+remaining});
        actual.memory().write(busy,static_cast<std::uint8_t>(moving));
        actual.memory().write(0xC810,0x91);
        actual.memory().write(0xC29D,0xAA);
        actual.memory().write(0xC202,9);
        const auto phase=actual.cycles()%Machine::frame_cycles;
        actual.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
        auto native=actual;
        for (unsigned steps=0;actual.cpu().pc.uint16_value!=0x0180;++steps) {
            check(steps<2000,"button-test continuation returned");
            actual.step(g.harness.registry());
            while (native.cycles()<actual.cycles()) native.step();
            check(native.cycles()==actual.cycles(),"button-test continuation cycle boundary");
        }
        check(native.equivalent(actual),"resumed walking-mode exit agrees with original");
        check(actual.peek(0xC202)==(moving ? 9 : 12) && actual.peek(0xC29D)==(moving ? 0xAA : 0),
              "only a stationary party leaves walking mode");
        if (!moving) {
            check(actual.peek(0xC2D7)==0x91,"leader type saved before object clear");
            for (unsigned i=0;i<256;++i) check(actual.peek(0xC800+i)==0,"cleared object slot");
            for (unsigned i=0;i<24;++i) check(actual.peek(0xC26F+i)==0,"cleared animation record");
        }
    }
    for (const auto& w:wanted()) probe_entry(g,at(w.entry),inputs.rom);
    for (auto entry:game::map_button_resume_entries)
        check(g.harness.routine(entry).counters.cpp_calls>0,"button-test resumption ran in C++");
    for (auto entry:game::map_exit_copies)
        check(g.harness.routine(entry).counters.cpp_calls>0,"map exit fill ran in C++");
    std::uint64_t cpp=0;
    for (const auto& [entry,n]:g.per_entry) cpp+=n;
    check(g.per_entry.size()==wanted().size(),"every modes entry was probed in C++");
    std::cout<<"state probes ("<<inputs.version<<"): "<<cpp<<" C++ probes over "<<g.per_entry.size()<<" entries, "
             <<g.fallbacks<<" expected fallbacks; all equal to the original.\n";
}

// ---- Scenario probes --------------------------------------------------------------------
// The later-town image is a saved game standing on a town map. A scenario plays
// it to a map frame, sets the state a path needs in both machines and runs on.
struct Later { replay::Recording recording; map_probe::StartImage image; };
std::optional<Later> later_town(const map_probe::Inputs& inputs) {
    if (inputs.recordings.empty()) return std::nullopt;
    auto recording=replay::read(inputs.recording("later-town"));
    check(recording.rom_sha1==inputs.rom.sha1,"later-town was recorded with another ROM");
    auto image=map_probe::start_image(inputs,"later-town",recording);
    if (!image) return std::nullopt;
    return Later{std::move(recording),*image};
}
// Runs until the mode byte reads `mode` after a frame (limit frames), returns whether it did.
bool run_until_mode(map_probe::Scenario& s, std::uint8_t mode, unsigned limit) {
    for (unsigned n=0;n<limit;++n) { s.frames({},1); if (s.peek(map::game_mode)==mode) return true; }
    return false;
}

// One load of a map: the location (and the transport) poked on a map frame, the
// mode set to "load a map", run until the map is walking again.
void load_scenario(const map_probe::Scenario& start, unsigned loc, std::uint8_t carrier,
                   std::uint64_t& cpp_calls, std::uint64_t& fallback_calls) {
    map_probe::Scenario s(start,"load location "+std::to_string(loc)+" transport "+std::to_string(carrier));
    s.poke(map::location,static_cast<std::uint8_t>(loc));
    s.poke(map::transport,carrier);
    s.poke(map::game_mode,map::load_map_mode);
    check(run_until_mode(s,map::map_mode,900),"map loading ends in map mode (location "+std::to_string(loc)+")");
    s.frames({},6);
    const auto [cpp,fall]=s.calls(part);
    cpp_calls+=cpp; fallback_calls+=fall;
    check(s.counters(at(0x0B75)).cpp_calls+s.counters(at(0x0B75)).fallback_calls>=1,"the load ran 00:0B75");
}
void load_probes(const map_probe::Inputs& inputs, const map_probe::Scenario& start) {
    std::uint64_t cpp=0, fallback=0;
    for (unsigned loc=0;loc<24;++loc) load_scenario(start,loc,0,cpp,fallback);
    for (const std::uint8_t carrier:{4,8,0x0C,0x10,0x11}) load_scenario(start,2,carrier,cpp,fallback);
    std::cout<<"map loading ("<<inputs.version<<"): 24 locations on foot and 5 loads in vehicles and ship, routines on and off equal "
             <<"every frame ("<<cpp<<" C++ calls, "<<fallback<<" fallbacks).\n";
}

// The road: the six legs, set up the way the transfer code does (the index and
// the road's load mode); run until the leg has played and the map walks again.
void road_scenario(const map_probe::Scenario& start, unsigned index, std::uint64_t& cpp_calls) {
    map_probe::Scenario s(start,"road leg "+std::to_string(index));
    s.poke(road_index,static_cast<std::uint8_t>(index));
    s.poke(map::game_mode,map::load_road_mode);
    bool walked=false;
    for (unsigned n=0;n<1500 && !(walked && s.peek(map::game_mode)==map::map_mode);++n) {
        s.frames({},1);
        walked=walked || s.peek(map::game_mode)==map::road_mode;
    }
    check(walked,"the road mode was reached (leg "+std::to_string(index)+")");
    check(s.peek(map::game_mode)==map::map_mode,"the road leg ended in the map (leg "+std::to_string(index)+")");
    s.frames({},10);
    cpp_calls+=s.counters(at(0x0ED7)).cpp_calls+s.counters(at(0x0E8B)).cpp_calls;
}
void road_probes(const map_probe::Inputs& inputs, const map_probe::Scenario& start) {
    std::uint64_t cpp=0;
    for (unsigned index=1;index<=6;++index) road_scenario(start,index,cpp);
    check(cpp>0,"the road ran in C++");
    std::cout<<"road ("<<inputs.version<<"): six legs, routines on and off equal every frame ("<<cpp<<" calls of 00:0ED7 and 00:0E8B in C++).\n";
}

bool scenario_probes(const map_probe::Inputs& inputs) {
    const auto later=later_town(inputs);
    if (!later) { std::cout<<"later-town start image missing (reference folder not configured), scenario probes skipped.\n"; return false; }
    map_probe::Scenario start(inputs.rom,later->image,"later-town prefix");
    std::uint64_t from=0;
    check(start.play(later->recording,from,map_probe::on_map),"later-town reaches a map frame");
    start.frames({},2);
    load_probes(inputs,start);
    road_probes(inputs,start);
    return true;
}
}

int main(int argc, char** argv) {
    try {
        registration();
        if (argc==1) return 0;
        const auto inputs=map_probe::load(argc,argv);
        if (!inputs) return 77;
        state_probes(*inputs);
        return scenario_probes(*inputs) ? 0 : 77;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
