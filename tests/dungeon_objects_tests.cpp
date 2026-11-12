// SPDX-License-Identifier: GPL-3.0-or-later
// The door, exit lookup and object check (milestone 7, part R3): the 25
// segments of dungeon::object_segments.
//   no arguments     registration: the catalog of entries, units, exits and
//                    permissions (no ROM needed)
//   <rom> <us|br>    state probes of every segment against the original, each
//                    dispatched with the per-call verifier and asserted to run
//                    in C++ with no fallback (exit 77 without the ROM)
// A probe starts a power-on machine of the identified ROM at one entry, sets
// the state the callers leave, and runs the entry. When the original goes on
// into another segment of this part (the scan's next record, the exit kinds,
// the event path) the chain continues with that segment, every step verified;
// a native callee ends the chain. Tables come from DungeonTables; the first
// record of a table is the only one the entry segments can match, so its
// variants are probed on copies of the image with that record rewritten.
#include "game/dungeon_kit.h"
#include "game/dungeon_tables.h"
#include "probe.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <set>

using namespace psmodern;
using probe::check;
namespace dungeon=game::dungeon;
namespace kit=game::kit;
namespace {
// ---- Registration catalog ----------------------------------------------------

struct Expected { std::uint16_t entry; std::vector<SegmentExit> exits; unsigned scratch, inherited; bool mapper; };
std::vector<Expected> expected_catalog() {
    using kit::returned_at; using kit::core_final_at; using kit::conditional_at; using kit::loop_at;
    return {
        {0x681B,{returned_at(0x6822),core_final_at(0x6832,-4)},4,0,false},
        {0x6835,{core_final_at(0x6837)},0,0,false},
        {0x683A,{core_final_at(0x6832),returned_at(0x6849,4)},0,4,false},
        {0x68BC,{returned_at(0x68C3),returned_at(0x68EB),loop_at(0x68E1,0),conditional_at(0x68F4)},2,0,true},
        {0x68E2,{loop_at(0x68E1,0),returned_at(0x68EB),conditional_at(0x68F4)},0,0,false},
        {0x68F7,{core_final_at(0x68FD,-2),core_final_at(0x6949,-4)},4,0,false},
        {0x6900,{core_final_at(0x690B)},0,0,true},
        {0x690E,{core_final_at(0x6911)},0,0,false},
        {0x6914,{core_final_at(0x691F)},0,0,false},
        {0x6925,{core_final_at(0x692D,2)},0,2,true},
        {0x6930,{returned_at(0x6935),core_final_at(0x6936)},0,0,false},
        {0x6939,{core_final_at(0x693A)},0,0,false},
        {0x694C,{core_final_at(0x695D,2),core_final_at(0x692D,4)},0,4,true},
        {0x6960,{core_final_at(0x691F)},0,0,false},
        {0x6963,{returned_at(0x6968),returned_at(0x698D),returned_at(0x6996),returned_at(0x69CA),returned_at(0x69FC),
                 returned_at(0x6A1C),loop_at(0x6988,128,-2),core_final_at(0x69CE),core_final_at(0x69EB,-2),
                 core_final_at(0x6A04,-4),core_final_at(0x6A23,-2),core_final_at(0x6A39)},4,0,true},
        {0x6989,{returned_at(0x698D,2),returned_at(0x6996,2),returned_at(0x69CA,2),returned_at(0x69FC,2),returned_at(0x6A1C,2),
                 loop_at(0x6988,128),core_final_at(0x69CE,2),core_final_at(0x69EB),core_final_at(0x6A04,-2),
                 core_final_at(0x6A23),core_final_at(0x6A39,2)},2,2,false},
        {0x69D1,{core_final_at(0x69D2,-2)},2,0,false},
        {0x69D5,{core_final_at(0x69D6,2)},0,2,false},
        {0x69D9,{returned_at(0x69DD),core_final_at(0x69DE)},0,0,false},
        {0x69F1,{returned_at(0x69FC,2),core_final_at(0x6A04,-2)},2,2,true},
        {0x6A07,{core_final_at(0x6A0F,4)},0,4,false},
        {0x6A12,{returned_at(0x6A16),core_final_at(0x6A17)},0,0,false},
        {0x6A29,{core_final_at(0x6A39,2)},0,2,true},
        {0x6A2F,{core_final_at(0x6A39)},0,0,false},
        {0x6A3F,{returned_at(0x6A57)},0,0,false},
    };
}
void registration() {
    RoutineRegistry registry;
    game::register_dungeon_objects(registry);
    check(registry.entries().size()==dungeon::object_segments.size() && dungeon::object_segments.size()==25,
          "the 25 object segments are registered, and nothing else");
    const auto catalog=expected_catalog();
    check(catalog.size()==25,"the expected catalog lists 25 entries");
    for (const auto& expected:catalog) {
        const RoutineEntry entry{1,expected.entry};
        const auto listed=std::find_if(dungeon::object_segments.begin(),dungeon::object_segments.end(),
                                       [&](const dungeon::Segment& s) { return s.entry==entry; });
        check(listed!=dungeon::object_segments.end(),"a catalog entry is not in dungeon::object_segments");
        const auto* routine=registry.find(entry);
        check(routine!=nullptr,"a catalog entry is not registered");
        const auto& spec=routine->spec;
        check(spec.unit==listed->unit,"an entry belongs to the unit dungeon.h names");
        check(spec.slot2_mapper==expected.mapper && spec.scratch_stack==expected.scratch && spec.inherited_stack==expected.inherited &&
              !spec.single_instruction && !spec.force_admit && !spec.absolute_wram_store,
              "an entry declares the permissions of the design");
        check(spec.exits.size()==expected.exits.size(),"an entry declares the exits of the design");
        for (const auto& exit:expected.exits)
            check(std::any_of(spec.exits.begin(),spec.exits.end(),[&](const SegmentExit& e) { return e.same_declaration(exit); }),
                  "an entry's exit differs from the design (address, kind, stack, conditional or after)");
    }
}

// ---- ROM probes ----------------------------------------------------------------

struct Stats {
    std::uint64_t calls=0, fallbacks=0, chains=0, unpredictable=0;
    std::map<std::uint16_t,std::uint64_t> per_entry;
    std::map<std::uint16_t,std::set<std::uint16_t>> exits;   // final instruction addresses reached
};

using Builder=std::function<void(Machine&)>;
constexpr std::uint16_t sentinel=0xBEEF;
constexpr std::uint64_t unpredictable_bound=60000;   // over a frame (59,736 cycles)

// The machine dispatch's own stack test (src/machine/machine.cpp), predicted
// here to decide whether a probe must run in C++ or fall back.
bool admitted(const Machine& m, const SegmentSpec& spec) {
    const auto sp=m.cpu().sp.uint16_value;
    auto unmapped=[&](std::uint16_t address) { return m.memory().mapping(address).kind==Memory::Kind::unmapped; };
    auto byte=[&](unsigned offset) {
        const auto address=static_cast<std::uint16_t>(sp+offset);
        return unmapped(address) || (spec.scratch_stack && address>=0xfffc);
    };
    bool bad=byte(0) || byte(1);
    for (unsigned i=1;i<=spec.scratch_stack;++i) bad=bad || byte(0x10000-i);
    if (spec.inherited_stack)
        for (unsigned i=0;i<spec.inherited_stack+2;++i) {
            const auto address=static_cast<std::uint16_t>(sp+i);
            bad=bad || unmapped(address) || address>=0xfffc;
        }
    return !bad;
}

class Lab {
public:
    Lab(probe::Rom rom, const game::DungeonTables& tables, Stats& stats, std::uint64_t seed)
        : harness(std::move(rom)), tables_(tables), stats_(stats), rng_(seed), seed_(seed) {}
    probe::Harness harness;

