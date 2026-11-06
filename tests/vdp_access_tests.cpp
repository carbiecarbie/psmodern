// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/vdp.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>
using psmodern::Vdp;
namespace {
constexpr unsigned frame=228*262;
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void reg(Vdp& v,unsigned n,unsigned value) { v.write_control(value);v.write_control(0x80|n); }
void address(Vdp& v,unsigned value,unsigned code=1) {
    v.write_control(value&255);v.write_control((value>>8)|(code<<6));
}
unsigned cycle(unsigned dot) { return (2*dot)/3; }
// Bus completions, expressed in the model's dot coordinates. Exact positions
// are Reference emulator evidence; the SMS manual specifies access spacing.
const std::vector<unsigned> active{3,35,67,99,131,163,195,227,
    257,259,261,263,289,291,293,295,297,323,325};
std::vector<unsigned> blank() {
    std::vector<unsigned> dots;
    for (unsigned d=3;d<256;d+=4) dots.push_back(d);
    for (unsigned d=257;d<342;d+=2) dots.push_back(d);
    return dots;
}
void slots(unsigned line,bool enabled,const std::vector<unsigned>& dots,unsigned next_dot) {
    for (unsigned start=0;start<228;++start) {
        Vdp v;reg(v,1,enabled?0x40:0);
        v.sync(line*228+start);address(v,0x1234);v.write_data(0x5a);
        auto slot=std::find_if(dots.begin(),dots.end(),[&](unsigned d){return cycle(d)>start;});
        const auto end=slot==dots.end() ? 228+cycle(next_dot) : cycle(*slot);
        v.sync(line*228+end-1);
        check(v.vram()[0x1234]==0,"VRAM changed before its slot");
        v.sync(line*228+end);
        check(v.vram()[0x1234]==0x5a,"VRAM did not change at its exact slot");
    }
}
void pending() {
    Vdp v;v.sync(8*228+2);address(v,0x100);v.write_data(0x11);
    v.sync(8*228+3);v.write_data(0x22);
    v.sync(8*228+22);check(v.vram()[0x100]==0,"pending writes wait");
    v.sync(8*228+23);
    check(v.vram()[0x100]==0x22 && v.vram()[0x101]==0,"second write replaces the shared latch");
    v.write_data(0x33);v.sync(8*228+44);
    check(v.vram()[0x101]==0x33,"address increments at service, once per slot");
    v.write_data(0x44);address(v,0x120);v.sync(8*228+66);
    check(v.vram()[0x102]==0 && v.vram()[0x120]==0x44,"pending write uses the live address");
    // A register change must affect arbitration, even for an already pending request.
    Vdp off;off.sync(8*228+2);address(off,0x200);off.write_data(0xaa);
    off.sync(8*228+3);reg(off,1,0);off.sync(8*228+4);
    check(off.vram()[0x100]==0xaa && off.vram()[0x200]==0,
          "display-off register command retargets the pending write to the earlier blank slot");
    Vdp palette;palette.sync(8*228+2);address(palette,0x10,3);palette.write_data(0xff);
    palette.read_status(0);palette.sync(8*228+22);
    check(palette.cram()[16]==0,"CRAM write waits for its slot, including after status read");
    palette.sync(8*228+23);
    check(palette.cram()[16]==0x3f && palette.read_data()==0xff,
          "slot-served CRAM write masks color but preserves the shared buffer");
}
void reads() {
    Vdp v;reg(v,1,0);address(v,0x100);v.write_data(0xa1);v.sync(2);
    v.write_data(0xb2);v.sync(4);reg(v,1,0x40);
    v.sync(8*228+2);address(v,0x100,0);
    check(v.read_data()==0xb2,"read command leaves old buffer until its slot");
    v.sync(8*228+22);check(v.read_data()==0xb2,"early reads coalesce without increment");
    v.sync(8*228+23);check(v.read_data()==0xa1,"read-ahead completes at the active slot");
    v.sync(8*228+44);check(v.read_data()==0xb2,"read-ahead increments at service");
    v.sync(8*228+66);
    address(v,0x100);v.write_data(0xcc);
    check(v.read_data()==0xcc,"read during pending write returns the shared latch");
    auto copy=v;
    v.sync(8*228+87);copy.sync(8*228+87);
    check(v==copy && v.vram()[0x100]==0xcc,"pending state survives a snapshot");
    check(v.read_data()==0xcc,"write has priority over pending read");
    v.sync(8*228+108);check(v.read_data()==0xb2,"pending read follows write at the next address");
    // Address wrap is driven by completion, for reads as well as writes.
    Vdp wrap;reg(wrap,1,0);address(wrap,0x3fff);wrap.write_data(0x6a);wrap.sync(2);
    wrap.write_data(0x7b);wrap.sync(4);address(wrap,0x3fff,0);wrap.sync(7);
    check(wrap.read_data()==0x6a,"read prefetch at 3fff");wrap.sync(10);
    check(wrap.read_data()==0x7b,"read prefetch wraps to zero");
}
void boundary() {
    Vdp v;v.sync(frame-5);address(v,0x72c);v.write_data(0xf4);
    v.sync(frame);check(v.vram()[0x72c]==0,"last-line write survives frame boundary");
    auto copy=v;
    v.sync(frame+1);check(v.vram()[0x72c]==0,"line-zero slot not completed early");
    v.sync(frame+2);copy.sync(frame+2);
    check(v==copy && v.vram()[0x72c]==0xf4,"line-zero slot completes after boundary");
}
}
int main() { try {
    slots(8,true,active,3);slots(192,true,blank(),3);
    slots(8,false,blank(),3);slots(192,false,blank(),3);
    auto last_blank=blank();last_blank.erase(std::remove_if(last_blank.begin(),last_blank.end(),
        [](unsigned dot){return dot>=327;}),last_blank.end());
    slots(260,true,last_blank,1);
    std::vector<unsigned> pre;
    for (unsigned d=1;d<256;d+=2) if ((d-1)%8!=2 || !((d-1)&24)) pre.push_back(d);
    pre.insert(pre.end(),active.begin()+8,active.end());slots(261,true,pre,3);
    pending();reads();boundary();
    std::cout<<"VDP access tests passed.\n";return 0;
} catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
