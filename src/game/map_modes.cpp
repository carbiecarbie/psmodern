// SPDX-License-Identifier: GPL-3.0-or-later
// World map and towns (milestone 11), part modes:
// The map loop 00:0B08 (00:0B14 is a segment of its own), map loading 00:0B6A (its three
// DI stretches are segments of their own), 00:0C97, 00:0CA6 (00:0CB3 and 00:0CE3), 00:02FD and the
// road 00:0E8B and 00:0ED7.
// Planned segments: map::mode_segments in game/map.h, rendered with game/map_kit.h.
//
// What these routines do, in the order the game reaches them.
//
// The map loop (00:0B08, game mode 9) runs once per frame. It handles the pause,
// waits for the frame, lets the object engine and the tile check run (the two
// bare calls at 00:0B14 and 00:0B17: the host opens Options and Language at
// 00:0B14, so the loop's pieces end before it and restart after it), then asks
// whether a random battle is due. A battle (or a button press while the party
// is free to act) sets the next game mode, remembers the leader's object type
// and clears the party-state block and the object slots.
//
// Map loading (00:0B6A, game mode 8) fades out, steps the mode to 9, picks one
// of two tile sets by the area number, decompresses it, builds the party
// objects (00:0CA6), runs four frame waits with the party frame upload between
// them (that is the stretch with interrupts off, a segment of its own), looks the
// location up in the area tables, copies the area's records into the work
// areas, builds the screen window and the name table, reads the tile the party
// stands on, asks the music request routine (00:0C97) for the area's tune and
// clears the sprite tables (00:02FD) before fading in.
//
// The road (game modes 0Eh and 0Fh) is the walk between towns: 00:0ED7 loads
// the road leg from the table, pushes a return target for the transfer code
// and calls the loader; 00:0E8B scripts the held buttons for the leg and, when
// it ends or a button is pressed, sends the party back to loading a map.
#include "game/joins.h"
#include "game/map.h"
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace mk=game::map_kit;
using mk::Exit;

// ---- The map loop (00:0B08-0B69) --------------------------------------------------------
// 00:0B41: a battle or a button press ends the mode. A holds the value saved.
template<class Z> void set_next_mode(Z& z) {
    z.store_to_hl(0x0B47);                     // the next game mode
    z.load_a_direct(0x0B49);
    z.store_a_direct(0x0B4C);
    z.load_hl(0x0B4F);
    z.load_de(0x0B52);
}
template<class Z> void object_fill_pointers(Z& z) {
    z.load_hl(0x0B5C);
    z.load_de(0x0B5F);
}
template<class Z> Exit leave_map(Z& z) {
    z.store_a_direct(0x0B41);
    z.load_hl(0x0B44);
    set_next_mode(z);
    z.load_bc(0x0B55);
    z.store_to_hl(0x0B58);
    z.block_copy(0x0B5A);                      // clear the party-state block
    object_fill_pointers(z);
    z.load_bc(0x0B62);
    z.store_to_hl(0x0B65);
    z.block_copy(0x0B67);                      // clear the object slots
    return z.final_return(0x0B69);
}
// 00:0B3B: a button press ends the mode only while the party is free to act;
// otherwise the loop returns (the exit), else A is cleared and the mode changes.
template<class Z> std::optional<Exit> button_blocked(Z& z) {
    z.load_a_direct(0x0B3B);
    z.or_a(0x0B3E);
    if (auto exit=z.final_return_if(0x0B3F,mk::Condition::nz)) return exit;
    z.xor_a(0x0B40);
    return std::nullopt;
}
// 00:0B35: the buttons, when no battle is due.
template<class Z> Exit buttons(Z& z) {
    z.load_a_direct(0x0B35);
    z.and_immediate(0x0B38);
    if (auto exit=z.final_return_if(0x0B3A,mk::Condition::z)) return *exit;
    if (auto exit=button_blocked(z)) return *exit;
    return leave_map(z);
}
// 00:0B1A, after the tile check returns.
template<class Z> Exit battle_gate(Z& z) {
    z.load_a_direct(0x0B1A);
    z.or_a(0x0B1D);
    if (z.relative_if(0x0B1E,!z.zero())) return buttons(z);
    z.load_a_direct(0x0B20);
    z.or_a(0x0B23);
    if (z.relative_if(0x0B24,z.zero())) return buttons(z);
    z.xor_a(0x0B26);
    z.store_a_direct(0x0B27);
    return z.final_call(0x0B2A);               // the random-battle check
}
// 00:0B2D, after the random-battle check returns.
template<class Z> Exit after_battle_check(Z& z) {
    z.or_a(0x0B2D);
    if (z.relative_if(0x0B2E,z.zero())) return buttons(z);
    z.load_immediate(0x0B30,z.r.a);
    (void)z.jump(0x0B32);
    return leave_map(z);
}

