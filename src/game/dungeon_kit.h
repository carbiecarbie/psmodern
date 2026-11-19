// SPDX-License-Identifier: GPL-3.0-or-later
// Shared rendering kit of the milestone 7 dungeon segments (dungeon_cells.cpp,
// dungeon_movement.cpp, dungeon_objects.cpp). It follows the pattern each
// milestone 4-6 file keeps privately (encounters.cpp): a segment is written
// once as a sequence of original instructions over a `Bus`; the live bus
// performs the accesses through the routine context, the estimating bus runs
// the same sequence without touching the machine to learn the exact cost of
// the path this machine state will take, which is the segment's bound.
//
// Every rendering follows the pinned core (third_party/redcode-z80): bus order
// of opcode fetches, operand reads and data accesses, cycles, flags including
// the undocumented bits, MEMPTR and Q. Operands (addresses, constants, the bank
// number) are read from the player's image, never written here.
//
// Q (milestone 14): the routine context clears the body's Q at every opcode
// fetch; an instruction that writes the flags sets Q = F after computing them
// (the flag helpers below do it, and every instruction writing r.f inline
// does it too). An instruction that loads F without computing it (POP AF,
// EX AF,AF') leaves Q at 0, as the core does.
//
// Final instructions (milestone 14 Part 0b): a segment ends with one of the
// final_* renderers of Instructions (or a plain instruction followed by
// fall_through), which renders the last instruction like the others and
// returns the exit with the next PC. Conditions are condition codes
// (Condition), evaluated here from the flags. Each final renderer declares
// the opcode pattern it renders; the tests check it against the ROM every
// time it runs (Machine::set_opcode_check), the program never does.
#pragma once
#include "game/dungeon.h"
#include "machine/machine.h"
#include <bit>
#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace psmodern::game::kit {
inline constexpr std::uint8_t carry_flag=0x01, subtract_flag=0x02, parity_flag=0x04, x_flag=0x08, half_flag=0x10,
    y_flag=0x20, zero_flag=0x40, sign_flag=0x80;

// The registers a segment works on. SP is the body's view of the stack
// pointer: pushes and pops rendered here move it and go through the bus, so a
// segment may pop words an earlier segment pushed (inherited_stack) and end
// with words it pushed still held (a negative exit offset); run() stores SP
// back before the final instruction. wz is MEMPTR, q the flag latch Q (see
// above), af_ to hl_ the alternate registers (EXX, EX AF,AF').
struct Registers {
    std::uint8_t a, f, b, c, d, e, h, l; std::uint16_t sp, wz;
    std::uint8_t q=0; std::uint16_t af_=0, bc_=0, de_=0, hl_=0;
};
inline Registers load(const Z80& cpu) {
    return {cpu.af.uint8_values.at_1,cpu.af.uint8_values.at_0,cpu.bc.uint8_values.at_1,cpu.bc.uint8_values.at_0,
        cpu.de.uint8_values.at_1,cpu.de.uint8_values.at_0,cpu.hl.uint8_values.at_1,cpu.hl.uint8_values.at_0,
        cpu.sp.uint16_value,cpu.memptr.uint16_value,cpu.q,
        cpu.af_.uint16_value,cpu.bc_.uint16_value,cpu.de_.uint16_value,cpu.hl_.uint16_value};
}
inline void store(Z80& cpu, const Registers& r) {
    cpu.af.uint8_values.at_1=r.a; cpu.af.uint8_values.at_0=r.f;
    cpu.bc.uint8_values.at_1=r.b; cpu.bc.uint8_values.at_0=r.c;
    cpu.de.uint8_values.at_1=r.d; cpu.de.uint8_values.at_0=r.e;
    cpu.hl.uint8_values.at_1=r.h; cpu.hl.uint8_values.at_0=r.l;
    cpu.sp.uint16_value=r.sp; cpu.memptr.uint16_value=r.wz; cpu.q=r.q;
    cpu.af_.uint16_value=r.af_; cpu.bc_.uint16_value=r.bc_; cpu.de_.uint16_value=r.de_; cpu.hl_.uint16_value=r.hl_;
}
// The condition of a conditional instruction (JP cc, JR cc, CALL cc, RET cc),
// in the order of the opcode's condition field. JR takes nz, z, nc and c only.
enum class Condition : std::uint8_t { nz, z, nc, c, po, pe, p, m };
constexpr bool holds(std::uint8_t f, Condition cc) {
    constexpr std::uint8_t masks[]{0x40,0x01,0x04,0x80}; // zero, carry, parity, sign
    const auto index=static_cast<unsigned>(cc);
    const bool set=(f&masks[index/2])!=0;
    return (index&1) ? set : !set;
}
constexpr std::uint16_t pair(std::uint8_t high, std::uint8_t low) { return static_cast<std::uint16_t>((high<<8)|low); }
constexpr std::uint8_t high_byte(std::uint16_t value) { return static_cast<std::uint8_t>(value>>8); }
constexpr std::uint8_t low_byte(std::uint16_t value) { return static_cast<std::uint8_t>(value); }

