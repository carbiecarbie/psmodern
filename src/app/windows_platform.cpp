// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/windows_platform.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_syswm.h>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#endif

namespace psmodern::app {
#ifdef _WIN32
namespace {
std::string utf8(const wchar_t* text) {
    const int size=WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
    if (size<=0) throw std::runtime_error("Cannot convert the command line to UTF-8.");
    std::string result(static_cast<std::size_t>(size),'\0');
    if (WideCharToMultiByte(CP_UTF8,0,text,-1,result.data(),size,nullptr,nullptr)!=size)
        throw std::runtime_error("Cannot convert the command line to UTF-8.");
    result.pop_back();
    return result;
}
// The shell dialogs expect a single-threaded COM apartment on their thread.
struct Com {
    bool initialized=SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE));
    ~Com() { if (initialized) CoUninitialize(); }
};
}
std::vector<std::string> utf8_arguments(int,char**) {
    int count=0;
    const auto words=std::unique_ptr<wchar_t*,decltype(&LocalFree)>(CommandLineToArgvW(GetCommandLineW(),&count),LocalFree);
    if (!words) throw std::runtime_error("Cannot read the command line.");
    std::vector<std::string> result;
    for (int i=0;i<count;++i) result.push_back(utf8(words.get()[i]));
    return result;
}
void attach_parent_console() {
    auto leads_somewhere=[](DWORD id) {
        const HANDLE handle=GetStdHandle(id);
        return handle && handle!=INVALID_HANDLE_VALUE && GetFileType(handle)!=FILE_TYPE_UNKNOWN;
    };
    const bool out=leads_somewhere(STD_OUTPUT_HANDLE),error=leads_somewhere(STD_ERROR_HANDLE);
    if ((out && error) || !AttachConsole(ATTACH_PARENT_PROCESS)) return;
    // The game shares the terminal's console now: a Ctrl+C typed there later
    // must not kill it (or cut a --record short). Close the window to stop it.
    SetConsoleCtrlHandler(nullptr,TRUE);
    if (!out && std::freopen("CONOUT$","w",stdout)) std::cout.clear();
    if (!error && std::freopen("CONOUT$","w",stderr)) std::cerr.clear();
}
std::optional<std::filesystem::path> pick_rom_file(SDL_Window* owner) {
    HWND parent=nullptr;
    if (owner) {
        SDL_SysWMinfo info; SDL_VERSION(&info.version);
        if (SDL_GetWindowWMInfo(owner,&info) && info.subsystem==SDL_SYSWM_WINDOWS) parent=info.info.win.window;
    }
    const bool fullscreen=owner && (SDL_GetWindowFlags(owner)&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (fullscreen) SDL_SetWindowFullscreen(owner,0);
    std::vector<wchar_t> name(32768,L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof dialog; dialog.hwndOwner=parent;
    dialog.lpstrFilter=L"Master System ROM (*.sms)\0*.sms\0All files (*.*)\0*.*\0";
    dialog.nFilterIndex=1; dialog.lpstrFile=name.data(); dialog.nMaxFile=static_cast<DWORD>(name.size());
    dialog.lpstrTitle=L"Choose your Phantasy Star ROM";
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;
    bool chosen=false; DWORD failure=0;
    { Com com; chosen=GetOpenFileNameW(&dialog); if (!chosen) failure=CommDlgExtendedError(); }
    if (fullscreen && SDL_SetWindowFullscreen(owner,SDL_WINDOW_FULLSCREEN_DESKTOP)!=0)
        std::cerr<<"Cannot restore fullscreen: "<<SDL_GetError()<<'\n';
    if (failure) throw std::runtime_error("Cannot show the file dialog (error "+std::to_string(failure)+").");
    if (!chosen) return {};
    return std::filesystem::path(name.data());
}
void open_folder(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    Com com;
    const auto result=reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
    if (result<=32) throw std::runtime_error("Cannot open the folder (error "+std::to_string(result)+").");
}
#else
std::vector<std::string> utf8_arguments(int argc,char** argv) { return {argv,argv+argc}; }
void attach_parent_console() {}
std::optional<std::filesystem::path> pick_rom_file(SDL_Window*) {
    throw std::runtime_error("Choosing a ROM needs Windows; run psmodern <rom-path> instead.");
}
void open_folder(const std::filesystem::path&) { throw std::runtime_error("Opening a folder needs Windows."); }
#endif
}