// ---- Map loading (00:0B6A-0C94) ---------------------------------------------------------
// One wait of the four-frame loop (00:0BAE-0BB1); the stretch with interrupts
// off that follows it is a segment of its own.
template<class Z> Exit frame_wait(Z& z) {
    z.push(0x0BAE,z.r.b,z.r.c);
    z.load_immediate(0x0BAF,z.r.a);
    return z.final_call(0x0BB1);
}
// 00:0B75: the mode steps on, flags reset, and the tile set of the area is
// loaded through the slot-2 bank.
template<class Z> Exit start_loading(Z& z) {
    auto& r=z.r;
    z.increment_at_hl(0x0B75);
    z.xor_a(0x0B76);
    z.store_a_direct(0x0B77);
    z.store_a_direct(0x0B7A);
    z.increment(0x0B7D,r.a);
    z.store_a_direct(0x0B7E);
    z.load_a_direct(0x0B81);
    z.compare_immediate(0x0B84);
    if (z.relative_if(0x0B86,!z.carry())) {    // the second tile set
        z.load_hl(0x0B98);
        z.store_to_hl(0x0B9B);
        z.load_hl(0x0B9D);
        z.load_de(0x0BA0);
        return z.final_call(0x0BA3);
    }
    z.load_hl(0x0B88);
    z.store_to_hl(0x0B8B);
    z.load_hl(0x0B8D);
    z.load_de(0x0B90);
    return z.final_call(0x0B93);
}
// One "ld e,(hl); ld d,n; ld (nn),de" field of an area record, after the HL step.
template<class Z> void area_field(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.increment_hl(at);
    z.load_from_hl(at+1,r.e);
    z.load_immediate(at+2,r.d);
    z.store_pair_direct(at+4,r.d,r.e);
}
// 00:0BBA, after the frame upload: the djnz either waits again or the area
// tables are read and the work areas filled, ending at the screen window call.
template<class Z> Exit read_area(Z& z) {
    auto& r=z.r;
    if (z.decrement_jump(0x0BBA)) return frame_wait(z);
    z.load_a_direct(0x0BBC);
    z.negate(0x0BBF);
    z.store_a_direct(0x0BC1);
    z.load_a_direct(0x0BC4);
    z.store_a_direct(0x0BC7);
    z.load_a_direct(0x0BCA);
    z.copy(0x0BCD,r.e,r.a);
    z.load_immediate(0x0BCE,r.d);
    z.load_hl(0x0BD0);
    z.add_to_hl(0x0BD3,z.de());
    z.load_from_hl(0x0BD4,r.a);
    z.store_a_direct(0x0BD5);
    z.add_register(0x0BD8,r.a);
    z.copy(0x0BD9,r.h,r.a);
    z.add_register(0x0BDA,r.a);
    z.copy(0x0BDB,r.l,r.a);
    z.add_register(0x0BDC,r.a);
    z.add_register(0x0BDD,r.l);
    z.add_register(0x0BDE,r.h);
    z.copy(0x0BDF,r.l,r.a);
    z.load_immediate(0x0BE0,r.h);
    z.load_de(0x0BE2);
    z.add_to_hl(0x0BE5,z.de());
    z.load_from_hl(0x0BE6,r.e);
    z.increment_hl(0x0BE7);
    z.load_from_hl(0x0BE8,r.d);
    z.increment_hl(0x0BE9);
    z.store_pair_direct(0x0BEA,r.d,r.e);
    z.load_from_hl(0x0BEE,r.a);
    z.store_a_direct(0x0BEF);
    for (std::uint16_t at=0x0BF2;at<=0x0C1A;at+=8) area_field(z,at);
    z.increment_hl(0x0C22);
    z.load_from_hl(0x0C23,r.a);
    z.store_a_direct(0x0C24);
    z.increment_hl(0x0C27);
    z.load_from_hl(0x0C28,r.a);
    z.increment_hl(0x0C29);
    z.push(0x0C2A,r.h,r.l);
    z.load_from_hl(0x0C2B,r.h);
    z.copy(0x0C2C,r.l,r.a);
    z.load_de(0x0C2D);
    z.load_bc(0x0C30);
    z.block_copy(0x0C33);
    z.load_a_direct(0x0C35);
    z.or_a(0x0C38);
    if (z.relative_if(0x0C39,!z.zero())) {     // on a vehicle: the other table
        z.load_hl(0x0C4C);
        z.load_bc(0x0C4F);
        z.block_copy(0x0C52);
    } else {
        z.push(0x0C3B,r.h,r.l);
        z.load_hl(0x0C3C);
        z.load_bc(0x0C3F);
        z.block_copy(0x0C42);
        z.pop(0x0C44,r.h,r.l);
        z.load_increment(0x0C45);
        z.load_increment(0x0C47);
        (void)z.jump(0x0C49);
    }
    z.pop(0x0C54,r.h,r.l);
    z.increment_hl(0x0C55);
    z.load_from_hl(0x0C56,r.a);
    z.increment_hl(0x0C57);
    z.load_from_hl(0x0C58,r.h);
    z.copy(0x0C59,r.l,r.a);
    z.store_hl_direct(0x0C5A);
    return z.final_call(0x0C5D);               // the screen window
}
// 00:0C68, after the tile attribute lookup returns: the interaction type, then
// the tune of the area, vehicle or ship (00:0C97 sets it).
template<class Z> Exit choose_tune(Z& z) {
    auto& r=z.r;
    for (std::uint16_t at=0x0C68;at<=0x0C6B;++at) z.rotate_right_circular_a(at);
    z.and_immediate(0x0C6C);
    z.store_a_direct(0x0C6E);
    z.load_a_direct(0x0C71);
    z.compare_immediate(0x0C74);
    z.load_immediate(0x0C76,r.c);
    if (!z.relative_if(0x0C78,!z.carry())) {
        z.or_a(0x0C7A);
        z.load_immediate(0x0C7B,r.c);
        if (!z.relative_if(0x0C7D,!z.zero())) {
            z.load_a_direct(0x0C7F);
            z.copy(0x0C82,r.e,r.a);
            z.load_immediate(0x0C83,r.d);
            z.load_hl(0x0C85);
            z.add_to_hl(0x0C88,z.de());
            z.load_from_hl(0x0C89,r.c);
        }
    }
    z.copy(0x0C8A,r.a,r.c);
    return z.final_call(0x0C8B);
}

