// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/joins.h"

namespace psmodern::game {
void register_joins(RoutineRegistry& registry) {
    register_battle_actor_walk_joins(registry);
    register_battle_commands_walk_joins(registry);
    register_battle_enemy_walk_joins(registry);
    register_battle_enemy_magic_walk_joins(registry);
    register_battle_magic_walk_joins(registry);
    register_battle_outcomes_walk_joins(registry);
    register_battle_status_walk_joins(registry);
    register_block_transfer_walk_joins(registry);
    register_dungeon_escape_walk_joins(registry);
    register_dungeon_movement_walk_joins(registry);
    register_dungeon_objects_walk_joins(registry);
    register_field_magic_walk_joins(registry);
    register_interaction_companion_walk_joins(registry);
    register_interaction_handoffs_walk_joins(registry);
    register_interaction_progress_walk_joins(registry);
    register_interaction_progression_walk_joins(registry);
    register_interaction_prompts_walk_joins(registry);
    register_interaction_replies_walk_joins(registry);
    register_interaction_rescue_walk_joins(registry);
    register_interaction_sales_walk_joins(registry);
    register_interaction_setup_walk_joins(registry);
    register_interaction_story_walk_joins(registry);
    register_interaction_trade_walk_joins(registry);
    register_items_field_walk_joins(registry);
    register_items_story_walk_joins(registry);
    register_map_modes_walk_joins(registry);
    register_menu_fades_walk_joins(registry);
    register_menu_field_walk_joins(registry);
    register_menu_places_walk_joins(registry);
    register_menu_windows_walk_joins(registry);
    register_party_helpers_walk_joins(registry);
    register_scenes_walk_joins(registry);
    register_scenes_quests_walk_joins(registry);
    register_scenes_script_walk_joins(registry);
}
}
