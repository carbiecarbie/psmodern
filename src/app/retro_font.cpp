// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_font.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <stdexcept>

namespace psmodern::app::retro {
namespace {
struct Glyph {
    char32_t character;
    int width;
    std::array<std::uint8_t, glyph_height> rows;
};
constexpr Glyph glyphs[] = {
#include "retro_glyphs.inc"
};
constexpr unsigned ascii_count = 95;
static_assert(std::is_sorted(std::begin(glyphs), std::end(glyphs),
    [](const Glyph& a, const Glyph& b) { return a.character < b.character; }));

const Glyph* find_glyph(char32_t code) {
    if (code >= U' ' && code <= U'~') return &glyphs[code - U' '];
    auto it = std::lower_bound(std::begin(glyphs) + ascii_count, std::end(glyphs), code,
        [](const Glyph& glyph, char32_t c) { return glyph.character < c; });
    return it != std::end(glyphs) && it->character == code ? it : nullptr;
}
struct Letter {
    const Glyph& glyph;
    unsigned bytes;
};
Letter next_letter(std::string_view input) {
    const auto lead = static_cast<unsigned char>(input.front());
    if (lead >= ' ' && lead <= '~') return {glyphs[lead - ' '], 1};
    const auto& fallback = glyphs['?' - ' '];
    // A bad sequence consumes one byte. A valid but unsupported code point
    // occupies one fallback glyph, including a four-byte scalar.
    if (lead < 0xc2 || lead > 0xf4) return {fallback, 1};
    const unsigned size = lead < 0xe0 ? 2 : (lead < 0xf0 ? 3 : 4);
    if (size > input.size()) return {fallback, 1};
    char32_t code = lead & (0x7f >> size);
    for (unsigned i = 1; i < size; ++i) {
        const auto part = static_cast<unsigned char>(input[i]);
        if ((part & 0xc0) != 0x80) return {fallback, 1};
        code = (code << 6) | (part & 0x3f);
    }
    if ((size == 3 && code < 0x800) || (size == 4 && code < 0x10000) ||
        (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff) return {fallback, 1};
    const auto* glyph = find_glyph(code);
    return {glyph ? *glyph : fallback, size};
}

void fill(std::span<std::uint32_t> pixels, int x, int y, int w, int h, std::uint32_t color) {
    const int left = std::max(x, 0), right = std::min(x + w, width);
    const int bottom = std::min(y + h, height);
    if (left >= right) return;
    for (int row = std::max(y, 0); row < bottom; ++row) {
        auto offset = row * width + left;
        std::fill_n(pixels.begin() + offset, right - left, color);
    }
}

// Move one word to a short last line when both resulting lines still have
// two words or more. Paragraphs and explicit hard breaks remain independent.
void balance_last_line(Lines& lines, std::size_t start, int budget) {
    if (lines.size() - start < 2 || lines.back().find(' ') != std::string::npos) return;
    auto& previous = lines[lines.size() - 2];
    const auto cut = previous.rfind(' ');
    if (cut == previous.find(' ')) return;
    std::string candidate = previous.substr(cut + 1) + " " + lines.back();
    if (text_width(candidate) <= budget) {
        lines.back() = std::move(candidate);
        previous.resize(cut);
    }
}

void wrap_paragraph(std::string_view paragraph, int budget, Lines& output) {
    const auto start = output.size();
    std::string line;
    int used = 0;
    while (!paragraph.empty()) {
        const auto begin = paragraph.find_first_not_of(" \t\r");
        if (begin == std::string_view::npos) break;
        paragraph.remove_prefix(begin);
        const auto word = paragraph.substr(0, paragraph.find_first_of(" \t\r"));
        paragraph.remove_prefix(word.size());
        const int word_width = text_width(word);
        // One inter-letter gap on each side of the space glyph.
        constexpr int separator = 2 + glyphs[0].width;
        if (!line.empty() && used + separator + word_width <= budget) {
            line += ' ';
            line += word;
            used += separator + word_width;
            continue;
        }
        if (!line.empty()) {
            output.push_back(std::move(line));
            line.clear();
            used = 0;
        }
        if (word_width <= budget) {
            line.assign(word);
            used = word_width;
            continue;
        }
        // Preserve a full name or unbroken token even in a very narrow box.
        for (auto rest = word; !rest.empty();) {
            const auto letter = next_letter(rest);
            if (!line.empty() && used + 1 + letter.glyph.width > budget) {
                output.push_back(std::move(line));
                line.clear();
                used = 0;
            }
            used += (line.empty() ? 0 : 1) + letter.glyph.width;
            line += rest.substr(0, letter.bytes);
            rest.remove_prefix(letter.bytes);
        }
    }
    output.push_back(std::move(line));
    balance_last_line(output, start, budget);
}
} // namespace

bool has_glyph(char32_t character) { return find_glyph(character) != nullptr; }

int text_width(std::string_view line) {
    int total = 0;
    while (!line.empty()) {
        const auto letter = next_letter(line);
        total += (total ? 1 : 0) + letter.glyph.width;
        line.remove_prefix(letter.bytes);
    }
    return total;
}

void text(std::span<std::uint32_t> pixels, int x, int y, std::string_view line,
          std::uint32_t color, Align align) {
    if (pixels.size() < width * height) return;
    if (align == Align::right) x -= text_width(line);
    if (align == Align::center) x -= text_width(line) / 2;
    while (!line.empty() && x < width) {
        const auto letter = next_letter(line);
        for (int row = 0; row < glyph_height; ++row) {
            const int yy = y - 2 + row;
            if (yy < 0 || yy >= height) continue;
            for (int col = 0; col < letter.glyph.width; ++col) {
                const int xx = x + col;
                if (xx >= 0 && xx < width && (letter.glyph.rows[row] & (1u << col)))
                    pixels[yy * width + xx] = color;
            }
        }
        x += letter.glyph.width + 1;
        line.remove_prefix(letter.bytes);
    }
}

void box(std::span<std::uint32_t> pixels, int x, int y, int w, int h, std::uint32_t color) {
    if (pixels.size() < width * height || w < 4 || h < 4) return;
    fill(pixels, x + 1, y, w - 2, h, black);
    fill(pixels, x, y + 1, w, h - 2, black);
    fill(pixels, x + 2, y, w - 4, 2, color);
    fill(pixels, x + 2, y + h - 2, w - 4, 2, color);
    fill(pixels, x, y + 2, 2, h - 4, color);
    fill(pixels, x + w - 2, y + 2, 2, h - 4, color);
    for (int corner_y : {y + 1, y + h - 3})
        for (int corner_x : {x + 1, x + w - 3})
            fill(pixels, corner_x, corner_y, 2, 2, color);
}

void divider(std::span<std::uint32_t> pixels, int x, int y, int w, std::uint32_t color) {
    if (pixels.size() >= width * height) fill(pixels, x, y, w, 1, color);
}

Lines wrap(std::string_view message, int max_width) {
    if (max_width <= 0) throw std::invalid_argument("retro text width must be positive");
    Lines result;
    for (;;) {
        const auto end = message.find('\n');
        wrap_paragraph(message.substr(0, end), max_width, result);
        if (end == std::string_view::npos) break;
        message.remove_prefix(end + 1);
    }
    return result;
}

std::vector<Lines> paginate(std::string_view message, int max_width, unsigned lines_per_page) {
    if (!lines_per_page) throw std::invalid_argument("retro page must have at least one line");
    auto lines = wrap(message, max_width);
    std::vector<Lines> pages;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i % lines_per_page == 0) pages.emplace_back();
        pages.back().push_back(std::move(lines[i]));
    }
    return pages;
}
}
