// SPDX-License-Identifier: GPL-3.0-or-later
// World map and towns (milestone 11): the rewritten units, their planned
// segment entries per part, the code each part may register in, and the
// instructions left out of the segments (none since milestone 14). Named constants only; every table byte is
// read from the player's ROM at run time.
//
// The six parts each own one source file and register only there:
//   video     map_video.cpp      the picture's work in the frame interrupt
//   scroll    map_scroll.cpp     steps, the camera, the screen window and strips
//   objects   map_objects.cpp    the object engine and the map's object types
//   terrain   map_terrain.cpp    what the ground allows, vehicles and ice
//   entrances map_entrances.cpp  entrances, transfers and special ground
//   modes     map_modes.cpp      the map loop, map loading and the road
// The planned lists below are the segment design; a part may register an
// entry that is not listed only inside its own code ranges, with a unit of
// its own, and must report it (tests/map_base_tests.cpp checks the ranges).
#pragma once
#include "machine/routines.h"
#include <array>
#include <cstdint>
#include <span>

namespace psmodern::game::map {
// ---- Work RAM shared by several parts ------------------------------------------
inline constexpr std::uint16_t game_mode=0xC202;        // the main dispatcher's mode byte
inline constexpr std::uint8_t map_mode=0x09;            // walking on the map or in a town
inline constexpr std::uint8_t load_map_mode=0x08;       // loading a map (00:0B6A)
inline constexpr std::uint8_t load_road_mode=0x0E, road_mode=0x0F;
inline constexpr std::uint16_t scroll_x=0xC300, camera_x=0xC301;    // camera X: 16 bits, high byte masked to 0-7
inline constexpr std::uint16_t scroll_y=0xC304, camera_y=0xC305;    // camera Y: low byte 0-BFh per page
inline constexpr std::uint16_t area=0xC308, location=0xC309;
inline constexpr std::uint16_t transport=0xC30E;        // 0 on foot; 4, 8, 0Ch vehicles; 10h, 11h the ship
inline constexpr std::uint16_t road_index=0xC2E9;       // 1-6 while on the road
inline constexpr std::uint16_t object_slots=0xC800;     // 8 slots of 20h bytes
inline constexpr std::uint16_t sprite_y=0xC900, sprite_xn=0xC980;
inline constexpr std::uint16_t screen_window=0xCC00;    // four decoded screens, C0h bytes each
inline constexpr std::uint16_t name_table_mirror=0xD000;

// ---- Parts, units and segments ---------------------------------------------------
enum class Part : unsigned { video, scroll, objects, terrain, entrances, modes, count };
inline constexpr std::array<const char*,static_cast<unsigned>(Part::count)> part_names{
    "video","scroll","objects","terrain","entrances","modes"};
struct Segment { RoutineEntry entry, unit; };
struct Range { unsigned bank; std::uint16_t first, last; };   // inclusive CPU addresses

constexpr RoutineEntry at0(std::uint16_t address) { return {0,address}; }
constexpr RoutineEntry at1(std::uint16_t address) { return {1,address}; }

// -- video (frame interrupt)
inline constexpr RoutineEntry frame_handler_unit=at0(0x0235);    // the map's frame-interrupt kind
inline constexpr RoutineEntry load_frame_handler_unit=at0(0x029F); // the kind used while a map loads
inline constexpr RoutineEntry sprite_upload_unit=at1(0x588B);    // sprite table upload and its unrolled tails
inline constexpr RoutineEntry party_frames_unit=at1(0x61F5);     // party frame tiles and the frame kinds
inline constexpr RoutineEntry tile_animators_unit=at1(0x62BC);   // map tile animators
inline constexpr RoutineEntry strip_upload_unit=at1(0x73D0);     // one pending column or row to VRAM
inline constexpr RoutineEntry colour_cycle_unit=at1(0x7C18);     // vehicle and ship colour cycling
inline constexpr std::array<Segment,34> video_segments{{
    {frame_handler_unit,frame_handler_unit},
    {load_frame_handler_unit,load_frame_handler_unit},{at0(0x02B4),load_frame_handler_unit},
    {at0(0x02D0),load_frame_handler_unit},{at0(0x02D5),load_frame_handler_unit},
    {sprite_upload_unit,sprite_upload_unit},{at1(0x589E),sprite_upload_unit},
    {at1(0x595E),sprite_upload_unit},{at1(0x599E),sprite_upload_unit},
    {party_frames_unit,party_frames_unit},{at1(0x6217),party_frames_unit},{at1(0x6229),party_frames_unit},
    {at1(0x6246),party_frames_unit},{at1(0x6263),party_frames_unit},{at1(0x6280),party_frames_unit},
    {at1(0x6293),party_frames_unit},{at1(0x62AE),party_frames_unit},
    {tile_animators_unit,tile_animators_unit},{at1(0x62CD),tile_animators_unit},{at1(0x62D9),tile_animators_unit},
    {at1(0x62E5),tile_animators_unit},{at1(0x62F1),tile_animators_unit},{at1(0x62FD),tile_animators_unit},
    {at1(0x6309),tile_animators_unit},{at1(0x630A),tile_animators_unit},{at1(0x6334),tile_animators_unit},{at1(0x6337),tile_animators_unit},{at1(0x6339),tile_animators_unit},
    {at1(0x633F),tile_animators_unit},{at1(0x6343),tile_animators_unit},
    {strip_upload_unit,strip_upload_unit},{at1(0x7403),strip_upload_unit},{at1(0x741B),strip_upload_unit},
    {colour_cycle_unit,colour_cycle_unit}}};

// -- scroll (steps, camera, screen window, strips)
inline constexpr RoutineEntry step_unit=at1(0x7143);             // the party's step (wrapper and 01:714D)
inline constexpr RoutineEntry screen_window_unit=at1(0x723D);    // decode four screens into CC00h (and 01:728A)
inline constexpr RoutineEntry column_unit=at1(0x72A6);           // build the column strip in the mirror
inline constexpr RoutineEntry row_unit=at1(0x733A);              // build the row strip in the mirror
inline constexpr RoutineEntry full_screen_unit=at1(0x744B);      // build the whole mirror (first instruction is a call to its own segment)
inline constexpr std::array<Segment,24> scroll_segments{{
    {step_unit,step_unit},{at1(0x7149),step_unit},{at1(0x714D),step_unit},{at1(0x7190),step_unit},
    {at1(0x71B5),step_unit},{at1(0x71E3),step_unit},{at1(0x71F7),step_unit},{at1(0x720C),step_unit},{at1(0x722A),step_unit},
    {screen_window_unit,screen_window_unit},{at1(0x7264),screen_window_unit},{at1(0x7271),screen_window_unit},
    {at1(0x7281),screen_window_unit},{at1(0x728A),screen_window_unit},{at1(0x728D),screen_window_unit},
    {column_unit,column_unit},{at1(0x72C6),column_unit},{at1(0x72DE),column_unit},{at1(0x72FE),column_unit},
    {row_unit,row_unit},{at1(0x7397),row_unit},
    {at1(0x744B),full_screen_unit},{at1(0x744E),full_screen_unit},{at1(0x7488),full_screen_unit}}};

// -- objects (the object engine and the map's object types)
inline constexpr RoutineEntry object_engine_unit=at1(0x576A);
inline constexpr RoutineEntry clear_object_unit=at1(0x5818);
inline constexpr RoutineEntry object_sprites_unit=at1(0x5853);
inline constexpr std::array<RoutineEntry,10> map_object_types{{
    at1(0x599F),at1(0x59D4),at1(0x59E0),at1(0x59E5),at1(0x59F1),at1(0x59F6),at1(0x5A02),at1(0x5A07),at1(0x5A4F),at1(0x5A70)}};
inline constexpr RoutineEntry vehicle_step_unit=at1(0x5A76);     // 5A4F, 5A70 and 5A76: first instruction is a call or DI of its own
inline constexpr RoutineEntry walker_unit=at1(0x5A98);           // shared by the walking types
inline constexpr RoutineEntry follow_unit=at1(0x5B5C);
inline constexpr RoutineEntry nudge_unit=at1(0x5B6B);
inline constexpr std::array<Segment,33> object_segments{{
    {object_engine_unit,object_engine_unit},{at1(0x5781),object_engine_unit},{at1(0x579F),object_engine_unit},{at1(0x580C),object_engine_unit},
    {clear_object_unit,clear_object_unit},{object_sprites_unit,object_sprites_unit},
    {at1(0x5869),object_sprites_unit},{at1(0x587C),object_sprites_unit},
    {at1(0x599F),at1(0x599F)},{at1(0x59A5),at1(0x599F)},
    {at1(0x59D4),at1(0x59D4)},{at1(0x59DD),at1(0x59D4)},
    {at1(0x59E0),at1(0x59E0)},
    {at1(0x59E5),at1(0x59E5)},{at1(0x59EE),at1(0x59E5)},
    {at1(0x59F1),at1(0x59F1)},
    {at1(0x59F6),at1(0x59F6)},{at1(0x59FF),at1(0x59F6)},
    {at1(0x5A02),at1(0x5A02)},
    {at1(0x5A07),at1(0x5A07)},{at1(0x5A10),at1(0x5A07)},
    {at1(0x5A4F),at1(0x5A4F)},{at1(0x5A70),at1(0x5A70)},{at1(0x5A76),vehicle_step_unit},
    {at1(0x5A52),at1(0x5A4F)},{at1(0x5A73),at1(0x5A70)},{at1(0x5A79),vehicle_step_unit},
    {walker_unit,walker_unit},{at1(0x5A9D),walker_unit},{at1(0x5B35),walker_unit},
    {follow_unit,follow_unit},{at1(0x5B66),follow_unit},{nudge_unit,nudge_unit}}};

// -- terrain (what the ground allows, vehicles, ice)
inline constexpr RoutineEntry tile_attribute_unit=at1(0x7764);
inline constexpr RoutineEntry blocked_unit=at1(0x74E4);
inline constexpr RoutineEntry ice_probe_unit=at1(0x756C);        // the ice breaking keeps its DI stretch as a segment of its own
inline constexpr RoutineEntry ice_preset_unit=at1(0x7602);       // and 01:761F
inline constexpr RoutineEntry landrover_unit=at1(0x7656);        // vehicle placement from the item menu
inline constexpr RoutineEntry hovercraft_unit=at1(0x76C1);
inline constexpr RoutineEntry leave_vehicle_unit=at1(0x7732);
inline constexpr std::array<Segment,29> terrain_segments{{
    {tile_attribute_unit,tile_attribute_unit},
    {blocked_unit,blocked_unit},{at1(0x74FD),blocked_unit},{at1(0x7516),blocked_unit},{at1(0x751F),blocked_unit},
    {at1(0x7535),blocked_unit},{at1(0x753E),blocked_unit},{at1(0x7552),blocked_unit},{at1(0x755A),blocked_unit},
    {ice_probe_unit,ice_probe_unit},{at1(0x75E4),ice_probe_unit},{at1(0x75E5),ice_probe_unit},
    {ice_preset_unit,ice_preset_unit},{at1(0x7613),ice_preset_unit},{at1(0x7618),ice_preset_unit},
    {at1(0x761D),ice_preset_unit},{at1(0x761F),ice_preset_unit},
    {landrover_unit,landrover_unit},{at1(0x7665),landrover_unit},{at1(0x766E),landrover_unit},
    {at1(0x7677),landrover_unit},{at1(0x7680),landrover_unit},
    {hovercraft_unit,hovercraft_unit},{at1(0x76D0),hovercraft_unit},{at1(0x76D9),hovercraft_unit},
    {at1(0x76E2),hovercraft_unit},{at1(0x76EB),hovercraft_unit},
    {leave_vehicle_unit,leave_vehicle_unit},{at1(0x7749),leave_vehicle_unit}}};

// -- entrances (entrances, transfers, special ground)
inline constexpr RoutineEntry tile_interaction_unit=at1(0x77AC);
inline constexpr RoutineEntry dungeon_exit_transfer_unit=at1(0x7877);   // also the target of a milestone 7 segment's JP
inline constexpr RoutineEntry transfer_unit=at1(0x787B);
inline constexpr RoutineEntry reset_after_transfer_unit=at1(0x7908);
inline constexpr RoutineEntry special_ground_unit=at1(0x792A);
inline constexpr RoutineEntry ground_damage_unit=at1(0x7A0C);           // first instruction is a call or DI of its own
inline constexpr std::array<Segment,24> entrance_segments{{
    {tile_interaction_unit,tile_interaction_unit},{at1(0x77C4),tile_interaction_unit},{at1(0x77D9),tile_interaction_unit},
    {at1(0x77E5),tile_interaction_unit},{at1(0x77EF),tile_interaction_unit},{at1(0x77F9),tile_interaction_unit},
    {at1(0x7803),tile_interaction_unit},{at1(0x7845),tile_interaction_unit},
    {dungeon_exit_transfer_unit,dungeon_exit_transfer_unit},{transfer_unit,transfer_unit},
    {reset_after_transfer_unit,reset_after_transfer_unit},{at1(0x7924),reset_after_transfer_unit},{at1(0x7928),reset_after_transfer_unit},
    {special_ground_unit,special_ground_unit},{at1(0x796B),special_ground_unit},{at1(0x7997),special_ground_unit},
    {at1(0x79DC),special_ground_unit},{at1(0x79E0),special_ground_unit},{at1(0x79E9),special_ground_unit},
    {at1(0x79EE),special_ground_unit},{at1(0x79F3),special_ground_unit},{at1(0x79F8),special_ground_unit},
    {at1(0x7A0C),ground_damage_unit},{at1(0x7A0F),ground_damage_unit}}};

// -- modes (the map loop, map loading, the road)
inline constexpr RoutineEntry map_loop_unit=at0(0x0B08);
inline constexpr RoutineEntry load_map_unit=at0(0x0B6A);         // first instruction is a call or DI of its own; three DI stretches are segments of their own
inline constexpr RoutineEntry map_sound_unit=at0(0x0C97);
inline constexpr RoutineEntry party_slots_unit=at0(0x0CA6);      // and 00:0CE3
inline constexpr RoutineEntry sprite_clear_unit=at0(0x02FD);   // the unit; its segment starts at 00:0306
inline constexpr RoutineEntry road_unit=at0(0x0E8B);
inline constexpr RoutineEntry load_road_unit=at0(0x0ED7);
inline constexpr std::array<Segment,34> mode_segments{{
    {map_loop_unit,map_loop_unit},{at0(0x0B0F),map_loop_unit},{at0(0x0B1A),map_loop_unit},{at0(0x0B2D),map_loop_unit},
    {at0(0x0B72),load_map_unit},{at0(0x0B75),load_map_unit},{at0(0x0B96),load_map_unit},{at0(0x0BAC),load_map_unit},
    {at0(0x0BB9),load_map_unit},{at0(0x0BBA),load_map_unit},{at0(0x0C63),load_map_unit},{at0(0x0C68),load_map_unit},
    {at0(0x0C94),load_map_unit},{at0(0x0B58),map_loop_unit},{at0(0x0B65),map_loop_unit},{at0(0x0BA6),load_map_unit},
    {at0(0x0BA9),load_map_unit},{at0(0x0C60),load_map_unit},{at0(0x0CAF),party_slots_unit},{at0(0x0EF5),load_road_unit},
    {map_sound_unit,map_sound_unit},
    {party_slots_unit,party_slots_unit},{at0(0x0CB3),party_slots_unit},{at0(0x0CCE),party_slots_unit},{at0(0x0CD6),party_slots_unit},
    {at0(0x0CDE),party_slots_unit},{at0(0x0CE3),party_slots_unit},
    {at0(0x0306),sprite_clear_unit},      // planned as 00:02FD; the first fill is the entry (interrupts)
    {road_unit,road_unit},{at0(0x0E92),road_unit},{at0(0x0E97),road_unit},{at0(0x0EA7),road_unit},
    {load_road_unit,load_road_unit},{at0(0x0EF8),load_road_unit}}};

inline constexpr std::span<const Segment> planned_segments(Part part) {
    switch (part) {
        case Part::video: return video_segments;
        case Part::scroll: return scroll_segments;
        case Part::objects: return object_segments;
        case Part::terrain: return terrain_segments;
        case Part::entrances: return entrance_segments;
        case Part::modes: return mode_segments;
        default: return {};
    }
}

// The code each part may register entries in (the static extent of its units).
inline constexpr std::array<Range,7> video_ranges{{{0,0x0235,0x025E},{0,0x029F,0x02DE},{1,0x588B,0x599E},
    {1,0x61F5,0x62BB},{1,0x62BC,0x6344},{1,0x73D0,0x744A},{1,0x7C18,0x7C84}}};
inline constexpr std::array<Range,5> scroll_ranges{{{1,0x7143,0x723C},{1,0x723D,0x72A5},{1,0x72A6,0x7339},
    {1,0x733A,0x73CF},{1,0x744B,0x74E3}}};
inline constexpr std::array<Range,3> object_ranges{{{1,0x576A,0x5826},{1,0x5853,0x588A},{1,0x599F,0x5B86}}};
inline constexpr std::array<Range,5> terrain_ranges{{{1,0x74E4,0x756B},{1,0x756C,0x7601},{1,0x7602,0x7655},
    {1,0x7656,0x7763},{1,0x7764,0x77AB}}};
inline constexpr std::array<Range,1> entrance_ranges{{{1,0x77AC,0x7A20}}};
inline constexpr std::array<Range,5> mode_ranges{{{0,0x02FD,0x0317},{0,0x0B08,0x0B69},{0,0x0B6A,0x0C96},
    {0,0x0C97,0x0CEA},{0,0x0E8B,0x0F0B}}};
inline constexpr std::span<const Range> part_ranges(Part part) {
    switch (part) {
        case Part::video: return video_ranges;
        case Part::scroll: return scroll_ranges;
        case Part::objects: return object_ranges;
        case Part::terrain: return terrain_ranges;
        case Part::entrances: return entrance_ranges;
        case Part::modes: return mode_ranges;
        default: return {};
    }
}

// ---- Instructions left out of the segments ------------------------------------------------
// Milestone 14: nothing is left out. The four stretches with interrupts off
// (00:0B6D-0B72 around the display-off helper, 00:0BB4-0BB9 the party frame
// upload, 00:0C91-0C94 VDP register 0, 01:75CC-75E4 the ice breaking's
// name-table update) are segments of their own, DI and EI rendered as finals,
// and the instruction after each EI is a joining segment.
inline constexpr std::array<Range,0> native_stretches{};
// The map loop's call of the object engine right after its frame wait: the
// host opens Options and Language there (src/app/frame_driver.h). It is a bare
// call between segments; no segment may start at it, declare it as an exit or
// render it.
inline constexpr RoutineEntry options_boundary=at0(0x0B14);
}

namespace psmodern::game {
// One registration function per part; register_routines calls all six for an
// identified image. Each lives in its part's file and registers only there.
void register_sprite_output_slices(RoutineRegistry& registry);
void register_map_video(RoutineRegistry& registry);
void register_map_scroll(RoutineRegistry& registry);
void register_map_objects(RoutineRegistry& registry);
void register_map_terrain(RoutineRegistry& registry);
void register_map_entrances(RoutineRegistry& registry);
void register_map_modes(RoutineRegistry& registry);
void register_map_button_resume(RoutineRegistry& registry);
inline constexpr std::array<RoutineEntry,3> map_button_resume_entries{{{0,0x0B3B},{0,0x0B47},{0,0x0B5C}}};
inline constexpr std::array<RoutineEntry,2> map_exit_copies{{{0,0x0B5A},{0,0x0B67}}};
}
