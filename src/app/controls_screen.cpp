// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/controls_screen.h"
#include <iterator>

namespace psmodern::app {
namespace {
using namespace controls_layout;
// Fixed navigation and reserved scancodes, whatever the bindings.
constexpr Key scan_x=27, scan_z=29, key_enter=40, key_escape=41, key_backspace=42, key_delete=76, key_right=79, key_left=80, key_down=81, key_up=82,
    key_lalt=226, key_lgui=227, key_ralt=230, key_rgui=231;
constexpr std::uint32_t white=0xffffffff, grey=0xffaaaaaa, yellow=0xffffff00;
// Every physical source of the fixed navigation, one bit each and three per
// meaning, so a blocked source never hides another source of the same meaning.
std::uint32_t sources(const InputSnapshot& s) {
    auto p=[&](PadInput i) { return bool(s.pad[std::size_t(i)]); };
    const bool held[]={s.keys[key_up],p(PadInput::dpad_up),s.stick_y<-stick_dead_zone,
        s.keys[key_down],p(PadInput::dpad_down),s.stick_y>stick_dead_zone,
        s.keys[key_left],p(PadInput::dpad_left),s.stick_x<-stick_dead_zone,
        s.keys[key_right],p(PadInput::dpad_right),s.stick_x>stick_dead_zone,
        s.keys[key_enter],s.keys[scan_z],p(PadInput::a),
        s.keys[key_escape],s.keys[scan_x],p(PadInput::b),
        s.keys[key_backspace],s.keys[key_delete],p(PadInput::x)};
    std::uint32_t bits=0;
    for (std::size_t i=0;i<std::size(held);++i) bits|=std::uint32_t(held[i])<<i;
    return bits;
}
struct Nav { bool up, down, left, right, accept, back, clear; };
Nav nav(std::uint32_t bits) {
    auto any=[&](unsigned meaning) { return (bits>>(3*meaning)&7u)!=0; };
    return {any(0),any(1),any(2),any(3),any(4),any(5),any(6)};
}
std::string label(Action a) { return std::string(action_label(a)); }
}
void ControlsScreen::open(const Bindings& current) {
    draft_=current; previous_={}; status_.clear(); message_.clear(); row_=column_=top_=0;
    visible_=block_held_=true; capturing_=capture_ready_=sticky_=false; blocked_=held_=previous_held_=0;
    next_repeat_=capture_started_=status_until_=0;
}
void ControlsScreen::notice(std::string text,double now) { message_=std::move(text); status_until_=now+result_seconds; }
ControlsAction ControlsScreen::update(const InputSnapshot& input,double now) {
    if (!visible_) return ControlsAction::none;
    auto result=ControlsAction::none;
    // Losing focus cannot count as observing a release: block whatever is held
    // when focus returns, abandon a capture.
    if (!input.focused) { block_held_=true; capturing_=false; held_=previous_held_=0; }
    else {
        const auto raw=sources(input);
        if (block_held_) { blocked_=raw; block_held_=false; }
        else blocked_&=raw;   // a released source counts again
        held_=raw&~blocked_;
        if (capturing_) capture(input,now);
        else result=browse(input,now);
        previous_held_=held_;
    }
    previous_=input;
    if (!visible_) return result;
    if (sticky_ || now<status_until_) status_=message_;
    else if (capturing_) status_=(column_==0 ? "PRESS A KEY FOR " : "PRESS BUTTON FOR ")+label(Action(row_));
    else status_=input.controller ? "CONTROLLER CONNECTED" : "NO CONTROLLER FOUND";
    return result;
}
void ControlsScreen::capture(const InputSnapshot& input,double now) {
    if (now-capture_started_>=capture_timeout) { end_capture(); return; }
    // The accept that started the capture is not the answer: wait for its
    // release. Only fresh presses answer, so nothing held before is captured.
    if (!capture_ready_) { capture_ready_=!nav(held_).accept; return; }
    const bool alt=input.keys[key_lalt] || input.keys[key_ralt], key_column=column_==0;
    std::optional<Key> key; std::optional<PadInput> pad;
    bool unavailable=false, other_device=false;
    for (std::size_t i=0;i<key_slots;++i) {
        // Alt and the GUI keys are never an answer; Return is Alt+Enter while Alt is held.
        if (!input.keys[i] || previous_.keys[i] || i==key_lalt || i==key_ralt || i==key_lgui || i==key_rgui || (i==key_enter && alt)) continue;
        if (!key_column) other_device=true;
        else if (key_bindable(Key(i))) { if (!key) key=Key(i); }
        else unavailable=true;
    }
    for (std::size_t i=0;i<pad_input_count;++i)
        if (input.pad[i] && !previous_.pad[i]) { if (key_column) other_device=true; else if (!pad) pad=PadInput(i); }
    const auto a=Action(row_);
    ChangeResult result{Change::unchanged};
    if (key_column && key) result=assign_key(draft_,a,*key);
    else if (!key_column && pad) result=assign_pad(draft_,a,*pad);
    else if (other_device) { end_capture(); return; }   // a press on the other device cancels
    else { if (unavailable) notice("KEY NOT AVAILABLE",now); return; }
    switch (result.change) {
    case Change::swapped: notice("SWAPPED WITH "+label(result.other),now); break;
    case Change::refused_in_use: notice((key_column ? "KEY USED BY " : "BUTTON USED BY ")+label(result.other),now); break;
    case Change::refused_required: notice(label(a)+" NEEDS A KEY",now); break;
    case Change::fixed: notice("ALT ENTER IS FIXED",now); break;
    case Change::set: case Change::unchanged: break;
    }
    end_capture();
}
ControlsAction ControlsScreen::browse(const InputSnapshot& input,double now) {
    const Nav n=nav(held_), p=nav(previous_held_);
    const bool accept=n.accept && !p.accept, back=n.back && !p.back, clear=n.clear && !p.clear;
    const bool fresh=(n.up && !p.up) || (n.down && !p.down) || (n.left && !p.left) || (n.right && !p.right);
    const bool pressed=fresh || accept || back || clear || (input.keys & ~previous_.keys).any() || (input.pad & ~previous_.pad).any();
    if (sticky_ && pressed) { sticky_=false; message_.clear(); }
    if (back) { close(); return ControlsAction::close; }
    const bool on_action=row_<action_rows;
    if (accept) {
        if (on_action) {
            if (column_==0 && key_fixed(Action(row_))) notice("ALT ENTER IS FIXED",now);
            else { capturing_=true; capture_ready_=false; capture_started_=now; status_until_=0; }
        } else if (row_==restore_row) { draft_=Bindings::defaults(); message_="DEFAULTS - NOT SAVED YET"; sticky_=true; }
        else if (row_==save_row) return ControlsAction::save;
        else { close(); return ControlsAction::close; }
        return ControlsAction::none;
    }
    if (clear && on_action) {
        const auto a=Action(row_);
        const auto result=column_==0 ? clear_key(draft_,a) : clear_pad(draft_,a);
        if (result.change==Change::set) notice("CLEARED",now);
        else if (result.change==Change::refused_required) notice(label(a)+" NEEDS A KEY",now);
        else if (result.change==Change::fixed) notice("ALT ENTER IS FIXED",now);
        return ControlsAction::none;
    }
    if ((n.up || n.down || n.left || n.right) && (fresh || now>=next_repeat_)) {
        next_repeat_=now+(fresh ? first_repeat : controls_layout::next_repeat);
        if (n.up!=n.down) {
            row_=(row_+(n.down ? 1 : rows-1))%rows;
            // The view follows the cursor; a wrap jumps to the other end of the list.
            if (row_<top_) top_=row_;
            else if (row_>=top_+visible_rows) top_=row_+1-visible_rows;
        }
        if (n.left!=n.right && row_<action_rows) column_=n.right ? 1 : 0;
    }
    return ControlsAction::none;
}
void ControlsScreen::saved() { close(); }
void ControlsScreen::failed() { sticky_=true; message_=status_="SAVE FAILED - RETRY"; }
void ControlsScreen::render(std::span<std::uint32_t> pixels,const RomFont& font) const {
    font.window(pixels,0,0,256,192,first_row_y+row_step*int(row_-top_));
    font.text(pixels,title_x,title_y,"CONTROLS",white);
    font.text(pixels,key_header_x,header_y,"KEYBOARD",white);
    font.text(pixels,pad_header_x,header_y,"GAMEPAD",white);
    if (top_>0) font.text(pixels,hint_x,hint_above_y,"MORE ABOVE",grey);
    if (top_+visible_rows<rows) font.text(pixels,hint_x,hint_below_y,"MORE BELOW",grey);
    for (unsigned i=0;i<visible_rows;++i) {
        const unsigned index=top_+i;const int y=first_row_y+row_step*int(i);
        const bool selected=index==row_;
        if (index>=action_rows) {
            static constexpr const char* commands[]={"RESTORE DEFAULTS","SAVE","CANCEL"};
            font.text(pixels,action_x,y,commands[index-action_rows],selected ? yellow : white);
            continue;
        }
        const auto a=Action(index);
        font.text(pixels,action_x,y,action_label(a),white);
        const bool key_selected=selected && column_==0, pad_selected=selected && column_==1;
        // The cell being captured is blank.
        if (!(capturing_ && key_selected)) {
            if (key_fixed(a)) font.text(pixels,key_x,y,fixed_fullscreen_label,key_selected ? yellow : grey);
            else if (draft_.keys[a]) font.text(pixels,key_x,y,key_token(*draft_.keys[a]),key_selected ? yellow : white);
            else font.text(pixels,key_x,y,unbound_label,key_selected ? yellow : grey);
        }
        if (!(capturing_ && pad_selected)) {
            if (draft_.pads[a]) font.text(pixels,pad_x,y,pad_label(*draft_.pads[a]),pad_selected ? yellow : white);
            else font.text(pixels,pad_x,y,unbound_label,pad_selected ? yellow : grey);
        }
    }
    font.text(pixels,status_x,status_y,status_,white);
}
}
