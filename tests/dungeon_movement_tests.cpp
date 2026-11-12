// SPDX-License-Identifier: GPL-3.0-or-later
// The dungeon handler with its pitfall, steps and turns (milestone 7, part R2).
// Without arguments: the registration catalog, needing no ROM (the 26 entries,
// their units, exits and permissions). With <rom> <us|br>: state probes of
// every segment against the original, each dispatched once with the per-call
// verifier and required to run in C++ (exit 77 without the ROM).
#include "game/dungeon_kit.h"
#include "game/dungeon_tables.h"
#include "probe.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace psmodern;
using probe::check;
namespace dungeon=game::dungeon;
namespace kit=game::kit;
namespace {
// ---- Registration catalog ------------------------------------------------------------

SegmentExit R(std::uint16_t address) { return kit::returned_at(address); }
SegmentExit F(std::uint16_t address, std::int8_t stack=0) { return kit::core_final_at(address,stack); }
SegmentExit X(std::uint16_t address) { return kit::conditional_at(address); }
SegmentExit L(std::uint16_t address, std::uint16_t after) { return kit::loop_at(address,after); }

struct Want {
    std::uint16_t entry, unit;
    std::vector<SegmentExit> exits;
    kit::Permissions permissions={};
};
std::vector<Want> wanted() {
    const std::vector<SegmentExit> handler{R(0x6807),R(0x672B),R(0x67D9),L(0x6618,128),F(0x6622),X(0x6712),X(0x672E),
        F(0x6755),F(0x675A),F(0x6785),F(0x67AF),F(0x67DA),F(0x6813),F(0x6817)};
    return {
        {0x65EE,0x65EE,handler,{.slot2_mapper=true,.scratch_stack=4}},{0x6619,0x65EE,handler,{.scratch_stack=4}},
        {0x6715,0x65EE,{F(0x6723)}},{0x6734,0x65EE,{F(0x674A)}},{0x6750,0x65EE,{F(0x6752)}},
        {0x6788,0x65EE,{F(0x678A)}},{0x678D,0x65EE,{F(0x678F)}},{0x67B2,0x65EE,{F(0x67B4)}},{0x67DD,0x65EE,{F(0x67DF)}},
        {0x661C,0x661C,{F(0x6622)}},{0x662C,0x661C,{F(0x6640,-2)},{.scratch_stack=2}},{0x6643,0x661C,{F(0x6657)}},
        {0x6660,0x661C,{F(0x6640,-2),F(0x666D)},{.scratch_stack=2}},{0x6670,0x661C,{F(0x6671)}},{0x6674,0x661C,{F(0x6681)}},
        {0x6684,0x661C,{F(0x6696,-2)},{.scratch_stack=2}},{0x6699,0x661C,{F(0x66AE)}},
        {0x66B7,0x661C,{F(0x6696,-2),F(0x66C9)},{.scratch_stack=2}},
        {0x66CC,0x661C,{F(0x66C9),F(0x66D9)},{.slot2_mapper=true}},{0x66DC,0x661C,{F(0x66DE)}},
        {0x6758,0x6758,{F(0x675A)}},{0x675D,0x6758,{F(0x6772)}},{0x6775,0x6758,{F(0x6777)}},
        {0x6792,0x6792,{F(0x67A8)}},{0x67B7,0x67B7,{F(0x67D4)},{.scratch_stack=4}},{0x67E2,0x67E2,{F(0x67FF)},{.scratch_stack=4}},
    };
}
void registration() {
    RoutineRegistry registry;
    game::register_dungeon_movement(registry);
    const auto want=wanted();
    check(want.size()==dungeon::movement_segments.size() && registry.entries().size()==want.size(),"26 movement segments are registered");
    for (const auto& w:want) {
        const RoutineEntry entry{1,w.entry}, unit{1,w.unit};
        const auto listed=std::find_if(dungeon::movement_segments.begin(),dungeon::movement_segments.end(),
                                       [&](const auto& s) { return s.entry==entry; });
        check(listed!=dungeon::movement_segments.end() && listed->unit==unit,"the entry is in the planned list with its unit");
        const auto* routine=registry.find(entry);
        check(routine!=nullptr,"the planned entry is registered");
        const auto& spec=routine->spec;
        check(spec.unit==unit && spec.slot2_mapper==w.permissions.slot2_mapper && spec.scratch_stack==w.permissions.scratch_stack &&
              spec.inherited_stack==0 && !spec.single_instruction && !spec.force_admit && !spec.absolute_wram_store,
              "the entry has the unit and permissions of the R2 table");
        check(spec.exits.size()==w.exits.size(),"the entry declares the exits of the R2 table");
        for (const auto& exit:w.exits)
            check(std::count_if(spec.exits.begin(),spec.exits.end(),[&](const auto& e) { return e.same_declaration(exit); })==1,
                  "an exit of the R2 table is declared exactly as written");
    }
    std::cout<<"26 movement segments registered with the exits and permissions of the R2 table.\n";
}

// ---- ROM probes ------------------------------------------------------------------------

// The verifier of the harness, wrapped to learn which exit each call took.
struct Spy final : RoutineVerifier {
    replay::CallVerifier inner{"probe"};
    std::optional<SegmentExit> last;
    void run(Machine& machine, RoutineEntry entry, const SegmentSpec& spec, bool forced, std::uint64_t bound,
             std::uint64_t call, const std::function<std::optional<SegmentExit>()>& body) override {
        inner.run(machine,entry,spec,forced,bound,call,[&] { last=body(); return last; });
    }
};

constexpr std::uint16_t ctrl_held=0xC204, ctrl_pressed=0xC205, scroll=0xC304;
// The game's work RAM that the segments read or write, plus bytes around it
// that nothing should depend on (the light, pending bytes, the sound request).
constexpr std::array<std::pair<std::uint16_t,std::uint16_t>,5> noise_ranges{{{0xC000,0xC010},{0xC200,0xC260},{0xC300,0xC340},{0xC500,0xC520},{0xC6C0,0xC6E0}}};

struct Scene {
    std::uint8_t floor=0, position=0, facing=0, held=0, pressed=0;
    std::array<std::uint8_t,256> layout{};
};

struct Rig {
    probe::Harness harness;
    Spy spy;
    const probe::Rom& rom;
    game::DungeonTables tables;
    std::mt19937_64 random{0x5A17};
    std::uint64_t seed=1, count=0, fallbacks=0, refusals=0;
    std::map<RoutineEntry,std::uint64_t> per_entry;
    std::map<RoutineEntry,std::map<std::uint16_t,std::set<std::uint16_t>>> reached;   // entry -> exit -> PCs after it

