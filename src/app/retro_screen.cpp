// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_screen.h"

#include "app/retro_cartridge.h"
#include "app/retro_font.h"
#include "app/retro_helpers.h"
#include "app/retro_text.h"
#include "app/retro_windows.h"
#include <algorithm>
#include <array>

namespace psmodern::app::retro {
namespace {
constexpr std::uint32_t red = 0xffff6b6b, green = 0xff55ee77, dim = 0xff646a78;
using std::to_string;

class Screen {
public:
    Screen(std::span<std::uint32_t> pixels, const Cartridge& data, const WindowState& state,
           const TextPage& text_page, Language language,const FieldMotion& motion)
        : px(pixels), cart(data), w(state), page(text_page), lang(language), field(motion),
          battle(!page.alternate && w.battle(cart)), cursor(cart.byte(0xc26b)),
          party_size(std::min(4u, cart.byte(0xc4f0) + 1)),
          // The US cartridge uses a special tile for H in its HP counter.
          br(cart.byte(0x30a9) != 0xf4) {}

    void draw() const {
        const bool cards_visible=field.active ? field.cards_visible() :
            w.has(Window::party) || (battle && (w.has(Window::enemy) ||
                                               w.has(Window::commands) || !page.lines.empty()));
        if (cards_visible && !page.alternate) party_strip(battle);
        if (battle) {
            if (field.active ? field.enemy_rows || field.enemy_stat_rows : w.has(Window::enemy)) enemies();
        }
        // Keep live parents underneath their children. A partly opened or
        // restored picker exposes the parent's remaining rows on this frame.
        if (battle && (field.active ? field.actor_rows!=0 : w.has(Window::actor))) actor_box();
        if (battle && (field.active ? field.command_rows!=0 : w.has(Window::commands))) commands(true);
        if (w.has(Window::field) || w.field_rows) commands(false);
        const bool foreground_picker=targeting();
        const bool second_picker=w.show(Window::battle_member);
        if (w.show(Window::member) && (!foreground_picker || second_picker))
            members(Window::member,false);
        if (w.show(Window::items)) inventory();
        if (w.stock_rows.total ? w.stock_rows.visible() : w.has(Window::shop)) shop();
        if (w.show(Window::status) || w.show(Window::equipment)) status();
        else if (w.show(Window::magic)) magic();
        if (w.money_rows.total ? w.money_rows.visible() : w.has(Window::money))
            money(w.shop_dialogue || w.has(Window::shop));
        if (foreground_picker) members(second_picker ? Window::battle_member : Window::member,true);
        if (w.show(Window::saves)) saves();

        // On shop entry the greeting stays in the native dialogue buffer
        // while the shop takes input. Browse help occupies that area.
        const bool dialogue_open=w.paced_dialogue() && !page.alternate ? w.dialogue_visible() : !page.lines.empty();
        if (dialogue_open && !browsing_stock()) dialogue();
        if (w.show(Window::buy_sell)) choices(0x3784, 6, 2, 27);
        if (w.show(Window::choice)) choices(0x3a33, 8, 3, 18);
        if (w.question_rows.total ? w.question_rows.visible() : w.has(Window::question))
            choices(0x37b0, 5, 2, 27, w.question_rows.total ? &w.question_rows : nullptr);
    }

private:
    std::span<std::uint32_t> px;
    const Cartridge& cart;
    const WindowState& w;
    const TextPage& page;
    Language lang;
    const FieldMotion& field;
    bool battle;
    unsigned cursor, party_size;
    bool br;

