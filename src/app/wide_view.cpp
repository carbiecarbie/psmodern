// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/wide_view.h"
#include "machine/machine.h"
#include <algorithm>
#include <cstring>

namespace psmodern::app {
namespace mode4 {
std::array<std::uint8_t,8> tile_row(const VdpState& s,std::uint16_t name,unsigned row) noexcept {
    const auto& r=s.registers;
    const unsigned line=(name&0x400) ? 7-(row&7) : row&7;
    const unsigned pattern=(name&0x1ff)*32+line*4;
    // Planes 0-1 and 2-3 are fetched with the masks of registers 3 and 4.
    const unsigned low=pattern&((unsigned(r[3])<<6)|0x3f);
    const unsigned high=pattern&(((r[4]&7u)<<11)|0x7ff);
    const std::uint8_t planes[4]{s.vram[low],s.vram[low+1],s.vram[high+2],s.vram[high+3]};
    const std::uint8_t palette=(name&0x800) ? 16 : 0;
    std::array<std::uint8_t,8> out{};
    for(unsigned px=0;px<8;++px) {
        const unsigned bit=(name&0x200) ? px : 7-px;
        unsigned colour=0;
        for(unsigned plane=0;plane<4;++plane)colour|=((planes[plane]>>bit)&1u)<<plane;
        out[px]=static_cast<std::uint8_t>(colour|palette);
    }
    return out;
}
void decode_background(const VdpState& s,Vdp::Picture& out) noexcept {
    const auto& r=s.registers;
    const auto backdrop=s.cram[16+(r[7]&15)];
    if(!(r[1]&0x40)) { out.fill(backdrop); return; }
    const unsigned table=(r[2]&0x0e)<<10, rows=(r[2]&1) ? 31 : 15;
    for(unsigned line=0;line<192;++line) {
        auto* row=out.data()+line*256;
        // Columns left of the fine scroll are never fetched: backdrop.
        std::fill(row,row+256,backdrop);
        const unsigned scroll=(line<16 && (r[0]&0x40)) ? 0 : r[8];
        const unsigned fine=scroll&7;
        for(unsigned column=0;column<32;++column) {
            const unsigned x=(column-(scroll>>3))&31;
            const unsigned sy=(column>=24 && (r[0]&0x80)) ? 0 : r[9];
            const unsigned y=(line+sy)%224;
            const unsigned at=table|(((y/8)&rows)<<6)|(x<<1);
            const auto name=static_cast<std::uint16_t>(s.vram[at]|(s.vram[at+1]<<8));
            const auto colours=tile_row(s,name,y&7);
            for(unsigned px=0;px<8;++px) {
                const unsigned target=column*8+fine+px;
                if(target>=256)break;
                row[target]=s.cram[colours[px]];
            }
        }
        if(r[0]&0x20)std::fill(row,row+8,backdrop);
    }
}
void render_wrapped(const VdpState& s,int x0,int x1,WidePicture& out) noexcept {
    const auto& r=s.registers;
    x0=std::max(x0,0);x1=std::min(x1,wide_width);
    const auto backdrop=s.cram[16+(r[7]&15)];
    const unsigned table=(r[2]&0x0e)<<10, rows=(r[2]&1) ? 31 : 15;
    for(int line=0;line<wide_height;++line) {
        auto* row=out.data()+line*wide_width;
        if(!(r[1]&0x40)) { std::fill(row+x0,row+x1,backdrop);continue; }
        const unsigned scroll=(line<16 && (r[0]&0x40)) ? 0 : r[8];
        const int fine=static_cast<int>(scroll&7);
        std::array<std::uint8_t,8> colours{};
        for(int x=x0;x<x1;++x) {
            const int sx=x-wide_centre_x;
            const unsigned name_x=static_cast<unsigned>(((sx-static_cast<int>(scroll))%256+256)%256);
            // A new tile starts where a fetch column of the screen does
            // (0-31 inside it); that column decides the vertical lock.
            if(x==x0 || (name_x&7)==0) {
                const int column=(sx-fine)>=0 ? (sx-fine)/8 : -1-(fine-sx-1)/8;
                const unsigned sy=(column>=24 && column<32 && (r[0]&0x80)) ? 0 : r[9];
                const unsigned y=(static_cast<unsigned>(line)+sy)%224;
                const unsigned at=table|(((y/8)&rows)<<6)|((name_x>>3)<<1);
                colours=tile_row(s,static_cast<std::uint16_t>(s.vram[at]|(s.vram[at+1]<<8)),y&7);
            }
            row[x]=s.cram[colours[name_x&7]];
        }
    }
}
}

void copy_snapshot(const Machine& machine,WideSnapshot& out) noexcept {
    const auto& vdp=machine.vdp();
    out.vdp.vram=vdp.vram();out.vdp.cram=vdp.cram();out.vdp.registers=vdp.registers();
    const auto& ram=machine.memory().ram();
    auto at=[&](unsigned address) { return ram[address&0x1fff]; };
    out.mode=at(0xC202);
    out.h_shadow=at(0xC300);out.camera_x=at(0xC301);out.page_x=at(0xC302);
    out.v_shadow=at(0xC304);out.camera_y=at(0xC305);out.page_y=at(0xC306);
    out.area={static_cast<std::uint16_t>(at(0xC260)|(at(0xC261)<<8)),at(0xC262),at(0xC263)};
    std::memcpy(out.window.data(),ram.data()+(0xCC00&0x1fff),out.window.size());
}

bool WideView::family_mode(std::uint8_t mode) noexcept {
    // 8 map load (fade of the old map), 9 map, 0Ah and 0Bh dungeon load and
    // dungeon (while the map fades and the screen is black), 0Ch interaction
    // load, 0Eh and 0Fh road. The space flight (4, 5) has its own rule
    // (WideView::ship) and never takes part in the held camera's.
    return mode==0x08 || mode==0x09 || mode==0x0A || mode==0x0B || mode==0x0C || mode==0x0E || mode==0x0F;
}
bool WideView::consistent(const WideSnapshot& s) noexcept {
    const auto& r=s.vdp.registers;
    return r[8]==s.h_shadow && s.h_shadow==static_cast<std::uint8_t>(-s.camera_x) && r[9]==s.v_shadow &&
           s.v_shadow<224 && s.page_x<game::map::screen_columns && s.camera_y<game::map::screen_height &&
           s.page_y<game::map::screen_rows;
}

WideView::WideView(std::shared_ptr<const Memory::Rom> rom)
    :screens_(std::move(rom)),shown_(std::make_unique<WideSnapshot>()),next_(std::make_unique<WideSnapshot>()),
     picture_(std::make_unique<WidePicture>()) {}

HeldCamera WideView::ship_camera(const WideSnapshot& s) noexcept {
    const auto& r=s.vdp.registers;
    int dy=(static_cast<int>(r[9]%224)-static_cast<int>(s.v_shadow%224)+224)%224;
    if(dy>=112)dy-=224;
    const unsigned x=(s.page_x&7u)*game::map::screen_width+s.camera_x;
    const int y=static_cast<int>((s.page_y&7u)*game::map::screen_height+s.camera_y)+dy;
    const int height=static_cast<int>(game::map::world_height);
    return {x%game::map::world_width,static_cast<unsigned>(((y%height)+height)%height),r[8],r[9]};
}
bool WideView::names_match(const WideSnapshot& s,const HeldCamera& camera) const noexcept {
    namespace map=game::map;
    const auto& r=s.vdp.registers;
    const unsigned table=(r[2]&0x0e)<<10,rows=(r[2]&1) ? 31 : 15,r8=r[8],r9=r[9]%224;
    unsigned compared=0;
    for(unsigned row=0;row<28;++row) {
        const unsigned y0=(row*8+224-r9)%224;
        if(y0+8>192)continue;
        for(unsigned column=0;column<32;++column) {
            const unsigned x0=(column*8+r8)&255;
            if(x0<8 || x0+8>256)continue;
            const unsigned wx=(camera.x+x0)%map::world_width,wy=(camera.y+y0)%map::world_height;
            const unsigned px=wx%map::screen_width,py=wy%map::screen_height;
            const std::uint8_t id=screens_.screen(wx/map::screen_width,wy/map::screen_height)[(py>>4)*map::cells_x+(px>>4)];
            // The row the VDP fetches for these lines (register 2's row mask).
            const unsigned at=table|((row&rows)<<6)|(column<<1);
            if(static_cast<std::uint16_t>(s.vdp.vram[at]|(s.vdp.vram[at+1]<<8))!=screens_.word(id,((py>>3)&1)*2+((px>>3)&1)))return false;
            ++compared;
        }
    }
    return compared>0;
}
// The space flight (00:0A5C loads it, 00:086F flies it; ROM run, milestone
// 10 ship study): take-off over the origin map, space, descent over the
// destination map, then the map load (mode 8). Register 0 stays 26h. The
// scene moves the camera itself, so a frame shows the map at the camera
// derived from work RAM when the name table proves it, and that camera is
// held for the fade that follows; with the scroll register 8 at 0, once the
// scene has shown a map or space, it shows space, whose bands are the name
// table wrapped; otherwise (the departure fade before the take-off included)
// it pillarboxes.
void WideView::ship(const WideSnapshot& s) noexcept {
    camera_.reset();
    if(!map_display(s.vdp))return;
    const auto camera=ship_camera(s);
    if(screens_.select(s.area) && names_match(s,camera)) {
        camera_=camera;source_=WideSource::map;widened_=scene_started_=true;
    } else if(s.vdp.registers[8]==0 && scene_started_) { source_=WideSource::space;widened_=true; }
}

void WideView::reset() noexcept {
    has_shown_=has_next_=false;camera_.reset();previous_page_.reset();
    widened_=window_used_=rendered_=scene_started_=false;source_=WideSource::none;held_count_=0;
}
void WideView::replace_rom(std::shared_ptr<const Memory::Rom> rom) {
    screens_=game::MapScreens(std::move(rom));
    reset();
}
void WideView::capture(const Machine& machine) noexcept {
    std::swap(shown_,next_);has_shown_=has_next_;
    copy_snapshot(machine,*next_);has_next_=true;
    update();
}
void WideView::push(const WideSnapshot& next) noexcept {
    std::swap(shown_,next_);has_shown_=has_next_;
    *next_=next;has_next_=true;
    update();
}

// The held camera (plan, Architecture). It is anchored only on a consistent
// frame of the map or the road, where only the step controller 01:7143 moves
// the camera and writes the shadows together, so work RAM is the displayed
// camera. Otherwise it follows only the signed deltas of the scroll
// registers the pictures were drawn with and reads work RAM again only on
// the next consistent map or road frame: transfers (01:787B) and exit
// restores rewrite the RAM camera while the old map still fades. A delta
// above one vehicle step, display off, register 0 other than 26h or a mode
// outside the family drops it. The space flight (modes 4 and 5) has its own
// rule (ship) and leaves its last map camera for the fade that follows.
void WideView::update() noexcept {
    rendered_=false;widened_=window_used_=false;source_=WideSource::none;
    if(!has_shown_) { camera_.reset();previous_page_.reset();held_count_=0;return; }
    const auto& s=*shown_;
    const std::array<std::uint8_t,2> page{s.page_x,s.page_y};
    const bool page_steady=previous_page_ && *previous_page_==page;
    previous_page_=page;
    if(ship_mode(s.mode)) { ship(s);held_count_=0;return; }
    scene_started_=false;
    if(!family_mode(s.mode) || !map_display(s.vdp)) { camera_.reset();held_count_=0;return; }
    const auto r8=s.vdp.registers[8],r9=s.vdp.registers[9];
    if(camera_) {
        // X: the register is minus the camera, modulo 256. Y: the register
        // runs modulo 224 (the name table), the world modulo 1536.
        const int dx=-static_cast<int>(static_cast<std::int8_t>(static_cast<std::uint8_t>(r8-camera_->r8)));
        int dy=(static_cast<int>(r9%224)-static_cast<int>(camera_->r9%224)+224)%224;
        if(dy>=112)dy-=224;
        if(dx<-2 || dx>2 || dy<-2 || dy>2)camera_.reset();
        else {
            camera_->x=static_cast<unsigned>(static_cast<int>(camera_->x)+dx+static_cast<int>(game::map::world_width))%game::map::world_width;
            camera_->y=static_cast<unsigned>(static_cast<int>(camera_->y)+dy+static_cast<int>(game::map::world_height))%game::map::world_height;
            camera_->r8=r8;camera_->r9=r9;
        }
    }
    // A consistent map or road frame is the displayed camera: it anchors,
    // and it corrects a held camera that disagrees (none does on the
    // recorded paths; the recording tests count corrections).
    if(anchor_mode(s.mode) && consistent(s)) {
        const HeldCamera ram{s.page_x*game::map::screen_width+s.camera_x,s.page_y*game::map::screen_height+s.camera_y,r8,r9};
        if(camera_ && *camera_!=ram)++corrections_;
        camera_=ram;
    }
    if(!camera_ || !screens_.select(s.area)) { held_count_=0;return; }
    widened_=true;source_=WideSource::map;
    // The screen window (01:723D decodes the 2 x 2 screens from the camera's
    // page) holds edited cells; it is trusted only on map and road frames,
    // where only 01:7143 moves the camera and its page (transfers by
    // 01:787B and exit restores rewrite it while the old map still fades in
    // the other modes), when that page is the held camera's and has not
    // changed since the previous copy (a decode may be in flight across the
    // frame boundary). Elsewhere the ROM screens are drawn.
    window_used_=anchor_mode(s.mode) && page_steady && page[0]==camera_->x/game::map::screen_width && page[1]==camera_->y/game::map::screen_height;
    hold_edits(s);
}

std::uint8_t WideView::base_id(const WideSnapshot& s,unsigned cx,unsigned cy) const noexcept {
    namespace map=game::map;
    const unsigned sx=cx/map::cells_x,sy=cy/map::cells_y,cell=(cy%map::cells_y)*map::cells_x+cx%map::cells_x;
    const unsigned dx=(sx-s.page_x)&7,dy=(sy-s.page_y)&7;
    return window_used_ && dx<2 && dy<2 ? s.window[(dx+2*dy)*map::window_slot+cell] : screens_.screen(sx,sy)[cell];
}

// Edited cells (HeldCell). First the cells already held: one that has left
// the wide picture is dropped (the game draws it from the window when it
// comes back); one whose name-table words, on a quarter fully inside the
// picture's x 8-255, are the window's or ROM's again is dropped (the game
// drew it again); otherwise it is kept. Then every cell of a trusted window
// that differs from the ROM, inside the wide picture, is held with its id.
void WideView::hold_edits(const WideSnapshot& s) noexcept {
    namespace map=game::map;
    if(!(held_area_==s.area)) { held_count_=0;held_area_=s.area; }
    const auto& cam=*camera_;
    const auto& r=s.vdp.registers;
    const unsigned table=(r[2]&0x0e)<<10,rows=(r[2]&1) ? 31 : 15;
    // A cell's top-left corner relative to the camera, signed (the world wraps).
    auto offset=[](unsigned v,unsigned origin,unsigned size) {
        int d=static_cast<int>((v+size-origin)%size);
        if(d>=static_cast<int>(size/2))d-=static_cast<int>(size);
        return d;
    };
    auto inside=[&](unsigned cx,unsigned cy,int& gx,int& gy) {
        gx=offset(cx*16,cam.x,map::world_width);gy=offset(cy*16,cam.y,map::world_height);
        return gx+16>-wide_centre_x && gx<256+wide_centre_x && gy+16>0 && gy<wide_height;
    };
    unsigned kept=0;
    for(unsigned k=0;k<held_count_;++k) {
        const auto cell=held_[k];
        int gx=0,gy=0;
        if(!inside(cell.x,cell.y,gx,gy))continue;
        const auto base=base_id(s,cell.x,cell.y);
        bool keep=base!=cell.id;
        for(unsigned q=0;keep && q<4;++q) {
            const int x0=gx+8*static_cast<int>(q&1),y0=gy+8*static_cast<int>(q>>1);
            if(x0<8 || x0+8>256 || y0<0 || y0+8>wide_height)continue;
            const unsigned row=((static_cast<unsigned>(y0)+r[9])%224)/8;
            const unsigned column=((cell.x*16u+8u*(q&1))%256)/8;
            const unsigned at=table|((row&rows)<<6)|(column<<1);
            const auto name=static_cast<std::uint16_t>(s.vdp.vram[at]|(s.vdp.vram[at+1]<<8));
            if(name!=screens_.word(cell.id,q) && name==screens_.word(base,q))keep=false;
        }
        if(keep)held_[kept++]=cell;
    }
    held_count_=kept;
    auto hold=[&](unsigned cx,unsigned cy,std::uint8_t id) {
        unsigned k=0;
        while(k<held_count_ && !(held_[k].x==cx && held_[k].y==cy))++k;
        if(k==held_count_) { if(held_count_==max_held)return;++held_count_; }
        held_[k]={static_cast<std::uint8_t>(cx),static_cast<std::uint8_t>(cy),id};
    };
    if(window_used_) for(unsigned slot=0;slot<4;++slot) {
        const unsigned sx=(s.page_x+(slot&1))%map::screen_columns,sy=(s.page_y+(slot>>1))%map::screen_rows;
        const auto& rom=screens_.screen(sx,sy);
        for(unsigned i=0;i<map::cells_per_screen;++i) {
            const auto id=s.window[slot*map::window_slot+i];
            if(id==rom[i])continue;
            const unsigned cx=sx*map::cells_x+i%map::cells_x,cy=sy*map::cells_y+i/map::cells_x;
            int gx=0,gy=0;
            if(inside(cx,cy,gx,gy))hold(cx,cy,id);
        }
    }
    // A cell edited in the same step that crossed into another page is in the
    // name table only (the window was decoded again before the view could see
    // it): on a map or road frame, a cell wholly inside x 8-255 whose four
    // names are not its window or ROM id's is held as the id whose four
    // words they are (any such id draws the same picture).
    if(!anchor_mode(s.mode) || !consistent(s))return;
    for(unsigned dy=0;dy<13;++dy)for(unsigned dx=0;dx<17;++dx) {
        const unsigned cx=(cam.x/16+dx)%(map::world_width/16),cy=(cam.y/16+dy)%(map::world_height/16);
        int gx=0,gy=0;
        inside(cx,cy,gx,gy);
        if(gx<8 || gx+16>256 || gy<0 || gy+16>wide_height)continue;
        std::array<std::uint16_t,4> names{};
        for(unsigned q=0;q<4;++q) {
            const unsigned row=((static_cast<unsigned>(gy)+8*(q>>1)+r[9])%224)/8,column=((cx*16u+8u*(q&1))%256)/8;
            const unsigned at=table|((row&rows)<<6)|(column<<1);
            names[q]=static_cast<std::uint16_t>(s.vdp.vram[at]|(s.vdp.vram[at+1]<<8));
        }
        auto shows=[&](std::uint8_t id) { for(unsigned q=0;q<4;++q)if(names[q]!=screens_.word(id,q))return false;return true; };
        if(shows(base_id(s,cx,cy)))continue;
        if(const auto h=held(cx,cy);h && shows(*h))continue;
        for(unsigned id=0;id<map::metatiles;++id)
            if(shows(static_cast<std::uint8_t>(id))) { hold(cx,cy,static_cast<std::uint8_t>(id));break; }
    }
}

void WideView::render(int x0,int x1,WidePicture& out) const noexcept {
    if(!widened_)return;
    if(source_==WideSource::space) { mode4::render_wrapped(shown_->vdp,x0,x1,out);return; }
    if(!camera_)return;
    namespace map=game::map;
    const auto& s=*shown_;
    const auto& cam=*camera_;
    x0=std::max(x0,0);x1=std::min(x1,wide_width);
    for(int y=0;y<wide_height;++y) {
        const unsigned wy=(cam.y+static_cast<unsigned>(y))%map::world_height;
        const unsigned sy=wy/map::screen_height, py=wy%map::screen_height;
        const unsigned cell_row=(py>>4)*map::cells_x, quarter_row=((py>>3)&1)*2, line=py&7;
        const unsigned window_dy=(sy-s.page_y)&7;
        // The held cells of this row of cells (at most one row of the picture).
        std::array<HeldCell,32> row_held{};unsigned row_count=0;
        for(unsigned k=0;k<held_count_ && row_count<row_held.size();++k)if(held_[k].y==wy>>4)row_held[row_count++]=held_[k];
        std::array<std::uint8_t,8> colours{};
        for(int x=x0;x<x1;++x) {
            const unsigned wx=(cam.x+map::world_width+static_cast<unsigned>(x-wide_centre_x))%map::world_width;
            if(x==x0 || (wx&7)==0) {
                const unsigned sx=wx/map::screen_width, px=wx%map::screen_width;
                const unsigned cell=cell_row+(px>>4);
                const unsigned window_dx=(sx-s.page_x)&7;
                std::uint8_t id=(window_used_ && window_dx<2 && window_dy<2) ?
                    s.window[(window_dx+2*window_dy)*map::window_slot+cell] : screens_.screen(sx,sy)[cell];
                for(unsigned k=0;k<row_count;++k)if(row_held[k].x==wx>>4)id=row_held[k].id;
                colours=mode4::tile_row(s.vdp,screens_.word(id,quarter_row+((px>>3)&1)),line);
            }
            out[static_cast<std::size_t>(y)*wide_width+static_cast<std::size_t>(x)]=s.vdp.cram[colours[wx&7]];
        }
    }
}

const WidePicture* WideView::picture(const Vdp::Picture& centre) const noexcept {
    if(!widened_)return nullptr;
    if(!rendered_) {
        auto& out=*picture_;
        for(int y=0;y<wide_height;++y)
            std::memcpy(out.data()+y*wide_width+wide_centre_x+8,centre.data()+y*256+8,256-8);
        render(0,wide_centre_x+8,out);
        render(wide_centre_x+256,wide_width,out);
        rendered_=true;
    }
    return picture_.get();
}
}