    // The four bytes below SP (a call's return address and a pushed HL) read back what was written.
    static bool stack_holds(const probe::Setup& setup) {
        for (unsigned i=1;i<=4;++i) {
            const auto address=static_cast<std::uint16_t>(setup.sp-i);
            if (address>=0xFFFC) return false;
            if (address>=0xC000) { if (setup.work_ram_disabled) return false; }
            else if (address<0x8000 || !setup.cartridge_ram) return false;
        }
        return true;
    }
    explicit Rig(const probe::Rom& r) : harness(r), rom(r), tables(*r.image) { harness.registry().verifier=&spy; }

    std::uint8_t byte() { return static_cast<std::uint8_t>(random()); }
    unsigned below(unsigned n) { return static_cast<unsigned>(random()%n); }
    // The cell index at view offset B from a position and facing, read from the tables.
    std::uint8_t cell(std::uint8_t position, std::uint8_t facing, unsigned index) const {
        return static_cast<std::uint8_t>(position+tables.offset(facing&3,index));
    }
    Scene random_scene() {
        Scene s;
        s.floor=byte(); s.position=byte(); s.facing=byte(); s.held=byte(); s.pressed=byte();
        for (auto& c:s.layout) c=byte();
        return s;
    }
    std::uint8_t edge_position() {
        static constexpr std::uint8_t edges[]{0x00,0x01,0x0E,0x0F,0x10,0x11,0x1F,0xE0,0xEF,0xF0,0xF1,0xFE,0xFF};
        return edges[below(std::size(edges))];
    }
    void write_scene(Machine& machine, const Scene& s) {
        auto& memory=machine.memory();
        for (const auto& [low,high]:noise_ranges)
            for (auto a=low;a<high;++a) memory.write(a,byte());
        memory.write(dungeon::facing,s.facing); memory.write(dungeon::position,s.position); memory.write(dungeon::floor,s.floor);
        memory.write(ctrl_held,s.held); memory.write(ctrl_pressed,s.pressed);
        for (unsigned i=0;i<256;++i) memory.write(static_cast<std::uint16_t>(dungeon::layout+i),s.layout[i]);
    }
    using Prepare=std::function<void(Machine&)>;
    Machine make(RoutineEntry entry, probe::Setup setup, const Scene& scene, const Prepare& prepare) {
        setup.seed=seed++;
        auto machine=harness.prepare(entry,setup);
        auto& memory=machine.memory();
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()&~0x10);
        write_scene(machine,scene);
        if (prepare) prepare(machine);
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
        return machine;
    }
    // One probe: the call runs in C++ with the exact cost its bound announced
    // (not when work RAM reads the open bus, whose path the estimate cannot
    // know), matching the original in the whole machine. Returns the exit taken.
    std::uint16_t run(RoutineEntry entry, probe::Setup setup, const Scene& scene, const Prepare& prepare={}) {
        auto machine=make(entry,setup,scene,prepare);
        const auto bound=harness.routine(entry).bound(machine);
        if (bound>Machine::frame_cycles) {
            // A state the segment refuses (its inline cell reader would return elsewhere, or the
            // path cannot be estimated): only where the stack gives back nothing it was given,
            // or work RAM is off; the call then runs natively.
            check(!stack_holds(setup) || setup.work_ram_disabled,"a segment refused a state with a working stack");
            harness.run_fallback(machine,entry,RoutineFallback::frame);
            ++refusals;
            return 0;
        }
        spy.last.reset();
        probe::Harness::Result result{};
        try { result=harness.run_cpp(machine,entry); }
        catch (const std::exception& error) {
            std::ostringstream text;
            text<<error.what()<<" [entry "<<std::hex<<entry.address<<" floor "<<+scene.floor<<" position "<<+scene.position<<" facing "<<+scene.facing
                <<" held "<<+scene.held<<" pressed "<<+scene.pressed<<" ahead "<<+scene.layout[cell(scene.position,scene.facing,1)]
                <<" under "<<+scene.layout[scene.position]<<" behind "<<+scene.layout[cell(scene.position,scene.facing,11)]
                <<" sp "<<setup.sp<<" ram_off "<<setup.work_ram_disabled<<" cart "<<setup.cartridge_ram<<" bank2 "<<+setup.banks[2]<<"]";
            throw std::runtime_error(text.str());
        }
        ++count; ++per_entry[entry];
        check(spy.last.has_value(),"the probe's call took no exit");
        if (!setup.work_ram_disabled)
            check(result.cycles==bound,"entry "+std::to_string(entry.address)+" cost "+std::to_string(result.cycles)+" cycles, its bound said "+std::to_string(bound));
        reached[entry][spy.last->address].insert(machine.cpu().pc.uint16_value);
        return spy.last->address;
    }
    std::uint16_t run(RoutineEntry entry, const Scene& scene, const Prepare& prepare={}) {
        probe::Setup setup;
        setup.iff1=setup.iff2=(seed&1);
        setup.r=byte();
        return run(entry,setup,scene,prepare);
    }
    void fallback(RoutineEntry entry, probe::Setup setup, const Scene& scene) {
        auto machine=make(entry,setup,scene,{});
        harness.run_fallback(machine,entry,RoutineFallback::unmapped_stack);
        ++fallbacks;
    }
};

