// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <bitset>
#include <cstdint>
#include <optional>

namespace psmodern { class Machine; }

namespace psmodern::app::retro {
enum class Window : unsigned {
    actor, party, commands, enemy, magic, items, member, battle_member,
    field, equipment, actions, buy_sell, question, status, shop, saves, money, choice, count
};
struct WindowRows {
    unsigned total, drawn=0, restored=0;
    bool restoring=false;
    bool visible() const { return restored < drawn; }
    void wrote(unsigned row);
};

struct InventoryRedraw {
    std::array<unsigned,8> previous_items{};
    unsigned previous_first=0, previous_count=0, names_written=0;
    bool header_written=false;
};

// Passive record of rows written in each window's native rectangle.
class MenuMotion {
public:
    const WindowRows* rows(Window window) const;
    const InventoryRedraw* inventory_redraw() const { return redraw_ ? &*redraw_ : nullptr; }
    bool visible() const;
    void observe(const Machine& machine,unsigned pc);
    std::bitset<static_cast<unsigned>(Window::count)> include_early_rows(const MenuMotion& latest,
                                                                      std::uint64_t completed_cycle);
private:
    struct Range {
        std::optional<WindowRows> rows;
        unsigned base=0, width=0;
        std::uint64_t early_picture=0;
        WindowRows early_rows{0};
    };
    std::array<Range,static_cast<unsigned>(Window::count)> ranges_;
    std::array<unsigned,8> inventory_items_{};
    unsigned inventory_first_=0, inventory_count_=0;
    std::optional<InventoryRedraw> redraw_;
    void begin_inventory_draw(const Machine& machine,Range& range);
    void wrote_inventory_row(unsigned row,Range& range);
};
}
