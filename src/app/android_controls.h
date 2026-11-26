// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/sdl_host.h"
#include "app/touch_controls.h"

namespace psmodern::app {
void watch_android_lifecycle();
bool android_foreground();

class AndroidControls {
public:
    // Starts as the previous AndroidControls ended, else hidden when a
    // controller is already connected (touch_start_visible).
    AndroidControls(Display& display,const RomFont& font,bool controller_connected);
    ~AndroidControls();
    AndroidControls(const AndroidControls&)=delete;
    AndroidControls& operator=(const AndroidControls&)=delete;
    void layout(bool gameplay,bool map);
    void event(const SDL_Event& e);
    // Going to the background: release the touches and forget held controller input.
    void clear();
    MenuInput menu(MenuInput input,bool start,bool in_menu) const;
    FrameInput frame(FrameInput input) const;
    InputSnapshot navigation(InputSnapshot input,bool capturing) const;
    void end_poll() { taps_={}; back_=false; }
    void end_frame() { frame_taps_={}; }
    bool speed_pressed() const { return taps_[Action::speed]; }
    bool redraw=true;
    void present(Language language,unsigned speed);
private:
    void release_touches();
    void draw(Language language,unsigned speed);
    struct ButtonImage {
        std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture{nullptr,SDL_DestroyTexture};
        bool pressed=false;
        Action held=Action::count; // the cross only: the direction it shows lit
    };
    Display& display_;
    const RomFont& font_;
    int width_=0,height_=0;
    TouchLayout layout_;
    TouchControls touch_;
    PerAction<bool> taps_{}, frame_taps_{};
    PerAction<ButtonImage> images_;
    bool back_=false;
    Language language_=Language::english;
    unsigned speed_=1;
};
}
