// SPDX-License-Identifier: GPL-3.0-or-later
// World map and towns (milestone 11), part terrain: what the ground allows.
// Twenty-eight segments, rendered with game/map_kit.h from the original
// instructions:
//   01:7764  the tile attribute lookup (the map tile under a probe point)
//   01:74E4  the blocked test of a step (on foot, Land Rover, Hovercraft, ice)
//   01:756C  the ice probe and the ice breaking (its name-table update, the
//            stretch with interrupts off, stays native)
//   01:7602  the ice preset used when a full screen is built (and 01:761F)
//   01:7656, 01:76C1, 01:7732  placing the party when it gets into the Land
//            Rover or the Hovercraft and when it gets out
//
// How the ground is read. A "probe point" is a pair of offsets (table at
// 01:7A8D, two bytes per index) from the camera: the lookup adds the vertical
// offset to the camera's Y low byte (wrapping a page at C0h, which moves to
// the next pair of screens in the decoded window), the horizontal offset to
// the camera's X, and reads the tile number from the decoded screens at
// CC00h. It remembers that tile in C2E5h, switches slot 2 to bank 3 and
// returns the tile's attribute byte from a table there (a second table, 100h
// bytes on, serves areas 4 and up). The blocked test combines attributes with
// a mask that depends on how the party travels: bit 0 stops walkers, bits 1
// and 3 (mask 0Ah) stop the Hovercraft, and the Land Rover (mask 0Dh while
// getting in) cannot stand on blocked, water or ice ground.
// A segment ends at every call into another unit; the call and everything
// after it run through the machine's own dispatch, so the C++ here never
// calls the C++ of another unit by itself.
#include "game/map.h"
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace mk=map_kit;
using mk::Exit;
using mk::call_cycles;
using mk::continues;
using mk::return_cycles;
using mk::returns;

constexpr unsigned taken_return=mk::taken_conditional_return_cycles;

// ---- Tile lookup (01:7764, copied at 01:756C and 01:761F) -------------------------------
// From the `ld hl,<offset table>` at `b` up to and including `ld a,(hl)`: the
// three copies of this code differ only by their base address.
//   A = probe index; on exit A = the tile number read from the decoded screens,
//   HL points at it, C and B hold the probe point's two offsets.
template<class Z> void locate(Z& z, std::uint16_t b) {
    auto& r=z.r;
    z.load_hl(b);
    z.add_register(b+3,r.l);
    z.copy(b+4,r.l,r.a);
    z.add_with_carry(b+5,r.h);
    z.subtract_register(b+6,r.l);
    z.copy(b+7,r.h,r.a);
    z.load_from_hl(b+8,r.c);
    z.increment_hl(b+9);
    z.load_from_hl(b+0xA,r.b);
    z.load_immediate(b+0xB,r.h);
    z.load_a_direct(b+0xD);
    z.add_register(b+0x10,r.c);
    bool in_page=false;
    if (!z.relative_if(b+0x11,z.carry())) {
        z.compare_immediate(b+0x13);
        in_page=z.relative_if(b+0x15,z.carry());
    }
    if (!in_page) {                       // past the page: the next pair of screens
        z.add_immediate(b+0x17);
        z.increment(b+0x19,r.h);
        z.increment(b+0x1A,r.h);
    }
    z.and_immediate(b+0x1B);
    z.copy(b+0x1D,r.l,r.a);
    z.load_a_direct(b+0x1E);
    z.add_register(b+0x21,r.b);
    if (!z.relative_if(b+0x22,!z.carry())) z.increment(b+0x24,r.h);
    for (unsigned i=0;i<4;++i) z.rotate_right_circular_a(static_cast<std::uint16_t>(b+0x25+i));
    z.and_immediate(b+0x29);
    z.add_register(b+0x2B,r.l);
    z.copy(b+0x2C,r.l,r.a);
    z.load_from_hl(b+0x2D,r.a);
}
// From `ld (C2E5),a` at `t` (01:7792, copied at 01:75E8) to the return: remember
// the tile, switch slot 2 to bank 3, read the attribute from the table there.
template<class Z> Exit attribute(Z& z, std::uint16_t t) {
    auto& r=z.r;
    z.store_a_direct(t);
    z.load_hl(t+3);
    z.store_to_hl(t+6);
    z.load_hl(t+8);
    z.add_register(t+0xB,r.l);
    z.copy(t+0xC,r.l,r.a);
    z.add_with_carry(t+0xD,r.h);
    z.subtract_register(t+0xE,r.l);
    z.copy(t+0xF,r.h,r.a);
    z.load_a_direct(t+0x10);
    z.compare_immediate(t+0x13);
    if (!z.relative_if(t+0x15,z.carry())) z.increment(t+0x17,r.h);
    z.load_from_hl(t+0x18,r.a);
    return returns(t+0x19,return_cycles);
}

