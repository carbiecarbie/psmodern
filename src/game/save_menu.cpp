// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/map_kit.h"
#include "game/routines.h"

namespace psmodern::game {
namespace {
namespace k=map_kit;
constexpr SegmentExit::FlagRange slot_flags{0x8201,0x8205};
bool writable_flag(const Machine& m) {
    const auto hl=m.cpu().hl.uint16_value;
    return hl>=slot_flags.first && hl<=slot_flags.last && m.memory().mapping(hl).kind==Memory::Kind::cartridge_ram;
}
}
void register_save_menu(RoutineRegistry& registry) {
    const auto save=save_menu_entries.front();
    auto control=[&](std::uint16_t pc,std::uint16_t store,std::int8_t stack,k::Permissions permissions,auto logic) {
        SegmentExit end{.kind=RoutineExit::core_final,.address=store,.stack=stack,.cartridge_control=true};
        auto spec=k::segment(save,{end},permissions);
        spec.observe_entry=pc==dungeon::after_save.address;
        k::add_segment(registry,{0,pc},std::move(spec),[=](auto& z) {
            logic(z);
            return k::continues(store,13);
        });
    };
    control(0x1D0E,0x1D12,-4,{.scratch_stack=4},[](auto& z) {
        z.push(0x1D0E,z.r.a,z.r.f);
        z.push(0x1D0F,z.r.h,z.r.l);
        z.load_immediate(0x1D10,z.r.a);
    });
    auto invalidate=[](auto& z) {
        z.store_to_hl(0x1D15);
        z.load_hl(0x1D17);
        return k::continues(0x1D1A,10);
    };
    registry.add({0,0x1D15},k::segment(save,{k::core_final_at(0x1D1A)}),[=](const Machine& m) {
        if (!writable_flag(m)) return k::uncertain_maximum;
        return k::cost(m,invalidate,k::uncertain_maximum);
    },[=](RoutineContext& c){k::run(c,invalidate);});
    control(0x1D1F,0x1D21,0,{},[](auto& z) { z.load_immediate(0x1D1F,z.r.a); });
    control(0x1D24,0x1D28,4,{.inherited_stack=4},[](auto& z) {
        z.pop(0x1D24,z.r.h,z.r.l);
        z.pop(0x1D25,z.r.a,z.r.f);
        z.load_immediate(0x1D26,z.r.a);
    });
    // The automap observes the instruction after publication. At that point
    // the 1 KiB block and its validity flag are already in cartridge RAM,
    // and the cartridge control still selects that RAM.
    SegmentExit publish{.kind=RoutineExit::core_final,.address=0x1D2B,.cartridge_flags=slot_flags};
    auto spec=k::segment(save,{publish});
    spec.single_instruction=true;
    registry.add({0,0x1D2B},std::move(spec),[](const Machine& m) {
        return writable_flag(m) ? std::uint64_t{10} : k::uncertain_maximum;
    },[](RoutineContext& c){c.finish_core(0x1D2B);});
    control(0x1D2D,0x1D2F,0,{},[](auto& z) { z.load_immediate(0x1D2D,z.r.a); });
    k::add_segment(registry,{0,0x1D32},k::segment(save,{k::conditional_at(0x1D33,2)},{.inherited_stack=2}),[](auto& z) {
        z.pop(0x1D32,z.r.b,z.r.c);
        return k::relative_if_exit(0x1D33,z.zero());
    });
}
}
