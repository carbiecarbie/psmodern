// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <thread>
#include <vector>

namespace psmodern::test {
namespace detail {
// One pass over a tree, children first. An entry that vanishes between the
// listing and its removal counts as removed. std::filesystem::remove_all is
// not used: on Windows, libstdc++ retries such an entry forever, because its
// fs::remove reports "not found" as false without an error. Real-time file
// scanners create and delete short-lived "<NAME>.tmp" files next to files
// being deleted, so the listing can contain one.
inline void remove_tree_once(const std::filesystem::path& path,std::error_code& first_error) {
    std::error_code error;
    const auto status=std::filesystem::symlink_status(path,error);
    if (status.type()==std::filesystem::file_type::not_found) return;
    if (error) { if (!first_error) first_error=error; return; }
    if (status.type()==std::filesystem::file_type::directory) {
        std::vector<std::filesystem::path> children;
        std::filesystem::directory_iterator it(path,error);
        for (const std::filesystem::directory_iterator end;!error && it!=end;it.increment(error))
            children.push_back(it->path());
        if (error && error!=std::errc::no_such_file_or_directory && !first_error) first_error=error;
        for (const auto& child:children) remove_tree_once(child,first_error);
    }
    error.clear();
    std::filesystem::remove(path,error);
    if (error && !first_error) first_error=error;
}
}

// Host locks can outlive the test body briefly. Cleanup must not turn passed
// assertions into a failure, but a persistent leftover must remain visible.
inline void remove_temporary_folder(const std::filesystem::path& path) {
    std::error_code error;
    for (unsigned attempt=0;attempt<6;++attempt) {
        error.clear();
        detail::remove_tree_once(path,error);
        std::error_code status_error;
        const auto left=std::filesystem::symlink_status(path,status_error);
        if (!error && left.type()==std::filesystem::file_type::not_found) return;
        if (!error && status_error) error=status_error;
        if (!error) error=std::make_error_code(std::errc::directory_not_empty);
        if (attempt<5) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    std::cerr<<"Warning: temporary test folder cleanup failed: "<<path<<": "<<error.message()<<'\n';
}
}