constexpr RoutineEntry at(std::uint16_t address) { return {1,address}; }
const RoutineEntry handler=at(0x65EE), continuation=at(0x6619);
constexpr std::uint16_t objects_start=0xAF5C;

// ---- The handler, the scan and the directions ------------------------------------------

// The cell ahead sweeps all 256 values in every facing with every held bits
// combination; the cell under and the cell behind sweep all 256 values too.
void direction_probes(Rig& g) {
    for (unsigned facing=0;facing<4;++facing)
        for (unsigned value=0;value<256;++value)
            for (unsigned held=0;held<16;++held) {
                auto s=g.random_scene();
                s.facing=static_cast<std::uint8_t>(facing|(g.byte()&0xFC));
                s.held=static_cast<std::uint8_t>(held|(g.byte()&0xF0));
                s.position=(value&3)==0 ? g.edge_position() : g.byte();
                if (s.layout[s.position]==8) s.layout[s.position]=0;
                s.layout[g.cell(s.position,s.facing,dungeon::ahead)]=static_cast<std::uint8_t>(value);
                g.run(handler,s);
            }
    for (unsigned facing=0;facing<4;++facing)
        for (unsigned value=0;value<256;++value) {
            auto s=g.random_scene();                                   // under the party (8 starts the scan)
            s.facing=static_cast<std::uint8_t>(facing|(g.byte()&0xFC));
            s.position=g.below(3)==0 ? g.edge_position() : g.byte();
            s.layout[s.position]=static_cast<std::uint8_t>(value);
            g.run(handler,s);
            auto b=g.random_scene();                                   // behind: Down held, Up not
            b.facing=static_cast<std::uint8_t>(facing|(g.byte()&0xFC));
            b.held=static_cast<std::uint8_t>(2|(g.byte()&0xFC));
            b.position=g.below(3)==0 ? g.edge_position() : g.byte();
            if (b.layout[b.position]==8) b.layout[b.position]=0;
            b.layout[g.cell(b.position,b.facing,dungeon::behind)]=static_cast<std::uint8_t>(value);
            g.run(handler,b);
        }
    // The pad: every held byte with every kind of cell ahead, and every pressed
    // byte (only bits 4 and 5, the buttons, count) with a door, a wall and floor ahead.
    for (unsigned held=0;held<256;++held)
        for (unsigned k=0;k<8;++k) {
            auto s=g.random_scene();
            s.held=static_cast<std::uint8_t>(held);
            if (s.layout[s.position]==8) s.layout[s.position]=0;
            s.layout[g.cell(s.position,s.facing,dungeon::ahead)]=static_cast<std::uint8_t>(k<7 ? k|(g.byte()&0x88) : g.byte());
            g.run(handler,s);
        }
    for (unsigned pressed=0;pressed<256;++pressed)
        for (const std::uint8_t ahead:{std::uint8_t{4},std::uint8_t{0x84},std::uint8_t{0x0C},std::uint8_t{1},std::uint8_t{0},std::uint8_t{6}}) {
            auto s=g.random_scene();
            s.held=static_cast<std::uint8_t>(g.byte()&0xF0);
            s.pressed=static_cast<std::uint8_t>(pressed);
            if (s.layout[s.position]==8) s.layout[s.position]=0;
            s.layout[g.cell(s.position,s.facing,dungeon::ahead)]=ahead;
            g.run(handler,s);
        }
}

