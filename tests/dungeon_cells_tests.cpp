// SPDX-License-Identifier: GPL-3.0-or-later
// The dungeon cell readers (01:6BCA, 01:6BE9) and the dungeon tables
// (milestone 7, part B).
//   no arguments              registration catalog, the rendering kit against
//                             the core, its exit helpers through the mechanism
//                             (authored programs), the table reader on authored images
//   <rom> <us|br> cells       state probes of both readers against the original
//   <rom> <us|br> tables [<other rom>]
//                             record counts and structural facts of the tables
//                             (counts only), and equality with the other version
// A ROM run exits 77 without the ROM.
#include "game/dungeon_kit.h"
#include "game/dungeon_tables.h"
#include "probe.h"
#include <algorithm>
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

void registration() {
    RoutineRegistry cells;
    game::register_dungeon_cells(cells);
    check(cells.entries().size()==dungeon::cell_segments.size(),"both cell readers are registered");
    for (const auto& [entry,unit]:dungeon::cell_segments) {
        const auto* routine=cells.find(entry);
        check(routine!=nullptr && routine->spec.unit==unit && routine->spec.exits.size()==1 &&
              routine->spec.exits[0].kind==RoutineExit::returned && !routine->spec.slot2_mapper &&
              !routine->spec.single_instruction && !routine->spec.force_admit,"cell reader declares one return and no permission");
    }
    check(cells.find(dungeon::cell_type_unit)->spec.exits[0].address==0x6BE8 && cells.find(dungeon::cell_type_unit)->spec.scratch_stack==2,
          "01:6BCA returns at 01:6BE8 with two scratch bytes (its pushed HL)");
    check(cells.find(dungeon::cell_value_unit)->spec.exits[0].address==0x6C05 && cells.find(dungeon::cell_value_unit)->spec.scratch_stack==0,
          "01:6BE9 returns at 01:6C05 without stack use");
    // The planned entries: distinct, in bank 1, each unit listed as its own
    // first entry, and the movement and object parts register only from their
    // own lists.
    std::set<RoutineEntry> all;
    auto list=[&](const auto& segments, const char* name) {
        std::set<RoutineEntry> units;
        for (const auto& [entry,unit]:segments) {
            check(all.insert(entry).second,std::string("dungeon entry listed twice in ")+name);
            check(entry.bank==1 && unit.bank==1,"dungeon entries are in bank 1");
            units.insert(unit);
        }
        for (const auto unit:units)
            check(std::any_of(segments.begin(),segments.end(),[&](const auto& s) { return s.entry==unit && s.unit==unit; }),
                  std::string("a unit of ")+name+" is not its own entry");
    };
    list(dungeon::cell_segments,"cells"); list(dungeon::movement_segments,"movement"); list(dungeon::object_segments,"objects");
    check(all.size()==2+26+25,"53 planned dungeon segments");
    auto registers_only=[](void (*registration)(RoutineRegistry&), const auto& segments) {
        RoutineRegistry registry;
        registration(registry);
        for (const auto& [entry,routine]:registry.entries())
            check(std::any_of(segments.begin(),segments.end(),[&](const auto& s) { return s.entry==entry && s.unit==routine.spec.unit; }),
                  "a dungeon segment is registered outside its part's list or unit");
    };
    registers_only(game::register_dungeon_movement,dungeon::movement_segments);
    registers_only(game::register_dungeon_objects,dungeon::object_segments);
}

// ---- The rendering kit against the core ----------------------------------------

// A bus over a machine of its own: memory accesses go to it directly, fetches
// and cycles are counted.
struct CountingBus {
    Machine& machine;
    std::uint64_t cycles=0, fetches=0;
    void fetch(std::uint16_t) { ++fetches; }
    std::uint8_t read(std::uint16_t address) { return machine.memory().read(address); }
    void write(std::uint16_t address, std::uint8_t value) { machine.memory().write(address,value); }
    void advance(std::uint64_t count) { cycles+=count; }
    std::uint64_t elapsed() const { return cycles; }
};
using Z=kit::Instructions<CountingBus>;
constexpr std::uint16_t code_at=0x1000;

struct KitCase {
    const char* name;
    std::function<std::vector<std::uint8_t>(std::uint8_t n)> code;            // n: an operand byte varied per state
    std::function<std::uint16_t(Z&)> render;                                  // returns the next PC
    std::function<void(Z80&, std::uint64_t)> arrange=[](Z80&, std::uint64_t) {};
};
// Every rendering is compared with one native instruction (all iterations of
// a block copy) from many register states: registers, flags with their
// undocumented bits, MEMPTR, SP, PC, work RAM, cycles and opcode fetches.
void kit_case(const KitCase& c, unsigned states) {
    for (std::uint64_t seed=0;seed<states;++seed) {
        const auto n=static_cast<std::uint8_t>(seed*37+11);
        auto rom=std::make_shared<Memory::Rom>(512*1024,0);
        const auto code=c.code(n);
        std::copy(code.begin(),code.end(),rom->begin()+code_at);
        Machine native(rom);
        (void)native.read_port(0xbf,native.cycles()); native.advance(1);
        auto& cpu=native.cpu();
        probe::randomize(cpu,seed+1);
        std::mt19937_64 random(seed*977+5);
        cpu.hl.uint16_value=static_cast<std::uint16_t>(0xC000+random()%0x0F00);
        cpu.de.uint16_value=static_cast<std::uint16_t>(0xD000+random()%0x0E00);
        cpu.sp.uint16_value=static_cast<std::uint16_t>(0xC100+random()%0x1D00);
        cpu.pc.uint16_value=code_at; cpu.iff1=cpu.iff2=0;
        for (std::uint16_t a=0xC000;a<0xE000;a+=7) native.memory().write(a,static_cast<std::uint8_t>(random()));
        c.arrange(cpu,seed);
        Machine rendered=native;
        const auto cycles=native.cycles(), fetches=native.opcode_fetches();
        do native.step(); while (native.cpu().pc.uint16_value==code_at && code.size()==2 && code[0]==0xED && code[1]==0xB0);
        CountingBus bus{rendered};
        auto registers=kit::load(rendered.cpu());
        Z z(bus,registers);
        const auto pc=c.render(z);
        kit::store(rendered.cpu(),registers);
        const auto& a=native.cpu(); const auto& b=rendered.cpu();
        std::string fields;
        auto compare=[&](const char* field, unsigned x, unsigned y) { if (x!=y) fields+=std::string(" ")+field; };
        compare("AF",a.af.uint16_value,b.af.uint16_value); compare("BC",a.bc.uint16_value,b.bc.uint16_value);
        compare("DE",a.de.uint16_value,b.de.uint16_value); compare("HL",a.hl.uint16_value,b.hl.uint16_value);
        compare("SP",a.sp.uint16_value,b.sp.uint16_value); compare("MEMPTR",a.memptr.uint16_value,b.memptr.uint16_value);
        compare("PC",a.pc.uint16_value,pc);
        compare("cycles",static_cast<unsigned>(native.cycles()-cycles),static_cast<unsigned>(bus.cycles));
        compare("fetches",static_cast<unsigned>(native.opcode_fetches()-fetches),static_cast<unsigned>(bus.fetches));
        if (native.memory().ram()!=rendered.memory().ram()) fields+=" RAM";
        check(fields.empty(),std::string("kit rendering of ")+c.name+" differs from the core (state "+std::to_string(seed)+"):"+fields);
    }
}
std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> list) { return list; }
std::uint16_t next(unsigned length) { return static_cast<std::uint16_t>(code_at+length); }

