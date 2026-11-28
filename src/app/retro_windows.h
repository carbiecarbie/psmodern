// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/retro_menu_motion.h"
#include "machine/machine.h"
#include <array>
#include <bitset>
#include <optional>
#include <string>
#include <span>

namespace psmodern::app::retro {
class Cartridge;
struct WindowState {
    std::bitset<static_cast<unsigned>(Window::count)> open;
    bool has(Window w) const { return open[static_cast<unsigned>(w)]; }
    const WindowRows* motion(Window w) const { return menus_.rows(w); }
    const InventoryRedraw* inventory_redraw() const { return menus_.inventory_redraw(); }
    bool show(Window w) const {
        const auto* rows=motion(w);
        return rows ? rows->visible() : has(w);
    }
    bool menu_motion_visible() const { return menus_.visible(); }
    void include_early_menu_rows(const WindowState& latest,std::uint64_t completed_cycle);
    unsigned spell_group=0, spell_count=0, spell_row=0;
    unsigned status_member=0, equipment_member=0, item_row=0, action_row=0, shop_row=0;
    bool equipping=false, shop_browse=false, spell_target=false;
    std::array<std::string,5> save_names;
    std::optional<WindowRows> field_rows;
    bool dialogue_active=false, shop_dialogue=false;
    bool paced_dialogue() const { return dialogue_active; }
    WindowRows dialogue_rows{6}, question_rows{0}, stock_rows{0}, money_rows{0};
    unsigned displayed_meseta=0;
    int question_arrow=-1, stock_arrow=-1, save_arrow=-1;
    void capture_window_arrows(const Cartridge& cart,std::span<const std::uint8_t,64*24> table);
    bool dialogue_visible() const { return dialogue_rows.visible(); }
    bool include_early_dialogue_row(const WindowState& latest, std::uint64_t completed_cycle);

    bool battle(const Cartridge& cart) const;
    void observe(const Machine& m, unsigned pc);
private:
    void observe_field(const Machine& m, unsigned pc);
    void observe_dialogue(const Machine& m, unsigned pc);
    MenuMotion menus_;
    std::uint64_t early_dialogue_cycle_=0;
};
}
