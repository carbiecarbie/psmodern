// SPDX-License-Identifier: GPL-3.0-or-later
// The host's canvas (milestone 10) on SDL's dummy video driver and software
// renderer, no ROM: the narrow canvas presents exactly what 0.4.0's 256x192
// texture presented; the wide canvas presents a synthetic wide picture or a
// pillarbox; apply_canvas resizes a normal window and leaves a fullscreen one.
#include "app/sdl_host.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Each presented frame is read back just before SDL presents it.
std::vector<std::uint32_t> presented;
int presented_width=0,presented_height=0;
void captured_present(SDL_Renderer* renderer) {
    if(SDL_GetRendererOutputSize(renderer,&presented_width,&presented_height)!=0)throw std::runtime_error("Reading the output size");
    presented.assign(std::size_t(presented_width)*presented_height,0);
    if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,presented.data(),presented_width*4)!=0)
        throw std::runtime_error("Reading the presented frame");
    for(auto& pixel:presented)pixel|=0xff000000u;
    SDL_RenderPresent(renderer);
}
void check(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
}
#define SDL_RenderPresent captured_present
#include "../src/app/sdl_host.cpp"
#undef SDL_RenderPresent

namespace {
using namespace psmodern;
using namespace psmodern::app;
std::pair<int,int> window_size(const Display& display) {
    int w=0,h=0;SDL_GetWindowSize(display.window.get(),&w,&h);return {w,h};
}
// 0.4.0's presentation: a 256x192 texture drawn whole into viewport(256).
std::vector<std::uint32_t> release_040(const Display& display,const std::vector<std::uint32_t>& centre) {
    auto* renderer=display.renderer.get();
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture{
        SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,256,192),SDL_DestroyTexture};
    check(bool(texture) && SDL_SetTextureScaleMode(texture.get(),SDL_ScaleModeNearest)==0,"0.4.0 texture");
    check(SDL_UpdateTexture(texture.get(),nullptr,centre.data(),256*4)==0,"0.4.0 upload");
    int w,h;SDL_GetRendererOutputSize(renderer,&w,&h);
    const auto v=viewport(w,h,256);const SDL_Rect rect{v.x,v.y,v.width,v.height};
    SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
    check(SDL_RenderCopy(renderer,texture.get(),nullptr,&rect)==0,"0.4.0 copy");
    captured_present(renderer);
    return presented;
}
// What the host shows: bands, then the centre (with any overlay) at centre_rect.
void show(Display& display,const std::vector<std::uint32_t>& centre,const WidePicture* wide) {
    upload_bands(display,wide);
    const auto rect=centre_rect(display);
    check(SDL_UpdateTexture(display.texture.get(),&rect,centre.data(),256*4)==0,"centre upload");
    present(display);
}
// The presented frame is the canvas, scaled, black around it.
void expect_canvas(const std::vector<std::uint32_t>& canvas,int canvas_width,const char* what) {
    const auto v=viewport(presented_width,presented_height,canvas_width);
    for(int y=0;y<presented_height;++y)for(int x=0;x<presented_width;++x) {
        std::uint32_t expected=0xff000000;
        if(x>=v.x && x<v.x+v.width && y>=v.y && y<v.y+v.height)
            expected=canvas[((y-v.y)/v.scale)*canvas_width+(x-v.x)/v.scale];
        check(presented[std::size_t(y)*presented_width+x]==expected,what);
    }
}
}

