// SPDX-License-Identifier: GPL-3.0-or-later
// Encounter checks (map 01:5F63 with 01:5FD8, dungeon 00:0F3C). Without
// arguments: registration checks needing no ROM. With <rom> <us|br>: state
// probes of every segment, verified call by call against the original (exit
// 77 without the ROM), and the tests of the encounter-rate option.
#include "probe.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <set>

using namespace psmodern;
using probe::check;
namespace {
// register_encounters adds exactly the declared encounter segments, belonging
// to the three encounter units, for every option value.
void registration() {
    const game::Options variants[]{{},{0,100},{50,100},{200,100},{100,200},{100,400},{0,400}};
    for (const auto& options:variants) {
        RoutineRegistry registry;
        game::register_encounters(registry,options);
        check(registry.entries().size()==game::encounter_entries.size(),"every declared encounter entry is registered");
        for (const auto& [entry,routine]:registry.entries()) {
            check(std::find(game::encounter_entries.begin(),game::encounter_entries.end(),entry)!=game::encounter_entries.end(),
                  "undeclared encounter entry registered");
            const auto unit=routine.spec.unit;
            check(unit==game::map_check_unit || unit==game::group_choice_unit || unit==game::dungeon_check_unit,
                  "encounter segment outside the encounter units");
            const bool compare=entry==game::map_compare_entry || entry==game::dungeon_compare_entry;
            const bool scaled=options.encounter_percent!=100;
            check(routine.spec.single_instruction==compare && routine.spec.force_admit==(compare && scaled),
                  "only the compare segments are single instructions, forced only with the option on");
            check(!routine.spec.exits.empty() && routine.spec.scratch_stack==0,"segments declare exits and no scratch stack");
            const bool mapper=entry==game::map_check_unit || entry==game::map_after_compare_entry || entry==game::group_choice_unit;
            check(routine.spec.slot2_mapper==mapper,"slot-2 mapper permission only where the original writes it");
        }
    }
}

// ---- ROM probes ------------------------------------------------------------

constexpr std::uint16_t caller=0x0180;
constexpr std::array<std::uint8_t,8> modifiers{0,1,4,8,12,16,17,255};

struct Context {
    const probe::Rom& rom;
    probe::Harness& harness;
    std::uint64_t index=0, probes=0;
    // The 16-bit operand of the instruction at bank:address, read from the image.
    std::uint16_t operand(unsigned bank, std::uint16_t address) const {
        return static_cast<std::uint16_t>(rom.byte(bank,address)|(rom.byte(bank,static_cast<std::uint16_t>(address+1))<<8));
    }
    // Addresses of the game variables, as the original instructions name them.
    std::uint16_t selector() const { return operand(1,0x5F69); }
    std::uint16_t position_a() const { return operand(1,0x5F7D); }
    std::uint16_t position_b() const { return operand(1,0x5F92); }
    std::uint16_t modifier() const { return operand(1,0x5FA4); }
    std::uint16_t pool_offset() const { return operand(1,0x5FB9); }
    std::uint16_t pool_chosen() const { return operand(1,0x5FF4); }
    std::uint16_t pending() const { return operand(1,0x5FFB); }
    std::uint16_t paused() const { return operand(0,0x0F3D); }
    std::uint16_t dungeon_pending() const { return operand(0,0x0F4C); }
    std::uint16_t dungeon_threshold() const { return operand(0,0x0F55); }
    std::uint16_t dungeon_pool() const { return operand(0,0x0F68); }
    std::uint16_t dungeon_monsters() const { return operand(0,0x0F76); }
    std::uint16_t random_state() const { return operand(0,0x05B3); }
    std::uint16_t region_table() const { return operand(1,0x5F78); }
    std::uint16_t pool_table() const { return operand(1,0x5FE6); }

