// SPDX-License-Identifier: GPL-3.0-or-later
// The experience award after a battle, rewritten as segments (milestone 4,
// part D): the award itself (00:170D) and its per-character helper (00:1754).
// Every segment renders the original instructions from its entry for any
// machine state and ends with one instruction executed by the core: a return,
// or a call or jump into original code (the text routine runs natively and
// comes back to the next segment's entry), or a plain instruction.
//
// As in the encounter checks, each segment is written once as a sequence of
// instruction renderings over a `Bus`. The live bus performs the accesses
// through the routine context; the estimating bus performs the same sequence
// on a copy of memory to learn the exact cost of the path this machine will
// take, which is the segment's bound. The helper is one function used both by
// its own entry and, inline, by the segment that falls into it.
//
// The experience-rate option sits in the two single-instruction segments that
// read the reward: the load behind the displayed number and the addition for
// each character. Both run the original instruction on the core and adjust a
// register around it, only when the option is on.
#include "game/experience.h"
#include "machine/machine.h"
#include <algorithm>
#include <bit>
#include <utility>

namespace psmodern::game {
namespace {
constexpr std::uint8_t carry_flag=0x01, subtract_flag=0x02, parity_flag=0x04, x_flag=0x08, half_flag=0x10,
    y_flag=0x20, zero_flag=0x40, sign_flag=0x80;

struct Registers {
    std::uint8_t a, f, b, c, d, e, h, l;
    std::uint16_t ix, iy, sp;
};
Registers load(const Z80& cpu) {
    return {cpu.af.uint8_values.at_1,cpu.af.uint8_values.at_0,cpu.bc.uint8_values.at_1,cpu.bc.uint8_values.at_0,
        cpu.de.uint8_values.at_1,cpu.de.uint8_values.at_0,cpu.hl.uint8_values.at_1,cpu.hl.uint8_values.at_0,
        cpu.ix_iy[0].uint16_value,cpu.ix_iy[1].uint16_value,cpu.sp.uint16_value};
}
void store(Z80& cpu, const Registers& r) {
    cpu.af.uint8_values.at_1=r.a; cpu.af.uint8_values.at_0=r.f;
    cpu.bc.uint8_values.at_1=r.b; cpu.bc.uint8_values.at_0=r.c;
    cpu.de.uint8_values.at_1=r.d; cpu.de.uint8_values.at_0=r.e;
    cpu.hl.uint8_values.at_1=r.h; cpu.hl.uint8_values.at_0=r.l;
    cpu.ix_iy[0].uint16_value=r.ix; cpu.ix_iy[1].uint16_value=r.iy;
    cpu.sp.uint16_value=r.sp;
}
std::uint16_t pair(std::uint8_t high, std::uint8_t low) { return static_cast<std::uint16_t>((high<<8)|low); }

// The last instruction of a segment, executed by the core, and the cycles it
// takes on the path the segment found.
struct Exit { SegmentExit segment; unsigned cycles; };
Exit returns(std::uint16_t address, unsigned cycles) { return {{RoutineExit::returned,address},cycles}; }
Exit continues(std::uint16_t address, unsigned cycles) { return {{RoutineExit::core_final,address},cycles}; }
constexpr unsigned call_cycles=17, jump_cycles=10, load_hl_direct_cycles=16, taken_conditional_return_cycles=11;

std::uint8_t sign_zero_xy(std::uint8_t value) {
    return static_cast<std::uint8_t>((value&(sign_flag|y_flag|x_flag))|(value ? 0 : zero_flag));
}
std::uint8_t parity(std::uint8_t value) { return std::popcount(value)%2 ? 0 : parity_flag; }
// Subtraction and comparison of the accumulator with an operand. The
// comparison takes the undocumented bits from the operand, the subtraction
// from the result.
std::uint8_t subtract_flags(std::uint8_t a, std::uint8_t operand, std::uint8_t result) {
    return static_cast<std::uint8_t>(((a^operand^result)&half_flag)|(((a^operand)&(a^result)&0x80) ? parity_flag : 0)|
        subtract_flag|(a<operand ? carry_flag : 0));
}
void subtract8(Registers& r, std::uint8_t operand) {
    const auto result=static_cast<std::uint8_t>(r.a-operand);
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|subtract_flags(r.a,operand,result));
    r.a=result;
}
void compare8(Registers& r, std::uint8_t operand) {
    const auto result=static_cast<std::uint8_t>(r.a-operand);
    r.f=static_cast<std::uint8_t>((result&sign_flag)|(result ? 0 : zero_flag)|(operand&(y_flag|x_flag))|
        subtract_flags(r.a,operand,result));
}
void logic_flags(Registers& r, bool and_operation) {
    r.f=static_cast<std::uint8_t>(sign_zero_xy(r.a)|parity(r.a)|(and_operation ? half_flag : 0));
}
// 16-bit addition into HL: sign, zero and parity stay, the rest comes from the
// sum (undocumented bits and half carry from the high byte).
void add_hl(Registers& r, std::uint16_t operand) {
    const unsigned hl=pair(r.h,r.l), sum=hl+operand;
    r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|((sum>>8)&(y_flag|x_flag))|
        (((hl^operand^sum)>>8)&half_flag)|((sum>>16)&1));
    r.h=static_cast<std::uint8_t>(sum>>8); r.l=static_cast<std::uint8_t>(sum);
}