void kit_rendering() {
    constexpr std::uint8_t ram_low=0x34, ram_high=0xC5;   // an operand address in work RAM
    const std::uint16_t ram_word=0xC534, far=0x2345;
    auto both=[](Z80& cpu, std::uint64_t seed) { cpu.bc.uint16_value=static_cast<std::uint16_t>(1+seed%6); };
    const std::vector<KitCase> cases{
        {"ld c,l",[](auto) { return bytes({0x4D}); },[](Z& z) { z.copy(code_at,z.r.c,z.r.l); return next(1); }},
        {"ld b,n",[](auto n) { return bytes({0x06,n}); },[](Z& z) { z.load_immediate(code_at,z.r.b); return next(2); }},
        {"ld a,(hl)",[](auto) { return bytes({0x7E}); },[](Z& z) { z.load_from_hl(code_at,z.r.a); return next(1); }},
        {"ld (hl),a",[](auto) { return bytes({0x77}); },[](Z& z) { z.store_register_to_hl(code_at,z.r.a); return next(1); }},
        {"ld (hl),n",[](auto n) { return bytes({0x36,n}); },[](Z& z) { z.store_to_hl(code_at); return next(2); }},
        {"ld a,(nn)",[&](auto) { return bytes({0x3A,ram_low,ram_high}); },[](Z& z) { z.load_a_direct(code_at); return next(3); }},
        {"ld (nn),a",[&](auto n) { return bytes({0x32,n,ram_high}); },[](Z& z) { z.store_a_direct(code_at); return next(3); }},
        {"ld a,(de)",[](auto) { return bytes({0x1A}); },[](Z& z) { z.load_a_from_pair(code_at,z.de()); return next(1); }},
        {"ld (de),a",[](auto) { return bytes({0x12}); },[](Z& z) { z.store_a_to_pair(code_at,z.de()); return next(1); }},
        {"ld hl,nn",[](auto n) { return bytes({0x21,n,0x9A}); },[](Z& z) { z.load_hl(code_at); return next(3); }},
        {"ld de,nn",[](auto n) { return bytes({0x11,n,0x9A}); },[](Z& z) { z.load_de(code_at); return next(3); }},
        {"ld bc,nn",[](auto n) { return bytes({0x01,n,0x9A}); },[](Z& z) { z.load_bc(code_at); return next(3); }},
        {"ld hl,(nn)",[&](auto n) { return bytes({0x2A,n,ram_high}); },[](Z& z) { z.load_hl_direct(code_at); return next(3); }},
        {"ld (nn),hl",[&](auto n) { return bytes({0x22,n,ram_high}); },[](Z& z) { z.store_hl_direct(code_at); return next(3); }},
        {"ld de,(nn)",[&](auto n) { return bytes({0xED,0x5B,n,ram_high}); },[](Z& z) { z.load_pair_direct(code_at,z.r.d,z.r.e); return next(4); }},
        {"ld (nn),de",[&](auto n) { return bytes({0xED,0x53,n,ram_high}); },[](Z& z) { z.store_pair_direct(code_at,z.r.d,z.r.e); return next(4); }},
        {"inc hl",[](auto) { return bytes({0x23}); },[](Z& z) { z.increment_hl(code_at); return next(1); }},
        {"dec hl",[](auto) { return bytes({0x2B}); },[](Z& z) { z.decrement_hl(code_at); return next(1); }},
        {"inc bc",[](auto) { return bytes({0x03}); },[](Z& z) { z.increment_pair(code_at,z.r.b,z.r.c); return next(1); }},
        {"ex de,hl",[](auto) { return bytes({0xEB}); },[](Z& z) { z.exchange_de_hl(code_at); return next(1); }},
        {"push bc",[](auto) { return bytes({0xC5}); },[](Z& z) { z.push(code_at,z.r.b,z.r.c); return next(1); }},
        {"pop bc",[](auto) { return bytes({0xC1}); },[](Z& z) { z.pop(code_at,z.r.b,z.r.c); return next(1); }},
        {"push af",[](auto) { return bytes({0xF5}); },[](Z& z) { z.push(code_at,z.r.a,z.r.f); return next(1); }},
        {"pop af",[](auto) { return bytes({0xF1}); },[](Z& z) { z.pop(code_at,z.r.a,z.r.f); return next(1); }},
        {"push hl",[](auto) { return bytes({0xE5}); },[](Z& z) { z.push(code_at,z.r.h,z.r.l); return next(1); }},
        {"pop hl",[](auto) { return bytes({0xE1}); },[](Z& z) { z.pop(code_at,z.r.h,z.r.l); return next(1); }},
        {"add hl,de",[](auto) { return bytes({0x19}); },[](Z& z) { z.add_to_hl(code_at,z.de()); return next(1); }},
        {"add hl,bc",[](auto) { return bytes({0x09}); },[](Z& z) { z.add_to_hl(code_at,z.bc()); return next(1); }},
        {"add a,n",[](auto n) { return bytes({0xC6,n}); },[](Z& z) { z.add_immediate(code_at); return next(2); }},
        {"add a,b",[](auto) { return bytes({0x80}); },[](Z& z) { z.add_register(code_at,z.r.b); return next(1); }},
        {"add a,a",[](auto) { return bytes({0x87}); },[](Z& z) { z.add_register(code_at,z.r.a); return next(1); }},
        {"add a,(hl)",[](auto) { return bytes({0x86}); },[](Z& z) { z.add_from_hl(code_at); return next(1); }},
        {"adc a,h",[](auto) { return bytes({0x8C}); },[](Z& z) { z.add_with_carry(code_at,z.r.h); return next(1); }},
        {"adc a,n",[](auto n) { return bytes({0xCE,n}); },[](Z& z) { z.add_with_carry_immediate(code_at); return next(2); }},
        {"sub n",[](auto n) { return bytes({0xD6,n}); },[](Z& z) { z.subtract_immediate(code_at); return next(2); }},
        {"sub l",[](auto) { return bytes({0x95}); },[](Z& z) { z.subtract_register(code_at,z.r.l); return next(1); }},
        {"sbc a,b",[](auto) { return bytes({0x98}); },[](Z& z) { z.subtract_with_carry(code_at,z.r.b); return next(1); }},
        {"cp n",[](auto n) { return bytes({0xFE,n}); },[](Z& z) { z.compare_immediate(code_at); return next(2); }},
        {"cp b",[](auto) { return bytes({0xB8}); },[](Z& z) { z.compare_register(code_at,z.r.b); return next(1); }},
        {"cp (hl)",[](auto) { return bytes({0xBE}); },[](Z& z) { z.compare_from_hl(code_at); return next(1); }},
        {"and n",[](auto n) { return bytes({0xE6,n}); },[](Z& z) { z.and_immediate(code_at); return next(2); }},
        {"and b",[](auto) { return bytes({0xA0}); },[](Z& z) { z.and_register(code_at,z.r.b); return next(1); }},
        {"or n",[](auto n) { return bytes({0xF6,n}); },[](Z& z) { z.or_immediate(code_at); return next(2); }},
        {"or c",[](auto) { return bytes({0xB1}); },[](Z& z) { z.or_register(code_at,z.r.c); return next(1); }},
        {"or a",[](auto) { return bytes({0xB7}); },[](Z& z) { z.or_a(code_at); return next(1); }},
        {"xor b",[](auto) { return bytes({0xA8}); },[](Z& z) { z.xor_register(code_at,z.r.b); return next(1); }},
        {"xor a",[](auto) { return bytes({0xAF}); },[](Z& z) { z.xor_a(code_at); return next(1); }},
        {"neg",[](auto) { return bytes({0xED,0x44}); },[](Z& z) { z.negate(code_at); return next(2); }},
        {"inc a",[](auto) { return bytes({0x3C}); },[](Z& z) { z.increment(code_at,z.r.a); return next(1); }},
        {"dec a",[](auto) { return bytes({0x3D}); },[](Z& z) { z.decrement(code_at,z.r.a); return next(1); }},
        {"inc h",[](auto) { return bytes({0x24}); },[](Z& z) { z.increment(code_at,z.r.h); return next(1); }},
        {"rrca",[](auto) { return bytes({0x0F}); },[](Z& z) { z.rotate_right_circular_a(code_at); return next(1); }},
        {"srl b",[](auto) { return bytes({0xCB,0x38}); },[](Z& z) { z.shift_right_logical(code_at,z.r.b); return next(2); }},
        {"rr c",[](auto) { return bytes({0xCB,0x19}); },[](Z& z) { z.rotate_right_through_carry(code_at,z.r.c); return next(2); }},
        {"bit 3,b",[](auto) { return bytes({0xCB,0x58}); },[](Z& z) { z.test_bit(code_at,3,z.r.b); return next(2); }},
        {"bit 0,c",[](auto) { return bytes({0xCB,0x41}); },[](Z& z) { z.test_bit(code_at,0,z.r.c); return next(2); }},
        {"bit 7,(hl)",[](auto) { return bytes({0xCB,0x7E}); },[](Z& z) { z.test_bit_at_hl(code_at,7); return next(2); }},
        {"bit 3,(hl)",[](auto) { return bytes({0xCB,0x5E}); },[](Z& z) { z.test_bit_at_hl(code_at,3); return next(2); }},
        {"set 7,(hl)",[](auto) { return bytes({0xCB,0xFE}); },[](Z& z) { z.set_bit_at_hl(code_at,7); return next(2); }},
        {"ldir",[](auto) { return bytes({0xED,0xB0}); },[](Z& z) { z.block_copy(code_at); return next(2); },both},
        {"jp nn",[](auto n) { return bytes({0xC3,n,0x23}); },[](Z& z) { return z.jump(code_at); }},
        {"jp z,nn",[](auto n) { return bytes({0xCA,n,0x23}); },[](Z& z) { return z.jump_if(code_at,z.zero()) ? z.r.wz : next(3); }},
        {"jp nc,nn",[](auto n) { return bytes({0xD2,n,0x23}); },[](Z& z) { return z.jump_if(code_at,!z.carry()) ? z.r.wz : next(3); }},
        {"jr e",[](auto n) { return bytes({0x18,n}); },[](Z& z) { z.relative(code_at); return z.r.wz; }},
        {"jr z,e",[](auto n) { return bytes({0x28,n}); },[](Z& z) { return z.relative_if(code_at,z.zero()) ? z.r.wz : next(2); }},
        {"jr nc,e",[](auto n) { return bytes({0x30,n}); },[](Z& z) { return z.relative_if(code_at,!z.carry()) ? z.r.wz : next(2); }},
        {"djnz e",[](auto n) { return bytes({0x10,n}); },[](Z& z) { return z.decrement_jump(code_at) ? z.r.wz : next(2); },
            [](Z80& cpu, std::uint64_t seed) { if (seed%3==0) cpu.bc.uint8_values.at_1=1; }},
        {"call nn",[&](auto) { return bytes({0xCD,0x45,0x23}); },[](Z& z) { return z.call(code_at); }},
        {"call z,nn",[&](auto) { return bytes({0xCC,0x45,0x23}); },[](Z& z) { return z.call_if(code_at,z.zero()) ? z.r.wz : next(3); }},
        {"ret",[](auto) { return bytes({0xC9}); },[](Z& z) { return z.return_from(code_at); }},
        {"ret z",[](auto) { return bytes({0xC8}); },[](Z& z) { return z.return_if(code_at,z.zero()) ? z.r.wz : next(1); }},
        {"ret nz (not taken)",[](auto) { return bytes({0xC0}); },[](Z& z) { z.return_not_taken(code_at); return next(1); },
            [](Z80& cpu, std::uint64_t) { cpu.af.uint8_values.at_0|=kit::zero_flag; }},
    };
    (void)ram_word; (void)far;
    for (const auto& c:cases) kit_case(c,300);
    std::cout<<cases.size()<<" kit renderings match the core over 300 states each.\n";
}