    // Entry state variations in the spirit of the harness: slot-2 bank or
    // cartridge RAM, stack in work RAM or its mirror, R, IFF1/IFF2, unrelated
    // registers; all picked from the probe's index.
    probe::Setup vary() {
        const auto mixed=(index++*0x9e3779b97f4a7c15ull)>>20;
        probe::Setup setup;
        setup.seed=index;
        setup.banks[2]=std::array<std::uint8_t,5>{3,3,0,15,2}[mixed%5];
        setup.cartridge_ram=(mixed>>8)%9==0;
        setup.sp=std::array<std::uint16_t,4>{0xdf00,0xdc00,0xe100,0xc0f0}[(mixed>>12)%4];
        setup.r=static_cast<std::uint8_t>(mixed>>16);
        setup.iff1=(mixed>>24)&1; setup.iff2=(mixed>>25)&1;
        return setup;
    }
    // One probe: prepare, arrange, run in C++ against the original (the
    // verifier compares the whole machine) and require the cost the segment
    // announced as its bound to equal the cycles of the path (at most, when
    // work RAM is disabled and the path is not known in advance).
    template<class Arrange, class Inspect>
    void run(RoutineEntry entry, probe::Setup setup, Arrange&& arrange, Inspect&& inspect) {
        auto machine=harness.prepare(entry,setup);
        arrange(machine);
        const auto bound=harness.routine(entry).bound(machine);
        const auto result=harness.run_cpp(machine,entry);
        ++probes;
        check(setup.work_ram_disabled ? result.cycles<=bound : result.cycles==bound,"announced bound differs from the cycles of the path");
        inspect(machine,result);
    }
    template<class Arrange> void run(RoutineEntry entry, probe::Setup setup, Arrange&& arrange) {
        run(entry,setup,arrange,[](Machine&, probe::Harness::Result) {});
    }
};
void poke(Machine& machine, std::uint16_t address, std::uint8_t value) { machine.memory().write(address,value); }
void poke_word(Machine& machine, std::uint16_t address, std::uint16_t value) {
    poke(machine,address,static_cast<std::uint8_t>(value)); poke(machine,static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
}
std::uint8_t peek(const Machine& machine, std::uint16_t address) { return machine.memory().read(address); }
std::uint8_t a_of(const Machine& m) { return m.cpu().af.uint8_values.at_1; }
std::uint8_t f_of(const Machine& m) { return m.cpu().af.uint8_values.at_0; }
std::uint8_t b_of(const Machine& m) { return m.cpu().bc.uint8_values.at_1; }
std::uint16_t hl_of(const Machine& m) { return m.cpu().hl.uint16_value; }
void set_a(Machine& m, unsigned v) { m.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(v); }
void set_f(Machine& m, unsigned v) { m.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(v); }
void set_b(Machine& m, unsigned v) { m.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(v); }
bool returned(const Machine& machine, std::uint16_t sp) {
    return machine.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp+2) && machine.cpu().pc.uint16_value==caller;
}
bool called(const Machine& machine, std::uint16_t sp, std::uint16_t target) {
    return machine.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp-2) && machine.cpu().pc.uint16_value==target;
}
bool skipped(const Machine& machine, std::uint16_t sp, std::uint16_t next) {
    return machine.cpu().sp.uint16_value==sp && machine.cpu().pc.uint16_value==next;
}
using Result=probe::Harness::Result;
struct Tally { std::uint64_t first=0, second=0, third=0; };

// ---- Map check -------------------------------------------------------------

// 01:5F63: every selector; for 0-2 every cell of the region (all 192 records),
// every carry combination of the two position bytes, the vehicle modifier;
// then every pair of position low bytes.
void map_start(Context& c, std::uint64_t& cells, std::uint64_t& failures, std::uint64_t& low_pairs) {
    const auto base=c.region_table();
    std::set<std::pair<unsigned,unsigned>> records;
    for (unsigned selector=3;selector<256;++selector)
        for (unsigned k=0;k<8;++k) {
            auto setup=c.vary();
            c.run(game::map_check_unit,setup,[&](Machine& m) { poke(m,c.selector(),static_cast<std::uint8_t>(selector)); },
                [&](Machine& m, Result r) {
                    check(r.cycles==77 && r.fetches==8,"selectors from 3 on fail in 77 cycles");
                    check(returned(m,setup.sp) && a_of(m)==0 && peek(m,c.pending())==0,"failure ending");
                    ++failures;
                });
        }
    for (unsigned selector=0;selector<3;++selector)
        for (unsigned cell=0;cell<64;++cell)
            for (unsigned carry=0;carry<4;++carry)
                for (const auto modifier:modifiers) {
                    const unsigned row=cell/8, column=cell%8;
                    const bool carry_a=carry&1, carry_b=carry&2;
                    // The low bytes decide the two carries, the high bytes carry
                    // the row and column (less the carry) in their low three bits
                    // and free upper bits.
                    constexpr std::uint8_t a_without[]{0x00,0x3f,0x5f}, a_with[]{0x60,0x9f,0xa0,0xff};
                    constexpr std::uint8_t b_without[]{0x00,0x7f}, b_with[]{0x80,0xff};
                    const auto low_a=carry_a ? a_with[(cell+modifier)%4] : a_without[(cell+modifier)%3];
                    const auto low_b=carry_b ? b_with[cell%2] : b_without[(cell/2)%2];
                    const auto high_a=static_cast<std::uint8_t>(((row+8-carry_a)&7)|(((cell*37u+modifier)&0x1f)<<3));
                    const auto high_b=static_cast<std::uint8_t>(((column+8-carry_b)&7)|(((cell*53u+modifier)&0x1f)<<3));
                    auto setup=c.vary();
                    c.run(game::map_check_unit,setup,[&](Machine& m) {
                        poke(m,c.selector(),static_cast<std::uint8_t>(selector));
                        poke_word(m,c.position_a(),static_cast<std::uint16_t>(low_a|(high_a<<8)));
                        poke_word(m,c.position_b(),static_cast<std::uint16_t>(low_b|(high_b<<8)));
                        poke(m,c.modifier(),modifier);
                    },[&](Machine& m, Result) {
                        const auto record=static_cast<std::uint16_t>(base+selector*128+cell*2);
                        check(called(m,setup.sp,0x05B1) && hl_of(m)==record,"record address");
                        const auto threshold=peek(m,record);
                        check(b_of(m)==(modifier ? threshold>>2 : threshold),"threshold after the vehicle reduction");
                        check(a_of(m)==modifier && m.cpu().bc.uint8_values.at_0==row*8 && m.cpu().de.uint16_value==cell*2,"registers at the draw");
                        records.insert({selector,cell}); ++cells;
                    });
                }
    check(records.size()==192,"all 192 region records are reached");
    for (unsigned selector=0;selector<3;++selector)
        for (unsigned low_a=0;low_a<256;++low_a)
            for (unsigned low_b=0;low_b<256;++low_b) {
                const auto mixed=(low_a*256+low_b)*0x9e3779b97f4a7c15ull>>24;
                c.run(game::map_check_unit,c.vary(),[&](Machine& m) {
                    poke(m,c.selector(),static_cast<std::uint8_t>(selector));
                    poke_word(m,c.position_a(),static_cast<std::uint16_t>(low_a|((mixed&0xff)<<8)));
                    poke_word(m,c.position_b(),static_cast<std::uint16_t>(low_b|(((mixed>>8)&0xff)<<8)));
                    poke(m,c.modifier(),modifiers[(mixed>>16)%8]);
                });
                ++low_pairs;
            }
}