    std::string_view local(std::string_view en, std::string_view pt) const {
        return lang == Language::portuguese ? pt : en;
    }
    bool targeting() const {
        // Field magic and status leave the caster picker open underneath.
        // A healing spell opens a second picker for its recipient.
        if (w.show(Window::equipment) || w.show(Window::status)) return false;
        return w.show(Window::battle_member) || (w.show(Window::member) && (!w.show(Window::magic) || w.spell_target));
    }
    unsigned stat(unsigned member, unsigned offset) const { return cart.byte(0xc400 + 16 * member + offset); }
    unsigned actor() const { return std::min(3u, cart.byte(0xc267)); }
    std::string_view hp() const { return br ? "PF" : "HP"; }
    std::string_view mp() const { return br ? "PM" : "MP"; }
    std::string_view attack() const { return br ? "ATQ" : "ATK"; }
    void panel(int x, int y, int wd, int ht, std::uint32_t color = white) const { box(px, x, y, wd, ht, color); }
    void label(int x, int y, std::string_view s, std::uint32_t color = white, Align align = Align::left) const {
        text(px, x, y, s, color, align);
    }
    void value(int right, int y, std::string_view s, std::uint32_t color = white) const {
        label(right, y, s, color, Align::right);
    }
    void marked(int x, int y, bool selected, bool active = true) const {
        if (!selected) return;
        if (!active) label(x, y, "·", cyan);
        else if (cart.byte(0xc268) && (cart.byte(0xc26d) & 8)) label(x, y, "▸", gold);
    }
    // Render the final geometry, then composite only the band whose native
    // rows have been written and have yet to be restored.
    template<class Draw>
    void clipped(const WindowRows& rows,int y,int h,Draw draw,std::span<const int> limits={},
                 int left=0,int right=width) const {
        if (!rows.visible()) return;
        if (!rows.restored && rows.drawn==rows.total && left==0 && right==width) {
            draw(*this);
            return;
        }
        const auto limit=[&](unsigned row) {
            return y + (limits.empty() ? h*static_cast<int>(row)/static_cast<int>(rows.total) : limits[row]);
        };
        std::array<std::uint32_t,width*height> scratch{};
        auto layer=*this;
        layer.px=scratch;
        draw(layer);
        for (int line=limit(rows.restored),end=limit(rows.drawn);line<end;++line) {
            for (int i=line*width+left,last=line*width+right;i<last;++i)
                if (scratch[i]) px[i]=scratch[i];
        }
    }

    template<class Draw>
    void field_rows(unsigned mask,int y,std::span<const int> edges,Draw paint) const {
        const auto count=static_cast<unsigned>(edges.size()-1);
        for(unsigned row=0;row<count;) {
            if (!(mask&(1u<<row))) { ++row; continue; }
            const auto first=row;
            while (++row<count && (mask&(1u<<row))) {}
            clipped(WindowRows{count,row,first},y,edges.back(),paint,edges);
        }
    }

    template<class Draw>
    void animate(Window window,int top,int height,Draw draw) const {
        if (const auto* rows=w.motion(window)) clipped(*rows,top,height,draw);
        else draw(*this);
    }

    // Native list labels use one inked row for every two rows written.
    // Place the complete proportional label in that same step; the blank
    // row advances through the space below it. Preserve header and footer.
    template<class Draw>
    void list_motion(Window window,int top,int height,unsigned first_row,int baseline,
                     int step,unsigned count,Draw draw) const {
        const auto* rows=w.motion(window);
        if (!rows || !count || !rows->visible() || (rows->drawn==rows->total && !rows->restored)) {
            animate(window,top,height,draw);
            return;
        }
        const auto last=first_row+2*count-1;
        std::array<int,25> edges{};
        if (last>=rows->total || rows->total>=edges.size()) {
            clipped(*rows,top,height,draw);
            return;
        }
        const int ink=baseline-top-2;
        for (unsigned n=1;n<=rows->total;++n) {
            if (n<=first_row) edges[n]=ink*int(n)/int(first_row);
            else if (n<=last) {
                const unsigned line=n-first_row;
                edges[n]=ink+step*int((line+1)/2)-int(line%2);
            } else edges[n]=edges[last]+(height-edges[last])*int(n-last)/int(rows->total-last);
        }
        clipped(*rows,top,height,draw,edges);
    }

    bool ready(Window window) const {
        const auto* rows=w.motion(window);
        return !rows || (rows->drawn==rows->total && !rows->restoring);
    }
    bool has_selector(Window window) const {
        return ready(window) && (!w.motion(window) || cart.byte(0xc268));
    }

    bool browsing_stock() const {
        if (!w.has(Window::shop) || !w.shop_browse) return false;
        return !w.stock_rows.total || (w.stock_rows.visible() && cart.byte(0xc268)!=0);
    }

