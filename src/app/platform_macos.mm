// SPDX-License-Identifier: GPL-3.0-or-later
// macOS host: saves under ~/Library/Application Support/PSModern, the AppKit
// Open panel for a .sms file and Finder for the save folder.
#include "app/platform.h"
#include "app/localization.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace psmodern::app {
// The tests isolate the save folder by setting %APPDATA%, as on Windows; an
// absolute APPDATA therefore wins here too, so a test never reaches the
// player's real saves. Players do not set it on a Mac.
std::filesystem::path production_save_folder() {
    if (const char* appdata=std::getenv("APPDATA"); appdata && *appdata) {
        const std::filesystem::path folder(appdata);
        if (!folder.is_absolute()) throw std::runtime_error("%APPDATA% must be an absolute folder.");
        return folder/"PSModern";
    }
    @autoreleasepool {
        NSArray<NSURL*>* urls=[[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask];
        if (urls.count==0 || !urls.firstObject.fileSystemRepresentation)
            throw std::runtime_error("Cannot resolve Application Support for saves.");
        std::filesystem::path folder(urls.firstObject.fileSystemRepresentation);
        if (!folder.is_absolute()) throw std::runtime_error("Application Support must be an absolute folder.");
        return folder/"PSModern";
    }
}
// argv is already UTF-8 on macOS. Finder on old systems adds a -psn_ process
// serial number when it starts an app bundle; it is not a game argument.
std::vector<std::string> utf8_arguments(int argc,char** argv) {
    std::vector<std::string> result;
    for (int i=0;i<argc;++i) {
        const std::string word(argv[i]);
        if (i>0 && word.rfind("-psn_",0)==0) continue;
        result.push_back(word);
    }
    return result;
}
// Started from a terminal, or redirected by a test, the streams already lead
// somewhere and are kept. Started from Finder they lead to /dev/null; the
// diagnostics then go to ~/Library/Logs/PSModern.log (replaced each launch).
void attach_parent_console() {
    auto discarded=[](int fd) {
        struct stat stream{},null{};
        return fstat(fd,&stream)!=0 || (stat("/dev/null",&null)==0 && stream.st_dev==null.st_dev && stream.st_ino==null.st_ino);
    };
    if (!discarded(STDERR_FILENO)) return;
    const char* home=getenv("HOME");
    if (!home || !*home) return;
    const auto folder=std::filesystem::path(home)/"Library"/"Logs";
    std::error_code error;
    std::filesystem::create_directories(folder,error);
    if (!std::freopen((folder/"PSModern.log").c_str(),"w",stderr)) return;
    std::setvbuf(stderr,nullptr,_IONBF,0);
    std::cerr.clear();
    if (discarded(STDOUT_FILENO)) { dup2(STDERR_FILENO,STDOUT_FILENO); std::cout.clear(); }
}
std::optional<std::filesystem::path> pick_rom_file(SDL_Window* owner,Language language) {
    const bool fullscreen=owner && (SDL_GetWindowFlags(owner)&SDL_WINDOW_FULLSCREEN_DESKTOP)==SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (fullscreen) SDL_SetWindowFullscreen(owner,0);
    std::optional<std::filesystem::path> chosen;
    @autoreleasepool {
        NSOpenPanel* panel=[NSOpenPanel openPanel];
        const auto title=ui_text(UiText::rom_dialog_title,language);
        NSString* text=[NSString stringWithUTF8String:std::string(title).c_str()];
        panel.title=text ? text : @"";
        panel.message=panel.title;
        panel.canChooseFiles=YES;
        panel.canChooseDirectories=NO;
        panel.allowsMultipleSelection=NO;
        panel.resolvesAliases=YES;
        // .sms has no system type; an unregistered extension still filters.
        if (UTType* sms=[UTType typeWithFilenameExtension:@"sms"]) panel.allowedContentTypes=@[sms];
        if ([panel runModal]==NSModalResponseOK && panel.URL.fileSystemRepresentation)
            chosen=std::filesystem::path(panel.URL.fileSystemRepresentation);
    }
    if (owner) SDL_RaiseWindow(owner);
    if (fullscreen && SDL_SetWindowFullscreen(owner,SDL_WINDOW_FULLSCREEN_DESKTOP)!=0)
        std::cerr<<"Cannot restore fullscreen: "<<SDL_GetError()<<'\n';
    return chosen;
}
void open_folder(const std::filesystem::path& folder) {
    std::filesystem::create_directories(folder);
    @autoreleasepool {
        NSURL* url=[NSURL fileURLWithFileSystemRepresentation:folder.c_str() isDirectory:YES relativeToURL:nil];
        if (!url || ![[NSWorkspace sharedWorkspace] openURL:url])
            throw std::runtime_error("Cannot open the folder in Finder.");
    }
}
}
