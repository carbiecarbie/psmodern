// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/routines.h"

namespace psmodern::game {
// Private FM registry. The caller supplies the full verified Japanese image,
// bank 0C in slot 2, and an enabled FM work byte.
void register_japanese_sound(RoutineRegistry& registry);
}
