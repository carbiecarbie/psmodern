// SPDX-License-Identifier: GPL-3.0-or-later
// Experience award (00:170D with its per-character helper 00:1754). Without
// arguments: registration checks needing no ROM. With <rom> <us|br>: state
// probes of every segment, verified call by call against the original (exit
// 77 without the ROM), the tests of the experience-rate option and of its cap
// rule, and the check whether the original can reach the experience cap.
#include "probe.h"
#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <set>

using namespace psmodern;
using probe::check;
namespace {
constexpr RoutineEntry award{0,0x170D}, message{0,0x1710}, first_record{0,0x171C}, second_record{0,0x172A},
    third_record{0,0x1739}, fourth_record{0,0x1748}, helper{0,0x1754}, add_entry{0,0x1773}, total_entry{0,0x1774},
    level_entry{0,0x179D};

// register_experience adds exactly the declared experience segments, belonging
// to the two experience units, for every option value.
void registration() {
    const game::Options variants[]{{},{0,100},{200,100},{100,200},{100,400},{0,400}};
    for (const auto& options:variants) {
        RoutineRegistry registry;
        game::register_experience(registry,options);
        check(registry.entries().size()==game::experience_entries.size(),"every declared experience entry is registered");
        for (const auto& [entry,routine]:registry.entries()) {
            check(std::find(game::experience_entries.begin(),game::experience_entries.end(),entry)!=game::experience_entries.end(),
                  "undeclared experience entry registered");
            const auto unit=routine.spec.unit;
            const bool award_side=entry==award || entry==message || entry==first_record || entry==second_record ||
                entry==third_record || entry==fourth_record;
            check(unit==(award_side ? game::experience_award_unit : game::experience_helper_unit),
                  "experience segment outside its unit");
            const bool option=entry==award || entry==add_entry;
            const bool scaled=options.experience_percent!=100;
            check(routine.spec.single_instruction==option && routine.spec.force_admit==(option && scaled),
                  "only the two reward instructions are single instructions, forced only with the option on");
            check(!routine.spec.exits.empty(),"segments declare exits");
            const bool mapper=entry==fourth_record || entry==helper || entry==level_entry;
            check(routine.spec.slot2_mapper==mapper,"slot-2 mapper permission only where the original writes it");
            check(routine.spec.scratch_stack==((entry==fourth_record || entry==helper) ? 2u : 0u),
                  "scratch stack only where the original pushes and pops");
        }
    }
}

// ---- ROM probes ------------------------------------------------------------

constexpr std::uint16_t caller=0x0180;
using Result=probe::Harness::Result;

struct Context {
    const probe::Rom& rom;
    probe::Harness& harness;
    std::uint64_t index=0, probes=0;
    // The 16-bit operand of the instruction at bank:address, read from the image.
    std::uint16_t operand(unsigned bank, std::uint16_t address) const {
        return static_cast<std::uint16_t>(rom.byte(bank,address)|(rom.byte(bank,static_cast<std::uint16_t>(address+1))<<8));
    }
    // Game variables, records, level tables and field displacements, as the
    // original instructions of the award name them.
    std::uint16_t reward() const { return operand(0,0x170E); }
    std::uint16_t displayed() const { return operand(0,0x1711); }
    std::uint16_t character() const { return operand(0,0x1725); }
    std::uint16_t record(unsigned k) const { return operand(0,std::array<std::uint16_t,4>{0x171E,0x172C,0x173B,0x174A}[k]); }
    std::uint16_t table(unsigned k) const { return operand(0,std::array<std::uint16_t,4>{0x1721,0x172F,0x173E,0x174D}[k]); }
    std::uint16_t sound() const { return operand(0,0x1795); }
    std::uint8_t sound_value() const { return rom.byte(0,0x1793); }
    std::uint8_t status_field() const { return rom.byte(0,0x1756); }
    std::uint8_t level_field() const { return rom.byte(0,0x1760); }
    std::uint8_t exp_field() const { return rom.byte(0,0x176C); }
    std::uint8_t battle_magic_field() const { return rom.byte(0,0x17AA); }
    std::uint8_t map_magic_field() const { return rom.byte(0,0x17B2); }
    std::uint8_t last_level() const { return rom.byte(0,0x1784); }
    std::uint8_t threshold_low_offset() const { return rom.byte(0,0x1790); }
    std::uint8_t threshold_high_offset() const { return rom.byte(0,0x1789); }
    std::uint8_t table_magic_offset() const { return rom.byte(0,0x17A7); }
    std::uint8_t table_map_magic_offset() const { return rom.byte(0,0x17AF); }
    std::uint16_t message_pointer() const { return operand(0,0x1717); }
    std::uint16_t text_entry() const { return operand(0,0x171A); }
    std::uint8_t level_bank() const { return rom.byte(0,0x175D); }
    std::uint16_t level_message() const { return operand(0,0x1798); }
    std::uint16_t level_text() const { return operand(0,0x179B); }
    std::uint16_t second_message() const { return operand(0,0x17B5); }
    std::uint16_t second_text() const { return operand(0,0x17B8); }

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
    // A work-RAM address that is not one of the four records, well clear of the
    // stack placements and of the mapper registers.
    std::uint16_t other_record() { return static_cast<std::uint16_t>(0xc500+((index*0x35)%0x1500)); }
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
        run(entry,setup,arrange,[](Machine&, Result) {});
    }
};
void poke(Machine& machine, std::uint16_t address, std::uint8_t value) { machine.memory().write(address,value); }
void poke_word(Machine& machine, std::uint16_t address, std::uint16_t value) {
    poke(machine,address,static_cast<std::uint8_t>(value)); poke(machine,static_cast<std::uint16_t>(address+1),static_cast<std::uint8_t>(value>>8));
}
std::uint8_t peek(const Machine& machine, std::uint16_t address) { return machine.memory().read(address); }
std::uint16_t peek_word(const Machine& machine, std::uint16_t address) {
    return static_cast<std::uint16_t>(peek(machine,address)|(peek(machine,static_cast<std::uint16_t>(address+1))<<8));
}
std::uint8_t a_of(const Machine& m) { return m.cpu().af.uint8_values.at_1; }
std::uint8_t f_of(const Machine& m) { return m.cpu().af.uint8_values.at_0; }
std::uint16_t hl_of(const Machine& m) { return m.cpu().hl.uint16_value; }
std::uint16_t de_of(const Machine& m) { return m.cpu().de.uint16_value; }
std::uint16_t ix_of(const Machine& m) { return m.cpu().ix_iy[0].uint16_value; }
std::uint16_t iy_of(const Machine& m) { return m.cpu().ix_iy[1].uint16_value; }
void set_a(Machine& m, unsigned v) { m.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(v); }
void set_f(Machine& m, unsigned v) { m.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(v); }
void set_hl(Machine& m, unsigned v) { m.cpu().hl.uint16_value=static_cast<std::uint16_t>(v); }
void set_de(Machine& m, unsigned v) { m.cpu().de.uint16_value=static_cast<std::uint16_t>(v); }
void set_ix(Machine& m, unsigned v) { m.cpu().ix_iy[0].uint16_value=static_cast<std::uint16_t>(v); }
void set_iy(Machine& m, unsigned v) { m.cpu().ix_iy[1].uint16_value=static_cast<std::uint16_t>(v); }
bool returned(const Machine& machine, std::uint16_t sp) {
    return machine.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp+2) && machine.cpu().pc.uint16_value==caller;
}
bool called(const Machine& machine, std::uint16_t sp, std::uint16_t target) {
    return machine.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp-2) && machine.cpu().pc.uint16_value==target;
}
bool jumped(const Machine& machine, std::uint16_t sp, std::uint16_t target) {
    return machine.cpu().sp.uint16_value==sp && machine.cpu().pc.uint16_value==target;
}
// The row of a character's level table for a level, as the helper locates it.
std::uint16_t row(Context& c, unsigned k, unsigned level) { return static_cast<std::uint16_t>(c.table(k)+level*8); }

