// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"
#include <string_view>

namespace psmodern::game {
inline constexpr RoutineEntry controller_poll_entry{0,0x0339};
// Only identified game images get replacements. Authored runner fixtures keep
// an empty registry. The caller must have verified this digest against its ROM.
bool register_routines(RoutineRegistry& registry, std::string_view rom_sha1);
}