// ---- Flags ------------------------------------------------------------------
inline std::uint8_t sign_zero_xy(std::uint8_t value) {
    return static_cast<std::uint8_t>((value&(sign_flag|y_flag|x_flag))|(value ? 0 : zero_flag));
}
inline std::uint8_t parity(std::uint8_t value) { return std::popcount(value)%2 ? 0 : parity_flag; }
// add/adc a: carry is 0 or 1.
inline void add8(Registers& r, std::uint8_t operand, unsigned carry) {
    const unsigned sum=r.a+operand+carry;
    const auto result=static_cast<std::uint8_t>(sum);
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|((r.a^operand^result)&half_flag)|
        ((~(r.a^operand)&(r.a^result)&0x80) ? parity_flag : 0)|(sum>0xff ? carry_flag : 0));
    r.q=r.f;
    r.a=result;
}
// The flags of a - operand - borrow; the result is returned (sub, sbc, neg).
inline std::uint8_t subtract_flags(Registers& r, std::uint8_t minuend, std::uint8_t operand, unsigned borrow) {
    const unsigned difference=static_cast<unsigned>(minuend)-operand-borrow;
    const auto result=static_cast<std::uint8_t>(difference);
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|((minuend^operand^result)&half_flag)|
        (((minuend^operand)&(minuend^result)&0x80) ? parity_flag : 0)|subtract_flag|(difference>0xff ? carry_flag : 0));
    r.q=r.f;
    return result;
}
// cp: as sub, but the undocumented bits come from the operand.
inline void compare8(Registers& r, std::uint8_t operand) {
    (void)subtract_flags(r,r.a,operand,0);
    r.f=static_cast<std::uint8_t>((r.f&~(y_flag|x_flag))|(operand&(y_flag|x_flag)));
    r.q=r.f;
}
inline void logic_flags(Registers& r, bool and_operation) {
    r.f=static_cast<std::uint8_t>(sign_zero_xy(r.a)|parity(r.a)|(and_operation ? half_flag : 0));
    r.q=r.f;
}
inline std::uint8_t increment8(Registers& r, std::uint8_t value) {
    const auto result=static_cast<std::uint8_t>(value+1);
    r.f=static_cast<std::uint8_t>((r.f&carry_flag)|sign_zero_xy(result)|((result&0x0f)==0 ? half_flag : 0)|
        (result==0x80 ? parity_flag : 0));
    r.q=r.f;
    return result;
}
inline std::uint8_t decrement8(Registers& r, std::uint8_t value) {
    const auto result=static_cast<std::uint8_t>(value-1);
    r.f=static_cast<std::uint8_t>((r.f&carry_flag)|sign_zero_xy(result)|((result&0x0f)==0x0f ? half_flag : 0)|
        (result==0x7f ? parity_flag : 0)|subtract_flag);
    r.q=r.f;
    return result;
}
// Shift or rotate right of one byte (CB group); the bit shifted out goes to
// carry and the bit shifted in is given.
inline std::uint8_t shift_right(Registers& r, std::uint8_t value, unsigned entering) {
    const auto result=static_cast<std::uint8_t>((value>>1)|(entering<<7));
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|parity(result)|(value&1));
    r.q=r.f;
    return result;
}
// 16-bit addition into HL: sign, zero and parity stay, the rest comes from the
// sum (undocumented bits and half carry from the high byte); MEMPTR = HL + 1.
inline void add_hl(Registers& r, std::uint16_t operand) {
    const unsigned hl=pair(r.h,r.l), sum=hl+operand;
    r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|((sum>>8)&(y_flag|x_flag))|
        (((hl^operand^sum)>>8)&half_flag)|((sum>>16)&1));
    r.q=r.f;
    r.wz=static_cast<std::uint16_t>(hl+1);
    r.h=high_byte(static_cast<std::uint16_t>(sum)); r.l=low_byte(static_cast<std::uint16_t>(sum));
}
// bit n: S only for a set bit 7, Z and P/V for a clear bit, H set, C kept; the
// undocumented bits come from `xy` (the register, or MEMPTR's high byte).
inline void bit_flags(Registers& r, unsigned bit, std::uint8_t value, std::uint8_t xy) {
    const auto tested=static_cast<std::uint8_t>(value&(1u<<bit));
    r.f=static_cast<std::uint8_t>((tested ? (tested&sign_flag) : (zero_flag|parity_flag))|(xy&(y_flag|x_flag))|
        half_flag|(r.f&carry_flag));
    r.q=r.f;
}

// ---- Buses ------------------------------------------------------------------
// One bus per way of running a segment: fetch (opcode read and refresh), read,
// write, time, and the cycles elapsed since the segment's entry.
// Milestone 14: expect (the declared opcode pattern of a renderer, for the
// test-side check), interrupts (EI/DI), return_from_nmi (RETN's IFF rule) and
// guard (an explicit in-body guard: the live run stops with GuardFailure, the
// estimate becomes uncertain, so that the segment refuses the state at its entry).
class Live {
public:
    explicit Live(RoutineContext& context) : context_(context) {}
    void fetch(std::uint16_t address) { (void)context_.fetch_opcode(address); }
    std::uint8_t read(std::uint16_t address) { return context_.read(address); }
    void write(std::uint16_t address, std::uint8_t value) { context_.write(address,value); }
    void advance(std::uint64_t cycles) { context_.advance(cycles); }
    std::uint64_t elapsed() const { return context_.elapsed(); }
    void expect(std::uint16_t address, OpcodePattern pattern) { context_.expect_opcode(address,pattern); }
    void interrupts(bool enable) { context_.set_interrupts(enable); }
    void return_from_nmi() { context_.return_from_nmi(); }
    bool guard(bool holds, std::string_view what) { if (!holds) context_.guard_failed(what); return true; }
    // State no kit register holds, live in the register file: the refresh
    // counter (every opcode fetch counts into it), IFF2 (changed only by EI,
    // DI, RETN and an interrupt response, never inside a body) and the
    // interrupt mode.
    std::uint8_t refresh() { return context_.cpu().r; }
    // The previous instruction's Q at this instruction's fetch (CCF, SCF).
    std::uint8_t previous_q() const { return context_.previous_q(); }
    bool iff2() { return context_.cpu().iff2!=0; }
    void interrupt_mode(std::uint8_t mode) { context_.cpu().im=mode; }
private:
    RoutineContext& context_;
};
// The same accesses without touching the machine, counting cycles. Reads come
// from the machine's memory until the first write, which makes a private copy
// of the memory (so a bank switch is followed too); a read-only path such as a
// table scan copies nothing. A read of an unmapped address returns the bus
// value the real read would see only by accident, so the estimate is then not
// exact (the caller uses its maximum).
class Estimate {
public:
    explicit Estimate(const Machine& machine) : machine_(machine) {}
    void fetch(std::uint16_t) {}
    std::uint8_t read(std::uint16_t address) {
        const Memory& memory=copy_ ? *copy_ : machine_.memory();
        if (memory.mapping(address).kind==Memory::Kind::unmapped) { uncertain=true; return machine_.peek(address); }
        return memory.read(address);
    }
    void write(std::uint16_t address, std::uint8_t value) {
        if (!copy_) copy_.emplace(machine_.memory());
        copy_->write(address,value);
    }
    void advance(std::uint64_t cycles) { cycles_+=cycles; }
    std::uint64_t elapsed() const { return cycles_; }
    std::uint64_t cycles() const { return cycles_; }
    void expect(std::uint16_t, OpcodePattern) {}
    void interrupts(bool) {}
    void return_from_nmi() {}
    bool guard(bool holds, std::string_view) { if (!holds) uncertain=true; return holds; }
    // Neither changes a path or its cost.
    std::uint8_t refresh() const { return 0; }
    std::uint8_t previous_q() const { return 0; }
    bool iff2() const { return false; }
    void interrupt_mode(std::uint8_t) {}
    bool uncertain=false;
private:
    const Machine& machine_;
    std::optional<Memory> copy_;
    std::uint64_t cycles_=0;
};

// ---- The exit a body takes -------------------------------------------------------
// A body names the exit by kind and address only; the mechanism applies the
// declaration's stack offset, conditional flag and loop threshold.
// - Rendered final (milestone 14): `next` is the PC the machine continues at
//   and `cycles` is 0 (the final's cycles were rendered like any other
//   instruction's). Made by the final_* renderers of Instructions and by
//   fall_through.
// Before returning, the body must leave SP (Registers::sp) at the entry SP plus
// the declared offset: pushes and pops rendered with Instructions move it.
struct Exit { SegmentExit segment; unsigned cycles; std::optional<std::uint16_t> next{}; };
// A plain final instruction (no transfer of control): the body renders it with
// the ordinary instruction method, then ends with
//   return kit::fall_through(0x3451,0x3454);   // LD BC,nn at 3451; next segment at 3454
// `final_at` is the declared exit (the address of that last instruction) and
// `next` the address after it.
inline Exit fall_through(std::uint16_t final_at, std::uint16_t next) {
    return {{RoutineExit::core_final,final_at},0,next};
}
// The exit of a path an explicit in-body guard refused. The live run never
// returns it (the guard has thrown GuardFailure); only the estimating run
// reaches it, and the segment's cost is then its maximum.
inline Exit refused() { return {}; }

