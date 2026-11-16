// SPDX-License-Identifier: GPL-3.0-or-later
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
#include "rom/rom.h"
#include <bit>
#include <stdexcept>

namespace psmodern::game {
namespace {
void poll_controller(RoutineContext& context) {
    auto& cpu=context.cpu();

    // Read the active-low controller port at the original T3 offset. All ROM
    // operands still come from the player's image, in their original bus order.
    context.fetch_opcode(0x0339);
    const auto port=context.read(0x033a);
    const auto current=static_cast<std::uint8_t>(~context.read_port(
        (cpu.af.uint8_values.at_1<<8)|port,10));
    context.advance(1);
    context.fetch_opcode(0x033b);
    const auto low=context.read(0x033c), high=context.read(0x033d);
    const auto held_address=static_cast<std::uint16_t>(low|(high<<8));
    context.advance(10);

    context.fetch_opcode(0x033e); context.advance(4);
    context.fetch_opcode(0x033f); context.advance(4);
    context.fetch_opcode(0x0340);
    const auto previous=context.read(held_address);
    context.advance(7);
    context.fetch_opcode(0x0341); context.write(held_address,current); context.advance(7);
    context.fetch_opcode(0x0342); context.advance(6);

    // A held button is recorded every poll; an edge is recorded only once.
    const auto pressed=static_cast<std::uint8_t>(current & ~previous);
    context.fetch_opcode(0x0343); context.advance(4);
    context.fetch_opcode(0x0344);
    context.write(static_cast<std::uint16_t>(held_address+1),pressed);
    context.advance(7);
    cpu.bc.uint8_values.at_1=current;
    cpu.hl.uint16_value=static_cast<std::uint16_t>(held_address+1);
    cpu.af.uint8_values.at_1=pressed;
    cpu.af.uint8_values.at_0=static_cast<std::uint8_t>((pressed&0xa8)|0x10|
        (pressed==0 ? 0x40 : 0)|((std::popcount(pressed)&1)==0 ? 0x04 : 0));
    // The native return restores PC, SP, MEMPTR, Q and core bookkeeping. The
    // fixed 70-cycle bound includes these final ten cycles and the tenth fetch.
    context.finish_return(0x0345);
}
}
bool register_routines(RoutineRegistry& registry, std::string_view digest, const Options& options) {
    validate(options);
    try { (void)identify_digest(digest); }
    catch (const std::runtime_error&) { return false; }
    registry.add(controller_poll_entry,[](const Machine&) { return 70; },poll_controller);
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
    return true;
}
}
