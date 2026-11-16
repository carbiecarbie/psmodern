// SPDX-License-Identifier: GPL-3.0-or-later
// Set up the per-voice records selected by a sound request.
#include "game/menu_kit.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
constexpr auto ix=k::Index::ix;
constexpr RoutineEntry unit{12,0x83F4};

// The selected score record begins with the number of voices to initialize.
template<class Z> k::Exit count_selected_voices(Z& z) {
    z.copy(0x83F4,z.r.h,z.r.b);
    z.copy(0x83F5,z.r.l,z.r.c);
    z.load_from_hl(0x83F6,z.r.b);
    return k::continues(0x83F7,6);
}
// The preceding shared-channel selection leaves HL at the last flag. Mark
// it before continuing into the selected-voice setup.
template<class Z> k::Exit mark_last_shared_voice(Z& z) {
    z.set_bit_at_hl(0x83F2,2);
    return count_selected_voices(z);
}
// Copy one score record into work RAM, set its playback state and arrange the
// original return through the PSG level writer before the next voice.
template<class Z> k::Exit initialize_voice_record(Z& z) {
    z.push(0x83F8,z.r.b,z.r.c);
    z.push(0x83F9,z.r.h,z.r.l);
    z.pop_index(0x83FA,ix);
    z.load_bc(0x83FC);
    z.block_copy(0x83FF);
    z.load_immediate(0x8401,z.r.a);
    z.store_a_to_pair(0x8403,z.de());
    z.increment_pair(0x8404,z.r.d,z.r.e);
    z.load_immediate(0x8405,z.r.a);
    z.store_a_to_pair(0x8407,z.de());
    z.increment_pair(0x8408,z.r.d,z.r.e);
    z.xor_a(0x8409);
    z.store_a_to_pair(0x840A,z.de());
    z.increment_pair(0x840B,z.r.d,z.r.e);
    z.store_a_to_pair(0x840C,z.de());
    z.increment_pair(0x840D,z.r.d,z.r.e);
    z.store_a_to_pair(0x840E,z.de());
    z.push(0x840F,z.r.h,z.r.l);
    z.load_hl(0x8410);
    z.add_to_hl(0x8413,z.de());
    z.exchange_de_hl(0x8414);
    z.pop(0x8415,z.r.h,z.r.l);
    z.load_bc(0x8416);
    z.push(0x8419,z.r.b,z.r.c);
    return k::continues(0x841A,k::jump_cycles);
}
// The level writer has returned; restore the count and visit the next voice.
template<class Z> k::Exit next_selected_voice(Z& z) {
    z.pop(0x841D,z.r.b,z.r.c);
    return k::decrement_jump_exit(z,0x841E);
}
}
void register_sound_setup(RoutineRegistry& registry) {
    k::add_segment(registry,unit,k::segment(unit,{k::core_final_at(0x83F7)}),[](auto& z) { return count_selected_voices(z); });
    const auto mark=[](auto& z) { return mark_last_shared_voice(z); };
    // Admitted only while HL points at the voice record at C1AE, the pointer
    // this entry was verified with; any other HL runs the original
    // instructions, so the C++ body never writes through an unverified address.
    registry.add({12,0x83F2},k::segment(unit,{k::core_final_at(0x83F7)}),
        [mark](const Machine& machine) { return machine.cpu().hl.uint16_value==0xc1ae ? k::cost(machine,mark,k::uncertain_maximum) : k::uncertain_maximum; },
        [mark](RoutineContext& context) { k::run(context,mark); });
    const auto setup=[](auto& z) { return initialize_voice_record(z); };
    registry.add({12,0x83F8},k::segment(unit,{k::core_final_at(0x841A,-4)},{.scratch_stack=4}),
        [setup](const Machine& machine) {
            const auto destination=machine.cpu().de.uint16_value;
            const auto stack=machine.cpu().sp.uint16_value;
            if (destination<0xc000 || destination>0xc1f0 || stack<0xc400 || stack>0xdffc ||
                machine.memory().mapping(stack-4).kind!=Memory::Kind::ram) return k::uncertain_maximum;
            return k::cost(machine,setup,k::uncertain_maximum);
        },[setup](RoutineContext& context) { k::run(context,setup); });
    k::add_segment(registry,{12,0x841D},k::segment(unit,{k::conditional_at(0x841E,2)},{.inherited_stack=2}),[](auto& z) { return next_selected_voice(z); });
}
}
