// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace k=map_kit;
constexpr RoutineEntry layout{1,0x6D56};

// One cartridge byte describes two adjacent cells, high nibble first.
// The original rereads the packed byte before extracting the second cell.
template<class Z> k::Exit unpack_pair(Z& z) {
    auto& r=z.r;
    z.load_from_hl(0x6D6E,r.a);
    for (unsigned i=0;i<4;++i) z.rotate_right_circular_a(0x6D6F+i);
    z.and_immediate(0x6D73);
    z.store_a_to_pair(0x6D75,z.de());
    z.increment_pair(0x6D76,r.d,r.e);
    z.load_from_hl(0x6D77,r.a);
    z.and_immediate(0x6D78);
    z.store_a_to_pair(0x6D7A,z.de());
    z.increment_pair(0x6D7B,r.d,r.e);
    z.increment_hl(0x6D7C);
    return k::decrement_jump_exit(z,0x6D7D);
}
template<class Z> k::Exit begin_layout(Z& z) {
    auto& r=z.r;
    z.load_hl(0x6D56);
    z.store_to_hl(0x6D59);
    z.load_a_direct(0x6D5B);
    z.copy(0x6D5E,r.h,r.a);
    z.load_immediate(0x6D5F,r.l);
    z.shift_right_logical(0x6D61,r.h);
    z.rotate_right_through_carry(0x6D63,r.l);
    z.load_de(0x6D65);
    z.add_to_hl(0x6D68,z.de());  // the floor number selects 128 packed bytes
    z.load_de(0x6D69);
    z.load_immediate(0x6D6C,r.b);
    return unpack_pair(z);
}
// In darkness the original checks six floor numbers for an exception.
// This is a CPIR on the player's table. A repeated compare leaves different
// undocumented flags and MEMPTR from the terminal comparison.
template<class Z> void find_lit_floor(Z& z) {
    auto& r=z.r;
    for (;;) {
        z.bus().fetch(0x6DB4);
        z.bus().fetch(0x6DB5);
        const auto item=z.bus().read(z.hl());
        z.set_hl(static_cast<std::uint16_t>(z.hl()+1));
        z.set_bc(static_cast<std::uint16_t>(z.bc()-1));
        const auto delta=static_cast<std::uint8_t>(r.a-item);
        const auto half=(r.a^item^delta)&kit::half_flag;
        const auto flags=(delta&kit::sign_flag) | (delta ? 0 : kit::zero_flag) |
            half | (z.bc() ? kit::parity_flag : 0) | kit::subtract_flag | (r.f&kit::carry_flag);
        if (delta && z.bc()) {
            r.f=flags | (0x6D & (kit::x_flag|kit::y_flag));
            r.wz=0x6DB5;
            z.bus().advance(21);
        } else {
            const auto adjusted=delta-(half>>4);
            r.f=flags | ((adjusted&2)<<4) | (adjusted&kit::x_flag);
            ++r.wz;
            z.bus().advance(16);
            return;
        }
    }
}
template<class Z> k::Exit configure_floor(Z& z) {
    auto& r=z.r;
    z.load_hl(0x6D7F);
    z.store_to_hl(0x6D82);
    z.load_hl(0x6D84);
    z.load_de(0x6D87);
    z.load_bc(0x6D8A);
    z.block_copy(0x6D8D);  // base colours used by each floor
    z.load_a_direct(0x6D8F);
    z.add_register(0x6D92,r.a);
    z.add_register(0x6D93,r.a);
    z.copy(0x6D94,r.l,r.a);
    z.load_immediate(0x6D95,r.h);
    z.load_de(0x6D97);
    z.add_to_hl(0x6D9A,z.de());
    z.load_from_hl(0x6D9B,r.e);
    z.increment_hl(0x6D9C);
    z.load_from_hl(0x6D9D,r.d);
    z.store_pair_direct(0x6D9E,r.d,r.e);
    z.increment_hl(0x6DA2);
    z.load_a_direct(0x6DA3);
    z.or_a(0x6DA6);
    z.load_from_hl(0x6DA7,r.a);
    if (!z.relative_if(0x6DA8,!z.zero())) {
        z.copy(0x6DAA,r.e,r.a);
        z.load_a_direct(0x6DAB);
        z.load_hl(0x6DAE);
        z.load_bc(0x6DB1);
        find_lit_floor(z);
        z.copy(0x6DB6,r.a,r.e);
        if (!z.relative_if(0x6DB7,z.zero())) z.load_immediate(0x6DB9,r.a);
    }
    z.increment(0x6DBB,r.a);
    z.store_a_direct(0x6DBC);
    for (unsigned i=0;i<3;++i) z.add_register(0x6DBF+i,r.a);
    z.copy(0x6DC2,r.l,r.a);
    z.load_immediate(0x6DC3,r.h);
    z.load_de(0x6DC5);
    z.add_to_hl(0x6DC8,z.de());
    z.load_from_hl(0x6DC9,r.a);
    z.store_a_direct(0x6DCA);
    z.load_de(0x6DCD);
    z.load_bc(0x6DD0);
    z.block_copy(0x6DD3);  // floor palette selected above
    z.load_a_direct(0x6DD5);
    z.store_a_direct(0x6DD8);
    z.load_a_direct(0x6DDB);
    z.store_a_direct(0x6DDE);
    return k::returns(0x6DE1,k::return_cycles);
}
}
void register_dungeon_layout(RoutineRegistry& registry) {
    const auto loop=k::conditional_at(0x6D7D);
    k::add_segment(registry,layout,k::segment(layout,{loop},{.slot2_mapper=true}),
                   [](auto& z){return begin_layout(z);});
    k::add_segment(registry,dungeon_layout_entries[1],k::segment(layout,{loop}),
                   [](auto& z){return unpack_pair(z);});
    k::add_segment(registry,dungeon_layout_entries[2],k::segment(layout,{k::returned_at(0x6DE1)},{.slot2_mapper=true}),
                   [](auto& z){return configure_floor(z);});
}
}
