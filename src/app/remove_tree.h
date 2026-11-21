// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <chrono>
#include <filesystem>
#include <system_error>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace psmodern::app {
namespace detail {
#ifdef _WIN32
// libstdc++ on Windows has no lstat: symlink_status reports a junction or a
// directory symlink as a plain directory (checked with GCC 16.2), so walking
// into it would delete the contents of its target. A reparse point is removed
// as a link instead, never followed. An entry whose attributes cannot be read
// (other than one already gone) is skipped and reported, not walked. Returns
// false only for an ordinary entry or a missing one. A process that swaps a
// checked directory for a link before the walk is not defended against: only
// this process writes to the stage.
inline bool remove_reparse_point(const std::filesystem::path& path,std::error_code& first_error) {
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES) {
        const DWORD code=GetLastError();
        if (code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND) return false;
        if (!first_error) first_error=std::error_code(static_cast<int>(code),std::system_category());
        return true;
    }
    if (!(attributes&FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    const BOOL removed=(attributes&FILE_ATTRIBUTE_DIRECTORY) ? RemoveDirectoryW(path.c_str()) : DeleteFileW(path.c_str());
    const DWORD code=removed ? ERROR_SUCCESS : GetLastError();
    if (!removed && code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND && !first_error)
        first_error=std::error_code(static_cast<int>(code),std::system_category());
    return true;
}
#endif

// One pass over a tree, children first. An entry that vanishes between the
// listing and its removal counts as removed. Links are removed, not followed.
// std::filesystem::remove_all is not used: on Windows, libstdc++ retries such
// an entry forever, because its fs::remove reports "not found" as false
// without an error, and it follows junctions. Real-time file scanners create
// and delete short-lived "<NAME>.tmp" files next to files being deleted, so
// the listing can contain one.
inline void remove_tree_once(const std::filesystem::path& path,std::error_code& first_error) {
#ifdef _WIN32
    if (remove_reparse_point(path,first_error)) return;
#endif
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

// Removes a directory tree, tolerating entries that disappear on their own.
// A host handle (a scanner, or a write that just failed) can briefly keep an
// entry alive, so up to six passes run 40 ms apart. Returns no error once the
// path is gone; otherwise the last error, or directory_not_empty when a pass
// reported none but something remained. Never throws for filesystem errors.
inline std::error_code remove_tree(const std::filesystem::path& path) {
    std::error_code error;
    for (unsigned attempt=0;attempt<6;++attempt) {
        error.clear();
        detail::remove_tree_once(path,error);
        std::error_code status_error;
        const auto left=std::filesystem::symlink_status(path,status_error);
        if (!error && left.type()==std::filesystem::file_type::not_found) return {};
        if (!error && status_error) error=status_error;
        if (!error) error=std::make_error_code(std::errc::directory_not_empty);
        if (attempt<5) std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return error;
}
}
