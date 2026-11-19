// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "replay/checkpoint.h"
#include <string>

namespace psmodern::replay {
using Manifest = std::array<std::string,7>;
inline constexpr std::array<const char*,7> manifest_components{
    "picture","ram","vram","cram","cart-ram","cpu","psg"};
Manifest manifest(const Console&, const std::vector<std::uint8_t>& frame_writes);
void compare_manifests(const std::string& recording, std::uint64_t frame,
                       const Manifest& original, const Manifest& replacement);
}
