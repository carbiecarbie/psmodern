// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <array>

namespace psmodern::game::dungeon_view {
// Load a packed little-endian record pointer using the cartridge's register order.
template<class Z>
void read_pointer(Z& z,std::uint16_t at) {
    z.load_from_hl(at,z.r.a);
    z.increment_hl(at+1);
    z.load_from_hl(at+2,z.r.h);
    z.copy(at+3,z.r.l,z.r.a);
}
inline constexpr RoutineEntry cone{1,0x6C46};
inline constexpr RoutineEntry frame{1,0x6AED};
inline constexpr std::array<std::uint16_t,13> frame_entries{
    0x6AED,0x6AF6,0x6AFC,0x6B1A,0x6B1C,0x6B1D,0x6B27,0x6B35,
    0x6B39,0x6B3A,0x6B3F,0x6B45,0x6B5E
};
// The same front, left and right checks appear at three successive distances.
inline constexpr std::array<std::uint16_t,3> depth{0x6C6A,0x6CA4,0x6CDE};
inline constexpr std::array<std::uint16_t,5> depth_offsets{5,0x12,0x1B,0x24,0x2C};
inline constexpr std::array<std::uint16_t,12> other_entries{
    0x6C46,0x6C4C,0x6C9F,0x6CD9,0x6D13,0x6D17,0x6D21,0x6D31,
    0x6D34,0x6D42,0x6D45,0x6D51
};
constexpr bool contains(RoutineEntry entry) {
    if (entry.bank!=cone.bank) return false;
    for (auto pc:other_entries) if (entry.address==pc) return true;
    for (auto pc:depth)
        for (auto offset:depth_offsets) if (entry.address==pc+offset) return true;
    return false;
}
void register_cone(RoutineRegistry& registry);
void register_frame(RoutineRegistry& registry);
inline constexpr RoutineEntry animation{1,0x6A58},floor_music{1,0x6DE2};
inline constexpr std::array<std::uint16_t,5> animation_entries{0x6A58,0x6A69,0x6A71,0x6AE5,0x6AE8};
void register_animation(RoutineRegistry& registry);
inline constexpr RoutineEntry load{0,0x0F7D},refresh{0,0x0FF3};
inline constexpr std::array<std::uint16_t,17> load_entries{
    0x0F7D,0x0F80,0x0F83,0x0F95,0x0F9E,0x0FA3,0x0FAA,0x0FAD,0x0FC1,
    0x0FC2,0x0FC6,0x0FCB,0x0FD6,0x0FD9,0x0FDC,0x0FEA,0x0FED};
inline constexpr std::array<std::uint16_t,3> spell_cell_entries{0x684A,0x684F,0x6863};
inline constexpr std::array<std::uint16_t,3> escape_choices{0x687A,0x687F,0x6885};
inline constexpr std::array<std::uint16_t,8> retreat_entries{0x688C,0x6891,0x6898,0x689F,0x68A4,0x68AA,0x68B2,0x68B7};
void register_escape(RoutineRegistry& registry);
void register_load(RoutineRegistry& registry);
inline constexpr RoutineEntry overlays{1,0x6E38};
inline constexpr std::array<std::uint16_t,12> overlay_entries{
    0x6E04,0x6E20,0x6E27,0x6E34,0x6E37,0x6E38,0x6E3D,0x6E43,0x6E4C,0x6E64,0x6E6A,0x6E74
};
void register_overlays(RoutineRegistry& registry);
}