class Live {
public:
    explicit Live(RoutineContext& context) : context_(context) {}
    void fetch(std::uint16_t address) { (void)context_.fetch_opcode(address); }
    std::uint8_t read(std::uint16_t address) { return context_.read(address); }
    void write(std::uint16_t address, std::uint8_t value) { context_.write(address,value); }
    void advance(std::uint64_t cycles) { context_.advance(cycles); }
private:
    RoutineContext& context_;
};
// The same accesses on a copy of memory, counting cycles. A read of an
// unmapped address returns the bus value the real read would see only by
// accident, so the estimate is then not exact (the caller uses its maximum).
class Estimate {
public:
    explicit Estimate(const Machine& machine) : machine_(machine), memory_(machine.memory()) {}
    void fetch(std::uint16_t) {}
    std::uint8_t read(std::uint16_t address) {
        if (memory_.mapping(address).kind==Memory::Kind::unmapped) { uncertain=true; return machine_.peek(address); }
        return memory_.read(address);
    }
    void write(std::uint16_t address, std::uint8_t value) { memory_.write(address,value); }
    void advance(std::uint64_t cycles) { cycles_+=cycles; }
    std::uint64_t cycles() const { return cycles_; }
    bool uncertain=false;
private:
    const Machine& machine_;
    Memory memory_;
    std::uint64_t cycles_=0;
};