// Every value-8 cell of the 61 floors: the first chunk from the handler, and
// the continuation entered at every record boundary of the table (the loop
// continues, a record matches, or the end of the table starts the pitfall).
void object_probes(Rig& g) {
    const unsigned records=static_cast<unsigned>(g.tables.objects().size());
    std::uint64_t pits=0, matches=0;
    for (unsigned floor=0;floor<dungeon::floor_count;++floor) {
        const auto layout=g.tables.layout(floor);
        for (unsigned cell=0;cell<256;++cell) {
            if (layout[cell]!=8) continue;
            Scene s;
            s.layout=layout; s.floor=static_cast<std::uint8_t>(floor); s.position=static_cast<std::uint8_t>(cell);
            s.facing=g.byte(); s.pressed=g.byte();
            s.held=g.below(4)==0 ? 0 : g.byte();
            g.run(handler,s);
            const bool pit=!g.tables.object_at(static_cast<std::uint8_t>(floor),static_cast<std::uint8_t>(cell));
            pits+=pit; matches+=!pit;
            for (unsigned i=0;i<=records;++i) {
                s.held=g.byte(); s.facing=g.byte(); s.pressed=g.byte();
                g.run(continuation,s,[&](Machine& m) {
                    auto& cpu=m.cpu();
                    cpu.hl.uint16_value=static_cast<std::uint16_t>(objects_start+7*i);
                    cpu.de.uint16_value=6;
                    cpu.bc.uint8_values.at_1=static_cast<std::uint8_t>(floor); cpu.bc.uint8_values.at_0=static_cast<std::uint8_t>(cell);
                });
            }
        }
    }
    check(pits==33,"33 pits found among the value-8 cells");
    // Arbitrary scan state: any record pointer, step and key.
    for (unsigned i=0;i<6000;++i) {
        auto s=g.random_scene();
        const auto hl=static_cast<std::uint16_t>(g.below(3)==0 ? g.random() : 0x8000+g.below(0x4000));
        const auto de=static_cast<std::uint16_t>(g.below(2) ? 6 : g.below(4)==0 ? g.random() : 1+g.below(12));
        const auto b=g.byte(), c=g.byte();
        g.run(continuation,s,[&](Machine& m) {
            auto& cpu=m.cpu(); cpu.hl.uint16_value=hl; cpu.de.uint16_value=de; cpu.bc.uint8_values.at_1=b; cpu.bc.uint8_values.at_0=c;
        });
    }
    std::cout<<"  object cells: "<<pits<<" pits and "<<matches<<" cells with a record, from every record boundary\n";
}

