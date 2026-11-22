// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sound_preference.h"
#include "app/save_store.h"
#include <stdexcept>

namespace psmodern::app {
SoundPreference parse_sound_preference(std::string_view text) {
    if (text.size()>sound_file_limit) throw std::runtime_error("Sound preference file is too large.");
    auto line=[&] {
        const auto end=text.find('\n');
        if (end==std::string_view::npos) throw std::runtime_error("Sound preference file is truncated.");
        auto value=text.substr(0,end);
        text.remove_prefix(end+1);
        if (!value.empty() && value.back()=='\r') value.remove_suffix(1);
        return value;
    };
    if (line()!="psmodern-sound 1") throw std::runtime_error("Unsupported sound preference format or version.");
    const auto value=line();
    if (!text.empty()) throw std::runtime_error("Unexpected sound preference file content.");
    if (value=="psg") return SoundPreference::psg;
    if (value=="fm") return SoundPreference::fm;
    throw std::runtime_error("Unknown sound preference.");
}
std::string format_sound_preference(SoundPreference preference) {
    switch (preference) {
    case SoundPreference::psg: return "psmodern-sound 1\npsg\n";
    case SoundPreference::fm: return "psmodern-sound 1\nfm\n";
    }
    throw std::invalid_argument("Unknown sound preference.");
}
SoundPreferenceLoad load_sound_preference(const std::filesystem::path& folder) {
    const auto path=folder/sound_file_name;
    std::error_code error;
    const auto status=std::filesystem::status(path,error);
    if (status.type()==std::filesystem::file_type::not_found) return {};
    try {
        if (error) throw std::runtime_error(error.message());
        return {parse_sound_preference(read_text_file(path,sound_file_limit,"sound preference file")),{}};
    } catch (const std::exception& e) {
        const auto utf8=path.u8string();
        const std::string name(reinterpret_cast<const char*>(utf8.data()),utf8.size());
        return {SoundPreference::psg,"Using PSG sound; sound preference file preserved: "+name+": "+e.what()};
    }
}
void save_sound_preference(const std::filesystem::path& folder,SoundPreference preference) {
    atomic_text(folder/sound_file_name,format_sound_preference(preference));
}
}