// ---- Instructions -------------------------------------------------------------
// The original instructions of the dungeon code, each given the address it has
// in the ROM. Register operands are passed by reference to fields of `r`.
template<class Bus> class Instructions {
public:
    Instructions(Bus& bus, Registers& registers) : r(registers), bus_(bus) {}
    Registers& r;

    std::uint16_t hl() const { return pair(r.h,r.l); }
    std::uint16_t de() const { return pair(r.d,r.e); }
    std::uint16_t bc() const { return pair(r.b,r.c); }
    void set_hl(std::uint16_t v) { r.h=high_byte(v); r.l=low_byte(v); }
    void set_de(std::uint16_t v) { r.d=high_byte(v); r.e=low_byte(v); }
    void set_bc(std::uint16_t v) { r.b=high_byte(v); r.c=low_byte(v); }
    bool zero() const { return r.f&zero_flag; }
    bool carry() const { return r.f&carry_flag; }
    bool sign() const { return r.f&sign_flag; }
    bool parity_even() const { return r.f&parity_flag; }
    // Cycles since the segment's entry, and the loop-exit rule of a split
    // scan: at an arrival at a loop exit declared with `after`, the segment
    // ends there exactly when this is true (the mechanism refuses an earlier
    // exit, and a later one diverges from the original).
    std::uint64_t elapsed() const { return bus_.elapsed(); }
    bool loop_exit(std::uint16_t after) const { return bus_.elapsed()>=after; }
    // Direct bus access for state the instructions do not cover (tests).
    Bus& bus() { return bus_; }

    // -- 8-bit loads
    // Every instruction method declares the opcode pattern it renders to the
    // test-side check (review R2 M1): exact where the operands name registers
    // of `r`, the instruction's family where an operand is passed by value.
    // one_byte is the plain fetch the methods share; it declares nothing.
    void one_byte(std::uint16_t at, unsigned cycles) { bus_.fetch(at); bus_.advance(cycles); }
    void copy(std::uint16_t at, std::uint8_t& to, std::uint8_t from) {                                     // ld r,r'
        const int d=field(to);
        bus_.expect(at,d<0 ? op(0x40,0xC0) : op(static_cast<std::uint8_t>(0x40|(d<<3)),0xF8));
        one_byte(at,4); to=from;
    }
    void load_immediate(std::uint16_t at, std::uint8_t& to) {                                              // ld r,n
        bus_.expect(at,with_field(0,0x06,3,to)); bus_.fetch(at); to=operand(at+1); bus_.advance(7);
    }
    void load_from_hl(std::uint16_t at, std::uint8_t& to) {                                                // ld r,(hl)
        bus_.expect(at,with_field(0,0x46,3,to)); bus_.fetch(at); to=bus_.read(hl()); bus_.advance(7);
    }
    void store_register_to_hl(std::uint16_t at, std::uint8_t value) {                                      // ld (hl),r
        bus_.expect(at,op(0x70,0xF8)); bus_.fetch(at); bus_.write(hl(),value); bus_.advance(7);
    }
    void store_to_hl(std::uint16_t at) {                                                                     // ld (hl),n
        bus_.expect(at,op(0x36));
        bus_.fetch(at); const auto value=operand(at+1); bus_.write(hl(),value); bus_.advance(10);
    }
    void load_a_direct(std::uint16_t at) {                                                                   // ld a,(nn)
        bus_.expect(at,op(0x3A));
        bus_.fetch(at); const auto address=operand_word(at+1); r.a=bus_.read(address);
        r.wz=static_cast<std::uint16_t>(address+1); bus_.advance(13);
    }
    void store_a_direct(std::uint16_t at) {                                                                  // ld (nn),a
        bus_.expect(at,op(0x32));
        bus_.fetch(at); const auto address=operand_word(at+1);
        r.wz=pair(r.a,low_byte(static_cast<std::uint16_t>(address+1)));
        bus_.write(address,r.a); bus_.advance(13);
    }
    void load_a_from_pair(std::uint16_t at, std::uint16_t address) {                                         // ld a,(bc) / ld a,(de)
        bus_.expect(at,op(0x0A,0xEF));
        bus_.fetch(at); r.wz=static_cast<std::uint16_t>(address+1); r.a=bus_.read(address); bus_.advance(7);
    }
    void store_a_to_pair(std::uint16_t at, std::uint16_t address) {                                          // ld (bc),a / ld (de),a
        bus_.expect(at,op(0x02,0xEF));
        bus_.fetch(at); r.wz=pair(r.a,low_byte(static_cast<std::uint16_t>(address+1)));
        bus_.write(address,r.a); bus_.advance(7);
    }
    // -- 16-bit loads and stack
    void load_pair(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                               // ld rr,nn
        bus_.expect(at,with_pair(0,0x01,high));
        bus_.fetch(at); const auto value=operand_word(at+1); high=high_byte(value); low=low_byte(value); bus_.advance(10);
    }
    void load_hl(std::uint16_t at) { load_pair(at,r.h,r.l); }
    void load_de(std::uint16_t at) { load_pair(at,r.d,r.e); }
    void load_bc(std::uint16_t at) { load_pair(at,r.b,r.c); }
    void load_hl_direct(std::uint16_t at) {                                                                  // ld hl,(nn)
        bus_.expect(at,op(0x2A));
        bus_.fetch(at); const auto address=operand_word(at+1); r.wz=static_cast<std::uint16_t>(address+1);
        r.l=bus_.read(address); r.h=bus_.read(r.wz); bus_.advance(16);
    }
    void store_hl_direct(std::uint16_t at) {                                                                 // ld (nn),hl
        bus_.expect(at,op(0x22));
        bus_.fetch(at); const auto address=operand_word(at+1); r.wz=static_cast<std::uint16_t>(address+1);
        bus_.write(address,r.l); bus_.write(r.wz,r.h); bus_.advance(16);
    }
    void load_pair_direct(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                        // ld bc/de,(nn) (ED)
        bus_.expect(at,with_pair(0xED,0x4B,high));
        bus_.fetch(at); bus_.fetch(at+1); const auto address=operand_word(at+2); r.wz=static_cast<std::uint16_t>(address+1);
        low=bus_.read(address); high=bus_.read(r.wz); bus_.advance(20);
    }
    void store_pair_direct(std::uint16_t at, std::uint8_t high, std::uint8_t low) {                         // ld (nn),bc/de (ED)
        bus_.expect(at,OpcodePattern{0xED,0x43,0xCF});
        bus_.fetch(at); bus_.fetch(at+1); const auto address=operand_word(at+2); r.wz=static_cast<std::uint16_t>(address+1);
        bus_.write(address,low); bus_.write(r.wz,high); bus_.advance(20);
    }
    void increment_pair(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                          // inc rr
        bus_.expect(at,with_pair(0,0x03,high)); one_byte(at,6); const auto v=static_cast<std::uint16_t>(pair(high,low)+1); high=high_byte(v); low=low_byte(v);
    }
    void decrement_pair(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                          // dec rr
        bus_.expect(at,with_pair(0,0x0B,high)); one_byte(at,6); const auto v=static_cast<std::uint16_t>(pair(high,low)-1); high=high_byte(v); low=low_byte(v);
    }
    void increment_hl(std::uint16_t at) { increment_pair(at,r.h,r.l); }
    void decrement_hl(std::uint16_t at) { decrement_pair(at,r.h,r.l); }
    void exchange_de_hl(std::uint16_t at) { bus_.expect(at,op(0xEB)); one_byte(at,4); std::swap(r.d,r.h); std::swap(r.e,r.l); } // ex de,hl
    void push(std::uint16_t at, std::uint8_t high, std::uint8_t low) {                                      // push rr (high byte first)
        bus_.expect(at,op(0xC5,0xCF)); bus_.fetch(at); push_word(pair(high,low)); bus_.advance(11);
    }
    void pop(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {                                     // pop rr (low byte first)
        bus_.expect(at,with_pair(0,0xC1,high));
        bus_.fetch(at); const auto v=pop_word(); high=high_byte(v); low=low_byte(v); bus_.advance(10);
    }
    void add_to_hl(std::uint16_t at, std::uint16_t operand) { bus_.expect(at,op(0x09,0xCF)); one_byte(at,11); add_hl(r,operand); } // add hl,rr
    void add_hl_with_carry(std::uint16_t at, std::uint16_t operand) {                                       // adc hl,rr (ED)
        // Flags from the 16-bit sum (undocumented bits and half carry from the
        // high byte, overflow of the signed sum); MEMPTR = HL + 1.
        bus_.expect(at,OpcodePattern{0xED,0x4A,0xCF});
        bus_.fetch(at); bus_.fetch(at+1);
        const unsigned lhs=hl(), sum=lhs+operand+(r.f&carry_flag);
        const auto result=static_cast<std::uint16_t>(sum);
        r.f=static_cast<std::uint8_t>(((result>>8)&(sign_flag|y_flag|x_flag))|(result ? 0 : zero_flag)|
            (((lhs^operand^result)&0x1000) ? half_flag : 0)|((~(lhs^operand)&(lhs^result)&0x8000) ? parity_flag : 0)|
            (sum>0xffff ? carry_flag : 0));
        r.q=r.f;
        r.wz=static_cast<std::uint16_t>(lhs+1);
        set_hl(result);
        bus_.advance(15);
    }
    void load_a_from_refresh(std::uint16_t at) {                                                            // ld a,r (ED)
        // The refresh counter after both fetches; S, Z and the undocumented
        // bits from it, P/V from IFF2, H and N cleared, C kept.
        bus_.expect(at,OpcodePattern::prefixed(0xED,0x5F));
        bus_.fetch(at); bus_.fetch(at+1);
        r.a=bus_.refresh();
        r.f=static_cast<std::uint8_t>(sign_zero_xy(r.a)|(bus_.iff2() ? parity_flag : 0)|(r.f&carry_flag));
        r.q=r.f;
        bus_.advance(9);
    }
    void interrupt_mode_one(std::uint16_t at) {                                                             // im 1 (ED)
        bus_.expect(at,OpcodePattern::prefixed(0xED,0x56));
        bus_.fetch(at); bus_.fetch(at+1); bus_.interrupt_mode(1); bus_.advance(8);
    }

    // -- 8-bit arithmetic and logic
    void add_immediate(std::uint16_t at) { bus_.expect(at,op(0xC6)); bus_.fetch(at); add8(r,operand(at+1),0); bus_.advance(7); }
    void add_register(std::uint16_t at, std::uint8_t value) { bus_.expect(at,op(0x80,0xF8)); one_byte(at,4); add8(r,value,0); }
    void add_from_hl(std::uint16_t at) { bus_.expect(at,op(0x86)); bus_.fetch(at); const auto v=bus_.read(hl()); add8(r,v,0); bus_.advance(7); }
    void add_with_carry(std::uint16_t at, std::uint8_t value) { bus_.expect(at,op(0x88,0xF8)); one_byte(at,4); add8(r,value,r.f&carry_flag); }
    void add_with_carry_immediate(std::uint16_t at) {
        bus_.expect(at,op(0xCE)); bus_.fetch(at); add8(r,operand(at+1),r.f&carry_flag); bus_.advance(7);
    }
    void subtract_immediate(std::uint16_t at) {
        bus_.expect(at,op(0xD6)); bus_.fetch(at); r.a=subtract_flags(r,r.a,operand(at+1),0); bus_.advance(7);
    }
    void subtract_register(std::uint16_t at, std::uint8_t value) {
        bus_.expect(at,op(0x90,0xF8)); one_byte(at,4); r.a=subtract_flags(r,r.a,value,0);
    }
    void subtract_with_carry(std::uint16_t at, std::uint8_t value) {
        bus_.expect(at,op(0x98,0xF8)); one_byte(at,4); r.a=subtract_flags(r,r.a,value,r.f&carry_flag);
    }
    void compare_immediate(std::uint16_t at) { bus_.expect(at,op(0xFE)); bus_.fetch(at); compare8(r,operand(at+1)); bus_.advance(7); }
    void compare_register(std::uint16_t at, std::uint8_t value) { bus_.expect(at,op(0xB8,0xF8)); one_byte(at,4); compare8(r,value); }
    void compare_from_hl(std::uint16_t at) { bus_.expect(at,op(0xBE)); bus_.fetch(at); const auto v=bus_.read(hl()); compare8(r,v); bus_.advance(7); }
    void and_immediate(std::uint16_t at) { bus_.expect(at,op(0xE6)); bus_.fetch(at); r.a&=operand(at+1); logic_flags(r,true); bus_.advance(7); }
    void and_register(std::uint16_t at, std::uint8_t value) { bus_.expect(at,op(0xA0,0xF8)); one_byte(at,4); r.a&=value; logic_flags(r,true); }
    void or_immediate(std::uint16_t at) { bus_.expect(at,op(0xF6)); bus_.fetch(at); r.a|=operand(at+1); logic_flags(r,false); bus_.advance(7); }
    void or_register(std::uint16_t at, std::uint8_t value) { bus_.expect(at,op(0xB0,0xF8)); one_byte(at,4); r.a|=value; logic_flags(r,false); }
    void xor_register(std::uint16_t at, std::uint8_t value) { bus_.expect(at,op(0xA8,0xF8)); one_byte(at,4); r.a^=value; logic_flags(r,false); }
    void or_a(std::uint16_t at) { bus_.expect(at,op(0xB7)); or_register(at,r.a); }
    void xor_a(std::uint16_t at) { bus_.expect(at,op(0xAF)); xor_register(at,r.a); }
    void negate(std::uint16_t at) {                                                                        // neg
        bus_.expect(at,OpcodePattern::prefixed(0xED,0x44)); bus_.fetch(at); bus_.fetch(at+1); r.a=subtract_flags(r,0,r.a,0); bus_.advance(8);
    }
    void increment(std::uint16_t at, std::uint8_t& value) {                                                // inc r
        bus_.expect(at,with_field(0,0x04,3,value)); one_byte(at,4); value=increment8(r,value);
    }
    void decrement(std::uint16_t at, std::uint8_t& value) {                                                // dec r
        bus_.expect(at,with_field(0,0x05,3,value)); one_byte(at,4); value=decrement8(r,value);
    }
    void rotate_right_circular_a(std::uint16_t at) {                                                       // rrca
        bus_.expect(at,op(0x0F)); one_byte(at,4);
        r.a=static_cast<std::uint8_t>((r.a>>1)|(r.a<<7));
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|(r.a&(y_flag|x_flag))|(r.a>>7));
        r.q=r.f;
    }
    void rotate_right_a(std::uint16_t at) {                                                                // rra
        // S, Z and P/V kept, H and N cleared, the undocumented bits from the result.
        bus_.expect(at,op(0x1F)); one_byte(at,4);
        const auto out=static_cast<std::uint8_t>(r.a&1);
        r.a=static_cast<std::uint8_t>((r.a>>1)|((r.f&carry_flag)<<7));
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|(r.a&(y_flag|x_flag))|out);
        r.q=r.f;
    }
    void shift_right_logical(std::uint16_t at, std::uint8_t& value) {                                      // srl r
        bus_.expect(at,with_field(0xCB,0x38,0,value)); bus_.fetch(at); bus_.fetch(at+1); value=shift_right(r,value,0); bus_.advance(8);
    }
    void rotate_right_through_carry(std::uint16_t at, std::uint8_t& value) {                               // rr r
        bus_.expect(at,with_field(0xCB,0x18,0,value));
        bus_.fetch(at); bus_.fetch(at+1); value=shift_right(r,value,r.f&carry_flag); bus_.advance(8);
    }
    void test_bit(std::uint16_t at, unsigned bit, std::uint8_t value) {                                    // bit n,r
        bus_.expect(at,OpcodePattern{0xCB,static_cast<std::uint8_t>(0x40|((bit&7)<<3)),0xF8});
        bus_.fetch(at); bus_.fetch(at+1); bit_flags(r,bit,value,value); bus_.advance(8);
    }
    void test_bit_at_hl(std::uint16_t at, unsigned bit) {                                                  // bit n,(hl)
        bus_.expect(at,OpcodePattern::prefixed(0xCB,static_cast<std::uint8_t>(0x46|((bit&7)<<3))));
        bus_.fetch(at); bus_.fetch(at+1); const auto v=bus_.read(hl()); bit_flags(r,bit,v,high_byte(r.wz)); bus_.advance(12);
    }
    void set_bit_at_hl(std::uint16_t at, unsigned bit) {                                                   // set n,(hl)
        bus_.expect(at,OpcodePattern::prefixed(0xCB,static_cast<std::uint8_t>(0xC6|((bit&7)<<3))));
        bus_.fetch(at); bus_.fetch(at+1); const auto v=bus_.read(hl());
        bus_.write(hl(),static_cast<std::uint8_t>(v|(1u<<bit))); bus_.advance(15);
    }
    // ldir: every iteration fetches both opcode bytes again; a repeated
    // iteration takes its undocumented bits from the instruction's address and
    // leaves MEMPTR at address + 1, the last one takes them from A + the byte.
    void block_copy_byte(std::uint16_t at) {
        bus_.expect(at,OpcodePattern::prefixed(0xED,0xB0));
        bus_.fetch(at); bus_.fetch(at+1);
        const auto value=bus_.read(hl());
        bus_.write(de(),value);
        set_hl(static_cast<std::uint16_t>(hl()+1)); set_de(static_cast<std::uint16_t>(de()+1));
        set_bc(static_cast<std::uint16_t>(bc()-1));
        const auto keep=static_cast<std::uint8_t>(r.f&(sign_flag|zero_flag|carry_flag));
        if (bc()) {
            r.f=static_cast<std::uint8_t>(keep|(high_byte(at)&(y_flag|x_flag))|parity_flag);
            r.q=r.f;
            r.wz=static_cast<std::uint16_t>(at+1); bus_.advance(21);
            return;
        }
        const auto t=static_cast<std::uint8_t>(value+r.a);
        r.f=static_cast<std::uint8_t>(keep|((t&2)<<4)|(t&x_flag));
        r.q=r.f;
        bus_.advance(16);
    }
    void block_copy(std::uint16_t at) {
        do block_copy_byte(at); while (bc());
    }

    // -- Control transfers inside a segment (the operand is read either way)
    std::uint16_t jump(std::uint16_t at) {                                                                 // jp nn
        bus_.expect(at,op(0xC3)); bus_.fetch(at); r.wz=operand_word(at+1); bus_.advance(10); return r.wz;
    }
    bool jump_if(std::uint16_t at, bool taken) {                                                           // jp cc,nn
        bus_.expect(at,op(0xC2,0xC7)); bus_.fetch(at); r.wz=operand_word(at+1); bus_.advance(10); return taken;
    }
    void relative(std::uint16_t at) { bus_.expect(at,op(0x18)); (void)relative_run(at,true); }              // jr e
    bool relative_if(std::uint16_t at, bool taken) {                                                        // jr cc,e
        bus_.expect(at,op(0x20,0xE7)); return relative_run(at,taken);
    }
    bool decrement_jump(std::uint16_t at) {                                                                // djnz e
        bus_.expect(at,op(0x10));
        bus_.fetch(at); bus_.advance(1);
        r.b=static_cast<std::uint8_t>(r.b-1);
        const auto offset=static_cast<std::int8_t>(operand(at+1));
        const bool taken=r.b!=0;
        if (taken) r.wz=static_cast<std::uint16_t>(at+2+offset);
        bus_.advance(taken ? 12 : 7); return taken;
    }
    // A bare call or return rendered inline (the callee is part of the
    // segment, for example the cell readers): the return address goes through
    // the stack exactly as in the original.
    std::uint16_t call(std::uint16_t at) {                                                                 // call nn
        bus_.expect(at,op(0xCD));
        bus_.fetch(at); r.wz=operand_word(at+1);
        push_word(static_cast<std::uint16_t>(at+3)); bus_.advance(17); return r.wz;
    }
    bool call_if(std::uint16_t at, bool taken) {                                                           // call cc,nn
        bus_.expect(at,op(0xC4,0xC7));
        bus_.fetch(at); r.wz=operand_word(at+1);
        if (!taken) { bus_.advance(10); return false; }
        push_word(static_cast<std::uint16_t>(at+3)); bus_.advance(17); return true;
    }
    std::uint16_t return_from(std::uint16_t at) {                                                          // ret
        bus_.expect(at,op(0xC9)); bus_.fetch(at); r.wz=pop_word(); bus_.advance(10); return r.wz;
    }
    bool return_if(std::uint16_t at, bool taken) {                                                         // ret cc
        bus_.expect(at,op(0xC0,0xC7));
        bus_.fetch(at);
        if (!taken) { bus_.advance(5); return false; }
        r.wz=pop_word(); bus_.advance(11); return true;
    }
    // A conditional return that is not taken (the taken one ends the segment:
    // final_return_if).
    void return_not_taken(std::uint16_t at) { bus_.expect(at,op(0xC0,0xC7)); one_byte(at,5); }

    // -- Inline conditional transfers with a condition code (milestone 14):
    // the kit evaluates the flag and checks the opcode in the tests. Prefer
    // these to the boolean forms above in new and converted code.
    bool jump_if(std::uint16_t at, Condition cc) {                                                         // jp cc,nn
        bus_.expect(at,conditional(0xC2,cc)); return jump_if(at,holds(r.f,cc));
    }
    bool relative_if(std::uint16_t at, Condition cc) {                                                     // jr cc,e
        bus_.expect(at,relative_condition(cc)); return relative_if(at,holds(r.f,cc));
    }
    bool call_if(std::uint16_t at, Condition cc) {                                                         // call cc,nn
        bus_.expect(at,conditional(0xC4,cc)); return call_if(at,holds(r.f,cc));
    }
    bool return_if(std::uint16_t at, Condition cc) {                                                       // ret cc
        bus_.expect(at,conditional(0xC0,cc)); return return_if(at,holds(r.f,cc));
    }

    // ccf / scf: the undocumented bits are A's ORed with those of F XOR Q,
    // where Q is the previous instruction's (live state: 0 after an
    // interrupt accepted just before, F after a flag-writing instruction).
    // Q is the one the opcode fetch found (RoutineContext::previous_q): the
    // boundary before it can deliver a handler, after which the previous Q is
    // the handler's last instruction's (review R1 M-1).
    void complement_carry(std::uint16_t at) {                                                              // ccf
        bus_.expect(at,OpcodePattern::exactly(0x3F));
        bus_.fetch(at); const auto previous_q=bus_.previous_q(); bus_.advance(4);
        const auto carry=static_cast<std::uint8_t>(r.f&carry_flag);
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|(((r.f^previous_q)|r.a)&(y_flag|x_flag))|
            (carry ? half_flag : 0)|(carry ? 0 : carry_flag));
        r.q=r.f;
    }
    void set_carry(std::uint16_t at) {                                                                     // scf
        bus_.expect(at,OpcodePattern::exactly(0x37));
        bus_.fetch(at); const auto previous_q=bus_.previous_q(); bus_.advance(4);
        r.f=static_cast<std::uint8_t>((r.f&(sign_flag|zero_flag|parity_flag))|(((r.f^previous_q)|r.a)&(y_flag|x_flag))|carry_flag);
        r.q=r.f;
    }

    // -- Instructions only the final renderers needed so far
    void load_stack_pointer(std::uint16_t at) {                                                            // ld sp,nn
        bus_.expect(at,OpcodePattern::exactly(0x31));
        bus_.fetch(at); r.sp=operand_word(at+1); bus_.advance(10);
    }
    void exchange_alternates(std::uint16_t at) {                                                           // exx
        bus_.expect(at,OpcodePattern::exactly(0xD9)); one_byte(at,4);
        const auto saved_bc=r.bc_, saved_de=r.de_, saved_hl=r.hl_;
        r.bc_=bc(); r.de_=de(); r.hl_=hl(); set_bc(saved_bc); set_de(saved_de); set_hl(saved_hl);
    }
    void exchange_af(std::uint16_t at) {                                                                   // ex af,af'
        bus_.expect(at,OpcodePattern::exactly(0x08)); one_byte(at,4);
        const auto saved=r.af_; r.af_=pair(r.a,r.f); r.a=high_byte(saved); r.f=low_byte(saved);
    }
    // An explicit in-body guard (review R2): a state this rendering does not
    // handle. The live run stops with GuardFailure naming the segment and
    // `what`; the estimate becomes uncertain (the segment refuses the state
    // at its entry). Returns whether the guard holds.
    bool guard(bool holds_now, std::string_view what) { return bus_.guard(holds_now,what); }

    // -- Final instructions (milestone 14). Each renders the segment's last
    // instruction exactly as the others (fetches, operands, stack, cycles,
    // flags, MEMPTR, Q, IFFs) and returns the exit with the next PC. The exit
    // must be declared: returned_at for RET, RET cc and RETN, the copy or
    // output repeat for the slices, core_final_at (or conditional_at /
    // loop_at) for everything else.
    Exit final_jump(std::uint16_t at) {                                                                    // jp nn
        bus_.expect(at,OpcodePattern::exactly(0xC3)); return ends(at,jump(at));
    }
    Exit final_jump_if(std::uint16_t at, Condition cc) {                                                   // jp cc,nn
        const bool taken=jump_if(at,cc); return ends(at,taken ? r.wz : static_cast<std::uint16_t>(at+3));
    }
    Exit final_relative(std::uint16_t at) {                                                                // jr e
        bus_.expect(at,OpcodePattern::exactly(0x18)); relative(at); return ends(at,r.wz);
    }
    Exit final_relative_if(std::uint16_t at, Condition cc) {                                               // jr cc,e
        const bool taken=relative_if(at,cc);
        return ends(at,taken ? r.wz : static_cast<std::uint16_t>(at+2));
    }
    Exit final_decrement_jump(std::uint16_t at) {                                                          // djnz e
        bus_.expect(at,OpcodePattern::exactly(0x10));
        const bool taken=decrement_jump(at); return ends(at,taken ? r.wz : static_cast<std::uint16_t>(at+2));
    }
    Exit final_jump_hl(std::uint16_t at) {                                                                 // jp (hl)
        bus_.expect(at,OpcodePattern::exactly(0xE9)); one_byte(at,4); return ends(at,hl());
    }
    Exit final_call(std::uint16_t at) {                                                                    // call nn
        bus_.expect(at,OpcodePattern::exactly(0xCD)); return ends(at,call(at));
    }
    Exit final_call_if(std::uint16_t at, Condition cc) {                                                   // call cc,nn
        const bool taken=call_if(at,cc); return ends(at,taken ? r.wz : static_cast<std::uint16_t>(at+3));
    }
    // rst p: MEMPTR = PC = p, at + 1 pushed. The vector is the instruction's
    // own (08 is the only one the game uses), part of its declared pattern.
    Exit final_restart(std::uint16_t at, std::uint8_t vector) {                                            // rst p
        bus_.expect(at,OpcodePattern::exactly(static_cast<std::uint8_t>(0xC7|(vector&0x38))));
        bus_.fetch(at); r.wz=static_cast<std::uint16_t>(vector&0x38);
        push_word(static_cast<std::uint16_t>(at+1)); bus_.advance(11);
        return ends(at,r.wz);
    }
    Exit final_return(std::uint16_t at) {                                                                  // ret
        bus_.expect(at,OpcodePattern::exactly(0xC9));
        const auto target=return_from(at); return {{RoutineExit::returned,at},0,target};
    }
    // ret cc: the returned exit when taken; when not taken the instruction is
    // rendered (5 cycles) and the body continues with the next instruction.
    //   if (auto exit=z.final_return_if(0x00F8,k::Condition::z)) return *exit;
    std::optional<Exit> final_return_if(std::uint16_t at, Condition cc) {                                  // ret cc
        if (!return_if(at,cc)) return std::nullopt;
        return Exit{{RoutineExit::returned,at},0,r.wz};
    }
    Exit final_enable_interrupts(std::uint16_t at) {                                                       // ei
        bus_.expect(at,OpcodePattern::exactly(0xFB)); one_byte(at,4); bus_.interrupts(true);
        return ends(at,static_cast<std::uint16_t>(at+1));
    }
    Exit final_disable_interrupts(std::uint16_t at) {                                                      // di
        bus_.expect(at,OpcodePattern::exactly(0xF3)); one_byte(at,4); bus_.interrupts(false);
        return ends(at,static_cast<std::uint16_t>(at+1));
    }
    Exit final_return_from_nmi(std::uint16_t at) {                                                         // retn
        bus_.expect(at,OpcodePattern::prefixed(0xED,0x45));
        bus_.fetch(at); bus_.fetch(at+1); bus_.return_from_nmi();
        r.wz=pop_word(); bus_.advance(14);
        return ends(at,r.wz);
    }
    // ldir as a slice of at most `limit` iterations (each an instruction with
    // its own boundary): the segment ends at the copy-repeat exit, at `at`
    // again while BC is not zero, else at at + 2. The limit only bounds how far
    // one top-level step runs (the overshoot of a frame).
    Exit final_block_copy(std::uint16_t at, unsigned limit=16) {                                           // ldir
        bus_.expect(at,OpcodePattern::prefixed(0xED,0xB0));
        for (unsigned i=0;i<limit;++i) { block_copy_byte(at); if (!bc()) break; }
        return {{RoutineExit::copy_repeat,at},0,bc() ? at : static_cast<std::uint16_t>(at+2)};
    }

