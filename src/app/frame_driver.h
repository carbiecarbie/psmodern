// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/option_flags.h"
#include "app/save_store.h"
#include <filesystem>
#include "machine/machine.h"
#include "app/tempo_audio.h"
#include "game/automap.h"
#include "game/dungeon_tables.h"
#include <array>
#include <vector>

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
// The native instructions the host watches (bank:address): the Options
// boundary, the frame-wait watch, the field waits and the menu cancel, the
// tempo-audio points and the automap's observation points. The core runs a
// segment's entry and its final instruction without the hook, so none of
// these may be a registered entry or a declared exit address of any segment.
inline constexpr std::array<RoutineEntry,13> host_observed_instructions{{
    {0,0x00AF},{0,0x0055},{0,0x0085},{0,0x0B14},{0,0x0F48},{0,0x2D93},{0,0x02ED},{0,0x02F5},
    {12,0x8043},{12,0x82EF},{0,0x1D2D},{0,0x06B4},{0,0x05F4}}};   // 00:00AF also ends a dungeon walk
// Throws std::logic_error naming the first watched instruction that is a
// registered entry or a declared exit address (exit addresses are compared
// without their bank, which can only refuse more).
void guard_observed_instructions(const RoutineRegistry& registry);

// What the host's hold of the native field menu is for.
enum class HostMenu { options, map };
// One automap event of the hook, in order, for tests that compare runs (for
// example routines on against routines off).
struct MapEvent {
    enum class Kind { sample, stage, replace, reset, leave } kind;   // leave: logged when it forgets a previous sample
    std::uint64_t cycles=0;                       // machine cycles at the observed instruction (0 for a power cycle)
    std::uint8_t floor=0, cell=0, facing=0;       // sample
    std::string digest{};                         // stage and replace (game block SHA-256)
    bool found=false;                             // replace: a record existed for the digest
    bool operator==(const MapEvent&) const = default;
};

class FrameDriver {
public:
    FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,bool routines_enabled=true,
                game::OptionOverrides overrides={},bool original_only=false);
    FrameOutput run(const FrameInput& next_input);
    std::optional<std::string> power_cycle();
    // An explicit flush (exit, power cycle) also retries a failed automap
    // write at once; the per-frame flush in run() follows its backoff.
    std::optional<std::string> flush() { return store_.flush(machine_->memory(),true); }
    Machine& machine() { return *machine_; }
    const SaveStore& store() const { return store_; }
    RoutineRegistry& routines() { return routines_; }
    const game::Options& effective_options() const { return effective_; }
    const game::Options& chosen_options() const { return chosen_; }
    const game::Options& configured_options() const { return store_.preferences(); }
    bool options_pending() const { return chosen_!=effective_; }
    bool options_restricted() const { return original_only_ || !routines_.enabled; }
    std::optional<std::string> choose_options(const game::Options& options);
    std::optional<std::string> choose_options(const game::Options& options,bool automap);
    // The host may request the native field menu only during its proven wait.
    // The map is admitted only at the dungeon wait (00:0F48, mode 0Bh), with
    // the Automap option on and a tracker running.
    bool request_menu(HostMenu purpose=HostMenu::options);
    HostMenu menu_purpose() const { return menu_purpose_; }
    // Automap. Tracking runs for an identified image with routines on and not
    // recording, even with the option off; the option only gates the screen.
    bool automap_tracking() const { return bool(tracker_); }
    bool automap_enabled() const { return automap_ && tracker_; }
    bool automap_option() const { return automap_; }
    const game::Exploration* exploration() const { return tracker_ ? &tracker_->exploration() : nullptr; }
    const game::DungeonTables* dungeon_tables() const { return tables_.get(); }
    // The view built once when a map hold began, until the next one.
    const std::optional<game::MapView>& map_view() const { return map_view_; }
    void log_map_events(std::vector<MapEvent>* log) { map_log_=log; }
    void close_menu() { if (menu_active_) menu_closing_=true; }
    bool menu_active() const { return menu_active_; }
    bool menu_closing() const { return menu_closing_; }
    void speed(unsigned value);
private:
    void activate(const game::Options& options);
    void track(std::uint16_t pc);
    void log(MapEvent event) { if(map_log_)map_log_->push_back(std::move(event)); }
    std::shared_ptr<const Memory::Rom> rom_;
    SaveStore store_;
    std::unique_ptr<Machine> machine_;
    RoutineRegistry routines_;
    game::Options chosen_,effective_;
    bool original_only_=false,automap_=false;
    std::unique_ptr<game::DungeonTables> tables_;
    std::unique_ptr<game::ExplorationTracker> tracker_;
    std::optional<game::MapView> map_view_;
    std::vector<MapEvent>* map_log_=nullptr;
    HostMenu menu_purpose_=HostMenu::options,requested_purpose_=HostMenu::options;
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