    static std::uint32_t row_color(bool selected, bool active) {
        return selected ? (active ? gold : cyan) : white;
    }

    // These lists contain two tile rows per label, with the visible letters
    // in the upper row. Spell names use a separate layout in Cartridge.
    std::string native_row(unsigned operand, unsigned columns, unsigned index, unsigned bank = 27) const {
        const auto offset = 2 * columns * (1 + 2 * index);
        return cart.tile_row(cart.word(operand) + offset + 2, columns - 2, bank);
    }
    void list_rows(unsigned operand, unsigned columns, unsigned rows, unsigned bank, int x, int y,
                   int step, bool active = true) const {
        for (unsigned i = 0; i < rows; ++i) {
            const int line = y + static_cast<int>(i) * step;
            marked(x, line, active && cursor == i);
            label(x + 7, line, native_row(operand, columns, i, bank), row_color(active && cursor == i, true));
        }
    }

    void party_strip(bool select_actor) const {
        const auto count=field.active ? static_cast<unsigned>(field.cards.size()) : party_size;
        for (unsigned i = 0; i < count; ++i) {
            const int x = 4 + 62 * static_cast<int>(i);
            const bool selected = select_actor && i == (targeting() ? cursor : cart.byte(0xc267));
            const auto color = selected ? gold : white;
            const auto health=field.active ? field.party_hp[i] : stat(i, 1);
            const auto magic_points=field.active ? field.party_mp[i] : stat(i, 2);
            const auto paint=[&](const Screen& layer) {
                layer.panel(x, 150, 60, 38, color);
                layer.label(x + 6, 156, cart.member_name(i), color);
                layer.label(x + 6, 167, hp(), grey);
                layer.value(x + 54, 167, to_string(health), health * 10 < stat(i, 6) * 3 ? red : white);
                layer.label(x + 6, 177, mp(), grey);
                layer.value(x + 54, 177, to_string(magic_points));
            };
            if (field.active) {
                // The native HP and MP rows own their entire proportional-font labels.
                constexpr std::array edges{0,5,13,15,25,36,38};
                field_rows(field.cards[i],150,edges,paint);
            } else paint(*this);
        }
    }
    void enemies() const {
        const auto count = std::min(8u, cart.byte(0xc2c7));
        const auto name=[&](const Screen& layer) {
            layer.panel(100, 4, 94, 22);
            layer.label(107, 11, cart.enemy_name());
            if (count > 1) layer.value(188, 11, "×" + to_string(count), grey);
        };
        if (field.active) {
            constexpr std::array edges{0,4,6,17,22};
            field_rows(field.enemy_rows,4,edges,name);
        } else name(*this);
        // The cartridge hides the HP column in its final encounter.
        if (!count || (field.active ? !field.enemy_stat_rows : cart.byte(0xc2e6)==0x49)) return;
        const int height=10+9*static_cast<int>(count);
        const auto stats=[&](const Screen& layer) {
            layer.panel(198, 4, 54, height);
            for (unsigned i = 0; i < count; ++i) {
                const auto points = field.active ? field.enemy_hp[i] : cart.byte(0xc441 + 16 * i);
                const int y = 10 + 9 * static_cast<int>(i);
                layer.label(204, y, hp(), grey);
                layer.value(246, y, to_string(points), points <= 10 ? gold : white);
            }
        };
        if (field.active) {
            std::array<int,11> edges{};
            for (unsigned i=0;i<=count;++i) edges[i+1]=5+9*static_cast<int>(i);
            edges[count+2]=height;
            field_rows(field.enemy_stat_rows,4,std::span(edges).first(count+3),stats);
        } else stats(*this);
    }
    void actor_box() const {
        const auto paint=[&](const Screen& layer) {
            layer.panel(4, 4, 74, 22);
            layer.label(11, 11, cart.member_name(field.actor_member.value_or(actor())), gold);
        };
        if (field.active) {
            constexpr std::array edges{0,6,15,22};
            field_rows(field.actor_rows,4,edges,paint);
        } else paint(*this);
    }
    void commands(bool fight) const {
        const auto paint=[&](const Screen& layer) {
            layer.panel(4, 4, 74, fight ? 72 : 61);
            if (fight && (!field.active || field.actor_member)) {
                layer.label(11, 10, cart.member_name(field.actor_member.value_or(actor())), gold);
                divider(layer.px, 10, 19, 62);
            }
            const bool active=!targeting() && !w.show(Window::saves) && page.lines.empty() &&
                (!w.field_rows || cart.byte(0xc268));
            if (fight && field.active) {
                for (unsigned row=0;row<5;++row) {
                    const int y=23+10*static_cast<int>(row);
                    if (field.command_arrow==row) layer.label(10,y,"▸",gold);
                    const bool selected=active && cart.byte(0xc268) && cursor==row &&
                        cart.word(0xc269)==cart.word(0x30d9)+64;
                    layer.label(17,y,native_row(0x30d6,6,row),row_color(selected,true));
                }
            } else {
                layer.list_rows(fight ? 0x30d6 : 0x36f0, 6, 5, 27, 10, fight ? 23 : 11, 10, active);
            }
        };
        if (fight && field.active) {
            constexpr std::array edges{0,16,26,27,36,37,46,47,56,57,66,72};
            field_rows(field.command_rows,4,edges,paint);
        } else if (w.field_rows && !fight) {
            constexpr std::array edges{0,6,12,17,23,28,34,39,45,50,56,61};
            clipped(*w.field_rows,4,61,paint,edges);
        } else paint(*this);
    }
    std::string capacity() const { return to_string(cart.byte(0xc4e2)) + "/24"; }
    void money(bool shop_counter,bool item_counter=false) const {
        const bool paced=!item_counter && w.money_rows.total;
        const int x = shop_counter ? 170 : 164;
        const int ht = shop_counter ? 30 : 22;
        const auto paint=[&](const Screen& layer) {
            layer.panel(x, 4, 252 - x, ht);
            if (paced && w.money_rows.drawn<2) return;
            layer.label(x + 6, 10, "Meseta", grey);
            const bool after_purchase = shop_counter && w.has(Window::question);
            const auto meseta=paced ? w.displayed_meseta : cart.word(0xc4e0);
            layer.value(246, 10, to_string(meseta), after_purchase ? cyan : white);
            if (shop_counter) {
                layer.label(x + 6, 21, local("Bag", "Bolsa"), grey);
                layer.value(246, 21, capacity(), after_purchase ? cyan : white);
            }
        };
        const std::array limits{0,4,ht-4,ht};
        if (paced) clipped(w.money_rows, 4, ht, paint, limits);
        else paint(*this);
    }