// ---- 01:7764 ------------------------------------------------------------------------------
template<class Z> Exit tile_attribute(Z& z) {
    locate(z,0x7764);
    return attribute(z,0x7792);
}

// ---- 01:74E4: the blocked test ------------------------------------------------------------
// A = probe index (0-9); returns A with the blocking bits of the point(s) the
// party's way of travel needs. HL, BC (and DE) are kept on the stack.
template<class Z> Exit blocked_pop_tail(Z& z, std::uint16_t at) {          // pop de; pop hl; pop bc; ret
    auto& r=z.r;
    z.pop(at,r.d,r.e);
    z.pop(at+1,r.h,r.l);
    z.pop(at+2,r.b,r.c);
    return returns(at+3,return_cycles);
}
template<class Z> Exit blocked_entry(Z& z) {
    auto& r=z.r;
    z.push(0x74E4,r.b,r.c);
    z.push(0x74E5,r.h,r.l);
    z.copy(0x74E6,r.c,r.a);
    z.load_a_direct(0x74E7);                // on the road
    z.or_a(0x74EA);
    if (z.relative_if(0x74EB,!z.zero())) {
        z.xor_a(0x7502);
        z.pop(0x7503,r.h,r.l);
        z.pop(0x7504,r.b,r.c);
        return returns(0x7505,return_cycles);
    }
    z.load_a_direct(0x74ED);                // transport
    z.or_a(0x74F0);
    if (!z.relative_if(0x74F1,!z.zero())) { // on foot
        z.load_immediate(0x74F3,r.b);
        z.load_hl(0x74F5);
        z.add_to_hl(0x74F8,z.bc());
        z.load_from_hl(0x74F9,r.a);
        return continues(0x74FA,call_cycles);
    }
    z.compare_immediate(0x7506);
    if (!z.relative_if(0x7508,!z.zero())) { // Land Rover: two points
        z.push(0x750A,r.d,r.e);
        z.load_immediate(0x750B,r.b);
        z.load_hl(0x750D);
        z.add_to_hl(0x7510,z.bc());
        z.exchange_de_hl(0x7511);
        z.load_a_from_pair(0x7512,z.de());
        return continues(0x7513,call_cycles);
    }
    z.compare_immediate(0x7525);
    if (!z.relative_if(0x7527,!z.zero())) { // Hovercraft: two points
        z.push(0x7529,r.d,r.e);
        z.load_immediate(0x752A,r.b);
        z.load_hl(0x752C);
        z.add_to_hl(0x752F,z.bc());
        z.exchange_de_hl(0x7530);
        z.load_a_from_pair(0x7531,z.de());
        return continues(0x7532,call_cycles);
    }
    z.push(0x7544,r.d,r.e);                 // the ship and the rest: two ice probes
    z.load_immediate(0x7545,r.b);
    z.load_hl(0x7547);
    z.add_to_hl(0x754A,z.bc());
    z.copy(0x754B,r.e,r.b);
    z.load_from_hl(0x754C,r.a);
    z.increment_hl(0x754D);
    z.load_from_hl(0x754E,r.d);
    return continues(0x754F,call_cycles);
}
// 01:74FD: the on-foot result.
template<class Z> Exit blocked_foot(Z& z) {
    auto& r=z.r;
    z.and_immediate(0x74FD);
    z.pop(0x74FF,r.h,r.l);
    z.pop(0x7500,r.b,r.c);
    return returns(0x7501,return_cycles);
}
// 01:7516 / 01:7535: the first point of a two-point test came back. A nonzero
// result ends the test; else the second point is probed.
template<class Z> Exit blocked_second_probe(Z& z, std::uint16_t first, std::uint16_t call, std::uint16_t tail) {
    auto& r=z.r;
    z.and_immediate(first);
    if (z.relative_if(first+2,!z.zero())) return blocked_pop_tail(z,tail);
    z.increment_pair(first+4,r.d,r.e);
    z.load_a_from_pair(first+5,z.de());
    return continues(call,call_cycles);
}
// 01:751F / 01:753E: the second point came back.
template<class Z> Exit blocked_second_result(Z& z, std::uint16_t at, std::uint16_t tail) {
    z.and_immediate(at);
    return blocked_pop_tail(z,tail);
}
// 01:7552: the first ice probe came back.
template<class Z> Exit ice_first_result(Z& z) {
    auto& r=z.r;
    z.and_immediate(0x7552);
    z.copy(0x7554,r.c,r.a);
    z.copy(0x7555,r.a,r.d);
    z.copy(0x7556,r.d,r.c);
    return continues(0x7557,call_cycles);
}
// 01:755A: the second ice probe came back; a break (E set by the ice probe)
// asks for the ice-breaking sound.
template<class Z> Exit ice_second_result(Z& z) {
    auto& r=z.r;
    z.and_immediate(0x755A);
    z.or_register(0x755C,r.d);
    z.push(0x755D,r.a,r.f);
    z.copy(0x755E,r.a,r.e);
    z.or_a(0x755F);
    if (!z.relative_if(0x7560,z.zero())) {
        z.load_immediate(0x7562,r.a);
        z.store_a_direct(0x7564);
    }
    z.pop(0x7567,r.a,r.f);
    z.pop(0x7568,r.d,r.e);
    z.pop(0x7569,r.h,r.l);
    z.pop(0x756A,r.b,r.c);
    return returns(0x756B,return_cycles);
}

