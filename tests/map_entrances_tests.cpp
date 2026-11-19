// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 11, part entrances: entrances, transfers and special ground.
//   no arguments                       registration catalog (no ROM)
//   <rom> <us|br> [<recordings folder> [<reference folder>]]
//                                      state probes (power-on machine, and a
//                                      real map frame when the recordings are
//                                      given) and scenario probes on that ROM
// A ROM run exits 77 without the ROM.
//
// The state probes start a machine at one entry, set the state the callers
// leave (registers, the work-RAM bytes the path reads, a stack with the
// caller's words) and dispatch it with the per-call verifier (the original
// runs first and the whole machines are compared). Where the original goes on
// into another segment of this part, the chain follows it; a native callee
// (the tile attributes, the inventory search, the vehicle sound, the
// character check) is run natively between two segments. The entrance table
// the scan reads is built in cartridge RAM mapped into slot 2 (so every
// record kind and every match rule can be set up), keys derived by running
// the original up to the loop head; the same scan also runs over real ROM
// data (bank 3) and over the real tables of real map frames.
#include "game/map_kit.h"
#include "map_probe.h"
#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

using namespace psmodern;
using probe::check;
namespace map=game::map;
namespace {
constexpr map::Part part=map::Part::entrances;

// ---- Work RAM of the part ------------------------------------------------------------
constexpr std::uint16_t ram_mode=0xC202, ram_flag=0xC265, ram_type=0xC29E, ram_none=0xC29D, ram_guard=0xC2D5,
    ram_mark=0xC2D7, ram_table=0xC2D9, ram_scene=0xC2DB, ram_tile=0xC2E5, ram_music=0xC2E6, ram_chest=0xC2F0,
    ram_cam_x=0xC301, ram_cam_y=0xC305, ram_area=0xC308, ram_location=0xC309, ram_facing=0xC30A, ram_position=0xC30C,
    ram_transport=0xC30E, ram_saved_y=0xC311, ram_saved_x=0xC313, ram_slot0=0xC800, ram_step_block=0xC26F,
    ram_stats=0xC400, ram_inventory=0xC4C0, ram_flag_506=0xC506;
constexpr std::uint16_t sentinel=0x0180;   // the word under the entry's return address

// ---- Registration catalog --------------------------------------------------------
struct Expected { std::uint16_t entry, unit; std::vector<SegmentExit> exits; unsigned scratch, inherited; bool mapper; bool single=false; };
std::vector<Expected> expected_catalog() {
    using game::map_kit::returned_at; using game::map_kit::core_final_at; using game::map_kit::conditional_at;
    using game::map_kit::loop_at;
    const auto scan=[](std::vector<SegmentExit> first) {
        first.insert(first.end(),{loop_at(0x7844,512),returned_at(0x7847),returned_at(0x78E7),core_final_at(0x7921)});
        return first;
    };
    return {
        {0x77AC,0x77AC,{returned_at(0x77B8),returned_at(0x77BB),core_final_at(0x77C1)},0,0,false},
        {0x77C4,0x77AC,scan({conditional_at(0x77D6)}),2,0,true},
        {0x77D9,0x77AC,{core_final_at(0x77E2),returned_at(0x780F)},0,0,false},
        {0x77E5,0x77AC,scan({core_final_at(0x77EC)}),2,0,true},
        {0x77EF,0x77AC,scan({core_final_at(0x77F6)}),2,0,true},
        {0x77F9,0x77AC,scan({core_final_at(0x7800)}),2,0,true},
        {0x7803,0x77AC,{returned_at(0x780F)},0,0,false},
        {0x7845,0x77AC,{loop_at(0x7844,512),returned_at(0x7847),returned_at(0x78E7),core_final_at(0x7921)},2,0,false},
        {0x7877,0x7877,{core_final_at(0x7921)},0,0,false},
        {0x787B,0x787B,{core_final_at(0x7921)},0,0,false},
        {0x7908,0x7908,{core_final_at(0x7921)},0,0,false},
        {0x7924,0x7908,{core_final_at(0x7924)},0,0,false,true},   // joining: the fill's seed store (milestone 14)
        {0x7928,0x7908,{returned_at(0x7929,2)},0,2,false},
        {0x792A,0x792A,{returned_at(0x7937),returned_at(0x793C),returned_at(0x7965),returned_at(0x7988),returned_at(0x7993),
                        returned_at(0x79A6),returned_at(0x79B9),returned_at(0x79C3),returned_at(0x79D3),returned_at(0x79D6),
                        core_final_at(0x7968),core_final_at(0x7994),core_final_at(0x79D9),core_final_at(0x7921)},0,0,false},
        {0x796B,0x792A,{returned_at(0x796B),core_final_at(0x7921)},0,0,false},
        {0x7997,0x792A,{core_final_at(0x79E6)},0,0,false},
        {0x79DC,0x792A,{returned_at(0x79DC),core_final_at(0x79DD)},0,0,false},
        {0x79E0,0x792A,{core_final_at(0x79E6)},0,0,false},
        {0x79E9,0x792A,{core_final_at(0x79EB)},0,0,false},
        {0x79EE,0x792A,{core_final_at(0x79F0)},0,0,false},
        {0x79F3,0x792A,{core_final_at(0x79F5)},0,0,false},
        {0x79F8,0x792A,{returned_at(0x79FA),core_final_at(0x7921)},0,0,false},
        {0x7A0C,0x7A0C,{core_final_at(0x7A0C)},2,0,false,true},   // joining: the aliveness call (milestone 14)
        {0x7A0F,0x7A0C,{returned_at(0x7A20)},0,0,false},
    };
}
void registration() {
    const auto entries=map_probe::registered_entries(part,game::register_map_entrances);
    std::cout<<entries.size()<<" of "<<map::planned_segments(part).size()<<" planned entrances segments registered.\n";
    check(entries.size()==24 && map::planned_segments(part).size()==24,"all 24 planned entrances segments are registered, and nothing else");
    RoutineRegistry registry;
    game::register_map_entrances(registry);
    const auto catalog=expected_catalog();
    check(catalog.size()==24,"the expected catalog lists 24 entries");
    for (const auto& expected:catalog) {
        const RoutineEntry entry{1,expected.entry};
        const auto* routine=registry.find(entry);
        check(routine!=nullptr,"a catalog entry is not registered");
        const auto& spec=routine->spec;
        check(spec.unit==RoutineEntry{1,expected.unit},"unit of a registered entry");
        check(spec.exits.size()==expected.exits.size(),"exit count of a registered entry");
        for (std::size_t i=0;i<expected.exits.size();++i)
            check(spec.exits[i].same_declaration(expected.exits[i]),"an exit of a registered entry is declared as expected");
        check(spec.scratch_stack==expected.scratch && spec.inherited_stack==expected.inherited && spec.slot2_mapper==expected.mapper,
              "the permissions of a registered entry");
        check(spec.single_instruction==expected.single,"only the joining one-instruction entrances segments are single-instruction, none is forced");
    }
}

// ---- The lab ---------------------------------------------------------------------------
struct Stats {
    std::uint64_t calls=0, fallbacks=0, chains=0;
    std::map<std::uint16_t,std::uint64_t> per_entry;
    std::map<std::uint16_t,std::set<std::uint16_t>> exits;      // last instruction the original ran, per entry
};
std::uint16_t word_at(const Machine& m, std::uint16_t address) {
    return static_cast<std::uint16_t>(m.peek(address)|(m.peek(static_cast<std::uint16_t>(address+1))<<8));
}
using map_probe::admitted;   // the machine dispatch's stack test, predicted (tests/map_probe.h)
bool is_entry(std::uint16_t pc) {
    for (const auto& segment:map::entrance_segments) if (segment.entry.address==pc) return true;
    return false;
}

class Lab {
public:
    using Builder=std::function<void(Machine&)>;
    Lab(probe::Rom rom, Stats& stats, std::uint64_t seed) : harness(std::move(rom)), stats_(stats), rng_(seed), seed_(seed) {}
    probe::Harness harness;

