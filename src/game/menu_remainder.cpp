// SPDX-License-Identifier: GPL-3.0-or-later
// Complete the interrupt and SRAM boundaries around the existing printers.
// Milestone 14: every instruction of these stretches is rendered, the switches
// of the interrupt mask included, and the instructions that sit
// between the segments (the frame wait after an EI, the printers' returns)
// are segments of their own.
#include "game/menu_remainder.h"
#include "game/menu_kit.h"
#include "game/retro_events.h"
namespace psmodern::game {
namespace mk=menu_kit;
void register_menu_remainder(RoutineRegistry& registry) {
    // The interrupt switches of the menu printers: one instruction each, the
    // first or last of a printer's draw (interrupts off while it writes to the
    // video chip, on again before it waits for the frame).
    auto mask=[&](RoutineEntry entry,RoutineEntry unit,InterruptMask value) {
        auto exit=mk::core_final_at(entry.address);
        exit.interrupt_mask=value;
        auto spec=mk::segment(unit,{exit});
        spec.single_instruction=true;
        spec.observe_entry=retro_window_entry(entry.address);
        const auto at=entry.address;
        mk::add_segment(registry,entry,std::move(spec),[](const Machine&) { return 4; },[at,value](auto& z)->mk::Exit {
            return value==InterruptMask::disable ? z.final_disable_interrupts(at) : z.final_enable_interrupts(at);
        });
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
    // The registers a printer saved come back just before interrupts are enabled.
    auto restore=[&](std::uint16_t at,std::uint16_t last,RoutineEntry unit,bool two) {
        auto exit=mk::core_final_at(last,two ? 4 : 2);
        exit.interrupt_mask=InterruptMask::enable;
        const RoutineEntry entry{0,at};
        auto spec=mk::segment(unit,{exit},{.inherited_stack=two ? 4u : 2u});
        spec.observe_entry=at==0x340F; // both dialogue glyph tiles have been written
        mk::add_segment(registry,entry,std::move(spec),[=](auto& z)->mk::Exit {
            if(two) {
                if(at==0x319A || at==0x35B3) z.pop(at,z.r.h,z.r.l);
                else z.pop(at,z.r.d,z.r.e);
            }
            z.pop(static_cast<std::uint16_t>(at+(two ? 1 : 0)),z.r.b,z.r.c);
            return z.final_enable_interrupts(last);
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
        return z.final_disable_interrupts(0x3A69);
    });
    // The save-slot names are drawn with cartridge RAM switched in: the load of
    // the control value, then the store that selects it.
    for(const auto [at,last]:{std::pair{std::uint16_t{0x39B1},std::uint16_t{0x39B3}},
                              std::pair{std::uint16_t{0x39C2},std::uint16_t{0x39C4}}}) {
        const RoutineEntry entry{0,at};
        auto exit=mk::core_final_at(last);
        exit.cartridge_control=true;
        auto spec=mk::segment(menu::save_slots_unit,{exit});
        spec.observe_entry=retro_window_entry(at);
        mk::add_segment(registry,entry,std::move(spec),[=](auto& z)->mk::Exit {
            z.load_immediate(at,z.r.a);
            z.store_a_direct(last);
            return mk::fall_through(last,static_cast<std::uint16_t>(last+3));
        });
    }
    const RoutineEntry bank{0,0x3A89};
    auto store=mk::core_final_at(bank.address);
    store.window_bank_store=true;
    auto spec=mk::segment(menu::draw_window_at_once_unit,{store});
    spec.single_instruction=true;
    mk::add_segment(registry,bank,std::move(spec),[](const Machine&) { return 13; },[](auto& z)->mk::Exit {
        z.store_a_direct(0x3A89);   // selects the window's bank in slot 2
        return mk::fall_through(0x3A89,0x3A8C);
    });

    // ---- The joining segments between these segments --------------------------------
    // The printers enable interrupts and then wait one frame: the load of the
    // wait's index and the call of the frame wait follow the enabling
    // instruction directly (the instruction after an EI is never interrupted).
    auto frame_wait_after_enable=[&](std::uint16_t at,RoutineEntry unit) {
        auto spec=mk::segment(unit,{mk::core_final_at(static_cast<std::uint16_t>(at+2))},{.scratch_stack=2});
        spec.observe_entry=at==0x332B || retro_window_entry(at);
        mk::add_segment(registry,{0,at},std::move(spec),[at](auto& z)->mk::Exit {
            z.load_immediate(at,z.r.a);
            return z.final_call(static_cast<std::uint16_t>(at+2));
        });
    };
    frame_wait_after_enable(0x3030,menu::number_unit);
    frame_wait_after_enable(0x30A1,menu::long_number_unit);
    frame_wait_after_enable(0x332B,text_number_unit);
    frame_wait_after_enable(0x3411,text_output_unit);
    frame_wait_after_enable(0x35B6,menu::item_name_line_unit);
    frame_wait_after_enable(0x3633,menu::money_unit);
    frame_wait_after_enable(0x3A7A,menu::row_drawer_unit);
    // The printer returns to its caller once the frame has passed.
    auto leave_printer=[&](std::uint16_t at,RoutineEntry unit) {
        auto leave=mk::segment(unit,{mk::returned_at(at)});
        leave.single_instruction=true;
        mk::add_segment(registry,{0,at},std::move(leave),[](const Machine&) { return 10; },
                        [at](auto& z)->mk::Exit { return z.final_return(at); });
    };
    leave_printer(0x3035,menu::number_unit);
    leave_printer(0x30A6,menu::long_number_unit);
    leave_printer(0x319D,menu::enemy_name_line_unit);
    leave_printer(0x35BB,menu::item_name_line_unit);
    leave_printer(0x3638,menu::money_unit);
    leave_printer(0x3AC3,menu::save_under_unit);
    // The alternate-text printer: the video setup call before interrupts are
    // enabled again, and the text pointer restored after it.
    auto video_setup=mk::segment(text_alternate_unit,{mk::core_final_at(0x33C3)},{.scratch_stack=2});
    video_setup.single_instruction=true;
    mk::add_segment(registry,{0,0x33C3},std::move(video_setup),[](const Machine&) { return 17; },
                    [](auto& z)->mk::Exit { return z.final_call(0x33C3); });
    auto restore_pointer=mk::segment(text_alternate_unit,{mk::core_final_at(0x33C7)});
    restore_pointer.single_instruction=true;
    mk::add_segment(registry,{0,0x33C7},std::move(restore_pointer),[](const Machine&) { return 10; },[](auto& z)->mk::Exit {
        z.pop(0x33C7,z.r.h,z.r.l);
        return mk::fall_through(0x33C7,0x33C8);
    });
    // The window draw gets its saved bank back.
    auto restore_bank=mk::segment(menu::draw_window_at_once_unit,{mk::core_final_at(0x3AA1)});
    restore_bank.single_instruction=true;
    mk::add_segment(registry,{0,0x3AA1},std::move(restore_bank),[](const Machine&) { return 10; },[](auto& z)->mk::Exit {
        z.pop(0x3AA1,z.r.a,z.r.f);
        return mk::fall_through(0x3AA1,0x3AA2);
    });
    register_menu_engine_joins(registry);
    register_menu_windows_joins(registry);
    register_menu_field_joins(registry);
    register_menu_places_joins(registry);
}
}