// ---- 01:756C: the ice probe ------------------------------------------------------------------
// The tile is looked up as at 01:7764. A tile numbered D8h-DFh is ice: it is
// replaced in the decoded screens by D7h (broken ice) and the four tiles of
// the picture are rewritten in the name table (native, with interrupts off).
// Any other tile only gets its attribute read (the code at 01:75E8).
template<class Z> Exit ice_probe(Z& z) {
    auto& r=z.r;
    locate(z,0x756C);
    z.compare_immediate(0x759A);
    if (z.relative_if(0x759C,z.carry())) return attribute(z,0x75E8);
    z.compare_immediate(0x759E);
    if (z.relative_if(0x75A0,!z.carry())) return attribute(z,0x75E8);
    z.store_to_hl(0x75A2);
    z.push(0x75A4,r.d,r.e);
    z.load_a_direct(0x75A5);
    z.add_register(0x75A8,r.c);
    if (!z.relative_if(0x75A9,!z.carry())) z.add_immediate(0x75AB);
    z.compare_immediate(0x75AD);
    if (!z.relative_if(0x75AF,z.carry())) z.add_immediate(0x75B1);
    z.and_immediate(0x75B3);
    z.copy(0x75B5,r.l,r.a);
    z.load_immediate(0x75B6,r.h);
    for (std::uint16_t at:{0x75B8,0x75B9,0x75BA}) z.add_to_hl(at,z.hl());
    z.load_a_direct(0x75BB);
    z.add_register(0x75BE,r.b);
    z.rotate_right_circular_a(0x75BF);
    z.rotate_right_circular_a(0x75C0);
    z.and_immediate(0x75C1);
    z.add_register(0x75C3,r.l);
    z.copy(0x75C4,r.e,r.a);
    z.copy(0x75C5,r.a,r.h);
    z.add_immediate(0x75C6);
    z.copy(0x75C8,r.d,r.a);
    return continues(0x75C9,10);            // ld hl,nn; the DI stretch follows natively
}
// 01:75E5: after the name-table update (and the instruction after EI).
template<class Z> Exit ice_after_update(Z& z) {
    auto& r=z.r;
    z.load_immediate(0x75E5,r.a);
    z.copy(0x75E7,r.e,r.a);
    return attribute(z,0x75E8);
}

