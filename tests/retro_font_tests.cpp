// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_font.h"
#include "app/retro_helpers.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

using namespace psmodern::app;
using namespace psmodern::app::retro;

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
void test_glyphs() {
    for (char32_t c = U' '; c <= U'~'; ++c) check(has_glyph(c), "missing ASCII glyph");
    for (auto c : std::u32string_view(U"‘’“”…ÁÀÂÃÇÉÊÍÓÔÕÚÜáàâãçéêíóôõúü·×—→▸▼▲"))
        check(has_glyph(c), "missing accent or punctuation");
    check(!has_glyph(U'猫'), "unsupported character silently accepted");
    check(text_width("l") == 2 && text_width("M") == 5 && text_width("lM") == 8, "proportional advances");
    check(text_width("") == 0 && text_width("á") == 4 && text_width("…") == 5, "UTF-8 advance");
    check(text_width("🌍") == text_width("?") && text_width("\xc3") == text_width("?"), "fallback advance");

    // Synthetic labels with the widths of the longest approved magic rows.
    check(90 - text_width("10") - (15 + text_width("Fewer Boats")) == 11, "two-digit cost gap");
    check(90 - text_width("6") - (15 + text_width("Basic Teller")) == 18, "long spell gap");
    check(90 - text_width("6 MP") - (10 + text_width("Rover Cool")) == 12, "target cost gap");
    std::array<std::uint32_t, width * height> image{};
    for (auto accent : {"Á", "À", "Â", "Ã", "É", "Ê", "Í", "Ó", "Ô", "Õ", "Ú", "Ü"}) {
        image.fill(0);
        text(image, 8, 2, accent);
        check(std::find(image.begin(), image.begin() + 2 * width, white) != image.begin() + 2 * width, "missing accent pixels");
        check(std::all_of(image.begin() + 2 * width, image.begin() + 3 * width, [](auto v) { return v == 0; }), "accent touches uppercase body");
        check(std::count(image.begin() + 3 * width, image.begin() + 4 * width, white) > 0, "missing uppercase body");
    }
    image.fill(0);
    text(image, 5, 2, "l", cyan, Align::right);
    check(image[2 * width + 3] == cyan && image[8 * width + 4] == cyan, "lowercase l shape and alignment");
    check(image[2 * width + 4] == 0, "lowercase l has a spurious second stroke");
    image.fill(0);
    text(image, 255, 191, "W");
    check(image.back() == white && std::count(image.begin(), image.end(), white) == 1, "glyph must clip to viewport");
}
void test_layout() {
    check(wrap("one two red fox", text_width("one two red")) == Lines{"one two", "red fox"}, "last-line orphan");
    check(wrap("ab    cd", text_width("ab cd")) == Lines{"ab cd"}, "exact column fit");
    check(wrap("ab cd", text_width("ab cd") - 1) == Lines{"ab", "cd"}, "one-pixel overflow");
    check(wrap("álá!", 4) == Lines{"á", "l", "á", "!"}, "unbroken UTF-8 token");
    check(wrap("W", 1) == Lines{"W"}, "single wide glyph");
    check(paginate("a\n\nb\nc\n", 224) == std::vector<Lines>{{"a", ""}, {"b", "c"}, {""}}, "two-line pages and blank lines");
    check(paginate("", 224) == std::vector<Lines>{{""}}, "empty message");
    std::array<std::uint32_t, width * height> image{};
    image.fill(cyan);
    box(image, 4, 4, 92, 24);
    check(image[4 * width + 4] == cyan && image[4 * width + 5] == black, "rounded transparent corner");
    check(image[5 * width + 5] == white && image[6 * width + 4] == white, "rounded white corner");
    check(image[12 * width + 4] == white && image[12 * width + 5] == white && image[12 * width + 6] == black, "two-pixel frame");
    check(image[12 * width + 94] == white && image[12 * width + 95] == white && image[12 * width + 96] == cyan, "right edge");
    divider(image, 8, 10, 84);
    check(image[10 * width + 8] == rule && image[10 * width + 91] == rule &&
          image[10 * width + 92] == black && image[11 * width + 8] == black, "one-pixel divider");
    image.fill(cyan);
    box(image, -2, -2, 8, 8);
    check(image[0] == white && image[width + 1] == black && image[6] == cyan, "clipped box");
}
void test_helpers() {
    for (unsigned id = 1; id <= 18; ++id)
        for (auto scene : {SpellScene::field, SpellScene::battle})
            for (auto language : {Language::english, Language::portuguese}) {
                auto help = spell_help(id, language, scene);
                check(!help.empty() && text_width(help) <= 232, "spell helper must fit one line");
            }
    for (unsigned id = 1; id <= 0x40; ++id)
        for (auto language : {Language::english, Language::portuguese}) {
            auto help = item_help(id, language);
            check(!help.empty() && text_width(help) <= 232, "item helper must fit one line");
        }
    check(spell_help(0, Language::english).empty() && item_help(0xff, Language::english).empty(), "unknown entries");
    check(spell_help(0x11, Language::english, SpellScene::field) !=
          spell_help(0x11, Language::english, SpellScene::battle), "context-sensitive spell");
    check(item_help(0x24, Language::english).find("10 HP") != std::string_view::npos, "small food effect");
    check(spell_help(2, Language::portuguese).find("80 PF") != std::string_view::npos, "large heal effect");
}
}
int main() {
    try {
        test_glyphs();
        test_layout();
        test_helpers();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
