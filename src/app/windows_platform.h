// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/language.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;
namespace psmodern::app {
// The command line as UTF-8. On Windows it is read again as UTF-16 because
// main's argv uses the ANSI code page and loses other characters; argc and
// argv are ignored there on purpose.
std::vector<std::string> utf8_arguments(int argc,char** argv);
// psmodern.exe is a GUI program. Standard streams that already lead somewhere
// (a pipe or file from a test or a redirection) are kept; otherwise they are
// sent to the console of the program that started it, if there is one. After
// attaching to a console, Ctrl+C no longer stops the program.
void attach_parent_console();
// The Windows "Open" dialog for a .sms file; empty when cancelled. A desktop
// fullscreen owner leaves fullscreen while the dialog is shown.
std::optional<std::filesystem::path> pick_rom_file(SDL_Window* owner,Language language=Language::english);
// Creates the folder and shows it in the file manager; throws on failure.
void open_folder(const std::filesystem::path& folder);
}