// ---- The kit's exit helpers through the mechanism ----------------------------------

// Authored programs in bank 0; each run starts at `pc` with a return address
// to 0180h on the stack and ends when execution reaches it.
std::shared_ptr<const Memory::Rom> authored(std::initializer_list<std::pair<std::uint16_t,std::vector<std::uint8_t>>> parts) {
    auto rom=std::make_shared<Memory::Rom>(512*1024,0);
    for (const auto& [at,code]:parts) std::copy(code.begin(),code.end(),rom->begin()+at);
    return rom;
}
Machine started(const std::shared_ptr<const Memory::Rom>& rom, std::uint16_t pc, std::uint64_t seed) {
    Machine m(rom);
    (void)m.read_port(0xbf,m.cycles()); m.advance(1);
    probe::randomize(m.cpu(),seed);
    m.cpu().sp.uint16_value=0xDF00; m.cpu().pc.uint16_value=pc; m.cpu().iff1=m.cpu().iff2=0;
    m.memory().write(0xDF00,0x80); m.memory().write(0xDF01,0x01);
    return m;
}
void until_return(Machine& m, RoutineRegistry* registry) {
    for (unsigned i=0;i<1000000 && m.cpu().pc.uint16_value!=0x0180;++i) registry ? (void)m.step(*registry) : (void)m.step();
    check(m.cpu().pc.uint16_value==0x0180,"authored program returned");
}
// Runs the program with the registry (every call verified) and without, and
// requires equal machines; returns the C++ and fallback calls.
std::pair<std::uint64_t,std::uint64_t> same_as_native(Machine cpp, RoutineRegistry& registry, const char* what) {
    replay::CallVerifier verifier("kit");
    registry.verifier=&verifier;
    Machine native=cpp;
    until_return(cpp,&registry); until_return(native,nullptr);
    registry.verifier=nullptr;
    std::string fields;
    for (const auto& difference:native.differences(cpp)) fields+=" "+difference.field;
    check(fields.empty(),std::string(what)+" differs from the original program:"+fields);
    std::uint64_t cpp_calls=0, fallbacks=0;
    for (const auto& [entry,routine]:registry.entries()) { cpp_calls+=routine.counters.cpp_calls; fallbacks+=routine.counters.fallback_calls; }
    return {cpp_calls,fallbacks};
}

