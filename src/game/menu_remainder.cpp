// SPDX-License-Identifier: GPL-3.0-or-later
// Complete the interrupt and SRAM boundaries around the existing printers.
#include "game/menu_remainder.h"
#include "game/menu_kit.h"
namespace psmodern::game {
namespace mk=menu_kit;
void register_menu_remainder(RoutineRegistry& registry) {
    auto mask=[&](RoutineEntry entry,RoutineEntry unit,InterruptMask value) {
        auto exit=mk::core_final_at(entry.address);
        exit.interrupt_mask=value;
        auto spec=mk::segment(unit,{exit});
        spec.single_instruction=true;
        registry.add(entry,spec,[](const Machine&) { return 4; },[=](RoutineContext& c) { c.finish_core(entry.address); });
    };
    for(const auto [at,unit]:{
        std::pair{std::uint16_t{0x2FD8},menu::number_wide_label_unit},
        std::pair{std::uint16_t{0x2FE1},menu::number_unit},
        std::pair{std::uint16_t{0x3036},menu::long_number_unit},
        std::pair{std::uint16_t{0x315C},menu::enemy_name_line_unit},
        std::pair{std::uint16_t{0x3304},text_number_unit},
        std::pair{std::uint16_t{0x33C2},text_alternate_unit},
        std::pair{std::uint16_t{0x33D6},text_output_unit},
        std::pair{std::uint16_t{0x356B},menu::item_name_line_unit},
        std::pair{std::uint16_t{0x35BC},menu::money_wide_label_unit},
        std::pair{std::uint16_t{0x35C4},menu::money_unit},
        std::pair{std::uint16_t{0x393A},menu::shop_line_unit},
        std::pair{std::uint16_t{0x3A69},menu::row_drawer_unit},
        std::pair{std::uint16_t{0x3A8C},menu::draw_window_at_once_unit},
        std::pair{std::uint16_t{0x3AA6},menu::save_under_unit}})
        mask({0,at},unit,InterruptMask::disable);
    for(const auto [at,unit]:{
        std::pair{std::uint16_t{0x302F},menu::number_unit},
        std::pair{std::uint16_t{0x30A0},menu::long_number_unit},
        std::pair{std::uint16_t{0x319C},menu::enemy_name_line_unit},
        std::pair{std::uint16_t{0x332A},text_number_unit},
        std::pair{std::uint16_t{0x33C6},text_alternate_unit},
        std::pair{std::uint16_t{0x3410},text_output_unit},
        std::pair{std::uint16_t{0x35B5},menu::item_name_line_unit},
        std::pair{std::uint16_t{0x3632},menu::money_unit},
        std::pair{std::uint16_t{0x3A79},menu::row_drawer_unit},
        std::pair{std::uint16_t{0x3AA0},menu::draw_window_at_once_unit},
        std::pair{std::uint16_t{0x3AC2},menu::save_under_unit}})
        mask({0,at},unit,InterruptMask::enable);
    auto restore=[&](std::uint16_t at,std::uint16_t last,RoutineEntry unit,bool two) {
        auto exit=mk::core_final_at(last,two ? 4 : 2);
        exit.interrupt_mask=InterruptMask::enable;
        const RoutineEntry entry{0,at};
        mk::add_segment(registry,entry,mk::segment(unit,{exit},{.inherited_stack=two ? 4u : 2u}),[=](auto& z)->mk::Exit {
            if(two) {
                if(at==0x319A || at==0x35B3) z.pop(at,z.r.h,z.r.l);
                else z.pop(at,z.r.d,z.r.e);
            }
            z.pop(static_cast<std::uint16_t>(at+(two ? 1 : 0)),z.r.b,z.r.c);
            return mk::continues(last,4);
        });
    };
    restore(0x319A,0x319C,menu::enemy_name_line_unit,true);
    restore(0x340F,0x3410,text_output_unit,false);
    restore(0x35B3,0x35B5,menu::item_name_line_unit,true);
    restore(0x3AC0,0x3AC2,menu::save_under_unit,true);
    const RoutineEntry row{0,0x3A68};
    auto disable=mk::core_final_at(0x3A69,-2);
    disable.interrupt_mask=InterruptMask::disable;
    mk::add_segment(registry,row,mk::segment(menu::row_drawer_unit,{disable},{.scratch_stack=2}),[](auto& z)->mk::Exit {
        z.push(0x3A68,z.r.b,z.r.c);
        return mk::continues(0x3A69,4);
    });
    for(const auto [at,last]:{std::pair{std::uint16_t{0x39B1},std::uint16_t{0x39B3}},
                              std::pair{std::uint16_t{0x39C2},std::uint16_t{0x39C4}}}) {
        const RoutineEntry entry{0,at};
        auto exit=mk::core_final_at(last);
        exit.cartridge_control=true;
        mk::add_segment(registry,entry,mk::segment(menu::save_slots_unit,{exit}),[=](auto& z)->mk::Exit {
            z.load_immediate(at,z.r.a);
            return mk::continues(last,13);
        });
    }
    const RoutineEntry bank{0,0x3A89};
    auto store=mk::core_final_at(bank.address);
    store.window_bank_store=true;
    auto spec=mk::segment(menu::draw_window_at_once_unit,{store});
    spec.single_instruction=true;
    registry.add(bank,spec,[](const Machine&) { return 13; },[](RoutineContext& c) { c.finish_core(0x3A89); });
}
}