    unsigned spell_row() const {
        return std::min(targeting() ? w.spell_row : cursor, w.spell_count ? w.spell_count - 1 : 0);
    }
    void magic() const {
        const unsigned count = std::min(w.spell_count, 5u);
        const unsigned caster = w.spell_group % 3 == 2 ? 3 : w.spell_group % 3;
        const int wd = battle ? 92 : 132;
        const int step = battle ? 10 : 11;
        list_motion(Window::magic,4,22+int(count)*step,2,23,step,count,[&](const Screen& layer) {
            layer.panel(4, 4, wd, 22 + static_cast<int>(count) * step);
            auto heading = cart.member_name(caster);
            if (!battle) heading += " · " + native_row(0x36f0, 6, 1);
            layer.label(10, 10, heading, gold);
            if (battle) layer.value(90, 10, std::string(mp()) + " " + to_string(stat(caster, 2)), cyan);
            divider(layer.px, 9, 19, wd - 10);
            for (unsigned i = 0; i < count; ++i) {
                const auto id = cart.spell_id(w.spell_group, i);
                const int y = 23 + static_cast<int>(i) * step;
                const bool selected = i == spell_row() && (targeting() || has_selector(Window::magic));
                layer.marked(9, y, selected, !targeting());
                layer.label(battle ? 15 : 17, y, cart.spell_name(w.spell_group, i), row_color(selected, !targeting()));
                auto cost = to_string(cart.spell_cost(id));
                if (!battle) cost += " " + std::string(mp());
                layer.value(battle ? 90 : 129, y, cost, selected ? gold : cyan);
            }
        });
        if (!battle && ready(Window::magic)) {
            panel(140, 4, 84, 30);
            label(146, 10, hp(), grey);
            value(218, 10, to_string(stat(caster, 1)) + "/" + to_string(stat(caster, 6)));
            label(146, 21, mp(), grey);
            value(218, 21, to_string(stat(caster, 2)) + "/" + to_string(stat(caster, 7)), cyan);
        }
        if (count && !targeting() && page.lines.empty() && has_selector(Window::magic)) {
            const auto id = cart.spell_id(w.spell_group, spell_row());
            panel(4, battle ? 124 : 158, 248, battle ? 22 : 30);
            label(12, battle ? 131 : 169, spell_help(id, lang, battle ? SpellScene::battle : SpellScene::field));
        }
    }

