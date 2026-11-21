// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <iostream>
#include "app/remove_tree.h"

namespace psmodern::test {
// Host locks can outlive the test body briefly. Cleanup must not turn passed
// assertions into a failure, but a persistent leftover must remain visible.
inline void remove_temporary_folder(const std::filesystem::path& path) {
    if (const auto error=app::remove_tree(path))
        std::cerr<<"Warning: temporary test folder cleanup failed: "<<path<<": "<<error.message()<<'\n';
}
}
