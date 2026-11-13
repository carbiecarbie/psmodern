// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/language.h"
#include "app/rom_list.h"
#include "hw/memory.h"
#include <array>
#include <memory>
#include <vector>

namespace psmodern::app {
// The source version is fixed even when a different cartridge supplies the text.
// Images are completely loaded and identified before the session starts.
struct SessionRoms {
    RomVersion source_version=RomVersion::us_rev_a;
    std::array<std::shared_ptr<const Memory::Rom>,2> images;
    LanguagePreference preference=LanguagePreference::native;
    Language active=Language::english;
    bool restricted=false,language_unavailable=false;
    std::vector<std::string> warnings;
    const std::shared_ptr<const Memory::Rom>& image(Language language) const {
        return images[language==Language::english ? 0 : 1];
    }
    bool available(Language language) const { return bool(image(language)); }
    bool available(LanguagePreference value) const { return available(resolve_language(value,source_version)); }
};
// The primary path is mandatory; a failed optional companion is a warning.
// Restricted sessions neither load the companion nor apply the preference.
SessionRoms load_session_roms(const std::filesystem::path& primary,const RomList& remembered,
                             LanguagePreference preference,bool restricted=false);
}