constexpr std::uint16_t scan_loop=0x40, scan_exit=0x44, scan_jump=0x45, scan_found=0x49;
// A scan in the dungeon scans' shape: test a byte, leave on a match, step to
// the next record (the loop exit), jump back.
template<class Instr> kit::Exit scan_records(Instr& z, std::uint16_t after) {
    for (;;) {
        z.load_from_hl(scan_loop,z.r.a);
        z.compare_register(scan_loop+1,z.r.c);
        if (z.relative_if(scan_loop+2,z.zero())) return kit::returns(scan_found,kit::return_cycles);
        if (z.loop_exit(after)) return kit::continues(scan_exit,11);
        z.add_to_hl(scan_exit,z.de());
        (void)z.jump(scan_jump);
    }
}
// Test-only: an independent model of the cost of one chunk of a split table
// scan, by the mechanism's loop rule, to check the estimating bus against:
// the chunk ends at the first arrival at the loop exit with at least `after`
// cycles elapsed since the entry. `start` is the elapsed cycles when the first
// record's test begins (the entry's own setup, or the loop jump of a
// continuation); `record(i)` describes record i of this chunk, read with
// Machine::peek:
//   ScanRecord::next(c)   not the end: c cycles from the record's first
//                         instruction to the arrival at the loop exit
//   ScanRecord::stop(c)   the scan ends inside the segment: c cycles to the end
//                         of the segment, its final instruction included
// At a qualifying arrival the chunk ends with the loop exit's own instruction
// (`exit_cycles`, the record-step addition); otherwise the addition and the
// loop jump (`loop_cycles`) run in the body and the next record follows.
struct ScanRecord {
    bool stops; unsigned cycles;
    static ScanRecord next(unsigned to_exit) { return {false,to_exit}; }
    static ScanRecord stop(unsigned to_end) { return {true,to_end}; }
};
template<class Record>
std::uint64_t scan_chunk_cost(std::uint64_t start, std::uint16_t after, unsigned exit_cycles, unsigned loop_cycles, Record record) {
    std::uint64_t elapsed=start;
    for (unsigned i=0;;++i) {
        const ScanRecord step=record(i);
        if (step.stops) return elapsed+step.cycles;
        elapsed+=step.cycles;
        if (elapsed>=after) return elapsed+exit_cycles;
        elapsed+=exit_cycles+loop_cycles;
    }
}

