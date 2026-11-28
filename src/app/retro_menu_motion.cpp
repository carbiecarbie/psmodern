// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_menu_motion.h"
#include "machine/machine.h"
#include <algorithm>

namespace psmodern::app::retro {
namespace {
struct Menu {
    Window kind;
    unsigned open, close, destination, dimensions;
};

// Operands of the instructions setting DE/BC for the saved rectangle.
constexpr Menu menus[]{
    {Window::magic,0x3478,0x34c9,0x347e,0x3481},
    {Window::items,0x3521,0x3656,0x3525,0x3528},
    {Window::member,0x3665,0x36bb,0x366e,0x3671},
    {Window::battle_member,0x3682,0x36cc,0x368b,0x368e},
    {Window::equipment,0x3707,0x374d,0x370c,0x370f},
    {Window::actions,0x3759,0x376b,0x375d,0x3760},
    {Window::buy_sell,0x3777,0x3797,0x377b,0x377e},
    {Window::status,0x37cf,0x38c1,0x37e2,0x37e5},
    {Window::saves,0x39a5,0x39dd,0x39a9,0x39ac},
    {Window::choice,0x3a21,0x3a46,0x3a25,0x3a28}
};

unsigned word(const Machine& machine,unsigned address) {
    return machine.peek(static_cast<std::uint16_t>(address)) |
           (machine.peek(static_cast<std::uint16_t>(address+1))<<8);
}

bool member_picker(Window kind) {
    return kind==Window::member || kind==Window::battle_member;
}

// The inventory and equipment names, and the status values, use their own
// printers. Their caller identifies the window even when two rectangles share
// a column. DE has already advanced to the next row before their frame wait.
Window printed_line(unsigned pc,unsigned caller) {
    if (pc==0x35b6) {
        if (caller==0x3559 || caller==0x355e) return Window::items;
        if (caller==0x373a || caller==0x373f) return Window::equipment;
    }
    if (pc==0x3632) {
        if (caller==0x353c) return Window::items;
        if (caller==0x385c) return Window::status;
    }
    if (pc==0x30a0 && caller==0x3808) return Window::status;
    if (pc==0x302f && (caller==0x37fc || caller==0x381a || caller==0x382c ||
                      caller==0x383e || caller==0x3850)) return Window::status;
    return Window::count;
}
}

const WindowRows* MenuMotion::rows(Window window) const {
    const auto& range=ranges_[static_cast<unsigned>(window)];
    return range.rows ? &*range.rows : nullptr;
}

bool MenuMotion::visible() const {
    return std::ranges::any_of(ranges_,[](const auto& range) { return range.rows && range.rows->visible(); });
}

std::bitset<static_cast<unsigned>(Window::count)> MenuMotion::include_early_rows(
    const MenuMotion& latest,std::uint64_t completed_cycle) {
    std::bitset<static_cast<unsigned>(Window::count)> appeared;
    const auto picture=completed_cycle/Machine::frame_cycles;
    if (!picture) return appeared;
    for (const auto& menu:menus) {
        const auto index=static_cast<unsigned>(menu.kind);
        const auto& incoming=latest.ranges_[index];
        if (incoming.early_picture!=picture) continue;
        auto& shown=ranges_[index].rows;
        appeared.set(index,incoming.early_rows.visible() && (!shown || !shown->visible()));
        if (incoming.early_rows.visible()) shown=incoming.early_rows;
        else shown.reset();
    }
    return appeared;
}

void MenuMotion::begin_inventory_draw(const Machine& m,Range& range) {
    if (!range.rows || range.rows->restoring) return;
    const auto first=std::min(16u,static_cast<unsigned>(m.peek(0xc299)));
    const auto count=std::min(24u,static_cast<unsigned>(m.peek(0xc4e2)));
    std::array<unsigned,8> items{};
    for (unsigned i=0;i<items.size() && first+i<count;++i)
        items[i]=m.peek(static_cast<std::uint16_t>(0xc4c0+first+i));
    if (range.rows->drawn==range.rows->total &&
        (first!=inventory_first_ || count!=inventory_count_ || items!=inventory_items_))
        redraw_=InventoryRedraw{inventory_items_,inventory_first_,inventory_count_};
    inventory_first_=first;
    inventory_count_=count;
    inventory_items_=items;
}

void MenuMotion::wrote_inventory_row(unsigned row,Range& range) {
    if (!redraw_ || range.rows->restoring) return;
    const unsigned first_name=inventory_count_>8 ? 4 : 2;
    redraw_->header_written|=row+1>=first_name;
    if (row>=first_name)
        redraw_->names_written=std::min(8u,(row-first_name+1)/2);
    // Eight two-row names and the footer follow the budget and page arrows.
    if (row==first_name+16) {
        range.rows->total=range.rows->drawn=row+1;
        redraw_.reset();
    }
}

void MenuMotion::observe(const Machine& m,unsigned pc) {
    if (pc==0x06b4 || pc==0x072a) {
        // Continue and Delete leave the whole scene without restoring the
        // saved rectangle used by the field Save command.
        ranges_[static_cast<unsigned>(Window::saves)]={};
        return;
    }
    if (pc==0x352d) {
        begin_inventory_draw(m,ranges_[static_cast<unsigned>(Window::items)]);
        return;
    }
    if (pc==0x3a79 || pc==0x35b6 || pc==0x302f || pc==0x30a0 || pc==0x3632) {
        const auto address=(m.cpu().de.uint16_value-0x40u)&0x3fffu;
        const auto sp=m.cpu().sp.uint16_value;
        const bool rectangle=pc==0x3a79;
        const auto printer=rectangle ? Window::count : printed_line(pc,word(m,sp));
        if (!rectangle && printer==Window::count) return;
        const unsigned bytes=rectangle ? m.peek(sp) : 0u;
        for (const auto& menu:menus) {
            auto& range=ranges_[static_cast<unsigned>(menu.kind)];
            if (!range.rows || address<range.base || (address-range.base)%64) continue;
            const auto row=(address-range.base)/64;
            const auto total=(menu.kind==Window::items && redraw_) ?
                (inventory_count_>8 ? 21u : 19u) : range.rows->total;
            if (row>=total) continue;
            if (rectangle ? bytes!=range.width : menu.kind!=printer) continue;
            range.rows->wrote(row);
            if (menu.kind==Window::items) wrote_inventory_row(row,range);
            // A writer can pass the start of a picture while saving the
            // background, then finish before the VDP scans its first row.
            // Keep just the progress that made it onto that picture.
            if (m.vdp().v_counter()<(address&0x7ffu)/64*8) {
                range.early_picture=m.cycles()/Machine::frame_cycles+1;
                range.early_rows=*range.rows;
            }
            if (range.rows->restoring && !range.rows->visible()) range.rows.reset();
            return;
        }
        return;
    }

    for (const auto& menu:menus) {
        auto& range=ranges_[static_cast<unsigned>(menu.kind)];
        // Title Continue/Delete enter the same row printer without the
        // field command's save-under wrapper. A rejected slot redraws
        // the window in place, so its existing rows stay visible.
        const bool title_slots=menu.kind==Window::saves && pc==0x39b1 && !range.rows;
        if (pc==menu.open || title_slots) {
            const auto party=m.peek(0xc4f0)+1u;
            if (member_picker(menu.kind) && party==1) return; // no native window for a lone member
            auto total=static_cast<unsigned>(m.peek(static_cast<std::uint16_t>(menu.dimensions+1)));
            if (member_picker(menu.kind)) total=std::min(total,2*party+1);
            if (menu.kind==Window::items && m.peek(0xc4e2)<=8) total-=2; // the page arrows occupy two rows
            if (menu.kind==Window::items) redraw_.reset();
            range={WindowRows{total},word(m,menu.destination)&0x3fffu,
                   m.peek(static_cast<std::uint16_t>(menu.dimensions))};
            return;
        }
        if (pc==menu.close && range.rows) {
            if (menu.kind==Window::items) redraw_.reset();
            range.rows->restoring=true;
            return;
        }
    }
}
}
