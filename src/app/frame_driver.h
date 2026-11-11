// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/option_flags.h"
#include "app/save_store.h"
#include <filesystem>
#include "machine/machine.h"
#include "app/tempo_audio.h"

namespace psmodern::app {
// What psmodern.exe was asked to do: play (a ROM path was given), show the
// start screen (no arguments) or print the version (--version alone).
enum class AppMode { play, launcher, version };
// The command line of psmodern.exe, already validated. rom and the other
// fields are meaningful only in play mode.
struct AppArguments {
    AppMode mode=AppMode::play;
    std::filesystem::path rom;
    std::optional<std::filesystem::path> record_path;
    bool routines_enabled=true;
    game::Options options;
    game::OptionOverrides overrides;
};
// A ROM file read completely and identified.
struct LoadedRom {
    RomVersion version;
    std::shared_ptr<Memory::Rom> rom;
};
// Identifies the file, reads it and checks that the bytes read are still the
// identified dump; throws std::runtime_error otherwise.
LoadedRom load_rom(const std::filesystem::path& path);
// The usage message that refusals carry.
std::string app_usage();
// argv[0] is the program name; paths are read as UTF-8. Throws std::runtime_error with a usage or
// refusal message; nothing is opened or run before this returns.
AppArguments parse_app_arguments(int argc,const char* const* argv);

struct FrameOutput {
    std::vector<PsgWrite> writes;
    PsgSamples samples;
    std::optional<std::string> save_error;
    std::optional<PsgSamples> playback;
    std::optional<std::string> audio_error;
};
class FrameDriver {
public:
    FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,bool routines_enabled=true,
                game::OptionOverrides overrides={},bool original_only=false);
    FrameOutput run(const FrameInput& next_input);
    std::optional<std::string> power_cycle();
    std::optional<std::string> flush() { return store_.flush(machine_->memory()); }
    Machine& machine() { return *machine_; }
    const SaveStore& store() const { return store_; }
    RoutineRegistry& routines() { return routines_; }
    const game::Options& effective_options() const { return effective_; }
    const game::Options& chosen_options() const { return chosen_; }
    const game::Options& configured_options() const { return store_.preferences(); }
    bool options_pending() const { return chosen_!=effective_; }
    bool options_restricted() const { return original_only_ || !routines_.enabled; }
    std::optional<std::string> choose_options(const game::Options& options);
    // The host may request the native field menu only during its proven wait.
    bool request_menu();
    void close_menu() { if (menu_active_) menu_closing_=true; }
    bool menu_active() const { return menu_active_; }
    bool menu_closing() const { return menu_closing_; }
    void speed(unsigned value);
private:
    void activate(const game::Options& options);
    std::shared_ptr<const Memory::Rom> rom_;
    SaveStore store_;
    std::unique_ptr<Machine> machine_;
    RoutineRegistry routines_;
    game::Options chosen_,effective_;
    bool original_only_=false;
    std::optional<std::uint16_t> exploration_wait_,menu_request_;
    std::uint16_t exploration_sp_=0;
    bool menu_active_=false,menu_closing_=false,menu_cancel_seen_=false;
    Z80 menu_caller_{};
    std::uint16_t menu_return_=0;
    std::array<std::uint8_t,6> menu_saved_{};
    std::unique_ptr<TempoAudio> tempo_audio_;
    unsigned speed_=1;
    bool audio_failed_=false;
    std::optional<std::uint8_t> sound_request_;
};
}
