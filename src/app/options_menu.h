// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/host.h"
#include "game/options.h"
#include <string_view>
namespace psmodern::app {
struct MenuInput {
    bool up=false,down=false,left=false,right=false,accept=false,cancel=false,toggle=false;
    bool focused=true;
    bool any() const { return up||down||left||right||accept||cancel||toggle; }
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
enum class MenuAction { none,apply,close };
class OptionsMenu {
public:
    void open(game::Options options,bool restricted,bool pending,bool recording=false);
    bool visible() const { return visible_; }
    void close() { visible_=false; }
    const game::Options& draft() const { return draft_; }
    unsigned selected() const { return selected_; }
    MenuAction update(const MenuInput& input,double now);
    void render(std::span<std::uint32_t> pixels,const RomFont& font) const;
    void failed() { failed_=true; }
private:
    game::Options draft_;
    MenuInput previous_;
    unsigned selected_=0;
    bool visible_=false,armed_=false,restricted_=false,pending_=false,failed_=false,recording_=false;
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
