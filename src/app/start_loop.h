// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/sdl_host.h"
#include "app/session_roms.h"
#include <filesystem>
#include <optional>

namespace psmodern::app {
// psmodern.exe without arguments: forgets remembered ROMs that are missing or
// changed, asks for a ROM while none is remembered, then runs the start screen
// in a new window left in display. Returns verified session ROMs, or nothing when
// the player cancels or quits.
std::optional<SessionRoms> run_launcher(std::optional<Display>& display,Language* current_language=nullptr);
}