    std::string_view slot_label(unsigned slot) const {
        if (slot == 0) return local("Weapon", "Arma");
        if (slot == 1) return local("Armor", "Armadura");
        return local("Shield", "Escudo");
    }
    void status() const {
        if (w.show(Window::equipment)) {
            animate(Window::equipment,14,96,[&](const Screen& layer) {
                layer.panel(4, 14, 124, 96);
                layer.label(11, 21, native_row(0x3766, 5, 1), gold);
                divider(layer.px, 10, 30, 112);
                for (unsigned i = 0; i < 3; ++i) {
                    const int y = 35 + 24 * static_cast<int>(i);
                    layer.label(11, y, slot_label(i), grey);
                    layer.label(15, y + 10, cart.item_name(stat(w.equipment_member, 10 + i)));
                }
            });
        }
        if (w.show(Window::status)) {
            const auto m = w.status_member;
            animate(Window::status,14,118,[&](const Screen& layer) {
                layer.panel(132, 14, 120, 118);
                layer.label(139, 21, cart.member_name(m), gold);
                layer.value(245, 21, std::string(br ? "NV " : "LV ") + to_string(stat(m, 5)));
                divider(layer.px, 138, 30, 108);
                const std::array<std::string_view, 7> names{hp(), mp(), attack(), "DEF", br ? "PE" : "EXP",
                                                            br ? "PRÓX." : "NEXT", "Meseta"};
                const auto next = cart.next_experience(m);
                const std::array<std::string, 7> values{
                    to_string(stat(m, 1)) + " / " + to_string(stat(m, 6)),
                    to_string(stat(m, 2)) + " / " + to_string(stat(m, 7)),
                    to_string(stat(m, 8)), to_string(stat(m, 9)), to_string(cart.word(0xc403 + 16 * m)),
                    next ? to_string(*next) : "—", to_string(cart.word(0xc4e0))};
                for (unsigned i = 0; i < names.size(); ++i) {
                    const int y = 36 + static_cast<int>(i) * 13 + (i >= 4 ? 3 : 0);
                    if (i == 4) divider(layer.px, 138, y - 4, 108);
                    layer.label(139, y, names[i], i == 5 ? cyan : grey);
                    layer.value(245, y, values[i], i == 5 ? cyan : white);
                }
            });
        }
        // Status opens a read-only battle-spell list on its second press.
        if (w.show(Window::magic)) {
            const auto paint=[&](const Screen& layer) {
                layer.panel(4, 136, 248, 52);
                for (unsigned i = 0; i < std::min(w.spell_count, 5u); ++i) {
                    const int x = i < 3 ? 12 : 134, y = 145 + 12 * static_cast<int>(i % 3);
                    layer.label(x, y, cart.spell_name(w.spell_group, i));
                    layer.value(x + 108, y, to_string(cart.spell_cost(cart.spell_id(w.spell_group, i))) +
                        " " + std::string(mp()), cyan);
                }
            };
            const auto* rows=w.motion(Window::magic);
            if (!rows || (!rows->restored && rows->drawn==rows->total)) paint(*this);
            else {
                // The single native column becomes two. Spells four and
                // five occupy native rows eight and ten in the right half.
                constexpr std::array left{0,4,7,17,19,29,31,41,44,46,48,50,52};
                constexpr std::array right{0,4,4,4,4,4,4,4,7,17,19,50,52};
                clipped(*rows,136,52,paint,left,4,128);
                clipped(*rows,136,52,paint,right,128,252);
            }
        }
    }