struct Tally {
    std::uint64_t award_load=0, message=0, records=0, helper_ineligible=0, helper_eligible=0, helper_fourth=0, adds=0,
        totals=0, levels=0, variations=0, chains=0;
};

// ---- The award ---------------------------------------------------------------

// 00:170D: every reward value. The core loads it; nothing else changes.
void award_load(Context& c, Tally& t) {
    for (unsigned reward=0;reward<65536;++reward)
        c.run(award,c.vary(),[&](Machine& m) { poke_word(m,c.reward(),static_cast<std::uint16_t>(reward)); },
            [&](Machine& m, Result r) {
                check(r.cycles==16 && r.fetches==1,"the reward load is one instruction");
                check(hl_of(m)==reward && m.cpu().pc.uint16_value==0x1710 && peek_word(m,c.reward())==reward,"reward loaded");
                ++t.award_load;
            });
}
// 00:1710: every value of the displayed reward (the register): it is stored; a
// zero reward returns; any other shows the message. The flags come from the
// logical or of the two bytes.
void award_message(Context& c, Tally& t) {
    for (unsigned reward=0;reward<65536;++reward) {
        auto setup=c.vary();
        c.run(message,setup,[&](Machine& m) { set_hl(m,reward); },[&](Machine& m, Result r) {
            check(peek_word(m,c.displayed())==reward,"displayed reward stored");
            if (!reward) { check(r.cycles==35 && returned(m,setup.sp),"zero reward returns"); ++t.message; return; }
            check(r.cycles==56 && called(m,setup.sp,c.text_entry()) && hl_of(m)==c.message_pointer(),"message call");
            check(a_of(m)==static_cast<std::uint8_t>(reward|(reward>>8)),"accumulator holds the or of the bytes");
            ++t.message;
        });
    }
}
// 00:171C, 00:172A, 00:1739: the record, the level table and the character
// number of the first three characters, then the call of the helper.
void records(Context& c, Tally& t) {
    const std::array<RoutineEntry,3> entries{first_record,second_record,third_record};
    for (unsigned k=0;k<3;++k)
        for (unsigned flags=0;flags<256;++flags)
            for (unsigned variant=0;variant<4;++variant) {
                auto setup=c.vary();
                c.run(entries[k],setup,[&](Machine& m) { set_f(m,flags); set_a(m,flags*3+variant); poke(m,c.character(),0x5a); },
                    [&](Machine& m, Result r) {
                        check(r.cycles==(k ? 61u : 58u),"record setup cost");
                        check(called(m,setup.sp,0x1754) && iy_of(m)==c.record(k) && de_of(m)==c.table(k),"record and table");
                        check(peek(m,c.character())==k && a_of(m)==k,"character number");
                        ++t.records;
                    });
            }
}

// ---- The helper --------------------------------------------------------------

// 00:1754 and, for the fourth character, 00:1748 which falls into it: every
// status byte and every level. Not eligible returns. Otherwise slot 2 holds the
// level-table bank, IX points at the level's row, DE holds the character's
// experience and the reward load has been executed.
void helper_probes(Context& c, Tally& t) {
    for (unsigned status=0;status<256;++status)
        for (unsigned level=0;level<256;++level) {
            const unsigned k=(status+level)%4;
            for (const bool fourth:{false,true}) {
                if (fourth && ((status+level)%3)) continue;   // the inline copy: a third of the space
                auto setup=c.vary();
                const bool own_iy=!fourth && (level%5==0);
                const auto record=fourth ? c.record(3) : own_iy ? c.other_record() : c.record(k);
                const auto table_base=fourth ? c.table(3) : (level%7==0) ? static_cast<std::uint16_t>(0x9000+level*13) : c.table(k);
                const std::uint16_t exp=static_cast<std::uint16_t>(status*257+level*31);
                const std::uint16_t reward=static_cast<std::uint16_t>(level*1111+status);
                c.run(fourth ? fourth_record : helper,setup,[&](Machine& m) {
                    set_iy(m,record); set_de(m,table_base);
                    poke(m,static_cast<std::uint16_t>(record+c.status_field()),static_cast<std::uint8_t>(status));
                    poke(m,static_cast<std::uint16_t>(record+c.level_field()),static_cast<std::uint8_t>(level));
                    poke_word(m,static_cast<std::uint16_t>(record+c.exp_field()),exp);
                    poke_word(m,c.reward(),reward);
                },[&](Machine& m, Result r) {
                    const auto sp=setup.sp;
                    if (!(status&1)) {
                        check(r.cycles==(fourth ? 75u : 31u) && returned(m,sp),"a character who is not eligible returns");
                        ++t.helper_ineligible; return;
                    }
                    check(m.cpu().pc.uint16_value==0x1773 && m.cpu().sp.uint16_value==sp,"the reward load ran, then the add is next");
                    check(hl_of(m)==reward && de_of(m)==exp,"reward and experience loaded");
                    check(ix_of(m)==static_cast<std::uint16_t>(table_base+level*8),"IX points at the level's row");
                    if (fourth) { check(iy_of(m)==c.record(3) && peek(m,c.character())==3,"fourth record"); ++t.helper_fourth; }
                    ++t.helper_eligible;
                });
            }
        }
}
// A table-bank write really leaves slot 2 on the level table.
void bank_written(Context& c) {
    probe::Setup setup; setup.banks[2]=0;
    auto machine=c.harness.prepare(helper,setup);
    set_iy(machine,c.record(0)); poke(machine,static_cast<std::uint16_t>(c.record(0)+c.status_field()),1);
    c.harness.run_cpp(machine,helper);
    check(machine.memory().mapping(0x8000)==Memory::Mapping{Memory::Kind::rom,c.level_bank()},"slot 2 holds the level-table bank");
}