// ---- The tune request (00:0C97-0CA5) -----------------------------------------------------
// The request is written only when it differs from the one already playing.
template<class Z> Exit tune_request(Z& z) {
    auto& r=z.r;
    z.push(0x0C97,r.h,r.l);
    z.load_hl(0x0C98);
    z.compare_from_hl(0x0C9B);
    if (z.relative_if(0x0C9C,!z.zero())) {
        z.store_a_direct(0x0CA0);
        z.store_register_to_hl(0x0CA3,r.a);
        z.pop(0x0CA4,r.h,r.l);
        return z.final_return(0x0CA5);
    }
    z.pop(0x0C9E,r.h,r.l);
    return z.final_return(0x0C9F);
}

// ---- The party objects (00:0CA6-0CEA) -----------------------------------------------------
// 00:0CE3: a character in the party puts its object type in the slot and the
// slot pointer moves on (BC = slot size).
template<class Z> Exit party_member(Z& z) {
    z.test_bit_at_hl(0x0CE3,0);
    if (auto exit=z.final_return_if(0x0CE5,mk::Condition::z)) return *exit;
    z.store_a_to_pair(0x0CE6,z.de());
    z.exchange_de_hl(0x0CE7);
    z.add_to_hl(0x0CE8,z.bc());
    z.exchange_de_hl(0x0CE9);
    return z.final_return(0x0CEA);
}
// The first fill (255 bytes, about 5,400 cycles) is not in one segment: as one segment
// it was refused on about one call in eight (an interrupt inside its bound), so
// the segment ends at 00:0CAC and the next one starts after the fill, 00:0CB3.
template<class Z> Exit clear_slots(Z& z) {
    z.load_hl(0x0CA6);
    z.load_de(0x0CA9);
    z.load_bc(0x0CAC);
    return mk::fall_through(0x0CAC,0x0CAF);    // the fill follows
}
template<class Z> Exit build_slots(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x0CB3);
    z.or_a(0x0CB6);
    if (z.jump_if(0x0CB7,z.zero())) {          // on foot: one object per party member
        z.load_de(0x0CC0);
        z.load_bc(0x0CC3);
        z.load_hl(0x0CC6);
        z.load_immediate(0x0CC9,r.a);
        return z.final_call(0x0CCB);
    }
    z.load_hl(0x0CBA);                         // on a vehicle: the one vehicle object
    z.store_to_hl(0x0CBD);
    return z.final_return(0x0CBF);
}
template<class Z> Exit member_call(Z& z, std::uint16_t at) {
    z.load_hl(at);
    z.load_immediate(at+3,z.r.a);
    return z.final_call(at+5);
}
template<class Z> Exit last_member(Z& z) {    // 00:0CDE falls into 00:0CE3
    z.load_hl(0x0CDE);
    z.load_immediate(0x0CE1,z.r.a);
    return party_member(z);
}