protected:
    // Opcode patterns of the instruction methods (review R2 M1).
    static constexpr OpcodePattern op(std::uint8_t value, std::uint8_t mask=0xff) { return {0,value,mask}; }
    // The register field of an operand (B C D E H L A: 0-5 and 7), or -1 when
    // it is not one of r's 8-bit registers (a value the body holds elsewhere).
    int field(const std::uint8_t& x) const {
        const std::uint8_t* registers[]{&r.b,&r.c,&r.d,&r.e,&r.h,&r.l,nullptr,&r.a};
        for (int i=0;i<8;++i) if (registers[i]==&x) return i;
        return -1;
    }
    // `base` with the register's field at `shift`, or the whole family when
    // the operand is not a register of r.
    OpcodePattern with_field(std::uint8_t prefix, std::uint8_t base, unsigned shift, const std::uint8_t& x) const {
        const int f=field(x);
        if (f<0) return {prefix,base,static_cast<std::uint8_t>(~(7u<<shift))};
        return {prefix,static_cast<std::uint8_t>(base|(f<<shift)),0xff};
    }
    // The pair field (BC DE HL, and AF for PUSH/POP: 0-3) from the pair's high register.
    OpcodePattern with_pair(std::uint8_t prefix, std::uint8_t base, const std::uint8_t& high) const {
        const std::uint8_t* pairs[]{&r.b,&r.d,&r.h,&r.a};
        for (int i=0;i<4;++i) if (pairs[i]==&high) return {prefix,static_cast<std::uint8_t>(base|(i<<4)),0xff};
        return {prefix,base,0xCF};
    }
