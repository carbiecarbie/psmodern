// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_cartridge.h"
#include <algorithm>

namespace psmodern::app::retro {
char cartridge_character(unsigned code) {
    if (code==0x40) return '\'';
    return code>=0x20 && code<=0x5a ? static_cast<char>(code) : ' ';
}
unsigned Cartridge::byte(unsigned address, unsigned bank) const {
    if (address>0xffff) return 0;
    if (address>=0xc000) return ram_[address & 0x1fff];
    const auto index=address<0x8000 ? address : bank*0x4000+(address & 0x3fff);
    return index<rom_.size() ? rom_[index] : 0;
}
unsigned Cartridge::word(unsigned address, unsigned bank) const {
    return byte(address,bank) | (byte(address+1,bank)<<8);
}
std::string Cartridge::name(unsigned address, unsigned width, unsigned bank) const {
    std::string s;
    for (unsigned i=0;i<width;++i) {
        const auto c=byte(address+i,bank);
        if (c==0x65) break;
        s+=cartridge_character(c);
    }
    while (!s.empty() && s.back()==' ') s.pop_back();
    return s;
}
std::string Cartridge::member_name(unsigned member) const { return name(0x31bb+4*(member&3),4); }
std::string Cartridge::item_name(unsigned item) const { return name(word(0x3284)+8*item,8); }
std::string Cartridge::enemy_name() const { return name(0xc2c8,8); }
std::string Cartridge::tile_row(unsigned address, unsigned columns, unsigned bank) const {
    std::string s;
    for (unsigned col=0;col<columns;++col) {
        const auto tile=byte(address+2*col,bank);
        char letter=' ';
        for (unsigned c=0x20;c<=0x5a;++c) {
            if (byte(0x7d18+2*(c-32))==tile) { letter=cartridge_character(c); break; }
        }
        s+=letter;
    }
    while (!s.empty() && s.back()==' ') s.pop_back();
    return s;
}
unsigned Cartridge::spell_id(unsigned group, unsigned index) const {
    return byte(word(group<3 ? 0x1a2c : 0x1da3)+5*(group%3)+index)&31;
}
std::string Cartridge::spell_name(unsigned group,unsigned index) const {
    // 00:3478 selects one of six 12-row, six-tile layouts. The lower row
    // of each two-row letter contains the visible glyph.
    return tile_row(word(0x3496)+144*group+12*(2+2*index)+2,4);
}
unsigned Cartridge::spell_cost(unsigned spell) const { return byte(word(0x1b88)+spell); }
unsigned Cartridge::shop_table() const { return word(0x38f9)+10*(byte(0xc2db)&31); }
unsigned Cartridge::shop_count() const { return std::min(3u,byte(shop_table(),3)+1); }
Cartridge::ShopItem Cartridge::shop_item(unsigned row) const {
    const auto address=shop_table()+1+3*row;
    return {byte(address,3),word(address+1,3)};
}
unsigned Cartridge::level_row(unsigned member, unsigned level) const {
    constexpr std::array<unsigned,4> operands{0x2b5d,0x2b68,0x2b74,0x2b80};
    return word(operands[member&3])+8*level;
}
std::optional<unsigned> Cartridge::next_experience(unsigned member) const {
    const auto record=0xc400+16*(member&3);
    const auto level=byte(record+5);
    if (level>=byte(0x2b90)) return {};
    return (word(level_row(member,level)+4,3)-word(record+3))&0xffff;
}
Cartridge::Comparison Cartridge::compare(unsigned member,unsigned item) const {
    const auto record=0xc400+16*(member&3);
    const auto properties=item<64 ? byte(word(0x26d1)+item,2) : 0u;
    const unsigned slot=properties&3;
    const bool equipment=(properties>>4)!=0 && slot<3;
    const unsigned before=byte(record+(slot==0 ? 8 : 9));
    const bool eligible=equipment && (properties&(16u<<(member&3))) && (byte(record)&1);
    Comparison result{slot,before,before,equipment,eligible,false};
    const auto level=byte(record+5);
    if (!eligible || level==0) return result;
    const auto row=level_row(member,level-1);
    const auto boost=[&](unsigned id){return byte(0x183a+id);};
    result.equipped=byte(record+10+slot)==item;
    unsigned after=byte(row+(slot==0 ? 1 : 2),3)+boost(item);
    if (slot!=0) after+=boost(byte(record+(slot==1 ? 12 : 11)));
    result.after=after&255;
    return result;
}
}
