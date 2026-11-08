// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/vdp.h"
#include <algorithm>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
using psmodern::Vdp;
namespace psmodern {
// Seed a power-on fixture without issuing simultaneous CPU port requests.
struct VdpFixture {
    static void bytes(Vdp& v,unsigned address,std::initializer_list<unsigned> values,unsigned code) {
        if (v.cycle()) throw std::runtime_error("Fixture initialization after clock start");
        for (auto value:values) {
            if (code==3) v.cram_[address&31]=value&63;
            else v.vram_[address&0x3fff]=value;
            ++address;
        }
    }
};
}
namespace {
constexpr std::uint64_t frame=228*262;
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
void reg(Vdp& v,unsigned r,unsigned value) {
    v.write_control(value);v.write_control(0x80|r);
}
void bytes(Vdp& v,unsigned address,std::initializer_list<unsigned> data,unsigned code=1) {
    psmodern::VdpFixture::bytes(v,address,data,code);
}
void timed_byte(Vdp& v,unsigned address,unsigned value) {
    v.write_control(address&255);v.write_control(0x40|(address>>8));v.write_data(value);
}
void pixel_tile(Vdp& v,unsigned tile,unsigned color,std::uint8_t bits=0xff) {
    for(unsigned y=0;y<8;++y) {
        unsigned address=tile*32+y*4;
        for(unsigned plane=0;plane<4;++plane)
            bytes(v,address+plane,{(color&(1u<<plane))?static_cast<unsigned>(bits):0u});
    }
}
void name(Vdp& v,unsigned row,unsigned col,unsigned tile,unsigned flags=0) {
    bytes(v,0x3800+row*64+col*2,{(tile|flags)&255,(tile|flags)>>8});
}
Vdp setup() {
    Vdp v;v.read_status(0);
    reg(v,0,0x04);reg(v,1,0x40);reg(v,2,0x0f);
    reg(v,3,0xff);reg(v,4,7);reg(v,5,0x7f);reg(v,6,3);reg(v,7,0);
    reg(v,8,0);reg(v,9,0);
    bytes(v,0x3f00,{0xd0});
    bytes(v,0xc010,{0x3f,0x15,0x2a},3);
    bytes(v,0xc001,{0x11,0x22},3);
    pixel_tile(v,1,1);pixel_tile(v,2,2);
    return v;
}
void finish(Vdp& v) { v.sync(frame); }
std::uint8_t at(const Vdp& v,unsigned y,unsigned x) { return v.picture()[y*256+x]; }
void background() {
    auto v=setup();
    name(v,0,0,1);name(v,0,1,2);name(v,0,2,1,0x800);
    finish(v);
    check(at(v,0,0)==0x11 && at(v,0,8)==0x22 && at(v,0,16)==0x15,
          "background tiles and palette select");
    auto flipped=setup();pixel_tile(flipped,3,1,0x80);
    for(unsigned y=1;y<8;++y) bytes(flipped,3*32+y*4,{0});
    name(flipped,0,0,3);name(flipped,0,1,3,0x200);
    name(flipped,1,0,3,0x400);
    finish(flipped);
    check(at(flipped,0,0)==0x11 && at(flipped,0,7)==0x00 &&
          at(flipped,0,8)==0x00 && at(flipped,0,15)==0x11 &&
          at(flipped,8,0)==0x00,"tile horizontal and vertical flip");
    auto blank=setup();name(blank,0,0,1);name(blank,0,1,1);
    reg(blank,0,0x24);finish(blank);
    check(at(blank,0,0)==0x3f && at(blank,0,7)==0x3f && at(blank,0,8)==0x11,
          "left column blank");
    auto off=setup();name(off,0,0,1);reg(off,1,0);finish(off);
    check(at(off,0,0)==0x3f,"display off uses backdrop");
}
void background_color_zero() {
    auto v=setup();
    bytes(v,0xc000,{0x10},3);
    name(v,0,2,0,0x800);
    finish(v);
    check(at(v,0,12)==0x10 && at(v,0,16)==0x3f,
          "background color zero uses the selected tile palette");
}
void timed_background() {
    // Column 24's name completes at dot 193; the preceding CPU slot
    // completes at 163. A write at cycle 100 reaches it, at 130 misses it.
    auto before=setup();before.sync(8*228+100);timed_byte(before,0x3870,1);finish(before);
    check(at(before,8,192)==0x11,"name written before its fetch affects that column");
    auto after=setup();after.sync(8*228+130);timed_byte(after,0x3870,1);finish(after);
    check(at(after,8,192)==0 && at(after,9,192)==0x11,"name written after fetch waits for next line");

    // Column zero fetches planes 0/1 at dot 5, planes 2/3 at dot 7.
    auto early=setup();name(early,0,0,3);name(early,0,4,3);
    early.sync(1);timed_byte(early,3*32,0xff);finish(early);
    check(at(early,0,0)==0x11,"pattern write served before the plane fetch is visible");
    auto late=setup();name(late,0,0,3);name(late,0,4,3);
    late.sync(3);timed_byte(late,3*32,0xff);finish(late);
    check(at(late,0,0)==0 && at(late,0,32)==0x11,"pattern changes affect only later plane fetches");

    auto planes=setup();pixel_tile(planes,64,5);name(planes,0,0,64);
    bytes(planes,0xc005,{0x33},3);
    planes.sync(3);reg(planes,4,0);finish(planes);
    check(at(planes,0,0)==0x11,"R4 change between plane fetches preserves the fetched low planes");
    auto fetched=setup();pixel_tile(fetched,64,5);name(fetched,0,0,64);
    bytes(fetched,0xc005,{0x33},3);
    fetched.sync(4);reg(fetched,4,0);finish(fetched);
    check(at(fetched,0,0)==0x33,"R4 change after both fetches preserves the whole tile");

    auto base=setup();name(base,0,0,1);name(base,0,1,1);
    base.sync(1);reg(base,2,0x0d);finish(base);
    check(at(base,0,0)==0x11 && at(base,0,8)==0,"R2 changes apply to names not yet fetched");
    auto disabled=setup();name(disabled,0,0,1);name(disabled,0,1,1);
    disabled.sync(8);reg(disabled,1,0);finish(disabled);
    check(at(disabled,0,0)==0x11 && at(disabled,0,8)==0x3f,"display off changes later output pixels");
}
void sprites() {
    auto v=setup();name(v,0,0,1,0x1000);
    bytes(v,0x3f00,{255,0xd0});bytes(v,0x3f80,{0,2});
    finish(v);
    check(at(v,1,0)==0x11,"opaque priority tile covers sprite");
    auto low=setup();name(low,0,0,1);
    bytes(low,0x3f00,{255,0xd0});bytes(low,0x3f80,{0,2});
    finish(low);
    check(at(low,1,0)==0x2a,"sprite overlays low priority tile");
    auto zero=setup();name(zero,0,0,0,0x1000);
    bytes(zero,0xc000,{0x10},3);
    bytes(zero,0x3f00,{255,0xd0});bytes(zero,0x3f80,{0,2});
    finish(zero);
    check(at(zero,1,0)==0x2a,"sprite overlays priority tile with color zero");
    auto shifted=setup();reg(shifted,0,0x0c);
    bytes(shifted,0x3f00,{255,0xd0});bytes(shifted,0x3f80,{8,2});
    finish(shifted);
    check(at(shifted,1,0)==0x2a && at(shifted,1,8)==0x00,"R0 sprite shift");
    auto tall=setup();reg(tall,1,0x42);pixel_tile(tall,3,1);
    bytes(tall,0x3f00,{255,0xd0});bytes(tall,0x3f80,{0,3});
    finish(tall);
    check(at(tall,1,0)==0x2a && at(tall,9,0)==0x15,"8x16 uses even tile and next tile");
    auto zoom3=setup();reg(zoom3,1,0x41);pixel_tile(zoom3,3,1,0x80);
    bytes(zoom3,0x3f00,{255,255,255,0xd0});
    for(unsigned i=0;i<3;++i) bytes(zoom3,0x3f80+i*2,{i*24,3});
    finish(zoom3);
    for(unsigned i=0;i<3;++i)
        check(at(zoom3,1,i*24)==0x15 && at(zoom3,1,i*24+1)==0x00 &&
              at(zoom3,1,i*24+2)==0x00 && at(zoom3,2,i*24)==0x15,
              "three sprites zoom vertically only");
    auto zoom8=setup();reg(zoom8,1,0x41);pixel_tile(zoom8,3,1,0x80);
    bytes(zoom8,0x3f00,{255,255,255,255,255,255,255,255,0xd0});
    for(unsigned i=0;i<8;++i) bytes(zoom8,0x3f80+i*2,{i*24,3});
    finish(zoom8);
    for(unsigned i=0;i<8;++i) {
        check(at(zoom8,1,i*24)==0x15 && at(zoom8,2,i*24)==0x15,
              "all eight sprites zoom vertically");
        check(at(zoom8,1,i*24+1)==(i<4?0x15:0x00),
              "only first four sprites zoom horizontally");
    }
    auto term=setup();bytes(term,0x3f00,{0xd0,255});
    bytes(term,0x3f82,{0,2});finish(term);
    check(at(term,1,0)==0x00,"D0 ends sprite list");
}
void sprite_flags() {
    auto v=setup();
    bytes(v,0x3f00,{0,0,0,0,0,0,0,0,0,0xd0});
    for(unsigned i=0;i<9;++i) bytes(v,0x3f80+i*2,{0,2});
    // Line 0 dot 315 is CPU cycle 210; overflow for line 1 is recorded there.
    v.sync(209);
    check(!(v.read_status(0)&0x40),"overflow not before the dot-315 event");
    v.sync(210);
    check(v.read_status(0)&0x40,"ninth sprite overflows on prior line");
    // Pixel 32 of line 1 is dot 9+32=41: cycle 26 is dot 40, cycle 27 dot 41.
    auto c=setup();bytes(c,0x3f00,{0,0,0xd0});
    bytes(c,0x3f80,{32,2,32,2});
    c.sync(228+26);check(!(c.read_status(0)&0x20),"collision not before pixel");
    c.sync(228+27);check(c.read_status(0)&0x20,"opaque overlap sets collision");
    auto transparent=setup();pixel_tile(transparent,2,0);
    bytes(transparent,0x3f00,{0,0,0xd0});
    bytes(transparent,0x3f80,{32,2,32,2});
    transparent.sync(2*228);
    check(!(transparent.read_status(0)&0x20),"transparent sprites do not collide");
    auto limit=setup();bytes(limit,0x3f00,{255,255,255,255,255,255,255,255,255,0xd0});
    for(unsigned i=0;i<8;++i) bytes(limit,0x3f80+i*2,{32,2});
    bytes(limit,0x3f90,{0,2});finish(limit);
    check(at(limit,1,0)==0x00 && at(limit,1,32)==0x2a,
          "ninth sprite does not draw");
}
void scroll() {
    auto h=setup();name(h,0,0,1);name(h,0,31,2);
    name(h,2,0,1);name(h,2,31,2);
    reg(h,8,8);reg(h,0,0x44);finish(h);
    check(at(h,1,0)==0x11 && at(h,16,0)==0x22,"top two rows lock horizontal scroll");
    auto latch=setup();name(latch,0,0,1);name(latch,0,31,2);
    latch.sync(220);reg(latch,8,8);finish(latch);
    check(at(latch,1,0)==0x11 && at(latch,2,0)==0x22,
          "horizontal scroll latches at the prior line end");
    // R8 is latched at dot 313: cycle 207 is dot 311, cycle 208 is dot 313.
    auto before313=setup();name(before313,0,0,1);name(before313,0,31,2);
    before313.sync(207);reg(before313,8,8);finish(before313);
    check(at(before313,1,0)==0x22,"R8 written before dot 313 scrolls the next line");
    auto at313=setup();name(at313,0,0,1);name(at313,0,31,2);
    at313.sync(208);reg(at313,8,8);finish(at313);
    check(at(at313,1,0)==0x11 && at(at313,2,0)==0x22,"R8 written at dot 313 waits one line");
    // Column zero was fetched before either port write can get a CPU slot.
    auto before7=setup();name(before7,0,0,1);
    before7.sync(228+3);timed_byte(before7,0x3800,2);finish(before7);
    check(at(before7,0,0)==0x11 && at(before7,1,0)==0x11 && at(before7,2,0)==0x22,"late column-zero write waits for the next line");
    auto at7=setup();name(at7,0,0,1);
    at7.sync(228+4);timed_byte(at7,0x3800,2);finish(at7);
    check(at(at7,1,0)==0x11 && at(at7,2,0)==0x22,"write at dot 7 waits for the next line");
    auto v=setup();name(v,0,0,1);name(v,1,0,2);
    name(v,0,24,1);name(v,1,24,2);
    reg(v,9,8);reg(v,0,0x84);finish(v);
    check(at(v,0,0)==0x11,"vertical scroll remains latched in current frame");
    v.sync(2*frame);
    check(at(v,0,0)==0x22 && at(v,0,192)==0x11,"right eight columns lock vertical scroll");
    auto active=setup();name(active,0,0,1);name(active,1,0,2);
    active.sync(100*228);reg(active,9,8);
    active.sync(frame);check(at(active,0,0)==0x11,
                            "active-display R9 write does not change current frame");
    active.sync(2*frame);check(at(active,0,0)==0x22,
                              "active-display R9 write applies next frame");
    auto vblank=setup();name(vblank,0,0,1);name(vblank,1,0,2);
    vblank.sync(200*228);reg(vblank,9,8);
    vblank.sync(frame);check(at(vblank,0,0)==0x11,
                            "vertical-blank R9 write does not change current frame");
    vblank.sync(2*frame);check(at(vblank,0,0)==0x22,
                              "vertical-blank R9 write applies next frame");
    auto fine=setup();name(fine,0,0,1);reg(fine,8,3);finish(fine);
    check(at(fine,1,0)==0x3f && at(fine,1,2)==0x3f && at(fine,1,3)==0x11,
          "fine horizontal scroll leaves backdrop strip");
}
void register_masks() {
    auto base=setup();name(base,0,0,2);finish(base);
    check(at(base,0,0)==0x22,"unmasked tile pattern");
    auto r3=setup();name(r3,0,0,2);reg(r3,3,0);finish(r3);
    check(at(r3,0,0)==0x00,"R3 masks pattern planes zero and one");
    auto r4=setup();pixel_tile(r4,64,4);name(r4,0,0,64);
    bytes(r4,0xc004,{0x33},3);reg(r4,4,0);finish(r4);
    check(at(r4,0,0)==0x00,"R4 masks pattern planes two and three");
    auto r4full=setup();pixel_tile(r4full,64,4);name(r4full,0,0,64);
    bytes(r4full,0xc004,{0x33},3);finish(r4full);
    check(at(r4full,0,0)==0x33,"R4 permits upper pattern planes");
    auto r2=setup();name(r2,0,0,1);reg(r2,2,0x0d);finish(r2);
    check(at(r2,0,0)==0x00,"R2 name table address mask");
    auto r5=setup();reg(r5,5,0x7e);
    bytes(r5,0x3f00,{255,0xd0});bytes(r5,0x3f80,{0,2});finish(r5);
    check(at(r5,1,0)==0x00,"R5 sprite attribute address bit");
    auto r6=setup();pixel_tile(r6,0,2);pixel_tile(r6,64,1);reg(r6,6,2);
    bytes(r6,0x3f00,{255,0xd0});bytes(r6,0x3f80,{0,64});finish(r6);
    check(at(r6,1,0)==0x2a,"R6 bit zero masks sprite tile bit six");
    auto r6wide=setup();pixel_tile(r6wide,0,2);pixel_tile(r6wide,64,1);
    reg(r6wide,6,3);bytes(r6wide,0x3f00,{255,0xd0});
    bytes(r6wide,0x3f80,{0,64});finish(r6wide);
    check(at(r6wide,1,0)==0x15,"R6 bit zero permits sprite tile bit six");
    auto r6high=setup();pixel_tile(r6high,0,2);pixel_tile(r6high,256,1);
    reg(r6high,6,6);bytes(r6high,0x3f00,{255,0xd0});
    bytes(r6high,0x3f80,{0,0});finish(r6high);
    check(at(r6high,1,0)==0x15,"R6 upper sprite pattern bit");
    auto backdrop=setup();reg(backdrop,7,1);reg(backdrop,1,0);finish(backdrop);
    check(at(backdrop,0,0)==0x15,"R7 selects backdrop CRAM entry");
}
// Authored patterns isolate the review regressions from game checkpoints.
void regression(unsigned finding) {
    if (finding==1) {
        for (unsigned scroll=0;scroll<256;++scroll) {
            auto v=setup();
            for (unsigned row=0;row<28;++row) {
                pixel_tile(v,32+row,1); name(v,row,0,32+row);
                for (unsigned y=0;y<8;++y) bytes(v,(32+row)*32+y*4,{(row&1)?0u:255u,(row&1)?255u:0u});
            }
            reg(v,9,scroll); v.sync(2*frame);
            for (unsigned y=0;y<192;++y)
                check(at(v,y,0)==((((y+scroll)%224)/8&1)?0x22:0x11),"192-line R9 always wraps at 224");
        }
    } else if (finding==2) {
        for (unsigned bank=0;bank<2;++bank) for (unsigned mask=0;mask<4;++mask) {
            auto v=setup();
            for (unsigned tile=0;tile<512;++tile) pixel_tile(v,tile,0);
            const unsigned expected=bank*256+((mask&2)?128:0)+((mask&1)?64:0);
            pixel_tile(v,expected,1);
            reg(v,6,bank*4+mask); bytes(v,0x3f00,{0,0xd0});bytes(v,0x3f80,{16,192});
            finish(v);check(at(v,1,16)==0x15,"R6 masks tile bits 7/6 without masking bank bit");
        }
        auto latched=setup();reg(latched,1,0x42);
        pixel_tile(latched,255,1);pixel_tile(latched,256,2);
        bytes(latched,0x3f00,{241,0xd0});bytes(latched,0x3f80,{16,255});
        latched.sync(100);reg(latched,1,0x40);finish(latched);
        check(at(latched,1,16)==0x15,"latched tall row and later odd index cannot carry into the R6 bank bit");
    } else if (finding==3) {
        for (unsigned n=1;n<=8;++n) {
            auto v=setup();reg(v,1,0x41);pixel_tile(v,3,1,0x80);
            for (unsigned i=0;i<n;++i) {bytes(v,0x3f00+i,{0});bytes(v,0x3f80+i*2,{16+i*24,3});}
            bytes(v,0x3f00+n,{0xd0});finish(v);
            for (unsigned i=0;i<n;++i) {
                check(at(v,1,16+i*24)==0x15 && at(v,2,16+i*24)==0x15,"all selected sprites zoom vertically");
                check(at(v,1,17+i*24)==((i+4<n)?0x15:0),"SMS1 horizontally zooms first max(0,N-4)");
            }
        }
    } else if (finding==4) {
        for (bool before:{true,false}) {
            auto v=setup();bytes(v,0x3f00,{0,0xd0});bytes(v,0x3f80,{32,2});
            // CPU slot 263 precedes entry zero's X/index fetch at 265;
            // a request at dot 265 instead waits until slot 289.
            v.sync(before?174:176);timed_byte(v,0x3f80,96);finish(v);
            check(at(v,1,before?96:32)==0x2a && at(v,1,before?32:96)==0,
                  "sprite X changes only when its prior-line fetch has not passed");
        }
        for (bool before:{true,false}) {
            auto v=setup();bytes(v,0x3f00,{0,0xd0});bytes(v,0x3f80,{32,2});
            v.sync(before?1:8);timed_byte(v,0x3f00,0xd0);finish(v);
            check(at(v,1,32)==(before?0:0x2a),"sprite Y selection retains a late-overwritten entry");
        }
        for (unsigned field=0;field<4;++field) {
            auto v=setup();bytes(v,0x3f00,{0,0xd0});bytes(v,0x3f80,{32,2});
            v.sync(228);
            const unsigned address=field==0?0x3f00:field==1?0x3f80:field==2?0x3f81:2*32+1;
            timed_byte(v,address,field==0?0xd0:field==1?96:0);
            finish(v);check(at(v,1,32)==0x2a,"sprite Y/X/index/pattern fetched before its display line");
        }
        auto wrap=setup();pixel_tile(wrap,3,0);bytes(wrap,3*32,{0x80});
        for (unsigned i=0;i<40;++i) bytes(wrap,0x3f00+i,{100});
        bytes(wrap,0x3f28,{255,0xd0});bytes(wrap,0x3fd0,{32,3});
        wrap.sync(2*frame);
        check(at(wrap,0,32)==0x15,"line 260 starts the Y scan feeding next-frame line zero");
    } else if (finding==5) {
        auto v=setup();bytes(v,0x3f00,{0,0,0,0,0xd0});
        bytes(v,0x3f80,{32,2,32,2,160,2,160,2});
        v.sync(228+32);check(v.read_status(0)&32,"first collision sets status");
        v.sync(228+114);check(v.read_status(0)&32,"later collision reasserts cleared status");
    } else if (finding==6) {
        auto v=setup();v.sync(228+60);v.write_control(15);v.write_control(0xc0);v.write_data(63);
        finish(v);
        check(std::count(v.picture().begin()+256,v.picture().begin()+512,63)==1,"active CRAM write emits exactly one written-color dot");
    } else throw std::runtime_error("Unknown regression");
}
}
int main(int argc,char** argv) { try { if(argc==2) {regression(std::stoul(argv[1]));return 0;} background();background_color_zero();timed_background();sprites();sprite_flags();scroll();register_masks();
    for(unsigned finding=1;finding<=6;++finding) regression(finding);
    std::cout<<"VDP picture tests passed.\n";return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
