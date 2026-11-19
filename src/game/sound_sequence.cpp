// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/menu_kit.h"
#include "game/routines.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
// Sound-sequence commands use an indexed table in the player's ROM. Keep the
// indirect jump as the registered final instruction and preserve its saved
// return address on the stack for the selected handler.
template<class Z> k::Exit dispatch_command_table(Z& z) {
    z.push(0x851C,z.r.h,z.r.l);
    z.subtract_immediate(0x851D);
    z.load_hl(0x851F);
    z.add_register(0x8522,z.r.a);
    z.copy(0x8523,z.r.c,z.r.a);
    z.load_immediate(0x8524,z.r.b);
    z.add_to_hl(0x8526,z.bc());
    z.load_from_hl(0x8527,z.r.c);
    z.increment_hl(0x8528);
    z.load_from_hl(0x8529,z.r.h);
    z.copy(0x852A,z.r.l,z.r.c);
    return z.final_jump_hl(0x852B);
}
template<class Z> k::Exit dispatch_sequence_command(Z& z) {
    z.load_hl(0x8519);
    return dispatch_command_table(z);
}
// A completed command resumes the score reader at its original jump.
template<class Z> k::Exit resume_sequence_after_command(Z& z) {
    z.increment_pair(0x852C,z.r.d,z.r.e);
    return z.final_jump(0x852D);
}
// The indexed reset instructions preserve flags, just like their set form.
template<class Z> void clear_indexed_bit(Z& z,std::uint16_t at,unsigned bit) {
    z.bus().fetch(at);
    z.bus().fetch(static_cast<std::uint16_t>(at+1));
    const auto displacement=static_cast<std::int8_t>(z.bus().read(static_cast<std::uint16_t>(at+2)));
    z.r.wz=static_cast<std::uint16_t>(z.r.ix+displacement);
    (void)z.bus().read(static_cast<std::uint16_t>(at+3)); // the operation byte, read as data
    z.bus().advance(4);
    auto value=z.bus().read(z.r.wz);
    z.bus().write(z.r.wz,static_cast<std::uint8_t>(value&~(1u<<bit)));
    z.bus().advance(19);
}
// Commands fetched from the score adjust one voice at a time. Every operand
// is read through the bus from the player's cartridge or work RAM.
template<class Z> k::Exit command_fade(Z& z) {
    z.load_a_from_pair(0x8554,z.de());
    z.store_a_direct(0x8555);
    z.store_a_direct(0x8558);
    return z.final_jump(0x855B);
}
template<class Z> k::Exit command_add_timing(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_a_from_pair(0x855E,z.de());
    z.add_from_indexed(0x855F,ix);
    z.store_to_indexed(0x8562,ix,z.r.a);
    return z.final_return(0x8565);
}
template<class Z> k::Exit command_add_level(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_a_direct(0x8566);
    z.or_a(0x8569);
    if (!z.jump_if(0x856A,z.sign())) {
        z.load_a_from_pair(0x856D,z.de());
        z.add_from_indexed(0x856E,ix);
        return z.final_jump(0x8571);
    }
    z.load_a_from_pair(0x8574,z.de());
    z.add_from_indexed(0x8575,ix);
    z.and_immediate(0x8578);
    z.store_to_indexed(0x857A,ix,z.r.a);
    return z.final_return(0x857D);
}
template<class Z> k::Exit command_add_shift(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_a_from_pair(0x8595,z.de());
    z.increment_pair(0x8596,z.r.d,z.r.e);
    z.relative(0x8597);
    z.add_from_indexed(0x8599,ix);
    z.store_to_indexed(0x859C,ix,z.r.a);
    return z.final_return(0x859F);
}
template<class Z> k::Exit command_set_timing(Z& z) {
    z.load_a_from_pair(0x85A0,z.de());
    z.store_to_indexed(0x85A1,k::Index::ix,z.r.a);
    return z.final_return(0x85A4);
}
template<class Z> k::Exit command_set_level(Z& z) {
    z.and_immediate(0x85A5);
    z.store_to_indexed(0x85A7,k::Index::ix,z.r.a);
    return z.final_jump(0x85AA);
}
template<class Z> k::Exit command_noise(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_a_from_pair(0x85AD,z.de());
    z.or_immediate(0x85AE);
    z.output(0x85B0);
    z.or_immediate(0x85B2);
    z.increment(0x85B4,z.r.a);
    if (!z.relative_if(0x85B5,!z.zero())) {
        clear_indexed_bit(z,0x85B7,6);
        return z.final_return(0x85BB);
    }
    z.set_bit_indexed(0x85BC,ix,6);
    return z.final_return(0x85C0);
}
template<class Z> k::Exit command_set_mode(Z& z) {
    z.load_a_from_pair(0x85C1,z.de());
    z.increment_pair(0x85C2,z.r.d,z.r.e);
    z.compare_immediate(0x85C3);
    if (auto exit=z.final_return_if(0x85C5,k::Condition::z)) return *exit;
    z.store_to_indexed(0x85C6,k::Index::ix,z.r.a);
    return z.final_return(0x85C9);
}
template<class Z> k::Exit command_set_field(Z& z) {
    z.load_a_from_pair(0x85CA,z.de());
    z.store_to_indexed(0x85CB,k::Index::ix,z.r.a);
    return z.final_return(0x85CE);
}
// The score toggles voice flags, adjusts level steps and repeats a passage.
template<class Z> k::Exit command_toggle_mode(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_a_from_pair(0x857E,z.de());
    z.compare_immediate(0x857F);
    if (!z.relative_if(0x8581,z.zero())) { clear_indexed_bit(z,0x8583,0); clear_indexed_bit(z,0x8587,1);
    return z.final_return(0x858B); }
    z.set_bit_indexed(0x858C,ix,0);
    z.set_bit_indexed(0x8590,ix,1);
    return z.final_return(0x8594);
}
template<class Z> k::Exit command_loop_address(Z& z) {
    z.exchange_de_hl(0x85E7);
    z.load_from_hl(0x85E8,z.r.e);
    z.increment_hl(0x85E9);
    z.load_from_hl(0x85EA,z.r.d);
    z.decrement_pair(0x85EB,z.r.d,z.r.e);
    return z.final_return(0x85EC);
}
template<class Z> k::Exit command_loop(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_immediate(0x85CF,z.r.b);
    z.load_immediate(0x85D1,z.r.c);
    z.push_index(0x85D3,ix);
    z.pop(0x85D5,z.r.h,z.r.l);
    z.add_to_hl(0x85D6,z.bc());
    z.load_from_hl(0x85D7,z.r.a);
    z.or_a(0x85D8);
    if (!z.relative_if(0x85D9,!z.zero())) {
        z.load_a_from_pair(0x85DB,z.de());
        z.decrement(0x85DC,z.r.a);
        z.store_register_to_hl(0x85DD,z.r.a);
        z.increment_pair(0x85DE,z.r.d,z.r.e);
        z.increment_pair(0x85DF,z.r.d,z.r.e);
        return z.final_return(0x85E0);
    }
    z.increment_pair(0x85E1,z.r.d,z.r.e);
    z.decrement_at_hl(0x85E2);
    if (z.relative_if(0x85E3,z.zero())) return command_loop_address(z);
    z.increment_pair(0x85E5,z.r.d,z.r.e);
    return z.final_return(0x85E6);
}
template<class Z> k::Exit command_shift_level(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_a_from_pair(0x85ED,z.de());
    z.compare_immediate(0x85EE);
    if (!z.relative_if(0x85F0,!z.zero())) {
        z.set_bit_indexed(0x85F2,ix,5);
        z.load_from_indexed(0x85F6,ix,z.r.a);
        z.load_from_indexed(0x85F9,ix,z.r.a);
        z.or_a(0x85FC);
        if (auto exit=z.final_return_if(0x85FD,k::Condition::z)) return *exit;
        z.decrement_indexed(0x85FE,ix);
        z.decrement_indexed(0x8601,ix);
        return z.final_return(0x8604);
    }
    clear_indexed_bit(z,0x8605,5);
    z.load_from_indexed(0x8609,ix,z.r.a);
    z.load_from_indexed(0x860C,ix,z.r.a);
    z.or_a(0x860F);
    if (auto exit=z.final_return_if(0x8610,k::Condition::z)) return *exit;
    z.increment_indexed(0x8611,ix);
    z.increment_indexed(0x8614,ix);
    return z.final_return(0x8617);
}
template<class Z> k::Exit command_toggle_voice(Z& z) {
    z.load_a_from_pair(0x8618,z.de());
    z.compare_immediate(0x8619);
    if (!z.relative_if(0x861B,!z.zero())) { z.set_bit_indexed(0x861D,k::Index::ix,3);
    return z.final_return(0x8621); }
    clear_indexed_bit(z,0x8622,3);
    return z.final_return(0x8626);
}
// Sequence subroutines save and restore score return addresses inside the
// voice record. The stack-like index is a byte and deliberately wraps.
template<class Z> k::Exit command_call_sequence(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_a_from_pair(0x8687,z.de());
    z.copy(0x8688,z.r.c,z.r.a);
    z.increment_pair(0x8689,z.r.d,z.r.e);
    z.load_a_from_pair(0x868A,z.de());
    z.copy(0x868B,z.r.b,z.r.a);
    z.push(0x868C,z.r.b,z.r.c);
    z.push_index(0x868D,ix);
    z.pop(0x868F,z.r.h,z.r.l);
    z.decrement_indexed(0x8690,ix);
    z.load_from_indexed(0x8693,ix,z.r.c);
    z.decrement_indexed(0x8696,ix);
    z.load_immediate(0x8699,z.r.b);
    z.add_to_hl(0x869B,z.bc());
    z.store_register_to_hl(0x869C,z.r.d);
    z.decrement_hl(0x869D);
    z.store_register_to_hl(0x869E,z.r.e);
    z.pop(0x869F,z.r.d,z.r.e);
    z.decrement_pair(0x86A0,z.r.d,z.r.e);
    return z.final_return(0x86A1);
}
template<class Z> k::Exit command_return_sequence(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.push_index(0x86A2,ix);
    z.pop(0x86A4,z.r.h,z.r.l);
    z.load_from_indexed(0x86A5,ix,z.r.c);
    z.load_immediate(0x86A8,z.r.b);
    z.add_to_hl(0x86AA,z.bc());
    z.load_from_hl(0x86AB,z.r.e);
    z.increment_hl(0x86AC);
    z.load_from_hl(0x86AD,z.r.d);
    z.increment_indexed(0x86AE,ix);
    z.increment_indexed(0x86B1,ix);
    return z.final_return(0x86B4);
}
// A score loop selects a counter in the voice record; an unfinished loop
// branches to the shared target reader, while an exhausted one falls through.
template<class Z> k::Exit command_counted_loop(Z& z) {
    constexpr auto ix=k::Index::ix;
    z.load_a_from_pair(0x86B5,z.de());
    z.increment_pair(0x86B6,z.r.d,z.r.e);
    z.add_immediate(0x86B7);
    z.copy(0x86B9,z.r.c,z.r.a);
    z.load_immediate(0x86BA,z.r.b);
    z.push_index(0x86BC,ix);
    z.pop(0x86BE,z.r.h,z.r.l);
    z.add_to_hl(0x86BF,z.bc());
    z.load_from_hl(0x86C0,z.r.a);
    z.or_a(0x86C1);
    if (!z.relative_if(0x86C2,!z.zero())) { z.load_a_from_pair(0x86C4,z.de());
    z.store_register_to_hl(0x86C5,z.r.a); }
    z.increment_pair(0x86C6,z.r.d,z.r.e);
    z.decrement_at_hl(0x86C7);
    return z.final_jump_if(0x86C8,k::Condition::nz);
}
template<class Z> k::Exit command_loop_done(Z& z) { z.increment_pair(0x86CB,z.r.d,z.r.e);
return z.final_return(0x86CC); }
// Voice-record commands that use the stack need IX in the records and SP in
// work RAM with room for the held words. Registers and the fixed work-RAM
// mapping do not change when a handler runs: the guard holds at entry and
// stays valid after a delivery.
template<class Logic> void add_voice_stack(RoutineRegistry& registry,RoutineEntry entry,SegmentSpec spec,Logic logic) {
    k::add_segment(registry,entry,std::move(spec),
        k::Guard{"IX in the voice records, SP in work RAM",[](const Machine& machine) {
            const auto ix=machine.cpu().ix_iy[0].uint16_value, sp=machine.cpu().sp.uint16_value;
            return ix>=0xc001 && ix<=0xc1ff && sp>=0xc400 && sp<=0xdffc && machine.memory().mapping(sp-4).kind==Memory::Kind::ram;
        }},logic);
}
// This tiny helper preserves HL through the original stack and returns.
template<class Z> k::Exit preserve_pointer(Z& z) {
    z.push(0x86CD,z.r.h,z.r.l);
    z.pop(0x86CE,z.r.h,z.r.l);
    return z.final_return(0x86CF);
}
// Indexed writes outside work RAM can reach the mapper. The game's records
// live in this range; arbitrary entry states outside it are refused at the entry.
template<class Logic> void add_voice(RoutineRegistry& registry,RoutineEntry entry,SegmentSpec spec,Logic logic) {
    k::add_segment(registry,entry,std::move(spec),
        k::Guard{"IX points into the voice records in work RAM",[](const Machine& machine) {
            const auto ix=machine.cpu().ix_iy[0].uint16_value;
            return ix>=0xc000 && ix<=0xdfe8;
        }},logic);
}
}
void register_sound_sequence(RoutineRegistry& registry) {
    k::add_segment(registry,{12,0x852C},k::segment(sound::command,{k::core_final_at(0x852D)}),[](auto& z) { return resume_sequence_after_command(z); });
    k::add_segment(registry,{12,0x86CD},k::segment({12,0x86CD},{k::returned_at(0x86CF)},{.scratch_stack=2}),[](auto& z) { return preserve_pointer(z); });
    k::add_segment(registry,sound::command,k::segment(sound::command,{k::core_final_at(0x852B,-2)},{.scratch_stack=2}),[](auto& z) { return dispatch_sequence_command(z); });
    k::add_segment(registry,{12,0x851C},k::segment(sound::command,{k::core_final_at(0x852B,-2)},{.scratch_stack=2}),[](auto& z) { return dispatch_command_table(z); });
    k::add_segment(registry,{12,0x8554},k::segment({12,0x8554},{k::core_final_at(0x855B)}),[](auto& z) { return command_fade(z); });
    add_voice(registry,{12,0x855E},k::segment({12,0x855E},{k::returned_at(0x8565)}),[](auto& z) { return command_add_timing(z); });
    add_voice(registry,{12,0x8566},k::segment({12,0x8566},{k::core_final_at(0x8571),k::returned_at(0x857D)}),[](auto& z) { return command_add_level(z); });
    add_voice(registry,{12,0x8595},k::segment({12,0x8595},{k::returned_at(0x859F)}),[](auto& z) { return command_add_shift(z); });
    add_voice(registry,{12,0x85A0},k::segment({12,0x85A0},{k::returned_at(0x85A4)}),[](auto& z) { return command_set_timing(z); });
    add_voice(registry,{12,0x85A5},k::segment({12,0x85A5},{k::core_final_at(0x85AA)}),[](auto& z) { return command_set_level(z); });
    add_voice(registry,{12,0x85AD},k::segment({12,0x85AD},{k::returned_at(0x85BB),k::returned_at(0x85C0)}),[](auto& z) { return command_noise(z); });
    add_voice(registry,{12,0x85C1},k::segment({12,0x85C1},{k::returned_at(0x85C5),k::returned_at(0x85C9)}),[](auto& z) { return command_set_mode(z); });
    add_voice(registry,{12,0x85CA},k::segment({12,0x85CA},{k::returned_at(0x85CE)}),[](auto& z) { return command_set_field(z); });
    add_voice(registry,{12,0x857E},k::segment({12,0x857E},{k::returned_at(0x858B),k::returned_at(0x8594)}),[](auto& z) { return command_toggle_mode(z); });
    // The loop command also pushes and pops through SP; same kind of guard as
    // add_voice_stack (registers and the fixed work-RAM mapping only).
    k::add_segment(registry,{12,0x85CF},k::segment({12,0x85CF},{k::returned_at(0x85E0),k::returned_at(0x85E6),k::returned_at(0x85EC)},{.scratch_stack=2}),
        k::Guard{"IX in the voice records, SP in work RAM",[](const Machine& machine) {
            const auto ix=machine.cpu().ix_iy[0].uint16_value, sp=machine.cpu().sp.uint16_value;
            return ix>=0xc000 && ix<=0xdfe0 && sp>=0xc002 && sp<0xe000 && machine.memory().mapping(sp-2).kind==Memory::Kind::ram;
        }},[](auto& z) { return command_loop(z); });
    k::add_segment(registry,{12,0x85E7},k::segment({12,0x85E7},{k::returned_at(0x85EC)}),[](auto& z) { return command_loop_address(z); });
    add_voice(registry,{12,0x85ED},k::segment({12,0x85ED},{k::returned_at(0x85FD),k::returned_at(0x8604),k::returned_at(0x8610),k::returned_at(0x8617)}),[](auto& z) { return command_shift_level(z); });
    add_voice(registry,{12,0x8618},k::segment({12,0x8618},{k::returned_at(0x8621),k::returned_at(0x8626)}),[](auto& z) { return command_toggle_voice(z); });
    add_voice_stack(registry,{12,0x8687},k::segment({12,0x8687},{k::returned_at(0x86A1)},{.scratch_stack=4}),[](auto& z) { return command_call_sequence(z); });
    add_voice_stack(registry,{12,0x86A2},k::segment({12,0x86A2},{k::returned_at(0x86B4)},{.scratch_stack=2}),[](auto& z) { return command_return_sequence(z); });
    add_voice_stack(registry,{12,0x86B5},k::segment({12,0x86B5},{k::conditional_at(0x86C8)},{.scratch_stack=2}),[](auto& z) { return command_counted_loop(z); });
    k::add_segment(registry,{12,0x86CB},k::segment({12,0x86B5},{k::returned_at(0x86CC)}),[](auto& z) { return command_loop_done(z); });
}
}