// ---- The sprite tables (00:02FD) -------------------------------------------------------------
// The loader can arrive with an interrupt pending. This existing 00:0306 entry
// covers the first fill when execution reaches it after the setup before it; a
// separate common entry at 00:02FD covers setup and the fill when admitted.
// The common 00:030E entry executes the second fill in bounded slices.
template<class Z> Exit clear_sprites(Z& z) {
    z.store_to_hl(0x0306);                     // the hidden-sprite marker
    z.block_copy(0x0308);
    z.load_immediate(0x030A,z.r.c);
    return mk::fall_through(0x030A,0x030C);    // the second fill follows
}

// ---- The road (00:0E8B-0F0B) -----------------------------------------------------------------
// 00:0EB4: the leg is over (or a button was pressed): back to loading a map.
template<class Z> Exit leave_road(Z& z) {
    auto& r=z.r;
    z.load_hl(0x0EB4);
    z.store_to_hl(0x0EB7);
    z.load_a_direct(0x0EB9);
    for (std::uint16_t at=0x0EBC;at<=0x0EBE;++at) z.add_register(at,r.a);
    z.copy(0x0EBF,r.l,r.a);
    z.load_immediate(0x0EC0,r.h);
    z.load_de(0x0EC2);
    z.add_to_hl(0x0EC5,z.de());
    z.xor_a(0x0EC6);
    z.store_a_direct(0x0EC7);
    z.store_a_direct(0x0ECA);
    z.store_a_direct(0x0ECD);
    z.store_a_direct(0x0ED0);
    return z.final_call(0x0ED3);                // the transfer code returns past 00:0ED6
}
template<class Z> Exit road_pause(Z& z) {
    z.load_a_direct(0x0E8B);
    z.or_a(0x0E8E);
    return z.final_call_if(0x0E8F,mk::Condition::nz);
}
template<class Z> Exit road_wait(Z& z) {
    z.load_immediate(0x0E92,z.r.a);
    return z.final_call(0x0E94);
}
// 00:0E97: a button press ends the leg at once; otherwise the scripted buttons
// replace the held ones and the object engine runs.
template<class Z> Exit road_input(Z& z) {
    z.load_a_direct(0x0E97);
    z.and_immediate(0x0E9A);
    if (z.relative_if(0x0E9C,!z.zero())) return leave_road(z);
    z.load_a_direct(0x0E9E);
    z.store_a_direct(0x0EA1);
    return z.final_call(0x0EA4);
}
// 00:0EA7: after the object engine.
template<class Z> Exit road_step(Z& z) {
    z.load_a_direct(0x0EA7);
    z.or_a(0x0EAA);
    if (auto exit=z.final_return_if(0x0EAB,mk::Condition::nz)) return *exit;
    z.load_a_direct(0x0EAC);
    z.decrement(0x0EAF,z.r.a);
    z.store_a_direct(0x0EB0);
    if (auto exit=z.final_return_if(0x0EB3,mk::Condition::nz)) return *exit;
    return leave_road(z);
}
// 00:0ED7: the leg's script is read from the table and the return target
// 00:0EF5 is pushed for the transfer code's pop-and-return.
template<class Z> Exit load_road(Z& z) {
    auto& r=z.r;
    z.load_a_direct(0x0ED7);
    for (std::uint16_t at=0x0EDA;at<=0x0EDC;++at) z.add_register(at,r.a);
    z.copy(0x0EDD,r.l,r.a);
    z.load_immediate(0x0EDE,r.h);
    z.load_de(0x0EE0);
    z.add_to_hl(0x0EE3,z.de());
    z.load_from_hl(0x0EE4,r.a);
    z.store_a_direct(0x0EE5);
    z.increment_hl(0x0EE8);
    z.load_from_hl(0x0EE9,r.a);
    z.store_a_direct(0x0EEA);
    z.increment_hl(0x0EED);
    z.load_de(0x0EEE);
    z.push(0x0EF1,r.d,r.e);
    return z.final_call(0x0EF2);
}
// 00:0EF8: after the map is loaded.
template<class Z> Exit after_road_load(Z& z) {
    z.load_hl(0x0EF8);
    z.load_de(0x0EFB);
    z.load_bc(0x0EFE);
    z.store_to_hl(0x0F01);
    z.block_copy(0x0F03);
    z.load_hl(0x0F05);
    z.store_hl_direct(0x0F08);
    return z.final_return(0x0F0B);
}
}