// 00:1773 (rate 100: the original instruction): reward and experience pairs
// around the sum edges. The total wraps, the carry says it overflowed.
void add_probes(Context& c, Tally& t) {
    std::vector<unsigned> edges{0,1,2,255,256,257,1000,32767,32768,32769,43690,65533,65534,65535};
    for (unsigned k=0;k<4;++k)
        for (unsigned level=0;level<31;++level) {
            const unsigned threshold=peek_word(c.harness.prepare(add_entry,{}),static_cast<std::uint16_t>(row(c,k,level)+4));
            for (const int delta:{-1,0,1}) edges.push_back(static_cast<std::uint16_t>(threshold+delta));
        }
    std::sort(edges.begin(),edges.end()); edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
    auto probe_pair=[&](unsigned reward, unsigned exp) {
        c.run(add_entry,c.vary(),[&](Machine& m) { set_hl(m,reward); set_de(m,exp); },[&](Machine& m, Result r) {
            const unsigned sum=reward+exp;
            check(r.cycles==11 && r.fetches==1 && m.cpu().pc.uint16_value==0x1774,"the add is one instruction");
            check(hl_of(m)==(sum&0xffff) && ((f_of(m)&1)!=0)==(sum>0xffff),"sum and carry");
            ++t.adds;
        });
    };
    // Every reward and experience that is an edge or its complement to the
    // 65,535/65,536 boundary, and random pairs besides.
    for (const auto reward:edges)
        for (const auto exp:edges) probe_pair(reward,exp);
    for (unsigned reward=0;reward<65536;reward+=1) {
        probe_pair(reward,65535-reward);          // the largest total that does not overflow
        probe_pair(reward,(65536-reward)&0xffff); // the smallest that does (or exactly zero)
    }
    for (unsigned n=0;n<20000;++n) {
        const auto mixed=n*0x9e3779b97f4a7c15ull>>16;
        probe_pair(static_cast<std::uint16_t>(mixed),static_cast<std::uint16_t>(mixed>>20));
    }
}

// The expected outcome of 00:1774 for the entry registers and memory.
struct Total { bool carry; unsigned exit; }; // exit: 0 177F, 1 1785, 2 178A, 3 1791, 4 message
// 00:1774: every character x level 0-255 x totals around the level's threshold
// and the byte edges x the two carries.
void total_probes(Context& c, Tally& t) {
    for (unsigned k=0;k<4;++k)
        for (unsigned level=0;level<256;++level) {
            const auto matching_ix=row(c,k,level);
            auto reference=c.harness.prepare(total_entry,{});
            const unsigned threshold=peek_word(reference,static_cast<std::uint16_t>(matching_ix+c.threshold_low_offset()));
            const unsigned high=threshold>>8, low=threshold&0xff;
            std::vector<unsigned> totals{(threshold+65535)&0xffff,threshold,(threshold+1)&0xffff,0,65535,
                (((high+255)&0xff)<<8)|0xff,((high+1)&0xff)<<8,(high<<8)|0xff,high<<8,(high<<8)|((low+255)&0xff)};
            for (const auto total:totals)
                for (const bool carry:{false,true}) {
                    auto setup=c.vary();
                    const bool own_iy=(total+level)%5==0;
                    const auto record=own_iy ? c.other_record() : c.record(k);
                    // IX points at another character's row of another level in a third of the cases:
                    // the segment reads the machine's IX, not the record's level.
                    const auto ix=((total>>3)+level+k)%3==0 ? row(c,(k+2)%4,(level*5+11)&0xff) : matching_ix;
                    unsigned row_high=0, row_low=0;
                    c.run(total_entry,setup,[&](Machine& m) {
                        set_hl(m,total); set_f(m,(f_of(m)&0xfe)|carry); set_ix(m,ix); set_iy(m,record);
                        // The threshold as the machine shows it before the segment runs (the sound request can sit in it).
                        row_high=peek(m,static_cast<std::uint16_t>(ix+c.threshold_high_offset()));
                        row_low=peek(m,static_cast<std::uint16_t>(ix+c.threshold_low_offset()));
                        poke(m,static_cast<std::uint16_t>(record+c.level_field()),static_cast<std::uint8_t>(level));
                        poke_word(m,static_cast<std::uint16_t>(record+c.exp_field()),0x5a5a);
                    },[&](Machine& m, Result r) {
                        const auto sp=setup.sp;
                        // What the original decides, from the machine's own view of the table.
                        const unsigned stored=carry ? 0xffff : total;
                        check(peek_word(m,static_cast<std::uint16_t>(record+c.exp_field()))==stored,"total stored");
                        const unsigned high_byte=stored>>8, low_byte=stored&0xff;
                        unsigned exit=4;
                        if (carry) exit=0;
                        else if (level==c.last_level()) exit=1;
                        else if (high_byte<row_high) exit=2;
                        else if (high_byte==row_high && low_byte<row_low) exit=3;
                        if (exit<4) { check(returned(m,sp),"the helper returns: exit "+std::to_string(exit)+" k "+std::to_string(k)+" level "+std::to_string(level)+" total "+std::to_string(total)+" carry "+std::to_string(carry)+" pc "+std::to_string(m.cpu().pc.uint16_value)+" sp "+std::to_string(m.cpu().sp.uint16_value-sp)); }
                        else {
                            check(called(m,sp,c.level_text()) && peek(m,c.sound())==c.sound_value() && hl_of(m)==c.level_message(),
                                  "level-up message and sound");
                        }
                        (void)r; ++t.totals;
                    });
                }
        }
}

