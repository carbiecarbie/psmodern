// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_windows.h"
#include "app/retro_cartridge.h"
#include <algorithm>

namespace psmodern::app::retro {
namespace {
struct WindowChange { unsigned begin, end; Window kind; };
constexpr WindowChange windows[]{
    {0x2eac,0x2ecd,Window::actor},{0x2ed9,0x30b7,Window::party},
    {0x30c3,0x30e1,Window::commands},{0x30ed,0x319e,Window::enemy},
    {0x3478,0x34c9,Window::magic},{0x3521,0x3656,Window::items},
    {0x3665,0x36bb,Window::member},{0x3682,0x36cc,Window::battle_member},
    {0x36dd,0x36fb,Window::field},{0x3707,0x374d,Window::equipment},
    {0x3759,0x376b,Window::actions},{0x3777,0x3797,Window::buy_sell},
    {0x37a3,0x37c3,Window::question},{0x37cf,0x38c1,Window::status},
    {0x38cd,0x3999,Window::shop},{0x39a5,0x39dd,Window::saves},
    {0x39e9,0x3a12,Window::money},{0x3a21,0x3a46,Window::choice}
};
unsigned word(const Machine& m, unsigned address) {
    return m.peek(static_cast<std::uint16_t>(address)) |
           (m.peek(static_cast<std::uint16_t>(address+1))<<8);
}
}

void WindowRows::wrote(unsigned row) {
    auto& progress=restoring ? restored : drawn;
    progress=std::max(progress,std::min(total,row+1));
}

bool WindowState::include_early_dialogue_row(const WindowState& latest, std::uint64_t completed_cycle) {
    // The greeting's first row may already be on the screen even though its
    // writer began after the start-of-picture snapshot.
    if (latest.early_dialogue_cycle_ && latest.early_dialogue_cycle_ <= completed_cycle &&
        completed_cycle - latest.early_dialogue_cycle_ < Machine::frame_cycles) {
        const bool starting=!dialogue_active || dialogue_rows.restoring || !dialogue_rows.visible();
        if (starting) dialogue_rows={6};
        dialogue_active=latest.dialogue_active;
        shop_dialogue=latest.shop_dialogue;
        dialogue_rows.wrote(0);
        return starting;
    }
    return false;
}

void WindowState::capture_window_arrows(const Cartridge& cart,std::span<const std::uint8_t,64*24> table) {
    // The selector can return before restoration erases the blinking tile.
    // Read the last drawn cell in the name table paired with this picture.
    const auto base=cart.word(0xc269);
    const auto row=cart.byte(0xc26c);
    const auto tile=cart.byte(cart.byte(0xc202)==3 ? 0x2e73 : 0x2e6e);
    const auto offset=(base+row*128)&0x7ff;
    const bool visible=offset<table.size() && table[offset]==tile;
    question_arrow=visible && base==cart.word(0x37b6) && row<2 ? int(row) : -1;
    stock_arrow=stock_rows.visible() && visible && base==cart.word(0x3923) && row<cart.shop_count() ? int(row) : -1;
    save_arrow=visible && base==cart.word(0x39c8) && row<save_names.size() ? int(row) : -1;
}

void WindowState::observe_dialogue(const Machine& m, unsigned pc) {
    const auto sp=m.cpu().sp.uint16_value;
    switch (pc) {
    case 0x31d4:
        if (!m.peek(0xc2d3)) {
            dialogue_active=true;
            shop_dialogue=word(m,sp)==0x2bc6;
            dialogue_rows={6};
            early_dialogue_cycle_=0;
        }
        return;
    case 0x3464:
        dialogue_rows.restoring=true;
        if (!m.peek(0xc2d3)) dialogue_active=shop_dialogue=false;
        return;
    case 0x37a3: question_rows={5}; return;
    case 0x37c3: question_rows.restoring=true; return;
    case 0x38cd: stock_rows={8}; return;
    case 0x3999: stock_rows.restoring=true; return;
    case 0x39e9: money_rows={3}; return;
    case 0x3a12: money_rows.restoring=true; return;
    default: break;
    }
    if (pc!=0x3a79 && pc!=0x3632) return;

    // After a write DE points to the next line. Other windows can cover
    // the same cells, so validate the writer as well as the destination.
    const auto dest=(m.cpu().de.uint16_value-0x40u)&0x3fffu;
    const auto src=m.cpu().hl.uint16_value;
    auto record=[&](WindowRows& rows,unsigned origin,auto owns) {
        const auto base=word(m,origin)&0x3fffu;
        if (dest<base || (dest-base)%64) return false;
        const auto row=(dest-base)/64;
        if (row>=rows.total || !owns(row)) return false;
        rows.wrote(row);
        return true;
    };
    if (pc==0x3632) {
        const auto caller=word(m,sp);
        if (caller==0x390b || caller==0x3912)
            record(stock_rows,0x38dd,[](unsigned){return true;});
        if (caller==0x3a07 && record(money_rows,0x39ee,[](unsigned row){return row==1;}))
            displayed_meseta=word(m,0xc4e0);
        return;
    }
    if (word(m,sp+2)!=0x3a63) return;
    auto ends_at=[&](unsigned operand,unsigned bytes) {return src==word(m,operand)+bytes;};
    if (dialogue_active && record(dialogue_rows,0x31e5,[&](unsigned row) {
            return ends_at(dialogue_rows.restoring ? 0x346d : 0x31ee,40*(row+1));
        })) {
        const auto y=(dest-(word(m,0x31e5)&0x3800u))/64*8;
        if (!dialogue_rows.restoring && dialogue_rows.drawn==1 && m.vdp().v_counter()<y)
            early_dialogue_cycle_=m.cycles();
        if (dialogue_rows.restoring && !dialogue_visible()) dialogue_active=shop_dialogue=false;
        return;
    }
    record(question_rows,0x37a7,[&](unsigned row) {
        return ends_at(question_rows.restoring ? 0x37c4 : 0x37b0,10*(row+1));
    });
    record(stock_rows,0x38dd,[&](unsigned row) {
        if (stock_rows.restoring) return ends_at(0x399a,32*(row+1));
        return (row==0 && ends_at(0x38da,32)) || (row==7 && ends_at(0x391a,32));
    });
    record(money_rows,0x39ee,[&](unsigned row) {
        if (money_rows.restoring) return ends_at(0x3a14,20*(row+1));
        return (row==0 && ends_at(0x39f9,20)) || (row==2 && ends_at(0x3a08,20));
    });
}

bool WindowState::battle(const Cartridge& cart) const {
    if (has(Window::enemy) || has(Window::commands) || has(Window::actor)) return true;
    const auto mode=cart.byte(0xc202)&31;
    return cart.byte(0xc29d)!=0 && (mode==9 || mode==11 || mode==13);
}
void WindowState::observe_field(const Machine& m, unsigned pc) {
    if (pc==0x36dd) field_rows=WindowRows{11};
    if (!field_rows) return;
    if (pc==0x36fb) field_rows->restoring=true;
    if (pc!=0x3a79) return;

    // Track the command rectangle's own writes. The native member picker
    // overlaps columns within the parent. Enhanced places it beside the
    // parent, so its writes must never hide rows of the command list.
    const auto stack=m.cpu().sp.uint16_value;
    if (word(m,stack+2)!=0x3a63) return;
    const auto base=word(m,0x36e1);
    const auto address=m.cpu().de.uint16_value-0x40u;
    if (address<base || (address-base)%64 || (address-base)/64>=field_rows->total) return;
    field_rows->wrote((address-base)/64);
    if (field_rows->restoring && !field_rows->visible()) field_rows.reset();
}

void WindowState::include_early_menu_rows(const WindowState& latest,std::uint64_t completed_cycle) {
    const auto appeared=menus_.include_early_rows(latest.menus_,completed_cycle);
    const auto first_row=[&](Window w) { return appeared.test(static_cast<unsigned>(w)); };
    if (first_row(Window::magic)) {
        spell_group=latest.spell_group;
        spell_count=latest.spell_count;
        spell_row=latest.spell_row;
    }
    if (first_row(Window::member) || first_row(Window::battle_member)) {
        equipping=latest.equipping;
        spell_target=latest.spell_target;
        action_row=latest.action_row;
        spell_row=latest.spell_row;
    }
    if (first_row(Window::equipment)) equipment_member=latest.equipment_member;
    if (first_row(Window::status)) status_member=latest.status_member;
    if (first_row(Window::actions)) item_row=latest.item_row;
    if (first_row(Window::saves)) save_names=latest.save_names;
    if (!show(Window::member) && !show(Window::battle_member)) {
        equipping=false;
        spell_target=false;
    }
}

void WindowState::observe(const Machine& m,unsigned pc) {
    menus_.observe(m,pc);
    if (pc==0x06b4 || pc==0x072a) {
        open.reset(static_cast<unsigned>(Window::saves));
        return;
    }
    if (field_rows || pc==0x36dd) observe_field(m,pc);
    observe_dialogue(m,pc);
    const unsigned a=m.cpu().af.uint8_values.at_1;
    const auto row=m.peek(0xc26b);
    if (pc==0x3478) {
        spell_group=a%6;
        spell_count=std::min(5u,unsigned(m.cpu().bc.uint8_values.at_1));
        spell_row=0;
    }
    if (pc==0x3707) equipment_member=a&3;
    if (pc==0x37cf) status_member=a&3;
    if (pc==0x3759) {
        const auto slot=m.peek(0xc29b)|(m.peek(0xc29c)<<8);
        item_row=static_cast<unsigned>(slot-0xc4c0)&7;
        action_row=0;
    }
    if (pc==0x3665 || pc==0x3682) {
        if (has(Window::actions)) action_row=row;
        spell_target=has(Window::magic);
        if (spell_target) spell_row=row;
        const auto sp=m.cpu().sp.uint16_value;
        equipping=pc==0x3665 && m.peek(sp)==0xf1 && m.peek(static_cast<std::uint16_t>(sp+1))==0x26;
        if (!m.peek(0xc4f0)) return; // the native helper returns the only member
    }
    if ((pc==0x36bb || pc==0x36cc || pc==0x3a79) &&
        !motion(Window::member) && !motion(Window::battle_member)) {
        equipping=false;
        spell_target=false;
    }
    if (pc==0x38d9) shop_browse=true;
    if (pc==0x392b) { shop_row=a; shop_browse=false; }
    if (pc==0x39a5 || pc==0x39b1) {
        open.set(static_cast<unsigned>(Window::saves));
        const auto& sram=m.memory().cartridge_ram();
        for (unsigned i=0;i<5;++i) {
            auto& name=save_names[i];
            name.clear();
            if (!sram[0x201+i]) continue;
            for (unsigned j=0;j<5;++j) {
                const auto top=sram[0x118+36*i+2*j];
                const auto bottom=sram[0x12a+36*i+2*j];
                char c=' ';
                for (unsigned code=32;code<=0x5a;++code) {
                    const auto pair=static_cast<std::uint16_t>(0x7d17+2*(code-32));
                    if (m.peek(pair)==top && m.peek(pair+1)==bottom) {
                        c=cartridge_character(code); break;
                    }
                }
                name+=c;
            }
            while (!name.empty() && name.back()==' ') name.pop_back();
        }
    }
    if (pc==0x2f3c) pc=0x2ed9;
    if (pc==0x30d5) pc=0x30c3;
    if (pc==0x36ef) pc=0x36dd;
    for (const auto& w:windows) {
        if (pc==w.begin || pc==w.end) {
            open.set(static_cast<unsigned>(w.kind),pc==w.begin);
            return;
        }
    }
}
}