    static void poke(Machine& m, std::uint16_t address, std::uint8_t value) {
        if (address>=0xfffc) return;
        m.memory().write(address,value);
    }
    static void poke_word(Machine& m, std::uint16_t address, std::uint16_t value) {
        poke(m,address,static_cast<std::uint8_t>(value)); poke(m,static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
    }
    static void lay_stack(Machine& m, std::initializer_list<std::uint16_t> words) {
        auto sp=m.cpu().sp.uint16_value;
        for (const auto word:words) { poke_word(m,sp,word); sp=static_cast<std::uint16_t>(sp+2); }
    }
    static void set_a(Machine& m, std::uint8_t v) { m.cpu().af.uint8_values.at_1=v; }
    static void set_f(Machine& m, std::uint8_t v) { m.cpu().af.uint8_values.at_0=v; }
    static void set_b(Machine& m, std::uint8_t v) { m.cpu().bc.uint8_values.at_1=v; }
    static void set_c(Machine& m, std::uint8_t v) { m.cpu().bc.uint8_values.at_0=v; }
    static void set_d(Machine& m, std::uint8_t v) { m.cpu().de.uint8_values.at_1=v; }
    static void set_e(Machine& m, std::uint8_t v) { m.cpu().de.uint8_values.at_0=v; }
    static void set_hl(Machine& m, std::uint16_t v) { m.cpu().hl.uint16_value=v; }
    static std::uint8_t reg_d(const Machine& m) { return m.cpu().de.uint8_values.at_1; }
    static std::uint8_t reg_e(const Machine& m) { return m.cpu().de.uint8_values.at_0; }
    std::uint8_t byte() { return static_cast<std::uint8_t>(rng_()); }
    std::uint16_t word() { return static_cast<std::uint16_t>(rng_()); }
    unsigned below(unsigned n) { return static_cast<unsigned>(rng_()%n); }
    std::uint64_t serial() const { return serial_; }

    // The next setup of a rotation through slot-2 mappings, refresh-counter
    // wraps, interrupt flags and stack places (as the dungeon labs do).
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
        static const auto wraps=probe::r_wrap_values(900);
        s.r=(n%3==0) ? wraps[(n/3)%wraps.size()] : byte();
        s.sp=static_cast<std::uint16_t>(0xCC10+rng_()%0x1300);
        if (n%11==0) {
            static const auto cases=probe::stack_cases();
            const auto& c=cases[(n/11)%cases.size()];
            s.sp=c.sp; s.cartridge_ram=c.cartridge_ram; s.work_ram_disabled=c.work_ram_disabled;
            if (s.cartridge_ram) s.banks[2]=3;
        }
        return s;
    }
    // A setup with the table in cartridge RAM (the scan builds its own tables), a normal stack.
    probe::Setup synthetic_setup() {
        auto s=next_setup();
        s.cartridge_ram=true; s.banks[2]=3; s.work_ram_disabled=false;
        if (s.sp<0xCC10 || s.sp>=0xDF20) s.sp=0xDF00;
        return s;
    }

