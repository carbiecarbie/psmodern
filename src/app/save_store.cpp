// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/save_store.h"
#include <fstream>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace psmodern::app {
const char* save_name(RomVersion version) {
    return version==RomVersion::us_rev_a ? "phantasy-star-us-rev-a.sav" : "phantasy-star-brazil.sav";
}
std::filesystem::path production_save_folder() {
#ifdef _WIN32
    const auto length=GetEnvironmentVariableW(L"APPDATA",nullptr,0);
    if (length==0) throw std::runtime_error("Cannot resolve %APPDATA% for saves.");
    std::wstring value(length,L'\0');
    const auto written=GetEnvironmentVariableW(L"APPDATA",value.data(),length);
    if (!written || written>=length) throw std::runtime_error("Cannot resolve %APPDATA% for saves.");
    value.resize(written);
    std::filesystem::path folder(value);
    if (!folder.is_absolute()) throw std::runtime_error("%APPDATA% must be an absolute folder.");
    return folder/"PSModern";
#else
    throw std::runtime_error("Production save location currently requires Windows %APPDATA%.");
#endif
}
SaveStore::SaveStore(std::filesystem::path folder,RomVersion version,Writer writer)
    :path_(std::move(folder)/save_name(version)),writer_(std::move(writer)) {}
Memory::CartridgeRam SaveStore::load() const {
    Memory::CartridgeRam image{};
    const auto status=std::filesystem::status(path_);
    if (status.type()==std::filesystem::file_type::not_found) return image;
    if (!std::filesystem::is_regular_file(status) || std::filesystem::file_size(path_)!=image.size())
        throw std::runtime_error("Refusing save (expected a regular 8192-byte file): "+path_.string());
    std::ifstream file(path_,std::ios::binary);
    if (!file.read(reinterpret_cast<char*>(image.data()),image.size()) || file.peek()!=std::char_traits<char>::eof())
        throw std::runtime_error("Cannot read complete save; it will not be overwritten: "+path_.string());
    return image;
}
void atomic_save(const std::filesystem::path& path,const Memory::CartridgeRam& image) {
    std::filesystem::create_directories(path.parent_path());
    // Refuse a newly malformed destination too (e.g. external replacement).
    if (std::filesystem::exists(path) && (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path)!=image.size()))
        throw std::runtime_error("Refusing to replace malformed save: "+path.string());
#ifdef _WIN32
    std::filesystem::path temp;
    HANDLE handle=INVALID_HANDLE_VALUE;
    for (unsigned attempt=0;attempt<100 && handle==INVALID_HANDLE_VALUE;++attempt) {
        static unsigned sequence=0;
        temp=path; temp+=L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++sequence);
        handle=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (handle==INVALID_HANDLE_VALUE && GetLastError()!=ERROR_FILE_EXISTS)
            throw std::system_error(GetLastError(),std::system_category(),"Creating temporary save");
    }
    if (handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot allocate temporary save.");
    try {
        DWORD written=0;
        if (!WriteFile(handle,image.data(),static_cast<DWORD>(image.size()),&written,nullptr) || written!=image.size())
            throw std::system_error(GetLastError(),std::system_category(),"Writing temporary save");
        if (!FlushFileBuffers(handle)) throw std::system_error(GetLastError(),std::system_category(),"Flushing temporary save");
        const auto closed=CloseHandle(handle); handle=INVALID_HANDLE_VALUE;
        if (!closed) throw std::system_error(GetLastError(),std::system_category(),"Closing temporary save");
        if (!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(),std::system_category(),"Replacing save");
    } catch (...) {
        if (handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);
        DeleteFileW(temp.c_str()); throw;
    }
#else
    auto temp=path.string()+".tmp-XXXXXX";
    int fd=mkstemp(temp.data());
    if (fd<0) throw std::system_error(errno,std::generic_category(),"Creating temporary save");
    try {
        std::size_t offset=0;
        while (offset<image.size()) {
            const auto n=::write(fd,image.data()+offset,image.size()-offset);
            if (n<=0) throw std::system_error(errno,std::generic_category(),"Writing temporary save");
            offset+=n;
        }
        if (fsync(fd)) throw std::system_error(errno,std::generic_category(),"Flushing temporary save");
        const auto closed=close(fd); fd=-1;
        if (closed) throw std::system_error(errno,std::generic_category(),"Closing temporary save");
        std::filesystem::rename(temp,path);
    } catch (...) { if (fd>=0) close(fd); unlink(temp.c_str()); throw; }
#endif
}
std::optional<std::string> SaveStore::flush(Memory& memory) {
    if (!retry_ && !memory.cartridge_ram_dirty()) return {};
    retry_=true;
    try {
        writer_(path_,memory.cartridge_ram());
        memory.clear_cartridge_ram_dirty(); retry_=false; ++writes_; return {};
    } catch (const std::exception& e) {
        return "Save write failed; retrying next frame: "+path_.string()+": "+e.what();
    }
}
}