// ---- 01:7602 / 01:761F: the ice preset ---------------------------------------------------------
// With the road area (2) and the Hovercraft (0Ch), four probe points around the
// party that hold ice are turned into broken ice in the decoded screens (no
// picture update).
template<class Z> Exit preset_entry(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x7602);
    z.compare_immediate(0x7605);
    if (!z.zero()) return returns(0x7607,taken_return);
    z.return_not_taken(0x7607);
    z.load_a_direct(0x7608);
    z.compare_immediate(0x760B);
    if (!z.zero()) return returns(0x760D,taken_return);
    z.return_not_taken(0x760D);
    z.load_immediate(0x760E,r.a);
    return continues(0x7610,call_cycles);
}
template<class Z> Exit preset_point(Z& z) {                    // 01:761F
    locate(z,0x761F);
    z.compare_immediate(0x764D);
    if (z.carry()) return returns(0x764F,taken_return);
    z.return_not_taken(0x764F);
    z.compare_immediate(0x7650);
    if (!z.carry()) return returns(0x7652,taken_return);
    z.return_not_taken(0x7652);
    z.store_to_hl(0x7653);
    return returns(0x7655,return_cycles);
}
template<class Z> Exit preset_next(Z& z, std::uint16_t at, std::uint16_t call) {  // ld a,n; call 761F
    z.load_immediate(at,z.r.a);
    return continues(call,call_cycles);
}
template<class Z> Exit preset_last(Z& z) {                      // 01:761D: ld a,14h, then 01:761F
    z.load_immediate(0x761D,z.r.a);
    return preset_point(z);
}

// ---- Vehicles: placing the party (01:7656, 01:76C1, 01:7732) -------------------------------
// Each tries the party's surroundings in turn: a try is a short table of probe
// indexes (4 for a vehicle, 1 for getting out), every probe's attribute ANDed
// with the vehicle's mask; the first try with no blocking bit wins and the
// party (and the camera) move by the try's offset. All tries blocked: A = FFh.
// The try number lives in C on the stack.
struct Chain {
    std::uint16_t loop;          // push bc: the top of the tries loop
    std::uint16_t first_and;     // the `and mask` after the first probe
    std::uint16_t blocked;       // pop bc: next try
};
constexpr Chain landrover{0x7659,0x7665,0x7684};
constexpr Chain hovercraft{0x76C4,0x76D0,0x76EF};
constexpr std::uint16_t move_tail_address=0x76FD;

