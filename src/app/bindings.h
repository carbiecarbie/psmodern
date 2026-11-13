// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The player's controls (milestone 8): one key and one controller input per
// action, the rules for changing them, the controls.txt file and the pure
// mapping from held inputs to console, menu and start-screen input. No SDL:
// keys are SDL scancodes (USB HID usage numbers) and pad inputs follow
// SDL_GameControllerButton; sdl_host.cpp asserts both against SDL's enums.
#include "app/host.h"
#include "app/options_menu.h"
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace psmodern::app {
enum class Action : std::uint8_t {
    up, down, left, right, button1, button2, pause, reset,   // console, in Keys order
    speed, options, map, power, fullscreen, count };         // shortcuts
inline constexpr std::size_t action_count=std::size_t(Action::count);
inline constexpr std::size_t console_action_count=8;
using Key=std::uint16_t;                        // SDL scancode, < key_slots
inline constexpr std::size_t key_slots=512;     // == SDL_NUM_SCANCODES
// Same order and values as SDL_GameControllerButton 0..20, then the triggers.
enum class PadInput : std::uint8_t { a, b, x, y, back, guide, start, left_stick, right_stick,
    left_shoulder, right_shoulder, dpad_up, dpad_down, dpad_left, dpad_right,
    misc1, paddle1, paddle2, paddle3, paddle4, touchpad, left_trigger, right_trigger, count };
inline constexpr std::size_t pad_input_count=std::size_t(PadInput::count);
inline constexpr std::size_t pad_button_count=std::size_t(PadInput::left_trigger);
// Trigger axis 0..32767: pressed above half, released only below a quarter,
// so a trigger resting near the press point does not chatter.
inline constexpr int trigger_threshold=16384, trigger_release_threshold=8192;

// One value per action, indexed by Action or by its number.
template<class T> struct PerAction : std::array<T,action_count> {
    using std::array<T,action_count>::operator[];
    T& operator[](Action a) { return (*this)[std::size_t(a)]; }
    const T& operator[](Action a) const { return (*this)[std::size_t(a)]; }
    bool operator==(const PerAction&) const = default;
};
struct Bindings {
    PerAction<std::optional<Key>> keys;        // keys[fullscreen] stays empty (Alt+Enter is fixed)
    PerAction<std::optional<PadInput>> pads;
    static Bindings defaults();                // the 0.2.0 mapping
    bool operator==(const Bindings&) const = default;
};
// set: the input was bound (or a binding cleared); swapped: another action
// held the input and received this action's previous one (other names it);
// unchanged: nothing to do; refused_in_use: the swap would leave the other,
// required action without a key; refused_required: a required key cannot be
// cleared; fixed: the fullscreen key never changes. A refusal changes nothing.
enum class Change { set, swapped, unchanged, refused_in_use, refused_required, fixed };
struct ChangeResult { Change change; Action other=Action::count; };
// A key outside the table (key_bindable false) is refused as unchanged; the
// Controls screen and the file reject such keys before calling.
ChangeResult assign_key(Bindings& bindings,Action action,Key key);
ChangeResult assign_pad(Bindings& bindings,Action action,PadInput input);
ChangeResult clear_key(Bindings& bindings,Action action);
ChangeResult clear_pad(Bindings& bindings,Action action);
bool key_required(Action action);   // up, down, left, right, button1, button2
bool key_fixed(Action action);      // fullscreen (Alt+Enter)
bool key_bindable(Key key);         // in the key table (never Alt, GUI or unknown keys)

// Tokens are the file's words; labels are what the Controls screen draws.
// Every token and label uses A-Z, 0-9 and space only (the ROM font).
std::string_view action_token(Action action);   // "up" ... "button1" ... "power" "fullscreen"
std::string_view action_label(Action action);   // "UP" ... "BUTTON 1" ... "POWER CYCLE" "FULLSCREEN"
std::optional<Key> key_from_token(std::string_view token);   // case-insensitive, bindable keys only
std::string_view key_token(Key key);            // also the screen label; empty when not bindable
std::optional<PadInput> pad_from_token(std::string_view token);   // case-insensitive
std::string_view pad_token(PadInput input);     // SDL's name uppercased: "A" "LEFTSHOULDER" "RIGHTTRIGGER"
std::string_view pad_label(PadInput input);     // short screen label, at most 7 characters: "LB" "D UP"
inline constexpr std::string_view fixed_fullscreen_label="ALT ENTER";
inline constexpr std::string_view unbound_label="NONE";
// Every bindable key, ascending by scancode; sdl_host.cpp asserts each value.
struct KeyName { Key key; std::string_view token; };
inline constexpr KeyName key_table[]={
    {4,"A"},{5,"B"},{6,"C"},{7,"D"},{8,"E"},{9,"F"},{10,"G"},{11,"H"},{12,"I"},{13,"J"},{14,"K"},{15,"L"},{16,"M"},
    {17,"N"},{18,"O"},{19,"P"},{20,"Q"},{21,"R"},{22,"S"},{23,"T"},{24,"U"},{25,"V"},{26,"W"},{27,"X"},{28,"Y"},{29,"Z"},
    {30,"1"},{31,"2"},{32,"3"},{33,"4"},{34,"5"},{35,"6"},{36,"7"},{37,"8"},{38,"9"},{39,"0"},
    {40,"ENTER"},{41,"ESCAPE"},{42,"BACKSPACE"},{43,"TAB"},{44,"SPACE"},{45,"MINUS"},{46,"EQUALS"},{47,"LBRACKET"},
    {48,"RBRACKET"},{49,"BACKSLASH"},{51,"SEMICOLON"},{52,"QUOTE"},{53,"GRAVE"},{54,"COMMA"},{55,"PERIOD"},{56,"SLASH"},
    {57,"CAPSLOCK"},{58,"F1"},{59,"F2"},{60,"F3"},{61,"F4"},{62,"F5"},{63,"F6"},{64,"F7"},{65,"F8"},{66,"F9"},{67,"F10"},
    {68,"F11"},{69,"F12"},{73,"INSERT"},{74,"HOME"},{75,"PAGEUP"},{76,"DELETE"},{77,"END"},{78,"PAGEDOWN"},
    {79,"RIGHT"},{80,"LEFT"},{81,"DOWN"},{82,"UP"},{84,"KPSLASH"},{85,"KPSTAR"},{86,"KPMINUS"},{87,"KPPLUS"},{88,"KPENTER"},
    {89,"KP1"},{90,"KP2"},{91,"KP3"},{92,"KP4"},{93,"KP5"},{94,"KP6"},{95,"KP7"},{96,"KP8"},{97,"KP9"},{98,"KP0"},
    {99,"KPPERIOD"},{100,"OEM102"},{135,"INTL1"},{224,"LCTRL"},{225,"LSHIFT"},{228,"RCTRL"},{229,"RSHIFT"}};
inline constexpr std::size_t key_table_size=std::size(key_table);

// One frame's held inputs, built by the SDL samplers or by tests.
struct InputSnapshot {
    std::bitset<key_slots> keys;          // held keys; empty when unfocused
    std::bitset<pad_input_count> pad;     // held pad inputs, triggers thresholded
    int stick_x=0, stick_y=0;             // left stick
    bool focused=true;                    // keyboard focus on the game window
    bool controller=false;                // a controller is open
};
// Console input: each console action's key OR pad input, the left stick added
// through controller() and both halves joined by combine(), as in 0.2.0.
FrameInput frame_input(const Bindings& bindings,const InputSnapshot& input);
// Options and map screens: directions (keys, pad, stick); accept button1;
// cancel button2; toggle options; map map; focused copied.
MenuInput menu_input(const Bindings& bindings,const InputSnapshot& input);
// Start screen: always the default (0.2.0) mapping, whatever the bindings, so
// the start screen and its CONTROLS entry stay reachable: arrows, d-pad and
// left stick move; Z, Return, A and Start accept; Escape cancels. The sampler
// filters Return (Alt+Enter) beforehand.
MenuInput start_input(const InputSnapshot& input);

// controls.txt beside roms.txt in the save folder.
inline constexpr const char* bindings_file_name="controls.txt";
inline constexpr std::size_t bindings_file_limit=4096;
struct BindingsLoad {
    Bindings bindings=Bindings::defaults();
    unsigned ignored=0;                   // lines that changed nothing
    std::optional<std::string> warning;   // for stderr only; the file never blocks play
};
BindingsLoad parse_bindings(std::string_view text);   // never throws on content
std::string format_bindings(const Bindings& bindings);
// Missing file: defaults and no warning. Unreadable, oversize, non-regular or
// damaged: defaults (or line by line) and a warning naming the file.
BindingsLoad load_bindings(const std::filesystem::path& folder);
// Atomically writes folder/controls.txt, creating the folder; throws on failure.
void save_bindings(const std::filesystem::path& folder,const Bindings& bindings);
}