private:
    bool relative_run(std::uint16_t at, bool taken) {
        bus_.fetch(at); const auto offset=static_cast<std::int8_t>(operand(at+1));
        if (taken) r.wz=static_cast<std::uint16_t>(at+2+offset);
        bus_.advance(taken ? 12 : 7); return taken;
    }
    Exit ends(std::uint16_t at, std::uint16_t next) { return {{RoutineExit::core_final,at},0,next}; }
    static constexpr OpcodePattern conditional(std::uint8_t base, Condition cc) {
        return OpcodePattern::exactly(static_cast<std::uint8_t>(base|(static_cast<unsigned>(cc)<<3)));
    }
    static OpcodePattern relative_condition(Condition cc) {
        if (static_cast<unsigned>(cc)>3) throw std::invalid_argument("JR takes only the conditions nz, z, nc and c");
        return OpcodePattern::exactly(static_cast<std::uint8_t>(0x20|(static_cast<unsigned>(cc)<<3)));
    }
    std::uint8_t operand(unsigned at) { return bus_.read(static_cast<std::uint16_t>(at)); }
    std::uint16_t operand_word(unsigned at) { const auto low=operand(at), high=operand(at+1); return pair(high,low); }
    void push_word(std::uint16_t value) {
        r.sp=static_cast<std::uint16_t>(r.sp-1); bus_.write(r.sp,high_byte(value));
        r.sp=static_cast<std::uint16_t>(r.sp-1); bus_.write(r.sp,low_byte(value));
    }
    std::uint16_t pop_word() {
        const auto low=bus_.read(r.sp); r.sp=static_cast<std::uint16_t>(r.sp+1);
        const auto high=bus_.read(r.sp); r.sp=static_cast<std::uint16_t>(r.sp+1);
        return pair(high,low);
    }
    Bus& bus_;
};

