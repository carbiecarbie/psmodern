// SPDX-License-Identifier: GPL-3.0-or-later
// The experience award after a battle, rewritten as segments (milestone 4,
// part D): the award itself (00:170D) and its per-character helper (00:1754).
// Every segment renders the original instructions from its entry for any
// machine state, its final instruction included (milestone 14): a return, a
// call or jump into original code (the text routine comes back to the next
// segment's entry), or a plain instruction.
//
// Each segment is written once, as a sequence of instruction renderings of the
// shared menu kit; the kit runs it on the live bus and, to learn the cost of
// the path this machine will take (the segment's bound), on an estimating bus.
// The helper is one function used both by its own entry and, inline, by the
// segment that falls into it.
//
// The experience-rate option sits in the two single-instruction segments that
// read the reward: the load behind the displayed number and the addition for
// each character. At the default rate both render the original instruction;
// with the option on the segment renders it and adjusts a register around it.
#include "game/experience.h"
#include "game/menu_kit.h"
#include <algorithm>
#include <utility>

namespace psmodern::game {
namespace {
namespace k=menu_kit;
using k::Index;

// ---- The award -------------------------------------------------------------

// 00:1710-1719: the displayed number is stored; a zero reward ends the award;
// otherwise the reward message is shown (the text routine is another registered segment).
template<class Z> k::Exit award_message(Z& z) {
    auto& r=z.r;
    z.store_hl_direct(0x1710);
    z.copy(0x1713,r.a,r.l);
    z.or_register(0x1714,r.h);
    if (auto exit=z.final_return_if(0x1715,k::Condition::z)) return *exit;
    z.load_hl(0x1716);
    return z.final_call(0x1719);
}
// The four record setups: iy and de name the character's record and level
// table, the character number is stored (the first by clearing the
// accumulator), and the helper follows.
template<class Z> void record_setup(Z& z, std::uint16_t at, bool first) {
    auto& r=z.r;
    z.load_index(at,Index::iy);
    z.load_de(at+4);
    if (first) { z.xor_a(at+7); z.store_a_direct(at+8); }
    else { z.load_immediate(at+7,r.a); z.store_a_direct(at+9); }
}
// 00:171C: the first character, then a call of the helper.
template<class Z> k::Exit first_record(Z& z) {
    record_setup(z,0x171C,true);
    return z.final_call(0x1727);
}
// 00:172A and 00:1739: the second and third characters.
template<class Z> k::Exit middle_record(Z& z, std::uint16_t at) {
    record_setup(z,at,false);
    return z.final_call(static_cast<std::uint16_t>(at+12));
}

// ---- The helper, one character ------------------------------------------------

// 00:1754-1770: a character who is not eligible ends the helper; otherwise
// the level table row for the current level is found (slot 2 is set to the
// table bank) and the character's experience loaded (the load at 1770 is the
// final instruction).
template<class Z> k::Exit helper_start(Z& z) {
    auto& r=z.r;
    z.test_bit_indexed(0x1754,Index::iy,0);
    if (auto exit=z.final_return_if(0x1758,k::Condition::z)) return *exit;
    z.load_hl(0x1759);
    z.store_to_hl(0x175C);                 // slot 2 bank register
    z.load_from_indexed(0x175E,Index::iy,r.l);
    z.load_immediate(0x1761,r.h);
    z.add_to_hl(0x1763,z.hl()); z.add_to_hl(0x1764,z.hl()); z.add_to_hl(0x1765,z.hl());
    z.add_to_hl(0x1766,z.de());
    z.push(0x1767,r.h,r.l);
    z.pop_index(0x1768,Index::ix);
    z.load_from_indexed(0x176A,Index::iy,r.e);
    z.load_from_indexed(0x176D,Index::iy,r.d);
    z.load_hl_direct(0x1770);
    return k::fall_through(0x1770,0x1773);
}
// 00:1748: the fourth character's setup, then the helper.
template<class Z> k::Exit last_record(Z& z) {
    record_setup(z,0x1748,false);
    return helper_start(z);
}

// 00:1774-179A: the new total is stored (the overflow of the addition saturates
// it); an overflow, level 30 or too little experience end the helper;
// otherwise the level-up sound is requested and the message shown.
template<class Z> k::Exit helper_total(Z& z) {
    auto& r=z.r;
    if (!z.relative_if(0x1774,k::Condition::nc)) z.load_hl(0x1776);
    z.store_to_indexed(0x1779,Index::iy,r.l);
    z.store_to_indexed(0x177C,Index::iy,r.h);
    if (auto exit=z.final_return_if(0x177F,k::Condition::c)) return *exit;
    z.load_from_indexed(0x1780,Index::iy,r.a);
    z.compare_immediate(0x1783);
    if (auto exit=z.final_return_if(0x1785,k::Condition::z)) return *exit;
    z.copy(0x1786,r.a,r.h);
    z.subtract_indexed(0x1787,Index::ix);
    if (auto exit=z.final_return_if(0x178A,k::Condition::c)) return *exit;
    if (!z.relative_if(0x178B,k::Condition::nz)) {
        z.copy(0x178D,r.a,r.l);
        z.subtract_indexed(0x178E,Index::ix);
        if (auto exit=z.final_return_if(0x1791,k::Condition::c)) return *exit;
    }
    z.load_immediate(0x1792,r.a);
    z.store_a_direct(0x1794);              // the sound request
    z.load_hl(0x1797);
    return z.final_call(0x179A);
}

// 00:179D-17B7: the level is incremented (slot 2 is set to the table bank
// first); the magic numbers of the new level are compared with the character's
// and, when either differs, the second message is shown (a jump to the text
// routine, which returns to the award's caller).
template<class Z> k::Exit helper_level(Z& z) {
    auto& r=z.r;
    z.load_hl(0x179D);
    z.store_to_hl(0x17A0);                 // slot 2 bank register
    z.increment_indexed(0x17A2,Index::iy);
    z.load_from_indexed(0x17A5,Index::ix,r.a);
    z.compare_indexed(0x17A8,Index::iy);
    if (!z.relative_if(0x17AB,k::Condition::nz)) {
        z.load_from_indexed(0x17AD,Index::ix,r.a);
        z.compare_indexed(0x17B0,Index::iy);
        if (auto exit=z.final_return_if(0x17B3,k::Condition::z)) return *exit;
    }
    z.load_hl(0x17B4);
    return z.final_jump(0x17B7);
}

// ---- Registration ----------------------------------------------------------

// The maxima apply only if the path cannot be estimated (a read of an unmapped
// address, whose value is the open bus); the live body renders such a read
// exactly. Each is the cost of the longest path of its segment.
void add_segment(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit, std::vector<SegmentExit> exits,
                 bool slot2_mapper, unsigned scratch_stack, std::uint64_t maximum, auto logic) {
    k::add_segment(registry,entry,k::segment(unit,std::move(exits),{.slot2_mapper=slot2_mapper,.scratch_stack=scratch_stack}),
                   maximum,logic);
}

// The reward as the option scales it: nothing stays nothing, any other reward
// is at least 1 and at most 65,535, rounded down.
std::uint16_t scale(std::uint16_t reward, unsigned percent) {
    if (!reward) return 0;
    const auto scaled=static_cast<unsigned long long>(reward)*percent/100;
    return static_cast<std::uint16_t>(std::clamp<unsigned long long>(scaled,1,0xffff));
}
// HL as the option scales it, written back into the register pair.
template<class Z> void scale_hl(Z& z, unsigned percent) {
    const auto value=scale(z.hl(),percent);
    z.r.h=static_cast<std::uint8_t>(value>>8);
    z.r.l=static_cast<std::uint8_t>(value);
}
// The two single instructions that read the reward, as the option scales it.
// With the option at its default each is the original instruction; with it on
// the same instruction is rendered between the scaling steps (the option
// changes values, not the instructions or their timing). `scaled` renders the
// instruction with the option's adjustment around it.
void add_option_instruction(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit, unsigned cycles,
                            unsigned percent, auto rendered, auto scaled) {
    auto spec=k::segment(unit,{k::core_final_at(entry.address)});
    spec.single_instruction=true;
    // The instruction costs the same whatever it reads, so the bound is fixed.
    const auto bound=[cycles](const Machine&) -> std::uint64_t { return cycles; };
    if (percent==experience_percent_choices.original) k::add_segment(registry,entry,std::move(spec),bound,rendered);
    else k::add_segment(registry,entry,std::move(spec),bound,[scaled,percent](auto& z) { return scaled(z,percent); });
}
}

void register_experience(RoutineRegistry& registry, const Options& options) {
    using E=SegmentExit;
    const auto ret=RoutineExit::returned, fin=RoutineExit::core_final;
    const auto percent=options.experience_percent;
    // 00:170D loads the reward the message shows. The option replaces the
    // loaded value; the stored reward is not touched.
    add_option_instruction(registry,experience_award_unit,experience_award_unit,16,percent,
        [](auto& z) { z.load_hl_direct(0x170D); return k::fall_through(0x170D,0x1710); },
        [](auto& z,unsigned percent) {
            z.load_hl_direct(0x170D);
            scale_hl(z,percent);
            return k::fall_through(0x170D,0x1710);
        });
    add_segment(registry,{0,0x1710},experience_award_unit,{E{ret,0x1715},E{fin,0x1719}},false,0,56,
        [](auto& z) { return award_message(z); });
    add_segment(registry,{0,0x171C},experience_award_unit,{E{fin,0x1727}},false,0,58,
        [](auto& z) { return first_record(z); });
    add_segment(registry,{0,0x172A},experience_award_unit,{E{fin,0x1736}},false,0,61,
        [](auto& z) { return middle_record(z,0x172A); });
    add_segment(registry,{0,0x1739},experience_award_unit,{E{fin,0x1745}},false,0,61,
        [](auto& z) { return middle_record(z,0x1739); });
    add_segment(registry,{0,0x1748},experience_award_unit,{E{ret,0x1758},E{fin,0x1770}},true,2,238,
        [](auto& z) { return last_record(z); });
    // Per-character helper.
    add_segment(registry,experience_helper_unit,experience_helper_unit,{E{ret,0x1758},E{fin,0x1770}},true,2,194,
        [](auto& z) { return helper_start(z); });
    // 00:1773 adds the reward (loaded at 1770) to the character's experience.
    // The option scales the reward first. When the sum overflows, the original
    // would store the cap and leave before the level test; with the option on
    // the carry is cleared after the addition, so the original code stores the
    // cap and goes on to the level test (maintainer decision, 2026-11-11).
    add_option_instruction(registry,experience_add_entry,experience_helper_unit,11,percent,
        [](auto& z) { z.add_to_hl(0x1773,z.de()); return k::fall_through(0x1773,0x1774); },
        [](auto& z,unsigned percent) {
            scale_hl(z,percent);
            z.add_to_hl(0x1773,z.de());
            if (z.r.f&k::carry_flag) {
                z.r.h=0xff;
                z.r.l=0xff;
                z.r.f=static_cast<std::uint8_t>(z.r.f&~k::carry_flag);
                z.r.q=z.r.f;
            }
            return k::fall_through(0x1773,0x1774);
        });
    add_segment(registry,{0,0x1774},experience_helper_unit,
        {E{ret,0x177F},E{ret,0x1785},E{ret,0x178A},E{ret,0x1791},E{fin,0x179A}},false,0,196,
        [](auto& z) { return helper_total(z); });
    add_segment(registry,{0,0x179D},experience_helper_unit,{E{ret,0x17B3},E{fin,0x17B7}},true,0,151,
        [](auto& z) { return helper_level(z); });
}
}