int main() {try {
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    check(SDL_Init(SDL_INIT_VIDEO)==0,"Starting SDL");
    Vdp::Picture picture{};WidePicture wide{};
    for(int i=0;i<256*192;++i)picture[i]=static_cast<std::uint8_t>((i*5+i/256)&0x3f);
    for(int i=0;i<wide_width*wide_height;++i)wide[i]=static_cast<std::uint8_t>((i*7+i/wide_width*3)&0x3f);
    std::vector<std::uint32_t> centre(256*192);
    {
        auto display=open_display("canvas");
        int tw=0,th=0;SDL_QueryTexture(display.texture.get(),nullptr,nullptr,&tw,&th);
        check(tw==344 && th==192,"one 344 x 192 texture");
        check(!display.wide_canvas && window_size(display)==std::pair{768,576},"a narrow 768 x 576 window opens");
        int mw=0,mh=0;SDL_GetWindowMinimumSize(display.window.get(),&mw,&mh);check(mw==256 && mh==192,"0.4.0's minimum size");
        // Narrow: byte-identical to 0.4.0, also after the wide bands were uploaded once.
        centre_pixels(centre,picture,nullptr);
        const auto reference=release_040(display,centre);
        show(display,centre,nullptr);
        check(presented==reference,"the narrow canvas presents what 0.4.0 presented");
        apply_canvas(display,false);
        check(window_size(display)==std::pair{768,576},"the same width changes nothing");
        // Wide: 3x (1032 x 576) does not fit the dummy display's 1024 x 768; 2x does.
        apply_canvas(display,true);
        check(display.wide_canvas && window_size(display)==std::pair{688,384},"the wide canvas resizes to 2x on a 1024 x 768 display");
        SDL_GetWindowMinimumSize(display.window.get(),&mw,&mh);check(mw==344 && mh==192,"the wide minimum size");
        centre_pixels(centre,picture,&wide);
        show(display,centre,&wide);
        std::vector<std::uint32_t> expected(344*192);
        std::transform(wide.begin(),wide.end(),expected.begin(),rgb);
        expect_canvas(expected,344,"the wide canvas presents the wide picture");
        // Pillarbox: the VDP picture in the centre, black bands.
        centre_pixels(centre,picture,nullptr);
        show(display,centre,nullptr);
        for(int y=0;y<192;++y)for(int x=0;x<344;++x)
            expected[y*344+x]=x<44 || x>=300 ? 0xff000000u : rgb(picture[y*256+x-44]);
        expect_canvas(expected,344,"the pillarbox presents black bands");
        // An overlay drawn into the centre stays in the centre.
        centre_pixels(centre,picture,&wide);
        for(int y=10;y<20;++y)for(int x=0;x<256;++x)centre[y*256+x]=0xffffffff;
        show(display,centre,&wide);
        std::transform(wide.begin(),wide.end(),expected.begin(),rgb);
        for(int y=10;y<20;++y)for(int x=44;x<300;++x)expected[y*344+x]=0xffffffff;
        expect_canvas(expected,344,"an overlay covers the centre only");
        // Back to narrow at the current scale (2x), around the same centre.
        int x0,y0;SDL_GetWindowPosition(display.window.get(),&x0,&y0);
        apply_canvas(display,false);
        int x1,y1;SDL_GetWindowPosition(display.window.get(),&x1,&y1);
        check(window_size(display)==std::pair{512,384} && x1==x0+88 && y1==y0,"narrow again at 2x around the same centre");
        centre_pixels(centre,picture,nullptr);
        show(display,centre,nullptr);
        check(presented==release_040(display,centre),"narrow again presents what 0.4.0 presented");
        // Fullscreen: only the viewport changes.
        toggle_fullscreen(display.window.get());
        const auto full=window_size(display);
        apply_canvas(display,true);
        check(window_size(display)==full && display.wide_canvas,"a fullscreen window keeps its size");
        show(display,centre,nullptr);
        check(presented_width==full.first && presented_height==full.second,"fullscreen output");
        for(int y=0;y<192;++y)for(int x=0;x<344;++x)
            expected[y*344+x]=x<44 || x>=300 ? 0xff000000u : rgb(picture[y*256+x-44]);
        expect_canvas(expected,344,"fullscreen wide viewport");
    }
    SDL_Quit();
    std::cout<<"Canvas: narrow equals 0.4.0, wide picture, pillarbox, overlay, window resize and fullscreen passed.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}
