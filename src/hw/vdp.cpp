// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/vdp.h"
#include <algorithm>
#include <stdexcept>

namespace psmodern {
std::uint8_t Vdp::read_data() {
    control_latch_=false;
    auto value=buffer_; read_pending_=true;
    return value;
}
std::uint8_t Vdp::read_status(std::uint8_t open_bus) {
    // Inference from community source: MesenCE ReadPort supplies open-bus low
    // bits in mode 4. This is not a hardware measurement or a ROM observation.
    auto value=static_cast<std::uint8_t>((frame_visible_ ? 0x80 : 0) | (overflow_ ? 0x40 : 0) |
        (collision_ ? 0x20 : 0) | (open_bus & 0x1f));
    frame_flag_=frame_visible_=line_flag_=overflow_=collision_=control_latch_=false;
    return value;
}
void Vdp::write_data(std::uint8_t value) {
    control_latch_=false; buffer_=value;
    write_pending_=code_==3 ? Write::cram : Write::vram;
}
void Vdp::write_control(std::uint8_t value) {
    if (!control_latch_) { address_=(address_&0x3f00)|value; control_latch_=true; return; }
    control_latch_=false; code_=value>>6; address_=(address_&0xff)|((value&0x3f)<<8);
    if (code_==0) read_pending_=true;
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
constexpr std::uint64_t pixel_dot=9;
// CPU cycle 0 is VDP dot 1. One T-state advances three half-dots.
constexpr std::uint64_t cpu_at_dot(std::uint64_t dot) {
    return (2*dot-2*initial_state::vdp_dot+2)/3;
}
}
// Inference from community source: CE's shared write latch and command-prefetch request
// use the live address, with writes taking priority. Data-port read-ahead
// uses the same arbitration here (Inference from the buffered VRAM bus);
// CE instead refills immediately on data reads. Fast mixed/overrun access
// behavior has not been measured on SMS1 hardware.
void Vdp::cpu_slot() {
    if (write_pending_!=Write::none) {
        if (write_pending_==Write::cram) cram_[address_&31]=buffer_&0x3f;
        else vram_[address_]=buffer_;
        write_pending_=Write::none;
    } else if (read_pending_) {
        buffer_=vram_[address_];
        read_pending_=false;
    } else return;
    address_=(address_+1)&0x3fff;
}
bool Vdp::cpu_slot_available(unsigned line, unsigned dot) const {
    // Event dot is one past the bus slot: a slot at 2 completes at 3.
    if (!(dot&1)) return false;
    const unsigned bus=dot-1;
    if (!(regs_[1]&0x40)) return bus>=256 || bus%4==2;
    if (line>=192 && line!=261) {
        // The last blank line starts evaluating sprites for line zero.
        return !(line==260 && bus>=326) && (bus>=256 || bus%4==2);
    }
    if (bus<256) return line<192 ? bus%32==2 : (bus%8!=2 || !(bus&24));
    if (bus<264) return true;
    return (bus>=288 && bus<=296) || bus==322 || bus==324;
}
void Vdp::fetch_background(unsigned line, unsigned dot) {
    const unsigned bus=dot-1, column=bus/8;
    switch (bus&7) {
    case 0: {
        const unsigned scroll=(line<16 && (regs_[0]&0x40)) ? 0 : horizontal_scroll_;
        const unsigned x=(column-(scroll>>3))&31;
        const unsigned sy=(bus>=192 && (regs_[0]&0x80)) ? 0 : vertical_scroll_;
        // Hardware documentation: the 192-line name table always wraps at 224.
        const unsigned y=(line+sy)%224;
        const unsigned table=((regs_[2]&0x0e)<<10) |
            (((y/8)&((regs_[2]&1)?31:15))<<6) | (x<<1);
        tile_name_=vram_[table] | (vram_[table+1]<<8);
        const unsigned row=(tile_name_&0x400) ? 7-(y&7) : y&7;
        tile_pattern_=(tile_name_&0x1ff)*32+row*4;
        break;
    }
    case 4: {
        const unsigned address=tile_pattern_&((regs_[3]<<6)|0x3f);
        tile_planes_[0]=vram_[address]; tile_planes_[1]=vram_[address+1];
        break;
    }
    case 6: {
        const unsigned address=tile_pattern_&(((regs_[4]&7)<<11)|0x7ff);
        tile_planes_[2]=vram_[address+2]; tile_planes_[3]=vram_[address+3];
        for (unsigned px=0;px<8;++px) {
            const unsigned x=column*8+fine_scroll_+px;
            if (x>=256) break;
            const unsigned bit=(tile_name_&0x200) ? px : 7-px;
            unsigned color=0;
            for (unsigned plane=0;plane<4;++plane) color|=((tile_planes_[plane]>>bit)&1)<<plane;
            background_[x]=color | ((tile_name_&0x800)?16:0) |
                (((tile_name_&0x1000) && color)?32:0);
        }
        break;
    }
    }
}
void Vdp::draw_pixel(unsigned line, unsigned x) {
    unsigned color=16+(regs_[7]&15);
    if ((regs_[1]&0x40) && !((regs_[0]&0x20) && x<8)) {
        const auto bg=background_[x];
        if (bg!=64) color=bg&31;
        if (sprites_[x] && !(bg&32)) color=16+sprites_[x];
    }
    drawing_picture_[line*256+x]=cram_[color];
}
void Vdp::evaluate_sprite(unsigned target_line) {
    if (evaluation_done_ || evaluation_index_==64) return;
    const unsigned sat=(regs_[5]&0x7e)<<7;
    const unsigned i=evaluation_index_++;
    const unsigned y=vram_[sat+i];
    if (y==0xd0) { evaluation_done_=true; return; }
    const unsigned zoom=(regs_[1]&1)?2:1;
    const unsigned height=((regs_[1]&2)?16:8)*zoom;
    const unsigned row=(target_line-((y+1)&255))&255;
    if (row>=height) return;
    if (evaluation_count_==8) { overflow_pending_=true; return; }
    auto& sprite=preparing_sprites_[evaluation_count_++];
    sprite.index=i; sprite.row=row/zoom;
}
void Vdp::fetch_sprites(unsigned dot) {
    // Inference from community source (MesenCE LoadSpriteTilesSms): exact bus
    // dots are reference scheduling, not measured SMS1 timings. Our events are
    // one dot after its bus indices. Selection spans the previous hblank and
    // this line; X/index and both pattern words finish before next-line output.
    const unsigned bus=dot-1;
    unsigned pair, phase;
    if (bus<288) { pair=(bus-264)/12; phase=(bus-264)%12; }
    else if (bus>=298 && bus<322) { pair=2+(bus-298)/12; phase=(bus-298)%12; }
    else return;
    if (phase&1) return;
    unsigned entry=pair*2+((phase==2 || phase>=8)?1:0);
    if (entry>=evaluation_count_) return;
    auto& sprite=preparing_sprites_[entry];
    if (phase==0 || phase==2) {
        const unsigned sat=(regs_[5]&0x7e)<<7;
        const unsigned attributes=(sat+((regs_[5]&1)?0x80:0)+sprite.index*2)&0x3fff;
        sprite.x=vram_[attributes];
        unsigned tile=vram_[(attributes+1)&0x3fff];
        if (regs_[1]&2) tile&=~1u;
        // Hardware documentation: R6.1/.0 mask tile bits 7/6; R6.2 is
        // independent address bit 13. MesenCE does not implement these masks.
        if (!(regs_[6]&1)) tile&=~0x40u;
        if (!(regs_[6]&2)) tile&=~0x80u;
        // Address fields are wired OR. A height change after Y evaluation can
        // overlap row/tile bits; addition would carry into the independent bank.
        sprite.pattern=((regs_[6]&4)<<11) | (tile<<5) | (sprite.row<<2);
    } else {
        const unsigned plane=(phase==6 || phase==10)?2:0;
        sprite.planes[plane]=vram_[(sprite.pattern+plane)&0x3fff];
        sprite.planes[plane+1]=vram_[(sprite.pattern+plane+1)&0x3fff];
        if (plane==2 && (regs_[0]&8)) sprite.x-=8;
    }
}
void Vdp::prepare_sprite_pixels() {
    sprites_.fill(0);
    std::array<bool,256> occupied{};
    collisions_.fill(false);
    const unsigned zoom=(regs_[1]&1)?2:1;
    if (!(regs_[1]&0x40)) return;
    for (unsigned entry=0;entry<prepared_count_;++entry) {
        const auto& sprite=prepared_sprites_[entry];
        // Hardware documentation: the final four selected sprites have no
        // horizontal zoom on SMS1, including when fewer than eight are found.
        unsigned xzoom=(zoom==2 && entry+4<prepared_count_)?2:1;
        for (unsigned dx=0;dx<8*xzoom;++dx) {
            int x=sprite.x+static_cast<int>(dx);
            if (x<0 || x>=256) continue;
            unsigned bit=7-dx/xzoom, color=0;
            for (unsigned plane=0;plane<4;++plane) color|=((sprite.planes[plane]>>bit)&1)<<plane;
            if (!color) continue;
            if (occupied[x]) collisions_[x]=true;
            else { occupied[x]=true; sprites_[x]=color; }
        }
    }
}
void Vdp::sync(std::uint64_t target) {
    if (target < cycle_) throw std::invalid_argument("VDP cannot run backwards");
    const auto target_dot=(2*initial_state::vdp_dot+3*target)/2;
    // Advance in chronological order. No event reads VRAM from the future,
    // and already fetched names/planes are never redrawn after a CPU write.
    for (auto absolute=processed_dot_+1; absolute<=target_dot; ++absolute) {
        const unsigned line=(absolute/dots_per_line)%262, dot=absolute%dots_per_line;
        if (dot==0) {
            prepare_sprite_pixels();
            if (line==0) {
                completed_picture_=drawing_picture_;
                vertical_scroll_=regs_[9];
            }
            background_.fill(64);
            fine_scroll_=(line<16 && (regs_[0]&0x40)) ? 0 : horizontal_scroll_&7;
        }
        // At reset dot zero has already passed; initialize its border once.
        if (absolute==1) {
            background_.fill(64);
            fine_scroll_=(regs_[0]&0x40) ? 0 : horizontal_scroll_&7;
        }
        // Prior-line sprite pipeline. The Y scan begins at the preceding
        // hblank's tail, continues in this line, then loads X/index/planes.
        const bool active=line<192 || line==261;
        if (dot<256 && (dot&1)) {
            const unsigned bus=dot-1;
            if (active && (regs_[1]&0x40)) {
                if ((bus&7)==2 && (bus&24)) {evaluate_sprite((line+1)%262);evaluate_sprite((line+1)%262);}
            } else if ((bus&3)==0) {
                if (bus==0) {evaluation_index_=evaluation_count_=0;evaluation_done_=false;}
                evaluate_sprite((line+1)%262);evaluate_sprite((line+1)%262);
            }
        }
        if (dot>=265 && dot<=321 && (dot&1) && (regs_[1]&0x40)) fetch_sprites(dot);
        if (dot==321) {prepared_sprites_=preparing_sprites_;prepared_count_=(regs_[1]&0x40)?evaluation_count_:0;}
        if ((active || line==260) && dot>=327 && dot<=341 && (dot&1) && (regs_[1]&0x40)) {
            if (dot==327) {evaluation_index_=evaluation_count_=0;evaluation_done_=false;}
            evaluate_sprite((line+2)%262);evaluate_sprite((line+2)%262);
        }
        if (line<192) {
            if (dot<256 && (dot&1) && (regs_[1]&0x40)) fetch_background(line,dot);
            if (dot>=pixel_dot && dot<pixel_dot+256) draw_pixel(line,dot-pixel_dot);
        }
        // Hardware documentation: every overlapping opaque pixel reasserts
        // collision, even after an earlier status read in this line.
        if (dot>=pixel_dot && dot<pixel_dot+256 && collisions_[dot-pixel_dot]) collision_=true;
        if (dot==visible_dot) {
            if (overflow_pending_ && (line<192 || line==261)) overflow_=true;
            overflow_pending_=false;
        }
        if (dot==scroll_dot) horizontal_scroll_=regs_[8];
        if (line==192 && dot==irq_dot) frame_flag_=true;
        if (dot==line_dot) {
            if (line<192 || line==261) {
                if (line_counter_==0) { line_counter_=regs_[10]; line_flag_=true; }
                else --line_counter_;
            } else line_counter_=regs_[10];
        }
        if (line==192 && dot==visible_dot && frame_flag_) frame_visible_=true;
        if ((write_pending_!=Write::none || read_pending_) && cpu_slot_available(line,dot)) {
            const bool cram_dot=write_pending_==Write::cram;
            cpu_slot();
            // Hardware documentation describes CRAM dots; the one-pixel
            // completion coordinate is an inference. CE's active path loses
            // this dot (its forced-blank path retains it); see development.md.
            if (cram_dot && line<192 && dot>=pixel_dot && dot<pixel_dot+256)
                drawing_picture_[line*256+dot-pixel_dot]=buffer_&63;
        }
    }
    processed_dot_=target_dot;
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