// Every cell of every floor directly ahead of the party (all four facings),
// and behind it (facing right).
void floor_probes(Rig& g) {
    for (unsigned floor=0;floor<dungeon::floor_count;++floor) {
        const auto layout=g.tables.layout(floor);
        for (unsigned cell=0;cell<256;++cell)
            for (unsigned facing=0;facing<4;++facing) {
                Scene s;
                s.layout=layout; s.floor=static_cast<std::uint8_t>(floor); s.facing=static_cast<std::uint8_t>(facing);
                s.position=static_cast<std::uint8_t>(cell-g.tables.offset(facing,dungeon::ahead));
                if (s.layout[s.position]==8) s.layout[s.position]=0;
                s.held=static_cast<std::uint8_t>(1|(g.byte()&0xFE)); s.pressed=g.byte();
                g.run(handler,s);
                if (facing==1) {
                    s.position=static_cast<std::uint8_t>(cell-g.tables.offset(facing,dungeon::behind));
                    if (s.layout[s.position]==8) s.layout[s.position]=0;
                    s.held=static_cast<std::uint8_t>(2|(g.byte()&0xFC));
                    g.run(handler,s);
                }
            }
    }
}

// ---- Facts observed on real runs ------------------------------------------------------------

void facts(Rig& g) {
    auto scene=[&](std::uint8_t held, std::uint8_t pressed, std::uint8_t ahead, std::uint8_t behind=0) {
        Scene s;
        for (auto& c:s.layout) c=1;
        s.position=0x55; s.facing=1; s.held=held; s.pressed=pressed;
        s.layout[0x55]=0; s.layout[g.cell(0x55,1,dungeon::ahead)]=ahead; s.layout[g.cell(0x55,1,dungeon::behind)]=behind;
        return s;
    };
    // A closed door ahead: Up only bumps (the handler returns), button 1 or 2 opens it.
    check(g.run(handler,scene(1,0,4))==0x672B,"pushing Up at a closed door returns without moving");
    check(g.run(handler,scene(0,0x10,4))==0x6813 && g.run(handler,scene(0,0x20,4))==0x6813,"buttons 1 and 2 call the door opening with a door ahead");
    check(g.run(handler,scene(0,0x30,0))==0x6817,"a button with floor ahead goes to the field menu");
    // An opened door is passed through, with the exit lookup when its exit bit is set.
    check(g.run(handler,scene(1,0,0x84))!=0x672B,"an opened door does not bump");
    // A back step towards a stairs cell does nothing: no step, no turn, a return.
    check(g.run(handler,scene(2,0,0,2))==0x67D9 && g.run(handler,scene(2,0,0,3))==0x67D9,"Down with stairs behind returns at once");
    check(g.run(handler,scene(2,0,0,0))==0x6785,"Down with floor behind steps back");
    check(g.run(handler,scene(1,0,0))==0x675A && g.run(handler,scene(1,0,7))==0x6755,"floor ahead steps forward, a fake wall calls the step twice");
    check(g.run(handler,scene(4,0,0))==0x67AF && g.run(handler,scene(8,0,0))==0x67DA,"Left and Right call the turns");
    // The pit cell is an ordinary object-cell visit at the handler's first call: the scan comes first.
    Scene pit=scene(0,0,0);
    pit.layout[0x55]=8; pit.floor=60;
    check(g.run(handler,pit)==0x6618,"standing on a pit cell starts the object scan");
}

// ---- Steps, turns, stairs and the pitfall ------------------------------------------------------

