// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

namespace psmodern::test {
// Host locks can outlive the test body briefly. Cleanup must not turn passed
// assertions into a failure, but a persistent leftover must remain visible.
inline void remove_temporary_folder(const std::filesystem::path& path) {
    std::error_code error;
    for (unsigned attempt=0;attempt<6;++attempt) {
        std::filesystem::remove_all(path,error);
        if (!error) return;
        if (attempt<5) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    std::cerr<<"Warning: temporary test folder cleanup failed: "<<path<<": "<<error.message()<<'\n';
}
}
