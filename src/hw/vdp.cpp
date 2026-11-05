// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/vdp.h"
#include <algorithm>
#include <stdexcept>

namespace psmodern {
std::uint8_t Vdp::read_data() {
    control_latch_=false;
    auto value=buffer_; buffer_=vram_[address_]; address_=(address_+1)&0x3fff;
    return value;
}
std::uint8_t Vdp::read_status(std::uint8_t open_bus) {
    auto value=static_cast<std::uint8_t>((frame_visible_ ? 0x80 : 0) | (overflow_ ? 0x40 : 0) |
        (collision_ ? 0x20 : 0) | (open_bus & 0x1f));
    frame_flag_=frame_visible_=line_flag_=overflow_=collision_=control_latch_=false;
    return value;
}
void Vdp::write_data(std::uint8_t value) {
    control_latch_=false; buffer_=value;
    if (code_==3) cram_[address_&31]=value&0x3f;
    else vram_[address_]=value;
    address_=(address_+1)&0x3fff;
}
void Vdp::write_control(std::uint8_t value) {
    if (!control_latch_) { address_=(address_&0x3f00)|value; control_latch_=true; return; }
    control_latch_=false; code_=value>>6; address_=(address_&0xff)|((value&0x3f)<<8);
    if (code_==0) { buffer_=vram_[address_]; address_=(address_+1)&0x3fff; }
    else if (code_==2 && (value&15)<regs_.size()) regs_[value&15]=address_&0xff;
}
}