// 01:5FB1: the comparison's carry set (the draw is below the threshold) or
// clear, every pool-offset byte, every region record.
void map_after_compare(Context& c, std::uint64_t& failures, std::uint64_t& no_pool, std::uint64_t& groups) {
    const auto base=c.region_table();
    for (const unsigned selector:{0u,1u,2u,3u,255u})
        for (unsigned cell=0;cell<(selector<3 ? 64u : 4u);++cell)
            for (unsigned offset=0;offset<256;++offset)
                for (unsigned carry=0;carry<2;++carry) {
                    auto setup=c.vary();
                    const auto record=static_cast<std::uint16_t>(base+(selector&3)*128+cell*2);
                    c.run(game::map_after_compare_entry,setup,[&](Machine& m) {
                        poke(m,c.selector(),static_cast<std::uint8_t>(selector));
                        poke(m,c.pool_offset(),static_cast<std::uint8_t>(offset));
                        m.cpu().hl.uint16_value=record;
                        set_f(m,(f_of(m)&0xfe)|carry);
                    },[&](Machine& m, Result r) {
                        if (!carry) { check(r.cycles==37 && returned(m,setup.sp) && a_of(m)==0 && peek(m,c.pending())==0,"failure ending"); ++failures; return; }
                        if (called(m,setup.sp,0x05B1)) { ++groups; return; }
                        check(returned(m,setup.sp) && a_of(m)==0,"no pool returns zero"); ++no_pool;
                    });
                }
    check(no_pool>0 && groups>0,"both the no-pool return and the group choice are reached");
}

// 01:5FD8 and 01:5FEC: every pool id (with every flag byte), every pool id
// with every draw.
void group_choice(Context& c, std::uint64_t& ids, std::uint64_t& draws) {
    const auto pool_base=c.pool_table();
    for (unsigned pool=0;pool<256;++pool)
        for (unsigned flags=0;flags<256;++flags) {
            auto setup=c.vary();
            c.run(game::group_choice_unit,setup,[&](Machine& m) { set_a(m,pool); set_f(m,flags); },
                [&](Machine& m, Result r) {
                    if (!pool) { check(returned(m,setup.sp) && r.cycles==15,"pool id zero returns"); ++ids; return; }
                    check(called(m,setup.sp,0x05B1) && r.cycles==111 && hl_of(m)==static_cast<std::uint16_t>(pool_base+pool*8),"pool address");
                    ++ids;
                });
        }
    for (unsigned pool=0;pool<256;++pool)
        for (unsigned draw=0;draw<256;++draw) {
            auto setup=c.vary();
            const auto address=static_cast<std::uint16_t>(pool_base+pool*8);
            std::uint8_t expected=0;
            c.run(game::group_choice_after_draw_entry,setup,[&](Machine& m) {
                set_a(m,draw); m.cpu().hl.uint16_value=address; poke(m,c.pool_chosen(),0x5a);
                // The member the draw's low three bits select, read as the
                // game sees slot 2.
                expected=peek(m,static_cast<std::uint16_t>(address+(draw&7)));
            },[&](Machine& m, Result r) {
                check(returned(m,setup.sp) && r.cycles==66 && a_of(m)==0xff,"group chosen");
                check(peek(m,c.pool_chosen())==expected,"chosen group");
                ++draws;
            });
        }
}

// The two comparisons with the option at its default: every pair A x B.
void compares(Context& c, std::uint64_t& count) {
    for (const auto entry:{game::map_compare_entry,game::dungeon_compare_entry})
        for (unsigned a=0;a<256;++a)
            for (unsigned b=0;b<256;++b)
                c.run(entry,c.vary(),[&](Machine& m) { set_a(m,a); set_b(m,b); },[&](Machine& m, Result r) {
                    check(r.cycles==4 && r.fetches==1,"the compare is one instruction");
                    check(((f_of(m)&1)!=0)==(a<b) && ((f_of(m)&0x40)!=0)==(a==b) && (f_of(m)&2),"compare flags");
                    ++count;
                });
}

// ---- Dungeon ---------------------------------------------------------------

