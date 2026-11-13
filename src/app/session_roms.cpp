// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/session_roms.h"
#include "app/frame_driver.h"
#include <stdexcept>

namespace psmodern::app {
SessionRoms load_session_roms(const std::filesystem::path& primary,const RomList& remembered,
                             LanguagePreference preference,bool restricted) {
    auto loaded=load_rom(primary);
    SessionRoms result;
    result.source_version=loaded.version;result.preference=preference;result.restricted=restricted;
    result.active=native_language(loaded.version);
    result.images[loaded.version==RomVersion::us_rev_a ? 0 : 1]=std::move(loaded.rom);
    if(restricted)return result;
    const auto other=loaded.version==RomVersion::us_rev_a ? RomVersion::brazilian : RomVersion::us_rev_a;
    if(const auto& path=remembered.get(other);path) {
        try {
            auto companion=load_rom(*path);
            if(companion.version!=other)throw std::runtime_error("The remembered companion has the wrong ROM version.");
            result.images[other==RomVersion::us_rev_a ? 0 : 1]=std::move(companion.rom);
        } catch(const std::exception& e) {
            result.warnings.push_back(std::string("Companion ROM unavailable: ")+e.what());
        }
    }
    const auto wanted=resolve_language(preference,result.source_version);
    if(result.available(wanted))result.active=wanted;
    else result.language_unavailable=true;
    return result;
}
}