// 00:179D: every character x level 0-255 x the four equal/unequal outcomes of
// the two magic-number comparisons. The level is incremented; the second
// message is skipped only when both numbers match.
void level_probes(Context& c, Tally& t) {
    for (unsigned k=0;k<4;++k)
        for (unsigned level=0;level<256;++level)
            for (unsigned outcome=0;outcome<4;++outcome) {
                const bool first_differs=outcome&1, second_differs=outcome&2;
                auto setup=c.vary();
                // The segment sets slot 2 itself; the expected magic numbers are read from the table bank.
                setup.banks[2]=3; setup.cartridge_ram=false;
                const bool own_iy=(level+outcome)%5==0;
                const auto record=own_iy ? c.other_record() : c.record(k);
                const auto ix=(level+outcome+k)%3==0 ? row(c,(k+1)%4,(level*3+7)&0xff) : row(c,k,level);
                c.run(level_entry,setup,[&](Machine& m) {
                    set_iy(m,record); set_ix(m,ix);
                    poke(m,static_cast<std::uint16_t>(record+c.level_field()),static_cast<std::uint8_t>(level));
                    const auto battle=peek(m,static_cast<std::uint16_t>(ix+c.table_magic_offset())),
                        map=peek(m,static_cast<std::uint16_t>(ix+c.table_map_magic_offset()));
                    poke(m,static_cast<std::uint16_t>(record+c.battle_magic_field()),static_cast<std::uint8_t>(first_differs ? battle^0x5a : battle));
                    poke(m,static_cast<std::uint16_t>(record+c.map_magic_field()),static_cast<std::uint8_t>(second_differs ? map+1 : map));
                },[&](Machine& m, Result r) {
                    const auto sp=setup.sp;
                    check(peek(m,static_cast<std::uint16_t>(record+c.level_field()))==static_cast<std::uint8_t>(level+1),"one level gained");
                    (void)r;
                    if (!first_differs && !second_differs) check(returned(m,sp),"both numbers match: return k "+std::to_string(k)+" level "+std::to_string(level)+" own "+std::to_string(own_iy)+" pc "+std::to_string(m.cpu().pc.uint16_value)+" sp "+std::to_string(sp)+" "+std::to_string(m.cpu().sp.uint16_value)+" ix "+std::to_string(ix));
                    else check(jumped(m,sp,c.second_text()) && hl_of(m)==c.second_message(),"a number differs: second message");
                    ++t.levels;
                });
            }
}

// ---- Variations over every segment -------------------------------------------

struct Family {
    RoutineEntry entry;
    unsigned fetches;
    unsigned scratch;
    std::function<void(Context&, Machine&, unsigned)> arrange;
};
std::vector<Family> families() {
    using M=Machine;
    auto records=[](Context& c, M& m, unsigned k, unsigned level) {
        set_iy(m,c.record(k)); poke(m,static_cast<std::uint16_t>(c.record(k)+c.status_field()),1);
        poke(m,static_cast<std::uint16_t>(c.record(k)+c.level_field()),static_cast<std::uint8_t>(level));
    };
    return {
        {award,2,0,[](Context& c, M& m, unsigned k) { poke_word(m,c.reward(),static_cast<std::uint16_t>(k*30000)); }},
        {message,8,0,[](Context&, M& m, unsigned k) { set_hl(m,k*30000); }},
        {first_record,12,0,[](Context&, M& m, unsigned k) { set_f(m,k*0x55); }},
        {second_record,12,0,[](Context&, M& m, unsigned k) { set_f(m,k*0x55); }},
        {third_record,12,0,[](Context&, M& m, unsigned k) { set_f(m,k*0x55); }},
        {fourth_record,40,2,[records](Context& c, M& m, unsigned k) { records(c,m,3,k*9+1); poke_word(m,c.reward(),static_cast<std::uint16_t>(k*999)); }},
        {helper,40,2,[records](Context& c, M& m, unsigned k) {
            records(c,m,k,k*9+1); set_de(m,c.table(k)); poke_word(m,c.reward(),static_cast<std::uint16_t>(k*999)); }},
        {add_entry,2,0,[](Context&, M& m, unsigned k) { set_hl(m,k*30000+7); set_de(m,65535-k*20000); }},
        {total_entry,40,0,[](Context& c, M& m, unsigned k) {
            set_iy(m,c.record(k)); set_ix(m,row(c,k,k*9+1)); set_hl(m,k*30000+5);
            poke(m,static_cast<std::uint16_t>(c.record(k)+c.level_field()),static_cast<std::uint8_t>(k*9+1)); set_f(m,k&1 ? 0x01 : 0x00); }},
        {level_entry,30,0,[](Context& c, M& m, unsigned k) {
            set_iy(m,c.record(k)); set_ix(m,row(c,k,k*9+1));
            poke(m,static_cast<std::uint16_t>(c.record(k)+c.level_field()),static_cast<std::uint8_t>(k*9+1));
            poke(m,static_cast<std::uint16_t>(c.record(k)+c.battle_magic_field()),static_cast<std::uint8_t>(k)); }},
    };
}
// Slot 2 on other banks and on cartridge RAM, R around its wrap, stack
// placements in RAM, ROM and on the mapper registers, work RAM disabled.
void variations(Context& c, Tally& t) {
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
                if (stack.admitted(family.scratch)) c.run(family.entry,setup,arrange);
                else {
                    auto machine=c.harness.prepare(family.entry,setup);
                    arrange(machine);
                    c.harness.run_fallback(machine,family.entry,RoutineFallback::unmapped_stack);
                }
            }
        }
        check(c.probes>runs_before,"variations ran");
    }
    t.variations=c.probes;
}

// A stack on the mapper write: the level segment's bank write changes the
// stack's last byte (the RAM mirror), so its return goes elsewhere; the
// original's instructions are compared instead of the verifier's stop.
void own_return(Context& c) {
    auto setup=c.vary(); setup.sp=0xdffe; setup.banks[2]=3;
    auto machine=c.harness.prepare(level_entry,setup);
    set_iy(machine,c.record(1)); set_ix(machine,row(c,1,4));
    const auto ix=ix_of(machine);
    poke(machine,static_cast<std::uint16_t>(c.record(1)+c.battle_magic_field()),peek(machine,static_cast<std::uint16_t>(ix+c.table_magic_offset())));
    poke(machine,static_cast<std::uint16_t>(c.record(1)+c.map_magic_field()),peek(machine,static_cast<std::uint16_t>(ix+c.table_map_magic_offset())));
    // Nine instructions of the original up to its return; the core takes an index prefix as a step of its own.
    c.harness.run_cpp_against_steps(machine,level_entry,14);
    check(machine.cpu().pc.uint16_value==static_cast<std::uint16_t>(c.rom.byte(0,0x175D)<<8|(caller&0xff)),
          "return address with the bank number in its high byte");
}

// The harness catches a wrong body: the message segment with R one fetch ahead.
void sabotage(Context& c) {
    const auto& real=c.harness.routine(message);
    RoutineRegistry sabotaged;
    replay::CallVerifier verifier("sabotage");
    sabotaged.verifier=&verifier;
    sabotaged.add(message,real.spec,real.bound,[body=real.body](RoutineContext& context) {
        auto& r=context.cpu().r; r=static_cast<std::uint8_t>((r&0x80)|((r+1)&0x7f)); body(context);
    });
    auto machine=c.harness.prepare(message,{});
    set_hl(machine,300);
    bool caught=false;
    try { machine.step(sabotaged); } catch (const std::runtime_error& e) { caught=std::string(e.what()).find("Divergence")==0; }
    check(caught,"the verifier did not catch a sabotaged body");
}