    unsigned item_slot() const {
        if (w.show(Window::actions) || targeting()) return w.item_row;
        return cart.byte(0xc4e2) > 8 && cursor ? cursor - 1 : cursor;
    }
    unsigned item_id() const {
        const auto slot = cart.byte(0xc299) + item_slot();
        return slot < std::min(24u, cart.byte(0xc4e2)) ? cart.byte(0xc4c0 + slot) : 0;
    }
    void inventory() const {
        const unsigned count = std::min(24u, cart.byte(0xc4e2));
        const unsigned first = std::min(16u, cart.byte(0xc299));
        const auto* redraw=w.inventory_redraw();
        const bool main = !w.show(Window::actions) && !targeting();
        const bool page_focus = !redraw && count > 8 && !cursor && main && has_selector(Window::items);
        const int wd = battle ? 92 : 156;
        list_motion(Window::items,4,104,count>8 ? 5 : 3,23,10,8,[&](const Screen& layer) {
            layer.panel(4, 4, wd, 104);
            layer.label(11, 10, native_row(0x36f0, 6, 2), gold);
            const bool previous_header=redraw && !redraw->header_written;
            const unsigned page_first=previous_header ? redraw->previous_first : first;
            const unsigned page_count=previous_header ? redraw->previous_count : count;
            auto indicator = to_string(page_first / 8 + 1) + "/" + to_string(std::max(1u, (page_count + 7) / 8));
            if (!battle) indicator = to_string(page_count) + "/24 · " + indicator;
            layer.value(battle ? 89 : 153, 10, indicator, page_focus ? gold : grey);
            divider(layer.px, 10, 19, wd - 12);
            for (unsigned i=0;i<8;++i) {
                const auto id=redraw && i>=redraw->names_written ? redraw->previous_items[i] :
                              first+i<count ? cart.byte(0xc4c0+first+i) : 0;
                if (!id) continue;
                const int y = 23 + static_cast<int>(i) * 10;
                const bool selected = !redraw && !page_focus && i == item_slot() && (!main || has_selector(Window::items));
                layer.marked(10, y, selected, main);
                layer.label(17, y, cart.item_name(id), row_color(selected, main));
            }
        });
        const auto* items=w.motion(Window::items);
        if (!battle && (!items || (items->drawn>=2 && items->restored<2))) money(false,true);
        if (w.show(Window::actions)) {
            list_motion(Window::actions,30,42,1,37,10,3,[&](const Screen& layer) {
                layer.panel(164, 30, 64, 42);
                const auto selected = targeting() ? w.action_row : cursor;
                for (unsigned i = 0; i < 3; ++i) {
                    const int y = 37 + static_cast<int>(i) * 10;
                    layer.marked(170,y,selected==i && (targeting() || has_selector(Window::actions)),!targeting());
                    layer.label(177, y, native_row(0x3766, 5, i), row_color(selected==i && (targeting() || has_selector(Window::actions)),!targeting()));
                }
            });
        }
        if (redraw || !page.lines.empty() || !ready(Window::items) || !ready(Window::actions) ||
            !ready(Window::member) || !ready(Window::battle_member)) return;
        if (page_focus) {
            panel(4, battle ? 124 : 158, 248, battle ? 22 : 30);
            label(12, battle ? 131 : 169, local("Left/Right or confirm to change page.", "Esq./Dir. ou confirme para mudar de página."));
            return;
        }
        if (!item_id()) return;
        if (battle) {
            panel(4, 124, 248, 22);
            label(12, 131, item_help(item_id(), lang));
        } else {
            item_info(item_id(), w.equipping || (w.show(Window::actions) && !targeting() && cursor == 1));
        }
    }

