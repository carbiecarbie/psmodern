// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/touch_controls.h"
#include <algorithm>
#include <cstdlib>

namespace psmodern::app {
namespace {
bool contains(const WindowRect& r,int x,int y) {
    return x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height;
}
WindowRect fit_game(WindowRect area,int canvas) {
    if (area.width<=0 || area.height<=0) return {};
    const int scale=std::min(area.width/canvas,area.height/picture_height);
    int w=canvas*scale,h=picture_height*scale;
    if (!scale) {
        w=static_cast<int>(std::min<std::int64_t>(area.width,std::int64_t(area.height)*canvas/picture_height));
        h=std::max(1,w*picture_height/canvas);
    }
    return {area.x+(area.width-w)/2,area.y+(area.height-h)/2,w,h};
}
WindowRect grow(WindowRect r,int left,int top,int right,int bottom) {
    return {r.x-left,r.y-top,r.width+left+right,r.height+top+bottom};
}
// By angle from the cross's centre, so a thumb that drifts off the drawing
// keeps its direction.
Action direction(const WindowRect& pad,int x,int y) {
    const int dx=x-pad.x-pad.width/2,dy=y-pad.y-pad.height/2;
    if (std::max(std::abs(dx),std::abs(dy))<=pad.width/6) return Action::count;
    if (std::abs(dx)>std::abs(dy)) return dx<0 ? Action::left : Action::right;
    return dy<0 ? Action::up : Action::down;
}
std::string_view label(Action action,Language language,unsigned speed) {
    const bool pt=language==Language::portuguese;
    switch (action) {
    case Action::button1: return "1";
    case Action::button2: return "2";
    case Action::pause: return pt ? "PAUSA" : "PAUSE";
    case Action::options: return pt ? "OPC" : "OPT";
    case Action::speed:
        if (speed==1) return "1X";
        if (speed==2) return "2X";
        if (speed==4) return "4X";
        break;
    case Action::map: return pt ? "MAPA" : "MAP";
    default: break;
    }
    throw std::invalid_argument("Unsupported touch button or speed.");
}
}
TouchLayout touch_layout(int width,int height,WindowBorders insets,bool wide,bool gameplay,bool automap) {
    TouchLayout layout;
    const int left=std::clamp(insets.left,0,std::max(0,width));
    const int top=std::clamp(insets.top,0,std::max(0,height));
    const int w=width-left-std::max(0,insets.right),h=height-top-std::max(0,insets.bottom);
    if (w<=0 || h<=0) return layout;
    const bool portrait=width<=height;
    auto& b=layout.buttons;
    auto& hit=layout.hit;
    int unit,side,lx,y;
    if (portrait) {
        // The picture at the top, a row of shortcuts under it, then the cross
        // and the two buttons centred in what is left, clear of the bottom edge.
        unit=std::min(w/6,h/9);
        side=3*unit;
        const int gap=std::max(1,unit/8),row=unit*3/4,reserve=side+row+3*gap;
        layout.game=fit_game({left,top,w,h-reserve},canvas_width(wide));
        if (!unit) return layout;
        layout.game.y=top+std::min(gap,h-reserve-layout.game.height);
        const int row_y=layout.game.y+layout.game.height+gap,below=row_y+row+gap;
        y=below+std::max(0,(top+h-below-side)/2);
        lx=left+unit/4;
        const int button=unit*5/4;
        b[Action::button2]={left+w-unit/8-button,y+unit/12,button,button};
        b[Action::button1]={b[Action::button2].x-button-unit/24,y+side-button-unit/2,button,button};
        hit[Action::button1]=grow(b[Action::button1],0,0,0,unit/2);
        hit[Action::button2]=grow(b[Action::button2],0,gap,width-b[Action::button2].x-button,0);
        layout.pad_hit=grow({lx,y,side,side},lx,gap,0,std::min(unit/2,top+h-y-side));
        if (gameplay) {
            const int slot=(w-2*(unit/4)-3*gap)/4;
            int i=0;
            for (Action a:{Action::pause,Action::options,Action::speed,Action::map}) {
                if (a!=Action::map || automap) hit[a]=b[a]={lx+i*(slot+gap),row_y,slot,row};
                ++i;
            }
        }
    } else {
        // Sized from the side bars of the 256-wide picture and the same with
        // Widescreen on: there the controls only reach the 44 extra columns.
        const int bar=(w-fit_game({left,top,w,h},canvas_width(false)).width)/2;
        unit=std::min(std::max(bar*5/16,std::min(w/20,h/16)),h*2/11);
        side=3*unit;
        const int margin=unit/5,gap=std::max(1,unit/12),row=unit*3/4,row_y=top+margin;
        // On a 4:3 tablet the picture alone can fill the whole window. Fit
        // its central 256 columns between the controls even on those screens.
        const int canvas=canvas_width(wide);
        int central_space=w-2*(margin+side);
        // Leave a pixel at each boundary for rounding a fractional downscale.
        if (wide && unit && central_space<canvas_width(false)) central_space-=2;
        const int picture_space=static_cast<int>(std::min<std::int64_t>(w,
            std::int64_t(central_space)*canvas/canvas_width(false)));
        layout.game=fit_game({left,top,picture_space,h},canvas);
        layout.game.x=left+(w-layout.game.width)/2;
        if (!unit) return layout;
        lx=left+margin;
        const int rx=left+w-margin-side,button=(side-gap)/2;
        y=std::max(row_y+row+gap,std::min(top+h*38/100,top+h-margin-side));
        b[Action::button1]={rx,y+side-button-unit/8,button,button};
        b[Action::button2]={rx+side-button,b[Action::button1].y-unit*9/10,button,button};
        hit[Action::button1]=grow(b[Action::button1],gap,0,0,unit/3);
        hit[Action::button2]=grow(b[Action::button2],0,gap,width-rx-side,0);
        layout.pad_hit=grow({lx,y,side,side},lx,unit/3,unit/3,std::min(unit/2,top+h-y-side));
        if (gameplay) {
            b[Action::pause]={lx,row_y,button,row};
            b[Action::options]={lx+side-button,row_y,button,row};
            b[Action::speed]={rx,row_y,button,row};
            hit[Action::pause]=grow(b[Action::pause],lx,row_y,0,gap);
            hit[Action::options]=grow(b[Action::options],0,row_y,gap,gap);
            hit[Action::speed]=grow(b[Action::speed],gap,row_y,0,gap);
            if (automap) {
                b[Action::map]={rx+side-button,row_y,button,row};
                hit[Action::map]=grow(b[Action::map],0,row_y,width-rx-side,gap);
            }
        }
        layout.overlay=lx+side>layout.game.x || rx<layout.game.x+layout.game.width;
    }
    layout.pad={lx,y,side,side};
    b[Action::up]={lx+unit,y,unit,unit};
    b[Action::down]={lx+unit,y+2*unit,unit,unit};
    b[Action::left]={lx,y+unit,unit,unit};
    b[Action::right]={lx+2*unit,y+unit,unit,unit};
    return layout;
}
void TouchControls::set_layout(const TouchLayout& layout) {
    clear();
    layout_=layout;
}
void TouchControls::down(std::int64_t finger,int x,int y) {
    if (!visible_) { visible_=true; return; } // consume the whole waking contact
    if (fingers_.contains(finger)) return;
    if (contains(layout_.pad_hit,x,y)) {
        // One thumb steers. A second finger cannot produce a diagonal.
        for (const auto& entry:fingers_) if (entry.second.pad) return;
        fingers_.emplace(finger,Finger{true,Action::count,direction(layout_.pad,x,y)});
        return;
    }
    for (Action button:{Action::button1,Action::button2,Action::pause,Action::options,Action::speed,Action::map}) {
        if (contains(layout_.hit[button],x,y)) {
            fingers_.emplace(finger,Finger{false,button,button});
            return;
        }
    }
}
void TouchControls::move(std::int64_t finger,int x,int y) {
    auto it=fingers_.find(finger);
    if (it==fingers_.end()) return;
    auto& state=it->second;
    if (state.pad) state.held=direction(layout_.pad,x,y);
    else state.held=contains(layout_.hit[state.button],x,y) ? state.button : Action::count;
}
PerAction<bool> TouchControls::held() const {
    PerAction<bool> result{};
    for (const auto& entry:fingers_) {
        const auto action=entry.second.held;
        if (action!=Action::count) result[action]=true;
    }
    return result;
}
void draw_touch_cross(const RomFont& font,std::span<std::uint32_t> pixels,Action held) {
    constexpr int size=72,cell=24,scale=3;
    if (pixels.size()!=size*size) throw std::invalid_argument("Expected a 72 by 72 touch cross.");
    std::fill(pixels.begin(),pixels.end(),0);
    // Each arm is a window that runs one tile into the centre. Blacking the
    // centre removes that tile, so the arm's side borders stop at the centre.
    font.window(pixels,size,cell,0,cell,cell+8,-1,menu_style::grey);
    font.window(pixels,size,cell,2*cell-8,cell,cell+8,-1,menu_style::grey);
    font.window(pixels,size,0,cell,cell+8,cell,-1,menu_style::grey);
    font.window(pixels,size,2*cell-8,cell,cell+8,cell,-1,menu_style::grey);
    for (int row=cell;row<2*cell;++row)
        std::fill(pixels.begin()+row*size+cell,pixels.begin()+row*size+2*cell,0xff000000);
    // Join the two-pixel border lines of neighbouring arms at each inner corner.
    for (int i=0;i<3;++i) for (int j=1;j<3;++j) for (bool fx:{false,true}) for (bool fy:{false,true}) {
        auto put=[&](int x,int y){pixels[(fy ? size-1-y : y)*size+(fx ? size-1-x : x)]=menu_style::grey;};
        put(cell+i,cell+j); put(cell+j,cell+i);
    }
    // The menu cursor: the lit pixels right of the border in window tile 0.
    const auto& cursor=font.window_tiles()[0];
    int x0=8,x1=-1,y0=8,y1=-1;
    for (int row=0;row<8;++row) for (int column=3;column<8;++column) if (cursor[row]&(1u<<(7-column))) {
        x0=std::min(x0,column); x1=std::max(x1,column); y0=std::min(y0,row); y1=std::max(y1,row);
    }
    if (x1<0) throw std::runtime_error("The ROM window tiles have no menu cursor.");
    const int length=x1-x0+1,breadth=y1-y0+1; // along and across the pointing axis
    auto arrow=[&](Action action,int cx,int cy) {
        const bool horizontal=action==Action::left || action==Action::right;
        const bool mirror=action==Action::left || action==Action::up;
        const int aw=(horizontal ? length : breadth)*scale,ah=(horizontal ? breadth : length)*scale;
        // Mirrored arrows also mirror the odd pixel of centring.
        const int ox=cx+(mirror && horizontal ? cell-aw-(cell-aw)/2 : (cell-aw)/2);
        const int oy=cy+(mirror && !horizontal ? cell-ah-(cell-ah)/2 : (cell-ah)/2);
        const auto color=action==held ? menu_style::yellow : menu_style::white;
        for (int row=y0;row<=y1;++row) for (int column=x0;column<=x1;++column) {
            if (!(cursor[row]&(1u<<(7-column)))) continue;
            const int along=mirror ? x1-column : column-x0,across=row-y0;
            const int px=ox+(horizontal ? along : across)*scale,py=oy+(horizontal ? across : along)*scale;
            for (int dy=0;dy<scale;++dy) for (int dx=0;dx<scale;++dx) pixels[(py+dy)*size+px+dx]=color;
        }
    };
    arrow(Action::up,cell,0); arrow(Action::down,cell,2*cell);
    arrow(Action::left,0,cell); arrow(Action::right,2*cell,cell);
}
void draw_touch_button(const RomFont& font,std::span<std::uint32_t> pixels,int width,Action action,
                       Language language,unsigned speed,bool pressed) {
    const auto text=label(action,language,speed);
    if (width<int(text.size()+2)*8 || pixels.size()%width || pixels.size()/width<24)
        throw std::invalid_argument("Touch texture too small for its label.");
    const int height=static_cast<int>(pixels.size()/width);
    font.window(pixels,width,0,0,width,height,-1,pressed ? menu_style::yellow : menu_style::grey);
    font.text(pixels,width,(width-static_cast<int>(text.size())*8)/2,(height-8)/2,text,
              pressed ? menu_style::yellow : menu_style::white);
}
}
