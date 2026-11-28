// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace psmodern::app::retro {
inline constexpr int width = 256, height = 192, glyph_height = 10;
inline constexpr std::uint32_t white = 0xffffffff, black = 0xff000000,
    gold = 0xfff7d358, grey = 0xff9aa2b1, cyan = 0xff66d9ff, rule = 0xff4a4e5a;
enum class Align { left, right, center };

// UTF-8. The anchor y is the cap line; accents occupy the two pixels above.
// Alignment uses the exact width without a trailing inter-letter gap.
bool has_glyph(char32_t character);
int text_width(std::string_view line);
void text(std::span<std::uint32_t> pixels, int x, int y, std::string_view line,
          std::uint32_t color = white, Align align = Align::left);
void box(std::span<std::uint32_t> pixels, int x, int y, int w, int h, std::uint32_t color = white);
void divider(std::span<std::uint32_t> pixels, int x, int y, int w, std::uint32_t color = rule);

// Hard line breaks (including blank lines) survive pagination. An oversized
// word splits between glyphs. A single glyph may exceed a narrower budget.
using Lines = std::vector<std::string>;
Lines wrap(std::string_view message, int max_width);
std::vector<Lines> paginate(std::string_view message, int max_width, unsigned lines_per_page = 2);
}