namespace psmodern {
namespace {
constexpr std::uint64_t dots_per_line=342;
// One event at dot 315 sets the frame flag (pending and readable), advances the
// V counter and records sprite overflow; the line counter follows one CPU cycle
// later. Port accesses are at T3 and the H latch sees TH three dots later, so a
// latched H count h was taken at dot 2h-3 (or 2h-2) of the T3 access.
constexpr std::uint64_t irq_dot=315, scroll_dot=313, line_dot=317, vcounter_dot=315, visible_dot=315;
constexpr unsigned h_latch_delay=3;
// The line snapshot precedes pixel zero in VDPTest H-count coordinates.
constexpr std::uint64_t draw_dot=7, pixel_dot=9;
// CPU cycle 0 is VDP dot 1. One T-state advances three half-dots.
constexpr std::uint64_t cpu_at_dot(std::uint64_t dot) {
    return (2*dot-2*initial_state::vdp_dot+2)/3;
}
}
void Vdp::draw_line(unsigned line) {
    const bool visible=line<192;
    const auto backdrop=cram_[16+(regs_[7]&15)];
    std::array<std::uint8_t,256> border{};
    auto* out=visible ? drawing_picture_.data()+line*256 : border.data();
    std::array<bool,256> priority{}, occupied{};
    collision_dot_=342;
    for (unsigned x=0;x<256 && visible;++x) {
        out[x]=backdrop;
        if (!(regs_[1]&0x40) || ((regs_[0]&0x20) && x<8)) continue;
        unsigned scroll=(line<16 && (regs_[0]&0x40)) ? 0 : horizontal_scroll_;
        unsigned fine=scroll&7;
        if (x<fine) continue;
        unsigned column=((x-fine)/8-(scroll>>3))&31;
        unsigned scroll_y=(x>=192 && (regs_[0]&0x80)) ? 0 : vertical_scroll_;
        unsigned row_pixel=(line+scroll_y)%((scroll_y<224)?224:256);
        unsigned row=row_pixel/8, px=(x-fine)&7, py=row_pixel&7;
        unsigned table=((regs_[2]&0x0e)<<10) | ((row&((regs_[2]&1)?31:15))<<6) | (column<<1);
        unsigned name=vram_[table] | (vram_[table+1]<<8);
        if (name&0x200) px=7-px;
        if (name&0x400) py=7-py;
        unsigned tile=name&0x1ff;
        unsigned pattern=(tile*32+py*4)&0x3fff;
        unsigned bit=7-px, color=0;
        for (unsigned plane=0;plane<4;++plane) {
            unsigned address=pattern+plane;
            address &= plane<2 ? ((regs_[3]<<6)|0x3f) : (((regs_[4]&7)<<11)|0x7ff);
            color|=((vram_[address]>>bit)&1)<<plane;
        }
        out[x]=cram_[((name&0x800)?16:0)+color];
        priority[x]=(name&0x1000) && color;
    }
    unsigned sat=(regs_[5]&0x7e)<<7;
    unsigned count=0;
    const unsigned zoom=(regs_[1]&1)?2:1;
    const unsigned height=((regs_[1]&2)?16:8)*zoom;
    std::array<unsigned,8> sprite_index{}, sprite_row{};
    for (unsigned i=0;i<64;++i) {
        unsigned y=vram_[(sat+i)&0x3fff];
        if (y==0xd0) break;
        unsigned start=(y+1)&255;
        unsigned dy=(line-start)&255;
        if (dy>=height) continue;
        if (count==8) break;
        sprite_index[count]=i; sprite_row[count]=dy; ++count;
    }
    if (!(regs_[1]&0x40)) return;
    for (unsigned entry=0;entry<count;++entry) {
        unsigned i=sprite_index[entry], dy=sprite_row[entry];
        unsigned attributes=(sat+((regs_[5]&1)?0x80:0)+i*2)&0x3fff;
        int sx=vram_[attributes]-((regs_[0]&8)?8:0);
        unsigned tile=vram_[(attributes+1)&0x3fff];
        if (regs_[1]&2) tile&=~1u;
        tile|=(regs_[6]&4)<<6;
        if (!(regs_[6]&1)) tile&=~0x40u;
        if (!(regs_[6]&2)) tile&=~0x100u;
        unsigned py=dy/zoom;
        tile+=py/8;
        unsigned pattern=(tile*32+(py&7)*4)&0x3fff;
        unsigned xzoom=(zoom==2 && entry<4)?2:1;
        for (unsigned dx=0;dx<8*xzoom;++dx) {
            int x=sx+static_cast<int>(dx);
            if (x<0 || x>=256) continue;
            unsigned bit=7-dx/xzoom, color=0;
            for (unsigned plane=0;plane<4;++plane) color|=((vram_[(pattern+plane)&0x3fff]>>bit)&1)<<plane;
            if (!color) continue;
            if (occupied[x]) collision_dot_=std::min<unsigned>(collision_dot_,pixel_dot+x);
            else { occupied[x]=true; if (!priority[x] && !((regs_[0]&0x20) && x<8)) out[x]=cram_[16+color]; }
        }
    }
}
bool Vdp::sprite_overflow_on_line(unsigned line) const {
    if (line>=192) return false;
    unsigned sat=(regs_[5]&0x7e)<<7;
    unsigned height=((regs_[1]&2)?16:8)*((regs_[1]&1)?2:1);
    unsigned count=0;
    for (unsigned i=0;i<64;++i) {
        unsigned y=vram_[(sat+i)&0x3fff];
        if (y==0xd0) break;
        if (((line-((y+1)&255))&255)<height && ++count>8) return true;
    }
    return false;
}
void Vdp::sync(std::uint64_t target) {
    if (target < cycle_) throw std::invalid_argument("VDP cannot run backwards");
    const auto old_half=2*initial_state::vdp_dot+3*cycle_, new_half=2*initial_state::vdp_dot+3*target;
    auto first=(old_half/2)/dots_per_line;
    auto last=(new_half/2)/dots_per_line;
    for (auto line=first; line<=last; ++line) {
        auto scanline=line%262;
        auto reached=[&](std::uint64_t dot) {
            auto event=2*(line*dots_per_line+dot);
            return event>old_half && event<=new_half;
        };
        if (scanline==0 && line!=0 && reached(0)) {
            completed_picture_=drawing_picture_;
            vertical_scroll_=regs_[9];
        }
        if (reached(draw_dot)) draw_line(scanline);
        if (collision_dot_<342 && reached(collision_dot_)) collision_=true;
        if (reached(visible_dot) && sprite_overflow_on_line((scanline+1)%262)) overflow_=true;
        if (reached(scroll_dot)) horizontal_scroll_=regs_[8];
        if (scanline==192 && reached(irq_dot)) frame_flag_=true;
        if (reached(line_dot)) {
            if (scanline<192 || scanline==261) {
                if (line_counter_==0) { line_counter_=regs_[10]; line_flag_=true; }
                else --line_counter_;
            } else line_counter_=regs_[10];
        }
        if (scanline==192 && reached(visible_dot) && frame_flag_) frame_visible_=true;
    }
    cycle_=target;
}
std::uint8_t Vdp::v_counter() const {
    auto position=(2*initial_state::vdp_dot+3*cycle_)/2;
    auto line=static_cast<unsigned>((position/dots_per_line)%262);
    if (position%dots_per_line>=vcounter_dot) line=(line+1)%262;
    return static_cast<std::uint8_t>(line<=0xda ? line : line-6);
}
void Vdp::latch_h_counter() {
    // TH reaches the latch three dots after the T3 port write. The late-phase
    // conversion keeps the VDPTest F3/F4/F5 sequence of B2a.
    auto p=static_cast<int>((dot()+h_latch_delay)%dots_per_line);
    h_latch_=static_cast<std::uint8_t>(p<296 ? p/2 : (p-343)/2);
}
std::optional<std::uint64_t> Vdp::next_irq() const {
    if (irq()) return cycle_;
    if (!(regs_[0]&0x10) && !(regs_[1]&0x20)) return {};
    auto copy=*this;
    auto first=((2*initial_state::vdp_dot+3*cycle_)/2)/dots_per_line;
    for (auto line=first; line<=first+524; ++line) {
        for (auto dot:{irq_dot,line_dot}) {
            auto event=cpu_at_dot(line*dots_per_line+dot);
            if (event<=cycle_) continue;
            copy.sync(event);
            if (copy.irq()) return event;
        }
    }
    return {};
}
}
