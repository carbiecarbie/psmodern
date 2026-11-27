// SPDX-License-Identifier: GPL-3.0-or-later
// The host's canvas (milestone 10) on SDL's dummy video driver and software
// renderer, no ROM: the narrow canvas presents exactly what 0.4.0's 256x192
// texture presented; the wide canvas presents a synthetic wide picture or a
// pillarbox; apply_canvas resizes a normal window and leaves a fullscreen one.
#include "app/sdl_host.h"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
// Each presented frame is read back just before SDL presents it.
std::vector<std::uint32_t> presented;
int presented_width=0,presented_height=0;
int target_allocations=0,target_binds=0;
bool no_targets=false,fail_target_allocation=false,fail_expansion=false;
SDL_bool SDLCALL target_supported(SDL_Renderer* renderer) {
    return no_targets ? SDL_FALSE : SDL_RenderTargetSupported(renderer);
}
SDL_Texture* SDLCALL create_texture(SDL_Renderer* renderer,Uint32 format,int access,int w,int h) {
    if (access==SDL_TEXTUREACCESS_TARGET) {
        ++target_allocations;
        if (fail_target_allocation) { SDL_SetError("forced target allocation failure"); return nullptr; }
    }
    return SDL_CreateTexture(renderer,format,access,w,h);
}
int SDLCALL bind_target(SDL_Renderer* renderer,SDL_Texture* texture) {
    if (texture) ++target_binds;
    return SDL_SetRenderTarget(renderer,texture);
}
int SDLCALL copy_texture(SDL_Renderer* renderer,SDL_Texture* texture,const SDL_Rect* source,const SDL_Rect* dest) {
    if (fail_expansion && SDL_GetRenderTarget(renderer)) return SDL_SetError("forced expansion failure");
    return SDL_RenderCopy(renderer,texture,source,dest);
}
void captured_present(SDL_Renderer* renderer) {
    if(SDL_GetRendererOutputSize(renderer,&presented_width,&presented_height)!=0)throw std::runtime_error("Reading the output size");
    presented.assign(std::size_t(presented_width)*presented_height,0);
    if(SDL_RenderReadPixels(renderer,nullptr,SDL_PIXELFORMAT_ARGB8888,presented.data(),presented_width*4)!=0)
        throw std::runtime_error("Reading the presented frame");
    for(auto& pixel:presented)pixel|=0xff000000u;
    SDL_RenderPresent(renderer);
}
void check(bool value,std::string_view message) {if(!value)throw std::runtime_error(std::string(message));}
}
#define SDL_RenderPresent captured_present
#define SDL_RenderTargetSupported target_supported
#define SDL_CreateTexture create_texture
#define SDL_SetRenderTarget bind_target
#define SDL_RenderCopy copy_texture
#include "../src/app/sdl_host.cpp"
#undef SDL_RenderCopy
#undef SDL_SetRenderTarget
#undef SDL_CreateTexture
#undef SDL_RenderTargetSupported
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
void upload(Display& display,const std::vector<std::uint32_t>& centre,const WidePicture* wide) {
    upload_bands(display,wide);
    const auto rect=centre_rect(display);
    check(SDL_UpdateTexture(display.texture.get(),&rect,centre.data(),256*4)==0,"centre upload");
}
void show(Display& display,const std::vector<std::uint32_t>& centre,const WidePicture* wide) {
    upload(display,centre,wide);
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

// Pixel centres in the destination map into a virtual nearest-expanded image.
// Compare both stripe axes with bilinear samples of that image, allowing for
// the software renderer's fixed-point channel rounding.
void expect_fill(WindowRect rect,int multiple,const std::vector<std::uint32_t>& centre,bool mixed=true) {
    auto sample=[&](int x,int y) {
        x=std::clamp(x/multiple,0,343)-44;
        y=std::clamp(y/multiple,0,191);
        return x<0 || x>=256 ? 0 : static_cast<int>(centre[y*256+x]&255);
    };
    int blends=0,solids=0;
    for (int y=0;y<presented_height;++y) for (int x=0;x<presented_width;++x) {
        const auto pixel=presented[std::size_t(y)*presented_width+x];
        if (x<rect.x || x>=rect.x+rect.width || y<rect.y || y>=rect.y+rect.height) {
            check(pixel==0xff000000,"FILL outside the destination is black");
            continue;
        }
        const double u=(x-rect.x+0.5)*344*multiple/rect.width-0.5;
        const double v=(y-rect.y+0.5)*192*multiple/rect.height-0.5;
        const int i=static_cast<int>(std::floor(u)),j=static_cast<int>(std::floor(v));
        const double a=u-i,b=v-j;
        const double expected=(1-b)*((1-a)*sample(i,j)+a*sample(i+1,j))+
            b*((1-a)*sample(i,j+1)+a*sample(i+1,j+1));
        for (int shift:{0,8,16}) {
            const auto channel=static_cast<int>((pixel>>shift)&255);
            // 16-bit sample steps can drift by just under one step unit per
            // output pixel, followed by 7-bit weights and channel truncation.
            constexpr double tolerance=11;
            if (std::abs(channel-expected)>tolerance)
                throw std::runtime_error("FILL two-pass sample differs at "+std::to_string(x)+","+std::to_string(y)+
                    " (actual "+std::to_string(channel)+", expected "+std::to_string(expected)+")");
        }
        if (expected>8 && expected<247 && (pixel&255)>4 && (pixel&255)<251) ++blends;
        if (expected==255 && (pixel&255)==255) ++solids;
    }
    check(!mixed || (blends>0 && solids>0),"FILL stripe phase contains both mixed boundaries and solid interiors");
}
void fill_presentation(Display& display) {
    SDL_SetWindowSize(display.window.get(),900,620);
    std::vector<std::uint32_t> stripes(256*192),canvas(344*192);
    for (int y=0;y<192;++y) for (int x=0;x<256;++x)
        canvas[y*344+x+44]=stripes[y*256+x]=((x/5+y/7)%2) ? 0xff000000u : 0xffffffffu;
    band_pixels(canvas,nullptr);
    apply_canvas(display,WidescreenMode::on);
    show(display,stripes,nullptr);
    expect_canvas(canvas,344,"ON remains nearest at a fractional output");
    const auto on=presented;
    int x0,y0,x1,y1;
    SDL_GetWindowPosition(display.window.get(),&x0,&y0);
    apply_canvas(display,WidescreenMode::fill);
    SDL_GetWindowPosition(display.window.get(),&x1,&y1);
    check(window_size(display)==std::pair{900,620} && x0==x1 && y0==y1,"ON to FILL keeps the window size and position");
    present(display);
    expect_fill({0,59,900,502},3,stripes);
    auto check_target=[&](int w,int h) {
        int tw=0,th=0,access=0;
        check(display.fill_texture && SDL_QueryTexture(display.fill_texture.get(),nullptr,&access,&tw,&th)==0 &&
              access==SDL_TEXTUREACCESS_TARGET && tw==w && th==h,"FILL target is the ceiling multiple of the destination");
    };
    check_target(1032,576);
    apply_canvas(display,WidescreenMode::on);
    present(display);
    SDL_GetWindowPosition(display.window.get(),&x1,&y1);
    check(presented==on && x0==x1 && y0==y1,"FILL to ON restores the same pixels and window position");
    apply_canvas(display,WidescreenMode::fill);
    const int allocations=target_allocations;
    SDL_SetWindowSize(display.window.get(),899,620);
    present(display);
    expect_fill({0,59,899,502},3,stripes);
    check(target_allocations==allocations,"same multiple reuses the target after resize");

    const PictureTarget small{{30,41,151,84},true};
    check(prepare_fill(display,small.rect),"prepare explicit Android sub-1x target");
    present(display,&small);
    check_target(344,192);
    expect_fill(small.rect,1,stripes);
    present(display,&small,[&] {
        auto* renderer=display.renderer.get();
        check(SDL_GetRenderTarget(renderer)==nullptr,"controls draw to the window target");
        check(SDL_SetRenderDrawColor(renderer,255,0,0,255)==0 && SDL_RenderDrawPoint(renderer,29,41)==0,"draw output-space touch control");
        check(SDL_RenderDrawPoint(renderer,80,70)==0,"draw control over the picture");
    });
    check(presented[41*presented_width+29]==0xffff0000u && presented[70*presented_width+80]==0xffff0000u,
          "touch control draws after the picture at output coordinates");
    const PictureTarget empty{{},true};
    present(display,&empty);
    check(std::all_of(presented.begin(),presented.end(),[](auto pixel){return pixel==0xff000000u;}),"empty FILL target draws no picture");

    fail_expansion=true;
    bool threw=false;
    try { present(display,&small); } catch (const std::runtime_error&) { threw=true; }
    fail_expansion=false;
    check(threw && SDL_GetRenderTarget(display.renderer.get())==nullptr,"expansion error restores the window target");

    for (auto event:{SDL_RENDER_TARGETS_RESET,SDL_RENDER_DEVICE_RESET}) {
        const int before=target_allocations;
        check(reset_picture_textures(display,event) && !display.fill_texture,"reset releases FILL target");
        show(display,stripes,nullptr);
        check(target_allocations==before+1,"reset recreates the intermediate on the next FILL frame");
        expect_fill({0,59,899,502},3,stripes);
    }

    apply_canvas(display,WidescreenMode::on);
    show(display,stripes,nullptr);
    const auto fallback_pixels=presented;
    for (bool missing_support:{true,false}) {
        auto fallback_display=open_display("fallback");
        apply_canvas(fallback_display,WidescreenMode::fill);
        SDL_SetWindowSize(fallback_display.window.get(),899,620);
        no_targets=missing_support;
        fail_target_allocation=!missing_support;
        const int before=target_allocations;
        std::ostringstream diagnostics;
        auto* log=std::cerr.rdbuf(diagnostics.rdbuf());
        try { show(fallback_display,stripes,nullptr); present(fallback_display); }
        catch (...) { std::cerr.rdbuf(log); throw; }
        std::cerr.rdbuf(log);
        no_targets=fail_target_allocation=false;
        check(presented==fallback_pixels,missing_support ? "unsupported targets present exactly as ON" : "failed target allocation presents exactly as ON");
        const auto message=diagnostics.str();
        check(message.starts_with("Widescreen FILL unavailable, using ON: ") && std::count(message.begin(),message.end(),'\n')==1,
              "fallback writes one diagnostic across repeated presents");
        check(target_allocations==before+(missing_support ? 0 : 1),"fallback does not repeatedly try allocation");
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
        check(!display.wide_canvas() && window_size(display)==std::pair{768,576},"a narrow 768 x 576 window opens");
        int mw=0,mh=0;SDL_GetWindowMinimumSize(display.window.get(),&mw,&mh);check(mw==256 && mh==192,"0.4.0's minimum size");
        // Narrow: byte-identical to 0.4.0, also after the wide bands were uploaded once.
        centre_pixels(centre,picture,nullptr);
        const auto reference=release_040(display,centre);
        show(display,centre,nullptr);
        check(presented==reference,"the narrow canvas presents what 0.4.0 presented");
        apply_canvas(display,WidescreenMode::off);
        check(window_size(display)==std::pair{768,576},"the same width changes nothing");
        // Wide: 3x (1032 x 576) does not fit the dummy display's 1024 x 768; 2x does.
        apply_canvas(display,WidescreenMode::fill);
        check(display.wide_canvas() && window_size(display)==std::pair{688,384},"the wide canvas resizes to 2x on a 1024 x 768 display");
        SDL_GetWindowMinimumSize(display.window.get(),&mw,&mh);check(mw==344 && mh==192,"the wide minimum size");
        centre_pixels(centre,picture,&wide);
        show(display,centre,&wide);
        std::vector<std::uint32_t> expected(344*192);
        std::transform(wide.begin(),wide.end(),expected.begin(),rgb);
        expect_canvas(expected,344,"the wide canvas presents the wide picture");
        const auto fill_pixels=presented;
        check(target_binds==0 && target_allocations==0,"exact integer FILL makes a single copy");
        apply_canvas(display,WidescreenMode::on);
        show(display,centre,&wide);
        check(presented==fill_pixels,"exact integer FILL presents the same pixels as ON");
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
        fill_presentation(display);
        SDL_SetWindowSize(display.window.get(),688,384);
        // Back to narrow at the current scale (2x), around the same centre.
        int x0,y0;SDL_GetWindowPosition(display.window.get(),&x0,&y0);
        apply_canvas(display,WidescreenMode::off);
        int x1,y1;SDL_GetWindowPosition(display.window.get(),&x1,&y1);
        check(window_size(display)==std::pair{512,384} && x1==x0+88 && y1==y0,"narrow again at 2x around the same centre");
        centre_pixels(centre,picture,nullptr);
        show(display,centre,nullptr);
        check(presented==release_040(display,centre),"narrow again presents what 0.4.0 presented");
        // Fullscreen: only the viewport changes.
        toggle_fullscreen(display.window.get());
        const auto full=window_size(display);
        apply_canvas(display,WidescreenMode::on);
        check(window_size(display)==full && display.wide_canvas(),"a fullscreen window keeps its size");
        show(display,centre,nullptr);
        check(presented_width==full.first && presented_height==full.second,"fullscreen output");
        for(int y=0;y<192;++y)for(int x=0;x<344;++x)
            expected[y*344+x]=x<44 || x>=300 ? 0xff000000u : rgb(picture[y*256+x-44]);
        expect_canvas(expected,344,"fullscreen wide viewport");
        apply_canvas(display,WidescreenMode::fill);
        std::fill(centre.begin(),centre.end(),0xffffffff);
        show(display,centre,nullptr);
        expect_fill({0,98,1024,572},3,centre,false);
    }
    SDL_Quit();
    std::cout<<"Canvas: narrow equals 0.4.0, ON stays nearest, and FILL geometry, sampling, resets and fallback passed.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}}