// ---- Whole awards, segment after segment ---------------------------------------

std::uint64_t fallbacks(const RoutineRegistry& registry) {
    std::uint64_t total=0;
    for (const auto& [entry,routine]:registry.entries()) total+=routine.counters.fallback_calls;
    return total;
}
// The text routine, replaced by its return: the machine goes back to the
// caller of the call that reached it, registers untouched. The same on every
// machine that runs an award, so they stay comparable.
bool stub_text(Context& c, Machine& m) {
    if (m.cpu().pc.uint16_value!=c.text_entry()) return false;
    const auto sp=m.cpu().sp.uint16_value;
    m.cpu().pc.uint16_value=static_cast<std::uint16_t>(peek(m,sp)|(peek(m,static_cast<std::uint16_t>(sp+1))<<8));
    m.cpu().sp.uint16_value=static_cast<std::uint16_t>(sp+2);
    return true;
}
// Runs an award from its entry to its return: on the registry (every segment
// in C++), or natively when registry is null. Returns the number of text calls.
unsigned run_award(Context& c, Machine& m, RoutineRegistry* registry) {
    const auto sp=m.cpu().sp.uint16_value;
    unsigned texts=0, steps=0;
    while (!(m.cpu().pc.uint16_value==caller && m.cpu().sp.uint16_value==static_cast<std::uint16_t>(sp+2))) {
        if (stub_text(c,m)) { ++texts; continue; }
        if (registry) m.step(*registry); else m.step();
        check(++steps<40000,"award did not return");
    }
    return texts;
}
void setup_character(Context& c, Machine& m, unsigned k, unsigned status, unsigned level, unsigned exp, unsigned battle, unsigned map) {
    const auto base=c.record(k);
    poke(m,static_cast<std::uint16_t>(base+c.status_field()),static_cast<std::uint8_t>(status));
    poke(m,static_cast<std::uint16_t>(base+c.level_field()),static_cast<std::uint8_t>(level));
    poke_word(m,static_cast<std::uint16_t>(base+c.exp_field()),static_cast<std::uint16_t>(exp));
    poke(m,static_cast<std::uint16_t>(base+c.battle_magic_field()),static_cast<std::uint8_t>(battle));
    poke(m,static_cast<std::uint16_t>(base+c.map_magic_field()),static_cast<std::uint8_t>(map));
}
unsigned exp_of(Context& c, const Machine& m, unsigned k) { return peek_word(m,static_cast<std::uint16_t>(c.record(k)+c.exp_field())); }
unsigned level_of(Context& c, const Machine& m, unsigned k) { return peek(m,static_cast<std::uint16_t>(c.record(k)+c.level_field())); }

// Whole awards from 00:170D with random rewards, eligibility, levels and
// experience around the thresholds: the registry (verifier on) against a native
// copy, machine for machine, with no fallback.
void award_chains(Context& c, Tally& t) {
    std::uint64_t level_ups=0;
    for (unsigned n=0;n<3000;++n) {
        auto setup=c.vary(); setup.iff1=setup.iff2=false; setup.sp=0xdf00; setup.banks[2]=3; setup.cartridge_ram=false;
        auto machine=c.harness.prepare(award,setup);
        const auto mixed=n*0x9e3779b97f4a7c15ull;
        const unsigned reward=n%7==0 ? 0 : (n%11==0 ? 65535 : static_cast<unsigned>((mixed>>8)%(n%3==0 ? 60000 : 900)));
        poke_word(machine,c.reward(),static_cast<std::uint16_t>(reward));
        for (unsigned k=0;k<4;++k) {
            const auto bits=static_cast<unsigned>(mixed>>(12+k*7));
            const unsigned level=(bits%9==0) ? 29 : (bits%9==1) ? 30 : bits%31;
            const unsigned threshold=peek_word(machine,static_cast<std::uint16_t>(row(c,k,level)+4));
            const unsigned exp=(bits%4==0) ? (threshold-reward+(bits>>8)%3-1)&0xffff : (bits%4==1) ? 65535-(bits>>8)%(reward+1) : (bits>>5)&0xffff;
            const auto row_base=row(c,k,level+1);
            setup_character(c,machine,k,(bits%5==0) ? 0 : (bits%5==1) ? 0x81 : 1,level,exp&0xffff,
                peek(machine,static_cast<std::uint16_t>(row_base+c.table_magic_offset()))^(bits&1),
                peek(machine,static_cast<std::uint16_t>(row_base+c.table_map_magic_offset()))^((bits>>1)&1));
        }
        auto native=machine;
        const auto before=fallbacks(c.harness.registry());
        const auto texts=run_award(c,machine,&c.harness.registry());
        const auto native_texts=run_award(c,native,nullptr);
        check(texts==native_texts,"text calls differ");
        std::string fields;
        for (const auto& difference:native.differences(machine)) fields+=" "+difference.field;
        check(fields.empty(),"award differs from the original:"+fields);
        check(fallbacks(c.harness.registry())==before,"a segment fell back inside a whole award");
        level_ups+=texts>1 ? 1 : 0;
        ++t.chains; ++c.probes;
    }
    check(level_ups>100,"awards with level-ups are reached");
}

// ---- Experience-rate option ----------------------------------------------------

constexpr unsigned rates[]{100,200,400}; // the allowed values (maintainer decision, 2026-11-11)
// The reward as the option should scale it, written out independently of the
// segments: rounded down, at least 1 for a nonzero reward, at most 65,535.
unsigned expected_scaled(unsigned reward, unsigned percent) {
    if (!reward) return 0;
    unsigned long long value=static_cast<unsigned long long>(reward)*percent/100;
    if (value<1) value=1;
    return static_cast<unsigned>(value>65535 ? 65535 : value);
}
// Properties of the scaling that need no machine.
void scaling_properties() {
    for (const auto percent:rates) {
        unsigned previous=0;
        for (unsigned reward=0;reward<65536;++reward) {
            const auto scaled=expected_scaled(reward,percent);
            check(scaled<=65535 && (reward==0)==(scaled==0),"a nonzero reward never scales to zero");
            check(scaled>=previous,"scaling is monotonic");
            previous=scaled;
            if (percent==100) check(scaled==reward,"100 percent is the original");
            if (percent<100) check(scaled<=reward,"fewer than the original");
            if (percent>100) check(scaled>=reward,"more than the original");
            // Exact floor of reward x percent / 100 unless clamped to 1 or to the cap.
            if (scaled>1 && scaled<65535) check(scaled*100<=reward*percent && reward*percent<(scaled+1)*100,"rounded down");
        }
    }
    check(expected_scaled(65535,200)==65535 && expected_scaled(16383,400)==65532 && expected_scaled(16384,400)==65535 && expected_scaled(1,200)==2 && expected_scaled(12500,400)==50000,"examples");
}