void step_probes(Rig& g) {
    // Forward and back steps and the pass-through read the facing's delta: every
    // facing byte (the upper bits are ignored) with edge and ordinary positions.
    for (const auto entry:{at(0x6734),at(0x675D),at(0x6792)})
        for (unsigned facing=0;facing<256;++facing)
            for (unsigned k=0;k<56;++k) {
                auto s=g.random_scene();
                s.facing=static_cast<std::uint8_t>(facing);
                s.position=k<13 ? g.edge_position() : g.byte();
                g.run(entry,s);
            }
    // Stairs: the floor changes by +1 for kind 2 (A = 0), else by -1; wraps and
    // out-of-range floors (0, 60, 255) are plain arithmetic.
    for (unsigned floor=0;floor<256;++floor)
        for (const std::uint8_t a:{std::uint8_t{0},std::uint8_t{1},std::uint8_t{2},std::uint8_t{0xFF},std::uint8_t{0x40}}) {
            auto s=g.random_scene();
            s.floor=static_cast<std::uint8_t>(floor);
            g.run(at(0x6715),s,[&](Machine& m) { m.cpu().af.uint8_values.at_1=a; });
        }
    // Turns: every facing byte, edge and ordinary positions, the two cells read
    // with open floor or something else; and every value in each of the two.
    for (const auto entry:{at(0x67B7),at(0x67E2)}) {
        const bool left=entry.address==0x67B7;
        const unsigned first=left ? 0x0D : 0x0C;
        auto turned=[&](const Scene& s) { return static_cast<std::uint8_t>(left ? s.facing-1 : s.facing+1); };
        for (unsigned facing=0;facing<256;++facing)
            for (unsigned k=0;k<24;++k) {
                auto s=g.random_scene();
                s.facing=static_cast<std::uint8_t>(facing);
                s.position=k<8 ? g.edge_position() : g.byte();
                const auto face=turned(s);
                s.layout[g.cell(s.position,face,first)]=(k&1) ? static_cast<std::uint8_t>(g.byte()|1) : std::uint8_t{0};
                s.layout[g.cell(s.position,face,dungeon::ahead)]=(k&2) ? static_cast<std::uint8_t>((g.byte()&0xF8)|(1+g.below(7))) : std::uint8_t{0};
                g.run(entry,s);
            }
        for (unsigned facing=0;facing<4;++facing)
            for (unsigned value=0;value<256;++value)
                for (unsigned which=0;which<2;++which) {
                    auto s=g.random_scene();
                    s.facing=static_cast<std::uint8_t>(facing|(g.byte()&0xFC));
                    s.position=g.byte();
                    const auto face=turned(s);
                    const auto a=g.cell(s.position,face,first), b=g.cell(s.position,face,dungeon::ahead);
                    s.layout[a]=static_cast<std::uint8_t>(which ? g.below(2) : value);
                    s.layout[b]=static_cast<std::uint8_t>(which ? value : g.below(2));
                    g.run(entry,s);
                }
    }
}

void pitfall_probes(Rig& g) {
    const RoutineEntry counted[]{at(0x6643),at(0x6660),at(0x6699),at(0x66B7),at(0x66CC)};
    for (const auto entry:counted) {
        // Every countdown (DJNZ taken or not, B = 0 wrapping) against varied scroll bytes,
        // and every scroll byte at the interesting countdowns.
        for (unsigned b=0;b<256;++b)
            for (unsigned k=0;k<4;++k) {
                auto s=g.random_scene();
                g.run(entry,s,[&](Machine& m) {
                    m.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(b);
                    m.memory().write(scroll,static_cast<std::uint8_t>(b^(k*0x55)^g.byte()));
                });
            }
        for (const unsigned b:{0u,1u,2u,5u,12u,255u})
            for (unsigned value=0;value<256;++value) {
                auto s=g.random_scene();
                g.run(entry,s,[&](Machine& m) {
                    m.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(b);
                    m.memory().write(scroll,static_cast<std::uint8_t>(value));
                });
            }
    }
    // The drop's last countdown lowers the floor by one, and floor 0 stays 0:
    // every floor byte at B = 1.
    for (unsigned floor=0;floor<256;++floor) {
        auto s=g.random_scene();
        s.floor=static_cast<std::uint8_t>(floor);
        g.run(at(0x6660),s,[&](Machine& m) { m.cpu().bc.uint8_values.at_1=1; });
    }
    // The scroll byte of the entries that read it, every value, with random A and B.
    for (const auto entry:{at(0x662C),at(0x6684)})
        for (unsigned value=0;value<256;++value) {
            auto s=g.random_scene();
            g.run(entry,s,[&](Machine& m) { m.memory().write(scroll,static_cast<std::uint8_t>(value)); });
        }
}

