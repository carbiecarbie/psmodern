#pragma once
// Milestone 10: the widescreen picture shared by the frame driver (which
// fills it) and the host (which presents it).

#include <array>
#include <cstdint>

namespace psmodern::app {

// 344 x 192 colour values in the same encoding as Vdp::Picture. The original
// 256-column picture sits at x = wide_centre_x.
inline constexpr int wide_width=344;
inline constexpr int wide_height=192;
inline constexpr int wide_centre_x=44;
using WidePicture=std::array<std::uint8_t,wide_width*wide_height>;

} // namespace psmodern::app