template<class Z> Exit chain_loop(Z& z, const Chain& c) {      // a try: ... ld a,(de); call 7764
    auto& r=z.r;
    z.push(c.loop,r.b,r.c);
    z.load_immediate(c.loop+1,r.b);
    z.load_hl(c.loop+3);
    z.add_to_hl(c.loop+6,z.bc());
    z.exchange_de_hl(c.loop+7);
    z.load_a_from_pair(c.loop+8,z.de());
    return continues(c.loop+9,call_cycles);
}
template<class Z> Exit chain_blocked(Z& z, const Chain& c) {   // pop bc; next try or A = FF
    auto& r=z.r;
    const auto b=c.blocked;
    z.pop(b,r.b,r.c);
    z.copy(b+1,r.a,r.c);
    z.add_immediate(b+2);
    z.copy(b+4,r.c,r.a);
    if (z.decrement_jump(b+5)) return chain_loop(z,c);
    z.load_immediate(b+7,r.a);
    z.or_a(b+9);
    return returns(b+10,return_cycles);
}
// Returns through `found` when the last probe of a try is clear (a try found).
template<class Z, class Found> Exit chain_after(Z& z, const Chain& c, unsigned k, Found found) {
    auto& r=z.r;
    const auto a=static_cast<std::uint16_t>(c.first_and+9*k);
    z.and_immediate(a);
    if (k<3) {
        if (z.relative_if(a+2,!z.zero())) return chain_blocked(z,c);
        z.increment_pair(a+4,r.d,r.e);
        z.load_a_from_pair(a+5,z.de());
        return continues(a+6,call_cycles);
    }
    if (z.relative_if(a+2,z.zero())) return found(z);
    return chain_blocked(z,c);
}
// 01:768F: the Land Rover's try is chosen. Try 0 (C = 0) leaves everything
// as it is; try 4 moves one tile right-down, try 8 one tile down; each moves the camera
// (and its copies in C311h/C313h).
template<class Z> Exit landrover_found(Z& z) {
    auto& r=z.r;
    z.pop(0x768F,r.b,r.c);
    z.copy(0x7690,r.a,r.c);
    z.or_a(0x7691);
    if (z.zero()) return returns(0x7692,taken_return);
    z.return_not_taken(0x7692);
    z.load_de(0x7693);
    z.compare_immediate(0x7696);
    if (!z.relative_if(0x7698,z.zero())) {
        z.compare_immediate(0x769A);
        if (!z.relative_if(0x769C,!z.zero())) z.load_de(0x769E);
        z.load_hl_direct(0x76A1);
        z.copy(0x76A4,r.a,r.l);
        z.add_immediate(0x76A5);
        z.compare_immediate(0x76A7);
        if (!z.relative_if(0x76A9,z.carry())) {
            z.add_immediate(0x76AB);
            z.increment(0x76AD,r.h);
        }
        z.copy(0x76AE,r.l,r.a);
        z.store_hl_direct(0x76AF);
        z.store_hl_direct(0x76B2);
    }
    z.load_hl_direct(0x76B5);
    z.add_to_hl(0x76B8,z.de());
    z.store_hl_direct(0x76B9);
    z.store_hl_direct(0x76BC);
    z.xor_a(0x76BF);
    return returns(0x76C0,return_cycles);
}
// 01:76FD: move the party by the offsets HL points at (plus the try's index in
// the C pushed earlier), shared by the Hovercraft and by getting out.
template<class Z> Exit move_tail(Z& z) {
    auto& r=z.r;
    z.pop(0x76FD,r.b,r.c);
    z.load_immediate(0x76FE,r.b);
    z.add_to_hl(0x7700,z.bc());
    z.load_pair_direct(0x7701,r.d,r.e);
    z.copy(0x7705,r.a,r.e);
    z.add_from_hl(0x7706);
    z.compare_immediate(0x7707);
    if (!z.relative_if(0x7709,z.carry())) {
        z.test_bit_at_hl(0x770B,7);
        if (z.relative_if(0x770D,!z.zero())) {
            z.subtract_immediate(0x7714);
            z.decrement(0x7716,r.d);
        } else {
            z.add_immediate(0x770F);
            z.increment(0x7711,r.d);
            z.relative(0x7712);
        }
    }
    z.copy(0x7717,r.e,r.a);
    z.store_pair_direct(0x7718,r.d,r.e);
    z.store_pair_direct(0x771C,r.d,r.e);
    z.increment_hl(0x7720);
    z.load_from_hl(0x7721,r.a);
    z.copy(0x7722,r.e,r.a);
    z.rotate_left_circular_a(0x7723);
    z.subtract_with_carry(0x7724,r.a);
    z.copy(0x7725,r.d,r.a);
    z.load_hl_direct(0x7726);
    z.add_to_hl(0x7729,z.de());
    z.store_hl_direct(0x772A);
    z.store_hl_direct(0x772D);
    z.xor_a(0x7730);
    return returns(0x7731,return_cycles);
}
template<class Z> Exit hovercraft_found(Z& z) {                // 01:76FA
    z.load_hl(0x76FA);
    return move_tail(z);
}

