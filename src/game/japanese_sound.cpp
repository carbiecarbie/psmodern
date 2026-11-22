// SPDX-License-Identifier: GPL-3.0-or-later
// Japanese cartridge FM sound driver. The private machine supplies all score,
// instrument and envelope data. The register operations share the game's
// existing cycle-accounted rendering kit.
#include "game/japanese_sound.h"
#include "game/menu_kit.h"

namespace psmodern::game {
namespace {
namespace k = menu_kit;
constexpr auto ix = k::Index::ix;

template<class Z> void clear_voice_bit(Z& z, std::uint16_t at, unsigned bit) {
    auto& bus = z.bus();
    bus.expect(at, OpcodePattern::prefixed(0xDD, 0xCB));
    bus.expect(at + 3, OpcodePattern::exactly(static_cast<std::uint8_t>(0x86 | (bit << 3))));
    bus.fetch(at);
    bus.fetch(at + 1);
    const auto displacement = static_cast<std::int8_t>(bus.read(at + 2));
    z.r.wz = static_cast<std::uint16_t>(z.r.ix + displacement);
    (void)bus.read(at + 3);
    bus.advance(4);
    const auto value = bus.read(z.r.wz);
    bus.write(z.r.wz, static_cast<std::uint8_t>(value & ~(1u << bit)));
    bus.advance(19);
}

template<class Z> void clear_pointed_bit(Z& z, std::uint16_t at, unsigned bit) {
    auto& bus = z.bus();
    bus.expect(at, OpcodePattern::prefixed(0xCB, static_cast<std::uint8_t>(0x86 | (bit << 3))));
    bus.fetch(at);
    bus.fetch(at + 1);
    const auto value = bus.read(z.hl());
    bus.write(z.hl(), static_cast<std::uint8_t>(value & ~(1u << bit)));
    bus.advance(15);
}

// OUT (C),r writes at T11. Updating WZ uses the full BC port address.
template<class Z> void output_frequency_byte(Z& z, std::uint16_t at, std::uint8_t value) {
    auto& bus = z.bus();
    bus.expect(at, OpcodePattern{0xED, 0x41, 0xC7});
    bus.fetch(at);
    bus.fetch(at + 1);
    z.r.wz = static_cast<std::uint16_t>(z.bc() + 1);
    bus.out(z.bc(), value, 11);
    bus.advance(1);
}

template<class Z> void double_frequency_low(Z& z, std::uint16_t at) {
    z.bus().expect(at, OpcodePattern::prefixed(0xCB, 0x25));
    z.bus().fetch(at);
    z.bus().fetch(at + 1);
    const auto previous = z.r.l;
    z.r.l = static_cast<std::uint8_t>(previous << 1);
    z.r.f = static_cast<std::uint8_t>(kit::sign_zero_xy(z.r.l) | kit::parity(z.r.l) | (previous >> 7));
    z.r.q = z.r.f;
    z.bus().advance(8);
}

template<class Z> bool wait_for_chip(Z& z, std::uint16_t call) {
    return z.inline_subroutine(call, [](auto& in) {
        in.push(0x8B34, in.r.h, in.r.l);
        in.pop(0x8B35, in.r.h, in.r.l);
        return in.return_from(0x8B36);
    });
}

// Clear the control bytes and deactivate fifteen voice records. Keep the caller's
// working registers through the fall-through to silence.
template<class Z> k::Exit initialize(Z& z) {
    z.push(0x8000, z.r.h, z.r.l);
    z.push(0x8001, z.r.d, z.r.e);
    z.push(0x8002, z.r.b, z.r.c);
    return z.final_call(0x8003);
}

template<class Z> k::Exit initialize_8006(Z& z) {
    z.load_immediate(0x8006, z.r.b);
    z.load_hl(0x8008);
    z.xor_a(0x800B);
    do {
        z.store_register_to_hl(0x800C, z.r.a);
        z.load_de(0x800D);
        z.add_to_hl(0x8010, z.de());
        z.store_register_to_hl(0x8011, z.r.a);
        z.increment_hl(0x8012);
        z.store_register_to_hl(0x8013, z.r.a);
        z.increment_hl(0x8014);
        z.store_register_to_hl(0x8015, z.r.a);
        z.load_de(0x8016);
        z.add_to_hl(0x8019, z.de());
    } while (z.decrement_jump(0x801A));
    z.pop(0x801C, z.r.b, z.r.c);
    z.pop(0x801D, z.r.d, z.r.e);
    z.pop(0x801E, z.r.h, z.r.l);
    return k::fall_through(0x801E, 0x801F);
}

// Send the cartridge's PSG mute sequence, then release the six FM melody keys.
template<class Z> k::Exit silence(Z& z) {
    z.push(0x801F, z.r.h, z.r.l);
    z.push(0x8020, z.r.b, z.r.c);
    z.load_hl(0x8021);
    z.load_immediate(0x8024, z.r.c);
    z.load_immediate(0x8026, z.r.b);
    do { z.output_increment_repeat(0x8028); } while (z.r.b);
    z.pop(0x802A, z.r.b, z.r.c);
    z.pop(0x802B, z.r.h, z.r.l);
    return k::fall_through(0x802B, 0x802C);
}

// D walks the six key-control registers. The address write, settling delay and
// zero data write are separate bus events.
template<class Z> k::Exit release_keys(Z& z) {
    z.push(0x802C, z.r.b, z.r.c);
    z.push(0x802D, z.r.d, z.r.e);
    z.load_immediate(0x802E, z.r.b);
    z.load_immediate(0x8030, z.r.d);
    return k::fall_through(0x8030, 0x8032);
}

template<class Z> k::Exit release_keys_8032(Z& z) {
    z.copy(0x8032, z.r.a, z.r.d);
    z.increment(0x8033, z.r.d);
    return z.final_call(0x8034);
}

template<class Z> k::Exit release_keys_8037(Z& z) {
    return z.final_decrement_jump(0x8037);
}

template<class Z> k::Exit release_keys_8039(Z& z) {
    z.pop(0x8039, z.r.d, z.r.e);
    z.pop(0x803A, z.r.b, z.r.c);
    return z.final_return(0x803B);
}

// Clear the control bytes by propagating a zero through work RAM.
template<class Z> k::Exit clear_controls(Z& z) {
    z.xor_a(0x803C);
    z.load_hl(0x803D);
    z.store_register_to_hl(0x8040, z.r.a);
    z.load_de(0x8041);
    z.load_bc(0x8044);
    z.block_copy(0x8047);
    return z.final_return(0x8049);
}

// Remember the FM selection in the alternate flags and install the note scratch
// pointer before timers, request dispatch and the voice pass.
template<class Z> k::Exit update(Z& z) {
    z.load_a_direct(0x8052);
    z.or_a(0x8055);
    z.exchange_af(0x8056);
    z.load_hl(0x8057);
    z.exchange_alternates(0x805A);
    return z.final_call(0x805B);
}

template<class Z> k::Exit update_805e(Z& z) {
    return z.final_call(0x805E);
}

template<class Z> k::Exit update_8061(Z& z) {
    return z.final_call(0x8061);
}

template<class Z> k::Exit update_8064(Z& z) {
    return z.final_call(0x8064);
}

template<class Z> k::Exit update_8067(Z& z) {
    z.load_a_direct(0x8067);
    z.or_a(0x806A);
    return z.final_jump_if(0x806B, k::Condition::z);
}

// The tempo selector chooses one of two overlapping voice groups. One record in
// each group uses the rhythm updater.
template<class Z> k::Exit visit_voices(Z& z) {
    z.load_a_direct(0x806E);
    z.or_a(0x8071);
    return z.final_jump_if(0x8072, k::Condition::m);
}

template<class Z> k::Exit visit_voices_8075(Z& z) {
    z.load_index(0x8075, ix);
    z.load_immediate(0x8079, z.r.b);
    return k::fall_through(0x8079, 0x807B);
}

template<class Z> k::Exit visit_voices_807b(Z& z) {
    z.push(0x807B, z.r.b, z.r.c);
    z.load_immediate(0x807C, z.r.a);
    z.compare_register(0x807E, z.r.b);
    return z.final_relative_if(0x807F, k::Condition::z);
}

template<class Z> k::Exit visit_voices_8081(Z& z) {
    z.test_bit_indexed(0x8081, ix, 7);
    return z.final_call_if(0x8085, k::Condition::nz);
}

template<class Z> k::Exit visit_voices_8088(Z& z) {
    z.load_de(0x8088);
    z.add_to_index(0x808B, ix, z.de());
    z.pop(0x808D, z.r.b, z.r.c);
    return z.final_decrement_jump(0x808E);
}

template<class Z> k::Exit visit_voices_8090(Z& z) {
    return z.final_return(0x8090);
}

template<class Z> k::Exit visit_voices_8091(Z& z) {
    z.test_bit_indexed(0x8091, ix, 7);
    return z.final_call_if(0x8095, k::Condition::nz);
}

template<class Z> k::Exit visit_voices_8098(Z& z) {
    return z.final_relative(0x8098);
}

template<class Z> k::Exit visit_voices_809a(Z& z) {
    z.load_index(0x809A, ix);
    z.load_immediate(0x809E, z.r.b);
    return k::fall_through(0x809E, 0x80A0);
}

template<class Z> k::Exit visit_voices_80a0(Z& z) {
    z.push(0x80A0, z.r.b, z.r.c);
    z.load_immediate(0x80A1, z.r.a);
    z.compare_register(0x80A3, z.r.b);
    return z.final_relative_if(0x80A4, k::Condition::z);
}

template<class Z> k::Exit visit_voices_80a6(Z& z) {
    z.test_bit_indexed(0x80A6, ix, 7);
    return z.final_call_if(0x80AA, k::Condition::nz);
}

template<class Z> k::Exit visit_voices_80ad(Z& z) {
    z.load_de(0x80AD);
    z.add_to_index(0x80B0, ix, z.de());
    z.pop(0x80B2, z.r.b, z.r.c);
    return z.final_decrement_jump(0x80B3);
}

template<class Z> k::Exit visit_voices_80b5(Z& z) {
    return z.final_return(0x80B5);
}

template<class Z> k::Exit visit_voices_80b6(Z& z) {
    z.test_bit_indexed(0x80B6, ix, 7);
    return z.final_call_if(0x80BA, k::Condition::nz);
}

template<class Z> k::Exit visit_voices_80bd(Z& z) {
    return z.final_relative(0x80BD);
}

// Set the inhibit bit on the music records when an effect takes their hardware
// channel.
template<class Z> k::Exit reserve_voices(Z& z) {
    z.load_hl(0x8110);
    z.test_bit_at_hl(0x8113, 7);
    if (auto exit = z.final_return_if(0x8115, k::Condition::z)) return *exit;
    z.load_a_direct(0x8116);
    z.or_a(0x8119);
    if (!z.jump_if(0x811A, k::Condition::m)) {
        z.test_bit_at_hl(0x811D, 6);
        if (!z.relative_if(0x811F, k::Condition::z)) {
            z.increment_hl(0x8121);
            z.load_from_hl(0x8122, z.r.a);
            z.compare_immediate(0x8123);
            if (!z.relative_if(0x8125, k::Condition::nz)) {
                z.load_hl(0x8127);
                z.set_bit_at_hl(0x812A, 2);
            }
        }
        z.load_hl(0x812C);
        z.set_bit_at_hl(0x812F, 2);
        return z.final_return(0x8131);
    }
    z.set_bit_at_hl(0x8132, 2);
    return z.final_return(0x8134);
}

// When the shared countdown wraps, reload it and advance the timing field in each
// record in the selected group.
template<class Z> k::Exit tempo_countdown(Z& z) {
    z.load_hl(0x8135);
    z.load_from_hl(0x8138, z.r.a);
    z.or_a(0x8139);
    if (auto exit = z.final_return_if(0x813A, k::Condition::z)) return *exit;
    z.decrement_at_hl(0x813B);
    if (auto exit = z.final_return_if(0x813C, k::Condition::nz)) return *exit;
    z.load_a_direct(0x813D);
    z.or_a(0x8140);
    z.reset_bit(0x8141, 7, z.r.a);
    z.store_register_to_hl(0x8143, z.r.a);
    z.load_hl(0x8144);
    z.load_de(0x8147);
    z.load_immediate(0x814A, z.r.b);
    if (!z.jump_if(0x814C, k::Condition::m)) {
        z.load_hl(0x814F);
        z.load_de(0x8152);
        z.load_immediate(0x8155, z.r.b);
    }
    do {
        z.increment_at_hl(0x8157);
        z.add_to_hl(0x8158, z.de());
    } while (z.decrement_jump(0x8159));
    return z.final_return(0x815B);
}

// Step the fade on its divider. At its limit, silence, restart the special song or
// begin restoring the suspended group.
template<class Z> k::Exit fade_out(Z& z) {
    z.load_a_direct(0x815C);
    z.or_a(0x815F);
    if (auto exit = z.final_return_if(0x8160, k::Condition::z)) return *exit;
    return z.final_jump_if(0x8161, k::Condition::m);
}

template<class Z> k::Exit fade_out_8164(Z& z) {
    z.load_a_direct(0x8164);
    z.decrement(0x8167, z.r.a);
    if (!z.relative_if(0x8168, k::Condition::z)) {
        z.store_a_direct(0x816A);
        return z.final_return(0x816D);
    }
    z.load_a_direct(0x816E);
    z.store_a_direct(0x8171);
    z.load_a_direct(0x8174);
    z.increment(0x8177, z.r.a);
    z.compare_immediate(0x8178);
    z.store_a_direct(0x817A);
    return z.final_relative_if(0x817D, k::Condition::nz);
}

template<class Z> k::Exit fade_out_817f(Z& z) {
    z.xor_a(0x817F);
    z.store_a_direct(0x8180);
    z.load_a_direct(0x8183);
    z.compare_immediate(0x8186);
    return z.final_relative_if(0x8188, k::Condition::nz);
}

template<class Z> k::Exit fade_out_818a(Z& z) {
    z.load_immediate(0x818A, z.r.a);
    return z.final_jump(0x818C);
}

template<class Z> k::Exit fade_out_818f(Z& z) {
    z.or_a(0x818F);
    return z.final_jump_if(0x8190, k::Condition::p);
}

template<class Z> k::Exit fade_out_8193(Z& z) {
    z.load_a_direct(0x8193);
    z.or_a(0x8196);
    if (auto exit = z.final_return_if(0x8197, k::Condition::z)) return *exit;
    return z.final_jump(0x8198);
}

template<class Z> k::Exit fade_out_819b(Z& z) {
    z.load_index(0x819B, ix);
    z.load_de(0x819F);
    z.load_immediate(0x81A2, z.r.b);
    z.load_a_direct(0x81A4);
    z.or_a(0x81A7);
    return z.final_jump_if(0x81A8, k::Condition::p);
}

template<class Z> k::Exit fade_out_81ab(Z& z) {
    z.load_index(0x81AB, ix);
    z.load_immediate(0x81AF, z.r.b);
    return k::fall_through(0x81AF, 0x81B1);
}

template<class Z> k::Exit fade_out_81b1(Z& z) {
    z.load_from_indexed(0x81B1, ix, z.r.a);
    z.increment(0x81B4, z.r.a);
    z.compare_immediate(0x81B5);
    return z.final_relative_if(0x81B7, k::Condition::z);
}

template<class Z> k::Exit fade_out_81b9(Z& z) {
    z.store_to_indexed(0x81B9, ix, z.r.a);
    z.load_a_direct(0x81BC);
    z.or_a(0x81BF);
    return z.final_call_if(0x81C0, k::Condition::nz);
}

template<class Z> k::Exit fade_out_81c3(Z& z) {
    z.add_to_index(0x81C3, ix, z.de());
    return z.final_decrement_jump(0x81C5);
}

template<class Z> k::Exit fade_out_81c7(Z& z) {
    return z.final_return(0x81C7);
}

template<class Z> k::Exit fade_out_81c8(Z& z) {
    z.test_bit_indexed(0x81C8, ix, 2);
    if (auto exit = z.final_return_if(0x81CC, k::Condition::nz)) return *exit;
    return z.final_jump(0x81CD);
}

// Decrease attenuation toward each saved level until the restore counter expires.
// Re-enable the reserved record at the end.
template<class Z> k::Exit fade_in(Z& z) {
    z.load_a_direct(0x81D0);
    z.decrement(0x81D3, z.r.a);
    if (!z.relative_if(0x81D4, k::Condition::z)) {
        z.store_a_direct(0x81D6);
        return z.final_return(0x81D9);
    }
    z.load_immediate(0x81DA, z.r.a);
    z.store_a_direct(0x81DC);
    z.load_a_direct(0x81DF);
    z.increment(0x81E2, z.r.a);
    z.compare_immediate(0x81E3);
    z.store_a_direct(0x81E5);
    if (!z.relative_if(0x81E8, k::Condition::nz)) {
        z.xor_a(0x81EA);
        z.store_a_direct(0x81EB);
        z.load_hl(0x81EE);
        clear_pointed_bit(z, 0x81F1, 2);
        return z.final_return(0x81F3);
    }
    z.load_index(0x81F4, ix);
    z.load_de(0x81F8);
    z.load_immediate(0x81FB, z.r.b);
    return k::fall_through(0x81FB, 0x81FD);
}

template<class Z> k::Exit fade_in_81fd(Z& z) {
    z.load_from_indexed(0x81FD, ix, z.r.a);
    z.decrement(0x8200, z.r.a);
    return z.final_jump_if(0x8201, k::Condition::m);
}

template<class Z> k::Exit fade_in_8204(Z& z) {
    z.compare_indexed(0x8204, ix);
    return z.final_relative_if(0x8207, k::Condition::c);
}

template<class Z> k::Exit fade_in_8209(Z& z) {
    z.store_to_indexed(0x8209, ix, z.r.a);
    z.load_a_direct(0x820C);
    z.or_a(0x820F);
    return z.final_call_if(0x8210, k::Condition::nz);
}

template<class Z> k::Exit fade_in_8213(Z& z) {
    z.add_to_index(0x8213, ix, z.de());
    return z.final_decrement_jump(0x8215);
}

template<class Z> k::Exit fade_in_8217(Z& z) {
    return z.final_return(0x8217);
}

// Restore the pre-effect tempo and save each music voice's original attenuation
// before beginning its fade back in.
template<class Z> k::Exit restore_group(Z& z) {
    return z.final_call(0x8218);
}

template<class Z> k::Exit restore_group_821b(Z& z) {
    z.load_a_direct(0x821B);
    z.store_a_direct(0x821E);
    z.load_immediate(0x8221, z.r.a);
    z.store_a_direct(0x8223);
    z.load_immediate(0x8226, z.r.a);
    z.store_a_direct(0x8228);
    z.load_hl(0x822B);
    z.set_bit_at_hl(0x822E, 2);
    z.push_index(0x8230, ix);
    z.load_index(0x8232, ix);
    z.load_immediate(0x8236, z.r.b);
    z.load_de(0x8238);
    do {
        z.load_from_indexed(0x823B, ix, z.r.a);
        z.store_to_indexed(0x823E, ix, z.r.a);
        z.load_immediate(0x8241, z.r.a);
        z.store_to_indexed(0x8243, ix, z.r.a);
        z.add_to_index(0x8246, ix, z.de());
    } while (z.decrement_jump(0x8248));
    z.pop_index(0x824A, ix);
    return z.final_jump(0x824C);
}

// Start the shared fade divider, adjust the special voice and mute the PSG noise
// output.
template<class Z> k::Exit begin_fade(Z& z) {
    z.load_immediate(0x824F, z.r.a);
    z.store_a_direct(0x8251);
    z.store_a_direct(0x8254);
    return k::fall_through(0x8254, 0x8257);
}

template<class Z> k::Exit begin_fade_8257(Z& z) {
    z.load_immediate(0x8257, z.r.a);
    z.store_a_direct(0x8259);
    z.load_a_direct(0x825C);
    z.or_a(0x825F);
    if (!z.jump_if(0x8260, k::Condition::m)) {
        z.xor_a(0x8263);
        z.store_a_direct(0x8264);
    }
    z.load_immediate(0x8267, z.r.a);
    z.output(0x8269);
    z.xor_a(0x826B);
    z.store_a_direct(0x826C);
    return z.final_jump(0x826F);
}

// Release the selected FM key, leaving the original chip access delay between
// address and data.
template<class Z> k::Exit write_key_off(Z& z) {
    z.output(0x8272);
    if (!wait_for_chip(z, 0x8274)) return k::refused();
    z.xor_a(0x8277);
    z.output(0x8278);
    return z.final_return(0x827A);
}

// Stop the shared effect voice and mute its PSG and FM channels. This control
// deliberately leaves the request pending.
template<class Z> k::Exit silence_effect(Z& z) {
    z.xor_a(0x827B);
    z.store_a_direct(0x827C);
    z.load_immediate(0x827F, z.r.a);
    z.output(0x8281);
    z.load_immediate(0x8283, z.r.a);
    z.output(0x8285);
    z.load_immediate(0x8287, z.r.a);
    z.output(0x8289);
    if (!wait_for_chip(z, 0x828B)) return k::refused();
    z.xor_a(0x828E);
    z.output(0x828F);
    return z.final_return(0x8291);
}

// Read register/value pairs from the cartridge. The two OUTI transfers each
// advance the source pointer and decrement the byte count.
template<class Z> k::Exit program_instruments(Z& z) {
    z.push(0x8292, z.r.b, z.r.c);
    z.load_immediate(0x8293, z.r.b);
    z.load_hl(0x8295);
    z.load_immediate(0x8298, z.r.c);
    do {
        z.decrement(0x829A, z.r.c);
        z.output_increment(0x829B);
        z.increment(0x829D, z.r.c);
        if (!wait_for_chip(z, 0x829E)) return k::refused();
        z.output_increment(0x82A1);
        if (!wait_for_chip(z, 0x82A3)) return k::refused();
    } while (z.relative_if(0x82A6, k::Condition::nz));
    z.pop(0x82A8, z.r.b, z.r.c);
    return z.final_return(0x82A9);
}

// Keep a resumption flag when the tempo selector permits it, then start fading.
template<class Z> k::Exit suspend_group(Z& z) {
    z.load_a_direct(0x82AA);
    z.compare_immediate(0x82AD);
    return z.final_jump_if(0x82AF, k::Condition::z);
}

template<class Z> k::Exit suspend_group_82b2(Z& z) {
    z.load_immediate(0x82B2, z.r.a);
    z.store_a_direct(0x82B4);
    return z.final_jump(0x82B7);
}

// Free the stolen effect voices in both groups and restore instruments to the two
// music channels that can become audible again.
template<class Z> k::Exit release_effects(Z& z) {
    z.xor_a(0x82BA);
    z.store_a_direct(0x82BB);
    z.store_a_direct(0x82BE);
    z.load_hl(0x82C1);
    return z.final_call(0x82C4);
}

template<class Z> k::Exit release_effects_82c7(Z& z) {
    z.load_hl(0x82C7);
    return z.final_call(0x82CA);
}

template<class Z> k::Exit release_effects_82cd(Z& z) {
    z.exchange_af(0x82CD);
    return z.final_relative_if(0x82CE, k::Condition::nz);
}

// Release the hardware keys before restoring the attenuations of the underlying
// music voices.
template<class Z> k::Exit restore_effect_keys(Z& z) {
    z.exchange_af(0x82DD);
    z.load_immediate(0x82DE, z.r.a);
    return z.final_call(0x82E0);
}

template<class Z> k::Exit restore_effect_keys_82e3(Z& z) {
    if (!wait_for_chip(z, 0x82E3)) return k::refused();
    z.load_immediate(0x82E6, z.r.a);
    return z.final_call(0x82E8);
}

template<class Z> k::Exit restore_effect_keys_82eb(Z& z) {
    z.load_a_direct(0x82EB);
    z.or_a(0x82EE);
    return z.final_jump_if(0x82EF, k::Condition::m);
}

template<class Z> k::Exit restore_effect_keys_82f2(Z& z) {
    z.push_index(0x82F2, ix);
    z.load_index(0x82F4, ix);
    return z.final_call(0x82F8);
}

template<class Z> k::Exit restore_effect_keys_82fb(Z& z) {
    z.load_index(0x82FB, ix);
    return z.final_call(0x82FF);
}

template<class Z> k::Exit restore_effect_keys_8302(Z& z) {
    z.pop_index(0x8302, ix);
    return z.final_jump(0x8304);
}

// Free three records spaced one voice apart. The second group includes its
// trailing work byte.
template<class Z> k::Exit clear_reservations(Z& z) {
    z.load_de(0x8307);
    z.load_immediate(0x830A, z.r.b);
    do {
        clear_pointed_bit(z, 0x830C, 2);
        z.add_to_hl(0x830E, z.de());
    } while (z.decrement_jump(0x830F));
    return z.final_return(0x8311);
}

// Classify the pending command, then use the cartridge's control table for the
// five control requests.
template<class Z> k::Exit dispatch_request(Z& z) {
    z.load_a_direct(0x8466);
    z.test_bit(0x8469, 7, z.r.a);
    return z.final_jump_if(0x846B, k::Condition::z);
}

template<class Z> k::Exit dispatch_request_846e(Z& z) {
    z.compare_immediate(0x846E);
    return z.final_relative_if(0x8470, k::Condition::c);
}

template<class Z> k::Exit dispatch_request_8472(Z& z) {
    z.compare_immediate(0x8472);
    return z.final_jump_if(0x8474, k::Condition::c);
}

template<class Z> k::Exit dispatch_request_8477(Z& z) {
    z.compare_immediate(0x8477);
    return z.final_jump_if(0x8479, k::Condition::c);
}

template<class Z> k::Exit dispatch_request_847c(Z& z) {
    z.compare_immediate(0x847C);
    return z.final_jump_if(0x847E, k::Condition::nc);
}

template<class Z> k::Exit dispatch_request_8481(Z& z) {
    z.subtract_immediate(0x8481);
    z.add_register(0x8483, z.r.a);
    z.copy(0x8484, z.r.c, z.r.a);
    z.load_immediate(0x8485, z.r.b);
    z.load_hl(0x8487);
    z.add_to_hl(0x848A, z.bc());
    z.load_from_hl(0x848B, z.r.a);
    z.increment_hl(0x848C);
    z.load_from_hl(0x848D, z.r.h);
    z.copy(0x848E, z.r.l, z.r.a);
    return z.final_jump_hl(0x848F);
}

// Choose the correct effect list and mute the shared PSG voices before installing
// the selected effect.
template<class Z> k::Exit start_shared_effect(Z& z) {
    z.load_hl(0x8490);
    z.subtract_immediate(0x8493);
    z.exchange_af(0x8495);
    if (!z.relative_if(0x8496, k::Condition::nz)) {
        z.load_hl(0x8498);
    }
    z.exchange_af(0x849B);
    return z.final_call(0x849C);
}

template<class Z> k::Exit start_shared_effect_849f(Z& z) {
    z.load_de(0x849F);
    z.load_immediate(0x84A2, z.r.a);
    z.output(0x84A4);
    z.load_immediate(0x84A6, z.r.a);
    z.output(0x84A8);
    return z.final_jump(0x84AA);
}

// Reject nonmusic requests, preserve the outgoing tempo, and select the new song's
// tempo and record group. Undo an unfinished fade before resetting.
template<class Z> k::Exit start_song(Z& z) {
    z.compare_immediate(0x84AD);
    return z.final_jump_if(0x84AF, k::Condition::nc);
}

template<class Z> k::Exit start_song_84b2(Z& z) {
    z.subtract_immediate(0x84B2);
    if (auto exit = z.final_return_if(0x84B4, k::Condition::m)) return *exit;
    z.load_immediate(0x84B5, z.r.b);
    z.copy(0x84B7, z.r.c, z.r.a);
    z.load_hl(0x84B8);
    z.add_to_hl(0x84BB, z.bc());
    z.push(0x84BC, z.r.a, z.r.f);
    z.load_a_direct(0x84BD);
    z.and_immediate(0x84C0);
    z.store_a_direct(0x84C2);
    z.load_from_hl(0x84C5, z.r.a);
    z.store_a_direct(0x84C6);
    z.store_a_direct(0x84C9);
    z.push(0x84CC, z.r.a, z.r.f);
    z.load_a_direct(0x84CD);
    z.or_a(0x84D0);
    if (!z.jump_if(0x84D1, k::Condition::p)) {
        z.load_index(0x84D4, ix);
        z.load_de(0x84D8);
        z.load_immediate(0x84DB, z.r.b);
        do {
            z.load_from_indexed(0x84DD, ix, z.r.a);
            z.store_to_indexed(0x84E0, ix, z.r.a);
            z.add_to_index(0x84E3, ix, z.de());
        } while (z.decrement_jump(0x84E5));
    }
    return z.final_call(0x84E7);
}

template<class Z> k::Exit start_song_84ea(Z& z) {
    return z.final_call(0x84EA);
}

template<class Z> k::Exit start_song_84ed(Z& z) {
    z.pop(0x84ED, z.r.a, z.r.f);
    z.exchange_af(0x84EE);
    return z.final_relative_if(0x84EF, k::Condition::nz);
}

// Program FM instruments and select the large or small record group before looking
// up the song header.
template<class Z> k::Exit select_song(Z& z) {
    z.exchange_af(0x8504);
    return z.final_call(0x8505);
}

template<class Z> k::Exit select_song_8508(Z& z) {
    z.load_de(0x8508);
    z.or_a(0x850B);
    return z.final_jump_if(0x850C, k::Condition::m);
}

template<class Z> k::Exit select_song_850f(Z& z) {
    z.load_de(0x850F);
    return z.final_call(0x8512);
}

template<class Z> k::Exit select_song_8515(Z& z) {
    return z.final_relative(0x8515);
}

template<class Z> k::Exit select_song_8517(Z& z) {
    return z.final_call(0x8517);
}

template<class Z> k::Exit select_song_851a(Z& z) {
    z.load_hl(0x851A);
    z.pop(0x851D, z.r.a, z.r.f);
    return z.final_call(0x851E);
}

template<class Z> k::Exit select_song_8521(Z& z) {
    return z.final_jump(0x8521);
}

// Find the FM effect header and decode its requested channel. Reserve music voices
// where an effect shares the same hardware channel.
template<class Z> k::Exit start_effect(Z& z) {
    z.subtract_immediate(0x8524);
    z.load_hl(0x8526);
    z.exchange_af(0x8529);
    if (!z.relative_if(0x852A, k::Condition::z)) {
        z.load_hl(0x852C);
    }
    z.exchange_af(0x852F);
    return z.final_call(0x8530);
}

template<class Z> k::Exit start_effect_8533(Z& z) {
    z.copy(0x8533, z.r.h, z.r.b);
    z.copy(0x8534, z.r.l, z.r.c);
    z.increment_hl(0x8535);
    z.increment_hl(0x8536);
    z.load_from_hl(0x8537, z.r.a);
    z.exchange_af(0x8538);
    return z.final_relative_if(0x8539, k::Condition::z);
}

template<class Z> k::Exit start_effect_853b(Z& z) {
    z.exchange_af(0x853B);
    z.compare_immediate(0x853C);
    return z.final_relative_if(0x853E, k::Condition::z);
}

template<class Z> k::Exit start_effect_8540(Z& z) {
    z.compare_immediate(0x8540);
    return z.final_relative_if(0x8542, k::Condition::z);
}

template<class Z> k::Exit start_effect_8544(Z& z) {
    z.load_de(0x8544);
    z.load_immediate(0x8547, z.r.a);
    return z.final_relative(0x8549);
}

template<class Z> k::Exit start_effect_854b(Z& z) {
    return z.final_call(0x854B);
}

template<class Z> k::Exit start_effect_854e(Z& z) {
    z.load_de(0x854E);
    return z.final_relative(0x8551);
}

template<class Z> k::Exit start_effect_8553(Z& z) {
    z.load_de(0x8553);
    z.load_immediate(0x8556, z.r.a);
    z.load_hl(0x8558);
    z.set_bit_at_hl(0x855B, 2);
    return k::fall_through(0x855B, 0x855D);
}

template<class Z> k::Exit start_effect_855d(Z& z) {
    z.load_hl(0x855D);
    z.set_bit_at_hl(0x8560, 2);
    return z.final_call(0x8562);
}

template<class Z> k::Exit start_effect_8565(Z& z) {
    return z.final_relative(0x8565);
}

// Copy each nine-byte voice header from the cartridge, initialize playback
// counters and program its instrument and attenuation.
template<class Z> k::Exit install_voices(Z& z) {
    z.copy(0x85C6, z.r.h, z.r.b);
    z.copy(0x85C7, z.r.l, z.r.c);
    z.load_from_hl(0x85C8, z.r.b);
    z.increment_hl(0x85C9);
    return k::fall_through(0x85C9, 0x85CA);
}

template<class Z> k::Exit install_voices_85ca(Z& z) {
    z.push(0x85CA, z.r.b, z.r.c);
    z.push(0x85CB, z.r.h, z.r.l);
    z.pop_index(0x85CC, ix);
    z.load_bc(0x85CE);
    z.block_copy(0x85D1);
    z.load_immediate(0x85D3, z.r.a);
    z.store_a_to_pair(0x85D5, z.de());
    z.increment_pair(0x85D6, z.r.d, z.r.e);
    z.load_immediate(0x85D7, z.r.a);
    z.store_a_to_pair(0x85D9, z.de());
    z.increment_pair(0x85DA, z.r.d, z.r.e);
    z.xor_a(0x85DB);
    z.store_a_to_pair(0x85DC, z.de());
    z.increment_pair(0x85DD, z.r.d, z.r.e);
    z.store_a_to_pair(0x85DE, z.de());
    z.increment_pair(0x85DF, z.r.d, z.r.e);
    z.store_a_to_pair(0x85E0, z.de());
    z.push(0x85E1, z.r.h, z.r.l);
    z.load_hl(0x85E2);
    z.add_to_hl(0x85E5, z.de());
    z.exchange_de_hl(0x85E6);
    z.pop(0x85E7, z.r.h, z.r.l);
    z.load_bc(0x85E8);
    z.push(0x85EB, z.r.b, z.r.c);
    z.load_a_direct(0x85EC);
    z.or_a(0x85EF);
    return z.final_jump_if(0x85F0, k::Condition::nz);
}

// Continue with the next header. Acknowledge a fully dispatched request with the
// idle value.
template<class Z> k::Exit finish_voice_setup(Z& z) {
    z.pop(0x85F6, z.r.b, z.r.c);
    return z.final_decrement_jump(0x85F7);
}

template<class Z> k::Exit finish_voice_setup_85f9(Z& z) {
    z.load_immediate(0x85F9, z.r.a);
    z.store_a_direct(0x85FB);
    return z.final_return(0x85FE);
}

// Read a little-endian pointer from the cartridge table selected by the caller.
template<class Z> k::Exit read_pointer(Z& z) {
    z.add_register(0x85FF, z.r.a);
    z.load_immediate(0x8600, z.r.b);
    z.copy(0x8602, z.r.c, z.r.a);
    z.add_to_hl(0x8603, z.bc());
    z.load_from_hl(0x8604, z.r.c);
    z.increment_hl(0x8605);
    z.load_from_hl(0x8606, z.r.b);
    return z.final_return(0x8607);
}

// Age the percussion note. Release the drums two ticks before expiry and write the
// new key mask when its next event is due.
template<class Z> k::Exit advance_rhythm(Z& z) {
    z.increment_indexed(0x8608, ix);
    z.load_from_indexed(0x860B, ix, z.r.a);
    z.subtract_indexed(0x860E, ix);
    return z.final_relative_if(0x8611, k::Condition::nz);
}

template<class Z> k::Exit advance_rhythm_8613(Z& z) {
    return z.final_call(0x8613);
}

template<class Z> k::Exit advance_rhythm_8616(Z& z) {
    z.test_bit_indexed(0x8616, ix, 2);
    if (auto exit = z.final_return_if(0x861A, k::Condition::nz)) return *exit;
    z.load_immediate(0x861B, z.r.a);
    z.output(0x861D);
    z.load_from_indexed(0x861F, ix, z.r.a);
    z.or_immediate(0x8622);
    z.output(0x8624);
    return z.final_return(0x8626);
}

template<class Z> k::Exit advance_rhythm_8627(Z& z) {
    z.compare_immediate(0x8627);
    if (auto exit = z.final_return_if(0x8629, k::Condition::nz)) return *exit;
    z.load_immediate(0x862A, z.r.a);
    z.output(0x862C);
    if (!wait_for_chip(z, 0x862E)) return k::refused();
    z.load_immediate(0x8631, z.r.a);
    z.output(0x8633);
    return z.final_return(0x8635);
}

// Read the next percussion event. A command saves the rhythm reader continuation.
// A drum hit expands the coupled drum bits before consuming its duration.
template<class Z> k::Exit read_rhythm(Z& z) {
    z.load_from_indexed(0x8636, ix, z.r.e);
    z.load_from_indexed(0x8639, ix, z.r.d);
    return k::fall_through(0x8639, 0x863C);
}

template<class Z> k::Exit read_rhythm_863c(Z& z) {
    z.load_a_from_pair(0x863C, z.de());
    z.increment_pair(0x863D, z.r.d, z.r.e);
    z.compare_immediate(0x863E);
    return z.final_jump_if(0x8640, k::Condition::nc);
}

template<class Z> k::Exit read_rhythm_8643(Z& z) {
    z.compare_immediate(0x8643);
    return z.final_jump_if(0x8645, k::Condition::c);
}

template<class Z> k::Exit read_rhythm_8648(Z& z) {
    z.test_bit(0x8648, 5, z.r.a);
    if (!z.relative_if(0x864A, k::Condition::z)) {
        z.or_immediate(0x864C);
    }
    z.test_bit(0x864E, 2, z.r.a);
    if (!z.relative_if(0x8650, k::Condition::z)) {
        z.or_immediate(0x8652);
    }
    z.store_to_indexed(0x8654, ix, z.r.a);
    return z.final_jump(0x8657);
}

template<class Z> k::Exit read_rhythm_865a(Z& z) {
    z.load_hl(0x865A);
    return z.final_jump(0x865D);
}

template<class Z> k::Exit read_rhythm_8660(Z& z) {
    z.increment_pair(0x8660, z.r.d, z.r.e);
    return z.final_jump(0x8661);
}

// Age the melody note; fetch its next event when due. Pitch drift uses two
// adjacent bytes, with a different high-byte step for explicit and table-based
// pitches.
template<class Z> k::Exit advance_melody(Z& z) {
    z.increment_indexed(0x8664, ix);
    z.load_from_indexed(0x8667, ix, z.r.a);
    z.subtract_indexed(0x866A, ix);
    return z.final_call_if(0x866D, k::Condition::z);
}

template<class Z> k::Exit advance_melody_8670(Z& z) {
    z.store_a_direct(0x8670);
    z.compare_immediate(0x8673);
    return z.final_jump_if(0x8675, k::Condition::z);
}

template<class Z> k::Exit advance_melody_8678(Z& z) {
    z.test_bit_indexed(0x8678, ix, 5);
    return z.final_jump_if(0x867C, k::Condition::z);
}

template<class Z> k::Exit advance_melody_867f(Z& z) {
    z.exchange_alternates(0x867F);
    z.store_to_hl(0x8680);
    z.exchange_alternates(0x8682);
    z.test_bit_indexed(0x8683, ix, 3);
    // Table-based pitches use two high-byte steps when the low byte wraps;
    // an explicit period uses one. A wrap also marks the key register dirty.
    bool wrapped;
    if (z.jump_if(0x8687, k::Condition::nz)) {
        z.load_from_indexed(0x86AD, ix, z.r.a);
        z.test_bit(0x86B0, 7, z.r.a);
        if (z.relative_if(0x86B2, k::Condition::z)) {
            z.add_from_indexed(0x86BE, ix);
            wrapped = !z.relative_if(0x86C1, k::Condition::nc);
            if (wrapped) z.increment_indexed(0x86C3, ix);
        } else {
            z.add_from_indexed(0x86B4, ix);
            wrapped = !z.relative_if(0x86B7, k::Condition::c);
            if (wrapped) {
                z.decrement_indexed(0x86B9, ix);
                z.relative(0x86BC);
            }
        }
    } else {
        z.load_from_indexed(0x868A, ix, z.r.a);
        z.test_bit(0x868D, 7, z.r.a);
        if (z.relative_if(0x868F, k::Condition::z)) {
            z.add_from_indexed(0x869F, ix);
            wrapped = !z.relative_if(0x86A2, k::Condition::nc);
            if (wrapped) {
                z.increment_indexed(0x86A4, ix);
                z.increment_indexed(0x86A7, ix);
                z.jump(0x86AA);
            }
        } else {
            z.add_from_indexed(0x8691, ix);
            wrapped = !z.relative_if(0x8694, k::Condition::c);
            if (wrapped) {
                z.decrement_indexed(0x8696, ix);
                z.decrement_indexed(0x8699, ix);
                z.jump(0x869C);
            }
        }
    }
    if (wrapped) z.set_bit_indexed(0x86C6, ix, 1);
    z.store_to_indexed(0x86CA, ix, z.r.a);
    return k::fall_through(0x86CA, 0x86CD);
}

// An inhibited or ended voice stays quiet. Otherwise apply the envelope, convert
// explicit periods, and update the low and high frequency registers.
template<class Z> k::Exit play_melody(Z& z) {
    z.test_bit_indexed(0x86CD, ix, 2);
    if (auto exit = z.final_return_if(0x86D1, k::Condition::nz)) return *exit;
    z.load_from_indexed(0x86D2, ix, z.r.a);
    z.compare_immediate(0x86D5);
    if (auto exit = z.final_return_if(0x86D7, k::Condition::z)) return *exit;
    z.load_a_direct(0x86D8);
    z.test_bit_indexed(0x86DB, ix, 0);
    return z.final_relative_if(0x86DF, k::Condition::nz);
}

template<class Z> k::Exit play_melody_86e1(Z& z) {
    z.compare_immediate(0x86E1);
    return z.final_jump_if(0x86E3, k::Condition::c);
}

template<class Z> k::Exit play_melody_86e6(Z& z) {
    z.or_a(0x86E6);
    if (z.jump_if(0x86E7, k::Condition::m)) {
        z.load_from_indexed(0x86F7, ix, z.r.a);
        z.decrement(0x86FA, z.r.a);
    } else {
        z.test_bit_indexed(0x86EA, ix, 7);
        if (auto exit = z.final_return_if(0x86EE, k::Condition::nz)) return *exit;
        z.load_from_indexed(0x86EF, ix, z.r.a);
        z.decrement(0x86F2, z.r.a);
        if (!z.jump_if(0x86F3, k::Condition::p)) return z.final_return(0x86F6);
    }
    z.load_from_indexed(0x86FB, ix, z.r.l);
    z.load_from_indexed(0x86FE, ix, z.r.h);
    return z.final_jump_if(0x8701, k::Condition::m);
}

template<class Z> k::Exit play_melody_8704(Z& z) {
    z.exchange_de_hl(0x8704);
    z.load_hl(0x8705);
    return z.final_call(0x8708);
}

template<class Z> k::Exit play_melody_870b(Z& z) {
    return z.final_call(0x870B);
}

template<class Z> k::Exit play_melody_870e(Z& z) {
    z.test_bit_indexed(0x870E, ix, 3);
    return z.final_call_if(0x8712, k::Condition::nz);
}

template<class Z> k::Exit play_melody_8715(Z& z) {
    z.load_immediate(0x8715, z.r.c);
    z.load_from_indexed(0x8717, ix, z.r.a);
    z.output(0x871A);
    z.add_immediate(0x871C);
    if (!wait_for_chip(z, 0x871E)) return k::refused();
    output_frequency_byte(z, 0x8721, z.r.l);
    if (!wait_for_chip(z, 0x8723)) return k::refused();
    z.exchange_alternates(0x8726);
    z.test_bit_at_hl(0x8727, 7);
    z.exchange_alternates(0x8729);
    z.output(0x872A);
    if (!z.relative_if(0x872C, k::Condition::nz)) {
        z.test_bit_indexed(0x872E, ix, 0);
        if (!z.relative_if(0x8732, k::Condition::z)) {
            z.test_bit_indexed(0x8734, ix, 1);
            if (auto exit = z.final_return_if(0x8738, k::Condition::z)) return *exit;
            clear_voice_bit(z, 0x8739, 1);
        }
    }
    z.test_bit_indexed(0x873D, ix, 2);
    if (!z.relative_if(0x8741, k::Condition::z)) {
        z.set_bit(0x8743, 5, z.r.h);
    }
    output_frequency_byte(z, 0x8745, z.r.h);
    return z.final_return(0x8747);
}

// Pack the instrument nibble and attenuation nibble into one channel register,
// preserving the distinct address and data bus cycles.
template<class Z> k::Exit write_instrument_level(Z& z) {
    z.load_from_indexed(0x8748, ix, z.r.a);
    z.add_immediate(0x874B);
    z.output(0x874D);
    z.load_from_indexed(0x874F, ix, z.r.a);
    z.and_immediate(0x8752);
    z.copy(0x8754, z.r.c, z.r.a);
    z.load_from_indexed(0x8755, ix, z.r.a);
    z.and_immediate(0x8758);
    z.or_register(0x875A, z.r.c);
    z.output(0x875B);
    return z.final_return(0x875D);
}

// Release this channel and remember that it has already been keyed off.
template<class Z> k::Exit release_note(Z& z) {
    z.load_from_indexed(0x875E, ix, z.r.a);
    z.add_immediate(0x8761);
    return z.final_call(0x8763);
}

template<class Z> k::Exit release_note_8766(Z& z) {
    z.store_immediate_indexed(0x8766, ix);
    return z.final_return(0x876A);
}

// Resolve the envelope pointer in the ROM table selected for this note.
template<class Z> k::Exit find_envelope(Z& z) {
    z.copy(0x876B, z.r.c, z.r.a);
    z.load_immediate(0x876C, z.r.b);
    z.add_to_hl(0x876E, z.bc());
    z.add_to_hl(0x876F, z.bc());
    z.load_from_hl(0x8770, z.r.a);
    z.increment_hl(0x8771);
    z.load_from_hl(0x8772, z.r.h);
    z.copy(0x8773, z.r.l, z.r.a);
    return z.final_return(0x8774);
}

// Read the envelope byte through the complete cartridge mapping. Values can
// advance, redirect or end an envelope and discard its caller's pending output.
template<class Z> k::Exit advance_envelope(Z& z) {
    z.store_to_indexed(0x8775, ix, z.r.a);
    return k::fall_through(0x8775, 0x8778);
}

template<class Z> k::Exit advance_envelope_8778(Z& z) {
    z.push(0x8778, z.r.h, z.r.l);
    z.load_from_indexed(0x8779, ix, z.r.c);
    z.load_immediate(0x877C, z.r.b);
    z.add_to_hl(0x877E, z.bc());
    z.copy(0x877F, z.r.c, z.r.l);
    z.copy(0x8780, z.r.b, z.r.h);
    z.pop(0x8781, z.r.h, z.r.l);
    z.load_a_from_pair(0x8782, z.bc());
    z.test_bit(0x8783, 7, z.r.a);
    return z.final_jump_if(0x8785, k::Condition::z);
}

template<class Z> k::Exit advance_envelope_8788(Z& z) {
    z.compare_immediate(0x8788);
    return z.final_relative_if(0x878A, k::Condition::z);
}

template<class Z> k::Exit advance_envelope_878c(Z& z) {
    z.compare_immediate(0x878C);
    return z.final_relative_if(0x878E, k::Condition::z);
}

template<class Z> k::Exit advance_envelope_8790(Z& z) {
    z.load_immediate(0x8790, z.r.a);
    z.store_to_indexed(0x8792, ix, z.r.a);
    z.pop(0x8795, z.r.h, z.r.l);
    return z.final_return(0x8796);
}

template<class Z> k::Exit advance_envelope_8797(Z& z) {
    z.increment_pair(0x8797, z.r.b, z.r.c);
    z.load_a_from_pair(0x8798, z.bc());
    return z.final_relative(0x8799);
}

template<class Z> k::Exit advance_envelope_879b(Z& z) {
    z.xor_a(0x879B);
    return z.final_relative(0x879C);
}

template<class Z> k::Exit advance_envelope_879e(Z& z) {
    z.increment_indexed(0x879E, ix);
    z.copy(0x87A1, z.r.l, z.r.a);
    z.load_immediate(0x87A2, z.r.h);
    z.add_to_hl(0x87A4, z.de());
    z.load_a_direct(0x87A5);
    z.or_a(0x87A8);
    if (!z.relative_if(0x87A9, k::Condition::z)) {
        z.copy(0x87AB, z.r.a, z.r.h);
        z.compare_indexed(0x87AC, ix);
        if (!z.relative_if(0x87AF, k::Condition::z)) {
            z.set_bit_indexed(0x87B1, ix, 1);
        }
    }
    z.store_to_indexed(0x87B5, ix, z.r.a);
    return z.final_return(0x87B8);
}

// Read a score event through its voice pointer. A note may contain a table pitch
// or explicit period, an optional slide and a new or repeated duration.
template<class Z> k::Exit read_melody(Z& z) {
    z.load_from_indexed(0x87B9, ix, z.r.e);
    z.load_from_indexed(0x87BC, ix, z.r.d);
    return k::fall_through(0x87BC, 0x87BF);
}

template<class Z> k::Exit read_melody_87bf(Z& z) {
    z.load_a_from_pair(0x87BF, z.de());
    z.increment_pair(0x87C0, z.r.d, z.r.e);
    z.compare_immediate(0x87C1);
    return z.final_jump_if(0x87C3, k::Condition::nc);
}

template<class Z> k::Exit read_melody_87c6(Z& z) {
    z.test_bit_indexed(0x87C6, ix, 3);
    return z.final_jump_if(0x87CA, k::Condition::nz);
}

template<class Z> k::Exit read_melody_87cd(Z& z) {
    z.compare_immediate(0x87CD);
    return z.final_jump_if(0x87CF, k::Condition::c);
}

template<class Z> k::Exit read_melody_87d2(Z& z) {
    z.relative_if(0x87D2, k::Condition::nz);
    return z.final_call(0x87D4);
}

template<class Z> k::Exit read_melody_87d7(Z& z) {
    z.load_from_hl(0x87D7, z.r.a);
    z.store_to_indexed(0x87D8, ix, z.r.a);
    z.increment_hl(0x87DB);
    z.load_from_hl(0x87DC, z.r.a);
    z.store_to_indexed(0x87DD, ix, z.r.a);
    return k::fall_through(0x87DD, 0x87E0);
}

template<class Z> k::Exit read_melody_87e0(Z& z) {
    z.test_bit_indexed(0x87E0, ix, 5);
    return z.final_jump_if(0x87E4, k::Condition::z);
}

template<class Z> k::Exit read_melody_87e7(Z& z) {
    z.load_a_from_pair(0x87E7, z.de());
    z.increment_pair(0x87E8, z.r.d, z.r.e);
    z.store_to_indexed(0x87E9, ix, z.r.a);
    z.store_to_indexed(0x87EC, ix, z.r.a);
    z.test_bit_indexed(0x87EF, ix, 3);
    z.load_a_from_pair(0x87F3, z.de());
    return z.final_relative_if(0x87F4, k::Condition::nz);
}

template<class Z> k::Exit read_melody_87f6(Z& z) {
    z.store_to_indexed(0x87F6, ix, z.r.a);
    z.increment_pair(0x87F9, z.r.d, z.r.e);
    z.load_a_from_pair(0x87FA, z.de());
    return z.final_relative(0x87FB);
}

template<class Z> k::Exit read_melody_87fd(Z& z) {
    z.load_a_from_pair(0x87FD, z.de());
    z.or_a(0x87FE);
    return z.final_jump_if(0x87FF, k::Condition::p);
}

template<class Z> k::Exit read_melody_8802(Z& z) {
    z.load_from_indexed(0x8802, ix, z.r.a);
    z.store_to_indexed(0x8805, ix, z.r.a);
    return z.final_relative(0x8808);
}

template<class Z> k::Exit read_melody_880a(Z& z) {
    z.increment_pair(0x880A, z.r.d, z.r.e);
    return k::fall_through(0x880A, 0x880B);
}

template<class Z> k::Exit read_melody_880b(Z& z) {
    z.load_from_indexed(0x880B, ix, z.r.b);
    z.decrement(0x880E, z.r.b);
    if (!z.relative_if(0x880F, k::Condition::z)) {
        z.copy(0x8811, z.r.c, z.r.a);
        do {
            z.add_register(0x8812, z.r.c);
        } while (z.decrement_jump(0x8813));
    }
    z.store_to_indexed(0x8815, ix, z.r.a);
    z.store_to_indexed(0x8818, ix, z.r.a);
    return k::fall_through(0x8818, 0x881B);
}

template<class Z> k::Exit read_melody_881b(Z& z) {
    z.xor_a(0x881B);
    z.store_to_indexed(0x881C, ix, z.r.a);
    z.store_to_indexed(0x881F, ix, z.r.a);
    z.store_to_indexed(0x8822, ix, z.r.a);
    z.store_to_indexed(0x8825, ix, z.r.a);
    z.store_to_indexed(0x8828, ix, z.r.a);
    z.store_to_indexed(0x882B, ix, z.r.e);
    z.store_to_indexed(0x882E, ix, z.r.d);
    z.load_immediate(0x8831, z.r.a);
    return z.final_return(0x8833);
}

// Transpose an explicit two-byte period by a signed amount and propagate the carry
// or borrow into its high byte.
template<class Z> k::Exit transpose_period(Z& z) {
    z.copy(0x8834, z.r.h, z.r.a);
    z.load_a_from_pair(0x8835, z.de());
    z.increment_pair(0x8836, z.r.d, z.r.e);
    z.copy(0x8837, z.r.l, z.r.a);
    z.load_from_indexed(0x8838, ix, z.r.a);
    z.or_a(0x883B);
    if (!z.relative_if(0x883C, k::Condition::z)) {
        if (z.jump_if(0x883E, k::Condition::p)) {
            z.add_register(0x8847, z.r.l);
            if (!z.relative_if(0x8848, k::Condition::nc)) z.increment(0x884A, z.r.h);
        } else {
            z.add_register(0x8841, z.r.l);
            if (!z.relative_if(0x8842, k::Condition::c)) {
                z.decrement(0x8844, z.r.h);
                z.relative(0x8845);
            }
        }
        z.copy(0x884B, z.r.l, z.r.a);
    }
    z.store_to_indexed(0x884C, ix, z.r.l);
    z.store_to_indexed(0x884F, ix, z.r.h);
    return z.final_jump(0x8852);
}

// Normalize the period by shifting its two bytes, then combine the selected octave
// and frequency into the OPLL register pair.
template<class Z> k::Exit convert_period(Z& z) {
    z.push(0x8855, z.r.d, z.r.e);
    z.copy(0x8856, z.r.a, z.r.h);
    z.or_a(0x8857);
    if (z.relative_if(0x8858, k::Condition::z)) {
        z.copy(0x8868, z.r.a, z.r.l);
        z.or_a(0x8869);
        if (z.jump_if(0x886A, k::Condition::z)) {
            z.pop(0x8892, z.r.d, z.r.e);
            return z.final_return(0x8893);
        }
        z.load_bc(0x886D);
        do {
            z.rotate_left_circular_a(0x8870);
            z.increment(0x8871, z.r.c);
            if (z.relative_if(0x8872, k::Condition::c)) break;
        } while (z.decrement_jump(0x8874));
        z.copy(0x8876, z.r.b, z.r.c);
        z.load_immediate(0x8877, z.r.a);
        do {
            z.increment(0x8879, z.r.a);
            z.increment(0x887A, z.r.a);
            double_frequency_low(z, 0x887B);
            z.rotate_left_through_carry(0x887D, z.r.h);
        } while (z.decrement_jump(0x887F));
    } else {
        z.compare_immediate(0x885A);
        z.load_immediate(0x885C, z.r.a);
        if (!z.relative_if(0x885E, k::Condition::c)) {
            z.shift_right_logical(0x8860, z.r.h);
            z.rotate_right_through_carry(0x8862, z.r.l);
            z.load_immediate(0x8864, z.r.a);
            z.relative(0x8866);
        }
    }
    z.load_de(0x8881);
    z.exchange_de_hl(0x8884);
    z.or_a(0x8885);
    z.subtract_hl_with_carry(0x8886, z.de());
    z.test_bit(0x8888, 1, z.r.h);
    if (!z.relative_if(0x888A, k::Condition::z)) {
        z.set_bit(0x888C, 0, z.r.h);
    }
    z.copy(0x888E, z.r.d, z.r.a);
    z.load_immediate(0x888F, z.r.e);
    z.add_to_hl(0x8891, z.de());
    z.pop(0x8892, z.r.d, z.r.e);
    return z.final_return(0x8893);
}

// Transpose the note index and look up its two-byte value in the FM frequency
// table.
template<class Z> k::Exit resolve_note(Z& z) {
    z.subtract_immediate(0x8894);
    if (!z.relative_if(0x8896, k::Condition::z)) {
        z.add_from_indexed(0x8898, ix);
    }
    z.load_hl(0x889B);
    z.exchange_af(0x889E);
    if (!z.relative_if(0x889F, k::Condition::z)) {
        z.load_hl(0x88A1);
    }
    z.exchange_af(0x88A4);
    z.copy(0x88A5, z.r.c, z.r.a);
    z.load_immediate(0x88A6, z.r.b);
    z.add_to_hl(0x88A8, z.bc());
    z.add_to_hl(0x88A9, z.bc());
    return z.final_return(0x88AA);
}

// Read a command handler address from the cartridge. The saved continuation
// returns to the melodic or percussion reader after consuming its parameters.
template<class Z> k::Exit dispatch_score_command(Z& z) {
    z.load_hl(0x88AB);
    return k::fall_through(0x88AB, 0x88AE);
}

template<class Z> k::Exit dispatch_score_command_88ae(Z& z) {
    z.push(0x88AE, z.r.h, z.r.l);
    z.subtract_immediate(0x88AF);
    z.load_hl(0x88B1);
    z.add_register(0x88B4, z.r.a);
    z.copy(0x88B5, z.r.c, z.r.a);
    z.load_immediate(0x88B6, z.r.b);
    z.add_to_hl(0x88B8, z.bc());
    z.load_from_hl(0x88B9, z.r.c);
    z.increment_hl(0x88BA);
    z.load_from_hl(0x88BB, z.r.h);
    z.copy(0x88BC, z.r.l, z.r.c);
    return z.final_jump_hl(0x88BD);
}

template<class Z> k::Exit dispatch_score_command_88be(Z& z) {
    z.increment_pair(0x88BE, z.r.d, z.r.e);
    return z.final_jump(0x88BF);
}

// Set the fade divider from the score and begin the fade.
template<class Z> k::Exit set_fade_speed(Z& z) {
    z.load_a_from_pair(0x88E6, z.de());
    z.store_a_direct(0x88E7);
    z.store_a_direct(0x88EA);
    return z.final_jump(0x88ED);
}

// Adjust the duration multiplier for subsequent events.
template<class Z> k::Exit adjust_duration(Z& z) {
    z.load_a_from_pair(0x88F0, z.de());
    z.add_from_indexed(0x88F1, ix);
    z.store_to_indexed(0x88F4, ix, z.r.a);
    return z.final_return(0x88F7);
}

// Key off before transferring the eight user-instrument bytes, interleaving
// address selection and data output with the chip wait.
template<class Z> k::Exit load_custom_instrument(Z& z) {
    z.load_from_indexed(0x88F8, ix, z.r.a);
    z.add_immediate(0x88FB);
    z.output(0x88FD);
    if (!wait_for_chip(z, 0x88FF)) return k::refused();
    z.xor_a(0x8902);
    z.output(0x8903);
    if (!wait_for_chip(z, 0x8905)) return k::refused();
    z.copy(0x8908, z.r.h, z.r.d);
    z.copy(0x8909, z.r.l, z.r.e);
    z.load_immediate(0x890A, z.r.b);
    z.load_immediate(0x890C, z.r.c);
    do {
        z.output(0x890E);
        z.increment(0x8910, z.r.a);
        if (!wait_for_chip(z, 0x8911)) return k::refused();
        z.output_increment(0x8914);
    } while (z.relative_if(0x8916, k::Condition::nz));
    z.copy(0x8918, z.r.d, z.r.h);
    z.copy(0x8919, z.r.e, z.r.l);
    z.decrement_pair(0x891A, z.r.d, z.r.e);
    return z.final_return(0x891B);
}

// During fade restoration an adjustment changes the saved target. Outside a fade
// it changes the live level.
template<class Z> k::Exit adjust_attenuation(Z& z) {
    z.load_a_direct(0x891C);
    z.or_a(0x891F);
    if (auto exit = z.final_return_if(0x8920, k::Condition::z)) return *exit;
    return k::fall_through(0x8920, 0x8921);
}

template<class Z> k::Exit adjust_attenuation_8921(Z& z) {
    z.load_a_direct(0x8921);
    z.or_a(0x8924);
    return z.final_jump_if(0x8925, k::Condition::m);
}

template<class Z> k::Exit adjust_attenuation_8928(Z& z) {
    z.load_a_from_pair(0x8928, z.de());
    z.add_from_indexed(0x8929, ix);
    return z.final_jump(0x892C);
}

template<class Z> k::Exit adjust_attenuation_892f(Z& z) {
    z.load_a_from_pair(0x892F, z.de());
    z.add_from_indexed(0x8930, ix);
    z.and_immediate(0x8933);
    z.store_to_indexed(0x8935, ix, z.r.a);
    return z.final_return(0x8938);
}

// Enable or disable the two articulation flags together.
template<class Z> k::Exit set_articulation(Z& z) {
    z.load_a_from_pair(0x8939, z.de());
    z.compare_immediate(0x893A);
    if (!z.relative_if(0x893C, k::Condition::z)) {
        clear_voice_bit(z, 0x893E, 0);
        clear_voice_bit(z, 0x8942, 1);
        return z.final_return(0x8946);
    }
    z.set_bit_indexed(0x8947, ix, 0);
    z.set_bit_indexed(0x894B, ix, 1);
    return z.final_return(0x894F);
}

// Select the FM-specific transpose parameter and apply it to this voice.
template<class Z> k::Exit choose_transpose(Z& z) {
    z.exchange_af(0x8950);
    return z.final_relative_if(0x8951, k::Condition::nz);
}

// Use the second score byte in FM mode.
template<class Z> k::Exit transpose_voice(Z& z) {
    z.increment_pair(0x8958, z.r.d, z.r.e);
    z.load_a_from_pair(0x8959, z.de());
    z.add_from_indexed(0x895A, ix);
    z.store_to_indexed(0x895D, ix, z.r.a);
    return z.final_return(0x8960);
}

// Set the voice duration multiplier.
template<class Z> k::Exit set_duration(Z& z) {
    z.load_a_from_pair(0x8961, z.de());
    z.store_to_indexed(0x8962, ix, z.r.a);
    return z.final_return(0x8965);
}

// Mask the new attenuation to four bits and select the output routine.
template<class Z> k::Exit set_attenuation(Z& z) {
    z.and_immediate(0x8966);
    z.store_to_indexed(0x8968, ix, z.r.a);
    z.exchange_af(0x896B);
    return z.final_jump_if(0x896C, k::Condition::nz);
}

// Restore the working flags before transferring to FM instrument/level output.
template<class Z> k::Exit publish_attenuation(Z& z) {
    z.exchange_af(0x8973);
    return z.final_jump(0x8974);
}

// Choose the FM instrument field of the score command.
template<class Z> k::Exit choose_instrument(Z& z) {
    z.exchange_af(0x898B);
    return z.final_relative_if(0x898C, k::Condition::nz);
}

// A sentinel keeps the old instrument; while fading the cartridge defers physical
// output.
template<class Z> k::Exit change_instrument(Z& z) {
    z.exchange_af(0x8998);
    z.increment_pair(0x8999, z.r.d, z.r.e);
    z.load_a_from_pair(0x899A, z.de());
    z.compare_immediate(0x899B);
    if (auto exit = z.final_return_if(0x899D, k::Condition::z)) return *exit;
    z.store_to_indexed(0x899E, ix, z.r.a);
    z.load_a_direct(0x89A1);
    z.or_a(0x89A4);
    if (auto exit = z.final_return_if(0x89A5, k::Condition::nz)) return *exit;
    return z.final_jump(0x89A6);
}

// Set the voice envelope selector.
template<class Z> k::Exit set_envelope(Z& z) {
    z.load_a_from_pair(0x89A9, z.de());
    z.store_to_indexed(0x89AA, ix, z.r.a);
    return z.final_return(0x89AD);
}

// Read a little-endian score destination. Subtract one because the common
// continuation increments it.
template<class Z> k::Exit branch_score(Z& z) {
    z.exchange_de_hl(0x89C6);
    z.load_from_hl(0x89C7, z.r.e);
    z.increment_hl(0x89C8);
    z.load_from_hl(0x89C9, z.r.d);
    z.decrement_pair(0x89CA, z.r.d, z.r.e);
    return z.final_return(0x89CB);
}

// Set the slide flag and prepare to switch instrument on eligible FM voices.
template<class Z> k::Exit enable_slide(Z& z) {
    z.load_a_from_pair(0x89CC, z.de());
    z.compare_immediate(0x89CD);
    return z.final_relative_if(0x89CF, k::Condition::nz);
}

template<class Z> k::Exit enable_slide_89d1(Z& z) {
    z.set_bit_indexed(0x89D1, ix, 5);
    z.load_from_indexed(0x89D5, ix, z.r.a);
    z.exchange_af(0x89D8);
    return z.final_relative_if(0x89D9, k::Condition::nz);
}

// Make the slide audible on eligible channels by saving the previous instrument
// and adjusting attenuation.
template<class Z> k::Exit install_slide_instrument(Z& z) {
    z.exchange_af(0x89E8);
    z.compare_immediate(0x89E9);
    if (auto exit = z.final_return_if(0x89EB, k::Condition::nc)) return *exit;
    z.decrement_indexed(0x89EC, ix);
    z.decrement_indexed(0x89EF, ix);
    z.load_from_indexed(0x89F2, ix, z.r.a);
    z.store_to_indexed(0x89F5, ix, z.r.a);
    z.store_immediate_indexed(0x89F8, ix);
    return z.final_jump(0x89FC);
}

// Clear the slide flag and select the FM restore path.
template<class Z> k::Exit disable_slide(Z& z) {
    clear_voice_bit(z, 0x89FF, 5);
    z.load_from_indexed(0x8A03, ix, z.r.a);
    z.exchange_af(0x8A06);
    return z.final_relative_if(0x8A07, k::Condition::nz);
}

// Restore the original instrument and its two steps of attenuation.
template<class Z> k::Exit restore_slide_instrument(Z& z) {
    z.exchange_af(0x8A16);
    z.compare_immediate(0x8A17);
    if (auto exit = z.final_return_if(0x8A19, k::Condition::nc)) return *exit;
    z.increment_indexed(0x8A1A, ix);
    z.increment_indexed(0x8A1D, ix);
    z.load_from_indexed(0x8A20, ix, z.r.a);
    z.store_to_indexed(0x8A23, ix, z.r.a);
    return z.final_jump(0x8A26);
}

// Select explicit or table-based periods for subsequent notes.
template<class Z> k::Exit select_period_format(Z& z) {
    z.load_a_from_pair(0x8A29, z.de());
    z.compare_immediate(0x8A2A);
    if (!z.relative_if(0x8A2C, k::Condition::nz)) {
        z.set_bit_indexed(0x8A2E, ix, 3);
        return z.final_return(0x8A32);
    }
    clear_voice_bit(z, 0x8A33, 3);
    return z.final_return(0x8A37);
}

// Deactivate the voice and release its reservation; FM restores the displaced
// channel before unwinding the score call.
template<class Z> k::Exit end_sequence(Z& z) {
    z.load_hl(0x8A38);
    clear_pointed_bit(z, 0x8A3B, 2);
    z.xor_a(0x8A3D);
    z.store_a_direct(0x8A3E);
    z.store_to_indexed(0x8A41, ix, z.r.a);
    z.exchange_af(0x8A44);
    return z.final_jump_if(0x8A45, k::Condition::nz);
}

// Discard the saved score-reader continuations and return to the voice traversal.
template<class Z> k::Exit leave_sequence(Z& z) {
    z.pop(0x8A9A, z.r.h, z.r.l);
    z.pop(0x8A9B, z.r.h, z.r.l);
    return z.final_return(0x8A9C);
}

// After releasing the effect key, restore the music instrument and attenuation on
// its hardware channel.
template<class Z> k::Exit restore_displaced_voice(Z& z) {
    z.exchange_af(0x8A9D);
    z.load_from_indexed(0x8A9E, ix, z.r.a);
    z.push(0x8AA1, z.r.a, z.r.f);
    z.add_immediate(0x8AA2);
    z.output(0x8AA4);
    if (!wait_for_chip(z, 0x8AA6)) return k::refused();
    z.xor_a(0x8AA9);
    z.output(0x8AAA);
    z.pop(0x8AAC, z.r.a, z.r.f);
    z.compare_immediate(0x8AAD);
    return z.final_relative_if(0x8AAF, k::Condition::z);
}

template<class Z> k::Exit restore_displaced_voice_8ab1(Z& z) {
    if (!wait_for_chip(z, 0x8AB1)) return k::refused();
    z.load_hl(0x8AB4);
    clear_pointed_bit(z, 0x8AB7, 2);
    z.load_immediate(0x8AB9, z.r.a);
    z.output(0x8ABB);
    z.load_hl(0x8ABD);
    return z.final_call(0x8AC0);
}

template<class Z> k::Exit restore_displaced_voice_8ac3(Z& z) {
    return z.final_jump(0x8AC3);
}

template<class Z> k::Exit restore_displaced_voice_8ac6(Z& z) {
    z.load_hl(0x8AC6);
    clear_pointed_bit(z, 0x8AC9, 2);
    z.load_a_direct(0x8ACB);
    z.or_a(0x8ACE);
    z.load_hl(0x8ACF);
    if (!z.jump_if(0x8AD2, k::Condition::p)) {
        z.load_hl(0x8AD5);
    }
    z.load_immediate(0x8AD8, z.r.a);
    z.output(0x8ADA);
    return z.final_call(0x8ADC);
}

template<class Z> k::Exit restore_displaced_voice_8adf(Z& z) {
    return z.final_jump(0x8ADF);
}

// Pack the original music instrument and volume bytes and write them to the
// selected register.
template<class Z> k::Exit restore_level(Z& z) {
    z.load_from_hl(0x8AE2, z.r.a);
    z.and_immediate(0x8AE3);
    z.copy(0x8AE5, z.r.c, z.r.a);
    z.increment_hl(0x8AE6);
    z.load_from_hl(0x8AE7, z.r.a);
    z.and_immediate(0x8AE8);
    z.or_register(0x8AEA, z.r.c);
    z.output(0x8AEB);
    return z.final_return(0x8AED);
}

// Save the score return pointer on the small stack inside the voice record, then
// enter the nested sequence.
template<class Z> k::Exit call_score(Z& z) {
    z.load_a_from_pair(0x8AEE, z.de());
    z.copy(0x8AEF, z.r.c, z.r.a);
    z.increment_pair(0x8AF0, z.r.d, z.r.e);
    z.load_a_from_pair(0x8AF1, z.de());
    z.copy(0x8AF2, z.r.b, z.r.a);
    z.push(0x8AF3, z.r.b, z.r.c);
    z.push_index(0x8AF4, ix);
    z.pop(0x8AF6, z.r.h, z.r.l);
    z.decrement_indexed(0x8AF7, ix);
    z.load_from_indexed(0x8AFA, ix, z.r.c);
    z.decrement_indexed(0x8AFD, ix);
    z.load_immediate(0x8B00, z.r.b);
    z.add_to_hl(0x8B02, z.bc());
    z.store_register_to_hl(0x8B03, z.r.d);
    z.decrement_hl(0x8B04);
    z.store_register_to_hl(0x8B05, z.r.e);
    z.pop(0x8B06, z.r.d, z.r.e);
    z.decrement_pair(0x8B07, z.r.d, z.r.e);
    return z.final_return(0x8B08);
}

// Pop the score position from the voice-local stack.
template<class Z> k::Exit return_score(Z& z) {
    z.push_index(0x8B09, ix);
    z.pop(0x8B0B, z.r.h, z.r.l);
    z.load_from_indexed(0x8B0C, ix, z.r.c);
    z.load_immediate(0x8B0F, z.r.b);
    z.add_to_hl(0x8B11, z.bc());
    z.load_from_hl(0x8B12, z.r.e);
    z.increment_hl(0x8B13);
    z.load_from_hl(0x8B14, z.r.d);
    z.increment_indexed(0x8B15, ix);
    z.increment_indexed(0x8B18, ix);
    return z.final_return(0x8B1B);
}

// The chosen counter is initialized once from the score. Repeat through the
// encoded target until decrement reaches zero.
template<class Z> k::Exit repeat_score(Z& z) {
    z.load_a_from_pair(0x8B1C, z.de());
    z.increment_pair(0x8B1D, z.r.d, z.r.e);
    z.add_immediate(0x8B1E);
    z.copy(0x8B20, z.r.c, z.r.a);
    z.load_immediate(0x8B21, z.r.b);
    z.push_index(0x8B23, ix);
    z.pop(0x8B25, z.r.h, z.r.l);
    z.add_to_hl(0x8B26, z.bc());
    z.load_from_hl(0x8B27, z.r.a);
    z.or_a(0x8B28);
    if (!z.relative_if(0x8B29, k::Condition::nz)) {
        z.load_a_from_pair(0x8B2B, z.de());
        z.store_register_to_hl(0x8B2C, z.r.a);
    }
    z.increment_pair(0x8B2D, z.r.d, z.r.e);
    z.decrement_at_hl(0x8B2E);
    return z.final_jump_if(0x8B2F, k::Condition::nz);
}

template<class Z> k::Exit repeat_score_8b32(Z& z) {
    z.increment_pair(0x8B32, z.r.d, z.r.e);
    return z.final_return(0x8B33);
}

// The cartridge spaces chip accesses with a stack round trip. Keep both scratch
// writes and the original delay.
template<class Z> k::Exit chip_wait(Z& z) {
    z.push(0x8B34, z.r.h, z.r.l);
    z.pop(0x8B35, z.r.h, z.r.l);
    return z.final_return(0x8B36);
}

struct Body {
    using Live = k::Instructions<k::Live>;
    using Estimate = k::Instructions<k::Estimate>;
    k::Exit (*play)(Live&);
    k::Exit (*measure)(Estimate&);
    k::Exit operator()(Live& z) const { return play(z); }
    k::Exit operator()(Estimate& z) const { return measure(z); }
};

void add(RoutineRegistry& registry, unsigned address, unsigned unit,
         std::vector<SegmentExit> exits, unsigned scratch, unsigned inherited, Body logic) {
    const RoutineEntry entry{12, static_cast<std::uint16_t>(address)};
    auto spec = k::segment({12, static_cast<std::uint16_t>(unit)}, std::move(exits),
                           {.scratch_stack = scratch, .inherited_stack = inherited});
    spec.single_instruction = spec.exits.size() == 1 && spec.exits.front().address == address;
    if (address == 0x8000 || address == 0x801F || address == 0x8052 || address == 0x8466) {
        // Direct dispatch between divided ticks must enter with the FM flag
        // that a full update normally moves into the alternate register bank.
        k::Guard fm_mode{"Japanese FM driver requires FM mode", [address](const Machine& machine) {
            return machine.peek(0xC000) == 1 &&
                   (address != 0x8466 || !(machine.cpu().af_.uint8_values.at_0 & kit::zero_flag));
        }};
        k::add_segment(registry, entry, std::move(spec), std::move(fm_mode), logic);
    } else {
        k::add_segment(registry, entry, std::move(spec), logic);
    }
}
} // namespace

void register_japanese_sound(RoutineRegistry& registry) {
    add(registry, 0x8000, 0x8000, {k::core_final_at(0x8003, -6)}, 8, 0,
        {initialize, initialize});
    add(registry, 0x8006, 0x8000, {k::core_final_at(0x801E, 6)}, 0, 6,
        {initialize_8006, initialize_8006});
    add(registry, 0x801F, 0x801F, {k::core_final_at(0x802B)}, 4, 0,
        {silence, silence});
    add(registry, 0x802C, 0x802C, {k::core_final_at(0x8030, -4)}, 4, 0,
        {release_keys, release_keys});
    add(registry, 0x8032, 0x802C, {k::core_final_at(0x8034)}, 2, 0,
        {release_keys_8032, release_keys_8032});
    add(registry, 0x8037, 0x802C, {k::conditional_at(0x8037)}, 0, 0,
        {release_keys_8037, release_keys_8037});
    add(registry, 0x8039, 0x802C, {k::returned_at(0x803B, 4)}, 0, 4,
        {release_keys_8039, release_keys_8039});
    add(registry, 0x803C, 0x803C, {k::returned_at(0x8049)}, 0, 0,
        {clear_controls, clear_controls});
    add(registry, 0x8052, 0x8052, {k::core_final_at(0x805B)}, 2, 0,
        {update, update});
    add(registry, 0x805E, 0x8052, {k::core_final_at(0x805E)}, 2, 0,
        {update_805e, update_805e});
    add(registry, 0x8061, 0x8052, {k::core_final_at(0x8061)}, 2, 0,
        {update_8061, update_8061});
    add(registry, 0x8064, 0x8052, {k::core_final_at(0x8064)}, 2, 0,
        {update_8064, update_8064});
    add(registry, 0x8067, 0x8052, {k::conditional_at(0x806B)}, 0, 0,
        {update_8067, update_8067});
    add(registry, 0x806E, 0x806E, {k::conditional_at(0x8072)}, 0, 0,
        {visit_voices, visit_voices});
    add(registry, 0x8075, 0x806E, {k::core_final_at(0x8079)}, 0, 0,
        {visit_voices_8075, visit_voices_8075});
    add(registry, 0x807B, 0x806E, {k::conditional_at(0x807F, -2)}, 2, 0,
        {visit_voices_807b, visit_voices_807b});
    add(registry, 0x8081, 0x806E, {k::core_final_at(0x8085)}, 2, 0,
        {visit_voices_8081, visit_voices_8081});
    add(registry, 0x8088, 0x806E, {k::conditional_at(0x808E, 2)}, 0, 2,
        {visit_voices_8088, visit_voices_8088});
    add(registry, 0x8090, 0x806E, {k::returned_at(0x8090)}, 0, 0,
        {visit_voices_8090, visit_voices_8090});
    add(registry, 0x8091, 0x806E, {k::core_final_at(0x8095)}, 2, 0,
        {visit_voices_8091, visit_voices_8091});
    add(registry, 0x8098, 0x806E, {k::core_final_at(0x8098)}, 0, 0,
        {visit_voices_8098, visit_voices_8098});
    add(registry, 0x809A, 0x806E, {k::core_final_at(0x809E)}, 0, 0,
        {visit_voices_809a, visit_voices_809a});
    add(registry, 0x80A0, 0x806E, {k::conditional_at(0x80A4, -2)}, 2, 0,
        {visit_voices_80a0, visit_voices_80a0});
    add(registry, 0x80A6, 0x806E, {k::core_final_at(0x80AA)}, 2, 0,
        {visit_voices_80a6, visit_voices_80a6});
    add(registry, 0x80AD, 0x806E, {k::conditional_at(0x80B3, 2)}, 0, 2,
        {visit_voices_80ad, visit_voices_80ad});
    add(registry, 0x80B5, 0x806E, {k::returned_at(0x80B5)}, 0, 0,
        {visit_voices_80b5, visit_voices_80b5});
    add(registry, 0x80B6, 0x806E, {k::core_final_at(0x80BA)}, 2, 0,
        {visit_voices_80b6, visit_voices_80b6});
    add(registry, 0x80BD, 0x806E, {k::core_final_at(0x80BD)}, 0, 0,
        {visit_voices_80bd, visit_voices_80bd});
    add(registry, 0x8110, 0x8110, {k::returned_at(0x8115), k::returned_at(0x8131), k::returned_at(0x8134)}, 0, 0,
        {reserve_voices, reserve_voices});
    add(registry, 0x8135, 0x8135, {k::returned_at(0x813A), k::returned_at(0x813C), k::returned_at(0x815B)}, 0, 0,
        {tempo_countdown, tempo_countdown});
    add(registry, 0x815C, 0x815C, {k::returned_at(0x8160), k::conditional_at(0x8161)}, 0, 0,
        {fade_out, fade_out});
    add(registry, 0x8164, 0x815C, {k::returned_at(0x816D), k::conditional_at(0x817D)}, 0, 0,
        {fade_out_8164, fade_out_8164});
    add(registry, 0x817F, 0x815C, {k::conditional_at(0x8188)}, 0, 0,
        {fade_out_817f, fade_out_817f});
    add(registry, 0x818A, 0x815C, {k::core_final_at(0x818C)}, 0, 0,
        {fade_out_818a, fade_out_818a});
    add(registry, 0x818F, 0x815C, {k::conditional_at(0x8190)}, 0, 0,
        {fade_out_818f, fade_out_818f});
    add(registry, 0x8193, 0x815C, {k::returned_at(0x8197), k::core_final_at(0x8198)}, 0, 0,
        {fade_out_8193, fade_out_8193});
    add(registry, 0x819B, 0x815C, {k::conditional_at(0x81A8)}, 0, 0,
        {fade_out_819b, fade_out_819b});
    add(registry, 0x81AB, 0x815C, {k::core_final_at(0x81AF)}, 0, 0,
        {fade_out_81ab, fade_out_81ab});
    add(registry, 0x81B1, 0x815C, {k::conditional_at(0x81B7)}, 0, 0,
        {fade_out_81b1, fade_out_81b1});
    add(registry, 0x81B9, 0x815C, {k::core_final_at(0x81C0)}, 2, 0,
        {fade_out_81b9, fade_out_81b9});
    add(registry, 0x81C3, 0x815C, {k::conditional_at(0x81C5)}, 0, 0,
        {fade_out_81c3, fade_out_81c3});
    add(registry, 0x81C7, 0x815C, {k::returned_at(0x81C7)}, 0, 0,
        {fade_out_81c7, fade_out_81c7});
    add(registry, 0x81C8, 0x815C, {k::returned_at(0x81CC), k::core_final_at(0x81CD)}, 0, 0,
        {fade_out_81c8, fade_out_81c8});
    add(registry, 0x81D0, 0x81D0, {k::returned_at(0x81D9), k::returned_at(0x81F3), k::core_final_at(0x81FB)}, 0, 0,
        {fade_in, fade_in});
    add(registry, 0x81FD, 0x81D0, {k::conditional_at(0x8201)}, 0, 0,
        {fade_in_81fd, fade_in_81fd});
    add(registry, 0x8204, 0x81D0, {k::conditional_at(0x8207)}, 0, 0,
        {fade_in_8204, fade_in_8204});
    add(registry, 0x8209, 0x81D0, {k::core_final_at(0x8210)}, 2, 0,
        {fade_in_8209, fade_in_8209});
    add(registry, 0x8213, 0x81D0, {k::conditional_at(0x8215)}, 0, 0,
        {fade_in_8213, fade_in_8213});
    add(registry, 0x8217, 0x81D0, {k::returned_at(0x8217)}, 0, 0,
        {fade_in_8217, fade_in_8217});
    add(registry, 0x8218, 0x8218, {k::core_final_at(0x8218)}, 2, 0,
        {restore_group, restore_group});
    add(registry, 0x821B, 0x8218, {k::core_final_at(0x824C)}, 2, 0,
        {restore_group_821b, restore_group_821b});
    add(registry, 0x824F, 0x824F, {k::core_final_at(0x8254)}, 0, 0,
        {begin_fade, begin_fade});
    add(registry, 0x8257, 0x824F, {k::core_final_at(0x826F)}, 0, 0,
        {begin_fade_8257, begin_fade_8257});
    add(registry, 0x8272, 0x8272, {k::returned_at(0x827A)}, 4, 0,
        {write_key_off, write_key_off});
    add(registry, 0x827B, 0x827B, {k::returned_at(0x8291)}, 4, 0,
        {silence_effect, silence_effect});
    add(registry, 0x8292, 0x8292, {k::returned_at(0x82A9)}, 6, 0,
        {program_instruments, program_instruments});
    add(registry, 0x82AA, 0x82AA, {k::conditional_at(0x82AF)}, 0, 0,
        {suspend_group, suspend_group});
    add(registry, 0x82B2, 0x82AA, {k::core_final_at(0x82B7)}, 0, 0,
        {suspend_group_82b2, suspend_group_82b2});
    add(registry, 0x82BA, 0x82BA, {k::core_final_at(0x82C4)}, 2, 0,
        {release_effects, release_effects});
    add(registry, 0x82C7, 0x82BA, {k::core_final_at(0x82CA)}, 2, 0,
        {release_effects_82c7, release_effects_82c7});
    add(registry, 0x82CD, 0x82BA, {k::conditional_at(0x82CE)}, 0, 0,
        {release_effects_82cd, release_effects_82cd});
    add(registry, 0x82DD, 0x82DD, {k::core_final_at(0x82E0)}, 2, 0,
        {restore_effect_keys, restore_effect_keys});
    add(registry, 0x82E3, 0x82DD, {k::core_final_at(0x82E8)}, 4, 0,
        {restore_effect_keys_82e3, restore_effect_keys_82e3});
    add(registry, 0x82EB, 0x82DD, {k::conditional_at(0x82EF)}, 0, 0,
        {restore_effect_keys_82eb, restore_effect_keys_82eb});
    add(registry, 0x82F2, 0x82DD, {k::core_final_at(0x82F8, -2)}, 4, 0,
        {restore_effect_keys_82f2, restore_effect_keys_82f2});
    add(registry, 0x82FB, 0x82DD, {k::core_final_at(0x82FF)}, 2, 0,
        {restore_effect_keys_82fb, restore_effect_keys_82fb});
    add(registry, 0x8302, 0x82DD, {k::core_final_at(0x8304, 2)}, 0, 2,
        {restore_effect_keys_8302, restore_effect_keys_8302});
    add(registry, 0x8307, 0x8307, {k::returned_at(0x8311)}, 0, 0,
        {clear_reservations, clear_reservations});
    add(registry, 0x8466, 0x8466, {k::conditional_at(0x846B)}, 0, 0,
        {dispatch_request, dispatch_request});
    add(registry, 0x846E, 0x8466, {k::conditional_at(0x8470)}, 0, 0,
        {dispatch_request_846e, dispatch_request_846e});
    add(registry, 0x8472, 0x8466, {k::conditional_at(0x8474)}, 0, 0,
        {dispatch_request_8472, dispatch_request_8472});
    add(registry, 0x8477, 0x8466, {k::conditional_at(0x8479)}, 0, 0,
        {dispatch_request_8477, dispatch_request_8477});
    add(registry, 0x847C, 0x8466, {k::conditional_at(0x847E)}, 0, 0,
        {dispatch_request_847c, dispatch_request_847c});
    add(registry, 0x8481, 0x8466, {k::core_final_at(0x848F)}, 0, 0,
        {dispatch_request_8481, dispatch_request_8481});
    add(registry, 0x8490, 0x8490, {k::core_final_at(0x849C)}, 2, 0,
        {start_shared_effect, start_shared_effect});
    add(registry, 0x849F, 0x8490, {k::core_final_at(0x84AA)}, 0, 0,
        {start_shared_effect_849f, start_shared_effect_849f});
    add(registry, 0x84AD, 0x84AD, {k::conditional_at(0x84AF)}, 0, 0,
        {start_song, start_song});
    add(registry, 0x84B2, 0x84AD, {k::returned_at(0x84B4), k::core_final_at(0x84E7, -4)}, 6, 0,
        {start_song_84b2, start_song_84b2});
    add(registry, 0x84EA, 0x84AD, {k::core_final_at(0x84EA)}, 2, 0,
        {start_song_84ea, start_song_84ea});
    add(registry, 0x84ED, 0x84AD, {k::conditional_at(0x84EF, 2)}, 0, 2,
        {start_song_84ed, start_song_84ed});
    add(registry, 0x8504, 0x8504, {k::core_final_at(0x8505)}, 2, 0,
        {select_song, select_song});
    add(registry, 0x8508, 0x8504, {k::conditional_at(0x850C)}, 0, 0,
        {select_song_8508, select_song_8508});
    add(registry, 0x850F, 0x8504, {k::core_final_at(0x8512)}, 2, 0,
        {select_song_850f, select_song_850f});
    add(registry, 0x8515, 0x8504, {k::core_final_at(0x8515)}, 0, 0,
        {select_song_8515, select_song_8515});
    add(registry, 0x8517, 0x8504, {k::core_final_at(0x8517)}, 2, 0,
        {select_song_8517, select_song_8517});
    add(registry, 0x851A, 0x8504, {k::core_final_at(0x851E, 2)}, 0, 2,
        {select_song_851a, select_song_851a});
    add(registry, 0x8521, 0x8504, {k::core_final_at(0x8521)}, 0, 0,
        {select_song_8521, select_song_8521});
    add(registry, 0x8524, 0x8524, {k::core_final_at(0x8530)}, 2, 0,
        {start_effect, start_effect});
    add(registry, 0x8533, 0x8524, {k::conditional_at(0x8539)}, 0, 0,
        {start_effect_8533, start_effect_8533});
    add(registry, 0x853B, 0x8524, {k::conditional_at(0x853E)}, 0, 0,
        {start_effect_853b, start_effect_853b});
    add(registry, 0x8540, 0x8524, {k::conditional_at(0x8542)}, 0, 0,
        {start_effect_8540, start_effect_8540});
    add(registry, 0x8544, 0x8524, {k::core_final_at(0x8549)}, 0, 0,
        {start_effect_8544, start_effect_8544});
    add(registry, 0x854B, 0x8524, {k::core_final_at(0x854B)}, 2, 0,
        {start_effect_854b, start_effect_854b});
    add(registry, 0x854E, 0x8524, {k::core_final_at(0x8551)}, 0, 0,
        {start_effect_854e, start_effect_854e});
    add(registry, 0x8553, 0x8524, {k::core_final_at(0x855B)}, 0, 0,
        {start_effect_8553, start_effect_8553});
    add(registry, 0x855D, 0x8524, {k::core_final_at(0x8562)}, 2, 0,
        {start_effect_855d, start_effect_855d});
    add(registry, 0x8565, 0x8524, {k::core_final_at(0x8565)}, 0, 0,
        {start_effect_8565, start_effect_8565});
    add(registry, 0x85C6, 0x85C6, {k::core_final_at(0x85C9)}, 0, 0,
        {install_voices, install_voices});
    add(registry, 0x85CA, 0x85C6, {k::conditional_at(0x85F0, -4)}, 4, 0,
        {install_voices_85ca, install_voices_85ca});
    add(registry, 0x85F6, 0x85F6, {k::conditional_at(0x85F7, 2)}, 0, 2,
        {finish_voice_setup, finish_voice_setup});
    add(registry, 0x85F9, 0x85F6, {k::returned_at(0x85FE)}, 0, 0,
        {finish_voice_setup_85f9, finish_voice_setup_85f9});
    add(registry, 0x85FF, 0x85FF, {k::returned_at(0x8607)}, 0, 0,
        {read_pointer, read_pointer});
    add(registry, 0x8608, 0x8608, {k::conditional_at(0x8611)}, 0, 0,
        {advance_rhythm, advance_rhythm});
    add(registry, 0x8613, 0x8608, {k::core_final_at(0x8613)}, 2, 0,
        {advance_rhythm_8613, advance_rhythm_8613});
    add(registry, 0x8616, 0x8608, {k::returned_at(0x861A), k::returned_at(0x8626)}, 0, 0,
        {advance_rhythm_8616, advance_rhythm_8616});
    add(registry, 0x8627, 0x8608, {k::returned_at(0x8629), k::returned_at(0x8635)}, 4, 0,
        {advance_rhythm_8627, advance_rhythm_8627});
    add(registry, 0x8636, 0x8636, {k::core_final_at(0x8639)}, 0, 0,
        {read_rhythm, read_rhythm});
    add(registry, 0x863C, 0x8636, {k::conditional_at(0x8640)}, 0, 0,
        {read_rhythm_863c, read_rhythm_863c});
    add(registry, 0x8643, 0x8636, {k::conditional_at(0x8645)}, 0, 0,
        {read_rhythm_8643, read_rhythm_8643});
    add(registry, 0x8648, 0x8636, {k::core_final_at(0x8657)}, 0, 0,
        {read_rhythm_8648, read_rhythm_8648});
    add(registry, 0x865A, 0x8636, {k::core_final_at(0x865D)}, 0, 0,
        {read_rhythm_865a, read_rhythm_865a});
    add(registry, 0x8660, 0x8636, {k::core_final_at(0x8661)}, 0, 0,
        {read_rhythm_8660, read_rhythm_8660});
    add(registry, 0x8664, 0x8664, {k::core_final_at(0x866D)}, 2, 0,
        {advance_melody, advance_melody});
    add(registry, 0x8670, 0x8664, {k::conditional_at(0x8675)}, 0, 0,
        {advance_melody_8670, advance_melody_8670});
    add(registry, 0x8678, 0x8664, {k::conditional_at(0x867C)}, 0, 0,
        {advance_melody_8678, advance_melody_8678});
    add(registry, 0x867F, 0x8664, {k::core_final_at(0x86CA)}, 0, 0,
        {advance_melody_867f, advance_melody_867f});
    add(registry, 0x86CD, 0x86CD, {k::returned_at(0x86D1), k::returned_at(0x86D7), k::conditional_at(0x86DF)}, 0, 0,
        {play_melody, play_melody});
    add(registry, 0x86E1, 0x86CD, {k::conditional_at(0x86E3)}, 0, 0,
        {play_melody_86e1, play_melody_86e1});
    add(registry, 0x86E6, 0x86CD, {k::returned_at(0x86EE), k::returned_at(0x86F6), k::conditional_at(0x8701)}, 0, 0,
        {play_melody_86e6, play_melody_86e6});
    add(registry, 0x8704, 0x86CD, {k::core_final_at(0x8708)}, 2, 0,
        {play_melody_8704, play_melody_8704});
    add(registry, 0x870B, 0x86CD, {k::core_final_at(0x870B)}, 2, 0,
        {play_melody_870b, play_melody_870b});
    add(registry, 0x870E, 0x86CD, {k::core_final_at(0x8712)}, 2, 0,
        {play_melody_870e, play_melody_870e});
    add(registry, 0x8715, 0x86CD, {k::returned_at(0x8738), k::returned_at(0x8747)}, 4, 0,
        {play_melody_8715, play_melody_8715});
    add(registry, 0x8748, 0x8748, {k::returned_at(0x875D)}, 0, 0,
        {write_instrument_level, write_instrument_level});
    add(registry, 0x875E, 0x875E, {k::core_final_at(0x8763)}, 2, 0,
        {release_note, release_note});
    add(registry, 0x8766, 0x875E, {k::returned_at(0x876A)}, 0, 0,
        {release_note_8766, release_note_8766});
    add(registry, 0x876B, 0x876B, {k::returned_at(0x8774)}, 0, 0,
        {find_envelope, find_envelope});
    add(registry, 0x8775, 0x8775, {k::core_final_at(0x8775)}, 0, 0,
        {advance_envelope, advance_envelope});
    add(registry, 0x8778, 0x8775, {k::conditional_at(0x8785)}, 2, 0,
        {advance_envelope_8778, advance_envelope_8778});
    add(registry, 0x8788, 0x8775, {k::conditional_at(0x878A)}, 0, 0,
        {advance_envelope_8788, advance_envelope_8788});
    add(registry, 0x878C, 0x8775, {k::conditional_at(0x878E)}, 0, 0,
        {advance_envelope_878c, advance_envelope_878c});
    add(registry, 0x8790, 0x8775, {k::returned_at(0x8796, 2)}, 0, 2,
        {advance_envelope_8790, advance_envelope_8790});
    add(registry, 0x8797, 0x8775, {k::core_final_at(0x8799)}, 0, 0,
        {advance_envelope_8797, advance_envelope_8797});
    add(registry, 0x879B, 0x8775, {k::core_final_at(0x879C)}, 0, 0,
        {advance_envelope_879b, advance_envelope_879b});
    add(registry, 0x879E, 0x8775, {k::returned_at(0x87B8)}, 0, 0,
        {advance_envelope_879e, advance_envelope_879e});
    add(registry, 0x87B9, 0x87B9, {k::core_final_at(0x87BC)}, 0, 0,
        {read_melody, read_melody});
    add(registry, 0x87BF, 0x87B9, {k::conditional_at(0x87C3)}, 0, 0,
        {read_melody_87bf, read_melody_87bf});
    add(registry, 0x87C6, 0x87B9, {k::conditional_at(0x87CA)}, 0, 0,
        {read_melody_87c6, read_melody_87c6});
    add(registry, 0x87CD, 0x87B9, {k::conditional_at(0x87CF)}, 0, 0,
        {read_melody_87cd, read_melody_87cd});
    add(registry, 0x87D2, 0x87B9, {k::core_final_at(0x87D4)}, 2, 0,
        {read_melody_87d2, read_melody_87d2});
    add(registry, 0x87D7, 0x87B9, {k::core_final_at(0x87DD)}, 0, 0,
        {read_melody_87d7, read_melody_87d7});
    add(registry, 0x87E0, 0x87B9, {k::conditional_at(0x87E4)}, 0, 0,
        {read_melody_87e0, read_melody_87e0});
    add(registry, 0x87E7, 0x87B9, {k::conditional_at(0x87F4)}, 0, 0,
        {read_melody_87e7, read_melody_87e7});
    add(registry, 0x87F6, 0x87B9, {k::core_final_at(0x87FB)}, 0, 0,
        {read_melody_87f6, read_melody_87f6});
    add(registry, 0x87FD, 0x87B9, {k::conditional_at(0x87FF)}, 0, 0,
        {read_melody_87fd, read_melody_87fd});
    add(registry, 0x8802, 0x87B9, {k::core_final_at(0x8808)}, 0, 0,
        {read_melody_8802, read_melody_8802});
    add(registry, 0x880A, 0x87B9, {k::core_final_at(0x880A)}, 0, 0,
        {read_melody_880a, read_melody_880a});
    add(registry, 0x880B, 0x87B9, {k::core_final_at(0x8818)}, 0, 0,
        {read_melody_880b, read_melody_880b});
    add(registry, 0x881B, 0x87B9, {k::returned_at(0x8833)}, 0, 0,
        {read_melody_881b, read_melody_881b});
    add(registry, 0x8834, 0x8834, {k::core_final_at(0x8852)}, 0, 0,
        {transpose_period, transpose_period});
    add(registry, 0x8855, 0x8855, {k::returned_at(0x8893)}, 2, 0,
        {convert_period, convert_period});
    add(registry, 0x8894, 0x8894, {k::returned_at(0x88AA)}, 0, 0,
        {resolve_note, resolve_note});
    add(registry, 0x88AB, 0x88AB, {k::core_final_at(0x88AB)}, 0, 0,
        {dispatch_score_command, dispatch_score_command});
    add(registry, 0x88AE, 0x88AB, {k::core_final_at(0x88BD, -2)}, 2, 0,
        {dispatch_score_command_88ae, dispatch_score_command_88ae});
    add(registry, 0x88BE, 0x88AB, {k::core_final_at(0x88BF)}, 0, 0,
        {dispatch_score_command_88be, dispatch_score_command_88be});
    add(registry, 0x88E6, 0x88E6, {k::core_final_at(0x88ED)}, 0, 0,
        {set_fade_speed, set_fade_speed});
    add(registry, 0x88F0, 0x88F0, {k::returned_at(0x88F7)}, 0, 0,
        {adjust_duration, adjust_duration});
    add(registry, 0x88F8, 0x88F8, {k::returned_at(0x891B)}, 4, 0,
        {load_custom_instrument, load_custom_instrument});
    add(registry, 0x891C, 0x891C, {k::returned_at(0x8920), k::core_final_at(0x8920)}, 0, 0,
        {adjust_attenuation, adjust_attenuation});
    add(registry, 0x8921, 0x891C, {k::conditional_at(0x8925)}, 0, 0,
        {adjust_attenuation_8921, adjust_attenuation_8921});
    add(registry, 0x8928, 0x891C, {k::core_final_at(0x892C)}, 0, 0,
        {adjust_attenuation_8928, adjust_attenuation_8928});
    add(registry, 0x892F, 0x891C, {k::returned_at(0x8938)}, 0, 0,
        {adjust_attenuation_892f, adjust_attenuation_892f});
    add(registry, 0x8939, 0x8939, {k::returned_at(0x8946), k::returned_at(0x894F)}, 0, 0,
        {set_articulation, set_articulation});
    add(registry, 0x8950, 0x8950, {k::conditional_at(0x8951)}, 0, 0,
        {choose_transpose, choose_transpose});
    add(registry, 0x8958, 0x8958, {k::returned_at(0x8960)}, 0, 0,
        {transpose_voice, transpose_voice});
    add(registry, 0x8961, 0x8961, {k::returned_at(0x8965)}, 0, 0,
        {set_duration, set_duration});
    add(registry, 0x8966, 0x8966, {k::conditional_at(0x896C)}, 0, 0,
        {set_attenuation, set_attenuation});
    add(registry, 0x8973, 0x8973, {k::core_final_at(0x8974)}, 0, 0,
        {publish_attenuation, publish_attenuation});
    add(registry, 0x898B, 0x898B, {k::conditional_at(0x898C)}, 0, 0,
        {choose_instrument, choose_instrument});
    add(registry, 0x8998, 0x8998, {k::returned_at(0x899D), k::returned_at(0x89A5), k::core_final_at(0x89A6)}, 0, 0,
        {change_instrument, change_instrument});
    add(registry, 0x89A9, 0x89A9, {k::returned_at(0x89AD)}, 0, 0,
        {set_envelope, set_envelope});
    add(registry, 0x89C6, 0x89C6, {k::returned_at(0x89CB)}, 0, 0,
        {branch_score, branch_score});
    add(registry, 0x89CC, 0x89CC, {k::conditional_at(0x89CF)}, 0, 0,
        {enable_slide, enable_slide});
    add(registry, 0x89D1, 0x89CC, {k::conditional_at(0x89D9)}, 0, 0,
        {enable_slide_89d1, enable_slide_89d1});
    add(registry, 0x89E8, 0x89E8, {k::returned_at(0x89EB), k::core_final_at(0x89FC)}, 0, 0,
        {install_slide_instrument, install_slide_instrument});
    add(registry, 0x89FF, 0x89FF, {k::conditional_at(0x8A07)}, 0, 0,
        {disable_slide, disable_slide});
    add(registry, 0x8A16, 0x8A16, {k::returned_at(0x8A19), k::core_final_at(0x8A26)}, 0, 0,
        {restore_slide_instrument, restore_slide_instrument});
    add(registry, 0x8A29, 0x8A29, {k::returned_at(0x8A32), k::returned_at(0x8A37)}, 0, 0,
        {select_period_format, select_period_format});
    add(registry, 0x8A38, 0x8A38, {k::conditional_at(0x8A45)}, 0, 0,
        {end_sequence, end_sequence});
    add(registry, 0x8A9A, 0x8A9A, {k::returned_at(0x8A9C, 4)}, 0, 4,
        {leave_sequence, leave_sequence});
    add(registry, 0x8A9D, 0x8A9D, {k::conditional_at(0x8AAF)}, 6, 0,
        {restore_displaced_voice, restore_displaced_voice});
    add(registry, 0x8AB1, 0x8A9D, {k::core_final_at(0x8AC0)}, 4, 0,
        {restore_displaced_voice_8ab1, restore_displaced_voice_8ab1});
    add(registry, 0x8AC3, 0x8A9D, {k::core_final_at(0x8AC3)}, 0, 0,
        {restore_displaced_voice_8ac3, restore_displaced_voice_8ac3});
    add(registry, 0x8AC6, 0x8A9D, {k::core_final_at(0x8ADC)}, 2, 0,
        {restore_displaced_voice_8ac6, restore_displaced_voice_8ac6});
    add(registry, 0x8ADF, 0x8A9D, {k::core_final_at(0x8ADF)}, 0, 0,
        {restore_displaced_voice_8adf, restore_displaced_voice_8adf});
    add(registry, 0x8AE2, 0x8AE2, {k::returned_at(0x8AED)}, 0, 0,
        {restore_level, restore_level});
    add(registry, 0x8AEE, 0x8AEE, {k::returned_at(0x8B08)}, 4, 0,
        {call_score, call_score});
    add(registry, 0x8B09, 0x8B09, {k::returned_at(0x8B1B)}, 2, 0,
        {return_score, return_score});
    add(registry, 0x8B1C, 0x8B1C, {k::conditional_at(0x8B2F)}, 2, 0,
        {repeat_score, repeat_score});
    add(registry, 0x8B32, 0x8B1C, {k::returned_at(0x8B33)}, 0, 0,
        {repeat_score_8b32, repeat_score_8b32});
    add(registry, 0x8B34, 0x8B34, {k::returned_at(0x8B36)}, 2, 0,
        {chip_wait, chip_wait});
}
} // namespace psmodern::game
