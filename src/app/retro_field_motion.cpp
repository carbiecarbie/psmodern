// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_field_motion.h"
#include "app/retro_cartridge.h"
#include <algorithm>

namespace psmodern::app::retro {
namespace {
constexpr unsigned stride=64;
constexpr unsigned window_bank=27;

class NameTable {
public:
    NameTable(const Cartridge& data, std::span<const std::uint8_t,64*24> tiles) : cart(data), tiles(tiles) {}

    unsigned word(unsigned address) const {
        const auto offset=address&0x7ff;
        return tiles[offset] | (tiles[offset+1]<<8);
    }
    bool row_matches(unsigned address,unsigned source,unsigned bytes,bool has_cursor=false) const {
        for (unsigned offset=0;offset<bytes;offset+=2) {
            if (has_cursor && offset==0) continue;
            if (word(address+offset)!=cart.word(source+offset,window_bank)) return false;
        }
        return true;
    }
    bool sides_match(unsigned address,unsigned source,unsigned bytes) const {
        return word(address)==cart.word(source,window_bank) &&
               word(address+bytes-2)==cart.word(source+bytes-2,window_bank);
    }
    std::optional<unsigned> points(unsigned address,unsigned label,unsigned border) const {
        // 00:2FE1 writes the native four-tile label, three digits and the
        // right edge, then advances to the next row. The reader sees the
        // photographed number even if the member's RAM has already changed.
        for (unsigned offset=0;offset<8;offset+=2)
            if (word(address+offset)!=cart.word(label+offset)) return {};
        if (word(address+14)!=cart.word(border+14,window_bank)) return {};
        const auto zero=cart.byte(0x364c);
        const auto blank=cart.byte(0x2ff0);
        unsigned value=0;
        for (unsigned offset=8;offset<14;offset+=2) {
            const auto tile=word(address+offset)&255;
            if (tile==blank && !value && offset<12) continue;
            if (tile<zero || tile>zero+9) return {};
            value=10*value+tile-zero;
        }
        return value;
    }
private:
    const Cartridge& cart;
    std::span<const std::uint8_t,64*24> tiles;
};
}

FieldMotion::FieldMotion(const Cartridge& data,std::span<const std::uint8_t,64*24> name_table,bool in_battle)
    : active(true) {
    const NameTable shown(data,name_table);
    const auto footer=data.word(0x2f34);
    constexpr std::array<unsigned,4> members{0x2ee6,0x2ef3,0x2f00,0x2f0d};
    for (unsigned member=0;member<members.size();++member) {
        const auto source=data.word(members[member]);
        const auto base=data.word(members[member]+3);
        for (unsigned row=0;row<3;++row)
            if (shown.row_matches(base+stride*row,source+16*row,16)) cards[member]|=1u<<row;
        if (const auto hp=shown.points(base+3*stride,data.word(0x2f22),source+16)) {
            cards[member]|=1u<<3;
            party_hp[member]=*hp;
        }
        if (const auto mp=shown.points(base+4*stride,data.word(0x2f2b),source+16)) {
            cards[member]|=1u<<4;
            party_mp[member]=*mp;
        }
        if (shown.row_matches(base+5*stride,footer,16)) cards[member]|=1u<<5;
    }
    if (!in_battle) return;

    const auto actor_source=data.word(0x2ec3);
    const auto actor_base=data.word(0x2ed1);
    if (shown.row_matches(actor_base,actor_source,12)) actor_rows|=1;
    if (shown.row_matches(actor_base+2*stride,actor_source+24,12)) actor_rows|=4;
    for (unsigned member=0;member<members.size();++member)
        if (shown.row_matches(actor_base+stride,actor_source+36*member+12,12)) {
            actor_rows|=2;
            actor_member=member;
            break;
        }

    const auto command_source=data.word(0x30d6);
    const auto command_base=data.word(0x30d9);
    const auto arrow=data.byte(data.byte(0xc202)==3 ? 0x2e73 : 0x2e6e);
    for (unsigned row=0;row<11;++row) {
        const auto address=command_base+stride*row;
        const bool selectable=row%2!=0;
        if (!shown.row_matches(address,command_source+12*row,12,selectable)) continue;
        command_rows|=1u<<row;
        if (selectable && (shown.word(address)&255)==arrow) command_arrow=(row-1)/2;
    }

    const auto enemy_base=data.word(0x3109);
    const auto enemy_top=data.word(0x3106);
    if (shown.row_matches(enemy_base,enemy_top,20)) enemy_rows|=1;
    for (unsigned row=1;row<3;++row)
        if (shown.sides_match(enemy_base+stride*row,enemy_top+40,20)) enemy_rows|=1u<<row;
    if (shown.row_matches(enemy_base+3*stride,data.word(0x311f),20)) enemy_rows|=8;

    const auto stat_base=data.word(0x3131);
    const auto stat_top=data.word(0x312e);
    if (shown.row_matches(stat_base,stat_top,16)) enemy_stat_rows|=1;
    const auto count=std::min(8u,data.byte(0xc2c7));
    for (unsigned i=0;i<count;++i) {
        if (const auto hp=shown.points(stat_base+stride*(i+1),data.word(0x3143),stat_top+16)) {
            enemy_stat_rows|=1u<<(i+1);
            enemy_hp[i]=*hp;
        }
    }
    if (shown.row_matches(stat_base+stride*(count+1),data.word(0x3154),16))
        enemy_stat_rows|=1u<<(count+1);
}

bool FieldMotion::cards_visible() const {
    return std::ranges::any_of(cards,[](unsigned rows) { return rows!=0; });
}
bool FieldMotion::visible() const {
    return cards_visible() || actor_rows || command_rows || enemy_rows || enemy_stat_rows;
}
}