// The original instructions the award contains, each given the address it has
// in the ROM. Opcode fetches, operand reads and data accesses follow the bus
// order of the Z80; operands (addresses, constants, field displacements, the
// bank number) are read from the image, never written here. An index prefix
// and its opcode are two fetches; the displacement and, after a CB prefix, the
// final opcode byte are ordinary reads.
template<class Bus> class Instructions {
public:
    Instructions(Bus& bus, Registers& registers) : r(registers), bus_(bus) {}
    Registers& r;

    std::uint16_t hl() const { return pair(r.h,r.l); }
    std::uint16_t de() const { return pair(r.d,r.e); }

    void one_byte(std::uint16_t at, unsigned cycles) { bus_.fetch(at); bus_.advance(cycles); }
    void load_hl(std::uint16_t at) { load_pair(at,r.h,r.l); }
    void load_de(std::uint16_t at) { load_pair(at,r.d,r.e); }
    void load_iy(std::uint16_t at) { // ld iy,nn
        bus_.fetch(at); bus_.fetch(at+1); r.iy=operand_word(at+2); bus_.advance(14);
    }
    void store_to_hl(std::uint16_t at) { // ld (hl),n
        bus_.fetch(at); const auto value=operand(at+1); bus_.write(hl(),value); bus_.advance(10);
    }
    void load_immediate(std::uint16_t at, std::uint8_t& to) { bus_.fetch(at); to=operand(at+1); bus_.advance(7); }
    void copy(std::uint16_t at, std::uint8_t& to, std::uint8_t from) { one_byte(at,4); to=from; }
    void store_a_direct(std::uint16_t at) { // ld (nn),a
        bus_.fetch(at); const auto address=operand_word(at+1); bus_.write(address,r.a); bus_.advance(13);
    }
    void store_hl_direct(std::uint16_t at) { // ld (nn),hl: low byte first
        bus_.fetch(at); const auto address=operand_word(at+1);
        bus_.write(address,r.l); bus_.write(static_cast<std::uint16_t>(address+1),r.h); bus_.advance(16);
    }
    void compare_immediate(std::uint16_t at) { bus_.fetch(at); compare8(r,operand(at+1)); bus_.advance(7); }
    void or_register(std::uint16_t at, std::uint8_t value) { one_byte(at,4); r.a|=value; logic_flags(r,false); }
    void xor_a(std::uint16_t at) { one_byte(at,4); r.a=0; logic_flags(r,false); }
    void add_hl_hl(std::uint16_t at) { one_byte(at,11); add_hl(r,hl()); }
    void add_hl_de(std::uint16_t at) { one_byte(at,11); add_hl(r,de()); }

    // Index-register instructions: the displacement byte follows the prefix
    // and opcode (CB-prefixed ones: prefix, CB, displacement, opcode).
    void load_from_index(std::uint16_t at, std::uint16_t index, std::uint8_t& to) { // ld r,(ix/iy+d)
        bus_.fetch(at); bus_.fetch(at+1); to=bus_.read(indexed(index,at+2)); bus_.advance(19);
    }
    void store_to_index(std::uint16_t at, std::uint16_t index, std::uint8_t value) { // ld (ix/iy+d),r
        bus_.fetch(at); bus_.fetch(at+1); bus_.write(indexed(index,at+2),value); bus_.advance(19);
    }
    void compare_with_index(std::uint16_t at, std::uint16_t index) { // cp (ix/iy+d)
        bus_.fetch(at); bus_.fetch(at+1); compare8(r,bus_.read(indexed(index,at+2))); bus_.advance(19);
    }
    void subtract_index(std::uint16_t at, std::uint16_t index) { // sub (ix/iy+d)
        bus_.fetch(at); bus_.fetch(at+1); subtract8(r,bus_.read(indexed(index,at+2))); bus_.advance(19);
    }
    void increment_index(std::uint16_t at, std::uint16_t index) { // inc (ix/iy+d)
        bus_.fetch(at); bus_.fetch(at+1);
        const auto address=indexed(index,at+2), value=bus_.read(address), result=static_cast<std::uint8_t>(value+1);
        bus_.write(address,result);
        r.f=static_cast<std::uint8_t>((r.f&carry_flag)|sign_zero_xy(result)|((value&0x0f)==0x0f ? half_flag : 0)|
            (value==0x7f ? parity_flag : 0));
        bus_.advance(23);
    }
    // bit 0,(iy+d): the zero flag says whether the bit is clear; the
    // undocumented bits come from the high byte of the address.
    bool test_bit_zero(std::uint16_t at, std::uint16_t index) {
        bus_.fetch(at); bus_.fetch(at+1);
        const auto address=indexed(index,at+2);
        (void)operand(at+3);
        const bool clear=!(bus_.read(address)&1);
        r.f=static_cast<std::uint8_t>((r.f&carry_flag)|half_flag|(clear ? zero_flag|parity_flag : 0)|((address>>8)&(y_flag|x_flag)));
        bus_.advance(20);
        return clear;
    }
    // push hl, then pop ix: the stack traffic goes through the bus.
    void push_hl(std::uint16_t at) {
        one_byte(at,11);
        r.sp=static_cast<std::uint16_t>(r.sp-1); bus_.write(r.sp,r.h);
        r.sp=static_cast<std::uint16_t>(r.sp-1); bus_.write(r.sp,r.l);
    }
    void pop_ix(std::uint16_t at) {
        bus_.fetch(at); bus_.fetch(at+1);
        const auto low=bus_.read(r.sp); r.sp=static_cast<std::uint16_t>(r.sp+1);
        const auto high=bus_.read(r.sp); r.sp=static_cast<std::uint16_t>(r.sp+1);
        r.ix=pair(high,low); bus_.advance(14);
    }
    // Conditional relative jumps taken inside the segment; they report whether
    // the jump was taken.
    bool relative_if_no_carry(std::uint16_t at) { return relative(at,!(r.f&carry_flag)); }
    bool relative_if_not_zero(std::uint16_t at) { return relative(at,!(r.f&zero_flag)); }
    // A conditional return that is not taken (the taken one ends the segment
    // and is executed by the core).
    void return_not_taken(std::uint16_t at) { one_byte(at,5); }

private:
    std::uint8_t operand(unsigned at) { return bus_.read(static_cast<std::uint16_t>(at)); }
    std::uint16_t operand_word(unsigned at) {
        const auto low=operand(at), high=operand(at+1);
        return pair(high,low);
    }
    std::uint16_t indexed(std::uint16_t index, unsigned displacement_at) {
        return static_cast<std::uint16_t>(index+static_cast<std::int8_t>(operand(displacement_at)));
    }
    void load_pair(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {
        bus_.fetch(at); const auto value=operand_word(at+1);
        high=static_cast<std::uint8_t>(value>>8); low=static_cast<std::uint8_t>(value); bus_.advance(10);
    }
    bool relative(std::uint16_t at, bool taken) { bus_.fetch(at); (void)operand(at+1); bus_.advance(taken ? 12 : 7); return taken; }
    Bus& bus_;
};

// ---- The award -------------------------------------------------------------

// 00:1710-1719: the displayed number is stored; a zero reward ends the award;
// otherwise the reward message is shown (the text routine runs natively).
template<class Bus> Exit award_message(Instructions<Bus>& z) {
    auto& r=z.r;
    z.store_hl_direct(0x1710);
    z.copy(0x1713,r.a,r.l);
    z.or_register(0x1714,r.h);
    if (r.f&zero_flag) return returns(0x1715,taken_conditional_return_cycles);
    z.return_not_taken(0x1715);
    z.load_hl(0x1716);
    return continues(0x1719,call_cycles);
}
// The four record setups: iy and de name the character's record and level
// table, the character number is stored (the first by clearing the
// accumulator), and the helper follows.
template<class Bus> void record_setup(Instructions<Bus>& z, std::uint16_t at, bool first) {
    auto& r=z.r;
    z.load_iy(at);
    z.load_de(at+4);
    if (first) { z.xor_a(at+7); z.store_a_direct(at+8); }
    else { z.load_immediate(at+7,r.a); z.store_a_direct(at+9); }
}
// 00:171C: the first character, then a call of the helper.
template<class Bus> Exit first_record(Instructions<Bus>& z) {
    record_setup(z,0x171C,true);
    return continues(0x1727,call_cycles);
}
// 00:172A and 00:1739: the second and third characters.
template<class Bus> Exit middle_record(Instructions<Bus>& z, std::uint16_t at) {
    record_setup(z,at,false);
    return continues(static_cast<std::uint16_t>(at+12),call_cycles);
}

// ---- The helper, one character ------------------------------------------------

// 00:1754-1770: a character who is not eligible ends the helper; otherwise
// the level table row for the current level is found (slot 2 is set to the
// table bank) and the character's experience loaded. The reward load at 1770
// is the final instruction, executed by the core.
template<class Bus> Exit helper_start(Instructions<Bus>& z) {
    auto& r=z.r;
    if (z.test_bit_zero(0x1754,r.iy)) return returns(0x1758,taken_conditional_return_cycles);
    z.return_not_taken(0x1758);
    z.load_hl(0x1759);
    z.store_to_hl(0x175C);                 // slot 2 bank register
    z.load_from_index(0x175E,r.iy,r.l);
    z.load_immediate(0x1761,r.h);
    z.add_hl_hl(0x1763); z.add_hl_hl(0x1764); z.add_hl_hl(0x1765);
    z.add_hl_de(0x1766);
    z.push_hl(0x1767);
    z.pop_ix(0x1768);
    z.load_from_index(0x176A,r.iy,r.e);
    z.load_from_index(0x176D,r.iy,r.d);
    return continues(0x1770,load_hl_direct_cycles);
}
// 00:1748: the fourth character's setup, then the helper.
template<class Bus> Exit last_record(Instructions<Bus>& z) {
    record_setup(z,0x1748,false);
    return helper_start(z);
}

// 00:1774-179A: the new total is stored (the overflow of the addition saturates
// it); an overflow, level 30 or too little experience end the helper;
// otherwise the level-up sound is requested and the message shown.
template<class Bus> Exit helper_total(Instructions<Bus>& z) {
    auto& r=z.r;
    if (!z.relative_if_no_carry(0x1774)) z.load_hl(0x1776);
    z.store_to_index(0x1779,r.iy,r.l);
    z.store_to_index(0x177C,r.iy,r.h);
    if (r.f&carry_flag) return returns(0x177F,taken_conditional_return_cycles);
    z.return_not_taken(0x177F);
    z.load_from_index(0x1780,r.iy,r.a);
    z.compare_immediate(0x1783);
    if (r.f&zero_flag) return returns(0x1785,taken_conditional_return_cycles);
    z.return_not_taken(0x1785);
    z.copy(0x1786,r.a,r.h);
    z.subtract_index(0x1787,r.ix);
    if (r.f&carry_flag) return returns(0x178A,taken_conditional_return_cycles);
    z.return_not_taken(0x178A);
    if (!z.relative_if_not_zero(0x178B)) {
        z.copy(0x178D,r.a,r.l);
        z.subtract_index(0x178E,r.ix);
        if (r.f&carry_flag) return returns(0x1791,taken_conditional_return_cycles);
        z.return_not_taken(0x1791);
    }
    z.load_immediate(0x1792,r.a);
    z.store_a_direct(0x1794);              // the sound request
    z.load_hl(0x1797);
    return continues(0x179A,call_cycles);
}

// 00:179D-17B7: the level is incremented (slot 2 is set to the table bank
// first); the magic numbers of the new level are compared with the character's
// and, when either differs, the second message is shown (a jump to the text
// routine, which returns to the award's caller).
template<class Bus> Exit helper_level(Instructions<Bus>& z) {
    auto& r=z.r;
    z.load_hl(0x179D);
    z.store_to_hl(0x17A0);                 // slot 2 bank register
    z.increment_index(0x17A2,r.iy);
    z.load_from_index(0x17A5,r.ix,r.a);
    z.compare_with_index(0x17A8,r.iy);
    if (!z.relative_if_not_zero(0x17AB)) {
        z.load_from_index(0x17AD,r.ix,r.a);
        z.compare_with_index(0x17B0,r.iy);
        if (r.f&zero_flag) return returns(0x17B3,taken_conditional_return_cycles);
        z.return_not_taken(0x17B3);
    }
    z.load_hl(0x17B4);
    return continues(0x17B7,jump_cycles);
}

// ---- Registration ----------------------------------------------------------

// The exact cost of the path this machine will take: the segment run on a copy
// of memory. When it read an unmapped address the path is not known, and the
// longest path of the segment is used instead.
template<class Logic> std::uint64_t cost(const Machine& machine, const Logic& logic, std::uint64_t maximum) {
    Estimate bus(machine);
    auto registers=load(machine.cpu());
    Instructions<Estimate> instructions(bus,registers);
    const auto exit=logic(instructions);
    return bus.uncertain ? maximum : bus.cycles()+exit.cycles;
}
template<class Logic> void run(RoutineContext& context, const Logic& logic) {
    auto& cpu=context.cpu();
    Live bus(context);
    auto registers=load(cpu);
    Instructions<Live> instructions(bus,registers);
    const auto exit=logic(instructions);
    store(cpu,registers);
    if (exit.segment.kind==RoutineExit::returned) context.finish_return(exit.segment.address);
    else context.finish_core(exit.segment.address);
}
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit,
                                       std::vector<SegmentExit> exits, bool slot2_mapper, unsigned scratch_stack,
                                       std::uint64_t maximum, Logic logic) {
    registry.add(entry,SegmentSpec{unit,std::move(exits),slot2_mapper,scratch_stack},
        [logic,maximum](const Machine& machine) { return cost(machine,logic,maximum); },
        [logic](RoutineContext& context) { run(context,logic); });
}

