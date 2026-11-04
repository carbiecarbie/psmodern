// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <Z80.h>
#include <array>
#include <cstdint>

namespace psmodern::initial_state {
// MesenCE 2.2.1's no-BIOS post-BIOS state; see docs/development.md.
inline constexpr std::array<std::uint8_t, 11> vdp_registers{0x36, 0x60, 0x0f, 0, 0, 0x7e, 3, 0, 0, 0, 0xff};
inline constexpr std::array<std::uint8_t, 32> cram{
    0x00,0x3f,0x3e,0x3f,0x30,0x30,0x38,0x3f,0x37,0x3f,0,0,0,0,0,0,
    0,3,0x30,0x0f,7,0x16,0x3f,2,0,0x0f,0,0,0,0,0,0};
inline constexpr std::array<std::uint8_t, 3> mapper_banks{0,1,2};
inline constexpr std::uint8_t mapper_control = 0;
inline constexpr std::uint8_t memory_control = 0xab;
inline constexpr std::uint8_t io_control = 0x0f;
inline constexpr std::uint16_t vdp_address = 0x3d6a;
inline constexpr std::uint8_t vdp_code = 1, vdp_buffer = 0, vdp_line_counter = 0xfc;
inline constexpr bool vdp_frame_flag = true, vdp_overflow = true;
inline constexpr std::uint8_t ram_marker = 0xab;
inline constexpr std::uint64_t cpu_cycle = 0;
inline constexpr std::uint16_t vdp_dot = 1;
inline constexpr std::uint8_t open_bus = 0, h_latch = 0;
// SmsPsg constructor and zero-initialized SmsPsgState in MesenCE 2.2.1.
inline constexpr std::array<std::uint16_t,3> psg_periods{}, psg_timers{};
inline constexpr std::array<bool,3> psg_outputs{};
inline constexpr std::array<std::uint8_t,4> psg_attenuations{15,15,15,15};
inline constexpr std::uint16_t psg_lfsr = 0x8000, psg_noise_timer = 0;
inline constexpr std::uint8_t psg_latch = 0, psg_noise_control = 0;
inline constexpr bool psg_noise_clock = false, psg_noise_output = false;
inline std::array<std::uint8_t,8192> work_ram() {
    std::array<std::uint8_t,8192> ram{};
    ram[0]=ram_marker;
    return ram;
}
inline void cpu(Z80& state) {
    z80_power(&state, 0);
    state.af.uint16_value=0xab42; state.bc.uint16_value=0x0002;
    state.de.uint16_value=0xa51b; state.hl.uint16_value=0;
    state.af_.uint16_value=0; state.bc_.uint16_value=0x0200;
    state.de_.uint16_value=0xc700; state.hl_.uint16_value=0xc75f;
    state.ix_iy[0].uint16_value=state.ix_iy[1].uint16_value=0;
    state.pc.uint16_value=0; state.sp.uint16_value=0xdff0;
    state.i=0; state.r=0x1d; state.r7=0; state.im=1; state.iff1=state.iff2=0;
}
}