void dungeon(Context& c, std::uint64_t& count) {
    // 00:0F3C: every value of the pause flag; the call is taken for nonzero.
    for (unsigned paused=0;paused<256;++paused)
        for (unsigned k=0;k<4;++k) {
            auto setup=c.vary();
            c.run(game::dungeon_check_unit,setup,[&](Machine& m) { poke(m,c.paused(),static_cast<std::uint8_t>(paused)); },
                [&](Machine& m, Result r) {
                    check(r.cycles==(paused ? 34u : 27u),"pause check cost");
                    check(paused ? called(m,setup.sp,c.operand(0,0x0F41)) : skipped(m,setup.sp,0x0F43),"pause call");
                    check(a_of(m)==paused,"A holds the flag");
                    ++count;
                });
        }
    // 00:0F43: A is loaded with the wait count, then the wait routine is called.
    for (unsigned a=0;a<256;++a) {
        auto setup=c.vary();
        c.run({0,0x0F43},setup,[&](Machine& m) { set_a(m,a); },[&](Machine& m, Result r) {
            check(r.cycles==24 && called(m,setup.sp,c.operand(0,0x0F46)) && a_of(m)==c.rom.byte(0,0x0F44),"wait call"); ++count;
        });
    }
    // 00:0F4B: every pending byte x every threshold.
    for (unsigned pending=0;pending<256;++pending)
        for (unsigned threshold=0;threshold<256;++threshold) {
            auto setup=c.vary();
            c.run({0,0x0F4B},setup,[&](Machine& m) {
                poke(m,c.dungeon_pending(),static_cast<std::uint8_t>(pending)); poke(m,c.dungeon_threshold(),static_cast<std::uint8_t>(threshold));
            },[&](Machine& m, Result r) {
                if (!pending) { check(r.cycles==28 && returned(m,setup.sp),"nothing pending"); ++count; return; }
                check(r.cycles==73 && called(m,setup.sp,c.operand(0,0x0F59)) && peek(m,c.dungeon_pending())==0 &&
                      a_of(m)==threshold && b_of(m)==threshold,"draw call");
                ++count;
            });
        }
    // 00:0F5C: every flag byte (return on no carry).
    for (unsigned flags=0;flags<256;++flags)
        for (unsigned k=0;k<4;++k) {
            auto setup=c.vary();
            c.run({0,0x0F5C},setup,[&](Machine& m) { set_f(m,flags); set_b(m,k*77); },[&](Machine& m, Result r) {
                if (!(flags&1)) { check(r.cycles==11 && returned(m,setup.sp),"no carry returns"); ++count; return; }
                check(r.cycles==29 && called(m,setup.sp,c.operand(0,0x0F60)) && b_of(m)==c.rom.byte(0,0x0F5E),"cell test call");
                ++count;
            });
        }
    // 00:0F62: every flag byte x every selector value (return on not zero).
    for (unsigned flags=0;flags<256;++flags)
        for (unsigned pool=0;pool<256;++pool) {
            auto setup=c.vary();
            c.run({0,0x0F62},setup,[&](Machine& m) { set_f(m,flags); poke(m,c.dungeon_pool(),static_cast<std::uint8_t>(pool)); },
                [&](Machine& m, Result r) {
                    if (!(flags&0x40)) { check(r.cycles==11 && returned(m,setup.sp),"not zero returns"); ++count; return; }
                    check(r.cycles==52 && called(m,setup.sp,c.operand(0,0x0F6B)) && a_of(m)==pool && peek(m,c.dungeon_pending())==0,"group call");
                    ++count;
                });
        }
    // 00:0F6D: every value of A (return on zero).
    for (unsigned a=0;a<256;++a)
        for (unsigned k=0;k<4;++k) {
            auto setup=c.vary();
            c.run({0,0x0F6D},setup,[&](Machine& m) { set_a(m,a); set_f(m,k*0x55); },[&](Machine& m, Result r) {
                if (!a) { check(r.cycles==15 && returned(m,setup.sp),"no group returns"); ++count; return; }
                check(r.cycles==26 && called(m,setup.sp,c.operand(0,0x0F70)),"setup call"); ++count;
            });
        }
    // 00:0F75: every value of the monster record byte.
    for (unsigned monsters=0;monsters<256;++monsters)
        for (unsigned k=0;k<4;++k) {
            auto setup=c.vary();
            c.run({0,0x0F75},setup,[&](Machine& m) { poke(m,c.dungeon_monsters(),static_cast<std::uint8_t>(monsters)); },
                [&](Machine& m, Result r) {
                    check(r.cycles==(monsters ? 34u : 27u),"monster check cost");
                    check(monsters ? called(m,setup.sp,c.operand(0,0x0F7A)) : skipped(m,setup.sp,0x0F7C),"monster call");
                    ++count;
                });
        }
}

// ---- Variations over every segment ------------------------------------------

