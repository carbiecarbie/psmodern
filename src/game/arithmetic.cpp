// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace k=map_kit;

template<class Z> k::Exit byte_product(Z& z) {
    z.load_immediate(0x0429,z.r.d);
    z.copy(0x042B,z.r.l,z.r.d);
    for (std::uint16_t at=0x042C;at<0x0448;at+=4) {
        z.add_to_hl(at,z.hl());
        if (!z.relative_if(at+1,!z.carry())) z.add_to_hl(at+3,z.de());
    }
    z.add_to_hl(0x0448,z.hl());
    if (auto exit=z.final_return_if(0x0449,k::Condition::nc)) return *exit;
    z.add_to_hl(0x044A,z.de());
    return z.final_return(0x044B);
}

// Multiply DE by BC into DE:HL. Each unrolled group consumes four
// multiplier bits, adding BC into the low word and propagating its carry.
template<class Z> k::Exit word_product(Z& z,unsigned group) {
    if (!group) {
        z.or_a(0x044C);
        z.load_hl(0x044D);
    }
    for (unsigned i=4*group;i<4*group+4;++i) {
        const std::uint16_t at=0x0450+11*i;
        if (i%4) z.add_to_hl(at-1,z.hl());
        z.rotate_left_through_carry(at,z.r.e);
        z.rotate_left_through_carry(at+2,z.r.d);
        if (i==15) {
            if (auto exit=z.final_return_if(0x04F9,k::Condition::nc)) return *exit;
            z.add_to_hl(0x04FA,z.bc());
            if (auto exit=z.final_return_if(0x04FB,k::Condition::nc)) return *exit;
            z.increment_pair(0x04FC,z.r.d,z.r.e);
            return z.final_return(0x04FD);
        }
        if (!z.relative_if(at+4,!z.carry())) {
            z.add_to_hl(at+6,z.bc());
            if (!z.relative_if(at+7,!z.carry())) z.increment_pair(at+9,z.r.d,z.r.e);
        }
    }
    // The next group's first doubling of the low word ends this one.
    const auto next=static_cast<std::uint16_t>(0x0450+44*(group+1));
    z.add_to_hl(next-1,z.hl());
    return k::fall_through(next-1,next);
}

template<class Z> void shift_quotient(Z& z,std::uint16_t at) {
    const unsigned old=z.hl(), sum=old*2+z.carry();
    const auto result=static_cast<std::uint16_t>(sum);
    z.bus().fetch(at);
    z.bus().fetch(at+1);
    z.r.f=static_cast<std::uint8_t>(((result>>8)&0xA8)|(result==0 ? 0x40 : 0)|
        (result&0x1000 ? 0x10 : 0)|((old^result)&0x8000 ? 4 : 0)|(sum>>16)); z.r.q=z.r.f;
    z.r.wz=static_cast<std::uint16_t>(old+1);
    z.set_hl(result);
    z.bus().advance(15);
}

// A holds the partial remainder, HL the dividend and quotient. Form the
// next quotient bit by complementing the carry from the remainder test.
template<class Z> k::Exit divide(Z& z,unsigned group) {
    if (!group) {
        z.xor_a(0x04FE);
        z.add_to_hl(0x04FF,z.hl());
        z.add_with_carry(0x0500,z.r.a);
    }
    for (unsigned i=4*group;i<4*group+4;++i) {
        const std::uint16_t at=0x0501+11*i;
        const bool overflow=z.relative_if(at,z.carry());
        bool subtract=overflow;
        if (!overflow) {
            z.compare_register(at+2,z.r.e);
            subtract=!z.relative_if(at+3,z.carry());
        }
        if (subtract) {
            z.subtract_register(at+5,z.r.e);
            z.or_a(at+6);
        }
        // CCF, with its undocumented bits from F XOR Q (Q is F after the OR A
        // of the subtracting path, 0 after a relative jump).
        z.complement_carry(at+7);
        shift_quotient(z,at+8);
        if (i==15) return z.final_return(0x05B0);
        z.add_with_carry(at+10,z.r.a);
        // The next group begins after this bit's remainder shift.
        if (i%4==3) return k::fall_through(at+10,at+11);
    }
    throw std::logic_error("Division group has no final bit");
}
}
void register_arithmetic(RoutineRegistry& registry) {
    const RoutineEntry byte{0,0x0429},word{0,0x044C},quotient{0,0x04FE};
    k::add_segment(registry,byte,k::segment(byte,{k::returned_at(0x0449),k::returned_at(0x044B)}),
                   [](auto& z){return byte_product(z);});
    for (unsigned group=0;group<4;++group) {
        const RoutineEntry mul{0,word_product_entries[group]},div{0,division_entries[group]};
        auto mul_exits=group==3 ? std::vector{k::returned_at(0x04F9),k::returned_at(0x04FB),k::returned_at(0x04FD)}
                                : std::vector{k::core_final_at(word_product_entries[group+1]-1)};
        k::add_segment(registry,mul,k::segment(word,std::move(mul_exits)),[group](auto& z){return word_product(z,group);});
        auto div_exit=group==3 ? k::returned_at(0x05B0) : k::core_final_at(division_entries[group+1]-1);
        k::add_segment(registry,div,k::segment(quotient,{div_exit}),[group](auto& z){return divide(z,group);});
    }
}
}