    static std::string change(const Cartridge::Comparison& c) {
        if (!c.eligible) return "—";
        if (c.equipped) return "Eq.";
        const int delta = static_cast<int>(c.after) - static_cast<int>(c.before);
        return (delta >= 0 ? "+" : "") + to_string(delta);
    }
    static std::uint32_t change_color(const Cartridge::Comparison& c) {
        if (!c.eligible) return dim;
        if (c.equipped || c.after == c.before) return cyan;
        return c.after > c.before ? green : red;
    }
    void item_info(unsigned id, bool comparison) const {
        panel(4, 136, 248, 52);
        label(12, 143, cart.item_name(id), gold);
        const auto type = cart.compare(0, id);
        if (type.equipment) {
            const std::string stat_label(type.slot == 0 ? attack() : "DEF");
            value(244, 143, std::string(slot_label(type.slot)) + " · " + stat_label +
                " " + to_string(cart.byte(0x183a + id)), grey);
        }
        divider(px, 10, 153, 236);
        if (w.equipping) {
            const auto member = std::min(cursor, party_size - 1);
            const auto who = cart.member_name(member);
            const auto c = cart.compare(member, id);
            label(12, 159, local("Who will equip it?", "Quem vai equipar?"), grey);
            if (!c.eligible) label(12, 172, who + std::string(local(" cannot equip this.", " não pode equipar.")), red);
            else if (c.equipped) label(12, 172, who + std::string(local(" already has this equipped.", " já está com este item.")), cyan);
            else label(12, 172, who + " · " + std::string(c.slot == 0 ? attack() : "DEF") + " " +
                       to_string(c.before) + " → " + to_string(c.after) + " (" + change(c) + ")", change_color(c));
        } else if (comparison && type.equipment) {
            for (unsigned i = 0; i < party_size; ++i) {
                const auto c = cart.compare(i, id);
                const int x = 12 + 59 * static_cast<int>(i);
                label(x, 159, cart.member_name(i), c.eligible ? white : dim);
                label(x, 172, change(c), change_color(c));
            }
        } else label(12, 160, item_help(id, lang));
    }

    void shop() const {
        const auto count = cart.shop_count();
        const auto row = std::min(w.shop_browse ? cursor : w.shop_row, count - 1);
        const bool browsing=browsing_stock();
        const auto paint=[&](const Screen& layer) {
            layer.panel(4, 4, 162, 64);
            for (unsigned i = 0; i < count; ++i) {
                // The first native row of each item only clears its line.
                // Its next row writes both the visible name and the price.
                if (w.stock_rows.total && w.stock_rows.drawn<3+2*i) break;
                const auto entry = cart.shop_item(i);
                const int y = 11 + 10 * static_cast<int>(i);
                const bool selected=row==i && (!w.stock_rows.total || browsing);
                if (w.stock_rows.total) {
                    if (w.stock_arrow==int(i)) layer.label(10,y,"▸",gold);
                } else layer.marked(10,y,selected,browsing);
                layer.label(17, y, cart.item_name(entry.id), row_color(selected, browsing));
                layer.value(159, y, to_string(entry.price), row_color(selected, browsing));
            }
        };
        constexpr std::array limits{0,4,5,15,16,25,26,35,64};
        if (w.stock_rows.total) clipped(w.stock_rows, 4, 64, paint, limits);
        else paint(*this);
        if (browsing) item_info(cart.shop_item(row).id, true);
    }