// 01:7732: getting out. The try's probe index comes from C2D7h (the facing),
// eight tries of one probe each, stepping through a table of 18h bytes.
constexpr std::uint16_t leave_loop=0x773E;
template<class Z> Exit leave_try(Z& z) {
    auto& r=z.r;
    z.push(leave_loop,r.b,r.c);
    z.load_immediate(leave_loop+1,r.b);
    z.load_hl(leave_loop+3);
    z.add_to_hl(leave_loop+6,z.bc());
    z.load_from_hl(leave_loop+7,r.a);
    return continues(leave_loop+8,call_cycles);
}
template<class Z> Exit leave_entry(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x7732);
    z.and_immediate(0x7735);
    z.copy(0x7737,r.c,r.a);
    z.add_register(0x7738,r.a);
    z.add_register(0x7739,r.c);
    z.add_register(0x773A,r.a);
    z.copy(0x773B,r.c,r.a);
    z.load_immediate(0x773C,r.b);
    return leave_try(z);
}
template<class Z> Exit leave_after(Z& z) {                     // 01:7749
    auto& r=z.r;
    z.and_immediate(0x7749);
    if (z.relative_if(0x774B,z.zero())) {
        z.load_hl(0x775E);
        (void)z.jump(0x7761);
        return move_tail(z);
    }
    z.pop(0x774D,r.b,r.c);
    z.copy(0x774E,r.a,r.c);
    z.add_immediate(0x774F);
    z.compare_immediate(0x7751);
    if (!z.relative_if(0x7753,z.carry())) z.subtract_immediate(0x7755);
    z.copy(0x7757,r.c,r.a);
    if (z.decrement_jump(0x7758)) return leave_try(z);
    z.load_immediate(0x775A,r.a);
    z.or_a(0x775C);
    return returns(0x775D,return_cycles);
}
}

