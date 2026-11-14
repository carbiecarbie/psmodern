// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/map_screens.h"

namespace psmodern::game {
namespace {
constexpr unsigned bank_bytes=0x4000, slot2=0x8000, slot2_end=0xC000;
}
MapScreens::MapScreens(std::shared_ptr<const std::vector<std::uint8_t>> rom):rom_(std::move(rom)) {}

std::optional<MapScreen> MapScreens::decode(const MapArea& area,unsigned column,unsigned row,unsigned* given) const {
    if(!rom_ || column>=map::table_side || row>=map::table_side || area.table_bank>=rom_->size()/bank_bytes)return std::nullopt;
    const std::size_t base=std::size_t(area.table_bank)*bank_bytes;
    // The table and its screens are read with the area's bank in slot 2.
    auto read=[&](unsigned address)->std::optional<std::uint8_t> {
        if(address<slot2 || address>=slot2_end)return std::nullopt;
        return (*rom_)[base+address-slot2];
    };
    const unsigned entry=area.table+(row*map::table_side+column)*2;
    const auto low=read(entry),high=read(entry+1);
    if(!low || !high)return std::nullopt;
    unsigned at=*low|(*high<<8);
    std::array<std::uint8_t,map::window_slot> cells{};
    unsigned count=0;
    for(;;) {
        const auto code=read(at++);
        if(!code)return std::nullopt;
        if(*code==0)break;
        if(*code&0x80) {
            // A literal run of (code & 7Fh) ids; an empty one would make the
            // original copy 64 KiB.
            const unsigned length=*code&0x7f;
            if(!length)return std::nullopt;
            for(unsigned i=0;i<length;++i) {
                const auto id=read(at++);
                if(!id || count>=cells.size())return std::nullopt;
                cells[count++]=*id;
            }
        } else {
            const auto id=read(at++);
            if(!id)return std::nullopt;
            for(unsigned i=0;i<*code;++i) {
                if(count>=cells.size())return std::nullopt;
                cells[count++]=*id;
            }
        }
    }
    if(given)*given=count;
    if(count<map::cells_per_screen)return std::nullopt;
    MapScreen screen{};
    for(unsigned i=0;i<screen.size();++i)screen[i]=cells[i];
    return screen;
}

std::optional<std::array<std::uint16_t,4>> MapScreens::metatile(std::uint8_t bank,std::uint8_t id) const {
    if(!rom_ || bank>=rom_->size()/bank_bytes)return std::nullopt;
    const std::size_t at=std::size_t(bank)*bank_bytes+8u*id;
    std::array<std::uint16_t,4> words{};
    for(unsigned q=0;q<4;++q)words[q]=static_cast<std::uint16_t>((*rom_)[at+2*q]|((*rom_)[at+2*q+1]<<8));
    return words;
}

bool MapScreens::select(const MapArea& area) noexcept {
    if(tried_ && area==area_)return ready_;
    tried_=true;area_=area;ready_=false;
    for(unsigned row=0;row<map::screen_rows;++row)
        for(unsigned column=0;column<map::screen_columns;++column) {
            const auto screen=decode(area,column,row);
            if(!screen)return false;
            screens_[row*map::screen_columns+column]=*screen;
        }
    for(unsigned id=0;id<map::metatiles;++id) {
        const auto words=metatile(area.metatile_bank,static_cast<std::uint8_t>(id));
        if(!words)return false;
        for(unsigned q=0;q<4;++q)words_[id*4+q]=(*words)[q];
    }
    ready_=true;
    return true;
}
}
