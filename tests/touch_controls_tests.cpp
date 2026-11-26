// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/touch_controls.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace psmodern::app;
namespace {
void check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
bool inside(WindowRect r,WindowRect bounds) {
    return r.x>=bounds.x && r.y>=bounds.y && r.x+r.width<=bounds.x+bounds.width &&
           r.y+r.height<=bounds.y+bounds.height;
}
void layouts() {
    const WindowBorders insets{50,17,30,20};
    for (auto size:{WindowRect{0,0,1080,2340},WindowRect{0,0,2340,1080},WindowRect{0,0,210,270}}) {
        WindowRect safe{17,50,size.width-37,size.height-80};
        for (bool wide:{false,true}) {
            auto layout=touch_layout(size.width,size.height,insets,wide,true,true);
            check(inside(layout.game,safe) && layout.game.height>0,"visible game fits safe area");
            const int canvas=canvas_width(wide);
            check(std::abs(layout.game.width*192-layout.game.height*canvas)<canvas,"canvas keeps aspect ratio");
            if (layout.game.width>=canvas)
                check(layout.game.width%canvas==0 && layout.game.height%192==0,"integer upscale");
            for (std::size_t i=0;i<action_count;++i) {
                const auto button=layout.buttons[i];
                if (!button.width) continue;
                check(inside(button,safe),"all drawn controls avoid insets");
                if (i>std::size_t(Action::right))
                    check(inside(button,layout.hit[i]) && inside(layout.hit[i],size),"touch area holds its button and stays on screen");
                else check(inside(button,layout.pad),"directions are cells of the cross");
                if (size.height>size.width)
                    check(!layout.overlay && button.y>=layout.game.y+layout.game.height,"portrait controls below playfield");
            }
            check(inside(layout.pad,layout.pad_hit) && inside(layout.pad_hit,size),"cross touch area holds the cross");
            for (std::size_t i=std::size_t(Action::button1);i<action_count;++i) {
                const auto a=layout.hit[i];
                if (!a.width) continue;
                auto apart=[&](WindowRect o){return a.x+a.width<=o.x || o.x+o.width<=a.x || a.y+a.height<=o.y || o.y+o.height<=a.y;};
                check(apart(layout.pad_hit),"cross touch area is clear of the buttons");
                for (std::size_t j=i+1;j<action_count;++j)
                    check(!layout.hit[j].width || apart(layout.hit[j]),"button touch areas do not overlap");
            }
        }
    }
    auto normal=touch_layout(1920,1080,{},false,true,false);
    check(!normal.overlay && normal.pad.x+normal.pad.width<=normal.game.x,"normal landscape uses side bars");
    check(!normal.buttons[Action::map].width,"automap disabled hides map shortcut");
    auto wide=touch_layout(1920,1080,{},true,true,true);
    check(wide.overlay && wide.buttons[Action::map].width,"wide landscape overlays controls and exposes enabled map");
    // Maintainer decision, 2026-11-19: Widescreen never shrinks the controls.
    for (auto size:{WindowRect{0,0,2340,1080},WindowRect{0,0,3120,1440},WindowRect{0,0,1920,1080}}) {
        for (auto insets:{WindowBorders{},WindowBorders{0,95,0,0}}) {
            const auto narrow=touch_layout(size.width,size.height,insets,false,true,true);
            const auto widened=touch_layout(size.width,size.height,insets,true,true,true);
            check(narrow.buttons==widened.buttons && narrow.pad==widened.pad && narrow.hit==widened.hit,
                  "landscape controls are the same with Widescreen on and off");
            check(!narrow.overlay && narrow.pad.x+narrow.pad.width<=narrow.game.x,"cross fits beside the 256-wide picture");
            const int centre=widened.game.x+widened.game.width*44/344,centre_end=widened.game.x+widened.game.width*300/344;
            for (auto button:widened.buttons)
                check(!button.width || button.x+button.width<=centre || button.x>=centre_end,
                      "wide controls reach only the 44 extra columns");
            check(narrow.buttons[Action::up].width*4>=narrow.game.x-insets.left,"each direction is at least a quarter of the side bar");
            check(narrow.buttons[Action::pause].y+narrow.buttons[Action::pause].height<narrow.pad.y &&
                  narrow.buttons[Action::pause].y<size.height/8,"shortcuts sit in the top corners, away from the cross");
        }
    }
    const auto phone=touch_layout(2340,1080,{},true,true,true);
    for (auto size:{WindowRect{0,0,1280,960},WindowRect{0,0,1440,1080},
                    WindowRect{0,0,300,240},WindowRect{0,0,306,208}}) {
        for (bool widescreen:{false,true}) {
            auto compact=touch_layout(size.width,size.height,{},widescreen,true,true);
            const int canvas=canvas_width(widescreen);
            const int border=(canvas-canvas_width(false))/2;
            const int first=compact.game.x+compact.game.width*border/canvas;
            const int last=compact.game.x+compact.game.width*(canvas-border)/canvas;
            check(compact.game.width>0 && compact.pad.x+compact.pad.width<=first,
                  "compact landscape cross stays clear of text");
            for (auto button:compact.buttons)
                check(!button.width || button.x+button.width<=first || button.x>=last,
                      "compact landscape buttons stay outside the central picture");
        }
    }
    check(phone.buttons[Action::up].width>=160 && phone.buttons[Action::button1].width>=235,
          "2340x1080 wide landscape: about 11 mm per direction and 16 mm buttons on a 6.8 inch screen");
    const auto upright=touch_layout(1080,2340,{},false,true,true);
    check(upright.game.y<upright.game.height/8 && upright.pad.y+upright.pad.height<2340-upright.pad.height/3,
          "portrait: picture at the top, cross raised off the bottom edge");
    auto start=touch_layout(1080,1920,{},false,false,true);
    for (auto a:{Action::map,Action::pause,Action::speed,Action::options,Action::power,Action::reset})
        check(!start.buttons[a].width,"launcher hides gameplay shortcuts");
    check(!touch_layout(0,0,{},false,true,true).game.width,"no drawable surface gives empty layout");
}
void fingers() {
    TouchControls controls;
    const auto layout=touch_layout(1080,1920,{},false,true,true);
    controls.set_layout(layout);
    std::uint32_t ms=100000;
    auto press=[&](std::int64_t finger,Action action) {
        auto r=layout.buttons[action];
        controls.down(finger,r.x+r.width/2,r.y+r.height/2,ms);
    };
    auto slide=[&](std::int64_t finger,WindowRect r) { controls.move(finger,r.x+r.width/2,r.y+r.height/2); };
    press(9000000000LL,Action::up);
    press(2,Action::button1);
    press(3,Action::button1);
    controls.up(2);
    check(controls.held()[Action::up] && controls.held()[Action::button1],"independent contacts including two on one button");
    press(4,Action::left);
    check(!controls.held()[Action::left],"only one thumb owns the d-pad");
    slide(9000000000LL,layout.buttons[Action::right]);
    check(!controls.held()[Action::up] && controls.held()[Action::right],"d-pad slides between directions");
    slide(9000000000LL,layout.pad);
    check(!controls.held()[Action::right],"d-pad center is neutral");
    controls.move(9000000000LL,layout.pad.x+layout.pad.width+500,layout.pad.y+layout.pad.height/2);
    check(controls.held()[Action::right],"a thumb that drifts off the cross keeps its direction");
    controls.up(9000000000LL);
    controls.down(11,layout.pad_hit.x,layout.pad.y+layout.pad.height/2,ms);
    check(controls.held()[Action::left],"a touch at the screen edge beside the cross steers");
    controls.up(11);
    slide(3,layout.buttons[Action::button2]);
    check(!controls.held()[Action::button1] && !controls.held()[Action::button2],"slide off a button releases it");
    press(12,Action::button1);
    controls.controller_input(1,true,ms);
    check(!controls.visible() && !controls.held()[Action::button1],"controller input hides touch targets and releases fingers");
    controls.up(12);
    controls.controller_input(1,false,ms+=touch_wake_quiet_ms);
    press(13,Action::button1);
    check(!controls.visible(),"no wake inside the quiet time after a button release");
    controls.controller_input(2,true,ms);   // a stick pushed beyond the dead zone
    ms+=touch_wake_quiet_ms;
    press(13,Action::button1);
    check(!controls.visible(),"no wake while an axis is beyond the dead zone");
    controls.controller_settled(2);          // neither held nor a new input
    press(13,Action::button1);
    check(controls.visible(),"wake once the stick is back in the dead zone and the quiet time has passed");
    controls.up(13);
    controls.controller_input(3,true,ms);   // a button held down
    ms+=touch_wake_quiet_ms;
    press(13,Action::button1);
    check(!controls.visible(),"no wake while a button is held");
    controls.controller_reset();             // controller removed or app backgrounded
    press(13,Action::button1);
    check(controls.visible() && !controls.held()[Action::button1],"wake after the quiet time once nothing is held");
    controls.up(13);
    controls.controller_input(4,true,ms);
    controls.controller_input(4,false,ms);
    ms+=touch_wake_quiet_ms-1;
    press(13,Action::button1);
    check(!controls.visible(),"quiet time counts to the last input");
    ms+=1;
    press(10,Action::button1);
    slide(10,layout.buttons[Action::button1]);
    check(controls.visible() && !controls.held()[Action::button1],"whole wake gesture consumed");
    controls.up(10);
    press(10,Action::button1);
    check(controls.held()[Action::button1],"next tap works");
    controls.up(10);
    // The wrap of SDL's 32-bit tick count does not block waking.
    controls.controller_input(5,true,0xffffff00u);
    controls.controller_input(5,false,0xffffff00u);
    ms=0xffffff00u+touch_wake_quiet_ms;
    press(14,Action::button1);
    check(controls.visible(),"quiet time measured across the tick wrap");
    controls.up(14);
    press(10,Action::button1);
    controls.set_layout(layout);
    const auto held=controls.held();
    check(std::none_of(held.begin(),held.end(),[](bool value){return value;}),"rotation clears held contacts");
}
void start_state() {
    check(TouchControls{}.visible() && !TouchControls{false}.visible(),"controls start as asked");
    check(touch_start_visible(std::nullopt,false),"first controls are shown without a controller");
    check(!touch_start_visible(std::nullopt,true),"first controls are hidden with a controller connected");
    check(touch_start_visible(true,true) && !touch_start_visible(false,false),"later controls keep the previous state");
    TouchControls hidden(false);
    const auto layout=touch_layout(1080,1920,{},false,true,true);
    hidden.set_layout(layout);
    const auto r=layout.buttons[Action::button1];
    hidden.down(1,r.x+r.width/2,r.y+r.height/2,0);
    check(hidden.visible() && !hidden.held()[Action::button1],"no controller input yet: the first touch wakes and is consumed");
}
void resume_clock() {
    FrameClock clock(0);
    clock.speed(4,1);
    clock.rebase(600);
    check(clock.speed()==4 && clock.due(600)==1 && clock.due(600)==0,"resume runs one frame without a catch-up burst");
    check(clock.discarded_seconds()==0 && clock.deadline()>600,"time in background is not discarded game time");
}
}
int main() {
    try { layouts(); fingers(); start_state(); resume_clock(); }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
