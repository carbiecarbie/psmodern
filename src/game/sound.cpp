// SPDX-License-Identifier: GPL-3.0-or-later
// Sound driver initialization and silence, split at the repeated output.
#include "game/menu_kit.h"
#include "game/routines.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
using sound::init; using sound::silence; using sound::clear;

// Reset the shared sound-control bytes. The copy reads through the bus, so
// disabled work RAM and unusual stack placements keep their original effects.
template<class Z> void clear_body(Z& z) {
    z.xor_a(0x802D);
    z.load_hl(0x802E);
    z.store_register_to_hl(0x8031,z.r.a);
    z.load_de(0x8032);
    z.load_bc(0x8035);
    z.block_copy(0x8038);
}
template<class Z> k::Exit clear_controls(Z& z) {
    clear_body(z);
    return z.final_return(0x803A);
}
// Begin initialization and process the first record. Each record ends with
// the step to the next one (add hl,de); later records use the same two
// segments.
template<class Z> void record_prefix(Z& z) {
    z.store_register_to_hl(0x800C,z.r.a);
    z.load_de(0x800D);
}
template<class Z> k::Exit step_to_next_record(Z& z) {
    z.add_to_hl(0x8010,z.de());
    return k::fall_through(0x8010,0x8011);
}
template<class Z> k::Exit initialize(Z& z) {
    z.push(0x8000,z.r.h,z.r.l);
    z.push(0x8001,z.r.d,z.r.e);
    z.push(0x8002,z.r.b,z.r.c);
    if (!z.inline_subroutine(0x8003,[](auto& in) {
        clear_body(in);
        return in.return_from(0x803A);
    })) return k::refused();
    z.load_immediate(0x8006,z.r.b);
    z.load_hl(0x8008);
    z.xor_a(0x800B);
    record_prefix(z);
    return step_to_next_record(z);
}
template<class Z> k::Exit next_record(Z& z) {
    record_prefix(z);
    return step_to_next_record(z);
}
template<class Z> k::Exit record_end(Z& z) {
    z.store_register_to_hl(0x8011,z.r.a);
    z.increment_hl(0x8012);
    z.store_register_to_hl(0x8013,z.r.a);
    z.increment_hl(0x8014);
    z.store_register_to_hl(0x8015,z.r.a);
    z.load_de(0x8016);
    z.add_to_hl(0x8019,z.de());
    return z.final_decrement_jump(0x801A);
}
// Mark channels that need restoring when the shared noise voice is active.
template<class Z> k::Exit mark_shared_channels(Z& z) {
    z.load_hl(0x80A4);
    z.test_bit_at_hl(0x80A7,7);
    if (auto exit=z.final_return_if(0x80A9,k::Condition::z)) return *exit;
    z.load_a_direct(0x80AA);
    z.or_a(0x80AD);
    if (z.jump_if(0x80AE,z.sign())) {
        z.set_bit_at_hl(0x80C6,2);
        return z.final_return(0x80C8);
    }
    z.test_bit_at_hl(0x80B1,6);
    if (!z.relative_if(0x80B3,z.zero())) {
        z.increment_hl(0x80B5);
        z.load_from_hl(0x80B6,z.r.a);
        z.compare_immediate(0x80B7);
        if (!z.relative_if(0x80B9,!z.zero())) {
            z.load_hl(0x80BB);
            z.set_bit_at_hl(0x80BE,2);
        }
    }
    z.load_hl(0x80C0);
    z.set_bit_at_hl(0x80C3,2);
    return z.final_return(0x80C5);
}
// Count down the shared timer. When it expires, reload it and advance the
// timing byte of each participating voice in the selected bank of records.
template<class Z> k::Exit advance_timer(Z& z) {
    z.load_hl(0x80C9);
    z.load_from_hl(0x80CC,z.r.a);
    z.or_a(0x80CD);
    if (auto exit=z.final_return_if(0x80CE,k::Condition::z)) return *exit;
    z.decrement_at_hl(0x80CF);
    if (auto exit=z.final_return_if(0x80D0,k::Condition::nz)) return *exit;
    z.load_a_direct(0x80D1);
    z.or_a(0x80D4);
    z.reset_bit(0x80D5,7,z.r.a);
    z.store_register_to_hl(0x80D7,z.r.a);
    z.load_hl(0x80D8);
    z.load_de(0x80DB);
    z.load_immediate(0x80DE,z.r.b);
    if (!z.jump_if(0x80E0,z.sign())) {
        z.load_hl(0x80E3);
        z.load_de(0x80E6);
        z.load_immediate(0x80E9,z.r.b);
    }
    do { z.increment_at_hl(0x80EB);
        z.add_to_hl(0x80EC,z.de());
    } while (z.decrement_jump(0x80ED));
    return z.final_return(0x80EF);
}
// Advance a fade. Positive and negative phases change each participating
// channel toward its own limit; completed phases return or hand off to the
// sound request/initialization routines at their original jump boundaries.
template<class Z> k::Exit advance_fade(Z& z) {
    using k::Index; constexpr auto ix=Index::ix;
    z.load_a_direct(0x80F0);
    z.or_a(0x80F3);
    if (auto exit=z.final_return_if(0x80F4,k::Condition::z)) return *exit;
    if (z.jump_if(0x80F5,z.sign())) {
        z.load_a_direct(0x8155);
        z.decrement(0x8158,z.r.a);
        if (!z.relative_if(0x8159,z.zero())) { z.store_a_direct(0x815B);
        return z.final_return(0x815E); }
        z.load_immediate(0x815F,z.r.a);
        z.store_a_direct(0x8161);
        z.load_a_direct(0x8164);
        z.increment(0x8167,z.r.a);
        z.compare_immediate(0x8168);
        z.store_a_direct(0x816A);
        if (!z.relative_if(0x816D,!z.zero())) {
            z.xor_a(0x816F);
            z.store_a_direct(0x8170);
            z.load_hl(0x8173);
            z.bus().fetch(0x8176);
            z.bus().fetch(0x8177);
            const auto value=z.bus().read(z.hl());
            z.bus().write(z.hl(),static_cast<std::uint8_t>(value&~4u));
            z.bus().advance(15);
            return z.final_return(0x8178);
        }
        z.load_index(0x8179,ix);
        z.load_de(0x817D);
        z.load_immediate(0x8180,z.r.b);
        do {
            z.load_from_indexed(0x8182,ix,z.r.a);
            z.decrement(0x8185,z.r.a);
            if (!z.jump_if(0x8186,z.sign())) {
                z.compare_indexed(0x8189,ix);
                if (!z.relative_if(0x818C,z.carry())) z.store_to_indexed(0x818E,ix,z.r.a);
            }
            z.add_to_index(0x8191,ix,z.de());
        } while (z.decrement_jump(0x8193));
        return z.final_return(0x8195);
    }
    z.load_a_direct(0x80F8);
    z.decrement(0x80FB,z.r.a);
    if (!z.relative_if(0x80FC,z.zero())) { z.store_a_direct(0x80FE);
    return z.final_return(0x8101); }
    z.load_a_direct(0x8102);
    z.store_a_direct(0x8105);
    z.load_a_direct(0x8108);
    z.increment(0x810B,z.r.a);
    z.compare_immediate(0x810C);
    z.store_a_direct(0x810E);
    if (!z.relative_if(0x8111,!z.zero())) {
        z.xor_a(0x8113);
        z.store_a_direct(0x8114);
        z.load_a_direct(0x8117);
        z.compare_immediate(0x811A);
        if (!z.relative_if(0x811C,!z.zero())) { z.load_immediate(0x811E,z.r.a);
        return z.final_jump(0x8120); }
        z.or_a(0x8123);
        return z.final_jump_if(0x8124,k::Condition::p);
    }
    z.load_index(0x812F,ix);
    z.load_de(0x8133);
    z.load_immediate(0x8136,z.r.b);
    z.load_a_direct(0x8138);
    z.or_a(0x813B);
    if (!z.jump_if(0x813C,!z.sign())) { z.load_index(0x813F,ix);
    z.load_immediate(0x8143,z.r.b); }
    do {
        z.load_from_indexed(0x8145,ix,z.r.a);
        z.increment(0x8148,z.r.a);
        z.compare_immediate(0x8149);
        if (!z.relative_if(0x814B,z.zero())) z.store_to_indexed(0x814D,ix,z.r.a);
        z.add_to_index(0x8150,ix,z.de());
    } while (z.decrement_jump(0x8152));
    return z.final_return(0x8154);
}
template<class Z> k::Exit fade_handoff(Z& z) {
    z.load_a_direct(0x8127);
    z.or_a(0x812A);
    if (auto exit=z.final_return_if(0x812B,k::Condition::z)) return *exit;
    return z.final_jump(0x812C);
}
// Request classification starts after the host-observed read at 82EF.
// Each conditional transfer ends a segment and is a boundary for the dispatcher.
template<class Z> k::Exit request_reset_test(Z& z) {
    z.test_bit(0x82F2,7,z.r.a);
    return z.final_jump_if(0x82F4,k::Condition::z);
}
template<class Z> k::Exit request_low_test(Z& z) {
    z.compare_immediate(0x82F7);
    return z.final_relative_if(0x82F9,k::Condition::c);
}
template<class Z> k::Exit request_song_test(Z& z) {
    z.compare_immediate(0x82FB);
    return z.final_jump_if(0x82FD,k::Condition::c);
}
template<class Z> k::Exit request_effect_test(Z& z) {
    z.compare_immediate(0x8300);
    return z.final_jump_if(0x8302,k::Condition::c);
}
template<class Z> k::Exit request_limit_test(Z& z) {
    z.compare_immediate(0x8305);
    return z.final_jump_if(0x8307,k::Condition::nc);
}
// Resolve the command table; its indirect jump ends the segment.
template<class Z> k::Exit request_command(Z& z) {
    z.subtract_immediate(0x830A);
    z.add_register(0x830C,z.r.a);
    z.copy(0x830D,z.r.c,z.r.a);
    z.load_immediate(0x830E,z.r.b);
    z.load_hl(0x8310);
    z.add_to_hl(0x8313,z.bc());
    z.load_from_hl(0x8314,z.r.a);
    z.increment_hl(0x8315);
    z.load_from_hl(0x8316,z.r.h);
    z.copy(0x8317,z.r.l,z.r.a);
    return z.final_jump_hl(0x8318);
}
template<class Z> k::Exit request_voice_lookup(Z& z) {
    z.subtract_immediate(0x8319);
    z.load_hl(0x831B);
    return z.final_call(0x831E);
}
template<class Z> k::Exit request_voice_setup(Z& z) {
    z.load_de(0x8321);
    z.load_immediate(0x8324,z.r.a);
    z.output(0x8326);
    z.load_immediate(0x8328,z.r.a);
    z.output(0x832A);
    return z.final_jump(0x832C);
}
template<class Z> k::Exit request_track_test(Z& z) {
    z.compare_immediate(0x832F);
    return z.final_jump_if(0x8331,k::Condition::nc);
}
template<class Z> k::Exit request_track_lookup(Z& z) {
    z.subtract_immediate(0x8334);
    if (auto exit=z.final_return_if(0x8336,k::Condition::m)) return *exit;
    z.load_immediate(0x8337,z.r.b);
    z.copy(0x8339,z.r.c,z.r.a);
    z.load_hl(0x833A);
    z.add_to_hl(0x833D,z.bc());
    return k::fall_through(0x833D,0x833E);
}
// Carry the chosen track into the shared state while preserving the caller's
// two saved values. The conditional transfer and calls end the segment.
template<class Z> k::Exit request_track_select(Z& z) {
    z.load_a_direct(0x833F);
    z.and_immediate(0x8342);
    z.store_a_direct(0x8344);
    z.load_from_hl(0x8347,z.r.a);
    z.store_a_direct(0x8348);
    z.store_a_direct(0x834B);
    z.push(0x834E,z.r.a,z.r.f);
    z.load_a_direct(0x834F);
    z.or_a(0x8352);
    return z.final_jump_if(0x8353,k::Condition::p);
}
template<class Z> k::Exit restore_voice_levels(Z& z) {
    using k::Index; constexpr auto ix=Index::ix;
    z.load_index(0x8356,ix);
    z.load_de(0x835A);
    z.load_immediate(0x835D,z.r.b);
    do {
        z.load_from_indexed(0x835F,ix,z.r.a);
        z.store_to_indexed(0x8362,ix,z.r.a);
        z.add_to_index(0x8365,ix,z.de());
    } while(z.decrement_jump(0x8367));
    return z.final_call(0x8369);
}
template<class Z> k::Exit choose_voice_bank(Z& z) {
    z.load_de(0x8370);
    z.or_a(0x8373);
    return z.final_jump_if(0x8374,k::Condition::m);
}
template<class Z> k::Exit set_voice_bank(Z& z) {
    z.load_de(0x837A);
    z.load_hl(0x837D);
    return k::fall_through(0x837D,0x8380);
}
// A handled request becomes idle, so it is not processed again next tick.
template<class Z> k::Exit acknowledge(Z& z) {
    z.load_immediate(0x8420,z.r.a);
    z.store_a_direct(0x8422);
    return z.final_return(0x8425);
}
// Resolve an indexed pair from a table in the player's ROM or work RAM.
template<class Z> k::Exit lookup_pair(Z& z) {
    z.add_register(0x8426,z.r.a);
    z.load_immediate(0x8427,z.r.b);
    z.copy(0x8429,z.r.c,z.r.a);
    z.add_to_hl(0x842A,z.bc());
    z.load_from_hl(0x842B,z.r.c);
    z.increment_hl(0x842C);
    z.load_from_hl(0x842D,z.r.b);
    return z.final_return(0x842E);
}
// The second lookup keeps the selected pair in HL instead of BC.
template<class Z> k::Exit lookup_address(Z& z) {
    z.copy(0x842F,z.r.c,z.r.a);
    z.load_immediate(0x8430,z.r.b);
    z.add_to_hl(0x8432,z.bc());
    z.add_to_hl(0x8433,z.bc());
    z.load_from_hl(0x8434,z.r.a);
    z.increment_hl(0x8435);
    z.load_from_hl(0x8436,z.r.h);
    z.copy(0x8437,z.r.l,z.r.a);
    return z.final_return(0x8438);
}
// Preserve the caller's registers while preparing the cartridge's eight PSG
// commands. The next segment performs the output when its window is safe.
template<class Z> k::Exit prepare_silence(Z& z) {
    z.push(0x801F,z.r.h,z.r.l);
    z.push(0x8020,z.r.b,z.r.c);
    z.load_hl(0x8021);
    z.load_immediate(0x8024,z.r.c);
    z.load_immediate(0x8026,z.r.b);
    return k::fall_through(0x8026,0x8028);
}
// Repeat the cartridge's block output only when the whole transfer fits in
// the admitted window. Each intermediate iteration adds its five-cycle
// repeat overhead; its transient flags and MEMPTR are overwritten by the
// next transfer. A frame or interrupt in the window makes the segment refuse the state at its entry (the run stops).
template<class Z> k::Exit send_silence(Z& z) {
    do z.output_increment_repeat(0x8028); while (z.r.b);
    z.pop(0x802A,z.r.b,z.r.c);
    z.pop(0x802B,z.r.h,z.r.l);
    return z.final_return(0x802C);
}
template<class Z> k::Exit finish_silence(Z& z) {
    z.pop(0x802A,z.r.b,z.r.c);
    z.pop(0x802B,z.r.h,z.r.l);
    return z.final_return(0x802C);
}
}
void register_sound(RoutineRegistry& registry) {
    register_sound_sequence(registry);
    register_sound_output(registry);
    register_sound_voice(registry);
    register_sound_envelope(registry);
    register_sound_tick(registry);
    register_sound_setup(registry);
    register_sound_notes(registry);
    register_sound_effects(registry);
    register_sound_request(registry);
    register_sound_bridge(registry);
    k::add_segment(registry,init,k::segment(init,{k::core_final_at(0x8010,-6)},{.scratch_stack=8}),[](auto& z) { return initialize(z); });
    k::add_segment(registry,{12,0x800C},k::segment(init,{k::core_final_at(0x8010)},{.inherited_stack=6}),[](auto& z) { return next_record(z); });
    k::add_segment(registry,{12,0x8011},k::segment(init,{k::conditional_at(0x801A)},{.inherited_stack=6}),[](auto& z) { return record_end(z); });
    k::add_segment(registry,{12,0x80A4},k::segment({12,0x80A4},{k::returned_at(0x80A9),k::returned_at(0x80C5),k::returned_at(0x80C8)}),[](auto& z) { return mark_shared_channels(z); });
    k::add_segment(registry,sound::timer,k::segment(sound::timer,{k::returned_at(0x80CE),k::returned_at(0x80D0),k::returned_at(0x80EF)}),[](auto& z) { return advance_timer(z); });
    k::add_segment(registry,sound::fade,k::segment(sound::fade,{k::returned_at(0x80F4),k::returned_at(0x8101),k::returned_at(0x8154),k::returned_at(0x815E),k::returned_at(0x8178),k::returned_at(0x8195),k::core_final_at(0x8120),k::conditional_at(0x8124)}),[](auto& z) { return advance_fade(z); });
    k::add_segment(registry,{12,0x8127},k::segment(sound::fade,{k::returned_at(0x812B),k::core_final_at(0x812C)}),[](auto& z) { return fade_handoff(z); });
    const auto dispatch=sound::request;
    k::add_segment(registry,{12,0x82F2},k::segment(dispatch,{k::conditional_at(0x82F4)}),[](auto& z) { return request_reset_test(z); });
    k::add_segment(registry,{12,0x82F7},k::segment(dispatch,{k::conditional_at(0x82F9)}),[](auto& z) { return request_low_test(z); });
    k::add_segment(registry,{12,0x82FB},k::segment(dispatch,{k::conditional_at(0x82FD)}),[](auto& z) { return request_song_test(z); });
    k::add_segment(registry,{12,0x8300},k::segment(dispatch,{k::conditional_at(0x8302)}),[](auto& z) { return request_effect_test(z); });
    k::add_segment(registry,{12,0x8305},k::segment(dispatch,{k::conditional_at(0x8307)}),[](auto& z) { return request_limit_test(z); });
    k::add_segment(registry,{12,0x830A},k::segment(dispatch,{k::core_final_at(0x8318)}),[](auto& z) { return request_command(z); });
    k::add_segment(registry,{12,0x8319},k::segment(dispatch,{k::core_final_at(0x831E)},{.scratch_stack=2}),[](auto& z) { return request_voice_lookup(z); });
    k::add_segment(registry,{12,0x8321},k::segment(dispatch,{k::core_final_at(0x832C)}),[](auto& z) { return request_voice_setup(z); });
    k::add_segment(registry,{12,0x832F},k::segment(dispatch,{k::conditional_at(0x8331)}),[](auto& z) { return request_track_test(z); });
    k::add_segment(registry,{12,0x8334},k::segment(dispatch,{k::returned_at(0x8336),k::core_final_at(0x833D)}),[](auto& z) { return request_track_lookup(z); });
    k::add_segment(registry,{12,0x833F},k::segment(dispatch,{k::conditional_at(0x8353,-2)},{.scratch_stack=2,.inherited_stack=2}),[](auto& z) { return request_track_select(z); });
    k::add_segment(registry,{12,0x8356},k::segment(dispatch,{k::core_final_at(0x8369)},{.scratch_stack=2,.inherited_stack=4}),[](auto& z) { return restore_voice_levels(z); });
    k::add_segment(registry,{12,0x8370},k::segment(dispatch,{k::conditional_at(0x8374)},{.inherited_stack=2}),[](auto& z) { return choose_voice_bank(z); });
    k::add_segment(registry,{12,0x837A},k::segment(dispatch,{k::core_final_at(0x837D)},{.inherited_stack=2}),[](auto& z) { return set_voice_bank(z); });
    k::add_segment(registry,sound::ack,k::segment(sound::ack,{k::returned_at(0x8425)}),[](auto& z) { return acknowledge(z); });
    k::add_segment(registry,sound::pair,k::segment(sound::pair,{k::returned_at(0x842E)}),[](auto& z) { return lookup_pair(z); });
    k::add_segment(registry,sound::address,k::segment(sound::address,{k::returned_at(0x8438)}),[](auto& z) { return lookup_address(z); });
    k::add_segment(registry,clear,k::segment(clear,{k::returned_at(0x803A)}),[](auto& z) { return clear_controls(z); });
    k::add_segment(registry,silence,k::segment(silence,{k::core_final_at(0x8026,-4)},{.scratch_stack=4}),[](auto& z) { return prepare_silence(z); });
    // Other port numbers can control mapping or interrupts. The game enters
    // with C=7F (the PSG port); the guard reads registers only, so it holds at
    // entry and stays valid across a delivery (C is not changed by the body).
    k::add_segment(registry,{12,0x8028},k::segment(silence,{k::returned_at(0x802C,4)},{.inherited_stack=4}),
        k::Guard{"the port register holds the PSG port (C=7F)",[](const Machine& machine) { return machine.cpu().bc.uint8_values.at_0==0x7f; }},
        [](auto& z) { return send_silence(z); });
    k::add_segment(registry,{12,0x802A},k::segment(silence,{k::returned_at(0x802C,4)},{.inherited_stack=4}),[](auto& z) { return finish_silence(z); });
    // Joining instructions between the segments above: the end of the
    // initialization's record loop restores the caller's registers and runs
    // into the silence routine (801C); the fade and request paths call the
    // silence and clear routines (8196, 836C, 8369); the track lookup saves
    // the selector (833E) and the shared-track path skips over its
    // alternative (8380).
    {
        auto restore=k::segment(init,{k::core_final_at(0x801E,4)},{.inherited_stack=6});
        k::add_segment(registry,{12,0x801C},std::move(restore),[](auto& z) {
            z.pop(0x801C,z.r.b,z.r.c);
            z.pop(0x801D,z.r.d,z.r.e);
            z.pop(0x801E,z.r.h,z.r.l);
            return k::fall_through(0x801E,0x801F);
        });
        for (const std::uint16_t at:{0x8196,0x8369,0x836C}) {
            auto call_routine=k::segment(at==0x8196 ? RoutineEntry{12,0x8196} : dispatch,{k::core_final_at(at)},{.scratch_stack=2});
            call_routine.single_instruction=true;
            k::add_segment(registry,{12,at},std::move(call_routine),[at](auto& z) { return z.final_call(at); });
        }
        auto save_selector=k::segment(dispatch,{k::core_final_at(0x833E)},{.scratch_stack=2});
        save_selector.single_instruction=true;
        k::add_segment(registry,{12,0x833E},std::move(save_selector),[](auto& z) {
            z.push(0x833E,z.r.a,z.r.f);
            return k::fall_through(0x833E,0x833F);
        });
        auto skip_alternative=k::segment(dispatch,{k::core_final_at(0x8380)});
        skip_alternative.single_instruction=true;
        k::add_segment(registry,{12,0x8380},std::move(skip_alternative),[](auto& z) { return z.final_relative(0x8380); });
    }
}
}