void register_map_terrain(RoutineRegistry& registry) {
    auto add=[&](RoutineEntry unit, std::uint16_t entry, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        mk::add_segment(registry,map::at1(entry),mk::segment(unit,std::move(exits),permissions),logic);
    };
    const auto F=[](std::uint16_t a,int s=0) { return mk::core_final_at(a,static_cast<std::int8_t>(s)); };
    const auto R=[](std::uint16_t a,int s=0) { return mk::returned_at(a,static_cast<std::int8_t>(s)); };

    // 01:7764
    add(map::tile_attribute_unit,0x7764,{R(0x77AB)},{.slot2_mapper=true},[](auto& z) { return tile_attribute(z); });

    // 01:74E4: the blocked test
    const auto blocked=map::blocked_unit;
    add(blocked,0x74E4,{F(0x74FA,-4),F(0x7513,-6),F(0x7532,-6),F(0x754F,-6),R(0x7505)},{.scratch_stack=6},
        [](auto& z) { return blocked_entry(z); });
    add(blocked,0x74FD,{R(0x7501,4)},{.inherited_stack=4},[](auto& z) { return blocked_foot(z); });
    add(blocked,0x7516,{F(0x751C),R(0x7524,6)},{.inherited_stack=6},
        [](auto& z) { return blocked_second_probe(z,0x7516,0x751C,0x7521); });
    add(blocked,0x751F,{R(0x7524,6)},{.inherited_stack=6},[](auto& z) { return blocked_second_result(z,0x751F,0x7521); });
    add(blocked,0x7535,{F(0x753B),R(0x7543,6)},{.inherited_stack=6},
        [](auto& z) { return blocked_second_probe(z,0x7535,0x753B,0x7540); });
    add(blocked,0x753E,{R(0x7543,6)},{.inherited_stack=6},[](auto& z) { return blocked_second_result(z,0x753E,0x7540); });
    add(blocked,0x7552,{F(0x7557)},{.inherited_stack=6},[](auto& z) { return ice_first_result(z); });
    add(blocked,0x755A,{R(0x756B,6)},{.scratch_stack=2,.inherited_stack=6},[](auto& z) { return ice_second_result(z); });

    // 01:756C: the ice probe
    add(map::ice_probe_unit,0x756C,{F(0x75C9,-2),R(0x7601)},{.slot2_mapper=true,.scratch_stack=2},
        [](auto& z) { return ice_probe(z); });
    add(map::ice_probe_unit,0x75E5,{R(0x7601)},{.slot2_mapper=true},[](auto& z) { return ice_after_update(z); });

    // 01:7602: the ice preset
    const auto preset=map::ice_preset_unit;
    add(preset,0x7602,{R(0x7607),R(0x760D),F(0x7610)},{},[](auto& z) { return preset_entry(z); });
    add(preset,0x7613,{F(0x7615)},{},[](auto& z) { return preset_next(z,0x7613,0x7615); });
    add(preset,0x7618,{F(0x761A)},{},[](auto& z) { return preset_next(z,0x7618,0x761A); });
    add(preset,0x761D,{R(0x764F),R(0x7652),R(0x7655)},{},[](auto& z) { return preset_last(z); });
    add(preset,0x761F,{R(0x764F),R(0x7652),R(0x7655)},{},[](auto& z) { return preset_point(z); });

    // 01:7656: the Land Rover
    const auto lr=map::landrover_unit;
    add(lr,0x7656,{F(0x7662,-2)},{.scratch_stack=2},[](auto& z) {
        z.load_bc(0x7656);
        return chain_loop(z,landrover);
    });
    for (unsigned k=0;k<3;++k) {
        const auto entry=static_cast<std::uint16_t>(0x7665+9*k);
        add(lr,entry,{F(static_cast<std::uint16_t>(0x766B+9*k)),F(0x7662),R(0x768E,2)},{.inherited_stack=2},
            [k](auto& z) { return chain_after(z,landrover,k,[](auto&) -> Exit { throw std::logic_error("unreachable"); }); });
    }
    add(lr,0x7680,{F(0x7662),R(0x768E,2),R(0x7692,2),R(0x76C0,2)},{.inherited_stack=2},
        [](auto& z) { return chain_after(z,landrover,3,[](auto& zz) { return landrover_found(zz); }); });

    // 01:76C1: the Hovercraft
    const auto hv=map::hovercraft_unit;
    add(hv,0x76C1,{F(0x76CD,-2)},{.scratch_stack=2},[](auto& z) {
        z.load_bc(0x76C1);
        return chain_loop(z,hovercraft);
    });
    for (unsigned k=0;k<3;++k) {
        const auto entry=static_cast<std::uint16_t>(0x76D0+9*k);
        add(hv,entry,{F(static_cast<std::uint16_t>(0x76D6+9*k)),F(0x76CD),R(0x76F9,2)},{.inherited_stack=2},
            [k](auto& z) { return chain_after(z,hovercraft,k,[](auto&) -> Exit { throw std::logic_error("unreachable"); }); });
    }
    add(hv,0x76EB,{F(0x76CD),R(0x76F9,2),R(0x7731,2)},{.inherited_stack=2},
        [](auto& z) { return chain_after(z,hovercraft,3,[](auto& zz) { return hovercraft_found(zz); }); });

    // 01:7732: getting out
    const auto lv=map::leave_vehicle_unit;
    add(lv,0x7732,{F(0x7746,-2)},{.scratch_stack=2},[](auto& z) { return leave_entry(z); });
    add(lv,0x7749,{F(0x7746),R(0x775D,2),R(0x7731,2)},{.inherited_stack=2},[](auto& z) { return leave_after(z); });
}
}