    // One call that must run in C++ against the original; the address of the
    // original's last instruction (the exit it took) is found by running a copy
    // of the machine for the same number of cycles.
    void step(Machine& m, RoutineEntry entry) {
        Machine original=m;
        const auto start=m.cycles();
        const bool ram_on=(m.memory().memory_control()&0x10)==0;
        const auto bound=harness.routine(entry).bound(m);
        const auto result=harness.run_cpp(m,entry);
        if (ram_on)
            check(result.cycles==bound,"entry "+std::to_string(entry.address)+" cost "+std::to_string(result.cycles)+" cycles, its bound said "+std::to_string(bound));
        std::uint16_t last=0;
        unsigned guard=0;
        while (original.cycles()-start<result.cycles && ++guard<20000) { last=original.cpu().pc.uint16_value; original.step(); }
        check(original.cycles()-start==result.cycles,"the original does not end on the C++ call's last cycle");
        ++stats_.calls; ++stats_.per_entry[entry.address]; stats_.exits[entry.address].insert(last);
    }
    // A dispatch the machine refuses (an unmapped or mapper stack, or a frame
    // boundary inside the bound), predicted from its own rules.
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
            return true;
        }
        return false;
    }
    // Runs the entry, then follows the original: into the next segment of this
    // part, or natively through a callee, until it leaves the part's entries
    // (the sentinel word) or the chain has used 40,000 cycles.
    void run(RoutineEntry entry, const probe::Setup& setup, const Builder& build) {
        auto m=harness.prepare(entry,setup);
        build(m);
        chain(entry,m);
    }
    void chain(RoutineEntry entry, Machine& m) {
        if (refused(m,entry)) return;
        const auto started=m.cycles();
        step(m,entry);
        ++stats_.chains;
        for (unsigned n=0;n<200 && m.cycles()-started<40000;++n) {
            for (unsigned i=0;i<3000 && m.cycles()-started<40000;++i) {
                const auto pc=m.cpu().pc.uint16_value;
                if (pc==sentinel || (is_entry(pc) && m.memory().mapping(pc)==Memory::Mapping{Memory::Kind::rom,1})) break;
                m.step();
            }
            const auto pc=m.cpu().pc.uint16_value;
            if (!is_entry(pc) || m.memory().mapping(pc)!=Memory::Mapping{Memory::Kind::rom,1}) break;
            const RoutineEntry next{1,pc};
            if (refused(m,next)) break;
            step(m,next);
        }
    }

    // ---- Scan tables ----
    // The key (D, E) the original has derived when it reaches the loop head
    // 01:7844, for the state the machine is in at an entry that scans.
    std::optional<std::pair<std::uint8_t,std::uint8_t>> scan_key(const Machine& at_entry) const {
        Machine copy=at_entry;
        for (unsigned i=0;i<600;++i) {
            if (copy.cpu().pc.uint16_value==0x7844) return std::pair{reg_d(copy),reg_e(copy)};
            if (copy.cpu().pc.uint16_value==sentinel) break;
            copy.step();
        }
        return std::nullopt;
    }
    // Whether record key (kd) matches D by either rule of the scan.
    static bool high_matches(std::uint8_t kd, std::uint8_t d) {
        if (kd==d) return true;
        unsigned a=static_cast<std::uint8_t>(kd+1);
        if ((a&0x0F)>=0x0C) a=(a+0x10)&0x70;
        return a==d;
    }
    // Writes a table of up to `count` six-byte records at `at` in slot 2 (cartridge
    // RAM), the record `hit` matching the key (when hit < count) with the given
    // kind; others get random keys that do not match, random kinds. The table
    // ends with FFh. Sets the table pointer.
    void build_table(Machine& m, std::uint16_t at, unsigned count, int hit, std::uint8_t kind, std::uint8_t kd_d, std::uint8_t ke_e) {
        auto address=at;
        for (unsigned i=0;i<count;++i) {
            std::uint8_t kd, ke, k;
            if (static_cast<int>(i)==hit) {
                std::vector<std::uint8_t> options;
                for (unsigned c=0;c<256;++c) if (high_matches(static_cast<std::uint8_t>(c),kd_d)) options.push_back(static_cast<std::uint8_t>(c));
                if (options.empty()) options.push_back(kd_d);
                kd=options[below(options.size())];
                ke=(byte()&1) ? ke_e : static_cast<std::uint8_t>(ke_e-1);
                k=kind;
            } else {
                do { kd=byte(); } while (kd==0xFF || (high_matches(kd,kd_d) && below(4)));   // mostly a wrong key; sometimes the right row
                ke=byte();
                if (high_matches(kd,kd_d) && (ke==ke_e || static_cast<std::uint8_t>(ke+1)==ke_e)) ke=static_cast<std::uint8_t>(ke_e+5);
                static constexpr std::uint8_t kinds[]{0x08,0x0A,0x0C,0x00,0x05,0x0B,0x80,0xFF};
                k=kinds[below(8)];
            }
            const std::uint8_t record[6]{kd,ke,k,byte(),byte(),byte()};
            for (const auto b:record) poke(m,address++,b);
        }
        poke(m,address,0xFF);
        poke_word(m,ram_table,at);
    }

    // ---- State: 01:77AC and what follows ----
    void common_ram(Machine& m) {
        for (const auto a:{ram_flag,ram_guard,ram_tile,ram_transport,ram_area,ram_location,ram_chest,ram_none,ram_music,ram_flag_506,ram_mode})
            poke(m,a,byte());
        poke_word(m,ram_cam_x,word()&0x7FF); poke_word(m,ram_cam_y,static_cast<std::uint16_t>((byte()&7)<<8|(byte()%0xC0)));
        poke_word(m,ram_saved_x,word()); poke_word(m,ram_saved_y,word());
        poke_word(m,ram_scene,word()); poke_word(m,ram_position,word()); poke(m,ram_facing,byte());
        poke(m,ram_type,byte());
        for (unsigned i=0;i<256;++i) poke(m,static_cast<std::uint16_t>(ram_slot0+i),byte());
        for (unsigned i=0;i<24;++i) poke(m,static_cast<std::uint16_t>(ram_step_block+i),byte());
        poke(m,ram_mark,byte());
    }
    void stack_words(Machine& m, bool with_caller=true) {
        // [SP] is the unit's return address (the caller of 77AC); the word above it the caller's caller.
        (void)with_caller;
        lay_stack(m,{static_cast<std::uint16_t>(0x0B1A),sentinel,word()});
    }

    void head() {                                     // 01:77AC, 01:7803, 01:77D9
        for (unsigned k=0;k<300;++k) {
            const auto flag=(k%3==0) ? 0 : byte(), guard=(k%4==0) ? 0 : (k%4==1 ? 0xFF : byte());
            run({1,0x77AC},next_setup(),[&](Machine& m) {
                common_ram(m); poke(m,ram_flag,static_cast<std::uint8_t>(flag)); poke(m,ram_guard,static_cast<std::uint8_t>(guard));
                stack_words(m);
            });
        }
        for (unsigned k=0;k<200;++k) {
            const auto transport=(k%3==0) ? 0 : byte();
            run({1,0x77D9},next_setup(),[&](Machine& m) { common_ram(m); poke(m,ram_transport,static_cast<std::uint8_t>(transport)); set_a(m,byte()); stack_words(m); });
            run({1,0x7803},next_setup(),[&](Machine& m) { common_ram(m); stack_words(m); });
        }
    }

    // The scanning entries with a synthetic table: 77C4 with the attribute in A
    // (bit 3: the scan; bit 2 only: special ground), 77E5, 77EF and 77F9 with
    // the attribute in A, 7845 resumed over a record.
    void scans() {
        const std::uint8_t kinds[]{0x08,0x0A,0x0C,0x00,0x05};
        for (unsigned k=0;k<1200;++k) {
            const auto setup=synthetic_setup();
            const auto which=k%4;
            const std::uint16_t entry=which==0 ? 0x77C4 : which==1 ? 0x77E5 : which==2 ? 0x77EF : 0x77F9;
            const std::uint8_t probe_offset=which==0 ? 0x14 : which==1 ? 0x12 : which==2 ? 0x0A : 0x0C;
            const bool scan_bit=below(8)!=0;
            const unsigned count=below(16), hit_choice=below(count+3);
            const int hit=hit_choice<count ? static_cast<int>(hit_choice) : -1;
            const auto kind=kinds[below(5)];
            auto m=harness.prepare({1,entry},setup);
            common_ram(m);
            set_a(m,static_cast<std::uint8_t>((scan_bit ? 0x08 : (below(2) ? 0x04 : 0)) | (byte()&0xF3)));
            set_e(m,probe_offset);
            lay_stack(m,{0x0B1A,sentinel,word()});
            const auto key=scan_key(m);
            const auto d=key ? key->first : byte(), e=key ? key->second : byte();
            if (setup.cartridge_ram) build_table(m,static_cast<std::uint16_t>(0x8000+below(0x3000)),count,hit,kind,d,e);
            chain({1,entry},m);
        }
        // 7845: a record in the middle of a scan (A = the first byte, HL at the record).
        for (unsigned k=0;k<600;++k) {
            const auto setup=synthetic_setup();
            const unsigned count=1+below(12);
            const int hit=below(3) ? static_cast<int>(below(count)) : -1;
            const auto kind=kinds[below(5)];
            const auto d=byte(), e=byte();
            const std::uint16_t table=static_cast<std::uint16_t>(0x8000+below(0x3000));
            auto m=harness.prepare({1,0x7845},setup);
            common_ram(m);
            build_table(m,table,count,hit,kind,d,e);
            set_d(m,d); set_e(m,e); set_hl(m,table); set_a(m,m.peek(table));
            lay_stack(m,{0x0B1A,sentinel,0});
            chain({1,0x7845},m);
        }
    }

    // ---- 01:7877, 01:787B, 01:7908 ----
    void transfers() {
        const auto pointer=[&](Machine& m, bool cartridge) {
            const std::uint16_t at=cartridge ? static_cast<std::uint16_t>(0x8000+below(0x3000)) : static_cast<std::uint16_t>(0x4000+below(0x3F00));
            if (cartridge) for (unsigned i=0;i<8;++i) poke(m,static_cast<std::uint16_t>(at+i),byte());
            return at;
        };
        for (unsigned k=0;k<600;++k) {
            const bool cartridge=(k&1);
            auto setup=next_setup(); if (cartridge) { setup.cartridge_ram=true; setup.banks[2]=3; }
            const RoutineEntry entry=(k%3==0) ? RoutineEntry{1,0x7877} : (k%3==1 ? RoutineEntry{1,0x787B} : RoutineEntry{1,0x7908});
            run(entry,setup,[&](Machine& m) {
                common_ram(m);
                if (entry.address!=0x7908) {
                    // HL at an entrance record's mode byte (7877) or area byte (787B); the Y and X bytes
                    // decide the camera branches: spread them over the byte range.
                    auto at=pointer(m,cartridge);
                    if (cartridge) { poke(m,at+1,byte()); poke(m,at+2,static_cast<std::uint8_t>(k*37)); poke(m,at+3,static_cast<std::uint8_t>(k*91)); }
                    set_hl(m,at); set_a(m,byte());
                }
                lay_stack(m,{static_cast<std::uint16_t>(0x0B1A),sentinel,0});
            });
        }
        // The camera Y byte over its whole range (borrow and range branches), through a cartridge record.
        for (unsigned y=0;y<256;++y) {
            auto setup=synthetic_setup();
            run({1,0x787B},setup,[&](Machine& m) {
                common_ram(m);
                const std::uint16_t at=0x8100;
                poke(m,at,byte()); poke(m,at+1,static_cast<std::uint8_t>(y)); poke(m,at+2,static_cast<std::uint8_t>(byte()));
                set_hl(m,at); lay_stack(m,{0x0B1A,sentinel,0});
            });
        }
    }

    // ---- 01:792A and the damage step ----
    void special_ground() {
        const std::vector<std::uint8_t> tiles{0x4C,0x5E,0x5F,0x60,0x63,0x64,0xAD,0xC3,0xC4,0xC6,0xC7,0xC8,0xD1,0xD2,0xAC,0xAE,0xC2,0x5D,0x5C,0x00,0xFF};
        for (unsigned k=0;k<1200;++k) {
            const auto tile=(k%5==0) ? byte() : tiles[k%tiles.size()];
            const bool cartridge=(k%7==0);
            auto setup=next_setup(); if (cartridge) { setup.cartridge_ram=true; setup.banks[2]=3; }
            const auto location=below(3)==0 ? 5 : (below(2) ? static_cast<std::uint8_t>(below(10)) : byte());
            run({1,0x792A},setup,[&](Machine& m) {
                common_ram(m);
                poke(m,ram_tile,tile); poke(m,ram_location,static_cast<std::uint8_t>(location));
                poke(m,ram_flag_506,(k&1) ? 0 : byte()); poke(m,ram_transport,(k%4<2) ? 8 : (k%4==2 ? 0 : byte()));
                for (unsigned i=0;i<24;++i) poke(m,static_cast<std::uint16_t>(ram_inventory+i),(k%3==0) ? byte() : static_cast<std::uint8_t>(below(4)));
                for (unsigned c=0;c<4;++c) poke(m,static_cast<std::uint16_t>(ram_stats+16*c),(k%5<3) ? byte() : static_cast<std::uint8_t>(k%2));
                for (unsigned c=0;c<4;++c) { poke(m,static_cast<std::uint16_t>(ram_stats+16*c+1),byte()); poke(m,static_cast<std::uint16_t>(ram_stats+16*c+2),static_cast<std::uint8_t>(below(4))); }
                lay_stack(m,{0x0B1A,sentinel,0});
            });
        }
        // Every tile id once per combination of location 5 / another, flag set / clear, transport
        // on foot / 8 / another, with the items 35h and 3Bh carried or not.
        for (unsigned tile=0;tile<256;++tile)
            for (unsigned variant=0;variant<12;++variant) {
                const bool here=variant&1, flag=variant&2;
                const std::uint8_t transport=(variant>>2)==0 ? 0 : ((variant>>2)==1 ? 8 : 4);
                run({1,0x792A},next_setup(),[&](Machine& m) {
                    common_ram(m);
                    poke(m,ram_tile,static_cast<std::uint8_t>(tile)); poke(m,ram_location,here ? 5 : static_cast<std::uint8_t>(4+below(8)));
                    poke(m,ram_flag_506,flag ? byte()|1 : 0); poke(m,ram_transport,transport);
                    for (unsigned i=0;i<24;++i) poke(m,static_cast<std::uint16_t>(ram_inventory+i),static_cast<std::uint8_t>(below(4)));
                    if (variant&8) { poke(m,ram_inventory+3,0x35); poke(m,ram_inventory+9,0x3B); }
                    for (unsigned c=0;c<4;++c) { poke(m,static_cast<std::uint16_t>(ram_stats+16*c),static_cast<std::uint8_t>(below(2))); poke(m,static_cast<std::uint16_t>(ram_stats+16*c+1),static_cast<std::uint8_t>(below(40))); }
                    lay_stack(m,{0x0B1A,sentinel,0});
                });
            }
        // Entries after a native callee: the free-slot search (Z = a free slot), the vehicle call,
        // the damage loop and the character check.
        for (unsigned k=0;k<250;++k) {
            const unsigned zero=(k&1) ? 0x40 : 0; const std::uint8_t other=static_cast<std::uint8_t>(byte()&0xBF);
            run({1,0x796B},next_setup(),[&](Machine& m) { common_ram(m); set_f(m,static_cast<std::uint8_t>(other|zero)); lay_stack(m,{0x0B1A,sentinel,0}); });
            run({1,0x79DC},next_setup(),[&](Machine& m) { common_ram(m); set_f(m,static_cast<std::uint8_t>(other|zero)); lay_stack(m,{0x0B1A,sentinel,0}); });
            run({1,0x7997},next_setup(),[&](Machine& m) { common_ram(m); lay_stack(m,{0x0B1A,sentinel,0}); });
            run({1,0x79E0},next_setup(),[&](Machine& m) { common_ram(m); lay_stack(m,{0x0B1A,sentinel,0}); });
            for (const std::uint16_t entry:{0x79E9,0x79EE,0x79F3})
                run({1,entry},next_setup(),[&](Machine& m) { common_ram(m); lay_stack(m,{0x0B1A,sentinel,0}); });
            run({1,0x79F8},next_setup(),[&](Machine& m) { common_ram(m); set_b(m,(k%3==0) ? 0 : byte()); lay_stack(m,{0x0B1A,sentinel,0}); });
        }
        // Joining stretches (milestone 14): the object-table fill's seed store (HL in work RAM) and the call
        // that asks whether a character is alive.
        for (unsigned k=0;k<24;++k) {
            run({1,0x7924},next_setup(),[&](Machine& m) { common_ram(m); set_hl(m,static_cast<std::uint16_t>(0xC800+16*k)); lay_stack(m,{0x0B1A,sentinel,0}); });
            run({1,0x7A0C},next_setup(),[&](Machine& m) { common_ram(m); lay_stack(m,{0x0B1A,sentinel,0}); });
        }
        // 01:7A0F: the hit points at HL + 1 against C, both flags of the character check.
        for (unsigned k=0;k<750;++k) {
            const std::uint8_t c=(k%4==0) ? 0 : byte();
            const std::uint8_t hp=(k%5==0) ? c : (k%5==1 ? static_cast<std::uint8_t>(c+1) : (k%5==2 ? static_cast<std::uint8_t>(c-1) : byte()));
            const std::uint16_t at=static_cast<std::uint16_t>(0xC400+16*(k%4));
            run({1,0x7A0F},next_setup(),[&](Machine& m) {
                common_ram(m);
                set_hl(m,at); set_c(m,c); set_b(m,byte()); set_f(m,(k&1) ? 0x40 : 0x00);
                poke(m,static_cast<std::uint16_t>(at+1),hp); poke(m,at,byte());
                lay_stack(m,{0x0B1A,sentinel,0});
            });
        }
    }

    // ---- State probes from a real map frame: the real tables ----
    // For every record of the area's table in bank 3 the camera is searched (by
    // running the original up to the loop head) so that the probe tile falls in
    // the record, then the part runs from 77C4 on the real map.
    void real_records(const Machine& frame, const probe::Rom& rom, const std::string& name, std::set<unsigned>& kinds_seen) {
        const auto table=word_at(frame,ram_table);
        if (table<0x8000 || table>=0xC000) { std::cout<<"  "<<name<<": no entrance table at this frame (pointer "<<std::hex<<table<<std::dec<<", area "<<unsigned(frame.peek(ram_area))<<", location "<<unsigned(frame.peek(ram_location))<<")"<<std::endl; return; }
        unsigned records=0, hits=0; std::map<unsigned,unsigned> per_kind;
        for (auto at=table;records<64;at=static_cast<std::uint16_t>(at+6),++records) {
            if (rom.byte(3,at)==0xFF) break;
            const std::uint8_t kd=rom.byte(3,at), ke=rom.byte(3,static_cast<std::uint16_t>(at+1)), kind=rom.byte(3,static_cast<std::uint16_t>(at+2));
            if (per_kind[kind]++>=4) continue;      // a few records of each kind are enough
            // Search a camera whose key hits this record: first the Y bytes for D, then the X bytes for E.
            probe::Setup setup; setup.sp=0xDF00; setup.seed=at;
            auto base=map_probe::prepare_at(frame,{1,0x77C4},setup);
            set_a(base,0x08); set_e(base,0x14);
            lay_stack(base,{0x0B1A,sentinel,0});
            bool found_y=false, found_x=false;
            std::uint16_t cam_y=word_at(base,ram_cam_y), cam_x=word_at(base,ram_cam_x);
            for (unsigned page=0;page<8 && !found_y;++page)
                for (unsigned low=0;low<0xC0 && !found_y;++low) {
                    auto probe_machine=base;
                    poke_word(probe_machine,ram_cam_y,static_cast<std::uint16_t>((page<<8)|low));
                    const auto key=scan_key(probe_machine);
                    if (key && high_matches(kd,key->first)) { cam_y=static_cast<std::uint16_t>((page<<8)|low); found_y=true; }
                }
            for (unsigned x=0;x<0x800 && !found_x && found_y;++x) {
                auto probe_machine=base;
                poke_word(probe_machine,ram_cam_y,cam_y); poke_word(probe_machine,ram_cam_x,static_cast<std::uint16_t>(x));
                const auto key=scan_key(probe_machine);
                if (key && high_matches(kd,key->first) && (key->second==ke || static_cast<std::uint8_t>(ke+1)==key->second)) { cam_x=static_cast<std::uint16_t>(x); found_x=true; }
            }
            if (!found_y || !found_x) { std::cout<<"  "<<name<<": record at "<<std::hex<<at<<std::dec<<" (kind "<<unsigned(kind)<<") has no camera that reaches it\n"; continue; }
            for (unsigned variant=0;variant<3;++variant) {
                probe::Setup s; s.sp=0xDF00; s.seed=at+variant; s.iff1=variant==1; s.iff2=variant==1; s.r=static_cast<std::uint8_t>(variant*41);
                auto m=map_probe::prepare_at(frame,{1,0x77C4},s);
                poke_word(m,ram_cam_y,cam_y); poke_word(m,ram_cam_x,cam_x);
                set_a(m,0x08); set_e(m,0x14); lay_stack(m,{0x0B1A,sentinel,0});
                chain({1,0x77C4},m);
                // A record that started its transfer leaves its kind in the mode byte (the first
                // record of a key wins, so a duplicate key may show another kind).
                if (m.peek(ram_mode)==kind) { kinds_seen.insert(kind); ++hits; }
            }
        }
        std::cout<<"  "<<name<<": "<<records<<" real records, "<<hits<<" probes ended in their record's mode"<<std::endl;
    }
