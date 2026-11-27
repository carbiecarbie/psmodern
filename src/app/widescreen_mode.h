// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace psmodern::app {
// These values also order the Options choices and encode the settings token.
enum class WidescreenMode { off=0, on=1, fill=2 };

constexpr bool uses_wide_canvas(WidescreenMode mode) { return mode!=WidescreenMode::off; }
}
