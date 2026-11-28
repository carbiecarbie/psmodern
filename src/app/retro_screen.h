// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/language.h"
#include "app/retro_field_motion.h"
#include <cstdint>
#include <span>

namespace psmodern::app::retro {
class Cartridge;
struct WindowState;
struct TextPage;

// Draw over the clean 256x192 picture paired with these captured game values.
void render(std::span<std::uint32_t> pixels, const Cartridge& cartridge,
            const WindowState& windows, const TextPage& page, Language language,const FieldMotion& field={});
}
