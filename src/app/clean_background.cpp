// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/clean_background.h"
#include "game/menu.h"
#include <algorithm>

namespace psmodern::app {
namespace {
void snapshot(const Console& machine,RetroFrameSnapshot& result) {
    result.captured_cycle=machine.cycles();
    result.rom=machine.memory().image();
    result.ram=machine.memory().ram();
    result.vram=machine.vdp().vram();
    result.cram=machine.vdp().cram();
    result.registers=machine.vdp().registers();
}
bool window_write(const Console& machine,const RoutineEntry& unit,std::uint16_t entry_sp) {
    if (unit.bank!=0) return false;
    const auto stack_word=[&](unsigned offset) {
        return machine.peek(static_cast<std::uint16_t>(entry_sp+offset)) |
               (machine.peek(static_cast<std::uint16_t>(entry_sp+offset+1))<<8);
    };
    // The opening caption (01:436C) uses the same row copier as windows.
    // During the data writes its return address follows the saved row count.
    if (unit==game::menu::row_drawer_unit && stack_word(2)==0x437D) return false;
    // The alternate dialogue's page clear calls the general VRAM fill.
    // All other users of the fill, including clearing a scene, draw normally.
    if (unit.address==0x0363) return stack_word(0)==0x33C6;
    // Direct port writers in the menu engine, including the cursor, glyph,
    // number, name, money and shop printers and the two rectangle drawers.
    return unit.address>=game::menu::cursor_blink_unit.address &&
           unit.address<=game::menu::save_under_unit.address;
}
}
void CleanBackground::reset(const Console& machine) {
    has_shown_=false;
    reseed(machine);
}
void CleanBackground::reseed(const Console& machine) {
    scene_=machine.vdp();
    // A live opt-in can start halfway through an already drawn picture.
    // Publish the first full pair after that fragment. Power-on, however,
    // supplies the start of the first picture and can show that very frame.
    has_next_=machine.cycles()%Console::frame_cycles==0;
    snapshot(machine,next_);
    window_frame_start_=machine.cycles()/Console::frame_cycles*Console::frame_cycles;
    next_window_row_=0;
}
void CleanBackground::sample_window_rows(const Console& machine) {
    // Native window writers target the table at 0x3800. Take the state at
    // the first scanline of each tile row, before the next port write
    // commits. The picture can show a partial tile when a write reaches the
    // VDP mid-row. Enhanced reveals its replacement with the complete row.
    constexpr unsigned line_cycles=Console::frame_cycles/262, stride=64;
    const auto& vdp=machine.vdp();
    if (vdp.cycle()<window_frame_start_) return;
    const auto elapsed=vdp.cycle()-window_frame_start_;
    while (next_window_row_<next_window_table_.size()/stride && elapsed>=8*next_window_row_*line_cycles) {
        const auto offset=stride*next_window_row_++;
        std::copy_n(vdp.vram().begin()+0x3800+offset,stride,next_window_table_.begin()+offset);
    }
}
void CleanBackground::video_access(const Console& machine,PortAccess access,const RoutineEntry* writer,
                                   std::uint16_t entry_sp) {
    sample_window_rows(machine);
    scene_.sync(access.cycle,palette_);
    const bool control=(access.port&1)!=0;
    if (!access.write) {
        if (control) (void)scene_.read_status(access.value);
        else (void)scene_.read_data();
    } else if (control) scene_.write_control(access.value);
    else if (writer && window_write(machine,*writer,entry_sp)) scene_.retain_vram_data(access.value);
    else scene_.write_data(access.value);
}
void CleanBackground::capture(const Console& machine) {
    scene_.sync(machine.vdp().cycle(),palette_);
    sample_window_rows(machine);
    has_shown_=has_next_;
    if (has_shown_) {
        shown_.state=std::move(next_);
        shown_.completed_cycle=machine.cycles();
        shown_.native=machine.picture();
        shown_.background=scene_.picture();
        shown_.palette=palette_.completed;
        shown_.window_table=next_window_table_;
    }
    snapshot(machine,next_);
    window_frame_start_=machine.cycles()/Console::frame_cycles*Console::frame_cycles;
    next_window_row_=0;
    has_next_=true;
}
}
