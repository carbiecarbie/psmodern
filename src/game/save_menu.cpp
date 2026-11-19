// SPDX-License-Identifier: GPL-3.0-or-later
// The save path of the menu (00:1D0E-1D34): select cartridge RAM, clear the
// slot's validity flag, copy the 1 KiB game state into the slot, deselect,
// then set the flag and leave. Milestone 14 worked example (map kit): every
// final instruction is rendered, and the two states the bodies do not handle
// are explicit entry guards.
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace k=map_kit;
constexpr SegmentExit::FlagRange slot_flags{0x8201,0x8205};
// HL names one of the five slot flags and cartridge RAM is selected there.
// Read at entry, before the segment's first (and only boundary-free)
// instruction that uses it, so it does not need to hold across a delivery.
const k::Guard flag_guard{"HL is a save slot flag in selected cartridge RAM",[](const Machine& m) {
    const auto hl=m.cpu().hl.uint16_value;
    return hl>=slot_flags.first && hl<=slot_flags.last && m.memory().mapping(hl).kind==Memory::Kind::cartridge_ram;
}};
}
void register_save_menu(RoutineRegistry& registry) {
    const auto save=save_menu_entries.front();
    // A segment that ends by writing A to the cartridge control register
    // (FFFC): selecting cartridge RAM (08) or the ROM again (80).
    auto control=[&](std::uint16_t pc,std::uint16_t store,std::int8_t stack,k::Permissions permissions,auto logic) {
        SegmentExit end{.kind=RoutineExit::core_final,.address=store,.stack=stack,.cartridge_control=true};
        auto spec=k::segment(save,{end},permissions);
        spec.observe_entry=pc==dungeon::after_save.address;
        k::add_segment(registry,{0,pc},std::move(spec),[=](auto& z) {
            logic(z);
            z.store_a_direct(store);
            return k::fall_through(store,static_cast<std::uint16_t>(store+3));
        });
    };
    control(0x1D0E,0x1D12,-4,{.scratch_stack=4},[](auto& z) {
        z.push(0x1D0E,z.r.a,z.r.f);
        z.push(0x1D0F,z.r.h,z.r.l);
        z.load_immediate(0x1D10,z.r.a);
    });
    // Clear the slot's flag, then load the copy's source and length; the copy
    // itself is the block transfer at 00:1D1D.
    k::add_segment(registry,{0,0x1D15},k::segment(save,{k::core_final_at(0x1D1A)}),flag_guard,[](auto& z) {
        z.store_to_hl(0x1D15);
        z.load_hl(0x1D17);
        z.load_bc(0x1D1A);
        return k::fall_through(0x1D1A,0x1D1D);
    });
    control(0x1D1F,0x1D21,0,{},[](auto& z) { z.load_immediate(0x1D1F,z.r.a); });
    control(0x1D24,0x1D28,4,{.inherited_stack=4},[](auto& z) {
        z.pop(0x1D24,z.r.h,z.r.l);
        z.pop(0x1D25,z.r.a,z.r.f);
        z.load_immediate(0x1D26,z.r.a);
    });
    // Publish the save: set the slot's flag. The automap observes the
    // instruction after it, when the 1 KiB block and its flag are already in
    // cartridge RAM and the cartridge control still selects that RAM.
    SegmentExit publish{.kind=RoutineExit::core_final,.address=0x1D2B,.cartridge_flags=slot_flags};
    auto spec=k::segment(save,{publish});
    spec.single_instruction=true;
    k::add_segment(registry,{0,0x1D2B},std::move(spec),flag_guard,[](auto& z) {
        z.store_to_hl(0x1D2B);
        return k::fall_through(0x1D2B,0x1D2D);
    });
    control(0x1D2D,0x1D2F,0,{},[](auto& z) { z.load_immediate(0x1D2D,z.r.a); });
    // Leave the menu when the caller's flags say so.
    k::add_segment(registry,{0,0x1D32},k::segment(save,{k::conditional_at(0x1D33,2)},{.inherited_stack=2}),[](auto& z) {
        z.pop(0x1D32,z.r.b,z.r.c);
        return z.final_relative_if(0x1D33,k::Condition::z);
    });
}
}