// The reward as the option scales it: nothing stays nothing, any other reward
// is at least 1 and at most 65,535, rounded down.
std::uint16_t scale(std::uint16_t reward, unsigned percent) {
    if (!reward) return 0;
    const auto scaled=static_cast<unsigned long long>(reward)*percent/100;
    return static_cast<std::uint16_t>(std::clamp<unsigned long long>(scaled,1,0xffff));
}

// A single-instruction segment holding the original instruction that reads the
// reward. With the option at its default it is that instruction, executed by
// the core. Otherwise admission is forced (the segment adds no time of its
// own) and the adjustments run around the core's step: `before` on the
// registers the instruction will read, `after` on what it left.
template<class Before, class After>
void add_option_instruction(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit, unsigned cycles,
                            unsigned percent, Before before, After after) {
    const bool scaled=percent!=experience_percent_choices.original;
    SegmentSpec spec{unit,{{RoutineExit::core_final,entry.address}},false,0,true,scaled};
    registry.add(entry,std::move(spec),[cycles](const Machine&) { return cycles; },
        [entry,percent,scaled,before,after](RoutineContext& context) {
            if (!scaled) { context.finish_core(entry.address); return; }
            before(context.cpu(),percent);
            context.finish_core(entry.address);
            after(context.cpu(),percent);
        });
}
}