std::uint64_t scan_bound(const Machine& m, std::uint64_t start, std::uint16_t after) {
    const auto hl=m.cpu().hl.uint16_value, de=m.cpu().de.uint16_value;
    const auto c=m.cpu().bc.uint8_values.at_0;
    return scan_chunk_cost(start,after,11,10,[&](unsigned i) {
        return m.peek(static_cast<std::uint16_t>(hl+i*de))==c ? ScanRecord::stop(23+10) : ScanRecord::next(18);
    });
}

void kit_mechanism() {
    using namespace kit;
    // Held words: a segment pushes one or two words and ends with a core CALL
    // into original code holding them; the continuation inherits and pops
    // them and returns.
    const auto chain=authored({{0x40,{0xC5,0xCD,0x80,0x00,0xC1,0xC9}},{0x50,{0xC5,0xE5,0xCD,0x80,0x00,0xE1,0xC1,0xC9}},
                               {0x80,{0x04,0x23,0xC9}}});
    for (std::uint64_t seed=1;seed<=50;++seed) {
        RoutineRegistry one;
        add_segment(one,{0,0x40},segment({0,0x40},{core_final_at(0x41,-2)},{.scratch_stack=2}),64,
            [](auto& z) { z.push(0x40,z.r.b,z.r.c); return continues(0x41,call_cycles); });
        add_segment(one,{0,0x44},segment({0,0x40},{returned_at(0x45,+2)},{.inherited_stack=2}),64,
            [](auto& z) { z.pop(0x44,z.r.b,z.r.c); return returns(0x45,return_cycles); });
        check(same_as_native(started(chain,0x40,seed),one,"one held word")==std::pair<std::uint64_t,std::uint64_t>{2,0},"both chain segments ran in C++");
        RoutineRegistry two;
        add_segment(two,{0,0x50},segment({0,0x50},{core_final_at(0x52,-4)},{.scratch_stack=4}),64,
            [](auto& z) { z.push(0x50,z.r.b,z.r.c); z.push(0x51,z.r.h,z.r.l); return continues(0x52,call_cycles); });
        add_segment(two,{0,0x55},segment({0,0x50},{returned_at(0x57,+4)},{.inherited_stack=4}),64,
            [](auto& z) { z.pop(0x55,z.r.h,z.r.l); z.pop(0x56,z.r.b,z.r.c); return returns(0x57,return_cycles); });
        check(same_as_native(started(chain,0x50,seed),two,"two held words")==std::pair<std::uint64_t,std::uint64_t>{2,0},"both two-word segments ran in C++");
    }
    // Conditional exits, each outcome: JR Z, JP Z and DJNZ decided by the core.
    const auto branches=authored({{0x60,{0xB7,0x28,0x02,0x04,0xC9,0x05,0xC9}},{0x70,{0x0C,0x10,0xFD,0xC9}},
                                  {0x90,{0xB7,0xCA,0x96,0x00,0x04,0xC9,0x05,0xC9}}});
    for (std::uint64_t seed=1;seed<=40;++seed) {
        RoutineRegistry r;
        add_segment(r,{0,0x60},segment({0,0x60},{conditional_at(0x61)}),64,
            [](auto& z) { z.or_a(0x60); return relative_if_exit(0x61,z.zero()); });
        add_segment(r,{0,0x70},segment({0,0x70},{conditional_at(0x71)}),64,
            [](auto& z) { z.increment(0x70,z.r.c); return decrement_jump_exit(z,0x71); });
        add_segment(r,{0,0x90},segment({0,0x90},{conditional_at(0x91)}),64,
            [](auto& z) { z.or_a(0x90); return jump_if_exit(0x91); });
        for (const std::uint16_t pc:{std::uint16_t{0x60},std::uint16_t{0x90}}) {
            auto m=started(branches,pc,seed);
            m.cpu().af.uint8_values.at_1=seed%2 ? 0 : static_cast<std::uint8_t>(seed);
            check(same_as_native(m,r,"a conditional exit").second==0,"conditional exits never fall back");
        }
        auto m=started(branches,0x70,seed);
        m.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(1+seed%4);
        check(same_as_native(m,r,"a DJNZ exit").second==0,"DJNZ exits never fall back");
    }
    // A split scan: scan_chunk_cost repeats the loop rule exactly (equal to
    // the estimated path at every chunk start), and a scan crossing a frame
    // boundary equals the run without routines, falling back only there.
    const auto scan=authored({{scan_loop,{0x7E,0xB9,0x28,0x05,0x19,0xC3,0x40,0x00,0x00,0xC9}}});
    std::uint64_t frame_fallbacks=0, chunks=0;
    for (const std::uint16_t after:{std::uint16_t{0},std::uint16_t{40},std::uint16_t{128},std::uint16_t{1000}}) {
        auto first=[after](auto& z) { return scan_records(z,after); };
        auto resume=[after](auto& z) { (void)z.jump(scan_jump); return scan_records(z,after); };
        const unsigned records=150;
        auto table=[&](Machine& m) {
            for (unsigned i=0;i<records;++i) m.memory().write(static_cast<std::uint16_t>(0xC200+i),static_cast<std::uint8_t>(0x10+i%7));
            m.memory().write(static_cast<std::uint16_t>(0xC200+records),0x99);
            m.cpu().hl.uint16_value=0xC200; m.cpu().de.uint16_value=1; m.cpu().bc.uint16_value=0x7799;
        };
        for (unsigned k=0;k<=records;++k) {
            auto m=started(scan,scan_loop,k+1); table(m);
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xC200+k);
            check(scan_bound(m,0,after)==cost(m,first,100000) && scan_bound(m,10,after)==cost(m,resume,100000),
                  "scan_chunk_cost equals the estimated chunk");
        }
        for (std::uint64_t distance=0;distance<7000;distance+=97) {
            RoutineRegistry r;
            const auto exits=std::vector<SegmentExit>{loop_at(scan_exit,after),returned_at(scan_found)};
            add_segment(r,{0,scan_loop},segment({0,scan_loop},exits),[after](const Machine& m) { return scan_bound(m,0,after); },first);
            add_segment(r,{0,scan_jump},segment({0,scan_loop},exits),[after](const Machine& m) { return scan_bound(m,10,after); },resume);
            auto m=started(scan,scan_loop,distance+3); table(m);
            m.advance(Machine::frame_cycles-m.cycles()%Machine::frame_cycles-distance%Machine::frame_cycles);
            const auto [cpp,fallbacks]=same_as_native(m,r,"a split scan");
            chunks+=cpp; frame_fallbacks+=fallbacks;
            check(cpp>1 || after>=1000,"the scan is split into chunks");
        }
    }
    check(frame_fallbacks>0,"some scans fell back at the frame boundary");
    std::cout<<"Kit exit helpers: held-word chains, conditional exits and split scans equal the original ("<<chunks
             <<" C++ chunks, "<<frame_fallbacks<<" frame fallbacks).\n";
}