// ---- Exit declarations (SegmentSpec) -----------------------------------------------
// Named factories instead of positional initializers. `stack` is SP before the
// final instruction minus the entry SP, in even bytes: negative for words the
// segment pushed and still holds (it needs that much scratch_stack), positive
// for inherited words it popped (it needs that much inherited_stack).
//   returned_at(0x6807)             R 6807
//   returned_at(0x6849,+4)          R 6849 [+4]
//   core_final_at(0x6640,-2)        F 6640 [-2]
//   conditional_at(0x68F4)          X 68F4 (JP cc, JR cc or DJNZ, decided from the flags)
//   loop_at(0x6988,128,-2)          L 6988 [-2] (after 128)
inline SegmentExit returned_at(std::uint16_t address, std::int8_t stack=0) {
    return SegmentExit{.kind=RoutineExit::returned,.address=address,.stack=stack};
}
inline SegmentExit core_final_at(std::uint16_t address, std::int8_t stack=0) {
    return SegmentExit{.kind=RoutineExit::core_final,.address=address,.stack=stack};
}
inline SegmentExit conditional_at(std::uint16_t address, std::int8_t stack=0) {
    return SegmentExit{.kind=RoutineExit::core_final,.address=address,.stack=stack,.conditional=true};
}
inline SegmentExit loop_at(std::uint16_t address, std::uint16_t after, std::int8_t stack=0) {
    return SegmentExit{.kind=RoutineExit::core_final,.address=address,.stack=stack,.after=after};
}
// The permissions of a segment; every field defaults to none.
struct Permissions {
    bool slot2_mapper=false;      // writes the slot-2 bank register $FFFF
    unsigned scratch_stack=0;     // bytes below the entry SP the body pushes (held words included)
    unsigned inherited_stack=0;   // bytes above the entry SP pushed by an earlier segment of the unit
};
//   segment(dungeon::handler_unit,{returned_at(0x6807),loop_at(0x6618,128)},{.slot2_mapper=true,.scratch_stack=4})
inline SegmentSpec segment(RoutineEntry unit, std::vector<SegmentExit> exits, Permissions permissions={}) {
    SegmentSpec spec{unit,std::move(exits),permissions.slot2_mapper,permissions.scratch_stack};
    spec.inherited_stack=permissions.inherited_stack;
    return spec;
}

