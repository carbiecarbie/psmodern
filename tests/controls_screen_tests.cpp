// SPDX-License-Identifier: GPL-3.0-or-later
// The Controls screen (milestone 8): its state machine without SDL (fixed
// navigation, scrolling, press-to-set capture, the swap and refusal messages,
// the commands) and, with a ROM argument, its picture against the cartridge
// font. The ROM run writes frames of several states for inspection.
// Usage: psmodern_controls_tests [<rom> <output-folder>]
#include "app/controls_screen.h"
#include "hw/memory.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace psmodern;
using namespace psmodern::app;
namespace {
void check(bool condition,const std::string& message) { if (!condition) throw std::runtime_error(message); }
constexpr Key X=27, Z=29, K=14, Q=20, ENTER=40, ESCAPE=41, BACKSPACE=42, DELETE_KEY=76, RIGHT=79, LEFT=80, DOWN=81, UP=82, LALT=226, LGUI=227, RALT=230;
constexpr Key UNBINDABLE=200;
InputSnapshot keys(std::initializer_list<Key> list) { InputSnapshot s;for (auto k:list) s.keys[k]=true;return s; }
InputSnapshot key(Key k) { return keys({k}); }
InputSnapshot pad(PadInput p) { InputSnapshot s;s.controller=true;s.pad[std::size_t(p)]=true;return s; }
InputSnapshot stick(int x,int y) { InputSnapshot s;s.controller=true;s.stick_x=x;s.stick_y=y;return s; }
// One screen with a clock: 20 ms per frame unless told otherwise. It starts with nothing held.
struct Rig {
    ControlsScreen screen;double t=0;
    explicit Rig(const Bindings& current=Bindings::defaults()) { screen.open(current);step({}); }
    ControlsAction step(const InputSnapshot& input,double dt=0.02) { t+=dt;return screen.update(input,t); }
    // A press and its release; returns what the press frame answered.
    ControlsAction tap(const InputSnapshot& input) { const auto r=step(input);step({});return r; }
    void go(unsigned row,unsigned column=0) {
        while (screen.row()!=row) tap(key(DOWN));
        if (column!=screen.column()) tap(key(column ? RIGHT : LEFT));
    }
    // Moves to the cell and presses accept; the capture is armed once the accept is released.
    void capture(unsigned row,unsigned column) { step({});go(row,column);tap(key(Z));check(screen.capturing(),"accept starts a capture"); }
    const Bindings& draft() const { return screen.draft(); }
};
void arming() {
    ControlsScreen screen;double now=0;
    check(!screen.visible() && screen.update({},now)==ControlsAction::none,"hidden screen ignores input");
    auto current=Bindings::defaults();assign_key(current,Action::button1,K);
    screen.open(current);
    check(screen.visible() && screen.draft()==current && screen.row()==0 && screen.column()==0 && !screen.capturing() && screen.first_visible()==0,"open copies the bindings");
    check(screen.update(key(ESCAPE),now++)==ControlsAction::none && screen.visible(),"a key held while opening does not close");
    check(screen.update(key(ESCAPE),now++)==ControlsAction::none,"still held");
    check(screen.update({},now++)==ControlsAction::none,"releasing it");
    check(screen.update(key(ESCAPE),now++)==ControlsAction::close && !screen.visible(),"fresh Escape closes");
    for (const auto& back:{key(X),pad(PadInput::b)}) {
        screen.open(current);screen.update({},now++);
        auto unfocused=back;unfocused.focused=false;unfocused.keys.reset();
        check(screen.update(unfocused,now++)==ControlsAction::none,"unfocused snapshot disarms");
        check(screen.update(back,now++)==ControlsAction::none && screen.visible(),"held input after focus loss does not close");
        screen.update({},now++);
        check(screen.update(back,now++)==ControlsAction::close,"fresh back press closes");
    }
    // A stick held out of the dead zone at opening is ignored until it comes
    // back inside it; the other inputs work meanwhile.
    screen.open(current);
    screen.update(stick(20000,0),now++);screen.update(stick(20000,0),now++);
    check(screen.column()==0,"a stick held at opening does not move");
    auto both=stick(20000,0);both.keys[DOWN]=true;screen.update(both,now++);
    check(screen.row()==1 && screen.column()==0,"another input works while it is held");
    screen.update(stick(8000,-8000),now++);
    screen.update(stick(20000,0),now++);check(screen.column()==1,"back inside the dead zone and pushed again, it moves");
    screen.failed();check(screen.visible() && screen.status()=="SAVE FAILED - RETRY","failed stays open");
    screen.saved();check(!screen.visible(),"saved closes");
    screen.open(current);screen.close();check(!screen.visible() && screen.update(key(ESCAPE),now++)==ControlsAction::none,"close hides it");
}
InputSnapshot with(InputSnapshot s,Key k) { s.keys[k]=true;return s; }
void held_inputs() {
    // A key bound to nothing on this screen, held all the time (a stuck key):
    // the screen opens, browses, captures and closes regardless.
    {
        ControlsScreen screen;double now=0;screen.open(Bindings::defaults());
        auto step=[&](const InputSnapshot& s) { now+=0.02;return screen.update(with(s,K),now); };
        step({});step(key(DOWN));check(screen.row()==1,"browses with an unrelated key held");
        step({});step(key(Z));check(screen.capturing(),"accept starts a capture with an unrelated key held");
        step({});step({});check(screen.capturing(),"the key held since before the capture is not captured");
        step(key(Q));check(!screen.capturing() && screen.draft().keys[Action::down]==Q,"a fresh key is");
        step({});step(key(Z));step({});step({});
        now+=0.02;screen.update({},now);now+=0.02;screen.update(key(K),now);
        check(!screen.capturing() && screen.draft().keys[Action::down]==K,"released and pressed again, the stuck key can be captured");
        now+=0.02;screen.update({},now);
        check(step(key(ESCAPE))==ControlsAction::close,"Escape closes with an unrelated key held");
    }
    // The stick stuck down from the opening: that direction counts as not
    // pressed until released, so it neither moves nor repeats; the arrows,
    // the d-pad and the other stick directions work meanwhile.
    {
        ControlsScreen screen;double now=0;screen.open(Bindings::defaults());
        const auto stuck=stick(0,20000);
        for (int i=0;i<200;++i) screen.update(stuck,now+=0.02);
        check(screen.row()==0,"a stick held at opening does not move or repeat");
        screen.update(with(stuck,UP),now+=0.02);check(screen.row()==controls_layout::cancel_row,"the up arrow works with the stick held");
        screen.update(stuck,now+=0.02);
        screen.update(with(stuck,DOWN),now+=0.02);check(screen.row()==0,"the down arrow works with the stick held down");
        auto pad_down=stuck;pad_down.pad[std::size_t(PadInput::dpad_down)]=true;
        screen.update(stuck,now+=0.02);screen.update(pad_down,now+=0.02);check(screen.row()==1,"so does the d-pad");
        for (int i=0;i<100;++i) screen.update(stuck,now+=0.02);
        check(screen.row()==1,"and the stick still does not repeat");
        screen.update(stick(0,0),now+=0.02);screen.update(stuck,now+=0.02);check(screen.row()==2,"released and pushed again, it moves");
    }
    // Any fixed input held at opening is ignored until released: A, Enter, Backspace.
    {
        Rig rig;rig.screen.open(Bindings::defaults());
        rig.step(pad(PadInput::a));rig.step(pad(PadInput::a));check(!rig.screen.capturing(),"A held at opening does not accept");
        rig.step({});rig.step(pad(PadInput::a));check(rig.screen.capturing(),"pressed again, it does");
        rig.screen.open(Bindings::defaults());rig.step(keys({ENTER,BACKSPACE}));rig.step(key(RIGHT));
        check(!rig.screen.capturing() && rig.draft()==Bindings::defaults() && rig.screen.column()==1,"held Enter and Backspace do nothing; a fresh arrow moves");
    }
}
void status_line() {
    Rig rig;
    check(rig.screen.status()=="NO CONTROLLER FOUND","idle status without a controller");
    rig.step(pad(PadInput::dpad_down));check(rig.screen.status()=="CONTROLLER CONNECTED","idle status with a controller");
    rig.step({});check(rig.screen.status()=="NO CONTROLLER FOUND","and back");
}
void navigation() {
    // Rows move one per press, wrap, and the view of 7 rows follows the cursor.
    Rig rig;
    for (unsigned row=1;row<controls_layout::rows;++row) {
        rig.tap(key(DOWN));
        check(rig.screen.row()==row,"down moves one row");
        check(rig.screen.first_visible()==(row<controls_layout::visible_rows ? 0 : row+1-controls_layout::visible_rows),"the view scrolls only when the cursor leaves it");
    }
    check(rig.screen.first_visible()==9,"the last row shows rows 9 to 15");
    rig.tap(key(DOWN));check(rig.screen.row()==0 && rig.screen.first_visible()==0,"down from CANCEL wraps to the first row and the view jumps to the top");
    rig.tap(key(UP));check(rig.screen.row()==15 && rig.screen.first_visible()==9,"up from the first row wraps to CANCEL and the view jumps to the bottom");
    for (unsigned row=14;row>=9;--row) { rig.tap(key(UP));check(rig.screen.row()==row && rig.screen.first_visible()==9,"going up inside the view does not scroll"); }
    rig.tap(key(UP));check(rig.screen.row()==8 && rig.screen.first_visible()==8,"going up past the view scrolls one row");
    // The cursor is always inside the view, whatever the walk.
    unsigned seed=12345;
    for (int i=0;i<2000;++i) {
        seed=seed*1103515245u+12345u;
        rig.tap(key((seed>>16)&1 ? UP : DOWN));
        const auto row=rig.screen.row(),top=rig.screen.first_visible();
        check(row<controls_layout::rows && top<=row && row<top+controls_layout::visible_rows && top+controls_layout::visible_rows<=controls_layout::rows,"the cursor stays inside the view");
    }
    // Up and down together do not move; left and right switch the column on action rows only.
    Rig both;
    both.step(keys({UP,DOWN}));check(both.screen.row()==0,"up and down together do not move");
    both.step({});
    both.tap(key(RIGHT));check(both.screen.column()==1,"right selects the controller column");
    both.tap(key(RIGHT));check(both.screen.column()==1,"right again stays");
    both.tap(key(LEFT));check(both.screen.column()==0,"left selects the keyboard column");
    both.step(keys({LEFT,RIGHT}));check(both.screen.column()==0,"left and right together do nothing");
    both.step({});
    both.go(controls_layout::restore_row);both.tap(key(RIGHT));check(both.screen.column()==0,"left and right do nothing on a command row");
}
void navigation_devices() {
    // Whatever the bindings: even with the arrows' actions moved to other keys and no pad input bound.
    auto odd=Bindings::defaults();
    for (const auto a:{Action::up,Action::down,Action::left,Action::right,Action::button1,Action::button2})
        assign_key(odd,a,Key(20+unsigned(a)));
    for (std::size_t i=0;i<action_count;++i) odd.pads[i].reset();
    for (const auto& bindings:{Bindings::defaults(),odd}) {
        struct Case { const char* name;InputSnapshot down,up,right,left; };
        const Case cases[]={{"arrow keys",key(DOWN),key(UP),key(RIGHT),key(LEFT)},
            {"d-pad",pad(PadInput::dpad_down),pad(PadInput::dpad_up),pad(PadInput::dpad_right),pad(PadInput::dpad_left)},
            {"left stick",stick(0,20000),stick(0,-20000),stick(20000,0),stick(-20000,0)}};
        for (const auto& c:cases) {
            Rig rig(bindings);
            rig.tap(c.down);check(rig.screen.row()==1,std::string(c.name)+" moves down");
            rig.tap(c.up);check(rig.screen.row()==0,std::string(c.name)+" moves up");
            rig.tap(c.right);check(rig.screen.column()==1,std::string(c.name)+" moves right");
            rig.tap(c.left);check(rig.screen.column()==0,std::string(c.name)+" moves left");
        }
        for (const auto& accept:{key(ENTER),key(Z),pad(PadInput::a)}) {
            Rig rig(bindings);rig.tap(accept);check(rig.screen.capturing(),"Enter, Z and A accept");
        }
        for (const auto& back:{key(ESCAPE),key(X),pad(PadInput::b)}) {
            Rig rig(bindings);check(rig.step(back)==ControlsAction::close,"Escape, X and B go back");
        }
        for (const auto& clear:{key(BACKSPACE),key(DELETE_KEY),pad(PadInput::x)}) {
            Rig rig(bindings);rig.tap(key(RIGHT));
            rig.tap(clear);
            check(!rig.draft().pads[Action::up],"Backspace, Delete and X clear the cell");
            if (bindings.pads[Action::up]) check(rig.screen.status()=="CLEARED","clearing a set cell says so");
        }
        // Not navigation: a key bound to nothing here, the stick inside the dead zone, other pad buttons.
        Rig rig(bindings);
        rig.tap(key(K));rig.tap(stick(7000,7000));rig.tap(pad(PadInput::start));rig.tap(pad(PadInput::y));
        check(rig.screen.row()==0 && rig.screen.column()==0 && !rig.screen.capturing(),"other inputs do nothing");
    }
}
void repeat() {
    // OptionsMenu's repeat: first move at once, the next after .35 s, then every .12 s.
    ControlsScreen screen;screen.open(Bindings::defaults());screen.update({},0);
    screen.update(key(DOWN),10);check(screen.row()==1,"fresh press moves");
    screen.update(key(DOWN),10.34);check(screen.row()==1,"no repeat before .35 s");
    screen.update(key(DOWN),10.36);check(screen.row()==2,"repeat after .35 s");
    screen.update(key(DOWN),10.47);check(screen.row()==2,"no repeat before .12 s");
    screen.update(key(DOWN),10.49);check(screen.row()==3,"repeat every .12 s");
    screen.update({},10.5);screen.update(key(DOWN),10.51);check(screen.row()==4,"a new press moves at once");
    // The stick repeats like a direction.
    screen.update({},11.9);screen.update(stick(0,20000),12);check(screen.row()==5,"a pushed stick moves at once");
    screen.update(stick(0,20000),12.2);check(screen.row()==5,"and waits for the first repeat");
    screen.update(stick(0,20000),12.36);check(screen.row()==6,"then repeats");
}
void capture_keys() {
    Rig rig;
    rig.capture(4,0);   // BUTTON 1 keyboard
    check(rig.screen.status()=="PRESS A KEY FOR BUTTON 1","the prompt names the action");
    rig.step({});check(rig.screen.capturing(),"nothing pressed: still waiting");
    rig.step(key(K));
    check(!rig.screen.capturing() && rig.draft().keys[Action::button1]==K && rig.draft().keys[Action::map]==Key(16),"a key sets the entry");
    check(rig.screen.status()=="CONTROLLER CONNECTED" || rig.screen.status()=="NO CONTROLLER FOUND","no message for a plain set");
    // The accept that started it is not the answer, even held: Z (accept) stays down for a while.
    Rig held;held.go(4);
    held.step(key(Z));check(held.screen.capturing(),"accept pressed");
    held.step(key(Z));held.step(key(Z));check(held.screen.capturing() && held.draft().keys[Action::button1]==Z,"the held accept is not captured");
    held.step({});held.step(key(Q));check(held.draft().keys[Action::button1]==Q,"the first fresh key after the release is");
    // After a capture, navigation inputs still held count only once released.
    held.step(keys({Q,DOWN}));check(held.screen.row()==4,"nothing moves while the new key is still held");
    held.step({});held.step(key(DOWN));check(held.screen.row()==5,"then it browses again");
    // The lowest of several fresh keys decides.
    Rig two;two.capture(5,0);two.step(keys({Q,K}));check(two.draft().keys[Action::button2]==K,"the lowest new key wins");
    // Return is an ordinary key without Alt.
    Rig enter;enter.capture(5,0);enter.step(key(ENTER));
    check(enter.draft().keys[Action::button2]==ENTER && enter.draft().keys[Action::pause]==X,"Return can be bound; it swaps with Pause");
    check(enter.screen.status()=="SWAPPED WITH PAUSE","and says so");
}
void capture_pads() {
    Rig rig;
    rig.capture(4,1);   // BUTTON 1 controller
    check(rig.screen.status()=="PRESS BUTTON FOR BUTTON 1","the controller prompt");
    rig.step(stick(32000,-32000));check(rig.screen.capturing(),"stick movement is not an answer");
    rig.step({});
    auto both=pad(PadInput::y);both.pad[std::size_t(PadInput::back)]=true;
    rig.step(both);
    check(!rig.screen.capturing() && rig.draft().pads[Action::button1]==PadInput::y && rig.draft().pads[Action::options]==PadInput::a,"the lowest new input wins and swaps");
    Rig trigger;trigger.capture(11,1);trigger.step(pad(PadInput::right_trigger));
    check(trigger.draft().pads[Action::power]==PadInput::right_trigger,"a trigger can be bound");
    check(trigger.screen.status()=="CONTROLLER CONNECTED" && !trigger.screen.capturing(),"a free input is a plain set");
}
void capture_messages() {
    {   // Swap names the other action.
        Rig rig;rig.capture(1,0);rig.step(key(UP));
        check(rig.draft().keys[Action::down]==UP && rig.draft().keys[Action::up]==DOWN,"a key in use swaps");
        check(rig.screen.status()=="SWAPPED WITH UP","the swap is named");
        rig.step({},1.9);check(rig.screen.status()=="SWAPPED WITH UP","the message lasts 2 s");
        rig.step({},0.2);check(rig.screen.status()=="NO CONTROLLER FOUND","then the idle line returns");
    }
    {   // A key bound to a required action cannot move to an action without a key.
        Rig rig;rig.go(6);rig.tap(key(BACKSPACE));check(!rig.draft().keys[Action::pause] && rig.screen.status()=="CLEARED","a free action's key can be cleared");
        const auto before=rig.draft();
        rig.capture(6,0);rig.step(key(UP));
        check(rig.draft()==before && !rig.screen.capturing(),"a refused swap changes nothing");
        check(rig.screen.status()=="KEY USED BY UP","and names the action that needs the key");
        // An action without a key can take a free key.
        rig.capture(6,0);rig.step(key(K));check(rig.draft().keys[Action::pause]==K,"a free key is a plain set");
    }
    {   // Required keys cannot be emptied; controller inputs can.
        Rig rig;rig.tap(key(BACKSPACE));
        check(rig.draft().keys[Action::up]==UP && rig.screen.status()=="UP NEEDS A KEY","a required key is not cleared");
        rig.go(5);rig.tap(key(DELETE_KEY));check(rig.draft().keys[Action::button2] && rig.screen.status()=="BUTTON 2 NEEDS A KEY","button 2 either");
        rig.tap(key(RIGHT));rig.tap(pad(PadInput::x));
        check(!rig.draft().pads[Action::button2] && rig.screen.status()=="CLEARED","its controller input can be");
        rig.tap(pad(PadInput::x));check(!rig.draft().pads[Action::button2],"clearing an empty cell changes nothing");
    }
    {   // The fullscreen key is fixed; its controller input is free.
        Rig rig;rig.go(12);rig.tap(key(Z));
        check(!rig.screen.capturing() && rig.screen.status()=="ALT ENTER IS FIXED","the fullscreen key cell cannot be captured");
        rig.tap(key(BACKSPACE));check(!rig.draft().keys[Action::fullscreen] && rig.screen.status()=="ALT ENTER IS FIXED","nor cleared");
        rig.tap(key(RIGHT));rig.capture(12,1);rig.step(pad(PadInput::a));
        check(rig.draft().pads[Action::fullscreen]==PadInput::a && !rig.draft().pads[Action::button1] && rig.screen.status()=="SWAPPED WITH BUTTON 1","the fullscreen controller input swaps like any other");
    }
    {   // The longest message still fits the status line.
        Rig rig;rig.capture(0,1);rig.step(pad(PadInput::dpad_down));
        check(rig.screen.status()=="SWAPPED WITH DOWN","a controller swap");
    }
}
void capture_cancel() {
    {   // A press on the other device cancels, and the screen re-arms.
        Rig rig;rig.capture(4,0);const auto before=rig.draft();
        rig.step(pad(PadInput::a));
        check(!rig.screen.capturing() && rig.draft()==before,"a controller press cancels a keyboard capture");
        rig.step(pad(PadInput::dpad_down));check(rig.screen.row()==4,"the press that cancelled does not browse");
        rig.step({});rig.step(key(DOWN));check(rig.screen.row()==5,"browsing resumes after a release");
    }
    {
        Rig rig;rig.capture(4,1);const auto before=rig.draft();
        rig.step(key(K));
        check(!rig.screen.capturing() && rig.draft()==before,"a key press cancels a controller capture");
    }
    {   // Five seconds without an answer.
        Rig rig;rig.capture(4,0);
        rig.step({},4.8);check(rig.screen.capturing(),"still waiting before 5 s");
        rig.step({},0.3);check(!rig.screen.capturing() && rig.screen.status()=="NO CONTROLLER FOUND","cancelled after 5 s");
        rig.step({});rig.tap(key(DOWN));check(rig.screen.row()==5,"and browsing again");
    }
    {   // The 5 s count starts with the capture, even while waiting for the release.
        Rig rig;rig.go(4);rig.step(key(Z));rig.step(key(Z),5.1);check(!rig.screen.capturing(),"the timeout also covers a key held all the time");
    }
    {   // Losing focus abandons the capture and keeps the draft.
        Rig rig;rig.capture(4,0);const auto before=rig.draft();
        auto lost=InputSnapshot();lost.focused=false;rig.step(lost);
        check(!rig.screen.capturing() && rig.draft()==before,"focus loss abandons the capture");
        rig.step(key(K));check(rig.draft()==before && !rig.screen.capturing(),"a key held when focus returns is ignored");
    }
    {   // Alt, the Windows keys and Alt+Return are never an answer.
        Rig rig;rig.capture(4,0);const auto before=rig.draft();
        for (const auto& ignored:{key(LALT),key(RALT),key(LGUI),keys({LALT,ENTER}),keys({RALT,ENTER})}) {
            rig.step(ignored);rig.step({});
            check(rig.screen.capturing() && rig.draft()==before && rig.screen.status()=="PRESS A KEY FOR BUTTON 1","Alt, GUI and Alt+Return are ignored");
        }
        rig.step(key(UNBINDABLE));
        check(rig.screen.capturing() && rig.draft()==before && rig.screen.status()=="KEY NOT AVAILABLE","a key outside the table is refused and the wait goes on");
        rig.step({},2.1);check(rig.screen.status()=="PRESS A KEY FOR BUTTON 1","the prompt returns after the message");
        rig.step(key(K));check(rig.draft().keys[Action::button1]==K,"a good key still works");
        Rig pads;pads.capture(4,1);pads.step(key(LALT));check(pads.screen.capturing(),"Alt does not cancel a controller capture either");
    }
}
void commands() {
    {   // RESTORE DEFAULTS, then SAVE hands the draft to the owner.
        Rig rig;rig.capture(1,0);rig.step(key(K));
        check(rig.draft()!=Bindings::defaults(),"the draft changed");
        rig.step({});rig.go(controls_layout::restore_row);
        check(rig.tap(key(Z))==ControlsAction::none && rig.draft()==Bindings::defaults() && rig.screen.status()=="DEFAULTS - NOT SAVED YET","restore defaults");
        rig.step({});check(rig.screen.status()=="DEFAULTS - NOT SAVED YET","the message stays until the next press");
        rig.step(key(DOWN));check(rig.screen.row()==controls_layout::save_row && rig.screen.status()=="NO CONTROLLER FOUND","a press clears it");
        rig.step({});
        check(rig.step(key(Z))==ControlsAction::save && rig.screen.visible(),"SAVE asks the owner to write and stays open until told");
        check(rig.draft()==Bindings::defaults(),"the draft is what the owner writes");
        rig.screen.failed();check(rig.screen.visible() && rig.screen.status()=="SAVE FAILED - RETRY","failed keeps the screen");
        rig.step({});check(rig.screen.status()=="SAVE FAILED - RETRY","and the message");
        check(rig.step(key(Z))==ControlsAction::save,"SAVE can be tried again");
        rig.screen.saved();check(!rig.screen.visible(),"saved closes the screen");
    }
    {   // CANCEL and back discard the draft.
        auto current=Bindings::defaults();assign_key(current,Action::map,Q);
        Rig rig(current);rig.capture(0,0);rig.step(key(K));rig.step({});
        rig.go(controls_layout::cancel_row);
        check(rig.step(key(Z))==ControlsAction::close && !rig.screen.visible(),"CANCEL closes");
        rig.screen.open(current);check(rig.draft()==current,"the next opening starts from the current bindings");
        Rig back(current);back.capture(0,0);back.step(key(K));back.step({});
        check(back.step(key(ESCAPE))==ControlsAction::close && !back.screen.visible(),"back closes");
        check(back.draft().keys[Action::up]==K,"the draft the owner never saw is simply dropped");
    }
    {   // Accept on a command row never captures; a held accept after SAVE is not a second SAVE.
        Rig rig;rig.go(controls_layout::save_row);
        check(rig.step(key(Z))==ControlsAction::save && rig.step(key(Z))==ControlsAction::none,"a held accept saves once");
    }
}
// Frames ---------------------------------------------------------------------------------------
constexpr std::uint32_t BLACK=0xff000000, WHITE=0xffffffff, GREY=0xffaaaaaa, YELLOW=0xffffff00;
using Frame=std::vector<std::uint32_t>;
Frame draw(const ControlsScreen& screen,const RomFont& font) {
    Frame pixels(256*192,0xff123456);screen.render(pixels,font);return pixels;
}
bool blank(const Frame& f,int x0,int y0,int x1,int y1) {
    for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x) if (f[y*256+x]!=BLACK) return false;
    return true;
}
// Every lit pixel of the text drawn alone is in the frame, in that colour.
bool shows(const Frame& f,int x,int y,std::string_view text,std::uint32_t color,const RomFont& font) {
    Frame own(256*192,0);font.text(own,x,y,text,color);
    bool any=false;
    for (std::size_t i=0;i<own.size();++i) if (own[i]) { any=true;if (f[i]!=color) return false; }
    return any;
}
void write_ppm(const std::filesystem::path& file,const Frame& pixels) {
    std::ofstream ppm(file,std::ios::binary);
    ppm<<"P6\n256 192\n255\n";
    for (auto p:pixels) ppm.put(char(p>>16&255)).put(char(p>>8&255)).put(char(p&255));
    check(bool(ppm),file.string()+" written");
}
// No text row touches another: the tile row under each list row, the rows around the header and above the status line are black.
void check_spacing(const Frame& f,const std::string& state) {
    namespace l=controls_layout;
    for (unsigned i=0;i<l::visible_rows;++i)
        check(blank(f,8,l::first_row_y+l::row_step*int(i)+8,248,l::first_row_y+l::row_step*int(i)+16),state+": a blank tile row follows list row "+std::to_string(i));
    check(blank(f,8,16,248,24) && blank(f,8,32,248,40) && blank(f,8,160,248,168) && blank(f,8,176,248,184),state+": blank rows around the header, hints and status");
    for (auto p:f) check(p==BLACK || p==WHITE || p==GREY || p==YELLOW,state+": only black, white, grey and yellow");
}
void rom(const std::filesystem::path& path,const std::filesystem::path& output) {
    namespace l=controls_layout;
    std::ifstream file(path,std::ios::binary);
    const Memory::Rom bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
    const RomFont font(bytes);
    auto drawable=[&](std::string_view text,const std::string& what,std::size_t limit=0) {
        check(!text.empty(),what+" is empty");
        for (char c:text) check(font.supports(c),what+" uses a character missing from the ROM font: "+std::string(text));
        check(!limit || text.size()<=limit,what+" is too long: "+std::string(text));
    };
    for (std::size_t i=0;i<action_count;++i) {
        const std::string name(action_label(Action(i)));
        drawable(name,"action label",11);
        for (const auto* prefix:{"PRESS A KEY FOR ","PRESS BUTTON FOR ","SWAPPED WITH ","KEY USED BY ","BUTTON USED BY "}) drawable(prefix+name,"status",28);
        drawable(name+" NEEDS A KEY","status",28);
    }
    for (const auto& entry:key_table) drawable(entry.token,"key token",9);
    for (std::size_t i=0;i<pad_input_count;++i) drawable(pad_label(PadInput(i)),"pad label",7);
    for (auto text:{fixed_fullscreen_label,unbound_label,std::string_view("CONTROLS"),std::string_view("KEYBOARD"),std::string_view("GAMEPAD"),
                    std::string_view("RESTORE DEFAULTS"),std::string_view("SAVE"),std::string_view("CANCEL"),std::string_view("MORE ABOVE"),std::string_view("MORE BELOW"),
                    std::string_view("CONTROLLER CONNECTED"),std::string_view("NO CONTROLLER FOUND"),std::string_view("SAVE FAILED - RETRY"),std::string_view("KEY NOT AVAILABLE"),
                    std::string_view("ALT ENTER IS FIXED"),std::string_view("CLEARED"),std::string_view("DEFAULTS - NOT SAVED YET")})
        drawable(text,"screen text",28);

    std::filesystem::create_directories(output);
    // Top of the list: rows 0 to 6, the cursor on UP's keyboard cell, a hint for the rows below only.
    Rig top;
    auto frame=draw(top.screen,font);
    check(frame[0]!=0xff123456 && frame.back()!=0xff123456 && std::none_of(frame.begin(),frame.end(),[](auto p) { return p==0xff123456; }),"the window covers the whole picture");
    check_spacing(frame,"top");
    check(shows(frame,l::title_x,l::title_y,"CONTROLS",WHITE,font) && shows(frame,l::key_header_x,l::header_y,"KEYBOARD",WHITE,font) && shows(frame,l::pad_header_x,l::header_y,"GAMEPAD",WHITE,font),"title and headers");
    const auto defaults=Bindings::defaults();
    for (unsigned i=0;i<l::visible_rows;++i) {
        const auto a=Action(i);const int y=l::first_row_y+l::row_step*int(i);
        const bool selected=i==0;
        check(shows(frame,l::action_x,y,action_label(a),WHITE,font),"action label of row "+std::to_string(i));
        check(shows(frame,l::key_x,y,key_token(*defaults.keys[a]),selected ? YELLOW : WHITE,font),"key of row "+std::to_string(i));
        check(shows(frame,l::pad_x,y,pad_label(*defaults.pads[a]),WHITE,font),"pad input of row "+std::to_string(i));
    }
    check(shows(frame,l::hint_x,l::hint_below_y,"MORE BELOW",GREY,font) && blank(frame,l::hint_x,l::hint_above_y,100,l::hint_above_y+8),"top: a hint below, none above");
    check(shows(frame,l::status_x,l::status_y,"NO CONTROLLER FOUND",WHITE,font),"the status line");
    write_ppm(output/"controls.ppm",frame);

    // Scrolled to the bottom: rows 9 to 15 with the fullscreen key grey, a hint above only.
    Rig bottom;bottom.tap(key(UP));
    frame=draw(bottom.screen,font);check_spacing(frame,"bottom");
    check(bottom.screen.row()==15 && bottom.screen.first_visible()==9,"the bottom frame is scrolled");
    check(shows(frame,l::hint_x,l::hint_above_y,"MORE ABOVE",GREY,font) && blank(frame,l::hint_x,l::hint_below_y,100,l::hint_below_y+8),"bottom: a hint above, none below");
    check(shows(frame,l::key_x,l::first_row_y+l::row_step*3,fixed_fullscreen_label,GREY,font) && shows(frame,l::pad_x,l::first_row_y+l::row_step*3,unbound_label,GREY,font),"fullscreen: fixed key and no controller input in grey");
    check(shows(frame,l::action_x,l::first_row_y+l::row_step*6,"CANCEL",YELLOW,font) && shows(frame,l::action_x,l::first_row_y+l::row_step*5,"SAVE",WHITE,font) &&
          shows(frame,l::action_x,l::first_row_y+l::row_step*4,"RESTORE DEFAULTS",WHITE,font),"the three commands, CANCEL selected");
    write_ppm(output/"controls-bottom.ppm",frame);

    // Mid list on the controller column: hints on both sides.
    Rig middle;middle.go(9,1);
    frame=draw(middle.screen,font);check_spacing(frame,"middle");
    check(middle.screen.first_visible()==3 && shows(frame,l::hint_x,l::hint_above_y,"MORE ABOVE",GREY,font) && shows(frame,l::hint_x,l::hint_below_y,"MORE BELOW",GREY,font),"middle: hints on both sides");
    check(shows(frame,l::pad_x,l::first_row_y+l::row_step*6,"Y",YELLOW,font),"the selected controller cell is yellow");
    write_ppm(output/"controls-middle.ppm",frame);

    // Capturing: the cell is blank and the prompt shown.
    Rig capturing;capturing.capture(4,0);
    frame=draw(capturing.screen,font);check_spacing(frame,"capturing");
    check(blank(frame,l::key_x,l::first_row_y+l::row_step*4,l::pad_x-8,l::first_row_y+l::row_step*4+8),"the cell being set is blank");
    check(shows(frame,l::pad_x,l::first_row_y+l::row_step*4,"A",WHITE,font) && shows(frame,l::action_x,l::first_row_y+l::row_step*4,"BUTTON 1",WHITE,font),"the rest of the row stays");
    check(shows(frame,l::status_x,l::status_y,"PRESS A KEY FOR BUTTON 1",WHITE,font),"the prompt");
    write_ppm(output/"controls-capture.ppm",frame);

    // A status message after a swap.
    Rig swapped;swapped.capture(1,0);swapped.step(key(UP));
    frame=draw(swapped.screen,font);check_spacing(frame,"swapped");
    check(shows(frame,l::status_x,l::status_y,"SWAPPED WITH UP",WHITE,font),"the swap message");
    check(shows(frame,l::key_x,l::first_row_y+l::row_step,"UP",YELLOW,font) && shows(frame,l::key_x,l::first_row_y,"DOWN",WHITE,font),"the swapped keys are drawn");
    write_ppm(output/"controls-status.ppm",frame);

    // Every cursor position, both columns, with a capture, and the longest cells never break the spacing.
    auto full=Bindings::defaults();
    for (std::size_t i=0;i<action_count;++i) if (!key_fixed(Action(i))) full.keys[i]=key_table[(i*7)%key_table_size].key;
    Rig sweep(full);
    for (unsigned step=0;step<2*l::rows;++step) {
        for (unsigned column=0;column<2;++column) {
            if (sweep.screen.column()!=column) sweep.tap(key(column ? RIGHT : LEFT));
            check_spacing(draw(sweep.screen,font),"sweep row "+std::to_string(sweep.screen.row()));
        }
        sweep.tap(key(DOWN));
    }
    // Hidden screens may still be asked to draw.
    ControlsScreen closed;closed.open(Bindings::defaults());closed.close();
    check(draw(closed,font).size()==256*192,"a closed screen draws without error");
}
}

int main(int argc,char** argv) { try {
    if (argc>1) {
        if (!std::filesystem::is_regular_file(argv[1])) return 77;
        rom(argv[1],argc>2 ? argv[2] : ".");
        std::cout<<"Controls screen: every label is in the ROM font; rows never touch; frames written.\n";
        return 0;
    }
    arming();held_inputs();status_line();navigation();navigation_devices();repeat();
    capture_keys();capture_pads();capture_messages();capture_cancel();commands();
    std::cout<<"Controls screen: 11 test groups passed (no SDL or ROM).\n";
    return 0;
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