// ---- Environment: slot-2 banks, refresh counter, stack placements, held words ------------------

void environment(Rig& g, const std::function<Scene()>& scene, const Rig::Prepare& prepare, RoutineEntry entry, unsigned fetches) {
    const auto scratch=g.harness.routine(entry).spec.scratch_stack;
    for (auto setup:probe::slot2_variants({}))
        for (const auto r:probe::r_wrap_values(fetches)) {
            setup.r=r; setup.iff1=setup.iff2=(g.seed&1);
            g.run(entry,setup,scene(),prepare);
        }
    // The stack: the word at SP is a return address or a held counter; some
    // placements hold the words below SP the segment pushes in the game's own
    // variables, in a ROM slot, on the mapper registers or in disabled RAM.
    for (const auto& placement:probe::stack_cases())
        for (unsigned k=0;k<4;++k) {
            probe::Setup setup; setup.sp=placement.sp; setup.cartridge_ram=placement.cartridge_ram;
            setup.work_ram_disabled=placement.work_ram_disabled; setup.iff1=setup.iff2=(g.seed&1);
            setup.r=g.byte();
            if (setup.cartridge_ram) setup.banks[2]=3;
            if (!placement.admitted(scratch)) { g.fallback(entry,setup,scene()); continue; }
            g.run(entry,setup,scene(),prepare);
        }
    // Stack on the game's own state: the pushes land on the variables read afterwards.
    for (const std::uint16_t sp:{dungeon::position+2,dungeon::facing+2,dungeon::floor+2,dungeon::facing+4,0xC304+2,0xC005,0xCB10+1}) {
        probe::Setup setup; setup.sp=sp; setup.r=g.byte();
        g.run(entry,setup,scene(),prepare);
    }
}

