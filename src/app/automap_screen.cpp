// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/automap_screen.h"
#include <stdexcept>
#include <string_view>

namespace psmodern::app {
namespace {
namespace colors=automap_colors;
// New 8x8 drawings made for PSModern, transcribed from the approved mockup
// (m7-plan/map-mockup.html). They are the one named exception to "No new
// art", recorded in docs/design.md (Non-goals, maintainer decision
// 2026-11-11); no other new art may be added under it.
// Letters: Y yellow, O orange, B brown, W white, G grey, K black, E green,
// '.' transparent.
constexpr std::array<std::string_view,8> drawings[]{
    {"...YY...","..YYYY..",".YYYYYY.","YYYYYYYY","...YY...","...YY...","...YY...","........"},   // party, pointing up
    {"........",".BBBBBB.","BOOOOOOB","BBBWWBBB","BOOWWOOB","BOOOOOOB","BBBBBBBB","........"},   // chest
    {"........",".BBBBBB.","B......B","BBBBBBBB","B......B","B......B","BBBBBBBB","........"},   // opened chest
    {"......WW","......WW","....WWWW","....WWWW","..WWWWWW","..WWWWWW","WWWWWWWW","WWWWWWWW"},   // stairs up
    {"GGGGGGGG","GGGGGGGG","..GGGGGG","..GGGGGG","....GGGG","....GGGG","......GG","......GG"},   // stairs down
    {"..EEEE..",".EKKKKE.","EKKKKKKE","EKKKKKKE","EKKKKKKE","EKKKKKKE","EKKKKKKE","EKKKKKKE"},   // exit
    {"..BBBB..","..BBBB..","..BBBB..","..BBOB..","..BBBB..","..BBBB..","..BBBB..","..BBBB.."},   // door (one drawing, three colours)
    {"........","..GGGG..",".GKKKKG.","GKKKKKKG","GKKKKKKG",".GKKKKG.","..GGGG..","........"}};  // pitfall
std::uint32_t color(char letter) {
    switch(letter) {
        case 'Y': return colors::yellow;  case 'O': return colors::orange; case 'B': return colors::brown;
        case 'W': return colors::white;   case 'G': return colors::grey;   case 'K': return colors::black;
        case 'E': return colors::green;   default: return 0;
    }
}
// The door drawing's body colour by kind; the knob stays orange.
std::uint32_t door_body(game::MapKind kind) {
    return kind==game::MapKind::locked_door ? colors::grey : kind==game::MapKind::magic_door ? colors::magic : colors::brown;
}
void fill(std::span<std::uint32_t> pixels,int x,int y,std::uint32_t value) {
    for(int row=0;row<8;++row)for(int column=0;column<8;++column)pixels[(y+row)*256+x+column]=value;
}
// Quarter turns clockwise: 0 up, 1 right, 2 down, 3 left.
void draw(std::span<std::uint32_t> pixels,int x,int y,const IconPixels& icon,unsigned turns=0) {
    for(int row=0;row<8;++row)for(int column=0;column<8;++column) {
        int sx=column,sy=row;
        if(turns==1){sx=row;sy=7-column;} else if(turns==2){sx=7-column;sy=7-row;} else if(turns==3){sx=7-row;sy=column;}
        if(const auto value=icon[sy][sx])pixels[(y+row)*256+x+column]=value;
    }
}
IconPixels door(game::MapKind kind) {
    auto icon=automap_icon(MapIcon::door);
    for(auto& row:icon)for(auto& value:row)if(value==colors::brown)value=door_body(kind);
    return icon;
}
unsigned turns(std::int8_t ahead) {
    switch(ahead) {
        case -16: return 0; case 1: return 1; case 16: return 2; case -1: return 3;
        default: throw std::invalid_argument("The party must face one of the four directions.");
    }
}
}
IconPixels automap_icon(MapIcon icon) {
    IconPixels result{};
    const auto& drawing=drawings[static_cast<unsigned>(icon)];
    for(unsigned row=0;row<8;++row)for(unsigned column=0;column<8;++column)result[row][column]=color(drawing[row][column]);
    return result;
}
std::uint32_t automap_background(game::MapKind kind) {
    using game::MapKind;
    switch(kind) {
        case MapKind::unknown: return colors::black;
        case MapKind::wall: case MapKind::door: case MapKind::locked_door: case MapKind::magic_door: return colors::wall;
        default: return colors::floor;
    }
}
void render_automap(std::span<std::uint32_t> pixels,const RomFont& font,const game::MapView& view) {
    namespace layout=automap_layout;
    using game::MapKind;
    if(pixels.size()!=256*192)throw std::invalid_argument("Expected a 256 by 192 overlay.");
    // No cursor: -1 matches no tile row.
    font.window(pixels,layout::window_x,layout::window_y,layout::window_width,layout::window_height,-1);
    font.text(pixels,layout::title_x,layout::title_y,"MAP",colors::white);
    const auto party_turns=turns(view.party_ahead);
    for(unsigned cell=0;cell<view.cells.size();++cell) {
        const auto& map=view.cells[cell];
        const int x=layout::grid_x+static_cast<int>(cell%16)*layout::cell_size,y=layout::grid_y+static_cast<int>(cell/16)*layout::cell_size;
        fill(pixels,x,y,automap_background(map.kind));
        if(cell==view.party_cell) { draw(pixels,x,y,automap_icon(MapIcon::party),party_turns); continue; }
        if(map.exit) { draw(pixels,x,y,automap_icon(MapIcon::exit)); continue; }
        switch(map.kind) {
            case MapKind::chest: draw(pixels,x,y,automap_icon(MapIcon::chest)); break;
            case MapKind::opened_chest: draw(pixels,x,y,automap_icon(MapIcon::opened_chest)); break;
            case MapKind::stairs_up: draw(pixels,x,y,automap_icon(MapIcon::stairs_up)); break;
            case MapKind::stairs_down: draw(pixels,x,y,automap_icon(MapIcon::stairs_down)); break;
            case MapKind::door: case MapKind::locked_door: case MapKind::magic_door: draw(pixels,x,y,door(map.kind)); break;
            case MapKind::pitfall: draw(pixels,x,y,automap_icon(MapIcon::pitfall)); break;
            default: break;
        }
    }
    struct Row { std::string_view label; MapIcon icon; MapKind kind; };
    static constexpr Row legend[]{
        {"YOU",MapIcon::party,MapKind::floor},{"CHEST",MapIcon::chest,MapKind::chest},
        {"EMPTY",MapIcon::opened_chest,MapKind::opened_chest},{"UP",MapIcon::stairs_up,MapKind::stairs_up},
        {"DOWN",MapIcon::stairs_down,MapKind::stairs_down},{"EXIT",MapIcon::exit,MapKind::floor},
        {"DOOR",MapIcon::door,MapKind::door},{"LOCKED",MapIcon::door,MapKind::locked_door},
        {"MAGIC",MapIcon::door,MapKind::magic_door},{"PIT",MapIcon::pitfall,MapKind::pitfall}};
    for(unsigned i=0;i<std::size(legend);++i) {
        const int y=layout::legend_y+static_cast<int>(i)*layout::legend_step;
        fill(pixels,layout::legend_icon_x,y,automap_background(legend[i].kind));
        draw(pixels,layout::legend_icon_x,y,legend[i].icon==MapIcon::door ? door(legend[i].kind) : automap_icon(legend[i].icon));
        font.text(pixels,layout::legend_label_x,y,legend[i].label,colors::white);
    }
}
MenuAction MapScreen::update(const MenuInput& input) {
    if(!visible_)return MenuAction::none;
    if(!input.focused) { armed_=false;previous_=input;return MenuAction::none; }
    if(!armed_) { if(!input.any())armed_=true;previous_=input;return MenuAction::none; }
    const bool close=(input.map && !previous_.map)||(input.toggle && !previous_.toggle)||(input.cancel && !previous_.cancel);
    previous_=input;
    return close ? MenuAction::close : MenuAction::none;
}
}
