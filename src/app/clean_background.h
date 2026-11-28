// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/console.h"

namespace psmodern::app {
// Captured at the first instruction boundary of the picture, alongside the
// widescreen snapshot. The image pointer keeps a live language switch from
// changing the tables used to interpret a previously completed frame.
struct RetroFrameSnapshot {
    std::uint64_t captured_cycle=0;
    std::shared_ptr<const Memory::Rom> rom;
    std::array<std::uint8_t,8192> ram{};
    std::array<std::uint8_t,0x4000> vram{};
    std::array<std::uint8_t,32> cram{};
    std::array<std::uint8_t,11> registers{};
};
struct CleanFrame {
    RetroFrameSnapshot state;
    Vdp::Picture native{}, background{};
    Vdp::WindowPalette palette{};
    // Rows of the native window name table during this picture's scanout.
    std::array<std::uint8_t,64*24> window_table{};
    std::uint64_t completed_cycle=0;
};

// Video ports reach this private scanout in cycle order. Only window data is
// withheld from its VRAM. Scene updates, sprites, scrolling and palette
// changes therefore reach exactly the pixels that had yet to be scanned.
class CleanBackground final : public Console::VideoObserver {
public:
    explicit CleanBackground(const Console& machine) { reset(machine); }
    void reset(const Console& machine);
    // Resynchronize at an in-game transition. A segment may have already
    // published this frame; keep that result valid until its next capture.
    void reseed(const Console& machine);
    void video_access(const Console& machine,PortAccess access,const RoutineEntry* writer,
                      std::uint16_t entry_sp) override;
    void capture(const Console& machine);
    const CleanFrame* shown() const { return has_shown_ ? &shown_ : nullptr; }
private:
    Vdp scene_;
    Vdp::PaletteScan palette_;
    RetroFrameSnapshot next_;
    CleanFrame shown_;
    std::array<std::uint8_t,64*24> next_window_table_{};
    std::uint64_t window_frame_start_=0;
    unsigned next_window_row_=0;
    bool has_next_=false,has_shown_=false;
    void sample_window_rows(const Console& machine);
};
}
