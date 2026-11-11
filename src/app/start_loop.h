// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/sdl_host.h"
#include <filesystem>
#include <optional>

namespace psmodern::app {
// psmodern.exe without arguments: forgets remembered ROMs that are missing or
// changed, asks for a ROM while none is remembered, then runs the start screen
// in a new window left in display. Returns the ROM to play, or nothing when
// the player cancels or quits.
std::optional<std::filesystem::path> run_launcher(std::optional<Display>& display);
}