    // ---- State helpers ----
    static void poke(Machine& m, std::uint16_t address, std::uint8_t value) {
        if (address>=0xfffc) return;   // never the mapper registers
        m.memory().write(address,value);
    }
    static void poke_word(Machine& m, std::uint16_t address, std::uint16_t value) {
        poke(m,address,static_cast<std::uint8_t>(value)); poke(m,static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
    }
    static void lay_stack(Machine& m, std::initializer_list<std::uint16_t> words) {
        auto sp=m.cpu().sp.uint16_value;
        for (const auto word:words) { poke_word(m,sp,word); sp=static_cast<std::uint16_t>(sp+2); }
    }
    static std::uint16_t word_at(const Machine& m, std::uint16_t address) {
        return static_cast<std::uint16_t>(m.peek(address)|(m.peek(static_cast<std::uint16_t>(address+1))<<8));
    }
    static void set_b(Machine& m, std::uint8_t v) { m.cpu().bc.uint8_values.at_1=v; }
    static void set_c(Machine& m, std::uint8_t v) { m.cpu().bc.uint8_values.at_0=v; }
    static void set_d(Machine& m, std::uint8_t v) { m.cpu().de.uint8_values.at_1=v; }
    static void set_e(Machine& m, std::uint8_t v) { m.cpu().de.uint8_values.at_0=v; }
    static void set_a(Machine& m, std::uint8_t v) { m.cpu().af.uint8_values.at_1=v; }
    static void set_hl(Machine& m, std::uint16_t v) { m.cpu().hl.uint16_value=v; }
    std::uint8_t target(std::uint8_t facing, std::uint8_t position, unsigned b) const {
        return static_cast<std::uint8_t>(position+tables_.offset(facing&3,b));
    }
    std::uint8_t byte() { return static_cast<std::uint8_t>(rng_()); }
    // The party's place: facing (garbage high bits), position, floor and a random layout.
    void scene(Machine& m, std::uint8_t facing, std::uint8_t position, std::uint8_t floor) {
        for (unsigned i=0;i<256;++i) poke(m,static_cast<std::uint16_t>(dungeon::layout+i),byte());
        poke(m,dungeon::facing,facing); poke(m,dungeon::position,position); poke(m,dungeon::floor,floor);
    }
    void put_cell(Machine& m, std::uint8_t cell, std::uint8_t value) { poke(m,static_cast<std::uint16_t>(dungeon::layout+cell),value); }
    std::uint8_t garbage_facing() { return static_cast<std::uint8_t>((rng_()&0xFC)|(rng_()&3)); }
    // A done flag value: not done, done, or something else (anything but FFh is not done).
    std::uint8_t done_value(unsigned kind) { return kind==0 ? 0 : kind==1 ? 0xFF : static_cast<std::uint8_t>(1+rng_()%254); }

    // The next setup of a rotation through slot-2 mappings, refresh-counter
    // wraps, interrupt flags and stack places, so every combination turns up
    // over a family; every 11th is one of the fixed stack placements.
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
    static probe::Setup normal(probe::Setup s) {
        s.banks[2]=3; s.cartridge_ram=false; s.work_ram_disabled=false;
        if (s.sp<0xCC10 || s.sp>=0xDF20) s.sp=0xDF00;
        return s;
    }
    static bool is_normal(const probe::Setup& s) {
        return s.banks[2]==3 && !s.cartridge_ram && !s.work_ram_disabled && s.sp>=0xCC10 && s.sp<0xDF20;
    }

    // ---- Stepping ----
    // One call that must run in C++ against the original; the address of the
    // original's last instruction (the exit it took) is found by running a copy
    // of the machine for the same number of cycles.
    void step(Machine& m, RoutineEntry entry) {
        Machine original=m;
        const auto start=m.cycles();
        // With work RAM on, every bound is exact, as for the movement and cell
        // probes: an overstated final-instruction cost would otherwise pass.
        const bool ram_on=(m.memory().memory_control()&0x10)==0;
        const auto bound=harness.routine(entry).bound(m);
        const auto result=harness.run_cpp(m,entry);
        if (ram_on)
            check(result.cycles==bound,"entry "+std::to_string(entry.address)+" cost "+std::to_string(result.cycles)+" cycles, its bound said "+std::to_string(bound));
        std::uint16_t last=0;
        unsigned guard=0;
        while (original.cycles()-start<result.cycles && ++guard<4000) { last=original.cpu().pc.uint16_value; original.step(); }
        check(original.cycles()-start==result.cycles,"the original does not end on the C++ call's last cycle");
        ++stats_.calls; ++stats_.per_entry[entry.address]; stats_.exits[entry.address].insert(last);
    }
    std::optional<RoutineEntry> next_entry(const Machine& m) const {
        const auto pc=m.cpu().pc.uint16_value;
        const auto mapping=m.memory().mapping(pc);
        if (mapping.kind!=Memory::Kind::rom || mapping.bank!=1) return std::nullopt;
        for (const auto& s:dungeon::object_segments) if (s.entry.address==pc) return s.entry;
        return std::nullopt;
    }
    // A call dispatch refuses, predicted from the machine's own rules: an
    // unmapped or mapper stack, or a frame boundary inside the bound. The bound
    // of a segment whose inline call returns elsewhere (a stack in ROM) exceeds a
    // frame, so such a call falls back with the frame reason. A scan across
    // cartridge RAM zeros, or any other long one, crosses a frame legitimately.
    bool refused(Machine& m, RoutineEntry entry) {
        const auto& routine=harness.routine(entry);
        if (!admitted(m,routine.spec)) {
            harness.run_fallback(m,entry,RoutineFallback::unmapped_stack);
            ++stats_.fallbacks;
            return true;
        }
        const auto bound=routine.bound(m);
        if (Machine::frame_cycles-m.cycles()%Machine::frame_cycles<bound) {
            harness.run_fallback(m,entry,RoutineFallback::frame);
            ++stats_.fallbacks;
            if (bound>=unpredictable_bound) ++stats_.unpredictable;
            return true;
        }
        return false;
    }
    // Run one entry and the segments of this part the original goes on into.
    // Returns the machine at the end (nullopt when the first call fell back, as
    // the stack placement requires).
    std::optional<Machine> run(RoutineEntry entry, const probe::Setup& setup, const Builder& build) {
        if (forced_ && used_[entry.address]++>=quota_) return std::nullopt;
        auto m=harness.prepare(entry,setup);
        build(m);
        if (refused(m,entry)) return std::nullopt;
        const auto started=m.cycles();
        step(m,entry);
        ++stats_.chains;
        // A chain stops before the first frame's vertical blank (cartridge RAM
        // zeros in slot 2 make a scan run for tens of thousands of cycles).
        for (unsigned n=0;n<6000 && m.cycles()-started<30000;++n) {
            const auto next=next_entry(m);
            if (!next || refused(m,*next)) break;
            step(m,*next);
        }
        return m;
    }

    // ---- 01:681B, 01:6835, 01:683A: the door ----
    void door() {
        for (unsigned value=0;value<256;++value)
            for (unsigned k=0;k<10;++k) {
                const auto setup=next_setup();
                const std::uint8_t facing=garbage_facing(), position=byte(), c=k<3 ? static_cast<std::uint8_t>(2+k) : byte();
                const auto cell=target(facing,position,dungeon::ahead);
                auto m=run(dungeon::door_unit,setup,[&](Machine& m) {
                    scene(m,facing,position,byte()); put_cell(m,cell,static_cast<std::uint8_t>(value)); set_c(m,c);
                });
                if (!m || !is_normal(setup)) continue;
                // The mark is set unless it was; the sound byte and the row base follow.
                const auto after=m->peek(static_cast<std::uint16_t>(dungeon::layout+cell));
                check(after==(value|0x80),"the door cell carries the opened mark afterwards");
                if (!(value&0x80)) {
                    check(m->peek(0xC004)==harness.rom().byte(1,0x6826),"the door stores its sound byte");
                    check(m->cpu().hl.uint16_value==static_cast<std::uint16_t>(c<<8) && m->cpu().bc.uint8_values.at_1==3 &&
                          m->cpu().pc.uint16_value!=0x6832,"the door starts at the row of C with three rounds");
                }
            }
        // 01:6835: only A changes; two words are held above the stack pointer.
        for (unsigned k=0;k<400;++k)
            run({1,0x6835},next_setup(),[&](Machine& m) { lay_stack(m,{static_cast<std::uint16_t>(rng_()),static_cast<std::uint16_t>(rng_()),0x0180}); });
        // 01:683A: every high byte of the row, the low bytes around the row step
        // (40h), and the round counter at its ends.
        for (unsigned h=0;h<256;++h)
            for (const unsigned l:{0x00u,0x3Fu,0x40u,0x41u,0x80u,0xBFu,0xC0u,0xFFu,0x01u,0x7Fu})
                for (const unsigned b:{1u,2u,3u,0u,255u}) {
                    const auto setup=next_setup();
                    run({1,0x683A},setup,[&](Machine& m) {
                        lay_stack(m,{static_cast<std::uint16_t>((h<<8)|l),static_cast<std::uint16_t>((b<<8)|byte()),0x0180});
                    });
                }
    }

    // ---- 01:68BC and its segments: the exit lookup ----
    // A cell of the party's view with the exit mark (any value with bit 3) at the record's cell.
    void exit_scene(Machine& m, std::uint8_t cell, std::uint8_t floor, std::uint8_t value) {
        const auto facing=garbage_facing();
        const auto position=static_cast<std::uint8_t>(cell-tables_.offset(facing&3,dungeon::ahead));
        scene(m,facing,position,floor); put_cell(m,cell,value);
        poke(m,dungeon::game_mode,static_cast<std::uint8_t>(0x0B));
    }
    // After a lookup chain: the mode, or where the original went on (the world-map
    // transfer, or the fade the other kinds start with).
    void expect_exit(const Machine& m, std::uint8_t floor, std::uint8_t cell, bool marked) {
        const auto operand=[&](std::uint16_t at) {
            return static_cast<std::uint16_t>(harness.rom().byte(1,at)|(harness.rom().byte(1,static_cast<std::uint16_t>(at+1))<<8));
        };
        const auto pc=m.cpu().pc.uint16_value;
        if (!marked) { check(m.peek(dungeon::game_mode)==0x0B && pc==0x0180,"a cell without the exit mark is left alone"); return; }
        const auto record=tables_.exit_at(floor,cell);
        if (!record) { check(m.peek(dungeon::game_mode)==8 && pc==0x0180,"no exit record: the game mode becomes 8"); return; }
        check(m.peek(dungeon::game_mode)==0x0B,"a record leaves the game mode to its kind");
        if (record->kind<dungeon::world_map_kinds_end)
            check(pc==operand(0x68F5) && m.cpu().af.uint8_values.at_1==8,"a kind below 80h goes to the world-map transfer with A = 8");
        else if (record->kind==0xFF) check(pc==operand(0x68FE),"kind FFh starts the scene with a fade");
        else check(pc==operand(0x694A),"the other kinds start with a fade");
    }
    void exit_records() {
        for (const auto& record:tables_.exits())
            for (unsigned k=0;k<4;++k) {
                const auto setup=next_setup();
                const auto value=static_cast<std::uint8_t>(byte()|8);
                auto m=run(dungeon::exit_lookup_unit,setup,[&](Machine& m) { exit_scene(m,record.cell,record.floor,value); });
                if (m && is_normal(setup)) expect_exit(*m,record.floor,record.cell,true);
            }
    }
    // The real floors: every cell with the exit mark seen from each side, a
    // sample of cells without it (the lookup ends at once), every value at an
    // unrelated place, and floors without records.
    void exit_cells() {
        for (unsigned floor=0;floor<dungeon::floor_count;++floor) {
            const auto layout=tables_.layout(floor);
            for (unsigned cell=0;cell<256;++cell) {
                if (!(layout[cell]&8) && (cell%16)) continue;
                const auto setup=next_setup();
                const auto facing=garbage_facing();
                const auto position=static_cast<std::uint8_t>(cell-tables_.offset(facing&3,dungeon::ahead));
                auto m=run(dungeon::exit_lookup_unit,setup,[&](Machine& m) {
                    scene(m,facing,position,static_cast<std::uint8_t>(floor));
                    for (unsigned i=0;i<256;++i) put_cell(m,static_cast<std::uint8_t>(i),layout[i]);
                    poke(m,dungeon::game_mode,0x0B);
                });
                if (m && is_normal(setup)) expect_exit(*m,static_cast<std::uint8_t>(floor),static_cast<std::uint8_t>(cell),layout[cell]&8);
            }
        }
        for (unsigned value=0;value<256;++value)
            for (unsigned k=0;k<3;++k) {
                const auto facing=garbage_facing();
                const std::uint8_t position=byte(), cell=target(facing,position,dungeon::ahead);
                const auto floor=k==0 ? static_cast<std::uint8_t>(dungeon::floor_count+byte()%190) : static_cast<std::uint8_t>(byte()%dungeon::floor_count);
                const auto setup=next_setup();
                auto m=run(dungeon::exit_lookup_unit,setup,[&](Machine& m) {
                    scene(m,facing,position,floor);
                    put_cell(m,cell,static_cast<std::uint8_t>(value));
                    poke(m,dungeon::game_mode,0x0B);
                });
                if (m && is_normal(setup)) expect_exit(*m,floor,cell,value&8);
            }
    }
    // A table of five-byte records in cartridge RAM at slot 2, entered at the
    // continuation: terminators and matches anywhere, every kind byte.
    void exit_synthetic(unsigned count) {
        for (unsigned i=0;i<count;++i) {
            auto setup=next_setup(); setup.cartridge_ram=true; setup.banks[2]=3; setup.work_ram_disabled=false;
            if (setup.sp<0xCC10 || setup.sp>=0xDF20) setup.sp=0xDF00;
            const std::uint8_t floor=byte(), cell=byte();
            const unsigned n=1+rng_()%12, base=0x8000+(rng_()%0x400);
            const unsigned first=rng_()%(n+1);
            std::vector<std::uint8_t> bytes;
            for (unsigned r=0;r<n;++r) {
                const bool same_floor=rng_()%2, same_cell=rng_()%4==0;
                std::uint8_t f=same_floor ? floor : byte();
                if (f==0xFF) f=0xFE;
                const std::uint8_t c=same_cell ? cell : byte();
                static const std::uint8_t kinds[]{0x00,0x7F,0x80,0xFC,0xFD,0xFE,0xFF,0x40,0xA0};
                const std::uint8_t kind=rng_()%2 ? kinds[rng_()%9] : byte();
                for (const auto v:{f,c,kind,byte(),byte()}) bytes.push_back(v);
            }
            bytes.push_back(0xFF);
            if (rng_()%3==0) bytes[5*(rng_()%n)]=0xFF;   // an earlier terminator
            run({1,0x68E2},setup,[&](Machine& m) {
                for (unsigned k=0;k<bytes.size();++k) poke(m,static_cast<std::uint16_t>(base+k),bytes[k]);
                for (unsigned k=bytes.size();k<bytes.size()+8;++k) poke(m,static_cast<std::uint16_t>(base+k),byte());
                check(m.memory().read(static_cast<std::uint16_t>(base))==bytes[0],"the synthetic table is in slot 2");
                set_hl(m,static_cast<std::uint16_t>(base+5*first));
                set_b(m,floor); set_c(m,cell); set_d(m,0); set_e(m,4);
                poke(m,dungeon::game_mode,0x0B);
                lay_stack(m,{0x0180});
            });
        }
    }
    // The exit kinds after a match: D holds the kind; HL and AF are what the
    // native world-map and fade calls leave.
    void exit_tails() {
        for (unsigned kind=0;kind<256;++kind)
            for (unsigned k=0;k<4;++k)
                run({1,0x68F7},next_setup(),[&](Machine& m) { set_d(m,static_cast<std::uint8_t>(kind)); lay_stack(m,{0x0180}); });
        for (unsigned k=0;k<40;++k) {
            run({1,0x6900},next_setup(),[&](Machine& m) { lay_stack(m,{static_cast<std::uint16_t>(rng_()),0x0180}); });
            run({1,0x690E},next_setup(),[&](Machine& m) { lay_stack(m,{static_cast<std::uint16_t>(rng_()),0x0180}); });
            run({1,0x6914},next_setup(),[&](Machine& m) { lay_stack(m,{static_cast<std::uint16_t>(rng_()),0x0180}); });
            run({1,0x6939},next_setup(),[&](Machine& m) { lay_stack(m,{static_cast<std::uint16_t>(rng_()),0x0180}); });
            run({1,0x6960},next_setup(),[&](Machine& m) { lay_stack(m,{static_cast<std::uint16_t>(rng_()),0x0180}); });
        }
        // 01:6925: HL held (the record the lookup matched); the event path follows into 01:6A2F.
        for (unsigned k=0;k<200;++k)
            run({1,0x6925},next_setup(),[&](Machine& m) {
                set_hl(m,static_cast<std::uint16_t>(rng_()));
                lay_stack(m,{static_cast<std::uint16_t>(0x8000+rng_()%0x3000),0x0180});
            });
        // 01:6930: the game mode after the event: 0Bh or any other.
        for (unsigned mode=0;mode<256;++mode)
            for (unsigned k=0;k<2;++k)
                run({1,0x6930},next_setup(),[&](Machine& m) { poke(m,dungeon::game_mode,static_cast<std::uint8_t>(mode)); lay_stack(m,{0x0180}); });
        // 01:694C: AF held (A = kind), HL above it.
        for (unsigned kind=0;kind<256;++kind)
            for (unsigned k=0;k<3;++k)
                run({1,0x694C},next_setup(),[&](Machine& m) {
                    lay_stack(m,{static_cast<std::uint16_t>((kind<<8)|byte()),static_cast<std::uint16_t>(0x8000+rng_()%0x3000),0x0180});
                });
    }

    // ---- 01:6963 and its segments: the object check ----
    // The first record of the table at a cell, with the given view index.
    void object_scene(Machine& m, std::uint8_t floor, std::uint8_t cell, unsigned view, std::uint8_t value) {
        const auto facing=garbage_facing();
        const auto position=static_cast<std::uint8_t>(cell-tables_.offset(facing&3,view));
        scene(m,facing,position,floor); put_cell(m,cell,value);
        poke_word(m,0xC2E1,sentinel); poke(m,0xC2D2,0); poke(m,0xC2DF,0x5A); poke(m,0xC2E0,0xA5); poke(m,0xC2E6,0x33);
        poke_word(m,0xC2DD,0x4321);
        set_b(m,static_cast<std::uint8_t>(view));
        lay_stack(m,{0x0180});
    }
    std::size_t first_index(std::uint8_t floor, std::uint8_t cell) const {
        const auto objects=tables_.objects();
        for (std::size_t i=0;i<objects.size();++i) if (objects[i].floor==floor && objects[i].cell==cell) return i;
        throw std::runtime_error("no object record");
    }
    void object_records() {
        const auto objects=tables_.objects();
        const auto table=dungeon::image_offset(dungeon::objects_table);
        std::size_t shadowed=0, dead=0;
        for (std::size_t i=0;i<objects.size();++i) {
            shadowed+=first_index(objects[i].floor,objects[i].cell)!=i;
            dead+=objects[i].type==dungeon::fixed_fight && tables_.layout(objects[i].floor)[objects[i].cell]!=8;   // a fight on a cell no object read can reach
        }
        check(shadowed==10 && dead==5,"the tables hold 10 shadowed records and 5 dead fight records, found "+std::to_string(shadowed)+" and "+std::to_string(dead));
        for (const auto& record:objects)
            for (const unsigned view:{dungeon::ahead,dungeon::behind})
                for (unsigned done=0;done<3;++done) {
                    const auto setup=next_setup();
                    const auto first=first_index(record.floor,record.cell);
                    const auto flag=objects[first].flag;
                    const auto value=static_cast<std::uint8_t>(8|(rng_()&1 ? 0x80 : 0));
                    const auto done_byte=done_value(done);
                    auto m=run(dungeon::object_check_unit,setup,[&](Machine& m) {
                        object_scene(m,record.floor,record.cell,view,value);
                        poke(m,flag,done_byte);
                    });
                    if (!m || !is_normal(setup)) continue;
                    if (done_byte==0xFF) {
                        check(word_at(*m,0xC2E1)==sentinel,"a done record changes nothing");
                        continue;
                    }
                    // The first record of the cell wins, also where the cell has two.
                    check(word_at(*m,0xC2E1)==flag && m->peek(0xC2D2)==0xFF,"the first record's done flag is remembered");
                    const auto* image=harness.rom().image->data()+table+7*first;
                    const std::uint8_t type=image[4], d1=image[5], d2=image[6];
                    check(type==objects[first].type,"the record's type is its fifth byte");
                    if (type==dungeon::item_chest)
                        check(m->peek(0xC2DF)==d1 && m->peek(0xC2E0)==d2 && word_at(*m,0xC2DD)==0,"an item chest remembers its item and amount");
                    else if (type==dungeon::money_chest)
                        check(m->peek(0xC2DF)==0 && word_at(*m,0xC2DD)==static_cast<std::uint16_t>(d1|(d2<<8)),"a money chest remembers its sum");
                    else if (type==dungeon::fixed_fight && view==dungeon::ahead)
                        check(m->peek(0xC2E6)==d1,"a fight ahead reads its group");
                }
    }
    // Cells that are not objects, floors without records and cells on a real
    // floor with no record there.
    void object_misses() {
        for (unsigned value=0;value<256;++value)
            for (unsigned k=0;k<3;++k) {
                const auto facing=garbage_facing();
                const std::uint8_t position=byte(), cell=target(facing,position,k==2 ? dungeon::behind : dungeon::ahead);
                run(dungeon::object_check_unit,next_setup(),[&](Machine& m) {
                    scene(m,facing,position,static_cast<std::uint8_t>(byte()%dungeon::floor_count));
                    put_cell(m,cell,static_cast<std::uint8_t>(value));
                    set_b(m,k==2 ? dungeon::behind : dungeon::ahead);
                    lay_stack(m,{0x0180});
                });
            }
        for (unsigned floor=0;floor<256;++floor)
            for (unsigned k=0;k<(floor<dungeon::floor_count ? 4u : 1u);++k) {
                const std::uint8_t cell=byte();
                run(dungeon::object_check_unit,next_setup(),[&](Machine& m) {
                    object_scene(m,static_cast<std::uint8_t>(floor),cell,k%2 ? dungeon::behind : dungeon::ahead,8);
                });
            }
    }
    // A table of seven-byte records in cartridge RAM at slot 2, entered at the
    // continuation with BC held: every type byte, terminators and matches
    // anywhere (the last record included), done flags and view indexes.
    void object_synthetic(unsigned count) {
        for (unsigned i=0;i<count;++i) {
            auto setup=next_setup(); setup.cartridge_ram=true; setup.banks[2]=3; setup.work_ram_disabled=false;
            if (setup.sp<0xCC10 || setup.sp>=0xDF20) setup.sp=0xDF00;
            const std::uint8_t floor=byte(), cell=byte();
            const unsigned views[]{dungeon::ahead,dungeon::behind,byte(),0};
            const unsigned view=views[rng_()%4];
            const unsigned n=1+rng_()%14, base=0x8000+(rng_()%0x400), first=rng_()%(n+1);
            const unsigned match=rng_()%(n+2);   // the record that matches; n and above: none
            std::vector<std::uint8_t> bytes;
            std::vector<std::uint16_t> flags;
            for (unsigned r=0;r<n;++r) {
                const bool same_floor=(r==match) || rng_()%2, same_cell=(r==match);
                std::uint8_t f=same_floor ? floor : byte();
                if (f==0xFF) f=0xFE;
                const std::uint8_t c=same_cell ? cell : static_cast<std::uint8_t>(cell+1+rng_()%255);
                static const std::uint8_t types[]{0,1,2,3,4,7,0x80,0xFF};
                const std::uint8_t type=rng_()%4==0 ? byte() : types[rng_()%8];
                const std::uint16_t flag=rng_()%8 ? static_cast<std::uint16_t>(dungeon::object_flags_first+rng_()%(dungeon::object_flags_last-dungeon::object_flags_first))
                                                  : static_cast<std::uint16_t>(rng_());
                flags.push_back(flag);
                const std::uint8_t d1=rng_()%3==0 ? 0 : byte();
                for (const auto v:{f,c,static_cast<std::uint8_t>(flag),static_cast<std::uint8_t>(flag>>8),type,d1,byte()}) bytes.push_back(v);
            }
            bytes.push_back(0xFF);
            if (rng_()%4==0) bytes[7*(rng_()%n)]=0xFF;   // an earlier terminator
            run({1,0x6989},setup,[&](Machine& m) {
                for (unsigned k=0;k<bytes.size();++k) poke(m,static_cast<std::uint16_t>(base+k),bytes[k]);
                for (unsigned k=bytes.size();k<bytes.size()+8;++k) poke(m,static_cast<std::uint16_t>(base+k),byte());
                check(m.memory().read(static_cast<std::uint16_t>(base))==bytes[0],"the synthetic table is in slot 2");
                for (const auto flag:flags) poke(m,flag,done_value(rng_()%3));
                set_hl(m,static_cast<std::uint16_t>(base+7*first));
                set_b(m,floor); set_c(m,cell); set_d(m,0); set_e(m,6);
                poke_word(m,0xC2E1,sentinel);
                lay_stack(m,{static_cast<std::uint16_t>((view<<8)|cell),0x0180});
            });
        }
    }
    // The rest of the event part, entered where the native calls return.
    void object_tails() {
        const auto objects=tables_.objects();
        for (unsigned k=0;k<80;++k) {
            run({1,0x69D1},next_setup(),[&](Machine& m) { lay_stack(m,{0x0180}); });
            run({1,0x69D5},next_setup(),[&](Machine& m) { lay_stack(m,{static_cast<std::uint16_t>(rng_()),0x0180}); });
            run({1,0x6A07},next_setup(),[&](Machine& m) {
                lay_stack(m,{static_cast<std::uint16_t>(0x8000+rng_()%0x3000),static_cast<std::uint16_t>(rng_()),0x0180});
            });
            run({1,0x6A3F},next_setup(),[&](Machine& m) { lay_stack(m,{0x0180}); });
        }
        // The menu flag after a dialogue or a fight: zero returns, anything else goes on to the menu.
        for (unsigned value=0;value<256;++value)
            for (const std::uint16_t entry:{0x69D9,0x6A12})
                run({1,entry},next_setup(),[&](Machine& m) { poke(m,0xC800,static_cast<std::uint8_t>(value)); lay_stack(m,{0x0180}); });
        // After the party turned around for a fight or an event behind: the record
        // pointer comes back from the stack (the data bytes of every record, and others).
        const auto record_data=[&](std::size_t i) {
            return static_cast<std::uint16_t>(dungeon::objects_table.address+dungeon::object_record_bytes*i+5);
        };
        for (std::size_t i=0;i<objects.size();++i)
            for (unsigned k=0;k<3;++k) {
                run({1,0x69F1},next_setup(),[&](Machine& m) {
                    poke_word(m,0xC2E1,static_cast<std::uint16_t>(rng_()));
                    lay_stack(m,{record_data(i),0x0180});
                });
                run({1,0x6A29},next_setup(),[&](Machine& m) { lay_stack(m,{record_data(i),0x0180}); });
                run({1,0x6A2F},next_setup(),[&](Machine& m) { set_hl(m,record_data(i)); lay_stack(m,{0x0180}); });
            }
        for (unsigned k=0;k<200;++k) {
            const auto pointer=static_cast<std::uint16_t>(0x8000+rng_()%0x4000);
            run({1,0x69F1},next_setup(),[&](Machine& m) { lay_stack(m,{pointer,0x0180}); });
            run({1,0x6A29},next_setup(),[&](Machine& m) { lay_stack(m,{pointer,0x0180}); });
            run({1,0x6A2F},next_setup(),[&](Machine& m) { set_hl(m,pointer); lay_stack(m,{0x0180}); });
        }
    }

    // The stack matrix: while a placement is forced, every setup uses it and
    // each starting entry runs only `quota` probes (the first ones its families make).
    void force_stack(std::optional<std::size_t> placement, unsigned quota) { forced_=placement; quota_=quota; used_.clear(); }
    static std::size_t stack_placements() { return probe::stack_cases().size(); }
    void everything() {
        door(); exit_records(); exit_cells(); exit_synthetic(1500); exit_tails();
        object_records(); object_misses(); object_synthetic(3000); object_tails();
    }

private:
    const game::DungeonTables& tables_;
    Stats& stats_;
    std::mt19937_64 rng_;
    std::uint64_t seed_;
    unsigned serial_=0;
    std::optional<std::size_t> forced_;
    unsigned quota_=0;
    std::map<std::uint16_t,unsigned> used_;
};

// A copy of the image with bytes rewritten at an offset: the first record of a
// table is the only one the entry segments can match, so its variants are
// probed this way (the verifier compares the original and C++ on the same copy).
probe::Rom patched(const probe::Rom& rom, std::size_t offset, std::initializer_list<std::uint8_t> bytes) {
    auto image=std::make_shared<Memory::Rom>(*rom.image);
    std::size_t at=offset;
    for (const auto value:bytes) (*image)[at++]=value;
    return probe::Rom{image,rom.sha1,rom.version};
}
void first_records(const probe::Rom& rom, const game::DungeonTables& tables, Stats& stats) {
    std::mt19937_64 random(0x6963);
    auto byte=[&] { return static_cast<std::uint8_t>(random()); };
    const auto object_table=dungeon::image_offset(dungeon::objects_table);
    const auto exit_table=dungeon::image_offset(dungeon::exits_table);
    unsigned lab_number=0;
    // The object check: the first record of every type, with and without its
    // data, for both views and every done state; an empty table.
    struct Variant { std::uint8_t type, d1; };
    for (const Variant v:{Variant{0,1},Variant{0,0},Variant{1,9},Variant{2,0},Variant{2,5},Variant{3,7},Variant{4,3},Variant{0xFF,3},Variant{0x80,0}}) {
        const std::uint8_t floor=byte()%dungeon::floor_count, cell=byte();
        const auto flag=static_cast<std::uint16_t>(dungeon::object_flags_first+random()%(dungeon::object_flags_last-dungeon::object_flags_first));
        Lab lab(patched(rom,object_table,{floor,cell,static_cast<std::uint8_t>(flag),static_cast<std::uint8_t>(flag>>8),v.type,v.d1,byte()}),
                tables,stats,0x6000+(++lab_number));
        for (const unsigned view:{dungeon::ahead,dungeon::behind})
            for (unsigned done=0;done<3;++done)
                for (unsigned k=0;k<4;++k)
                    lab.run(dungeon::object_check_unit,lab.next_setup(),[&](Machine& m) {
                        lab.object_scene(m,floor,cell,view,static_cast<std::uint8_t>(8|(k&1 ? 0x80 : 0)));
                        Lab::poke(m,flag,lab.done_value(done));
                    });
    }
    {
        Lab lab(patched(rom,object_table,{0xFF}),tables,stats,0x6100);
        for (unsigned k=0;k<12;++k)
            lab.run(dungeon::object_check_unit,lab.next_setup(),[&](Machine& m) {
                lab.object_scene(m,static_cast<std::uint8_t>(byte()%dungeon::floor_count),byte(),k%2 ? dungeon::behind : dungeon::ahead,8);
            });
    }
    // The exit lookup: the first record's kind at the edges of 80h and in the
    // special values; an empty table.
    for (const std::uint8_t kind:{0x00,0x7F,0x80,0x90,0xFC,0xFD,0xFE,0xFF}) {
        const std::uint8_t floor=byte()%dungeon::floor_count, cell=byte();
        Lab lab(patched(rom,exit_table,{floor,cell,kind,byte(),byte()}),tables,stats,0x6200+kind);
        for (unsigned k=0;k<10;++k)
            lab.run(dungeon::exit_lookup_unit,lab.next_setup(),[&](Machine& m) { lab.exit_scene(m,cell,floor,static_cast<std::uint8_t>(byte()|8)); });
    }
    {
        Lab lab(patched(rom,exit_table,{0xFF}),tables,stats,0x6300);
        for (unsigned k=0;k<12;++k)
            lab.run(dungeon::exit_lookup_unit,lab.next_setup(),[&](Machine& m) {
                lab.exit_scene(m,byte(),static_cast<std::uint8_t>(byte()%dungeon::floor_count),static_cast<std::uint8_t>(byte()|8));
            });
    }
}

void probes(const probe::Rom& rom) {
    const auto started=std::chrono::steady_clock::now();
    const game::DungeonTables tables(*rom.image);
    // The addresses the original instructions name are the ones dungeon.h names.
    auto operand=[&](std::uint16_t at) { return static_cast<std::uint16_t>(rom.byte(1,at)|(rom.byte(1,static_cast<std::uint16_t>(at+1))<<8)); };
    check(operand(0x696C)==dungeon::floor && operand(0x6970)==0xFFFF && rom.byte(1,0x6973)==3 &&
          operand(0x6975)==dungeon::objects_table.address && operand(0x6978)==dungeon::object_record_bytes-1,
          "the object check reads the floor, switches slot 2 to bank 3 and scans the table dungeon.h names");
    check(operand(0x68CF)==dungeon::exits_table.address && operand(0x68D2)==dungeon::exit_record_bytes-1 && operand(0x68C6)==dungeon::floor &&
          operand(0x68E7)==dungeon::game_mode && rom.byte(1,0x68EA)==8 && rom.byte(1,0x68F1)==dungeon::world_map_kinds_end,
          "the exit lookup scans the table dungeon.h names and branches at 80h");
    Stats stats;
    Lab lab(rom,tables,stats,0x7AB1);
    auto phase=[&](const char* name, const std::function<void()>& work) {
        const auto before=stats.calls;
        const auto t=std::chrono::steady_clock::now();
        work();
        std::cout<<"  "<<name<<": "<<stats.calls-before<<" calls, "
                 <<std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count()<<" s\n";
    };
    phase("door",[&] { lab.door(); });
    phase("exit records",[&] { lab.exit_records(); });
    phase("exit cells",[&] { lab.exit_cells(); });
    phase("exit synthetic",[&] { lab.exit_synthetic(1500); });
    phase("exit tails",[&] { lab.exit_tails(); });
    phase("object records",[&] { lab.object_records(); });
    phase("object misses",[&] { lab.object_misses(); });
    phase("object synthetic",[&] { lab.object_synthetic(3000); });
    phase("object tails",[&] { lab.object_tails(); });
    phase("first records",[&] { first_records(rom,tables,stats); });
    phase("stack placements",[&] {
        for (std::size_t placement=0;placement<Lab::stack_placements();++placement) {
            lab.force_stack(placement,6);
            lab.everything();
        }
        lab.force_stack(std::nullopt,0);
    });
    // Every entry was called in C++, and every declared exit was taken.
    for (const auto& segment:dungeon::object_segments) {
        const auto entry=segment.entry;
        check(stats.per_entry[entry.address]>0,"a segment was never called in C++: "+std::to_string(entry.address));
        for (const auto& exit:lab.harness.routine(entry).spec.exits)
            check(stats.exits[entry.address].count(exit.address)>0,
                  "an exit was never taken: entry "+std::to_string(entry.address)+", exit "+std::to_string(exit.address));
    }
    std::cout<<stats.calls<<" calls in C++ against the original in "<<stats.chains<<" chains ("<<stats.fallbacks
             <<" fallbacks as expected, "<<stats.unpredictable<<" of them for a stack that cannot return the inline call), all 25 entries and every declared exit, in "
             <<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<" s.\n";
    for (const auto& [address,calls]:stats.per_entry)
        std::cout<<"  01:"<<std::hex<<address<<std::dec<<" "<<calls<<" calls, "<<stats.exits[address].size()<<" exits\n";
}
}

int main(int argc, char** argv) {
    try {
        if (argc==1) { registration(); std::cout<<"Dungeon object catalog checks passed.\n"; return 0; }
        if (argc!=3) throw std::runtime_error("Expected <rom> <us|br>.");
        const auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        probes(*rom);
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
