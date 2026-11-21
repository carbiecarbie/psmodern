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
    text(pixels,256,x,y,value,color);
}
void RomFont::text(std::span<std::uint32_t> pixels,int stride,int x,int y,std::string_view value,std::uint32_t color) const {
    if (stride<=0 || pixels.size()%stride) throw std::invalid_argument("Invalid ROM font surface.");
    const int height=static_cast<int>(pixels.size()/stride);
    for(char c:value) {
        if(!supports(c))throw std::runtime_error("Unsupported ROM menu character.");
        for(int row=0;row<8;++row)for(int column=0;column<8;++column)
            if(x+column>=0 && x+column<stride && y+row>=0 && y+row<height && (glyphs_[c-32][row]&(1u<<(7-column))))
                pixels[(y+row)*stride+x+column]=color;
        x+=8;
    }
}
void RomFont::window(std::span<std::uint32_t> pixels,int x,int y,int width,int height,int cursor_y) const {
    if (pixels.size()!=256*192) throw std::invalid_argument("Expected a 256 by 192 overlay.");
    window(pixels,256,x,y,width,height,cursor_y,menu_style::white);
}
void RomFont::window(std::span<std::uint32_t> pixels,int stride,int x,int y,int width,int height,int cursor_y,std::uint32_t border) const {
    if(stride<=0 || pixels.size()%stride || x<0 || y<0 || width<16 || height<16 ||
       x>stride-width || static_cast<std::size_t>(y)+height>pixels.size()/stride || width%8 || height%8)
        throw std::invalid_argument("Expected an in-bounds tile-aligned menu window.");
    for(int row=y;row<y+height;++row)std::fill(pixels.begin()+row*stride+x,pixels.begin()+row*stride+x+width,0xff000000);
    auto tile=[&](unsigned index,int tx,int ty,bool flip_x=false,bool flip_y=false) {
        for(int py=0;py<8;++py)for(int px=0;px<8;++px)
            if(window_tiles_[index][flip_y ? 7-py : py]&(1u<<(flip_x ? px : 7-px)))
                pixels[(ty+py)*stride+tx+px]=border;
    };
    tile(1,x,y);tile(1,x+width-8,y,true);tile(1,x,y+height-8,false,true);tile(1,x+width-8,y+height-8,true,true);
    for(int tx=x+8;tx<x+width-8;tx+=8){tile(2,tx,y);tile(2,tx,y+height-8,false,true);}
    for(int ty=y+8;ty<y+height-8;ty+=8){tile(ty==cursor_y ? 0 : 3,x,ty);tile(3,x+width-8,ty,true);}
}
namespace menu_style {
void window(std::span<std::uint32_t> pixels,const RomFont& font,const TileWindow& w,int cursor_row) {
    font.window(pixels,w.column*tile,w.row*tile,w.width*tile,w.height*tile,cursor_row<0 ? -1 : cursor_row*tile);
}
void text(std::span<std::uint32_t> pixels,const RomFont& font,int column,int row,std::string_view value,std::uint32_t color) {
    font.text(pixels,column*tile,row*tile,value,color);
}
}
namespace {
using namespace options_layout;
constexpr UiText row_labels[]{UiText::encounters,UiText::experience_meseta_short,UiText::automap,UiText::widescreen,UiText::controls,UiText::language};
constexpr UiText row_explanations[]{UiText::explain_encounters,UiText::explain_experience,UiText::explain_automap,
    UiText::explain_widescreen,UiText::explain_controls,UiText::explain_language};
constexpr LanguagePreference language_choices[]{LanguagePreference::native,LanguagePreference::english,LanguagePreference::portuguese};
// Each language is named in its own words, so that a player who reads only
// one of them finds it.
constexpr std::string_view language_names[]{"","ENGLISH","PORTUGUES"};
}
void OptionsMenu::open(game::Options options,bool pending,bool automap,bool widescreen) {
    game::validate(options);draft_=options;pending_=pending;automap_=automap;widescreen_=widescreen;
    visible_=true;armed_=false;failed_=language_failed_=false;selected_=0;previous_={};next_repeat_=0;
}
void OptionsMenu::set_language(LanguagePreference preference,std::array<bool,3> available) {
    (void)format_language(preference); // refuse unknown values
    language_=preference;language_available_=available;language_failed_=false;
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
    if(cancel)return MenuAction::apply;
    if(move) {
        next_repeat_=now+(fresh ? .35 : .12);
        if(input.up!=input.down)selected_=(selected_+(input.down ? 1 : rows-1))%rows;
        if(selected_==2 && input.left!=input.right)automap_=!automap_;
        if(selected_==3 && input.left!=input.right)widescreen_=!widescreen_;
        if(selected_<2 && input.left!=input.right) {
            const auto& choices=selected_==0 ? game::encounter_percent_choices : game::experience_percent_choices;
            auto& current=selected_==0 ? draft_.encounter_percent : draft_.experience_percent;
            std::size_t i=0;while(i<choices.count && choices.values[i]!=current)++i;
            current=choices.values[(i+choices.count+(input.right?1:choices.count-1))%choices.count];
        }
        if(selected_==language_row && input.left!=input.right) {
            // The next preference with a ROM, skipping those without; the
            // owner saves the change at once, as the Language screen's Save did.
            const auto from=std::size_t(language_);
            for(std::size_t step=1;step<std::size(language_choices);++step) {
                const auto i=(from+(input.right ? step : std::size(language_choices)-step))%std::size(language_choices);
                if(language_available_[i]) { language_=language_choices[i];return MenuAction::language; }
            }
        }
    }
    if(accept)return selected_==controls_row ? MenuAction::controls : MenuAction::apply;
    return MenuAction::none;
}
std::string_view OptionsMenu::label(unsigned row,Language language) {
    if(row>=rows)throw std::out_of_range("No such Options row.");
    return ui_text(row_labels[row],language);
}
std::vector<std::string> OptionsMenu::choices(unsigned row,Language language) const {
    auto percents=[](const game::PercentChoices& choices) {
        std::vector<std::string> values;
        for(std::size_t i=0;i<choices.count;++i)values.push_back(std::to_string(choices.values[i])+"%");
        return values;
    };
    switch(row) {
    case 0:return percents(game::encounter_percent_choices);
    case 1:return percents(game::experience_percent_choices);
    case 2:case 3:return {std::string(ui_text(UiText::off,language)),std::string(ui_text(UiText::on,language))};
    case controls_row:return {};
    case language_row:return {std::string(ui_text(UiText::automatic_short,language)),std::string(language_names[1]),std::string(language_names[2])};
    }
    throw std::out_of_range("No such Options row.");
}
std::size_t OptionsMenu::current(unsigned row) const {
    auto index=[](const game::PercentChoices& choices,unsigned value) {
        std::size_t i=0;while(i<choices.count && choices.values[i]!=value)++i;return i;
    };
    switch(row) {
    case 0:return index(game::encounter_percent_choices,draft_.encounter_percent);
    case 1:return index(game::experience_percent_choices,draft_.experience_percent);
    case 2:return automap_ ? 1 : 0;
    case 3:return widescreen_ ? 1 : 0;
    case language_row:return std::size_t(language_);
    }
    return 0;
}
bool OptionsMenu::available(unsigned row,std::size_t choice) const {
    return row!=language_row || (choice<language_available_.size() && language_available_[choice]);
}
std::optional<menu_style::TileWindow> OptionsMenu::values_window(Language language) const {
    const auto values=choices(selected_,language);
    if(values.empty())return {};
    // Beside the selected row, over the list's right border, its first
    // choice on the row's line; moved up a line at a time when it would
    // reach below the list (Language), so the explanation stays apart.
    std::size_t longest=0;for(const auto& value:values)longest=std::max(longest,value.size());
    menu_style::TileWindow window{values_column,line_row(selected_)-1,std::max(values_width,int(longest)+2),line_step*int(values.size())+1};
    while(window.row+window.height>list.row+list.height)window.row-=line_step;
    return window;
}
std::array<std::string_view,explanation_lines> OptionsMenu::explanation(unsigned row,Language language) {
    if(row>=rows)throw std::out_of_range("No such Options row.");
    std::array<std::string_view,explanation_lines> lines{};
    auto text=ui_text(row_explanations[row],language);
    for(auto& line:lines) {
        const auto end=text.find('\n');
        line=text.substr(0,end);
        text=end==std::string_view::npos ? std::string_view{} : text.substr(end+1);
    }
    return lines;
}
std::array<std::string,explanation_lines> OptionsMenu::explanation_text(Language language) const {
    const auto fixed=explanation(selected_,language);
    std::array<std::string,explanation_lines> lines{std::string(fixed[0]),std::string(fixed[1]),std::string(fixed[2])};
    // A language without its ROM: say which ROM it needs instead of how AUTO works.
    if(selected_==language_row) {
        if(!language_available_[std::size_t(LanguagePreference::english)])lines[2]=ui_text(UiText::english_needs_rom,language);
        else if(!language_available_[std::size_t(LanguagePreference::portuguese)])lines[2]=ui_text(UiText::portuguese_needs_rom,language);
    }
    return lines;
}
std::string_view OptionsMenu::state(Language language) const {
    // The pending state concerns the routine options only.
    if(failed_ || language_failed_)return ui_text(UiText::save_failed,language);
    if(pending_)return ui_text(UiText::saved_waiting,language);
    return ui_text(UiText::saves_on_exit,language);
}
bool OptionsMenu::drawn(int x,int y,Language language) const {
    const auto values=values_window(language);
    return list.contains(x,y) || options_layout::explanation.contains(x,y) || (values && values->contains(x,y));
}
void OptionsMenu::render(std::span<std::uint32_t> pixels,const RomFont& font,Language language) const {
    using menu_style::white;using menu_style::grey;
    // A line per row, the cursor in the list's left border, as the game's
    // own menus; the current value repeated in grey at the right.
    menu_style::window(pixels,font,list,line_row(selected_));
    for(unsigned row=0;row<rows;++row) {
        const int y=line_row(row);
        menu_style::text(pixels,font,list.column+1,y,label(row,language),white);
        const auto values=choices(row,language);
        if(values.empty())continue;
        const auto& value=values[current(row)];
        menu_style::text(pixels,font,list.column+list.width-1-int(value.size()),y,value,grey);
    }
    // The values window shows the choices; left and right change them, so
    // it has no cursor of its own. The current one is yellow.
    if(const auto window=values_window(language)) {
        menu_style::window(pixels,font,*window);
        const auto values=choices(selected_,language);
        for(std::size_t i=0;i<values.size();++i)
            menu_style::text(pixels,font,window->column+window->width-1-int(values[i].size()),window->row+1+line_step*int(i),values[i],
                i==current(selected_) ? menu_style::yellow : available(selected_,i) ? grey : menu_style::dim);
    }
    menu_style::window(pixels,font,options_layout::explanation);
    const auto lines=explanation_text(language);
    for(unsigned i=0;i<explanation_lines;++i)menu_style::text(pixels,font,options_layout::explanation.column+1,options_layout::explanation.row+1+line_step*int(i),lines[i],white);
    const bool quiet=!failed_ && !language_failed_ && !pending_;
    menu_style::text(pixels,font,options_layout::explanation.column+1,state_row,state(language),quiet ? grey : white);
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
