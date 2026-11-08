// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "hw/initial_state.h"
#include <array>
#include <cstdint>
#include <optional>

namespace psmodern {
class Vdp {
public:
    void sync(std::uint64_t cycle);
    std::uint8_t read_data();
    std::uint8_t read_status(std::uint8_t open_bus);
    void write_data(std::uint8_t value);
    void write_control(std::uint8_t value);
    bool irq() const { return (frame_flag_ && (regs_[1] & 0x20)) || (line_flag_ && (regs_[0] & 0x10)); }
    std::uint8_t v_counter() const;
    std::uint8_t h_counter() const { return h_latch_; }
    void latch_h_counter();
    std::optional<std::uint64_t> next_irq() const;
    std::uint64_t cycle() const { return cycle_; }
    std::uint8_t line_counter() const { return line_counter_; }
    std::uint16_t dot() const { return static_cast<std::uint16_t>(((2*initial_state::vdp_dot + cycle_*3)/2)%342); }
    const auto& vram() const { return vram_; }
    const auto& cram() const { return cram_; }
    const auto& registers() const { return regs_; }
    using Picture = std::array<std::uint8_t,256*192>;
    const Picture& picture() const { return completed_picture_; }
    bool operator==(const Vdp&) const = default;
private:
    friend struct VdpFixture;
    std::array<std::uint8_t, 0x4000> vram_{};
    std::array<std::uint8_t, 32> cram_ = initial_state::cram;
    std::array<std::uint8_t, 11> regs_ = initial_state::vdp_registers;
    std::uint64_t cycle_ = initial_state::cpu_cycle;
    std::uint16_t address_ = initial_state::vdp_address;
    std::uint8_t code_ = initial_state::vdp_code, buffer_ = initial_state::vdp_buffer,
        line_counter_ = initial_state::vdp_line_counter, h_latch_ = initial_state::h_latch;
    bool control_latch_ = false, frame_flag_ = initial_state::vdp_frame_flag, line_flag_ = false;
    bool frame_visible_ = initial_state::vdp_frame_flag;
    bool overflow_ = initial_state::vdp_overflow, collision_ = false;
    Picture drawing_picture_{}, completed_picture_{};
    std::uint8_t horizontal_scroll_ = initial_state::vdp_registers[8];
    std::uint8_t vertical_scroll_ = initial_state::vdp_registers[9];
    std::array<bool,256> collisions_{};
    struct Sprite {
        unsigned index=0, row=0, pattern=0;
        int x=0;
        std::array<std::uint8_t,4> planes{};
        bool operator==(const Sprite&) const = default;
    };
    std::array<Sprite,8> preparing_sprites_{}, prepared_sprites_{};
    unsigned evaluation_index_=0, evaluation_count_=0, prepared_count_=0;
    bool evaluation_done_=false, overflow_pending_=false;
    enum class Write { none, vram, cram };
    Write write_pending_ = Write::none;
    bool read_pending_ = false;
    // Pipeline state belongs to the snapshot, including partially fetched tiles.
    std::uint64_t processed_dot_ = 0;
    std::uint16_t tile_name_ = 0, tile_pattern_ = 0;
    std::array<std::uint8_t, 4> tile_planes_{};
    std::array<std::uint8_t, 256> background_{}, sprites_{};
    unsigned fine_scroll_ = 0;
    void cpu_slot();
    bool cpu_slot_available(unsigned line, unsigned dot) const;
    void fetch_background(unsigned line, unsigned dot);
    void draw_pixel(unsigned line, unsigned x);
    void evaluate_sprite(unsigned target_line);
    void fetch_sprites(unsigned dot);
    void prepare_sprite_pixels();
};
}
