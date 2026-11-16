// SPDX-License-Identifier: GPL-3.0-or-later
// Saved games, part startup (milestone 13): the cartridge-RAM check 00:07DA,
// run once when the console starts, and the erase 00:0847 it falls into when
// the check fails.
//
// What the original does. The first 64 bytes of cartridge RAM hold a header,
// a copy of a 64-byte block kept in the ROM at 0807h. At startup the game
// enables cartridge RAM and compares 32 bytes of that header (from the second
// byte) with the ROM copy. It does not trust one reading: the comparison is
// made sixteen times, and each pass that finds a difference is counted. Eight
// or more failed passes mean the cartridge RAM was never prepared (or was
// lost), and the game erases it all: one zero byte at 8000h is copied forward
// through the following bytes, then the empty saved-game window (the slot
// names, from the ROM at 3AC4h) and the header itself are written. Fewer than
// eight failed passes leave the cartridge RAM alone. Either way cartridge RAM
// is switched off again before the routine returns.
//
// How the code is cut. Every store to the cartridge control register
// ($FFFC) is the last instruction of a segment, run by the core: the machine
// remaps memory at that exact moment (the exit is declared
// `cartridge_control`). Each long copy (LDIR) is a separate, resumable entry
// (block_transfer.cpp), and the zero byte that seeds the erase is a declared
// seed store. The addresses passed to the kit are the original's, because the
// kit reads each instruction's operands and cycle cost from the player's ROM.
#include "game/scenes.h"
#include "game/menu_kit.h"
namespace psmodern::game {
void register_block_transfer(RoutineRegistry&,RoutineEntry);
namespace {
namespace mk=menu_kit;
using mk::Exit;

constexpr unsigned cartridge_store_cycles=13;   // `ld ($FFFC),a`, run by the core
constexpr unsigned seed_store_cycles=7;         // `ld (hl),l`, run by the core

// ---- The header check: 00:07DA-0806 --------------------------------------------------------------
// 00:07DA: cartridge RAM on (A = 08h, stored by the core at 07DC).
template<class Z> Exit enable_cartridge_ram(Z& z) {
    z.load_immediate(0x07DA,z.r.a);
    return mk::continues(0x07DC,cartridge_store_cycles);
}

// 00:07EE, CPI: compare A with the header byte at HL, step HL and count BC
// down. Rendered by hand because the kit has no CPI: the flags follow the core
// (Z when equal, P/V while BC is not zero; the two undocumented bits come
// from A minus the byte minus the half-borrow), MEMPTR steps by one, 16 cycles.
template<class Z> void compare_next_header_byte(Z& z) {
    z.bus().fetch(0x07EE);
    z.bus().fetch(0x07EF);
    const auto value=z.bus().read(z.hl());
    z.set_hl(static_cast<std::uint16_t>(z.hl()+1));
    const auto difference=static_cast<std::uint8_t>(z.r.a-value);
    const auto half=static_cast<std::uint8_t>((z.r.a^value^difference)&mk::half_flag);
    const auto adjusted=static_cast<std::uint8_t>(difference-(half>>4));
    z.set_bc(static_cast<std::uint16_t>(z.bc()-1));
    z.r.f=static_cast<std::uint8_t>((z.r.f&mk::carry_flag)|(difference&mk::sign_flag)|
        (difference==0 ? mk::zero_flag : 0)|half|((adjusted&2)<<4)|(adjusted&mk::x_flag)|
        (z.bc() ? mk::parity_flag : 0)|mk::subtract_flag);
    z.r.wz=static_cast<std::uint16_t>(z.r.wz+1);
    z.bus().advance(16);
}

// 00:07E2: one pass of the comparison. B counts the passes left and C the
// failed ones; both wait on the stack during the pass. 32 header bytes from
// 8001h are compared with the ROM copy, stopping at the first difference. A
// failed pass adds one to C (00:0803). The pass ends on the DJNZ at 07F6,
// which the core runs: back to 07E2 while passes are left.
template<class Z> Exit compare_header_once(Z& z) {
    z.push(0x07E2,z.r.b,z.r.c);                                      // the pass counters
    z.load_hl(0x07E3);                                               // the header in cartridge RAM
    z.load_de(0x07E6);                                               // its copy in the ROM
    z.load_bc(0x07E9);                                               // 32 bytes
    bool mismatch=false;
    for (;;) {
        z.load_a_from_pair(0x07EC,z.de());
        z.increment_pair(0x07ED,z.r.d,z.r.e);
        compare_next_header_byte(z);
        mismatch=z.relative_if(0x07F0,!z.zero());                    // a difference ends the pass
        if (mismatch || !z.jump_if(0x07F2,z.parity_even())) break;   // P/V clear: all 32 compared
    }
    if (mismatch) {
        z.pop(0x0803,z.r.b,z.r.c);
        z.increment(0x0804,z.r.c);                                   // one more failed pass
        z.relative(0x0805);
    } else z.pop(0x07F5,z.r.b,z.r.c);
    return mk::decrement_jump_exit(z,0x07F6);
}
// 00:07DF: sixteen passes (B = 10h), none failed yet (C = 0), then the first.
template<class Z> Exit start_header_check(Z& z) {
    z.load_bc(0x07DF);
    return compare_header_once(z);
}

// 00:07F8: the verdict. Eight or more failed passes take the JR NC at 07FB
// to the erase 00:0847; fewer fall through to 07FD.
template<class Z> Exit header_verdict(Z& z) {
    z.copy(0x07F8,z.r.a,z.r.c);
    z.compare_immediate(0x07F9);                                     // 8
    return mk::relative_if_exit(0x07FB,!z.carry());
}
// 00:07FD: the header is good; cartridge RAM off (A = 80h, stored at 07FF),
// then the routine returns.
template<class Z> Exit disable_cartridge_ram(Z& z) {
    z.load_immediate(0x07FD,z.r.a);
    return mk::continues(0x07FF,cartridge_store_cycles);
}

// ---- The erase: 00:0847-086E ---------------------------------------------------------------------
// 00:0847: erase cartridge RAM from 8000h. The core stores the zero seed
// (L of 8000h) at 0850; the LDIR at 0851 copies it forward over 1FFBh bytes,
// so the last four bytes of the 8 KiB are left as they were.
template<class Z> Exit erase_cartridge_ram(Z& z) {
    z.load_hl(0x0847);                                               // 8000h
    z.load_de(0x084A);                                               // 8001h
    z.load_bc(0x084D);                                               // 1FFBh
    return mk::continues(0x0850,seed_store_cycles);
}
// 00:0853: write the empty saved-game window (D8h bytes from the ROM at
// 3AC4h into 8100h: the slot names as the slot window shows them) and the
// 64-byte header (0807h into 8000h), then cartridge RAM off (stored at 086B).
template<class Z> Exit write_empty_slots_and_header(Z& z) {
    z.load_hl(0x0853);
    z.load_de(0x0856);
    z.load_bc(0x0859);
    z.block_copy(0x085C);                                            // the empty slot window
    z.load_hl(0x085E);
    z.load_de(0x0861);
    z.load_bc(0x0864);
    z.block_copy(0x0867);                                            // the header
    z.load_immediate(0x0869,z.r.a);                                  // 80h
    return mk::continues(0x086B,cartridge_store_cycles);
}
}

// ---- Registration ---------------------------------------------------------------------------------
// One entry per start address; each segment is its own unit. The pass entry
// 07E2 is where the DJNZ at 07F6 comes back to.
void register_scene_persistence(RoutineRegistry& registry) {
    auto cartridge_control_at=[](std::uint16_t at) { auto exit=mk::core_final_at(at); exit.cartridge_control=true; return exit; };
    auto add=[&](std::uint16_t at, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        const RoutineEntry entry{0,at};
        mk::add_segment(registry,entry,mk::segment(entry,std::move(exits),permissions),logic);
    };
    add(0x07DA,{cartridge_control_at(0x07DC)},{},[](auto& z) -> Exit { return enable_cartridge_ram(z); });
    add(0x07DF,{mk::conditional_at(0x07F6)},{.scratch_stack=2},[](auto& z) -> Exit { return start_header_check(z); });
    add(0x07E2,{mk::conditional_at(0x07F6)},{.scratch_stack=2},[](auto& z) -> Exit { return compare_header_once(z); });   // the DJNZ at 07F6 was taken
    add(0x07F8,{mk::conditional_at(0x07FB)},{},[](auto& z) -> Exit { return header_verdict(z); });
    add(0x07FD,{cartridge_control_at(0x07FF)},{},[](auto& z) -> Exit { return disable_cartridge_ram(z); });

    // The erase seeds cartridge RAM, so its bound needs cartridge RAM mapped
    // at 8000h (otherwise the path is not known and the segment falls back).
    const RoutineEntry erase{0,0x0847};
    auto seeded=mk::core_final_at(0x0850);
    seeded.wram_seed=0x8000;
    seeded.seed_target=SeedTarget::cartridge_ram;
    const auto erase_logic=[](auto& z) -> Exit { return erase_cartridge_ram(z); };
    registry.add(erase,mk::segment(erase,{seeded}),[=](const Machine& m) {
        if (m.memory().mapping(0x8000).kind!=Memory::Kind::cartridge_ram) return mk::uncertain_maximum;
        return mk::cost(m,erase_logic,mk::uncertain_maximum);
    },[=](RoutineContext& c) { mk::run(c,erase_logic); });
    register_block_transfer(registry,{0,0x0851});                    // the erase's copy
    add(0x0853,{cartridge_control_at(0x086B)},{},[](auto& z) -> Exit { return write_empty_slots_and_header(z); });
}
}
