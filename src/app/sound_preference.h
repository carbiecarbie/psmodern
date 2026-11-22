// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace psmodern::app {
enum class SoundPreference { psg, fm };
inline constexpr const char* sound_file_name="sound.txt";
inline constexpr std::size_t sound_file_limit=128;

constexpr SoundPreference effective_sound_preference(SoundPreference preference,bool fm_available) {
    return fm_available ? preference : SoundPreference::psg;
}

// A per-installation preference. Format: "psmodern-sound 1\n", then "psg\n" or "fm\n".
SoundPreference parse_sound_preference(std::string_view text);
std::string format_sound_preference(SoundPreference preference);
struct SoundPreferenceLoad {
    SoundPreference preference=SoundPreference::psg;
    std::optional<std::string> warning;
};
// Missing means PSG. An unusable file is reported and preserved until a new selection.
SoundPreferenceLoad load_sound_preference(const std::filesystem::path& folder);
// Write the preference atomically. Commit the live choice only after this succeeds.
void save_sound_preference(const std::filesystem::path& folder,SoundPreference preference);
}
