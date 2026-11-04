// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace psmodern {
std::string sha1(std::span<const std::uint8_t> bytes);
}