struct Family {
    RoutineEntry entry;
    unsigned fetches;
    std::function<void(Context&, Machine&, unsigned)> arrange;
};
std::vector<Family> families() {
    using M=Machine;
    return {
        {game::map_check_unit,40,[](Context& c, M& m, unsigned k) {
            poke(m,c.selector(),k==2 ? 9 : k); poke_word(m,c.position_a(),static_cast<std::uint16_t>(0x60+k*0x21+(k<<8)));
            poke_word(m,c.position_b(),static_cast<std::uint16_t>(k*0x80+((k+1)<<8))); poke(m,c.modifier(),k); }},
        {game::map_compare_entry,2,[](Context&, M& m, unsigned k) { set_a(m,k*100); set_b(m,k*60+30); }},
        {game::map_after_compare_entry,34,[](Context& c, M& m, unsigned k) {
            poke(m,c.selector(),k); poke(m,c.pool_offset(),static_cast<std::uint8_t>(17*k+3));
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(c.region_table()+k*40); set_f(m,k&1 ? 0x01 : 0x44); }},
        {game::group_choice_unit,16,[](Context&, M& m, unsigned k) { set_a(m,k*3); }},
        {game::group_choice_after_draw_entry,10,[](Context& c, M& m, unsigned k) {
            set_a(m,k*77+1); m.cpu().hl.uint16_value=static_cast<std::uint16_t>(c.pool_table()+8*(k+4)); }},
        {game::dungeon_check_unit,5,[](Context& c, M& m, unsigned k) { poke(m,c.paused(),static_cast<std::uint8_t>(k)); }},
        {{0,0x0F43},3,[](Context&, M& m, unsigned k) { set_a(m,k); }},
        {{0,0x0F4B},8,[](Context& c, M& m, unsigned k) { poke(m,c.dungeon_pending(),static_cast<std::uint8_t>(k)); poke(m,c.dungeon_threshold(),200); }},
        {game::dungeon_compare_entry,2,[](Context&, M& m, unsigned k) { set_a(m,k*100); set_b(m,k*60+30); }},
        {{0,0x0F5C},4,[](Context&, M& m, unsigned k) { set_f(m,k&1 ? 0x01 : 0x00); }},
        {{0,0x0F62},8,[](Context& c, M& m, unsigned k) { set_f(m,k&1 ? 0x40 : 0x00); poke(m,c.dungeon_pool(),static_cast<std::uint8_t>(k+5)); }},
        {{0,0x0F6D},4,[](Context&, M& m, unsigned k) { set_a(m,k); }},
        {{0,0x0F75},5,[](Context& c, M& m, unsigned k) { poke(m,c.dungeon_monsters(),static_cast<std::uint8_t>(k)); }},
    };
}
// Slot 2 on other banks and on cartridge RAM, R around its wrap, stack
// placements in RAM, ROM and on the mapper registers, work RAM disabled.
void variations(Context& c, std::uint64_t& count) {
    for (const auto& family:families()) {
        const auto runs_before=c.probes;
        for (unsigned k=0;k<3;++k) {
            auto arrange=[&](Machine& m) { family.arrange(c,m,k); };
            for (const auto& setup:probe::slot2_variants({})) c.run(family.entry,setup,arrange);
            for (const auto r:probe::r_wrap_values(family.fetches)) {
                probe::Setup setup; setup.r=r; setup.seed=r+k; setup.iff1=setup.iff2=r&1;
                c.run(family.entry,setup,arrange);
            }
            for (const auto& stack:probe::stack_cases()) {
                probe::Setup setup; setup.sp=stack.sp; setup.cartridge_ram=stack.cartridge_ram;
                setup.work_ram_disabled=stack.work_ram_disabled; setup.seed=stack.sp+k;
                if (stack.admitted(0)) c.run(family.entry,setup,arrange);
                else {
                    auto machine=c.harness.prepare(family.entry,setup);
                    arrange(machine);
                    c.harness.run_fallback(machine,family.entry,RoutineFallback::unmapped_stack);
                }
            }
        }
        check(c.probes>runs_before,"variations ran");
    }
    count=c.probes;
}

// A stack on the mapper write: the first segment's slot-2 write also changes
// the stack's last byte (the RAM mirror), so its return goes elsewhere; the
// original's eight instructions are compared instead of the verifier's stop.
void own_return(Context& c) {
    auto setup=c.vary(); setup.sp=0xdffe; setup.banks[2]=3;
    auto machine=c.harness.prepare(game::map_check_unit,setup);
    poke(machine,c.selector(),7);
    c.harness.run_cpp_against_steps(machine,game::map_check_unit,8);
    check(machine.cpu().pc.uint16_value==static_cast<std::uint16_t>(c.rom.byte(1,0x5F67)<<8|(caller&0xff)),
          "return address with the bank number in its high byte");
}

// The harness catches a wrong body: the group choice with R one fetch ahead.
void sabotage(Context& c) {
    const auto& real=c.harness.routine(game::group_choice_unit);
    RoutineRegistry sabotaged;
    replay::CallVerifier verifier("sabotage");
    sabotaged.verifier=&verifier;
    sabotaged.add(game::group_choice_unit,real.spec,real.bound,[body=real.body](RoutineContext& context) {
        auto& r=context.cpu().r; r=static_cast<std::uint8_t>((r&0x80)|((r+1)&0x7f)); body(context);
    });
    auto machine=c.harness.prepare(game::group_choice_unit,{});
    set_a(machine,5);
    bool caught=false;
    try { machine.step(sabotaged); } catch (const std::runtime_error& e) { caught=std::string(e.what()).find("Divergence")==0; }
    check(caught,"the verifier did not catch a sabotaged body");
}

// ---- Whole checks, segment after segment -------------------------------------

std::uint64_t fallbacks(const RoutineRegistry& registry) {
    std::uint64_t total=0;
    for (const auto& [entry,routine]:registry.entries()) total+=routine.counters.fallback_calls;
    return total;
}
// Runs one entry until it returns to its caller, on the registry (every
// segment, the random-number routine included, in C++ and verified) and on a
// native copy, and requires the two machines to be identical.
template<class Arrange>
Machine chain(Context& c, probe::Harness& harness, RoutineEntry entry, const probe::Setup& setup, Arrange&& arrange) {
    auto machine=harness.prepare(entry,setup);
    arrange(machine);
    auto native=machine;
    const auto sp=machine.cpu().sp.uint16_value;
    auto done=[&](const Machine& m) { return m.cpu().pc.uint16_value==caller && m.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp+2); };
    const auto fallbacks_before=fallbacks(harness.registry());
    unsigned steps=0;
    while (!done(machine)) { machine.step(harness.registry()); check(++steps<2000,"chain did not return"); }
    steps=0;
    while (!done(native)) { native.step(); check(++steps<20000,"native chain did not return"); }
    std::string fields;
    for (const auto& difference:native.differences(machine)) fields+=" "+difference.field;
    check(fields.empty(),"chain differs from the original:"+fields);
    check(fallbacks(harness.registry())==fallbacks_before,"a segment fell back inside a chain");
    ++c.probes;
    return machine;
}
std::uint16_t next_state(std::uint16_t state, std::uint16_t recovery) {
    const unsigned feedback=((state>>15)^(state>>13)^(state>>4)^state)&1;
    const auto shifted=static_cast<std::uint16_t>((state<<1)|feedback);
    return shifted ? shifted : recovery;
}