// ---- The table reader on authored images -----------------------------------------

template<class F> bool throws(F&& f) {
    try { f(); } catch (const std::invalid_argument&) { return true; }
    return false;
}
void authored_tables() {
    std::vector<std::uint8_t> image(512*1024,0xFF);
    check(throws([&] { game::DungeonTables(std::span(image).first(512*1024-1)); }),"a short image is refused");
    const game::DungeonTables empty(image);
    check(empty.objects().empty() && empty.exits().empty() && !empty.object_at(0,0) && empty.layout(0)[0]==15 &&
          empty.offset(5,15)==-1,"terminators at the start give empty tables");
    bool range=false;
    try { (void)empty.offset(0,16); } catch (const std::out_of_range&) { range=true; }
    check(range,"an offset index of 16 is refused");
    range=false;
    try { (void)empty.layout(61); } catch (const std::out_of_range&) { range=true; }
    check(range,"floor 61 has no layout");
    const auto objects=dungeon::image_offset(dungeon::objects_table), exits=dungeon::image_offset(dungeon::exits_table);
    auto object=[&](std::size_t at, std::uint8_t floor, std::uint8_t cell, std::uint16_t flag, std::uint8_t type) {
        image[at]=floor; image[at+1]=cell; image[at+2]=static_cast<std::uint8_t>(flag); image[at+3]=static_cast<std::uint8_t>(flag>>8);
        image[at+4]=type; image[at+5]=image[at+6]=0;
    };
    object(objects,3,0x22,0xC50A,1); object(objects+7,3,0x22,0xC50B,0); object(objects+14,4,0x22,0xC6FF,2);
    image[exits]=60; image[exits+1]=0x10; image[exits+2]=0xFE;
    const game::DungeonTables two(image);
    check(two.objects().size()==3 && two.exits().size()==1 && two.object_at(3,0x22)->flag==0xC50A &&
          two.object_at(4,0x22)->type==2 && !two.object_at(4,0x23) && two.exit_at(60,0x10)->kind==0xFE,"records and first match");
    auto refused=[&](std::size_t at, std::uint8_t value) {
        const auto old=image[at]; image[at]=value;
        const bool result=throws([&] { game::DungeonTables{image}; });
        image[at]=old; return result;
    };
    check(refused(objects+7,61) && refused(exits,61),"a floor of 61 is refused");
    check(refused(objects+3,0xC7) && refused(objects+3,0xC2),"a done flag outside the game block is refused");
    for (auto at=objects;at<0x10000;++at) image[at]=0;
    for (auto at=objects;at+7<=0x10000;at+=7) object(at,0,0,0xC50A,0);
    check(throws([&] { game::DungeonTables{image}; }),"an object table without its end inside bank 3 is refused");
}

// ---- ROM: state probes of the cell readers -------------------------------------

struct Probes {
    probe::Harness& harness;
    const probe::Rom& rom;
    std::uint64_t count=0, fallbacks=0;
    std::map<std::uint8_t,std::uint64_t> values;   // target cell values met in checked probes

    std::uint16_t operand(std::uint16_t address) const {
        return static_cast<std::uint16_t>(rom.byte(1,address)|(rom.byte(1,static_cast<std::uint16_t>(address+1))<<8));
    }
    // The cell the readers address: position plus the signed offset at index B
    // of the facing's row (read from the image, as the original reads it).
    std::uint8_t target(std::uint8_t facing, std::uint8_t position, std::uint8_t b) const {
        const auto at=static_cast<std::uint16_t>(dungeon::view_offsets_table.address+(facing&3)*16+b);
        return static_cast<std::uint8_t>(position+rom.byte(1,at));
    }
    // One probe: a random layout, the party's position and facing, B and the
    // target cell's value; the call must run in C++ with the exact fixed cost,
    // and (when work RAM is readable) leave the documented result.
    void run(RoutineEntry entry, probe::Setup setup, std::uint8_t facing, std::uint8_t position, std::uint8_t b,
             std::uint8_t value, bool expect=true) {
        auto machine=harness.prepare(entry,setup);
        auto& memory=machine.memory();
        const bool ram=!setup.work_ram_disabled;
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()&~0x10);
        std::mt19937_64 random(setup.seed*0x2545F4914F6CDD1Dull+position);
        for (unsigned i=0;i<256;++i) memory.write(static_cast<std::uint16_t>(dungeon::layout+i),static_cast<std::uint8_t>(random()));
        memory.write(dungeon::facing,facing); memory.write(dungeon::position,position);
        const auto cell=target(facing,position,b);
        memory.write(static_cast<std::uint16_t>(dungeon::layout+cell),value);
        if (setup.work_ram_disabled) memory.set_memory_control(memory.memory_control()|0x10);
        machine.cpu().bc.uint8_values.at_1=b;
        const auto hl=machine.cpu().hl.uint16_value, sp=machine.cpu().sp.uint16_value;
        const bool type=entry==dungeon::cell_type_unit;
        const auto bound=harness.routine(entry).bound(machine);
        const auto result=harness.run_cpp(machine,entry);
        ++count;
        check(result.cycles==bound && bound==(type ? 159u : 138u) && result.fetches==(type ? 21u : 19u),
              "a cell reader's cost differs from its fixed path");
        if (!expect || !ram) return;
        const auto& cpu=machine.cpu();
        check(cpu.pc.uint16_value==setup.return_address && cpu.sp.uint16_value==static_cast<std::uint16_t>(sp+2),"the reader returns to its caller");
        check(cpu.bc.uint8_values.at_1==b && cpu.de.uint16_value==b,"B is kept and DE holds it");
        if (type) check(cpu.af.uint8_values.at_1==(value&7) && cpu.hl.uint16_value==hl,"01:6BCA gives the cell kind and keeps HL");
        else check(cpu.af.uint8_values.at_1==(value&0x7F) && cpu.hl.uint16_value==(dungeon::layout|cell),
                   "01:6BE9 gives the value without bit 7 and points HL at the cell");
        ++values[value];
    }
    void fallback(RoutineEntry entry, probe::Setup setup) {
        auto machine=harness.prepare(entry,setup);
        harness.run_fallback(machine,entry,RoutineFallback::unmapped_stack);
        ++fallbacks;
    }
};

