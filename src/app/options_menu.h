// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/host.h"
#include "game/options.h"
#include "app/language.h"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace psmodern::app {
struct MenuInput {
    bool up=false,down=false,left=false,right=false,accept=false,cancel=false,toggle=false;
    bool map=false;   // M on the keyboard, X on a controller (the automap)
    bool focused=true;
    bool any() const { return up||down||left||right||accept||cancel||toggle||map; }
};
class RomFont {
public:
    explicit RomFont(std::span<const std::uint8_t> rom);
    bool supports(char character) const;
    void text(std::span<std::uint32_t> pixels,int x,int y,std::string_view text,std::uint32_t color) const;
    void window(std::span<std::uint32_t> pixels,int x,int y,int width,int height,int cursor_y) const;
    const auto& glyphs() const { return glyphs_; }
private:
    std::array<std::array<std::uint8_t,8>,96> glyphs_{};
    std::array<std::array<std::uint8_t,8>,12> window_tiles_{};
};
// The PSModern window set (milestone 12): the cartridge's own border, cursor
// and font placed on the Master System's 32x24 grid of 8x8 tiles, as the
// game's windows are. Options, Controls and the start screen share it.
namespace menu_style {
inline constexpr std::uint32_t black=0xff000000, white=0xffffffff, grey=0xffaaaaaa, dim=0xff555555, yellow=0xffffff00;
inline constexpr int tile=8, columns=32, rows=24;
// A window in tiles; the border takes its outer ring of tiles.
struct TileWindow {
    int column=0,row=0,width=0,height=0;
    bool contains(int x,int y) const {
        return x>=column*tile && x<(column+width)*tile && y>=row*tile && y<(row+height)*tile;
    }
    bool operator==(const TileWindow&) const=default;
};
// Black window with the cartridge border; the cursor tile replaces the left
// border on cursor_row (a tile row) when it is inside the window.
void window(std::span<std::uint32_t> pixels,const RomFont& font,const TileWindow& window,int cursor_row=-1);
void text(std::span<std::uint32_t> pixels,const RomFont& font,int column,int row,std::string_view text,std::uint32_t color);
}
// Options layout, in tiles (milestone 12): the list at the left, the values
// of the selected row in a window beside it that overlaps the list's right
// border, and the explanation window at the bottom; the scene shows at the
// right and between them. The cartridge's letters fill their whole tile, so
// lines are two tile rows apart, as in the game's own windows.
namespace options_layout {
inline constexpr unsigned rows=6, controls_row=4, language_row=5, explanation_lines=3;
inline constexpr int line_step=2;
inline constexpr menu_style::TileWindow list{1,1,20,13};
inline constexpr menu_style::TileWindow explanation{1,15,30,9};
inline constexpr int values_column=20, values_width=11, state_row=22;
// The tile row of a list row's line.
constexpr int line_row(unsigned row) { return list.row+1+line_step*int(row); }
// The longest line an explanation may have: the explanation window's inner width minus a blank column.
inline constexpr std::size_t line_limit=28;
}
// none: nothing to do. apply: save the draft and close. close: close without
// saving (the automap screen). controls: open the Controls screen over Options.
// language: the Language row changed language_draft(); the owner saves it at
// once and calls language_saved() or language_failed().
enum class MenuAction { none,apply,close,controls,language };
class OptionsMenu {
public:
    // Six rows, all visible: encounters, experience/meseta, automap,
    // widescreen, Controls and Language. Left and right change the selected
    // value. (Milestone 14 removed the read-only state: a recording never
    // opens Options, and the Z80 mode is gone.)
    void open(game::Options options,bool pending,bool automap=false,bool widescreen=false);
    // The global language preference and which preferences have a ROM
    // (indexed by LanguagePreference). Defaults: native, all available.
    void set_language(LanguagePreference preference,std::array<bool,3> available);
    bool visible() const { return visible_; }
    void close() { visible_=false; }
    // Back from Controls: draft and row kept; waits for a neutral snapshot as on opening.
    void resume() { armed_=false; previous_={}; }
    const game::Options& draft() const { return draft_; }
    bool automap_draft() const { return automap_; }
    bool widescreen_draft() const { return widescreen_; }
    LanguagePreference language_draft() const { return language_; }
    unsigned selected() const { return selected_; }
    MenuAction update(const MenuInput& input,double now);
    void render(std::span<std::uint32_t> pixels,const RomFont& font,Language language=Language::english) const;
    void failed() { failed_=true; }
    // The owner saved language_draft(), or could not: the previous
    // preference comes back and the state line shows the failure.
    void language_saved() { language_failed_=false; }
    void language_failed(LanguagePreference previous) { language_=previous; language_failed_=true; }
    // What render draws, for tests and the session checks.
    static std::string_view label(unsigned row,Language language);
    // A row's choices in order, and the index of the current one; empty for Controls.
    std::vector<std::string> choices(unsigned row,Language language) const;
    std::size_t current(unsigned row) const;
    bool available(unsigned row,std::size_t choice) const;
    std::optional<menu_style::TileWindow> values_window(Language language) const;
    static std::array<std::string_view,options_layout::explanation_lines> explanation(unsigned row,Language language);
    // The third explanation line of Language, which names a missing ROM when one is.
    std::array<std::string,options_layout::explanation_lines> explanation_text(Language language) const;
    std::string_view state(Language language) const;
    // Inside one of the windows drawn now: render leaves every other pixel alone.
    bool drawn(int x,int y,Language language=Language::english) const;
private:
    game::Options draft_;
    MenuInput previous_;
    unsigned selected_=0;
    LanguagePreference language_=LanguagePreference::native;
    std::array<bool,3> language_available_{true,true,true};
    bool visible_=false,armed_=false,pending_=false,failed_=false,language_failed_=false,automap_=false,widescreen_=false;
    double next_repeat_=0;
};
// Menu buttons are consumed until every gameplay button is released.
class InputRelease {
public:
    void consume() { waiting_=true; }
    FrameInput filter(FrameInput input,bool host_button_held=false,bool keyboard_focused=true);
private:
    bool waiting_=false;
};
}
