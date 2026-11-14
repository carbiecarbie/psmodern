// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/host.h"
#include "game/options.h"
#include "app/language.h"
#include <string_view>
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
enum class MenuAction { none,apply,close,controls,language };
class OptionsMenu {
public:
    // Six rows, four visible: encounters, experience/meseta, automap,
    // widescreen, Controls and Language. The viewport follows the cursor. Controls also
    // opens when read-only; Language cannot change in a restricted session.
    void open(game::Options options,bool restricted,bool pending,bool recording=false,bool automap=false,bool widescreen=false);
    bool visible() const { return visible_; }
    void close() { visible_=false; }
    // Back from Controls or Language: draft, viewport and row kept; waits for a neutral
    // snapshot as on opening.
    void resume() { armed_=false; previous_={}; }
    const game::Options& draft() const { return draft_; }
    bool automap_draft() const { return automap_; }
    bool widescreen_draft() const { return widescreen_; }
    unsigned selected() const { return selected_; }
    unsigned first_visible() const { return top_; }
    MenuAction update(const MenuInput& input,double now);
    void render(std::span<std::uint32_t> pixels,const RomFont& font,Language language=Language::english) const;
    void failed() { failed_=true; }
private:
    game::Options draft_;
    MenuInput previous_;
    unsigned selected_=0,top_=0;
    bool visible_=false,armed_=false,restricted_=false,pending_=false,failed_=false,recording_=false,automap_=false,widescreen_=false;
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