void cell_probes(const probe::Rom& rom) {
    probe::Harness harness(rom);
    Probes p{harness,rom,0,0,{}};
    // The addresses the original instructions name are the ones dungeon.h names.
    for (const std::uint16_t at:{std::uint16_t{0x6BCB},std::uint16_t{0x6BE9}}) {
        check(p.operand(at+1)==dungeon::facing && p.operand(at+19)==dungeon::position &&
              p.operand(at+13)==dungeon::view_offsets_table.address && rom.byte(1,at+23)==(dungeon::layout>>8),
              "the cell readers read the facing, position, offsets and layout dungeon.h names");
    }
    std::uint64_t seed=1;
    std::mt19937_64 random(0x6BCA);
    for (const auto entry:{dungeon::cell_type_unit,dungeon::cell_value_unit}) {
        // Every position with every facing byte (garbage high bits included)
        // at the index nearly every caller passes (1, ahead); the target value
        // runs through all 256.
        for (unsigned position=0;position<256;++position)
            for (unsigned facing=0;facing<256;++facing) {
                probe::Setup setup; setup.seed=seed++; setup.r=static_cast<std::uint8_t>(seed*13);
                setup.iff1=setup.iff2=(seed&1);
                p.run(entry,setup,static_cast<std::uint8_t>(facing),static_cast<std::uint8_t>(position),dungeon::ahead,
                      static_cast<std::uint8_t>(position+3*facing));
            }
        // Every position and facing with every index the callers pass (0-15:
        // 1-13 occur) and indexes reading past the facing's row; random high
        // bits and values.
        for (unsigned position=0;position<256;++position)
            for (unsigned facing=0;facing<4;++facing)
                for (const unsigned b:{0u,1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,14u,15u,16u,63u,64u,255u}) {
                    probe::Setup setup; setup.seed=seed++; setup.r=static_cast<std::uint8_t>(random());
                    p.run(entry,setup,static_cast<std::uint8_t>(facing|(random()&0xFC)),static_cast<std::uint8_t>(position),
                          static_cast<std::uint8_t>(b),static_cast<std::uint8_t>(random()));
                }
        // Slot-2 mappings and refresh-counter wraps.
        for (auto setup:probe::slot2_variants({}))
            for (const auto r:probe::r_wrap_values(21)) {
                setup.seed=seed++; setup.r=r;
                p.run(entry,setup,static_cast<std::uint8_t>(random()),static_cast<std::uint8_t>(random()),
                      static_cast<std::uint8_t>(random()%14),static_cast<std::uint8_t>(random()));
            }
        // Stack placements: the type reader pushes HL (two scratch bytes);
        // the value reader uses the stack only for its return.
        const unsigned scratch=entry==dungeon::cell_type_unit ? 2 : 0;
        for (const auto& placement:probe::stack_cases())
            for (unsigned k=0;k<8;++k) {
                probe::Setup setup; setup.seed=seed++; setup.sp=placement.sp; setup.cartridge_ram=placement.cartridge_ram;
                setup.work_ram_disabled=placement.work_ram_disabled;
                if (setup.cartridge_ram) setup.banks[2]=3;
                if (!placement.admitted(scratch)) { p.fallback(entry,setup); continue; }
                // A stack outside work RAM returns to whatever its bytes say.
                const bool own_return=placement.sp>=0xC000 && placement.sp<0xFFFC-1 && !placement.work_ram_disabled;
                p.run(entry,setup,static_cast<std::uint8_t>(random()),static_cast<std::uint8_t>(random()),
                      static_cast<std::uint8_t>(1+random()%13),static_cast<std::uint8_t>(random()),own_return);
            }
    }
    check(p.values.size()==256,"every target cell value is met");
    std::cout<<p.count<<" cell-reader probes in C++ against the original ("<<p.fallbacks
             <<" unmapped-stack fallbacks as expected), every position, facing byte and caller index.\n";
}

// ---- ROM: table facts (counts only) ---------------------------------------------