    void members(Window picker,bool foreground) const {
        if (w.show(Window::items) && !battle) {
            list_motion(picker,76,56,1,83,11,party_size,[&](const Screen& layer) {
                layer.panel(164, 76, 88, 56);
                for (unsigned i = 0; i < party_size; ++i) {
                    const int y = 83 + 11 * static_cast<int>(i);
                    const auto c = cart.compare(i, item_id());
                    const bool selected=foreground && i==cursor && has_selector(picker);
                    layer.marked(170,y,selected);
                    layer.label(177, y, cart.member_name(i), selected ? gold : (w.equipping && !c.eligible ? dim : white));
                    if (w.equipping) layer.value(246, y, change(c), change_color(c));
                }
            });
            return;
        }
        const int x = battle ? 4 : 160;
        const bool spell_recipient=foreground && w.show(Window::magic);
        const int y = !battle && spell_recipient ? 40 : 4;
        list_motion(picker,y,22+10*int(party_size),1,y+19,10,party_size,[&](const Screen& layer) {
            layer.panel(x, y, 92, 22 + 10 * static_cast<int>(party_size));
            if (spell_recipient) {
                const auto index = std::min(w.spell_row, 4u);
                layer.label(x + 6, y + 6, cart.spell_name(w.spell_group, index), cyan);
                layer.value(x + 86, y + 6, to_string(cart.spell_cost(cart.spell_id(w.spell_group, index))) +
                    " " + std::string(mp()), cyan);
            } else layer.label(x + 6, y + 6, local("Who?", "Quem?"), grey);
            divider(layer.px, x + 5, y + 15, 82);
            for (unsigned i = 0; i < party_size; ++i) {
                const int line = y + 19 + static_cast<int>(i) * 10;
                layer.marked(x+5,line,foreground && cursor==i && has_selector(picker));
                layer.label(x + 12, line, cart.member_name(i), row_color(foreground && cursor==i && has_selector(picker),true));
            }
        });
        if (foreground && battle && page.lines.empty() && has_selector(picker)) {
            panel(4, 124, 248, 22);
            label(12, 131, local("Choose an ally.", "Escolha um aliado."));
        }
    }
    void dialogue() const {
        const bool short_box = !battle && page.lines.size() <= 3 && !page.alternate;
        const int top = short_box ? 136 : 114;
        const int ht = battle ? 33 : 188 - top;
        const auto paint=[&](const Screen& layer) {
            layer.panel(4, top, 248, ht);
            const auto rows = std::min<std::size_t>(page.lines.size(), battle ? 2 : 4);
            for (unsigned i = 0; i < rows; ++i)
                layer.label(battle ? 11 : 12, top + (battle ? 7 : 10) + static_cast<int>(i) * 12, page.lines[i]);
            if (page.more) layer.label(240, battle ? 136 : 178, "▼", gold);
        };
        if (w.paced_dialogue() && !page.alternate) clipped(w.dialogue_rows, top, ht, paint);
        else paint(*this);
    }
    void choices(unsigned operand, unsigned columns, unsigned count, unsigned bank,
                 const WindowRows* motion=nullptr) const {
        const int wd = count == 3 ? 66 : 54;
        // The choices stay above every dialogue that asks the question.
        const int ht = 10 + 11 * static_cast<int>(count);
        const int y = (battle || page.lines.size() > 3 || page.alternate ? 114 : 136) - 4 - ht;
        const auto paint=[&](const Screen& layer) {
            layer.panel(252 - wd, y, wd, ht);
            if (!motion) {
                const auto kind=operand==0x37b0 ? Window::question : count==3 ? Window::choice : Window::buy_sell;
                layer.list_rows(operand,columns,count,bank,258-wd,y+7,11,has_selector(kind));
                return;
            }
            for (unsigned i=0; i<count; ++i) {
                if (motion->drawn<2+2*i) break;
                const int line=y+7+static_cast<int>(i)*11;
                const bool selected=cursor==i && !motion->restoring && cart.byte(0xc268)!=0;
                if (w.question_arrow==int(i)) layer.label(258-wd,line,"▸",gold);
                layer.label(265-wd, line, native_row(operand, columns, i, bank), row_color(selected,true));
            }
        };
        constexpr std::array limits{0,5,15,16,26,32};
        if (motion) clipped(*motion,y,ht,paint,limits);
        else list_motion(count==3 ? Window::choice : Window::buy_sell,y,ht,1,y+7,11,count,paint);
    }
    void saves() const {
        const bool tracked=w.motion(Window::saves)!=nullptr;
        const bool active=has_selector(Window::saves) && (!tracked || cart.word(0xc269)==cart.word(0x39c8));
        list_motion(Window::saves,4,66,2,11,11,5,[&](const Screen& layer) {
            layer.panel(80, 4, 96, 66);
            for (unsigned i = 0; i < w.save_names.size(); ++i) {
                const int y = 11 + 11 * static_cast<int>(i);
                const bool selected=i==cursor && active;
                if (!tracked) layer.marked(86,y,selected);
                else if (w.save_arrow==int(i)) layer.label(86,y,"▸",gold);
                layer.label(93,y,to_string(i+1)+"  "+w.save_names[i],row_color(selected,true));
            }
        });
    }
};
} // namespace

void render(std::span<std::uint32_t> pixels, const Cartridge& cartridge, const WindowState& windows,
            const TextPage& page, Language language,const FieldMotion& field) {
    Screen(pixels, cartridge, windows, page, language,field).draw();
}
}
