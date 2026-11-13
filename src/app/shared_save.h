// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/save_store.h"
#include <array>

namespace psmodern::app {
enum class LegacySaveState { absent, available, unavailable };
struct LegacySave {
    LegacySaveState state=LegacySaveState::absent;
    std::string error;
};
using LegacySaves=std::array<LegacySave,2>; // US, BR; independent of available ROMs
using ChooseLegacySave=std::function<std::optional<RomVersion>(const LegacySaves&)>;
enum class ImportStep { stage_created, file_copied, before_publish, after_publish };
using ImportObserver=std::function<void(ImportStep)>;
// Opens the shared store locked. A first launch preserves/imports one complete
// legacy set through atomic directory publication. Cancel returns no store.
// An observer lets crash/failure tests stop at the publication boundaries.
std::optional<SaveStore> open_shared_save(const std::filesystem::path& folder,
    const ChooseLegacySave& choose={},const ImportObserver& observer={});
}