std::shared_ptr<const Memory::Rom> read_image(const char* path) {
    std::ifstream file(path,std::ios::binary);
    return std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
}
void table_facts(const probe::Rom& rom, const char* other) {
    const game::DungeonTables t(*rom.image);
    std::array<std::array<std::uint8_t,256>,dungeon::floor_count> layouts{};
    std::map<unsigned,unsigned> values;
    for (unsigned f=0;f<dungeon::floor_count;++f) { layouts[f]=t.layout(f); for (const auto v:layouts[f]) ++values[v]; }
    check(values.size()==14 && !values.contains(9) && !values.contains(15) && values.rbegin()->first==14,
          "values 0-8 and 10-14 occur, 9 and 15 never");
    check(values[8]==204 && values[2]==97 && values[3]==97,"204 object or pit cells, 97 stairs of each direction");
    std::set<std::array<std::uint8_t,256>> distinct(layouts.begin(),layouts.end());
    check(distinct.size()==dungeon::floor_count,"61 distinct layouts");

    const auto objects=t.objects();
    std::map<std::uint8_t,unsigned> types;
    std::set<std::uint16_t> flags;
    std::map<std::pair<unsigned,unsigned>,unsigned> per_cell;
    unsigned dead=0, dead_fights=0;
    for (const auto& o:objects) {
        ++types[o.type]; flags.insert(o.flag); ++per_cell[{o.floor,o.cell}];
        check(o.flag>=dungeon::object_flags_first && o.flag<=dungeon::object_flags_last,"done flags lie in the flag range");
        if (layouts[o.floor][o.cell]!=8) { ++dead; dead_fights+=o.type==dungeon::fixed_fight; }
    }
    check(objects.size()==186 && types.size()==4 && types[0]==95 && types[1]==54 && types[2]==25 && types[3]==12,
          "186 object records: 95 item chests, 54 money chests, 25 fixed fights, 12 events");
    check(flags.size()==186,"every object record has its own done flag");
    check(dead==5 && dead_fights==5,"five fixed-fight records are not on a value-8 cell");
    unsigned doubled=0;
    for (const auto& [cell,n]:per_cell) {
        doubled+=n>1; check(n<=2,"no cell carries three records");
        if (n==2) {
            const auto first=std::find_if(objects.begin(),objects.end(),[&](const auto& o) { return o.floor==cell.first && o.cell==cell.second; });
            check(t.object_at(static_cast<std::uint8_t>(cell.first),static_cast<std::uint8_t>(cell.second))==*first,"the first record wins");
        }
    }
    check(doubled==10,"ten cells carry two records");
    unsigned pits=0; std::set<unsigned> pit_floors;
    for (unsigned f=0;f<dungeon::floor_count;++f)
        for (unsigned c=0;c<256;++c)
            if (layouts[f][c]==8 && !t.object_at(static_cast<std::uint8_t>(f),static_cast<std::uint8_t>(c))) { ++pits; pit_floors.insert(f); }
    check(pits==33 && pit_floors.size()==22 && !pit_floors.contains(0),"33 pitfalls on 22 floors, none on floor 0");

    const auto exits=t.exits();
    unsigned dead_exits=0;
    std::set<std::pair<unsigned,unsigned>> exit_cells;
    for (const auto& e:exits) {
        check(exit_cells.insert({e.floor,e.cell}).second,"no cell carries two exit records");
        const auto v=layouts[e.floor][e.cell];
        if (!(dungeon::exit_bit(v) && v!=8)) { ++dead_exits; check(e.kind==0xFF,"the dead exit record starts a scene"); }
    }
    check(exits.size()==65 && dead_exits==1,"65 exit records, one on a cell without the exit bit");
    unsigned exit_bit_cells=0, without=0, world=0, special=0;
    for (unsigned f=0;f<dungeon::floor_count;++f)
        for (unsigned c=0;c<256;++c) {
            const auto v=layouts[f][c];
            if (v<10) continue;
            ++exit_bit_cells;
            const auto e=t.exit_at(static_cast<std::uint8_t>(f),static_cast<std::uint8_t>(c));
            if (!e) ++without; else if (e->kind<dungeon::world_map_kinds_end) ++world;
            else { ++special; check(e->kind>=0xFC,"kinds from 80h on are FCh-FFh"); }
        }
    check(exit_bit_cells==80 && without==16 && world==31 && special==33,
          "80 exit-bit cells: 16 without a record, 31 to the world map, 33 scenes, interactions or events");
    unsigned stairs=0;
    for (unsigned f=0;f<dungeon::floor_count;++f)
        for (unsigned c=0;c<256;++c) {
            const auto v=layouts[f][c];
            if (v!=2 && v!=3) continue;
            ++stairs;   // kind 2 adds one to the floor ID, kind 3 subtracts one (01:6715-6720)
            const int to=static_cast<int>(f)+(v==2 ? 1 : -1);
            check(to>=0 && to<static_cast<int>(dungeon::floor_count) && layouts[static_cast<unsigned>(to)][c]==(v==2 ? 3 : 2),
                  "stairs lead to the opposite stairs on the target floor");
        }
    check(stairs==194,"194 stairs cells without the exit bit");
    for (unsigned f=0;f<4;++f) {
        check(t.offset(f,dungeon::behind)==-t.offset(f,dungeon::ahead) && t.offset(f,0)==0,"behind is minus ahead, index 0 is the party");
        check(t.offset(f+4,dungeon::ahead)==t.offset(f,dungeon::ahead),"only the facing's low two bits count");
    }
    check(t.offset(0,1)==-16 && t.offset(1,1)==1 && t.offset(2,1)==16 && t.offset(3,1)==-1,"ahead is up, right, down, left");
    std::cout<<"Dungeon tables: 61 layouts, 186 object and 65 exit records, all structural counts as studied.\n";
    if (*other && std::filesystem::exists(other)) {
        const auto image=read_image(other);
        check(game::DungeonTables(*image)==t,"the other version's dungeon tables are equal");
        std::cout<<"The other version's tables are equal.\n";
    }
}
}

int main(int argc, char** argv) {
    try {
        if (argc==1) {
            registration();
            kit_rendering();
            kit_mechanism();
            authored_tables();
            std::cout<<"Dungeon cell catalog, kit and table reader checks passed.\n";
            return 0;
        }
        if (argc<4 || argc>5) throw std::runtime_error("Expected <rom> <us|br> cells|tables [<other rom>].");
        const auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        const std::string_view mode=argv[3];
        if (mode=="cells" && argc==4) cell_probes(*rom);
        else if (mode=="tables") table_facts(*rom,argc==5 ? argv[4] : "");
        else throw std::runtime_error("Expected cells or tables.");
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