// A frame edge can split the long walking-mode exit inside the button test.
// Keep the two overlapping fills interruptible when resuming at that point.
void register_map_button_resume(RoutineRegistry& registry) {
    const auto unit=map::map_loop_unit;
    mk::add_segment(registry,map::at0(0x0B3B),mk::segment(unit,{mk::returned_at(0x0B3F),mk::core_final_at(0x0B44)}),
        [](auto& z) -> Exit {
            if (auto exit=button_blocked(z)) return *exit;
            z.store_a_direct(0x0B41);
            z.load_hl(0x0B44);
            return mk::fall_through(0x0B44,0x0B47);
        });
    mk::add_segment(registry,map::at0(0x0B47),mk::segment(unit,{mk::core_final_at(0x0B55)}),
        [](auto& z) -> Exit {
            set_next_mode(z);
            z.load_bc(0x0B55);
            return mk::fall_through(0x0B55,0x0B58);
        });
    mk::add_segment(registry,map::at0(0x0B5C),mk::segment(unit,{mk::core_final_at(0x0B62)}),
        [](auto& z) -> Exit {
            object_fill_pointers(z);
            z.load_bc(0x0B62);
            return mk::fall_through(0x0B62,0x0B65);
        });
    register_block_transfer(registry,map::at0(0x0B5A));
    register_block_transfer(registry,map::at0(0x0B67));
}