// The cycles of common final instructions, for the fixed bounds of
// single-instruction segments.
inline constexpr unsigned call_cycles=17, untaken_call_cycles=10, jump_cycles=10, return_cycles=10,
    taken_conditional_return_cycles=11;

// ---- Bounds and registration -------------------------------------------------------
// The exact cost of the path this machine will take: the segment run on the
// estimating bus. When it read an unmapped address the path is not known, and
// `maximum` (the longest path of the segment) is used instead. A split scan
// whose body tests Instructions::loop_exit(after) gets the mechanism's loop
// rule here unchanged, since the estimating bus counts the same elapsed cycles.
template<class Logic> std::uint64_t cost(const Machine& machine, const Logic& logic, std::uint64_t maximum) {
    Estimate bus(machine);
    auto registers=load(machine.cpu());
    Instructions<Estimate> instructions(bus,registers);
    const auto exit=logic(instructions);
    return bus.uncertain ? maximum : bus.cycles()+exit.cycles;
}
// The register sync of a body: stores and loads the body's registers with
// `store` and `load` (the kit's, or a private kit's), so that an instruction
// boundary inside the body can hand them to a handler or an observer.
template<class R, auto Store, auto Load> using Sync=RegisterSyncOf<R,Store,Load>;
// Ends the call at the exit the body took: its rendered final continues at
// the next PC. The registers must be stored first.
inline void finish(RoutineContext& context, const Exit& exit) {
    if (!exit.next) throw std::logic_error("A body ended without rendering its final instruction");
    context.finish(exit.segment,*exit.next);
}
template<class Logic> void run(RoutineContext& context, const Logic& logic) {
    auto& cpu=context.cpu();
    Live bus(context);
    auto registers=load(cpu);
    Instructions<Live> instructions(bus,registers);
    Exit exit{};
    {
        // An abandoned body (RoutineAbandoned) never stores its registers.
        Sync<Registers,&kit::store,&kit::load> sync(registers);
        RoutineContext::SyncScope scope(context,sync);
        exit=logic(instructions);
    }
    store(cpu,registers);
    finish(context,exit);
}
// A segment whose bound is its estimated path.
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec,
                                       std::uint64_t maximum, Logic logic,
                                       std::source_location where=std::source_location::current()) {
    registry.add_exact(entry,std::move(spec),
        [logic,maximum](const Machine& machine) { return cost(machine,logic,maximum); },
        [logic](RoutineContext& context) { run(context,logic); },where);
}
// A segment with its own bound function (a fixed cost, for one).
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec,
                                       RegisteredRoutine::Bound bound, Logic logic,
                                       std::source_location where=std::source_location::current()) {
    registry.add_exact(entry,std::move(spec),std::move(bound),[logic](RoutineContext& context) { run(context,logic); },where);
}
// A joining instruction alone (milestone 14): a one-instruction segment at
// `entry` of the routine `unit`, rendered by `logic`, its bound the longest
// path of that instruction; `scratch` is what it pushes (a call's return).
template<class Logic> void add_joining(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit,
                                       std::vector<SegmentExit> exits, std::uint64_t cycles, unsigned scratch,
                                       Logic logic, std::source_location where=std::source_location::current()) {
    auto spec=segment(unit,std::move(exits),{.scratch_stack=scratch});
    spec.single_instruction=true;
    add_segment(registry,entry,std::move(spec),RegisteredRoutine::Bound([cycles](const Machine&) { return cycles; }),
                std::move(logic),where);
}
// A segment with an explicit entry guard (review R2): the states its body
// renders, named. Replaces a bound function that returned an uncertain
// maximum to refuse some states:
//   k::add_segment(registry,{12,0x855E},spec,k::Guard{"IX in work RAM",[](const Machine& m) {
//       const auto ix=m.cpu().ix_iy[0].uint16_value; return ix>=0xc000 && ix<=0xdfe8; }},
//       k::uncertain_maximum,[](auto& z) { return command_add_timing(z); });
using Guard=EntryGuard;
template<class Logic> void add_segment(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec,
                                       Guard guard, std::uint64_t maximum, Logic logic,
                                       std::source_location where=std::source_location::current()) {
    registry.add_exact(entry,std::move(spec),std::move(guard),
        [logic,maximum](const Machine& machine) { return cost(machine,logic,maximum); },
        [logic](RoutineContext& context) { run(context,logic); },where);
}

