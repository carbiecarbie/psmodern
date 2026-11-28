// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/language.h"
#include <cstdint>
#include <string_view>

namespace psmodern::app::retro {
enum class SpellScene { field, battle };
// Names and costs come from the cartridge. Only these effect summaries are
// host-authored. A spell with different uses describes its current context.
std::string_view spell_help(std::uint8_t id, Language language, SpellScene scene = SpellScene::field);
std::string_view item_help(std::uint8_t id, Language language);
}
