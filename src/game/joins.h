// SPDX-License-Identifier: GPL-3.0-or-later
// Joining instructions that no input recording reaches (milestone 14, Part I),
// registered by the file that owns the neighbouring code: segments where the
// machine continues after a segment ends (the return point of a call, a branch
// target, a code-table target), found by the static walk of every declared exit.
// The catalog per file lists each entry with its routine (unit).
#pragma once
#include "machine/routines.h"
#include <array>
#include <span>

namespace psmodern::game {
void register_battle_actor_walk_joins(RoutineRegistry& registry);
void register_battle_commands_walk_joins(RoutineRegistry& registry);
void register_battle_enemy_walk_joins(RoutineRegistry& registry);
void register_battle_enemy_magic_walk_joins(RoutineRegistry& registry);
void register_battle_magic_walk_joins(RoutineRegistry& registry);
void register_battle_outcomes_walk_joins(RoutineRegistry& registry);
void register_battle_status_walk_joins(RoutineRegistry& registry);
void register_block_transfer_walk_joins(RoutineRegistry& registry);
void register_dungeon_escape_walk_joins(RoutineRegistry& registry);
void register_dungeon_movement_walk_joins(RoutineRegistry& registry);
void register_dungeon_objects_walk_joins(RoutineRegistry& registry);
void register_field_magic_walk_joins(RoutineRegistry& registry);
void register_interaction_companion_walk_joins(RoutineRegistry& registry);
void register_interaction_handoffs_walk_joins(RoutineRegistry& registry);
void register_interaction_progress_walk_joins(RoutineRegistry& registry);
void register_interaction_progression_walk_joins(RoutineRegistry& registry);
void register_interaction_prompts_walk_joins(RoutineRegistry& registry);
void register_interaction_replies_walk_joins(RoutineRegistry& registry);
void register_interaction_rescue_walk_joins(RoutineRegistry& registry);
void register_interaction_sales_walk_joins(RoutineRegistry& registry);
void register_interaction_setup_walk_joins(RoutineRegistry& registry);
void register_interaction_story_walk_joins(RoutineRegistry& registry);
void register_interaction_trade_walk_joins(RoutineRegistry& registry);
void register_items_field_walk_joins(RoutineRegistry& registry);
void register_items_story_walk_joins(RoutineRegistry& registry);
void register_map_modes_walk_joins(RoutineRegistry& registry);
void register_menu_fades_walk_joins(RoutineRegistry& registry);
void register_menu_field_walk_joins(RoutineRegistry& registry);
void register_menu_places_walk_joins(RoutineRegistry& registry);
void register_menu_windows_walk_joins(RoutineRegistry& registry);
void register_party_helpers_walk_joins(RoutineRegistry& registry);
void register_scenes_walk_joins(RoutineRegistry& registry);
void register_scenes_quests_walk_joins(RoutineRegistry& registry);
void register_scenes_script_walk_joins(RoutineRegistry& registry);
void register_joins(RoutineRegistry& registry);

namespace joins {
struct JoinSegment { RoutineEntry entry, unit; };
inline constexpr std::array<JoinSegment,3> battle_actor{{
    {{0,0x11EE},{0,0x1148}},
    {{0,0x1218},{0,0x1148}},
    {{0,0x121B},{0,0x1148}},
}};
inline constexpr std::array<JoinSegment,16> battle_commands{{
    {{0,0x192B},{0,0x192B}},
    {{0,0x192E},{0,0x192B}},
    {{0,0x1931},{0,0x192B}},
    {{0,0x1961},{0,0x192B}},
    {{0,0x196A},{0,0x192B}},
    {{0,0x199C},{0,0x199C}},
    {{0,0x199F},{0,0x199C}},
    {{0,0x19A2},{0,0x199C}},
    {{0,0x19EF},{0,0x19DE}},
    {{0,0x1A3F},{0,0x19DE}},
    {{0,0x1A51},{0,0x19DE}},
    {{0,0x1A54},{0,0x19DE}},
    {{0,0x1AAE},{0,0x1AAE}},
    {{0,0x1ACF},{0,0x1AB1}},
    {{0,0x1AFC},{0,0x1ADE}},
    {{0,0x1B4E},{0,0x1B0D}},
}};
inline constexpr std::array<JoinSegment,4> battle_enemy{{
    {{0,0x12B6},{0,0x128C}},
    {{0,0x1302},{0,0x128C}},
    {{0,0x1341},{0,0x128C}},
    {{0,0x1376},{0,0x128C}},
}};
inline constexpr std::array<JoinSegment,19> battle_enemy_magic{{
    {{0,0x13A3},{0,0x1386}},
    {{0,0x13FC},{0,0x1386}},
    {{0,0x13FF},{0,0x13FF}},
    {{0,0x141E},{0,0x1386}},
    {{0,0x1421},{0,0x1421}},
    {{0,0x1429},{0,0x1386}},
    {{0,0x143E},{0,0x1386}},
    {{0,0x1443},{0,0x1386}},
    {{0,0x144B},{0,0x1386}},
    {{0,0x1457},{0,0x1386}},
    {{0,0x147D},{0,0x1386}},
    {{0,0x148E},{0,0x1386}},
    {{0,0x149D},{0,0x149D}},
    {{0,0x14BC},{0,0x1386}},
    {{0,0x14E1},{0,0x1386}},
    {{0,0x14F2},{0,0x1386}},
    {{0,0x152F},{0,0x1386}},
    {{0,0x1537},{0,0x1386}},
    {{0,0x1552},{0,0x1386}},
}};
inline constexpr std::array<JoinSegment,4> battle_magic{{
    {{0,0x1EC6},{0,0x1EB2}},
    {{0,0x1EE1},{0,0x1EB2}},
    {{0,0x1F30},{0,0x1F25}},
    {{0,0x1F33},{0,0x1F25}},
}};
inline constexpr std::array<JoinSegment,1> battle_outcomes{{
    {{0,0x1642},{0,0x163E}},
}};
inline constexpr std::array<JoinSegment,8> battle_status{{
    {{0,0x1EA4},{0,0x1E8A}},
    {{0,0x1F7D},{0,0x1F36}},
    {{0,0x1FA9},{0,0x1F80}},
    {{0,0x1FB7},{0,0x1F80}},
    {{0,0x1FDC},{0,0x1FC0}},
    {{0,0x2019},{0,0x1FDF}},
    {{0,0x20F2},{0,0x20BF}},
    {{0,0x20F5},{0,0x20BF}},
}};
inline constexpr std::array<JoinSegment,1> block_transfer{{
    {{1,0x4CF8},{1,0x4CF6}},
}};
inline constexpr std::array<JoinSegment,2> dungeon_escape{{
    {{1,0x688B},{1,0x687A}},
    {{1,0x68AF},{1,0x688C}},
}};
inline constexpr std::array<JoinSegment,13> dungeon_movement{{
    {{1,0x66FA},{1,0x65EE}},
    {{1,0x66FF},{1,0x65EE}},
    {{1,0x6704},{1,0x65EE}},
    {{1,0x6709},{1,0x65EE}},
    {{1,0x6729},{1,0x65EE}},
    {{1,0x6755},{1,0x65EE}},
    {{1,0x677A},{1,0x65EE}},
    {{1,0x677E},{1,0x65EE}},
    {{1,0x6783},{1,0x65EE}},
    {{1,0x6785},{1,0x65EE}},
    {{1,0x67AB},{1,0x65EE}},
    {{1,0x67AF},{1,0x65EE}},
    {{1,0x67D7},{1,0x65EE}},
}};
inline constexpr std::array<JoinSegment,7> dungeon_objects{{
    {{1,0x6922},{1,0x68BC}},
    {{1,0x693D},{1,0x68BC}},
    {{1,0x6940},{1,0x68BC}},
    {{1,0x6943},{1,0x68BC}},
    {{1,0x6946},{1,0x68BC}},
    {{1,0x69EE},{1,0x6963}},
    {{1,0x6A26},{1,0x6963}},
}};
inline constexpr std::array<JoinSegment,4> field_magic{{
    {{0,0x2049},{0,0x201C}},
    {{0,0x2075},{0,0x2064}},
    {{0,0x2083},{0,0x2064}},
    {{0,0x20A7},{0,0x2091}},
}};
inline constexpr std::array<JoinSegment,2> interaction_companion{{
    {{1,0x4A20},{1,0x49E6}},
    {{1,0x5389},{1,0x49E6}},
}};
inline constexpr std::array<JoinSegment,33> interaction_handoffs{{
    {{1,0x4946},{1,0x4940}},
    {{1,0x496B},{1,0x4966}},
    {{1,0x4978},{1,0x4973}},
    {{1,0x49FA},{1,0x49E6}},
    {{1,0x49FD},{1,0x49E6}},
    {{1,0x4A2E},{1,0x49E6}},
    {{1,0x4A97},{1,0x4A50}},
    {{1,0x4AD9},{1,0x4AD3}},
    {{1,0x4ADC},{1,0x4AD3}},
    {{1,0x4AEA},{1,0x4AD3}},
    {{1,0x4AED},{1,0x4AD3}},
    {{1,0x4AFB},{1,0x4AD3}},
    {{1,0x4AFE},{1,0x4AD3}},
    {{1,0x4BA3},{1,0x4B9D}},
    {{1,0x4C35},{1,0x4C0D}},
    {{1,0x4C7C},{1,0x4C70}},
    {{1,0x4D80},{1,0x4D7A}},
    {{1,0x4D83},{1,0x4D7A}},
    {{1,0x4DB1},{1,0x4DAB}},
    {{1,0x4DB4},{1,0x4DAB}},
    {{1,0x4DC1},{1,0x4DAB}},
    {{1,0x4DDA},{1,0x4DD4}},
    {{1,0x4DDD},{1,0x4DD4}},
    {{1,0x4DEA},{1,0x4DD4}},
    {{1,0x4DEC},{1,0x4DD4}},
    {{1,0x4E7C},{1,0x4DFB}},
    {{1,0x4E89},{1,0x4DFB}},
    {{1,0x4F1B},{1,0x4F15}},
    {{1,0x4F5E},{1,0x4F58}},
    {{1,0x50E0},{1,0x50DA}},
    {{1,0x50E3},{1,0x50DA}},
    {{1,0x50F0},{1,0x50DA}},
    {{1,0x50F2},{1,0x50DA}},
}};
inline constexpr std::array<JoinSegment,1> interaction_progress{{
    {{1,0x4A3B},{1,0x49E6}},
}};
inline constexpr std::array<JoinSegment,3> interaction_progression{{
    {{1,0x4E07},{1,0x4DFB}},
    {{1,0x4E31},{1,0x4DFB}},
    {{1,0x4E34},{1,0x4DFB}},
}};
inline constexpr std::array<JoinSegment,5> interaction_prompts{{
    {{1,0x4BC3},{1,0x4BBD}},
    {{1,0x4ECB},{1,0x4EC5}},
    {{1,0x5032},{1,0x502C}},
    {{1,0x50AC},{1,0x50A6}},
    {{1,0x50CC},{1,0x50C6}},
}};
inline constexpr std::array<JoinSegment,8> interaction_replies{{
    {{1,0x49DD},{1,0x49B5}},
    {{1,0x4A8B},{1,0x4A50}},
    {{1,0x4B12},{1,0x4AD3}},
    {{1,0x4B15},{1,0x4AD3}},
    {{1,0x4B3F},{1,0x4AD3}},
    {{1,0x4B40},{1,0x4AD3}},
    {{1,0x4BB0},{1,0x4B9D}},
    {{1,0x4BBA},{1,0x4B9D}},
}};
inline constexpr std::array<JoinSegment,6> interaction_rescue{{
    {{1,0x43CF},{1,0x4C0D}},
    {{1,0x4C25},{1,0x4C0D}},
    {{1,0x4C28},{1,0x4C0D}},
    {{1,0x4C3E},{1,0x4C0D}},
    {{1,0x4C4F},{1,0x4C0D}},
    {{1,0x4C67},{1,0x4C0D}},
}};
inline constexpr std::array<JoinSegment,2> interaction_sales{{
    {{1,0x4FD4},{1,0x4F9B}},
    {{1,0x4FD5},{1,0x4F9B}},
}};
inline constexpr std::array<JoinSegment,4> interaction_setup{{
    {{1,0x4FA7},{1,0x4F9B}},
    {{1,0x4FAA},{1,0x4F9B}},
    {{1,0x507C},{1,0x5070}},
    {{1,0x507F},{1,0x5070}},
}};
inline constexpr std::array<JoinSegment,1> interaction_story{{
    {{1,0x4915},{1,0x48FC}},
}};
inline constexpr std::array<JoinSegment,2> interaction_trade{{
    {{1,0x4D8E},{1,0x4D7A}},
    {{1,0x4EAA},{1,0x4E9C}},
}};
inline constexpr std::array<JoinSegment,11> items_field{{
    {{0,0x2296},{0,0x228A}},
    {{0,0x22AC},{0,0x2299}},
    {{0,0x2330},{0,0x231A}},
    {{0,0x238C},{0,0x2369}},
    {{0,0x23AF},{0,0x239D}},
    {{0,0x23C4},{0,0x239D}},
    {{0,0x23E9},{0,0x23E2}},
    {{0,0x23FE},{0,0x23EC}},
    {{0,0x2413},{0,0x23EC}},
    {{0,0x2416},{0,0x2416}},
    {{0,0x242C},{0,0x2416}},
}};
inline constexpr std::array<JoinSegment,30> items_story{{
    {{0,0x2441},{0,0x242F}},
    {{0,0x2459},{0,0x242F}},
    {{0,0x2467},{0,0x242F}},
    {{0,0x246A},{0,0x242F}},
    {{0,0x24A3},{0,0x2491}},
    {{0,0x24BB},{0,0x2491}},
    {{0,0x24D7},{0,0x2491}},
    {{0,0x24E0},{0,0x2491}},
    {{0,0x24E3},{0,0x2491}},
    {{0,0x24E6},{0,0x2491}},
    {{0,0x24FB},{0,0x24E9}},
    {{0,0x2521},{0,0x24E9}},
    {{0,0x2524},{0,0x2524}},
    {{0,0x2534},{0,0x2524}},
    {{0,0x2549},{0,0x2537}},
    {{0,0x2559},{0,0x2537}},
    {{0,0x256E},{0,0x2537}},
    {{0,0x257A},{0,0x2537}},
    {{0,0x2585},{0,0x2537}},
    {{0,0x2586},{0,0x2537}},
    {{0,0x259B},{0,0x2589}},
    {{0,0x25AB},{0,0x2589}},
    {{0,0x25EC},{0,0x25C3}},
    {{0,0x25FC},{0,0x25C3}},
    {{0,0x2625},{0,0x2613}},
    {{0,0x262E},{0,0x2613}},
    {{0,0x264C},{0,0x2631}},
    {{0,0x2679},{0,0x2631}},
    {{0,0x26AD},{0,0x267C}},
    {{0,0x26C5},{0,0x26B0}},
}};
inline constexpr std::array<JoinSegment,1> map_modes{{
    {{0,0x0ED6},{0,0x0E8B}},
}};
inline constexpr std::array<JoinSegment,2> menu_fades{{
    {{1,0x7CC3},{1,0x7CBB}},
    {{1,0x7CC6},{1,0x7CBB}},
}};
inline constexpr std::array<JoinSegment,10> menu_field{{
    {{0,0x1C3F},{0,0x1BE1}},
    {{0,0x1C42},{0,0x1BE1}},
    {{0,0x1C4E},{0,0x1BE1}},
    {{0,0x1E24},{0,0x1E24}},
    {{0,0x2172},{0,0x2168}},
    {{0,0x28BF},{0,0x2839}},
    {{0,0x28C2},{0,0x2839}},
    {{0,0x28EE},{0,0x28EE}},
    {{0,0x2966},{0,0x2950}},
    {{1,0x4414},{0,0x1BE1}},
}};
inline constexpr std::array<JoinSegment,10> menu_places{{
    {{0,0x29E1},{0,0x298D}},
    {{0,0x2B43},{0,0x2A85}},
    {{0,0x2C5E},{0,0x2BC0}},
    {{0,0x2C61},{0,0x2BC0}},
    {{0,0x2C64},{0,0x2BC0}},
    {{0,0x2C7D},{0,0x2BC0}},
    {{0,0x2C86},{0,0x2BC0}},
    {{0,0x2C89},{0,0x2BC0}},
    {{0,0x2C8C},{0,0x2BC0}},
    {{0,0x2C95},{0,0x2C8F}},
}};
inline constexpr std::array<JoinSegment,1> menu_windows{{
    {{0,0x2FCB},{0,0x2F93}},
}};
inline constexpr std::array<JoinSegment,1> party_helpers{{
    {{0,0x18A1},{0,0x188E}},
}};
inline constexpr std::array<JoinSegment,5> scenes{{
    {{0,0x064A},{0,0x0639}},
    {{0,0x06CB},{0,0x0682}},
    {{0,0x06EA},{0,0x06E2}},
    {{0,0x0997},{0,0x0968}},
    {{0,0x0B07},{0,0x0B07}},
}};
inline constexpr std::array<JoinSegment,44> scenes_quests{{
    {{1,0x4461},{1,0x51A9}},
    {{1,0x5119},{1,0x510D}},
    {{1,0x517C},{1,0x5177}},
    {{1,0x5192},{1,0x518E}},
    {{1,0x51BF},{1,0x51BA}},
    {{1,0x51E8},{1,0x51E3}},
    {{1,0x520A},{1,0x5204}},
    {{1,0x522B},{1,0x5225}},
    {{1,0x5243},{1,0x522E}},
    {{1,0x5246},{1,0x522E}},
    {{1,0x524F},{1,0x5249}},
    {{1,0x5276},{1,0x5270}},
    {{1,0x5296},{1,0x5290}},
    {{1,0x52D6},{1,0x52D0}},
    {{1,0x52ED},{1,0x52E7}},
    {{1,0x52FE},{1,0x52F8}},
    {{1,0x530F},{1,0x5309}},
    {{1,0x5320},{1,0x5312}},
    {{1,0x533D},{1,0x5337}},
    {{1,0x5359},{1,0x5353}},
    {{1,0x536B},{1,0x5363}},
    {{1,0x5386},{1,0x535C}},
    {{1,0x53D8},{1,0x53D0}},
    {{1,0x53FB},{1,0x53F7}},
    {{1,0x53FE},{1,0x53F7}},
    {{1,0x5407},{1,0x5401}},
    {{1,0x5459},{1,0x5453}},
    {{1,0x547F},{1,0x5471}},
    {{1,0x54DE},{1,0x54D8}},
    {{1,0x54E1},{1,0x54D8}},
    {{1,0x5510},{1,0x550A}},
    {{1,0x551E},{1,0x5513}},
    {{1,0x5521},{1,0x5513}},
    {{1,0x5543},{1,0x553D}},
    {{1,0x555A},{1,0x5553}},
    {{1,0x5563},{1,0x555D}},
    {{1,0x5586},{1,0x557E}},
    {{1,0x5589},{1,0x557E}},
    {{1,0x5592},{1,0x558C}},
    {{1,0x55D3},{1,0x55CD}},
    {{1,0x5612},{1,0x5602}},
    {{1,0x56DF},{1,0x56D9}},
    {{1,0x5726},{1,0x5714}},
    {{1,0x573C},{1,0x56FF}},
}};
inline constexpr std::array<JoinSegment,11> scenes_script{{
    {{1,0x4453},{1,0x444D}},
    {{1,0x4456},{1,0x444D}},
    {{1,0x449E},{1,0x4497}},
    {{1,0x44A7},{1,0x4497}},
    {{1,0x44C5},{1,0x44BF}},
    {{1,0x44E2},{1,0x44D5}},
    {{1,0x44FD},{1,0x44E5}},
    {{1,0x4517},{1,0x44BF}},
    {{1,0x4540},{1,0x453B}},
    {{1,0x454B},{1,0x4543}},
    {{1,0x5663},{1,0x565D}},
}};
inline constexpr std::array<std::span<const JoinSegment>,34> all{{
    battle_actor,
    battle_commands,
    battle_enemy,
    battle_enemy_magic,
    battle_magic,
    battle_outcomes,
    battle_status,
    block_transfer,
    dungeon_escape,
    dungeon_movement,
    dungeon_objects,
    field_magic,
    interaction_companion,
    interaction_handoffs,
    interaction_progress,
    interaction_progression,
    interaction_prompts,
    interaction_replies,
    interaction_rescue,
    interaction_sales,
    interaction_setup,
    interaction_story,
    interaction_trade,
    items_field,
    items_story,
    map_modes,
    menu_fades,
    menu_field,
    menu_places,
    menu_windows,
    party_helpers,
    scenes,
    scenes_quests,
    scenes_script,
}};
}
}