void map_chains(Context& c, std::uint64_t& count) {
    for (unsigned selector=0;selector<3;++selector)
        for (unsigned cell=0;cell<64;++cell)
            for (const unsigned modifier:{0u,1u})
                for (const unsigned state:{0x0001u,0xace1u,0x8000u,0x1234u}) {
                    const auto setup=c.vary();
                    const auto machine=chain(c,c.harness,game::map_check_unit,setup,[&](Machine& m) {
                        poke(m,c.selector(),static_cast<std::uint8_t>(selector));
                        poke_word(m,c.position_a(),static_cast<std::uint16_t>(((cell/8)<<8)|0x00));
                        poke_word(m,c.position_b(),static_cast<std::uint16_t>(((cell%8)<<8)|0x00));
                        poke(m,c.modifier(),static_cast<std::uint8_t>(modifier));
                        poke_word(m,c.random_state(),static_cast<std::uint16_t>(state+cell*7));
                        poke(m,c.pool_chosen(),0x5a);
                    });
                    check(a_of(machine)==0xff || a_of(machine)==0 || peek(machine,c.pool_chosen())!=0x5a,"map check outcome");
                    ++count;
                }
}
void dungeon_chains(Context& c, std::uint64_t& count) {
    // Failing draws only (a passing draw continues into code outside this unit).
    for (unsigned threshold=0;threshold<256;threshold+=5)
        for (const unsigned state:{0x0001u,0xace1u,0x8000u,0x1234u,0xffffu}) {
            const auto setup=c.vary();
            chain(c,c.harness,{0,0x0F4B},setup,[&](Machine& m) {
                poke(m,c.dungeon_pending(),1); poke(m,c.dungeon_threshold(),static_cast<std::uint8_t>(threshold));
                poke_word(m,c.random_state(),static_cast<std::uint16_t>(state+threshold));
            });
            ++count;
        }
}

// ---- Encounter-rate option ---------------------------------------------------

constexpr unsigned rates[]{0,50,100,200}; // the allowed values (maintainer decision, 2026-11-11)
std::uint8_t scaled(unsigned b, unsigned percent) { return static_cast<std::uint8_t>(std::min(255u,b*percent/100)); }

// At every rate the compare's result equals the original instruction run on
// the scaled value, the register B is restored, nothing but the flags differs
// from the original instruction on the unscaled one, and the call is a forced
// (or, at 100, an ordinary) C++ call.
void rate_formula(const probe::Rom& rom, unsigned rate, std::uint64_t& count) {
    probe::Harness harness(rom,game::Options{rate,100});
    harness.registry().verifier=nullptr; // the verifier compares with the unscaled instruction
    for (const auto entry:{game::map_compare_entry,game::dungeon_compare_entry}) {
        const auto& spec=harness.routine(entry).spec;
        check(spec.force_admit==(rate!=100) && spec.single_instruction,"option declaration");
        probe::Setup setup; setup.seed=rate; setup.iff1=true;
        const auto base=harness.prepare(entry,setup);
        for (unsigned a=0;a<256;++a)
            for (unsigned b=0;b<256;++b) {
                Machine original=base, expected=base, actual=base;
                for (auto* m:{&original,&expected,&actual}) { set_a(*m,a); set_b(*m,b); }
                original.step();
                set_b(expected,scaled(b,rate)); expected.step(); set_b(expected,b);
                const auto before=harness.routine(entry).counters;
                actual.step(harness.registry());
                const auto& after=harness.routine(entry).counters;
                check(after.cpp_calls==before.cpp_calls+1 && after.fallback_calls==before.fallback_calls &&
                      after.forced_calls==before.forced_calls+(rate!=100) && after.forced_failed==0,"call was not admitted in C++");
                check(expected.equivalent(actual),"result differs from the original instruction on the scaled value");
                check(b_of(actual)==b,"B was not restored");
                const auto expected_carry=a<scaled(b,rate);
                check(((f_of(actual)&1)!=0)==expected_carry && ((f_of(actual)&0x40)!=0)==(a==scaled(b,rate)),"formula");
                // Nothing but the flags (F and the flag latch Q) differs from the
                // original instruction on the unscaled value.
                auto flags_aside=actual;
                flags_aside.cpu().af.uint8_values.at_0=original.cpu().af.uint8_values.at_0; flags_aside.cpu().q=original.cpu().q;
                check(original.equivalent(flags_aside),"something besides the flags differs from the original instruction");
                if (rate==100) check(original.equivalent(actual),"rate 100 is the original instruction");
                ++count;
            }
    }
}

