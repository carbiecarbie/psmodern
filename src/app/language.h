// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rom/rom.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace psmodern::app {
enum class Language { english, portuguese };
enum class LanguagePreference { native, english, portuguese };
constexpr Language native_language(RomVersion version) {
    return version==RomVersion::brazilian ? Language::portuguese : Language::english;
}
constexpr Language resolve_language(LanguagePreference preference,RomVersion profile) {
    return preference==LanguagePreference::native ? native_language(profile) :
        preference==LanguagePreference::portuguese ? Language::portuguese : Language::english;
}
constexpr RomVersion language_rom(Language language) {
    return language==Language::portuguese ? RomVersion::brazilian : RomVersion::us_rev_a;
}
inline constexpr const char* language_file_name="language.txt";
inline constexpr std::size_t language_file_limit=128;
// A global host preference, independent of the unchanged save companions.
// Format: "psmodern-language 1\n", then "native\n", "en\n" or "pt-br\n".
LanguagePreference parse_language(std::string_view text);
std::string format_language(LanguagePreference preference);
struct LanguageLoad {
    LanguagePreference preference=LanguagePreference::native;
    std::optional<std::string> warning;
};
// Missing means native. An unusable file is reported and preserved until Save.
LanguageLoad load_language(const std::filesystem::path& folder);
// Atomic replacement; throws on failure. Only an explicit host Save calls it.
void save_language(const std::filesystem::path& folder,LanguagePreference preference);
}
