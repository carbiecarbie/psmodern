// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/money.h"
#include "machine/machine.h"
#include <algorithm>
#include <bit>
#include <utility>

namespace psmodern::game {
namespace {
constexpr std::uint8_t carry_flag=0x01, subtract_flag=0x02, parity_flag=0x04, x_flag=0x08, half_flag=0x10,
    y_flag=0x20, zero_flag=0x40, sign_flag=0x80;

struct Registers { std::uint8_t a, f, b, c, d, e, h, l; std::uint16_t ix,sp,wz; };
Registers load(const Z80& cpu) {
    return {cpu.af.uint8_values.at_1,cpu.af.uint8_values.at_0,cpu.bc.uint8_values.at_1,cpu.bc.uint8_values.at_0,
        cpu.de.uint8_values.at_1,cpu.de.uint8_values.at_0,cpu.hl.uint8_values.at_1,cpu.hl.uint8_values.at_0, cpu.ix_iy[0].uint16_value,cpu.sp.uint16_value,cpu.memptr.uint16_value};
}
void store(Z80& cpu, const Registers& r) {
    cpu.af.uint8_values.at_1=r.a; cpu.af.uint8_values.at_0=r.f;
    cpu.bc.uint8_values.at_1=r.b; cpu.bc.uint8_values.at_0=r.c;
    cpu.de.uint8_values.at_1=r.d; cpu.de.uint8_values.at_0=r.e;
    cpu.hl.uint8_values.at_1=r.h; cpu.hl.uint8_values.at_0=r.l;
    cpu.ix_iy[0].uint16_value=r.ix; cpu.sp.uint16_value=r.sp; cpu.memptr.uint16_value=r.wz;
}
std::uint16_t pair(std::uint8_t high, std::uint8_t low) { return static_cast<std::uint16_t>((high<<8)|low); }

// The last instruction of a segment, executed by the core, and the cycles it
// takes on the path the segment found.
struct Exit { SegmentExit segment; unsigned cycles; };
Exit returns(std::uint16_t address, unsigned cycles) { return {{RoutineExit::returned,address},cycles}; }
Exit continues(std::uint16_t address, unsigned cycles) { return {{RoutineExit::core_final,address},cycles}; }
constexpr unsigned call_cycles=17, untaken_call_cycles=10, return_cycles=10, taken_conditional_return_cycles=11;

// Flags of the arithmetic and logic instructions the checks use.
std::uint8_t sign_zero_xy(std::uint8_t value) {
    return static_cast<std::uint8_t>((value&(sign_flag|y_flag|x_flag))|(value ? 0 : zero_flag));
}
std::uint8_t parity(std::uint8_t value) { return std::popcount(value)%2 ? 0 : parity_flag; }
void add8(Registers& r, std::uint8_t operand, unsigned carry) {
    const unsigned sum=r.a+operand+carry;
    const auto result=static_cast<std::uint8_t>(sum);
    r.f=static_cast<std::uint8_t>(sign_zero_xy(result)|((r.a^operand^result)&half_flag)|
        ((~(r.a^operand)&(r.a^result)&0x80) ? parity_flag : 0)|(sum>0xff ? carry_flag : 0));
    r.a=result;
}
void compare8(Registers& r, std::uint8_t operand) {
    const auto result=static_cast<std::uint8_t>(r.a-operand);
    r.f=static_cast<std::uint8_t>((result&sign_flag)|(result ? 0 : zero_flag)|(operand&(y_flag|x_flag))|
        ((r.a^operand^result)&half_flag)|(((r.a^operand)&(r.a^result)&0x80) ? parity_flag : 0)|
        subtract_flag|(r.a<operand ? carry_flag : 0));
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

// One bus per way of running a segment: fetch (opcode read and refresh), read,
// write, and time.
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

// The original instructions this setup contains, each given the address it has
// in the ROM. Opcode fetches, operand reads and data accesses follow the bus
// order of the Z80; operands (addresses, constants, the bank number) are read
// from the image, never written here.
template<class Bus> class Instructions {
public:
    Instructions(Bus& bus, Registers& registers) : r(registers), bus_(bus) {}
    Registers& r;

    std::uint16_t hl() const { return pair(r.h,r.l); }
    std::uint16_t de() const { return pair(r.d,r.e); }
    std::uint16_t bc() const { return pair(r.b,r.c); }

    void one_byte(std::uint16_t at, unsigned cycles) { bus_.fetch(at); bus_.advance(cycles); }
    void load_hl(std::uint16_t at) { load_pair(at,r.h,r.l); }
    void load_de(std::uint16_t at) { load_pair(at,r.d,r.e); }
    void store_to_hl(std::uint16_t at) { // ld (hl),n
        bus_.fetch(at); const auto value=operand(at+1); bus_.write(hl(),value); bus_.advance(10);
    }
    void load_immediate(std::uint16_t at, std::uint8_t& to) { bus_.fetch(at); to=operand(at+1); bus_.advance(7); }
    void copy(std::uint16_t at, std::uint8_t& to, std::uint8_t from) { one_byte(at,4); to=from; }
    void load_from_hl(std::uint16_t at, std::uint8_t& to) { bus_.fetch(at); to=bus_.read(hl()); bus_.advance(7); }
    void load_a_direct(std::uint16_t at) { // ld a,(nn)
        bus_.fetch(at); const auto address=operand_word(at+1); r.a=bus_.read(address); r.wz=address+1; bus_.advance(13);
    }
    void store_a_direct(std::uint16_t at) { // ld (nn),a
        bus_.fetch(at); const auto address=operand_word(at+1); bus_.write(address,r.a); r.wz=static_cast<std::uint16_t>((r.a<<8)|static_cast<std::uint8_t>(address+1)); bus_.advance(13);
    }
    void increment_hl(std::uint16_t at) { one_byte(at,6); const auto v=static_cast<std::uint16_t>(hl()+1); r.h=v>>8; r.l=static_cast<std::uint8_t>(v); }
    void and_immediate(std::uint16_t at) { bus_.fetch(at); r.a&=operand(at+1); logic_flags(r,true); bus_.advance(7); }
    void add_register(std::uint16_t at, std::uint8_t value) { one_byte(at,4); add8(r,value,0); }
    void add_to_hl(std::uint16_t at, std::uint16_t operand) { one_byte(at,11); r.wz=hl()+1; add_hl(r,operand); }
    void load_bc(std::uint16_t at) { load_pair(at,r.b,r.c); }
    void copy_block(std::uint16_t at) {
        do {
            bus_.fetch(at); bus_.fetch(at+1);
            const auto value=bus_.read(hl()); bus_.write(de(),value);
            const auto nh=static_cast<std::uint16_t>(hl()+1), nd=static_cast<std::uint16_t>(de()+1), nc=static_cast<std::uint16_t>(bc()-1);
            r.h=nh>>8; r.l=nh; r.d=nd>>8; r.e=nd; r.b=nc>>8; r.c=nc;
            const auto sum=static_cast<std::uint8_t>(r.a+value);
            r.f=static_cast<std::uint8_t>((r.f&0xc1)|(nc ? parity_flag : 0)|(sum&8)|((sum&2)<<4));
            if(nc) r.wz=static_cast<std::uint16_t>(at+1);
            bus_.advance(nc ? 21 : 16);
        } while(bc());
    }
    void increment_a(std::uint16_t at) {
        one_byte(at,4); const auto old=r.a; ++r.a;
        r.f=static_cast<std::uint8_t>((r.f&carry_flag)|sign_zero_xy(r.a)|((old&15)==15 ? half_flag : 0)|(old==0x7f ? parity_flag : 0));
    }
    void compare_register(std::uint16_t at,std::uint8_t value) { one_byte(at,4); compare8(r,value); }
    void test_bit7_a(std::uint16_t at) {
        bus_.fetch(at);bus_.fetch(at+1);
        r.f=static_cast<std::uint8_t>((r.f&carry_flag)|half_flag|(r.a&(sign_flag|y_flag|x_flag))|((r.a&0x80) ? 0 : zero_flag|parity_flag)); bus_.advance(8);
    }
    bool relative_if_not_zero(std::uint16_t at) { return relative(at,!(r.f&zero_flag)); }
    bool relative_if_no_carry(std::uint16_t at) { return relative(at,!(r.f&carry_flag)); }
    void exchange(std::uint16_t at) { one_byte(at,4); std::swap(r.d,r.h); std::swap(r.e,r.l); }
    void load_ix(std::uint16_t at) { bus_.fetch(at);bus_.fetch(at+1);r.ix=operand_word(at+2);bus_.advance(14); }
    void index_store_immediate(std::uint16_t at) { bus_.fetch(at);bus_.fetch(at+1); auto d=operand(at+2), v=operand(at+3);r.wz=static_cast<std::uint16_t>(r.ix+static_cast<std::int8_t>(d));bus_.write(r.wz,v);bus_.advance(19); }
    void index_store(std::uint16_t at,std::uint8_t v) { bus_.fetch(at);bus_.fetch(at+1);auto d=operand(at+2);r.wz=static_cast<std::uint16_t>(r.ix+static_cast<std::int8_t>(d));bus_.write(r.wz,v);bus_.advance(19); }
    void add_ix_de(std::uint16_t at) { bus_.fetch(at);bus_.fetch(at+1); const unsigned sum=r.ix+de(); r.f=static_cast<std::uint8_t>((r.f&0xc4)|((sum>>8)&0x28)|(((r.ix^de()^sum)>>8)&0x10)|((sum>>16)&1)); r.wz=r.ix+1;r.ix=static_cast<std::uint16_t>(sum);bus_.advance(15); }
    bool decrement_b_jump(std::uint16_t at) { bus_.fetch(at); const auto displacement=static_cast<std::int8_t>(operand(at+1)); --r.b; if(r.b) r.wz=static_cast<std::uint16_t>(at+2+displacement);bus_.advance(r.b ? 13 : 8);return r.b!=0; }
    void pop_hl(std::uint16_t at) { bus_.fetch(at);r.l=bus_.read(r.sp++);r.h=bus_.read(r.sp++);bus_.advance(10); }
    void push_hl(std::uint16_t at) { bus_.fetch(at);bus_.write(--r.sp,r.h);bus_.write(--r.sp,r.l);bus_.advance(11); }
    void store_hl_direct(std::uint16_t at) { bus_.fetch(at);const auto a=operand_word(at+1);bus_.write(a,r.l);bus_.write(a+1,r.h);r.wz=a+1;bus_.advance(16); }
    // Conditional jumps taken inside the segment; the operand is read either
    // way. They report whether the jump was taken.
    bool jump_if_no_carry(std::uint16_t at) { // jp nc,nn
        bus_.fetch(at); r.wz=operand_word(at+1); bus_.advance(10); return !(r.f&carry_flag);
    }
    // A conditional return that is not taken (the taken one ends the segment
    // and is executed by the core).
    void return_not_taken(std::uint16_t at) { one_byte(at,5); }

private:
    std::uint8_t operand(unsigned at) { return bus_.read(static_cast<std::uint16_t>(at)); }
    std::uint16_t operand_word(unsigned at) {
        const auto low=operand(at), high=operand(at+1);
        return pair(high,low);
    }
    void load_pair(std::uint16_t at, std::uint8_t& high, std::uint8_t& low) {
        bus_.fetch(at); const auto value=operand_word(at+1);
        high=static_cast<std::uint8_t>(value>>8); low=static_cast<std::uint8_t>(value); bus_.advance(10);
    }
    bool relative(std::uint16_t at, bool taken) { bus_.fetch(at); auto displacement=static_cast<std::int8_t>(operand(at+1)); if(taken) r.wz=static_cast<std::uint16_t>(at+2+displacement); bus_.advance(taken ? 12 : 7); return taken; }
    Bus& bus_;
};

// Each segment leaves calls and the small number of stack bridges to the
// original core. The two buses execute exactly the same path.
template<class Bus> Exit setup_start(Instructions<Bus>& z) {
    auto& r=z.r;
    z.load_hl(0x5FFE); z.store_to_hl(0x6001);
    z.load_hl(0x6003); z.load_de(0x6006); z.load_bc(0x6009); z.store_to_hl(0x600C); z.copy_block(0x600E);
    z.load_hl(0x6010); z.load_de(0x6013); z.load_bc(0x6016); z.store_to_hl(0x6019); z.copy_block(0x601B);
    z.load_a_direct(0x601D); z.copy(0x6020,r.a,r.a); z.and_immediate(0x6021);
    if(r.f&zero_flag) return returns(0x6023,taken_conditional_return_cycles);
    z.return_not_taken(0x6023);
    z.copy(0x6024,r.l,r.a); z.load_immediate(0x6025,r.h);
    z.add_to_hl(0x6027,z.hl()); z.add_to_hl(0x6028,z.hl()); z.add_to_hl(0x6029,z.hl());
    z.add_to_hl(0x602A,z.hl()); z.add_to_hl(0x602B,z.hl()); z.load_de(0x602C); z.add_to_hl(0x602F,z.de());
    z.load_de(0x6030); z.load_bc(0x6033); z.copy_block(0x6036);
    z.load_de(0x6038); z.load_bc(0x603B); z.copy_block(0x603E);
    z.load_from_hl(0x6040,r.b); z.increment_hl(0x6041); z.load_from_hl(0x6042,r.a);
    return continues(0x6043,6);
}
template<class Bus> Exit setup_picture(Instructions<Bus>& z) {
    auto& r=z.r;
    z.load_from_hl(0x6045,r.h); z.copy(0x6046,r.l,r.a); z.copy(0x6047,r.a,r.b);
    z.store_a_direct(0x6048); z.load_de(0x604B); return continues(0x604E,call_cycles);
}
template<class Bus> Exit setup_picture_done(Instructions<Bus>& z) {
    z.increment_hl(0x6052); z.load_immediate(0x6053,z.r.a); z.store_a_direct(0x6055);
    return continues(0x6058,7);
}
template<class Bus> void setup_count_and_records(Instructions<Bus>& z) {
    auto& r=z.r;
    z.and_immediate(0x607F); z.copy(0x6081,r.b,r.a); z.store_a_direct(0x6082);
    z.increment_hl(0x6085); z.load_from_hl(0x6086,r.a); z.increment_hl(0x6087);
    z.load_from_hl(0x6088,r.d); z.increment_hl(0x6089); z.load_from_hl(0x608A,r.e);
}
template<class Bus> Exit setup_count_start(Instructions<Bus>& z) {
    auto& r=z.r;
    z.increment_hl(0x605E); z.load_immediate(0x605F,r.a); z.store_a_direct(0x6061);
    z.load_from_hl(0x6064,r.a); z.test_bit7_a(0x6065);
    if(!z.relative_if_not_zero(0x6067)) {
        z.and_immediate(0x6069); z.copy(0x606B,r.b,r.a); z.load_a_direct(0x606C);
        z.increment_a(0x606F); z.add_register(0x6070,r.a); z.compare_register(0x6071,r.b);
        if(!z.relative_if_no_carry(0x6072)) z.copy(0x6074,r.b,r.a);
        return continues(0x6075,call_cycles);
    }
    setup_count_and_records(z); return continues(0x608B,6);
}
template<class Bus> Exit setup_count_drawn(Instructions<Bus>& z) {
    z.and_immediate(0x6078); z.compare_register(0x607A,z.r.b);
    if(z.jump_if_no_carry(0x607B)) return continues(0x6075,call_cycles);
    z.increment_a(0x607E); setup_count_and_records(z); return continues(0x608B,6);
}
template<class Bus> Exit setup_records(Instructions<Bus>& z) {
    auto& r=z.r;
    z.exchange(0x608D); z.load_ix(0x608E); z.load_de(0x6092);
    do {
        z.index_store_immediate(0x6095); z.index_store(0x6099,r.a); z.index_store(0x609C,r.a);
        z.index_store(0x609F,r.h); z.index_store(0x60A2,r.l); z.add_ix_de(0x60A5);
    } while(z.decrement_b_jump(0x60A7));
    z.pop_hl(0x60A9); z.load_from_hl(0x60AA,r.a); z.store_a_direct(0x60AB);
    z.increment_hl(0x60AE); z.load_from_hl(0x60AF,r.e); z.increment_hl(0x60B0);
    z.load_from_hl(0x60B1,r.d); z.push_hl(0x60B2); z.load_a_direct(0x60B3);
    z.copy(0x60B6,r.c,r.a); z.load_immediate(0x60B7,r.b); return continues(0x60B9,call_cycles);
}
template<class Bus> Exit setup_experience(Instructions<Bus>& z) {
    auto& r=z.r;
    z.pop_hl(0x60BF); z.increment_hl(0x60C0); z.load_from_hl(0x60C1,r.a); z.store_a_direct(0x60C2);
    z.increment_hl(0x60C5); z.load_from_hl(0x60C6,r.e); z.increment_hl(0x60C7); z.load_from_hl(0x60C8,r.d);
    z.push_hl(0x60C9); z.load_a_direct(0x60CA); z.copy(0x60CD,r.c,r.a); z.load_immediate(0x60CE,r.b);
    return continues(0x60D0,call_cycles);
}
template<class Bus> Exit setup_finish_fields(Instructions<Bus>& z) {
    z.increment_hl(0x60D7); z.load_from_hl(0x60D8,z.r.a); z.store_a_direct(0x60D9);
    z.increment_hl(0x60DC); z.load_from_hl(0x60DD,z.r.a); z.store_a_direct(0x60DE);
    z.load_hl(0x60E1); z.store_hl_direct(0x60E4); return continues(0x60E7,call_cycles);
}
template<class Bus> Exit setup_finish(Instructions<Bus>& z) {
    z.load_hl(0x60ED); z.load_de(0x60F0); z.load_bc(0x60F3); z.copy_block(0x60F6);
    z.load_immediate(0x60F8,z.r.a); return continues(0x60FA,10);
}
template<class Logic> std::uint64_t cost(const Machine& m,const Logic& logic,std::uint64_t maximum) {
    Estimate bus(m); auto r=load(m.cpu()); Instructions<Estimate> z(bus,r); const auto e=logic(z);
    return bus.uncertain ? maximum : bus.cycles()+e.cycles;
}
template<class Logic> void run(RoutineContext& c,const Logic& logic) {
    Live bus(c); auto r=load(c.cpu()); Instructions<Live> z(bus,r); const auto e=logic(z); store(c.cpu(),r);
    if(e.segment.kind==RoutineExit::returned) c.finish_return(e.segment.address); else c.finish_core(e.segment.address);
}
template<class Logic> void add(RoutineRegistry& registry,RoutineEntry entry,std::vector<SegmentExit> exits,
                                bool mapper,unsigned scratch,std::uint64_t maximum,Logic logic) {
    registry.add(entry,SegmentSpec{battle_setup_unit,std::move(exits),mapper,scratch},
        [logic,maximum](const Machine& m){return cost(m,logic,maximum);},
        [logic](RoutineContext& c){run(c,logic);});
}
} // namespace
void register_battle_setup(RoutineRegistry& r,const Options& options) {
    using E=SegmentExit; const auto ret=RoutineExit::returned, fin=RoutineExit::core_final;
    add(r,battle_setup_unit,{E{ret,0x6023},E{fin,0x6043}},true,0,10000,[](auto& z){return setup_start(z);});
    add(r,{1,0x6045},{E{fin,0x604E}},true,0,128,[](auto& z){return setup_picture(z);});
    add(r,{1,0x6052},{E{fin,0x6058}},true,0,128,[](auto& z){return setup_picture_done(z);});
    add(r,{1,0x605E},{E{fin,0x6075},E{fin,0x608B}},true,0,256,[](auto& z){return setup_count_start(z);});
    add(r,{1,0x6078},{E{fin,0x6075},E{fin,0x608B}},false,0,192,[](auto& z){return setup_count_drawn(z);});
    add(r,{1,0x608D},{E{fin,0x60B9}},false,2,40000,[](auto& z){return setup_records(z);});
    register_battle_money_store(r,options);
    add(r,{1,0x60BF},{E{fin,0x60D0}},false,2,128,[](auto& z){return setup_experience(z);});
    add(r,{1,0x60D7},{E{fin,0x60E7}},false,0,128,[](auto& z){return setup_finish_fields(z);});
    add(r,{1,0x60ED},{E{fin,0x60FA}},false,0,1000,[](auto& z){return setup_finish(z);});
}
}
