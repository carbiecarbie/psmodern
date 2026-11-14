// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/options_menu.h"
#include "app/localization.h"
#include <algorithm>
#include <stdexcept>
namespace psmodern::app {
RomFont::RomFont(std::span<const std::uint8_t> rom) {
    if(rom.size()!=512*1024)throw std::runtime_error("Cannot load font from an incomplete ROM.");
    // Original 00:03CD plane format and the two-tile character mapping used
    // by 00:33D6. Decode immutable ROM, never live VRAM.
    std::array<std::uint8_t,1408> patterns{};std::size_t source=0x43ad8;
    for(unsigned plane=0;plane<4;++plane) {
        std::size_t destination=plane;
        for(;;) {
            if(source>=0x43d58)throw std::runtime_error("Incomplete ROM font plane.");
            const auto control=rom[source++];if(!control)break;
            const unsigned count=control&127;
            if(!count)throw std::runtime_error("Empty ROM font packet.");
            for(unsigned i=0;i<count;++i) {
                if(source>=0x43d58 || destination>=patterns.size())throw std::runtime_error("Invalid ROM font packet.");
                patterns[destination]=rom[source];destination+=4;if(control&128)++source;
            }
            if(!(control&128))++source;
        }
        if(destination!=patterns.size()+plane)throw std::runtime_error("Wrong ROM font plane length.");
    }
    if(source!=0x43d58)throw std::runtime_error("Wrong ROM font stream length.");
    for(unsigned code=32;code<=95;++code) {
        const auto tile=rom[0x7d17+(code-32)*2+1];
        if(tile<192 || tile>=236)throw std::runtime_error("Invalid ROM font tile.");
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            unsigned color=0;
            for(unsigned plane=0;plane<4;++plane)color|=((patterns[(tile-192)*32+y*4+plane]>>(7-x))&1)<<plane;
            if(color==2)glyphs_[code-32][y]|=1u<<(7-x);
        }
    }
    // Decode the border/cursor block loaded alongside the original field font.
    std::array<std::uint8_t,384> window_patterns{};source=0x43d58;
    for(unsigned plane=0;plane<4;++plane) {
        std::size_t destination=plane;
        for(;;) {
            if(source>=0x43df8)throw std::runtime_error("Incomplete ROM window plane.");
            const auto control=rom[source++];if(!control)break;
            const unsigned count=control&127;
            if(!count)throw std::runtime_error("Empty ROM window packet.");
            for(unsigned i=0;i<count;++i) {
                if(source>=0x43df8 || destination>=window_patterns.size())throw std::runtime_error("Invalid ROM window packet.");
                window_patterns[destination]=rom[source];destination+=4;if(control&128)++source;
            }
            if(!(control&128))++source;
        }
        if(destination!=window_patterns.size()+plane)throw std::runtime_error("Wrong ROM window plane length.");
    }
    if(source!=0x43df8)throw std::runtime_error("Wrong ROM window stream length.");
    for(unsigned tile=0;tile<window_tiles_.size();++tile)
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            unsigned color=0;
            for(unsigned plane=0;plane<4;++plane)color|=((window_patterns[tile*32+y*4+plane]>>(7-x))&1)<<plane;
            if(color==2)window_tiles_[tile][y]|=1u<<(7-x);
        }
    // Host-only punctuation required by the approved labels. These slots are
    // blank in the cartridge font: reuse C's curved side, Z's diagonal and
    // reduced zero rings rather than embedding additional font artwork.
    auto pixel=[&](char c,unsigned x,unsigned y){return bool(glyphs_[c-32][y]&(1u<<(7-x)));};
    for(unsigned y=0;y<8;++y) {
        for(unsigned x=0;x<3;++x)if(pixel('C',x,y)) {
            glyphs_['('-32][y]|=1u<<(6-x);
            glyphs_[')'-32][y]|=1u<<(1+x);
        }
        const unsigned source_y=1+y*5/7;
        for(unsigned x=6-source_y;x<=7-source_y;++x)
            if(pixel('Z',x,source_y))glyphs_['/'-32][y]|=1u<<(7-x);
        glyphs_['%'-32][y]=glyphs_['/'-32][y];
    }
    for(unsigned y=0;y<3;++y)for(unsigned x=0;x<3;++x)
        if(pixel('0',x*3,y*3)) {
            glyphs_['%'-32][y]|=1u<<(7-x);
            glyphs_['%'-32][y+5]|=1u<<(3-x);
        }
}
bool RomFont::supports(char c) const {
    if(c==' ')return true;
    if(c<32 || c>95)return false;
    return std::any_of(glyphs_[c-32].begin(),glyphs_[c-32].end(),[](auto row){return row!=0;});
}
void RomFont::text(std::span<std::uint32_t> pixels,int x,int y,std::string_view value,std::uint32_t color) const {
    if(pixels.size()!=256*192)throw std::invalid_argument("Expected a 256 by 192 overlay.");
    for(char c:value) {
        if(!supports(c))throw std::runtime_error("Unsupported ROM menu character.");
        for(int row=0;row<8;++row)for(int column=0;column<8;++column)
            if(x+column>=0 && x+column<256 && y+row>=0 && y+row<192 && (glyphs_[c-32][row]&(1u<<(7-column))))
                pixels[(y+row)*256+x+column]=color;
        x+=8;
    }
}
void RomFont::window(std::span<std::uint32_t> pixels,int x,int y,int width,int height,int cursor_y) const {
    if(pixels.size()!=256*192 || x<0 || y<0 || width<16 || height<16 || x+width>256 || y+height>192 || width%8 || height%8)
        throw std::invalid_argument("Expected an in-bounds tile-aligned menu window.");
    for(int row=y;row<y+height;++row)std::fill(pixels.begin()+row*256+x,pixels.begin()+row*256+x+width,0xff000000);
    auto tile=[&](unsigned index,int tx,int ty,bool flip_x=false,bool flip_y=false) {
        for(int py=0;py<8;++py)for(int px=0;px<8;++px)
            if(window_tiles_[index][flip_y ? 7-py : py]&(1u<<(flip_x ? px : 7-px)))
                pixels[(ty+py)*256+tx+px]=0xffffffff;
    };
    tile(1,x,y);tile(1,x+width-8,y,true);tile(1,x,y+height-8,false,true);tile(1,x+width-8,y+height-8,true,true);
    for(int tx=x+8;tx<x+width-8;tx+=8){tile(2,tx,y);tile(2,tx,y+height-8,false,true);}
    for(int ty=y+8;ty<y+height-8;ty+=8){tile(ty==cursor_y ? 0 : 3,x,ty);tile(3,x+width-8,ty,true);}
}
void OptionsMenu::open(game::Options options,bool restricted,bool pending,bool recording,bool automap,bool widescreen) {
    game::validate(options);draft_=options;restricted_=restricted;pending_=pending;automap_=automap;widescreen_=widescreen;
    visible_=true;armed_=false;failed_=false;recording_=recording;selected_=top_=0;previous_={};next_repeat_=0;
}
MenuAction OptionsMenu::update(const MenuInput& input,double now) {
    if(!visible_)return MenuAction::none;
    // Losing focus cannot count as observing a physical release. Require a
    // neutral focused snapshot before accepting held keys on focus regain.
    if(!input.focused){armed_=false;previous_=input;return MenuAction::none;}
    if(!armed_) { if(!input.any())armed_=true;previous_=input;return MenuAction::none; }
    const bool cancel=(input.cancel&&!previous_.cancel)||(input.toggle&&!previous_.toggle);
    const bool accept=input.accept&&!previous_.accept;
    const bool fresh=(input.up&&!previous_.up)||(input.down&&!previous_.down)||
        (input.left&&!previous_.left)||(input.right&&!previous_.right);
    const bool direction=input.up||input.down||input.left||input.right;
    const bool move=direction && (fresh||now>=next_repeat_);
    previous_=input;
    if(cancel)return restricted_ ? MenuAction::close : MenuAction::apply;
    if(move) {
        next_repeat_=now+(fresh ? .35 : .12);
        if(input.up!=input.down) {
            selected_=(selected_+(input.down ? 1 : 5))%6;
            if(selected_<top_)top_=selected_;
            else if(selected_>=top_+4)top_=selected_-3;
        }
        if(!restricted_ && selected_==2 && input.left!=input.right)automap_=!automap_;
        if(!restricted_ && selected_==3 && input.left!=input.right)widescreen_=!widescreen_;
        if(!restricted_ && selected_<2 && input.left!=input.right) {
            const auto& choices=selected_==0 ? game::encounter_percent_choices : game::experience_percent_choices;
            auto& current=selected_==0 ? draft_.encounter_percent : draft_.experience_percent;
            std::size_t i=0;while(i<choices.count && choices.values[i]!=current)++i;
            current=choices.values[(i+choices.count+(input.right?1:choices.count-1))%choices.count];
        }
    }
    if(accept)return selected_==4 ? MenuAction::controls : restricted_ ? MenuAction::close :
        selected_==5 ? MenuAction::language : MenuAction::apply;
    return MenuAction::none;
}
void OptionsMenu::render(std::span<std::uint32_t> pixels,const RomFont& font,Language language) const {
    // Four visible rows retain the original 32-pixel pitch. Child-screen
    // entries have no value line, and WIDESCREEN keeps its value on its own line; the fifth and sixth rows scroll into the same window.
    static constexpr int rows[]{48,80,112,144};
    font.window(pixels,24,24,208,152,rows[selected_-top_]);
    auto text=[&](int x,int y,std::string_view label){font.text(pixels,x,y,label,0xffffffff);};
    auto label=[&](UiText id){return ui_text(id,language);};
    auto value=[&](unsigned percent,unsigned recommended) {
        return std::to_string(percent)+"%"+std::string(percent==100 ? label(UiText::default_suffix) :
            percent==recommended ? label(UiText::recommended_suffix) : std::string_view{});
    };
    const auto title=label(UiText::options);
    text((256-int(title.size())*8)/2,32,title);
    static constexpr UiText labels[]{UiText::encounters,UiText::experience_meseta,UiText::automap,UiText::widescreen,UiText::controls,UiText::language};
    for(unsigned i=0;i<4;++i) {
        const unsigned row=top_+i;const int y=rows[i];
        text(40,y,label(labels[row]));
        if(row==0)text(48,y+16,value(draft_.encounter_percent,game::switch_like_encounter_percent));
        else if(row==1)text(48,y+16,value(draft_.experience_percent,game::switch_like_experience_percent));
        else if(row==2)text(48,y+16,label(automap_ ? UiText::on : UiText::off_default));
        else if(row==3) {
            // One line, value at the right: a value line here would land on
            // the status and footer lines when this row sits in the last slot.
            const auto state=label(widescreen_ ? UiText::on : UiText::off);
            text(216-int(state.size())*8,y,state);
        }
    }
    // The pending state concerns the routine options only.
    if(failed_)text(40,152,label(UiText::save_failed));
    else if(restricted_)text(40,152,label(recording_ ? UiText::recording_read_only : UiText::z80_read_only));
    else if(pending_)text(40,152,label(UiText::saved_waiting));
    auto footer=std::string(label(top_ ? UiText::more_above : UiText::more_below));
    if(!failed_ && !restricted_ && !pending_)
        footer=std::string(label(UiText::saves_on_exit))+" "+std::to_string(top_+1)+"/3";
    font.text(pixels,40,160,footer,0xffaaaaaa);
}
FrameInput InputRelease::filter(FrameInput input,bool held,bool keyboard_focused) {
    if(!waiting_)return input;
    if(!keyboard_focused)return {};
    const auto& p=input.pads[0];const auto& q=input.pads[1];
    if(!held && !input.pause && !input.reset && !p.up&&!p.down&&!p.left&&!p.right&&!p.one&&!p.two &&
       !q.up&&!q.down&&!q.left&&!q.right&&!q.one&&!q.two)waiting_=false;
    return {};
}
}