void register_map_modes(RoutineRegistry& registry) {
    namespace m=map;
    using mk::core_final_at;
    using mk::returned_at;
    auto add=[&](RoutineEntry entry, RoutineEntry unit, std::vector<SegmentExit> exits, mk::Permissions permissions, auto logic) {
        mk::add_segment(registry,entry,mk::segment(unit,std::move(exits),permissions),logic);
    };
    // The two clearing stretches of the walking mode's exit start with a seed
    // store, then the block copy (joining segments, milestone 14).
    for (const std::uint16_t at:{0x0B58,0x0B65}) {
        auto spec=mk::segment(m::map_loop_unit,{core_final_at(at)});
        spec.single_instruction=true;
        mk::add_segment(registry,m::at0(at),std::move(spec),[at](auto& z) -> Exit {
            z.store_to_hl(at);
            return mk::fall_through(at,static_cast<std::uint16_t>(at+2));
        });
    }
    // Map loop.
    add(m::at0(0x0B08),m::map_loop_unit,{core_final_at(0x0B0C)},{},
        [](auto& z) -> Exit { z.load_a_direct(0x0B08); z.or_a(0x0B0B); return z.final_call_if(0x0B0C,mk::Condition::nz); });
    add(m::at0(0x0B0F),m::map_loop_unit,{core_final_at(0x0B11)},{},
        [](auto& z) -> Exit { z.load_immediate(0x0B0F,z.r.a); return z.final_call(0x0B11); });
    add(m::at0(0x0B1A),m::map_loop_unit,{core_final_at(0x0B2A),returned_at(0x0B3A),returned_at(0x0B3F),returned_at(0x0B69)},{},
        [](auto& z) { return battle_gate(z); });
    add(m::at0(0x0B2D),m::map_loop_unit,{returned_at(0x0B3A),returned_at(0x0B3F),returned_at(0x0B69)},{},
        [](auto& z) { return after_battle_check(z); });
    // Map loading.
    add(m::at0(0x0B75),m::load_map_unit,{core_final_at(0x0B93),core_final_at(0x0BA3)},{.slot2_mapper=true},
        [](auto& z) { return start_loading(z); });
    add(m::at0(0x0B96),m::load_map_unit,{core_final_at(0x0BA6)},{},
        [](auto& z) -> Exit { z.relative(0x0B96); return z.final_call(0x0BA6); });
    add(m::at0(0x0BAC),m::load_map_unit,{core_final_at(0x0BB1,-2)},{.scratch_stack=2},
        [](auto& z) { z.load_immediate(0x0BAC,z.r.b); return frame_wait(z); });
    add(m::at0(0x0BBA),m::load_map_unit,{core_final_at(0x0BB1,-2),core_final_at(0x0C5D)},{.scratch_stack=4},
        [](auto& z) { return read_area(z); });
    add(m::at0(0x0C63),m::load_map_unit,{core_final_at(0x0C65)},{},
        [](auto& z) -> Exit { z.load_immediate(0x0C63,z.r.a); return z.final_call(0x0C65); });
    add(m::at0(0x0C68),m::load_map_unit,{core_final_at(0x0C8B)},{},
        [](auto& z) { return choose_tune(z); });
    // Joining stretches of the map loader: the calls that follow a return
    // into the loader's own code (the party objects, the object engine, the
    // mirror build), each one instruction.
    auto call_here=[&](std::uint16_t at,RoutineEntry unit) {
        auto spec=mk::segment(unit,{core_final_at(at)},{.scratch_stack=2});
        spec.single_instruction=true;
        mk::add_segment(registry,m::at0(at),std::move(spec),[at](auto& z) -> Exit { return z.final_call(at); });
    };
    // The instructions that follow the loader's interrupt-off stretches: the
    // mode byte's address after the first one, the party frame counter restored
    // after each frame upload, and the jump to the sprite clearing after the
    // video register write.
    {
        auto spec=mk::segment(m::load_map_unit,{core_final_at(0x0B72)});
        spec.single_instruction=true;
        mk::add_segment(registry,m::at0(0x0B72),std::move(spec),[](auto& z) -> Exit {
            z.load_hl(0x0B72);
            return mk::fall_through(0x0B72,0x0B75);
        });
        auto restore=mk::segment(m::load_map_unit,{core_final_at(0x0BB9)});
        restore.single_instruction=true;
        mk::add_segment(registry,m::at0(0x0BB9),std::move(restore),[](auto& z) -> Exit {
            z.pop(0x0BB9,z.r.b,z.r.c);
            return mk::fall_through(0x0BB9,0x0BBA);
        });
        auto leave=mk::segment(m::load_map_unit,{core_final_at(0x0C94)});
        leave.single_instruction=true;
        mk::add_segment(registry,m::at0(0x0C94),std::move(leave),[](auto& z) -> Exit { return z.final_jump(0x0C94); });
    }
    call_here(0x0BA6,m::load_map_unit);
    call_here(0x0BA9,m::load_map_unit);
    call_here(0x0C60,m::load_map_unit);
    // The tune request, the party objects and the sprite tables.
    add(m::map_sound_unit,m::map_sound_unit,{returned_at(0x0C9F),returned_at(0x0CA5)},{.scratch_stack=2},
        [](auto& z) { return tune_request(z); });
    add(m::party_slots_unit,m::party_slots_unit,{core_final_at(0x0CAC)},{},
        [](auto& z) { return clear_slots(z); });
    {
        // The first fill starts with its seed store, then the block copy.
        auto spec=mk::segment(m::party_slots_unit,{core_final_at(0x0CAF)});
        spec.single_instruction=true;
        mk::add_segment(registry,m::at0(0x0CAF),std::move(spec),[](auto& z) -> Exit {
            z.store_to_hl(0x0CAF);
            return mk::fall_through(0x0CAF,0x0CB1);
        });
    }
    add(m::at0(0x0CB3),m::party_slots_unit,{returned_at(0x0CBF),core_final_at(0x0CCB)},{},
        [](auto& z) { return build_slots(z); });
    add(m::at0(0x0CCE),m::party_slots_unit,{core_final_at(0x0CD3)},{},
        [](auto& z) { return member_call(z,0x0CCE); });
    add(m::at0(0x0CD6),m::party_slots_unit,{core_final_at(0x0CDB)},{},
        [](auto& z) { return member_call(z,0x0CD6); });
    add(m::at0(0x0CDE),m::party_slots_unit,{returned_at(0x0CE5),returned_at(0x0CEA)},{},
        [](auto& z) { return last_member(z); });
    add(m::at0(0x0CE3),m::party_slots_unit,{returned_at(0x0CE5),returned_at(0x0CEA)},{},
        [](auto& z) { return party_member(z); });
    add(m::at0(0x0306),m::sprite_clear_unit,{core_final_at(0x030A)},{},
        [](auto& z) { return clear_sprites(z); });
    // The road.
    add(m::road_unit,m::road_unit,{core_final_at(0x0E8F)},{},
        [](auto& z) { return road_pause(z); });
    add(m::at0(0x0E92),m::road_unit,{core_final_at(0x0E94)},{},
        [](auto& z) { return road_wait(z); });
    add(m::at0(0x0E97),m::road_unit,{core_final_at(0x0EA4),core_final_at(0x0ED3)},{},
        [](auto& z) { return road_input(z); });
    add(m::at0(0x0EA7),m::road_unit,{returned_at(0x0EAB),returned_at(0x0EB3),core_final_at(0x0ED3)},{},
        [](auto& z) { return road_step(z); });
    add(m::load_road_unit,m::load_road_unit,{core_final_at(0x0EF2,-2)},{.scratch_stack=2},
        [](auto& z) { return load_road(z); });
    call_here(0x0EF5,m::load_road_unit);
    add(m::at0(0x0EF8),m::load_road_unit,{returned_at(0x0F0B)},{},
        [](auto& z) { return after_road_load(z); });
}

// Joining instructions of this file's routines that no input recording reaches
// (milestone 14, Part I): each place the machine continues at after one of the
// file's segments ends (the return point of a call, a branch target, a
// code-table target) begins a one-instruction segment of its own, so the game
// runs on in C++ there too. Found by the static walk of every declared exit
// and checked by lockstep probes (tests/joins_tests.cpp).
void register_map_modes_walk_joins(RoutineRegistry& registry) {
    kit::add_joining(registry,{0,0x0ED6},{0,0x0E8B},{kit::returned_at(0x0ED6)},kit::return_cycles,0,[](auto& z) { return z.final_return(0x0ED6); });
}
}
