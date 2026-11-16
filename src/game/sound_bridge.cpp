// SPDX-License-Identifier: GPL-3.0-or-later
// Bank selection and host-observed entry points of the sound driver.
#include "game/menu_kit.h"
#include "game/sound.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;

// Each wrapper maps the sound bank before its original jump. The host sees
// the init and silence entry before this work, including on native fallback.
template<class Z> k::Exit map_sound_bank(Z& z,std::uint16_t at) {
    z.load_hl(at);
    z.store_to_hl(static_cast<std::uint16_t>(at+3));
    return k::continues(static_cast<std::uint16_t>(at+5),k::jump_cycles);
}
// The frame host observes the entry before HL and the alternate registers
// are changed. EXX remains the final instruction executed by the core.
template<class Z> k::Exit begin_sound_tick(Z& z) {
    z.load_hl(0x8043);
    return k::continues(0x8046,4);
}
// The host observes the request before it is read; classification then uses
// the original conditional branch at 82F4.
template<class Z> k::Exit read_sound_request(Z& z) {
    z.load_a_direct(0x82EF);
    z.test_bit(0x82F2,7,z.r.a);
    return k::continues(0x82F4,k::jump_cycles);
}
template<class Logic> void add_observed(RoutineRegistry& registry,RoutineEntry entry,SegmentSpec spec,Logic logic) {
    spec.observe_entry=true;
    k::add_segment(registry,entry,std::move(spec),logic);
}
}
void register_sound_bridge(RoutineRegistry& registry) {
    const RoutineEntry update{0,0x02E5};
    const RoutineEntry initialize{0,0x02ED};
    const RoutineEntry silence{0,0x02F5};
    k::add_segment(registry,update,k::segment(update,{k::core_final_at(0x02EA)},{.slot2_mapper=true}),[](auto& z) { return map_sound_bank(z,0x02E5); });
    add_observed(registry,initialize,k::segment(initialize,{k::core_final_at(0x02F2)},{.slot2_mapper=true}),[](auto& z) { return map_sound_bank(z,0x02ED); });
    add_observed(registry,silence,k::segment(silence,{k::core_final_at(0x02FA)},{.slot2_mapper=true}),[](auto& z) { return map_sound_bank(z,0x02F5); });
    add_observed(registry,{12,0x8043},k::segment({12,0x8043},{k::core_final_at(0x8046)}),[](auto& z) { return begin_sound_tick(z); });
    add_observed(registry,{12,0x82EF},k::segment({12,0x82EF},{k::conditional_at(0x82F4)}),[](auto& z) { return read_sound_request(z); });
}
}
