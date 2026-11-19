// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/joins.h"
#include "game/routines.h"
#include "game/frame_updates.h"
#include "game/common.h"
#include "game/interaction_replies.h"
#include "game/interaction_setup.h"
#include "game/interaction_progress.h"
#include "game/interaction_companion.h"
#include "game/interaction_letter.h"
#include "game/interaction_rescue.h"
#include "game/interaction_progression.h"
#include "game/interaction_sales.h"
#include "game/interaction_trade.h"
#include "game/interaction_messages.h"
#include "game/interaction_prompts.h"
#include "game/interaction_party.h"
#include "game/interaction_state.h"
#include "game/interaction_story.h"
#include "game/interaction_handoffs.h"
#include "game/dungeon_view.h"
#include "game/video_transfer.h"
#include "game/menu_remainder.h"
#include "game/scenes.h"
#include "game/money.h"
#include "game/recovery.h"
#include "game/battle_commands.h"
#include "game/battle_turn.h"
#include "game/battle_magic.h"
#include "game/battle_sprites.h"
#include "game/battle_outcomes.h"
#include "game/items_field.h"
#include "game/items_story.h"
#include "game/party_helpers.h"
#include "game/battle_enemy_magic.h"
#include "game/battle_enemy.h"
#include "game/battle_actor.h"
#include "game/battle_damage.h"
#include "game/battle_status.h"
#include "game/field_magic.h"
#include "game/menu_kit.h"
#include "rom/rom.h"
#include <stdexcept>

namespace psmodern::game {
namespace {
namespace k=menu_kit;
// 00:0339, the controller poll, as an exact segment of the menu kit (port
// input): every instruction rendered, its final return included.
template<class Z> k::Exit poll_controller(Z& z) {
    auto& r=z.r;
    // Read the active-low controller port at the original T-state of the
    // read (in a,(n): the port's high byte is A) and invert it: a set bit is
    // a button held now. The held buttons' address is the operand at 00:033B.
    z.bus().fetch(0x0339);
    const auto port=k::pair(r.a,z.bus().read(0x033A));
    r.a=z.bus().in(port,10);
    r.wz=static_cast<std::uint16_t>(port+1);
    z.bus().advance(1);
    z.load_hl(0x033B);
    z.complement_a(0x033E);
    z.copy(0x033F,r.b,r.a);
    // The buttons that changed since the last poll (xor with the held byte
    // stored then), before the new held byte replaces it.
    z.bus().fetch(0x0340);
    r.a^=z.bus().read(z.hl());
    kit::logic_flags(r,false);
    z.bus().advance(7);
    z.store_register_to_hl(0x0341,r.b);
    // A held button is recorded every poll; an edge (pressed now, not held
    // before) only once, in the byte after it.
    z.increment_hl(0x0342);
    z.and_register(0x0343,r.b);
    z.store_register_to_hl(0x0344,r.a);
    return z.final_return(0x0345);
}
}
bool register_routines(RoutineRegistry& registry, std::string_view digest, const Options& options) {
    validate(options);
    try { (void)identify_digest(digest); }
    catch (const std::runtime_error&) { return false; }
    // Fixed cost: 70 cycles on every path, the final return included.
    k::add_segment(registry,controller_poll_entry,k::segment(controller_poll_entry,{k::returned_at(0x0345)}),
                   [](const Machine&) -> std::uint64_t { return 70; },[](auto& z) { return poll_controller(z); });
    register_random(registry);
    register_dispatch(registry);
    register_arithmetic(registry);
    register_block_transfers(registry);
    register_save_menu(registry);
    register_observed_menu_cancel(registry);
    register_window_rows(registry);
    register_encounters(registry,options);
    register_experience(registry,options);
    register_money_add(registry);
    register_battle_setup(registry,options);
    register_recovery(registry);
    register_battle_commands(registry);
    register_battle_turn(registry);
    register_battle_magic(registry);
    battle_sprites::register_sprites(registry);
    battle_sprites::register_animation_sequences(registry);
    battle_sprites::register_scene_objects(registry);
    register_battle_outcomes(registry);
    register_items_field(registry);
    register_items_story(registry);
    register_party_helpers(registry);
    register_battle_enemy_magic(registry);
    register_battle_enemy(registry);
    register_battle_actor(registry);
    register_battle_damage(registry);
    register_battle_status(registry);
    register_field_magic(registry);
    register_dungeon_cells(registry);
    register_dungeon_movement(registry);
    register_dungeon_objects(registry);
    dungeon_view::register_escape(registry);
    dungeon_view::register_load(registry);
    register_dungeon_layout(registry);
    register_dungeon_streams(registry);
    register_pitfall_video(registry);
    dungeon_view::register_cone(registry);
    dungeon_view::register_frame(registry);
    dungeon_view::register_animation(registry);
    dungeon_view::register_overlays(registry);
    video_transfer::register_transfers(registry);
    video_transfer::register_packets(registry);
    register_scenes(registry);
    register_text(registry);
    register_map_video(registry);
    register_sprite_output_slices(registry);
    register_map_display(registry);
    register_map_scroll(registry);
    register_map_objects(registry);
    register_map_terrain(registry);
    register_map_entrances(registry);
    register_map_modes(registry);
    register_map_button_resume(registry);
    register_menu_engine(registry);
    register_menu_windows(registry);
    register_menu_select(registry);
    register_menu_field(registry);
    register_menu_remainder(registry);
    register_menu_places(registry);
    register_menu_fades(registry);
    register_sound(registry);
    frame_updates::register_updates(registry);
    register_common(registry);
    register_interaction_replies(registry);
    register_interaction_setup(registry);
    register_interaction_progress(registry);
    register_interaction_companion(registry);
    register_interaction_letter(registry);
    register_interaction_rescue(registry);
    register_interaction_progression(registry);
    register_interaction_sales(registry);
    register_interaction_trade(registry);
    register_interaction_messages(registry);
    register_interaction_prompts(registry);
    register_interaction_party(registry);
    register_interaction_state(registry);
    register_interaction_story(registry);
    register_interaction_handoffs(registry);
    // Joining instructions no recording reaches (milestone 14, game/joins.h).
    register_joins(registry);
    return true;
}
}
