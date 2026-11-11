// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/options_menu.h"
#include "app/rom_list.h"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace psmodern::app {
enum class StartAction { none,play_us,play_br,choose,open_saves,quit };
// The start screen shown when psmodern.exe runs without arguments: one entry
// per remembered ROM, then CHOOSE ANOTHER ROM, OPEN SAVES FOLDER and QUIT.
// Input follows OptionsMenu: nothing is accepted until a neutral focused
// snapshot, losing focus disarms, held directions repeat, the cursor wraps.
class StartScreen {
public:
    // The cursor starts on selected when it is an entry, else on the first.
    explicit StartScreen(const RomList& list,StartAction selected=StartAction::none);
    const std::vector<StartAction>& entries() const { return entries_; }
    unsigned selected() const { return selected_; }
    // accept chooses the selected entry; cancel (Esc) quits.
    StartAction update(const MenuInput& input,double now);
    // Black picture with the original window style, centred on 256x192.
    void render(std::span<std::uint32_t> pixels,const RomFont& font) const;
    static const char* label(StartAction action);
    static std::string footer();
private:
    std::vector<StartAction> entries_;
    MenuInput previous_;
    unsigned selected_=0;
    bool armed_=false;
    double next_repeat_=0;
};
// What the launcher needs from the system, so that its decisions are tested
// without dialogs or files.
class LauncherPlatform {
public:
    virtual ~LauncherPlatform()=default;
    // The file the player picked, or nothing when cancelled.
    virtual std::optional<std::filesystem::path> pick_rom()=0;
    virtual void show_message(const std::string& text)=0;
    // true: choose again; false: the give_up button (Quit or Back).
    virtual bool ask_retry(const std::string& text,const char* give_up)=0;
    virtual RomListLoad load_list()=0;
    virtual void save_list(const RomList& list)=0;
    // Throws std::runtime_error with the reason for an unsupported file.
    virtual RomVersion identify(const std::filesystem::path& path)=0;
};
// Loads the remembered ROMs, forgets those that are missing or changed and asks
// for a ROM while none is left. Empty when the player cancels; nothing is
// written then. A ROM that identify reports as RomUnreadable is neither
// forgotten nor offered this session: it stays out of the returned list, goes
// into *unreadable if given, and is written back with every later save. One
// message names the forgotten and the unreadable ROMs.
std::optional<RomList> prepare_launcher(LauncherPlatform& platform,RomList* unreadable=nullptr);
// Asks for a ROM until a supported one is picked or the player gives up. A
// pick fills its version's slot and is saved. first_run names the give-up
// button Quit; otherwise Back (to the start screen). Returns the version of
// the ROM added, or nothing. Slots of unreadable (see prepare_launcher) that
// list does not fill are saved too.
std::optional<RomVersion> choose_rom(LauncherPlatform& platform,RomList& list,bool first_run,const RomList& unreadable={});
// The start screen entry that plays a version.
inline StartAction play_action(RomVersion version) { return version==RomVersion::us_rev_a ? StartAction::play_us : StartAction::play_br; }
// The ROM whose font draws the start screen: US preferred.
const std::filesystem::path& font_rom(const RomList& list);
}