private:
    Stats& stats_;
    std::mt19937_64 rng_;
    std::uint64_t seed_, serial_=0;
};

// ---- Probes on a ROM ------------------------------------------------------------------
void report(const Stats& stats, const RoutineRegistry& registry) {
    std::cout<<stats.calls<<" C++ calls verified, "<<stats.fallbacks<<" intended fallbacks, "<<stats.chains<<" chains.\n";
    std::vector<std::string> missing;
    for (const auto& segment:map::entrance_segments) {
        const auto* routine=registry.find(segment.entry);
        const auto calls=stats.per_entry.count(segment.entry.address) ? stats.per_entry.at(segment.entry.address) : 0;
        check(calls>0,"entry "+std::to_string(segment.entry.address)+" was never run in C++ by the probes");
        const auto& reached=stats.exits.count(segment.entry.address) ? stats.exits.at(segment.entry.address) : std::set<std::uint16_t>{};
        for (const auto& exit:routine->spec.exits)
            if (!reached.count(exit.address)) {
                std::ostringstream text;
                text<<std::hex<<std::uppercase<<segment.entry.address<<"->"<<exit.address;
                missing.push_back(text.str());
            }
    }
    // 01:79D3 (`ret c` after `cp C7`) is dead code: 79D1 is reached only with A >= C7.
    std::erase(missing,std::string("792A->79D3"));
    std::string text;
    for (const auto& m:missing) text+=" "+m;
    check(missing.empty(),"declared exits not reached by the probes:"+text);
    std::cout<<"every declared exit was reached by the probes (01:79D3 is dead code: no path gives it A < C7).\n";
}
void probes(const map_probe::Inputs& inputs) {
    Stats stats;
    Lab lab(inputs.rom,stats,inputs.version=="us" ? 11 : 17);
    lab.head();
    lab.scans();
    lab.transfers();
    lab.special_ground();
    report(stats,lab.harness.registry());

    // State probes from real map frames: the real entrance tables of the recordings.
    if (inputs.recordings.empty() || inputs.reference.empty()) {
        std::cout<<"real map frames skipped: no recordings or reference folder given\n";
        return;
    }
    std::set<unsigned> kinds_seen;
    bool skipped=false;
    for (const std::string stem:{"opening","later-town","deep-dungeon","road"}) {
        if (!std::filesystem::exists(inputs.recording(stem))) { skipped=true; continue; }
        const auto recording=replay::read(inputs.recording(stem));
        if (recording.rom_sha1!=inputs.rom.sha1) continue;     // a recording of the other version
        const auto image=map_probe::start_image(inputs,stem,recording);
        if (!image) { std::cout<<stem<<": start image not available, skipped\n"; skipped=true; continue; }
        map_probe::Scenario scenario(inputs.rom,*image,stem);
        std::uint64_t from=0;
        const auto with_table=[](const Machine& m) { const auto t=word_at(m,ram_table); return map_probe::on_map(m) && t>=0x8000 && t<0xC000; };
        if (!scenario.play(recording,from,with_table)) { std::cout<<stem<<": never reaches the map\n"; continue; }
        std::cout<<stem<<": real map at frame "<<from<<'\n';
        Stats real_stats;
        Lab real(inputs.rom,real_stats,from+inputs.rom.sha1.size());
        real.real_records(scenario.machine(),inputs.rom,stem,kinds_seen);
        stats.calls+=real_stats.calls;
    }
    std::cout<<"record kinds probed on real maps:";
    for (const auto k:kinds_seen) std::cout<<' '<<k;
    std::cout<<'\n';
    check((kinds_seen.count(8) && kinds_seen.count(0x0A) && kinds_seen.count(0x0C)) || inputs.version=="br","real maps: every record kind (08h, 0Ah, 0Ch) was probed");
    if (skipped) { std::cout<<"some recordings were skipped\n"; }
}
}