void environment_probes(Rig& g) {
    // Typical scenes of every entry, so the variations meet each path family.
    auto handler_scene=[&](unsigned kind) {
        return [&g,kind]() {
            auto s=g.random_scene();
            if (s.layout[s.position]==8) s.layout[s.position]=0;
            const auto ahead=g.cell(s.position,s.facing,dungeon::ahead), behind=g.cell(s.position,s.facing,dungeon::behind);
            switch (kind) {
            case 0: s.held=0; s.pressed=0; break;                                           // idle
            case 1: s.held=0; s.pressed=0x10; s.layout[ahead]=4; break;                     // a door opens
            case 2: s.held=1; s.layout[ahead]=0; break;                                     // forward
            case 3: s.held=1; s.layout[ahead]=0x84|(g.byte()&8); break;                     // an opened door
            case 4: s.held=1; s.layout[ahead]=2|(g.byte()&8); break;                        // stairs
            case 5: s.held=2; s.layout[behind]=0; break;                                    // back
            case 6: s.held=4; break;                                                        // Left
            case 7: s.held=8; break;                                                        // Right
            case 8: s.held=1; s.layout[ahead]=7; break;                                     // fake wall
            case 10: s.held=1; s.layout[ahead]=static_cast<std::uint8_t>(4+g.below(3)+(g.byte()&8)); break;   // Up at a closed door
            case 11: s.held=2; s.layout[behind]=static_cast<std::uint8_t>(2+g.below(2)); break;   // Down with stairs behind
            case 12: s.held=0; s.pressed=0x20; s.layout[ahead]=static_cast<std::uint8_t>(g.byte()&0xFB); break;   // a button, no door
            default: {                                                                      // an object cell with a record
                const auto& o=g.tables.objects()[g.below(static_cast<unsigned>(g.tables.objects().size()))];
                s.layout=g.tables.layout(o.floor); s.floor=o.floor; s.position=o.cell; break;
            }
            }
            return s;
        };
    };
    for (unsigned kind=0;kind<13;++kind) {
        const auto make=handler_scene(kind);
        environment(g,make,{},handler,kind==0 ? 120 : 190);
    }
    // The continuation at record boundaries around the real table, and on a
    // record that matches (B and C from the record itself) with every scene.
    const unsigned records=static_cast<unsigned>(g.tables.objects().size());
    {
        const auto make=handler_scene(9);
        for (unsigned variant=0;variant<3;++variant) {
            const auto start=variant==0 ? 0u : variant==1 ? records/2 : records;
            environment(g,make,[start](Machine& m) {
                auto& cpu=m.cpu();
                cpu.hl.uint16_value=static_cast<std::uint16_t>(objects_start+7*start); cpu.de.uint16_value=6;
                cpu.bc.uint8_values.at_1=m.memory().read(dungeon::floor); cpu.bc.uint8_values.at_0=m.memory().read(dungeon::position);
            },continuation,260);
        }
        for (unsigned kind=0;kind<13;++kind) {
            if (kind==9) continue;
            const auto scene=handler_scene(kind);
            for (unsigned i=0;i<400;++i) {
                const auto record=g.below(records);
                const auto flr=g.rom.byte(3,static_cast<std::uint16_t>(objects_start+7*record));
                const auto cell=g.rom.byte(3,static_cast<std::uint16_t>(objects_start+7*record+1));
                g.run(continuation,scene(),[&](Machine& m) {
                    auto& cpu=m.cpu();
                    cpu.hl.uint16_value=static_cast<std::uint16_t>(objects_start+7*record); cpu.de.uint16_value=6;
                    cpu.bc.uint8_values.at_1=flr; cpu.bc.uint8_values.at_0=cell;
                });
            }
        }
    }
    // A table the real ROM never has: cartridge RAM in slot 2 with the first
    // record an end mark (the pitfall's start from the handler itself), or a
    // record matching the party's cell (the directions from the first chunk).
    for (unsigned variant=0;variant<2;++variant)
        for (unsigned i=0;i<200;++i) {
            probe::Setup setup; setup.cartridge_ram=true; setup.banks[2]=3; setup.r=g.byte(); setup.iff1=setup.iff2=(i&1);
            auto s=handler_scene(i%9)();
            s.layout[s.position]=8;
            const auto position=s.position, floor=s.floor;
            g.run(handler,setup,s,[variant,position,floor](Machine& m) {
                m.memory().write(objects_start,variant==0 ? dungeon::table_end : floor);
                m.memory().write(objects_start+1,position);
            });
        }
    // Every other entry on random scenes with random registers (B counts included).
    for (const auto& [entry,unit]:dungeon::movement_segments) {
        (void)unit;
        if (entry==handler || entry==continuation) continue;
        const auto make=[&g]() { return g.random_scene(); };
        environment(g,make,{},entry,entry.address==0x6674 ? 90 : 60);
    }
}

void probes(const probe::Rom& rom) {
    Rig g(rom);
    const auto before=g.count;
    direction_probes(g);     const auto d=g.count-before;
    object_probes(g);        const auto o=g.count-before-d;
    floor_probes(g);         const auto f=g.count-before-d-o;
    facts(g);
    step_probes(g);
    pitfall_probes(g);
    environment_probes(g);
    // Every declared exit of every entry was taken, and both outcomes of the conditional ones.
    for (const auto& [entry,unit]:dungeon::movement_segments) {
        (void)unit;
        const auto& routine=g.harness.routine(entry);
        const auto& taken=g.reached[entry];
        for (const auto& exit:routine.spec.exits)
            check(taken.contains(exit.address),"entry "+std::to_string(entry.address)+" never took its exit at "+std::to_string(exit.address));
        for (const auto& exit:routine.spec.exits)
            if (exit.conditional) check(taken.at(exit.address).size()==2,"a conditional exit was taken only one way");
        check(g.per_entry[entry]>0,"every entry was probed");
    }
    std::cout<<g.count<<" probes in C++ against the original ("<<g.fallbacks<<" unmapped-stack fallbacks and "<<g.refusals<<" refusals as expected): "
             <<d<<" direction, "<<o<<" object-cell, "<<f<<" floor-cell, the rest steps, turns, stairs, pitfall and environment.\n";
    for (const auto& [entry,unit]:dungeon::movement_segments) { (void)unit; std::cout<<"  01:"<<std::hex<<entry.address<<std::dec<<" "<<g.per_entry[entry]<<'\n'; }
}
}

int main(int argc, char** argv) {
    try {
        if (argc==1) { registration(); return 0; }
        if (argc!=3) throw std::runtime_error("Expected <rom> <us|br>.");
        const auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        registration();
        probes(*rom);
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