void register_experience(RoutineRegistry& registry, const Options& options) {
    using E=SegmentExit;
    const auto ret=RoutineExit::returned, fin=RoutineExit::core_final;
    const auto percent=options.experience_percent;
    // 00:170D loads the reward the message shows. The option replaces the
    // loaded value; the stored reward is not touched.
    add_option_instruction(registry,experience_award_unit,experience_award_unit,load_hl_direct_cycles,percent,
        [](Z80&, unsigned) {},
        [](Z80& cpu, unsigned percent) { cpu.hl.uint16_value=scale(cpu.hl.uint16_value,percent); });
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
        [](Z80& cpu, unsigned percent) { cpu.hl.uint16_value=scale(cpu.hl.uint16_value,percent); },
        [](Z80& cpu, unsigned) {
            auto& flags=cpu.af.uint8_values.at_0;
            if (flags&carry_flag) { cpu.hl.uint16_value=0xffff; flags=static_cast<std::uint8_t>(flags&~carry_flag); }
        });
    add_segment(registry,{0,0x1774},experience_helper_unit,
        {E{ret,0x177F},E{ret,0x1785},E{ret,0x178A},E{ret,0x1791},E{fin,0x179A}},false,0,196,
        [](auto& z) { return helper_total(z); });
    add_segment(registry,{0,0x179D},experience_helper_unit,{E{ret,0x17B3},E{fin,0x17B7}},true,0,151,
        [](auto& z) { return helper_level(z); });
}
}