// ---- Interrupt sweeps (milestone 14, recipe step 5) ----------------------------------------------
// An IRQ and an NMI at every cycle of every converted body of this part
// (sampled: about 40 runs per kind), each run compared with the oracle at every
// instruction boundary, handler included. One-instruction segments have no
// boundary inside and are left out. The state is the map's work RAM with
// recognisable bytes, pointers into RAM and the video data port in C.
void sweep_state(RoutineEntry entry, Machine& m) {
    (void)entry;
    auto& cpu=m.cpu();
    for (unsigned a=0xC200;a<0xC700;++a) m.memory().write(static_cast<std::uint16_t>(a),static_cast<std::uint8_t>((a*167u+(a>>8)*13u)>>1));
    cpu.hl.uint16_value=0xC900; cpu.de.uint16_value=0xD000; cpu.bc.uint16_value=0x04BE;
    cpu.ix_iy[0].uint16_value=0xC800; cpu.ix_iy[1].uint16_value=0xC800;
}
void sweeps(const probe::Rom& rom) {
    probe::Harness h(rom);
    std::vector<RoutineEntry> entries;
    for (const auto& s:map::entrance_segments) entries.push_back(s.entry);
    unsigned swept=0;
    std::string failures;     // every failing segment is reported, not only the first
    for (const auto& entry:entries) {
        if (h.routine(entry).spec.single_instruction) continue;
        try {
            probe::Setup setup; setup.iff1=setup.iff2=true; setup.seed=entry.address;
            const auto adjust=[&](Machine& m) { sweep_state(entry,m); };
            auto base=h.prepare(entry,setup);
            adjust(base);
            const auto length=h.run_cpp(base,entry).cycles;
            probe::sweep(h,entry,setup,adjust,std::max<unsigned>(1,static_cast<unsigned>(length/40)));
            ++swept;
        } catch (const std::exception& error) {
            failures+="\n  "+std::to_string(entry.bank)+":"+std::to_string(entry.address)+": "+error.what();
        }
    }
    check(failures.empty(),"map sweeps failed:"+failures);
    std::cout<<"map entrances: "<<swept<<" segments swept\n";
}

int main(int argc, char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        registration();
        if (argc==1) return 0;
        const auto inputs=map_probe::load(argc,argv);
        if (!inputs) return 77;
        sweeps(inputs->rom);
        probes(*inputs);
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