// Runs an entry to its return on the registry (forced compare, no verifier:
// the verifier compares with the unscaled instruction) and counts the
// segments that ran in C++.
Machine run_to_return(probe::Harness& harness, Machine machine) {
    const auto sp=machine.cpu().sp.uint16_value;
    const auto cpp_before=[&] { std::uint64_t n=0; for (const auto& [e,r]:harness.registry().entries()) n+=r.counters.cpp_calls; return n; };
    const auto fallbacks_before=fallbacks(harness.registry());
    const auto cpp_start=cpp_before();
    unsigned steps=0;
    while (!(machine.cpu().pc.uint16_value==caller && machine.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp+2))) {
        machine.step(harness.registry()); check(++steps<2000,"check did not return");
    }
    check(fallbacks(harness.registry())==fallbacks_before && cpp_before()>cpp_start,"a segment fell back");
    return machine;
}
// Rate 0: the draw is consumed exactly once per check and the check fails.
void rate_zero(const probe::Rom& rom, std::uint64_t& count) {
    probe::Harness harness(rom,game::Options{0,100});
    harness.registry().verifier=nullptr;
    Context c{rom,harness};
    const auto recovery=static_cast<std::uint16_t>(rom.byte(0,0x05C6)|(rom.byte(0,0x05C7)<<8));
    auto state_of=[&](const Machine& m) { return static_cast<std::uint16_t>(peek(m,c.random_state())|(peek(m,static_cast<std::uint16_t>(c.random_state()+1))<<8)); };
    for (unsigned selector=0;selector<3;++selector)
        for (unsigned cell=0;cell<64;++cell)
            for (const unsigned state:{0x0001u,0xace1u,0x8000u,0x0000u,0xffffu}) {
                auto machine=harness.prepare(game::map_check_unit,c.vary());
                poke(machine,c.selector(),static_cast<std::uint8_t>(selector));
                poke_word(machine,c.position_a(),static_cast<std::uint16_t>((cell/8)<<8)); poke_word(machine,c.position_b(),static_cast<std::uint16_t>((cell%8)<<8));
                poke_word(machine,c.random_state(),static_cast<std::uint16_t>(state)); poke(machine,c.pool_chosen(),0x5a);
                const auto done=run_to_return(harness,machine);
                check(state_of(done)==next_state(static_cast<std::uint16_t>(state),recovery),"the random state advances once per map check");
                check(a_of(done)==0 && peek(done,c.pending())==0 && peek(done,c.pool_chosen())==0x5a,"a map check at rate 0 fails");
                ++count;
            }
    for (unsigned threshold=0;threshold<256;threshold+=3)
        for (const unsigned state:{0x0001u,0xace1u,0x8000u,0x0000u,0xffffu}) {
            auto machine=harness.prepare({0,0x0F4B},c.vary());
            poke(machine,c.dungeon_pending(),1); poke(machine,c.dungeon_threshold(),static_cast<std::uint8_t>(threshold));
            poke_word(machine,c.random_state(),static_cast<std::uint16_t>(state)); poke(machine,c.dungeon_pool(),0x5a);
            const auto done=run_to_return(harness,machine);
            check(state_of(done)==next_state(static_cast<std::uint16_t>(state),recovery),"the random state advances once per dungeon check");
            check(peek(done,c.dungeon_pending())==0 && peek(done,c.dungeon_pool())==0x5a,"a dungeon check at rate 0 stops at the compare");
            ++count;
        }
}

