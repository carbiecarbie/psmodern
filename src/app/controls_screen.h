// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The Controls screen (milestone 8): every action's key and controller input,
// changed by pressing the new one. It reads a raw InputSnapshot and answers a
// fixed navigation that never follows the bindings (arrows, Enter/Z,
// Escape/X, Backspace/Delete; d-pad, left stick, A, B, X), so a player can
// never lock themselves out. The owner saves draft() and reports the result.
#include "app/bindings.h"
#include "app/options_menu.h"
#include "app/localization.h"
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace psmodern::app {
// Layout on the 256x192 picture: the title and the column header stay at the
// top and the status line at the bottom; the 16 rows (13 actions, RESTORE
// DEFAULTS, SAVE, CANCEL) scroll between them at the Options window's 16-pixel
// pitch, so neighbouring rows never touch. The scroll hints sit in the free
// slots beside the header and under the list.
namespace controls_layout {
inline constexpr int title_x=96, title_y=8, header_y=24, key_header_x=112, pad_header_x=192;
inline constexpr int action_x=16, key_x=112, pad_x=192, first_row_y=40, row_step=16;
inline constexpr int hint_x=16, hint_above_y=24, hint_below_y=152, status_x=16, status_y=168;
inline constexpr unsigned action_rows=unsigned(action_count), command_rows=3, rows=action_rows+command_rows, visible_rows=7;
inline constexpr unsigned restore_row=action_rows, save_row=action_rows+1, cancel_row=action_rows+2;
inline constexpr double first_repeat=0.35, next_repeat=0.12, capture_timeout=5.0, result_seconds=2.0;
}
enum class ControlsAction { none, save, close };
class ControlsScreen {
public:
    // Draft from the current bindings; row 0, keyboard column. Each fixed
    // navigation input held when the screen opens, regains focus or ends a
    // capture counts as not pressed until it is released; every other input
    // (an unrelated stuck key, a drifting stick in another direction) is
    // ignored, so nothing held can keep the screen from answering.
    void open(const Bindings& current);
    bool visible() const { return visible_; }
    void close() { visible_=false; capturing_=false; }
    const Bindings& draft() const { return draft_; }
    // save: the owner writes draft() and calls saved() or failed(); close: the
    // draft is discarded and the screen is hidden.
    ControlsAction update(const InputSnapshot& input,double now);
    void saved();    // the owner wrote draft(): closes
    void failed();   // shows SAVE FAILED - RETRY, stays open
    void render(std::span<std::uint32_t> pixels,const RomFont& font,Language language=Language::english) const;
    unsigned row() const { return row_; }          // 0..12 actions, 13 restore, 14 save, 15 cancel
    unsigned column() const { return column_; }    // 0 key, 1 pad
    bool capturing() const { return capturing_; }
    std::string_view status() const { return status_; }
    std::string localized_status(Language language=Language::english) const;
    unsigned first_visible() const { return top_; }   // first row of the list drawn (visible_rows from here)
private:
    void capture(const InputSnapshot& input,double now);
    ControlsAction browse(const InputSnapshot& input,double now);
    void notice(UiText message,double now,Action action=Action::count);
    void end_capture() { capturing_=false; block_held_=true; }
    Bindings draft_=Bindings::defaults();
    InputSnapshot previous_;
    // Fixed navigation sources, one bit each (controls_screen.cpp): those
    // ignored until released, and those that count, now and one update ago.
    std::uint32_t blocked_=0, held_=0, previous_held_=0;
    std::string status_;
    UiText message_id_=UiText::none,status_id_=UiText::none;
    Action message_action_=Action::count,status_action_=Action::count;
    unsigned row_=0, column_=0, top_=0;
    // sticky_: the message stays until the next press (save failed, defaults restored).
    // block_held_: the next focused update blocks every source then held.
    bool visible_=false, block_held_=true, capturing_=false, capture_ready_=false, sticky_=false;
    double next_repeat_=0, capture_started_=0, status_until_=0;
};
}
