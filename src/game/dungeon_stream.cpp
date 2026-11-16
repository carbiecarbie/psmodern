// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace k = map_kit;

// Both encodings start with a count. Zero terminates a stream and the high
// bit selects a literal run. Leave the sign branch to the core so the next
// transfer opens its own interrupt window.
template<class Z>
k::Exit read_count(Z& z, std::uint16_t at) {
    z.load_from_hl(at,z.r.a);
    z.or_a(at+1);
    if (z.zero()) return k::returns(at+2,k::taken_conditional_return_cycles);
    z.return_not_taken(at+2);
    return k::jump_if_exit(at+3);
}

// The room's tile words use two streams. Each pass fills every other byte.
template<class Z>
k::Exit tile_byte(Z& z, bool literal) {
    const auto at = literal ? 0x6B85 : 0x6B76;
    z.load_increment(at);
    if (!literal) z.decrement_hl(0x6B78);
    z.increment_pair(literal ? 0x6B87 : 0x6B79,z.r.d,z.r.e);
    return k::jump_if_exit(literal ? 0x6B88 : 0x6B7A);
}

template<class Z>
void combine_plane(Z& z, std::uint16_t at) {
    z.bus().fetch(at);
    z.r.a |= z.bus().read(z.hl());
    kit::logic_flags(z.r,false);
    z.bus().advance(7);
}

// Three stored planes go to the video port. Their union supplies the fourth.
// Both paths keep their original spacing between consecutive port writes.
template<class Z>
k::Exit pattern_row(Z& z, bool literal) {
    const auto at = literal ? 0x6BB5 : 0x6B98;
    z.load_from_hl(at,z.r.a);
    z.output_increment(at+1);
    z.increment(at+3,z.r.b);
    combine_plane(z,at+4);
    z.output_increment(at+5);
    z.increment(at+7,z.r.b);
    combine_plane(z,at+8);
    z.output_increment(at+9);
    if (literal) {
        z.push(0x6BC0,z.r.a,z.r.f);
        z.pop(0x6BC1,z.r.a,z.r.f);
    } else {
        for (unsigned i=0; i<3; ++i) z.decrement_hl(0x6BA3+i);
    }
    z.output(literal ? 0x6BC2 : 0x6BA6);
    return k::jump_if_exit(literal ? 0x6BC4 : 0x6BA8);
}

template<class Logic>
void add_transfer(RoutineRegistry& registry, RoutineEntry unit, std::uint16_t pc,
                  std::uint16_t branch, k::Permissions permissions, Logic logic) {
    // A game stream writes tile bytes in RAM or pattern bytes to VDP data.
    // Let the original handle incoming pointers into the mapper or an
    // unrelated port, where an individual write can change the next window.
    registry.add({1,pc},k::segment(unit,{k::conditional_at(branch)},permissions),
        [unit,logic](const Machine& machine) {
            if (unit==dungeon_tile_entries.front() ? machine.cpu().de.uint16_value>=0xFFFC
                                                  : (machine.cpu().bc.uint16_value & 255)!=0xBE)
                return k::uncertain_maximum;
            return k::cost(machine,logic,k::uncertain_maximum);
        },
        [logic](RoutineContext& context) { k::run(context,logic); });
}
}

void register_dungeon_streams(RoutineRegistry& registry) {
    const auto tiles = dungeon_tile_entries.front(), patterns = dungeon_pattern_entries.front();
    k::add_segment(registry,tiles,k::segment(tiles,{k::core_final_at(0x6B67)},{.scratch_stack=2}),
        [](auto& z) {
            z.load_immediate(0x6B62,z.r.b);
            z.load_de(0x6B64);
            return k::continues(0x6B67,k::call_cycles);
        });
    // The tile reader is also called just before the line interrupt. Its
    // first eleven cycles leave a boundary between testing and branching.
    k::add_segment(registry,{1,0x6B6E},k::segment(tiles,{k::core_final_at(0x6B6F)}),[](auto& z) {
        z.load_from_hl(0x6B6E,z.r.a);
        return k::continues(0x6B6F,4);
    });
    k::add_segment(registry,{1,0x6B70},k::segment(tiles,{k::returned_at(0x6B70),k::conditional_at(0x6B71)}),[](auto& z) {
        if (z.zero()) return k::returns(0x6B70,k::taken_conditional_return_cycles);
        z.return_not_taken(0x6B70);
        return k::jump_if_exit(0x6B71);
    });
    k::add_segment(registry,{1,0x6B6A},k::segment(tiles,{k::core_final_at(0x6B6B)}),[](auto& z) {
        z.increment_hl(0x6B6A);
        return k::continues(0x6B6B,10);
    });
    // Return to the common count reader after either kind of run. Ending at
    // the original jump leaves a separate interrupt window for the header.
    for (const std::uint16_t pc : {0x6B7D,0x6B8B}) {
        const std::uint16_t jump=pc==0x6B7D ? 0x6B7E : pc;
        auto spec=k::segment(tiles,{k::core_final_at(jump)});
        spec.single_instruction=pc==jump;
        k::add_segment(registry,{1,pc},std::move(spec),[=](auto& z) {
            if (pc==0x6B7D) z.increment_hl(pc);
            return k::continues(jump,10);
        });
    }
    for (auto pc : {0x6B74,0x6B76,0x6B81,0x6B85}) {
        const bool literal = pc>=0x6B81;
        add_transfer(registry,tiles,pc,literal ? 0x6B88 : 0x6B7A,{},[pc,literal](auto& z) {
            if (pc==0x6B81) z.and_immediate(0x6B81);
            if (pc==0x6B81 || pc==0x6B74) {
                z.copy(literal ? 0x6B83 : 0x6B74,z.r.c,z.r.a);
                z.increment_hl(literal ? 0x6B84 : 0x6B75);
            }
            return tile_byte(z,literal);
        });
    }
    for (auto pc : {0x6B8E,0x6B90}) {
        k::add_segment(registry,{1,static_cast<std::uint16_t>(pc)},
            k::segment(patterns,{k::returned_at(0x6B92),k::conditional_at(0x6B93)}),
            [pc](auto& z) {
                if (pc==0x6B8E) z.load_immediate(0x6B8E,z.r.c);
                return read_count(z,0x6B90);
            });
    }
    for (const std::uint16_t pc : {0x6BAB,0x6BC7}) {
        const std::uint16_t jump=pc==0x6BAB ? 0x6BAE : pc;
        auto spec=k::segment(patterns,{k::core_final_at(jump)});
        spec.single_instruction=pc==jump;
        k::add_segment(registry,{1,pc},std::move(spec),[=](auto& z) {
            for (auto at=pc;at<jump;++at) z.increment_hl(at);
            return k::continues(jump,10);
        });
    }
    for (auto pc : {0x6B96,0x6B98,0x6BB1,0x6BB5}) {
        const bool literal = pc>=0x6BB1;
        add_transfer(registry,patterns,pc,literal ? 0x6BC4 : 0x6BA8,
            {.scratch_stack=literal ? 2u : 0u},[pc,literal](auto& z) {
                if (pc==0x6BB1) z.and_immediate(0x6BB1);
                if (pc==0x6BB1 || pc==0x6B96) {
                    z.copy(literal ? 0x6BB3 : 0x6B96,z.r.b,z.r.a);
                    z.increment_hl(literal ? 0x6BB4 : 0x6B97);
                }
                return pattern_row(z,literal);
            });
    }
}
}