// With a non-default rate the compare segments are admitted in every
// interrupt, NMI and frame window, with no added delay: the machine equals the
// original instruction on the scaled value, also a few steps later.
struct WindowCounts { std::uint64_t forced=0, irq=0, nmi=0, frame=0, ei=0, fallbacks=0, accepted=0; };
// Puts a window of the given kind `offset` cycles after the machine's entry:
// 1 a frame interrupt due at the VDP, 2 an NMI, 3 a frame boundary.
void make_window(Machine& machine, unsigned kind, unsigned offset) {
    if (kind==1) {
        machine.write_port(0xbf,0x20,machine.cycles()); machine.write_port(0xbf,0x81,machine.cycles());
        const auto due=machine.next_irq();
        check(due.has_value(),"a frame interrupt is due");
        if (*due>machine.cycles()+offset) machine.advance(*due-machine.cycles()-offset);
    } else if (kind==2) machine.schedule_nmi(machine.cycles()+offset);
    else if (kind==3) machine.advance(Machine::frame_cycles-machine.cycles()%Machine::frame_cycles-1-offset);
}
void windows(const probe::Rom& rom, WindowCounts& counts) {
    for (const unsigned rate:{0u,50u,200u}) {
        probe::Harness harness(rom,game::Options{rate,100});
        harness.registry().verifier=nullptr;
        for (const auto entry:{game::map_compare_entry,game::dungeon_compare_entry}) {
            for (unsigned kind=0;kind<4;++kind)
                for (unsigned offset=0;offset<7;++offset) {
                    probe::Setup setup; setup.seed=rate*8+offset; setup.iff1=setup.iff2=kind!=3; setup.sp=0xdf00;
                    auto base=harness.prepare(entry,setup);
                    make_window(base,kind,offset);
                    // An interrupt accepted at the entry itself starts no call.
                    if (base.interrupt_accepted_at_boundary()) { ++counts.accepted; continue; }
                    set_a(base,offset*37); set_b(base,100+offset*20);
                    Machine expected=base, actual=base;
                    set_b(expected,scaled(b_of(base),rate)); expected.step(); set_b(expected,b_of(base));
                    const auto before=harness.routine(entry).counters;
                    actual.step(harness.registry());
                    const auto& after=harness.routine(entry).counters;
                    check(after.cpp_calls==before.cpp_calls+1 && after.forced_calls==before.forced_calls+1 &&
                          after.fallback_calls==before.fallback_calls && after.forced_failed==before.forced_failed,
                          "forced call in a window: rate "+std::to_string(rate)+", kind "+std::to_string(kind)+", offset "+std::to_string(offset));
                    ++counts.forced;
                    counts.irq+=after.forced(RoutineFallback::irq)-before.forced(RoutineFallback::irq);
                    counts.nmi+=after.forced(RoutineFallback::nmi)-before.forced(RoutineFallback::nmi);
                    counts.frame+=after.forced(RoutineFallback::frame)-before.forced(RoutineFallback::frame);
                    check(expected.equivalent(actual),"forced call differs from the original instruction");
                    for (unsigned step=0;step<6;++step) {
                        expected.step(); actual.step();
                        check(expected.equivalent(actual),"forced call differs a few steps later");
                    }
                }
            // EI delay: forced, counted, equal.
            probe::Setup setup; setup.iff1=setup.iff2=true;
            auto base=harness.prepare(entry,setup);
            base.cpu().data.uint8_array[0]=0xfb;
            set_a(base,70); set_b(base,90);
            Machine expected=base, actual=base;
            set_b(expected,scaled(90,rate)); expected.step(); set_b(expected,90);
            const auto before=harness.routine(entry).counters;
            actual.step(harness.registry());
            const auto& after=harness.routine(entry).counters;
            check(after.forced_calls==before.forced_calls+1 && after.forced(RoutineFallback::ei_delay)==before.forced(RoutineFallback::ei_delay)+1 &&
                  after.forced_failed==before.forced_failed && after.fallback_calls==before.fallback_calls,"forced call after an EI");
            check(expected.equivalent(actual),"forced call after an EI differs");
            ++counts.ei;
        }
    }
    // The same windows with the option at its default: the call falls back.
    probe::Harness original(rom);
    for (const auto entry:{game::map_compare_entry,game::dungeon_compare_entry})
        for (unsigned kind=1;kind<4;++kind) {
            probe::Setup setup; setup.iff1=setup.iff2=kind!=3; setup.sp=0xdf00;
            auto machine=original.prepare(entry,setup);
            make_window(machine,kind,kind==3 ? 0 : 2);
            check(!machine.interrupt_accepted_at_boundary(),"control window starts a call");
            original.run_fallback(machine,entry,kind==1 ? RoutineFallback::irq : kind==2 ? RoutineFallback::nmi : RoutineFallback::frame);
            ++counts.fallbacks;
        }
}
}

int main(int argc, char** argv) { try {
    registration();
    if (argc==1) { std::cout<<"Encounter registration checks passed.\n"; return 0; }
    if (argc!=3) return 1;
    const auto rom=probe::load(argv[1],argv[2]);
    if (!rom) { std::cout<<"ROM path missing; skipping.\n"; return 77; }
    const auto start=std::chrono::steady_clock::now();
    auto seconds=[&] { return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(); };
    probe::Harness harness(*rom);
    for (const auto& entry:game::encounter_entries) check(harness.registry().find(entry)!=nullptr,"encounter entry registered for the ROM");
    Context c{*rom,harness};
    std::uint64_t cells=0, failures=0, low_pairs=0, after_failures=0, no_pool=0, groups=0, ids=0, draws=0, compared=0, dungeon_probes=0,
        variation_probes=0, map_chain_count=0, dungeon_chain_count=0;
    sabotage(c);
    map_start(c,cells,failures,low_pairs);
    map_after_compare(c,after_failures,no_pool,groups);
    group_choice(c,ids,draws);
    compares(c,compared);
    dungeon(c,dungeon_probes);
    const auto before_variations=c.probes;
    variations(c,variation_probes);
    variation_probes-=before_variations;
    own_return(c);
    map_chains(c,map_chain_count);
    dungeon_chains(c,dungeon_chain_count);
    std::cout<<"Segment probes (C++, verifier clean, announced bound equal to the cost): "
        <<"01:5F63 "<<cells<<" cells (all 192 records) + "<<failures<<" selectors >= 3 + "<<low_pairs<<" position pairs; "
        <<"01:5FB1 "<<after_failures<<" failures, "<<no_pool<<" no-pool, "<<groups<<" group calls; "
        <<"01:5FD8 and 01:5FEC "<<ids<<" pool ids, "<<draws<<" pool x draw; compares "<<compared<<"; dungeon segments "<<dungeon_probes
        <<"; variations (slot 2, R wrap, stacks) "<<variation_probes<<"; whole checks "<<map_chain_count<<" map, "<<dungeon_chain_count
        <<" dungeon; "<<seconds()<<" s.\n";
    std::uint64_t formula=0, zero=0;
    for (const auto rate:rates) rate_formula(*rom,rate,formula);
    rate_zero(*rom,zero);
    WindowCounts window;
    windows(*rom,window);
    std::cout<<"Encounter-rate option: "<<formula<<" compare results over "<<std::size(rates)<<" rates x 2 compares x 65536 inputs; "
        <<zero<<" rate-0 checks; "<<window.forced<<" forced windows ("<<window.irq<<" irq, "<<window.nmi<<" nmi, "<<window.frame
        <<" frame reasons), "<<window.ei<<" after EI, "<<window.fallbacks<<" default-rate windows falling back, "<<window.accepted<<" cases with the interrupt accepted at the entry; "<<seconds()<<" s total.\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
