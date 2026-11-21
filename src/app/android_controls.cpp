// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/android_controls.h"
#include <atomic>
#include <jni.h>
#include <mutex>

namespace psmodern::app {
namespace {
std::atomic<bool> foreground{true};
std::mutex inset_mutex;
WindowBorders safe_insets;
int SDLCALL lifecycle(void*,SDL_Event* event) {
    if (event->type==SDL_APP_WILLENTERBACKGROUND || event->type==SDL_APP_DIDENTERBACKGROUND)
        foreground=false;
    if (event->type==SDL_APP_DIDENTERFOREGROUND) foreground=true;
    return 1;
}
}
extern "C" JNIEXPORT void JNICALL Java_org_psmodern_game_PSModernActivity_setInsets(
    JNIEnv*,jclass,jint top,jint left,jint bottom,jint right) {
    std::lock_guard guard(inset_mutex);
    safe_insets={top,left,bottom,right};
}
void watch_android_lifecycle() {
    foreground=true;
    SDL_AddEventWatch(lifecycle,nullptr);
}
bool android_foreground() { return foreground.load(); }

void AndroidControls::layout(bool gameplay,bool map) {
    sdl_check(SDL_GetRendererOutputSize(display_.renderer.get(),&width_,&height_)==0,"Reading screen size");
    WindowBorders insets;
    { std::lock_guard guard(inset_mutex); insets=safe_insets; }
    const auto next=touch_layout(width_,height_,insets,display_.wide_canvas,gameplay,map);
    if (next.game!=layout_.game || next.buttons!=layout_.buttons || next.overlay!=layout_.overlay) {
        clear();
        layout_=next;
        touch_.set_layout(next);
    }
}
void AndroidControls::clear() {
    touch_.clear(); taps_={}; frame_taps_={}; back_=false; redraw=true;
}
void AndroidControls::event(const SDL_Event& e) {
    if (e.type==SDL_RENDER_TARGETS_RESET || e.type==SDL_RENDER_DEVICE_RESET) {
        images_={};
        recreate_picture_texture(display_);
        redraw=true;
    }
    if (e.type==SDL_KEYDOWN && e.key.keysym.scancode==SDL_SCANCODE_AC_BACK && !e.key.repeat) back_=true;
    if (e.type==SDL_CONTROLLERBUTTONDOWN ||
        (e.type==SDL_CONTROLLERAXISMOTION && std::abs(int(e.caxis.value))>stick_dead_zone)) {
        clear(); touch_.controller_used(); return;
    }
    if (e.type!=SDL_FINGERDOWN && e.type!=SDL_FINGERMOTION && e.type!=SDL_FINGERUP) return;
    const int x=int(e.tfinger.x*width_),y=int(e.tfinger.y*height_);
    const auto before=touch_.held();
    if (e.type==SDL_FINGERDOWN) touch_.down(e.tfinger.fingerId,x,y);
    else if (e.type==SDL_FINGERMOTION) touch_.move(e.tfinger.fingerId,x,y);
    else touch_.up(e.tfinger.fingerId);
    const auto after=touch_.held();
    for (std::size_t i=0;i<action_count;++i) {
        if (after[i] && !before[i]) {
            if (i<=std::size_t(Action::right)) {
                for (auto dir:{Action::left,Action::right,Action::up,Action::down}) taps_[dir]=frame_taps_[dir]=false;
            }
            taps_[i]=frame_taps_[i]=true;
        }
    }
    redraw=true;
}
MenuInput AndroidControls::menu(MenuInput in,bool start,bool in_menu) const {
    const auto held=touch_.held();
    auto on=[&](Action a){return taps_[a] || held[a];};
    in.up|=on(Action::up); in.down|=on(Action::down); in.left|=on(Action::left); in.right|=on(Action::right);
    in.accept|=on(Action::button1); in.cancel|=(!start && on(Action::button2)) || (in_menu && back_);
    in.toggle|=on(Action::options) || (!in_menu && back_); in.map|=on(Action::map);
    return in;
}
FrameInput AndroidControls::frame(FrameInput in) const {
    const auto held=touch_.held();
    auto on=[&](Action a){return frame_taps_[a] || held[a];};
    auto& pad=in.pads[0];
    pad.up|=on(Action::up); pad.down|=on(Action::down); pad.left|=on(Action::left); pad.right|=on(Action::right);
    pad.one|=on(Action::button1); pad.two|=on(Action::button2); in.pause|=on(Action::pause);
    return in;
}
InputSnapshot AndroidControls::navigation(InputSnapshot in,bool capturing) const {
    if (!capturing) {
        const auto m=menu({},false,true);
        if (m.up) in.keys[SDL_SCANCODE_UP]=true; if (m.down) in.keys[SDL_SCANCODE_DOWN]=true;
        if (m.left) in.keys[SDL_SCANCODE_LEFT]=true; if (m.right) in.keys[SDL_SCANCODE_RIGHT]=true;
        if (m.accept) in.keys[SDL_SCANCODE_Z]=true; if (m.cancel) in.keys[SDL_SCANCODE_X]=true;
    }
    // Back already supplies menu cancel above. Keep it out of key capture.
    in.keys.reset(SDL_SCANCODE_AC_BACK);
    in.cancel_capture=capturing && back_;
    return in;
}
void AndroidControls::present(Language language,unsigned speed) {
    const auto& r=layout_.game;
    if (!r.width || !r.height) return;
    SDL_Rect rect{r.x,r.y,r.width,r.height};
    app::present(display_,&rect,[&]{draw(language,speed);});
    redraw=false;
}
void AndroidControls::draw(Language language,unsigned speed) {
    if (!touch_.visible()) return;
    if (language!=language_) images_={};
    if (speed!=speed_) images_[Action::speed].texture.reset();
    language_=language; speed_=speed;
    const auto held=touch_.held();
    const Uint8 alpha=layout_.overlay ? 150 : 255;
    if (layout_.pad.width) {
        Action lit=Action::count;
        for (Action a:{Action::up,Action::down,Action::left,Action::right}) if (held[a]) lit=a;
        auto& image=images_[Action::up];
        if (!image.texture || image.held!=lit) {
            std::vector<std::uint32_t> pixels(72*72);
            draw_touch_cross(font_,pixels,lit);
            if (!image.texture) {
                image.texture.reset(SDL_CreateTexture(display_.renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,72,72));
                sdl_check(bool(image.texture),"Creating touch cross");
                SDL_SetTextureBlendMode(image.texture.get(),SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(image.texture.get(),SDL_ScaleModeNearest);
            }
            sdl_check(SDL_UpdateTexture(image.texture.get(),nullptr,pixels.data(),72*4)==0,"Uploading touch cross");
            image.held=lit;
        }
        SDL_SetTextureAlphaMod(image.texture.get(),alpha);
        SDL_Rect dest{layout_.pad.x,layout_.pad.y,layout_.pad.width,layout_.pad.height};
        sdl_check(SDL_RenderCopy(display_.renderer.get(),image.texture.get(),nullptr,&dest)==0,"Drawing touch cross");
    }
    for (std::size_t i=std::size_t(Action::right)+1;i<action_count;++i) {
        const auto& r=layout_.buttons[i];
        if (!r.width) continue;
        const auto a=static_cast<Action>(i);
        auto& image=images_[i];
        if (!image.texture || image.pressed!=held[i]) {
            int w=24,h=24;
            if (a==Action::pause || a==Action::options || a==Action::map || a==Action::speed) {
                w=56; h=40;
            }
            std::vector<std::uint32_t> pixels(w*h);
            draw_touch_button(font_,pixels,w,a,language,speed,held[i]);
            if (!image.texture) {
                image.texture.reset(SDL_CreateTexture(display_.renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,w,h));
                sdl_check(bool(image.texture),"Creating touch button");
                SDL_SetTextureBlendMode(image.texture.get(),SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(image.texture.get(),SDL_ScaleModeNearest);
            }
            sdl_check(SDL_UpdateTexture(image.texture.get(),nullptr,pixels.data(),w*4)==0,"Uploading touch button");
            image.pressed=held[i];
        }
        SDL_SetTextureAlphaMod(image.texture.get(),alpha);
        SDL_Rect dest{r.x,r.y,r.width,r.height};
        sdl_check(SDL_RenderCopy(display_.renderer.get(),image.texture.get(),nullptr,&dest)==0,"Drawing touch button");
    }
}
}