// ---- The cell readers -----------------------------------------------------------
// 01:6BCA: the kind (low three bits) of the layout cell at view offset index B
// from the party's position and facing; HL is preserved through the stack.
// 01:6BE9: the same cell's value without bit 7 (the opened-door mark); HL is
// left pointing at the cell, DE holds B. Both rendered without their final
// return, so a caller can render them inline after a bare call.
inline constexpr std::uint16_t cell_type_return=0x6BE8, cell_value_return=0x6C05;
inline constexpr unsigned cell_type_cycles=159, cell_value_cycles=138; // with the final return
template<class Bus> void cell_offset_address(Instructions<Bus>& z, std::uint16_t at) {
    // A = facing & 3, times 16; DE = that; HL = offsets + DE + B; A = position + offset.
    auto& r=z.r;
    z.load_a_direct(at);
    z.and_immediate(at+3);
    for (std::uint16_t i=0;i<4;++i) z.add_register(static_cast<std::uint16_t>(at+5+i),r.a);
    z.copy(at+9,r.e,r.a);
    z.load_immediate(at+10,r.d);
    z.load_hl(at+12);
    z.add_to_hl(at+15,z.de());
    z.copy(at+16,r.e,r.b);
    z.add_to_hl(at+17,z.de());
    z.load_a_direct(at+18);
    z.add_from_hl(at+21);
    z.load_immediate(at+22,r.h);
    z.copy(at+24,r.l,r.a);
    z.load_from_hl(at+25,r.a);
}
template<class Bus> void cell_type(Instructions<Bus>& z) {        // 01:6BCA-6BE7
    auto& r=z.r;
    z.push(0x6BCA,r.h,r.l);
    cell_offset_address(z,0x6BCB);
    z.and_immediate(0x6BE5);
    z.pop(0x6BE7,r.h,r.l);
}
template<class Bus> void cell_value(Instructions<Bus>& z) {       // 01:6BE9-6C04
    cell_offset_address(z,0x6BE9);
    z.and_immediate(0x6C03);
}
// A bare `call 6BCA` (kind) or `call 6BE9` (value) at `at`, with the reader's
// body and return rendered in the segment. The call pushes its return address
// and the reader's return pops it again. Where the stack does not give back
// what was pushed (stack bytes in ROM), the original returns elsewhere and the
// rendering cannot follow it: an explicit in-body guard (Instructions::guard:
// the estimate is uncertain, so the segment refuses the state at its entry;
// the live run stops with GuardFailure). Returns whether the
// reader came back to the instruction after the call.
template<class Bus> bool inline_reader(Instructions<Bus>& z, bool kind, std::uint16_t at) {
    (void)z.call(at);
    if (kind) cell_type(z); else cell_value(z);
    const auto target=z.return_from(kind ? cell_type_return : cell_value_return);
    return z.guard(target==static_cast<std::uint16_t>(at+3),"an inline cell reader returned to another address (stack not in RAM)");
}
}