// The displayed number, the value added to each eligible character and the
// formula, for every reward and rate. The verifier is off: it compares with
// the unscaled instruction. The stored reward stays as it was.
void rate_formula(const probe::Rom& rom, unsigned rate, std::uint64_t& count) {
    probe::Harness harness(rom,game::Options{100,rate});
    harness.registry().verifier=nullptr;
    Context c{rom,harness};
    for (const auto entry:{award,add_entry}) {
        const auto& spec=harness.routine(entry).spec;
        check(spec.force_admit==(rate!=100) && spec.single_instruction,"option declaration");
    }
    probe::Setup setup; setup.seed=rate; setup.iff1=true;
    const auto load_base=harness.prepare(award,setup), add_base=harness.prepare(add_entry,setup);
    auto admitted=[&](RoutineEntry entry, const RoutineCounters& before) {
        const auto& after=harness.routine(entry).counters;
        check(after.cpp_calls==before.cpp_calls+1 && after.fallback_calls==before.fallback_calls &&
              after.forced_calls==before.forced_calls+(rate!=100) && after.forced_failed==0,"call was not admitted in C++");
    };
    for (unsigned reward=0;reward<65536;++reward) {
        const auto scaled=expected_scaled(reward,rate);
        // The display: the loaded value, then the original store of the displayed number.
        auto shown=load_base;
        poke_word(shown,c.reward(),static_cast<std::uint16_t>(reward));
        auto before=harness.routine(award).counters;
        shown.step(harness.registry());
        admitted(award,before);
        check(hl_of(shown)==scaled && peek_word(shown,c.reward())==reward,"value loaded for display");
        before=harness.routine(message).counters;
        shown.step(harness.registry());
        check(harness.routine(message).counters.cpp_calls==before.cpp_calls+1,"message segment ran in C++");
        check(peek_word(shown,c.displayed())==scaled,"displayed number");
        // The addition for an eligible character with no experience: the value added.
        auto added=add_base;
        set_hl(added,reward); set_de(added,0);
        before=harness.routine(add_entry).counters;
        added.step(harness.registry());
        admitted(add_entry,before);
        check(hl_of(added)==scaled && !(f_of(added)&1) && added.cpu().pc.uint16_value==0x1774,"value added to the character");
        check(hl_of(added)==peek_word(shown,c.displayed()),"the displayed value is the value added");
        // Totals at and just past the cap: the carry is never left set, the total saturates.
        if (scaled) for (const int offset:{0,1}) {
            const unsigned experience=65535-scaled+offset;
            auto sum=add_base;
            set_hl(sum,reward); set_de(sum,experience);
            sum.step(harness.registry());
            if (rate==100) check(hl_of(sum)==((scaled+experience)&0xffff) && ((f_of(sum)&1)!=0)==(scaled+experience>65535),"the original addition: the total wraps and the carry stays");
            else check(hl_of(sum)==65535 && !(f_of(sum)&1),"the total saturates and the carry is cleared: rate "+std::to_string(rate)+" reward "+std::to_string(reward)+" offset "+std::to_string(offset));
        }
        count+=2;
    }
    if (rate==100) {
        // At 100 the segments are the original instructions.
        for (const auto entry:{award,add_entry}) check(!harness.routine(entry).spec.force_admit,"not forced at 100");
    }
}

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
// The oracle for a non-default rate: the original instruction, run natively on
// a copy, with the adjustment the option describes written out again.
void oracle_load(Machine& m, unsigned rate) { m.step(); set_hl(m,expected_scaled(hl_of(m),rate)); }
void oracle_add(Machine& m, unsigned rate) {
    set_hl(m,expected_scaled(hl_of(m),rate)); m.step();
    if (f_of(m)&1) { set_hl(m,0xffff); set_f(m,f_of(m)&0xfe); }
}
struct WindowCounts { std::uint64_t forced=0, irq=0, nmi=0, frame=0, ei=0, fallbacks=0, accepted=0; };
// With a non-default rate both option segments are admitted in every
// interrupt, NMI and frame window, with no added delay: the machine equals the
// oracle, also a few steps later.
void windows(const probe::Rom& rom, WindowCounts& counts) {
    for (const unsigned rate:{200u,400u}) {
        probe::Harness harness(rom,game::Options{100,rate});
        harness.registry().verifier=nullptr;
        for (const auto entry:{award,add_entry}) {
            auto oracle=[&](Machine& m) { if (entry==award) oracle_load(m,rate); else oracle_add(m,rate); };
            auto arrange=[&](Machine& m, unsigned offset) {
                if (entry==award) poke_word(m,Context{rom,harness}.reward(),static_cast<std::uint16_t>(1000+offset*9000));
                else { set_hl(m,1000+offset*9000); set_de(m,offset%2 ? 65535-offset*500 : offset*1000); }
            };
            for (unsigned kind=0;kind<4;++kind)
                for (unsigned offset=0;offset<7;++offset) {
                    probe::Setup setup; setup.seed=rate*8+offset; setup.iff1=setup.iff2=kind!=3; setup.sp=0xdf00;
                    auto base=harness.prepare(entry,setup);
                    make_window(base,kind,offset);
                    // An interrupt accepted at the entry itself starts no call.
                    if (base.interrupt_accepted_at_boundary()) { ++counts.accepted; continue; }
                    arrange(base,offset);
                    Machine expected=base, actual=base;
                    oracle(expected);
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
                    check(expected.equivalent(actual),"forced call differs from the oracle");
                    for (unsigned step=0;step<6;++step) {
                        expected.step(); actual.step();
                        check(expected.equivalent(actual),"forced call differs a few steps later");
                    }
                }
            // EI delay: forced, counted, equal.
            probe::Setup setup; setup.iff1=setup.iff2=true;
            auto base=harness.prepare(entry,setup);
            base.cpu().data.uint8_array[0]=0xfb;
            arrange(base,3);
            Machine expected=base, actual=base;
            oracle(expected);
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
    for (const auto entry:{award,add_entry})
        for (unsigned kind=1;kind<4;++kind) {
            probe::Setup setup; setup.iff1=setup.iff2=kind!=3; setup.sp=0xdf00;
            auto machine=original.prepare(entry,setup);
            make_window(machine,kind,kind==3 ? 0 : 2);
            check(!machine.interrupt_accepted_at_boundary(),"control window starts a call");
            original.run_fallback(machine,entry,kind==1 ? RoutineFallback::irq : kind==2 ? RoutineFallback::nmi : RoutineFallback::frame);
            ++counts.fallbacks;
        }
}

// With a non-default rate and no total overflowing, a whole award equals the
// original run on the scaled reward, apart from the stored reward which keeps
// its value; every segment ran in C++ and none was forced to fall back.
void award_chains_with_rate(const probe::Rom& rom, unsigned rate, std::uint64_t& count) {
    probe::Harness harness(rom,game::Options{100,rate});
    harness.registry().verifier=nullptr;
    Context c{rom,harness};
    for (unsigned n=0;n<400;++n) {
        auto setup=c.vary(); setup.iff1=setup.iff2=false; setup.sp=0xdf00; setup.banks[2]=3; setup.cartridge_ram=false;
        auto machine=harness.prepare(award,setup);
        const auto mixed=(n+rate)*0x9e3779b97f4a7c15ull;
        const unsigned reward=1+static_cast<unsigned>((mixed>>8)%(65535/rate>60 ? 65535/rate : 60));
        const unsigned scaled=expected_scaled(reward,rate);
        poke_word(machine,c.reward(),static_cast<std::uint16_t>(reward));
        for (unsigned k=0;k<4;++k) {
            const auto bits=static_cast<unsigned>(mixed>>(12+k*7));
            const unsigned level=(bits%9==0) ? 29 : (bits%9==1) ? 30 : bits%31;
            const unsigned threshold=peek_word(machine,static_cast<std::uint16_t>(row(c,k,level)+4));
            unsigned exp=(bits%2==0) ? (threshold+65536-scaled+(bits>>8)%3-1)&0xffff : (bits>>5)&0xffff;
            if (exp+scaled>65535) exp=65535-scaled;           // no overflow: the cap rule has its own tests
            const auto row_base=row(c,k,level+1);
            setup_character(c,machine,k,(bits%5==0) ? 0 : 1,level,exp,
                peek(machine,static_cast<std::uint16_t>(row_base+c.table_magic_offset()))^(bits&1),
                peek(machine,static_cast<std::uint16_t>(row_base+c.table_map_magic_offset())));
        }
        auto native=machine;
        poke_word(native,c.reward(),static_cast<std::uint16_t>(scaled));
        run_award(c,machine,&harness.registry());
        run_award(c,native,nullptr);
        check(peek_word(machine,c.reward())==reward,"the stored reward is unchanged");
        poke_word(native,c.reward(),static_cast<std::uint16_t>(reward));
        std::string fields;
        for (const auto& difference:native.differences(machine)) fields+=" "+difference.field;
        check(fields.empty(),"award at a rate differs from the original on the scaled reward:"+fields);
        check(peek_word(machine,c.displayed())==scaled,"the displayed reward is the scaled one");
        ++count;
    }
}

// ---- The cap rule --------------------------------------------------------------

// Runs one award for the four characters and returns the machine.
struct Cap {
    Context& c;
    probe::Harness& harness;
    Machine machine;
    unsigned texts=0;
    void award_once(unsigned reward) {
        poke_word(machine,c.reward(),static_cast<std::uint16_t>(reward));
        machine.cpu().pc.uint16_value=award.address; // the award's entry, called from the caller
        machine.cpu().sp.uint16_value=0xdf00; poke_word(machine,0xdf00,caller);
        texts=run_award(c,machine,&harness.registry());
    }
};
// With a non-default rate, a character below level 30 whose total would
// overflow ends at the cap and still passes the level test: one level per
// award until 30, then nothing changes. With rate 100 the original early
// return is kept.
void cap_rule(const probe::Rom& rom, std::uint64_t& awards) {
    for (const unsigned rate:{100u,200u,400u}) {
        probe::Harness harness(rom,game::Options{100,rate});
        if (rate!=100) harness.registry().verifier=nullptr;
        Context c{rom,harness};
        probe::Setup setup; setup.iff1=setup.iff2=false; setup.sp=0xdf00;
        auto machine=harness.prepare(award,setup);
        // A reward whose scaled value is at least 40,000 at this rate (30,000 scales to 60,000 at 200 and clamps at 65,535 at 400), so every start below overflows.
        const unsigned reward=rate==100 ? 40000u : 30000u;
        const unsigned scaled=expected_scaled(reward,rate);
        for (unsigned k=0;k<4;++k) {
            const auto row_base=row(c,k,2);
            // Levels 1-3 start low; the characters differ in level and experience so the paths differ.
            setup_character(c,machine,k,1,1+k*7,(rate==100 ? 65535u-scaled+1+k : 65535u-scaled+10+k)&0xffff,
                peek(machine,static_cast<std::uint16_t>(row_base+c.table_magic_offset())),
                peek(machine,static_cast<std::uint16_t>(row_base+c.table_map_magic_offset())));
        }
        Cap cap{c,harness,machine};
        if (rate==100) {
            // The original: the first award overflows, stores the cap and leaves before the level test.
            cap.award_once(reward);
            for (unsigned k=0;k<4;++k) {
                check(exp_of(c,cap.machine,k)==65535 && level_of(c,cap.machine,k)==1+k*7,
                      "original quirk: the total reaches the cap and the level does not change");
            }
            cap.award_once(reward);
            for (unsigned k=0;k<4;++k) check(exp_of(c,cap.machine,k)==65535 && level_of(c,cap.machine,k)==1+k*7,"original quirk persists");
            awards+=2;
            continue;
        }
        std::array<unsigned,4> levels{1,8,15,22};
        for (unsigned award_number=0;award_number<40;++award_number) {
            cap.award_once(reward);
            for (unsigned k=0;k<4;++k) {
                const unsigned expected=std::min(30u,levels[k]+1);
                check(exp_of(c,cap.machine,k)==65535,"the total is the cap");
                check(level_of(c,cap.machine,k)==expected,"exactly one level per award until 30");
                levels[k]=expected;
            }
            // The award text comes with a message for every level gained.
            ++awards;
        }
        for (unsigned k=0;k<4;++k) check(levels[k]==30,"every character reached level 30");
        // At level 30 nothing changes and no level message is shown: only the reward message.
        cap.award_once(reward);
        check(cap.texts==1,"at level 30 only the reward message is shown");
        for (unsigned k=0;k<4;++k) check(level_of(c,cap.machine,k)==30 && exp_of(c,cap.machine,k)==65535,"level 30 stays");
        ++awards;
    }
}

// ---- Is the experience cap reachable in the original? -------------------------------

struct CapReach { unsigned largest_reward=0, highest_enemy=0, enemy_id=0, count=0; std::array<unsigned,4> margin{}, reachable{}; };
// Reads the enemy table, the group tables, the fixed encounters and the level
// tables through the machine, as the game sees them, and bounds the largest
// reward and what a character's total can be before an award.
CapReach cap_reachability(const probe::Rom& rom) {
    probe::Harness harness(rom);
    Context c{rom,harness};
    const auto machine=harness.prepare(award,{});
    auto word=[&](std::uint16_t address) { return peek_word(machine,address); };
    // Table addresses as the original instructions name them.
    const auto enemy_table=c.operand(1,0x602D), pool_table=c.operand(1,0x5FE6);
    const auto chosen=c.operand(1,0x5FF4);
    // The enemy identifiers the game can name: every group of every pool, and the
    // fixed encounters (an immediate load stored to the chosen-enemy variable).
    unsigned highest=0;
    for (unsigned pool=1;pool<(enemy_table-pool_table)/8u;++pool)
        for (unsigned member=0;member<8;++member) highest=std::max<unsigned>(highest,peek(machine,static_cast<std::uint16_t>(pool_table+pool*8+member))&0x7f);
    unsigned fixed=0;
    for (unsigned address=0x4000;address+4<0x8000;++address)
        if (peek(machine,static_cast<std::uint16_t>(address))==0x3e && peek(machine,static_cast<std::uint16_t>(address+2))==0x32 &&
            word(static_cast<std::uint16_t>(address+3))==chosen) {
            ++fixed; highest=std::max<unsigned>(highest,peek(machine,static_cast<std::uint16_t>(address+1))&0x7f);
        }
    check(fixed>=10,"fixed encounters are found");
    CapReach result;
    result.highest_enemy=highest;
    // Reward of one battle: the record's experience times the group size the
    // setup builds (a fixed size, or a draw up to the record's maximum capped at
    // twice the party size, which is eight for four members), kept to 16 bits.
    for (unsigned id=1;id<=highest;++id) {
        const auto record=static_cast<std::uint16_t>(enemy_table+id*32);
        const unsigned count_byte=peek(machine,static_cast<std::uint16_t>(record+20)), experience=word(static_cast<std::uint16_t>(record+28));
        const unsigned size=(count_byte&0x80) ? count_byte&15 : std::min(count_byte&15,8u);
        const unsigned reward=(experience*size)&0xffff;
        if (reward>result.largest_reward) { result.largest_reward=reward; result.enemy_id=id; result.count=size; }
    }
    // Largest total a character can hold at a level before an award: staying below
    // the level's threshold, or arriving by a level gain with the largest award.
    // The total reaches the cap below level 30 only if that plus the largest award
    // is above 65,535.
    for (unsigned k=0;k<4;++k) {
        std::vector<unsigned> threshold(c.last_level());
        for (unsigned level=0;level<threshold.size();++level) threshold[level]=word(static_cast<std::uint16_t>(row(c,k,level)+4));
        result.margin[k]=65535-threshold[c.last_level()-1];
        unsigned largest=threshold[1]-1, worst=0;
        for (unsigned level=1;level<c.last_level();++level) {
            if (level>1) largest=std::max(largest+result.largest_reward,threshold[level]-1);
            worst=std::max(worst,largest);
        }
        result.reachable[k]=worst+result.largest_reward;
    }
    return result;
}
}

int main(int argc, char** argv) { try {
    registration();
    scaling_properties();
    if (argc==1) { std::cout<<"Experience registration and scaling checks passed.\n"; return 0; }
    if (argc!=3) return 1;
    const auto rom=probe::load(argv[1],argv[2]);
    if (!rom) { std::cout<<"ROM path missing; skipping.\n"; return 77; }
    const auto start=std::chrono::steady_clock::now();
    auto seconds=[&] { return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(); };
    probe::Harness harness(*rom);
    for (const auto& entry:game::experience_entries) check(harness.registry().find(entry)!=nullptr,"experience entry registered for the ROM");
    Context c{*rom,harness};
    Tally t;
    sabotage(c);
    award_load(c,t);
    award_message(c,t);
    records(c,t);
    const auto after_simple=seconds();
    helper_probes(c,t);
    bank_written(c);
    add_probes(c,t);
    total_probes(c,t);
    level_probes(c,t);
    const auto before_variations=c.probes;
    variations(c,t);
    t.variations-=before_variations;
    own_return(c);
    award_chains(c,t);
    std::cout<<"Segment probes (C++, verifier clean, announced bound equal to the cost): "
        <<"170D "<<t.award_load<<" rewards; 1710 "<<t.message<<" rewards; 171C/172A/1739 "<<t.records
        <<" records; helper "<<t.helper_ineligible<<" ineligible + "<<t.helper_eligible<<" eligible (of which "<<t.helper_fourth
        <<" at 1748); 1773 "<<t.adds<<" sums; 1774 "<<t.totals<<" totals; 179D "<<t.levels
        <<" levels; variations (slot 2, R wrap, stacks) "<<t.variations<<"; whole awards "<<t.chains<<"; "<<seconds()<<" s ("
        <<after_simple<<" s for the first three).\n";

    // The experience-rate option.
    std::uint64_t formula=0, with_rate=0, cap_awards=0;
    for (const auto rate:rates) rate_formula(*rom,rate,formula);
    for (const auto rate:rates) if (rate!=100) award_chains_with_rate(*rom,rate,with_rate);
    WindowCounts window;
    windows(*rom,window);
    cap_rule(*rom,cap_awards);
    std::cout<<"Experience-rate option: "<<formula<<" reward checks over "<<std::size(rates)<<" rates x 65536 rewards (display, addition, cap); "
        <<with_rate<<" whole awards at rates; "<<window.forced<<" forced windows ("<<window.irq<<" irq, "<<window.nmi<<" nmi, "<<window.frame
        <<" frame reasons), "<<window.ei<<" after EI, "<<window.fallbacks<<" default-rate windows falling back, "<<window.accepted
        <<" cases with the interrupt accepted at the entry; cap rule "<<cap_awards<<" awards; "<<seconds()<<" s total.\n";

    // Can the original reach the cap below level 30?
    const auto reach=cap_reachability(*rom);
    unsigned smallest_margin=65535, largest_reach=0;
    for (unsigned k=0;k<4;++k) { smallest_margin=std::min(smallest_margin,reach.margin[k]); largest_reach=std::max(largest_reach,reach.reachable[k]); }
    std::cout<<"Cap reachability (ROM static): enemy identifiers up to "<<reach.highest_enemy<<"; largest reward of one battle "
        <<reach.largest_reward<<" (enemy "<<reach.enemy_id<<", group of "<<reach.count<<"); margin between the last level's threshold and 65,535: ";
    for (const auto margin:reach.margin) std::cout<<margin<<' ';
    std::cout<<"; largest total before an award that can still be below level 30, plus the largest reward: ";
    for (const auto value:reach.reachable) std::cout<<value<<' ';
    std::cout<<".\n";
    check(reach.largest_reward<smallest_margin && largest_reach<65536,"the cap is reachable below level 30 in the original");
    return 0;
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
