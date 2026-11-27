// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/option_flags.h"
#include "game/observed.h"
#include "app/save_store.h"
#include <filesystem>
#include "machine/machine.h"
#include "app/tempo_audio.h"
#include "app/fm_audio.h"
#include "app/session_roms.h"
#include "game/automap.h"
#include "game/dungeon_tables.h"
#include "app/wide_view.h"
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
    unsigned playback_sample_cycles=Psg::sample_cycles;
};
using game::host_observed_instructions;
// Check observed entry opt-ins and reject exits that would hide an observation.
void guard_observed_instructions(const RoutineRegistry& registry);
// The game's whole registry (review R3 L1): the guard above, and every watched
// address begins an observed segment, since the host's hook runs only there.
void require_observed_entries(const RoutineRegistry& registry);

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
    // original_only: recording mode (the original game, no options, no host
    // menus).
    FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,
                game::OptionOverrides overrides={},bool original_only=false,
                std::shared_ptr<const Memory::Rom> sound_rom={});
    // One frame. Everything the frame hands over (picture, sound, the
    // widescreen capture, the save, the end of a menu request) is taken at
    // the frame end (Machine's frame observer); the machine itself can stop
    // up to one segment later. An error of the engine (code with no C++
    // body, a state a body does not handle) propagates: the game stops
    // (stopped()), and run and power_cycle refuse from then on.
    FrameOutput run(const FrameInput& next_input);
    std::optional<std::string> power_cycle();
    // An explicit flush (exit, power cycle) also retries a failed automap
    // write at once; the per-frame flush in run() follows its backoff. After
    // a stop it writes only the cartridge RAM of the last frame end not yet
    // written (none: nothing), never the state of the stopped frame.
    std::optional<std::string> flush();
    // The engine's error that stopped the game, if one did.
    const std::optional<std::string>& stopped() const { return stopped_; }
    Machine& machine() { return *machine_; }
    const SaveStore& store() const { return store_; }
    RoutineRegistry& routines() { return routines_; }
    const game::Options& effective_options() const { return effective_; }
    const game::Options& chosen_options() const { return chosen_; }
    const game::Options& configured_options() const { return store_.preferences(); }
    bool options_pending() const { return chosen_!=effective_; }
    // Recording mode (or the test-only driver without routines).
    bool options_restricted() const { return original_only_; }
    std::optional<std::string> choose_options(const game::Options& options);
    std::optional<std::string> choose_options(const game::Options& options,bool automap);
    // Also stores the profile's widescreen mode (like the
    // automap, never a routine option and never pending); on a failed write the
    // stored switch is kept.
    std::optional<std::string> choose_options(const game::Options& options,bool automap,WidescreenMode widescreen);
    // The stored switch, off in a restricted session (recording).
    WidescreenMode widescreen_option() const { return options_restricted() ? WidescreenMode::off : store_.widescreen_preference(); }
    // Host-owned, verified images. Prepare may throw BEFORE language.txt is
    // saved. Commit only queues the last successfully saved choice; the
    // native Options return installs it before the caller's next instruction.
    void configure_languages(const SessionRoms& session);
    bool fm_available() const { return bool(fm_audio_); }
    bool fm_selected() const { return fm_available() && prefer_fm_; }
    void choose_fm(bool selected) { prefer_fm_=!original_only_ && selected; }
    void prepare_language(LanguagePreference preference);
    void commit_language() noexcept;
    Language active_language() const { return language_; }
    bool language_pending() const { return pending_language_.has_value(); }
    std::uint64_t language_changes() const { return language_changes_; }
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
    // Test instrumentation (milestone 14): called at each frame's frame
    // observer, the first instruction boundary at or after the frame end,
    // after this frame's sound was taken. It must not change the machine.
    using FrameProbe=std::function<void(const Machine&)>;
    void set_frame_probe(FrameProbe probe) { frame_probe_=std::move(probe); }
    void close_menu() { if (menu_active_) menu_closing_=true; }
    bool menu_active() const { return menu_active_; }
    bool menu_closing() const { return menu_closing_; }
    void speed(unsigned value);
    // Widescreen (milestone 10), applied at once and never seen by the
    // machine or the routine options. Off: nothing is copied. Refused (stays
    // off) in recording mode.
    void set_widescreen(bool on);
    bool widescreen() const { return bool(wide_); }
    // The 344-wide picture for the frame just run, or nullptr when it is
    // not widened (pillarbox). Valid until the next run, power cycle or
    // set_widescreen; a host menu keeps its own copy, as with the picture.
    const WidePicture* wide_picture() const { return wide_ ? wide_->picture(machine_->picture()) : nullptr; }
    const WideView* wide_view() const { return wide_.get(); }
private:
    void activate(const game::Options& options);
    void track(std::uint16_t pc);
    void log(MapEvent event) { if(map_log_)map_log_->push_back(std::move(event)); }
    std::shared_ptr<const Memory::Rom> rom_;
    std::array<std::shared_ptr<const Memory::Rom>,2> language_roms_;
    std::array<std::optional<Machine::RomReplacement>,2> replacements_;
    RomVersion source_version_=RomVersion::us_rev_a;
    Language language_=Language::english;
    std::optional<Language> prepared_language_,pending_language_;
    std::uint64_t language_changes_=0;
    bool languages_configured_=false;
    SaveStore store_;
    std::unique_ptr<Machine> machine_;
    RoutineRegistry routines_;
    game::Options chosen_,effective_;
    bool original_only_=false,automap_=false;
    std::unique_ptr<game::DungeonTables> tables_;
    std::unique_ptr<game::ExplorationTracker> tracker_;
    std::optional<game::MapView> map_view_;
    FrameProbe frame_probe_;
    std::vector<MapEvent>* map_log_=nullptr;
    HostMenu menu_purpose_=HostMenu::options,requested_purpose_=HostMenu::options;
    std::optional<std::uint16_t> exploration_wait_,menu_request_;
    std::uint16_t exploration_sp_=0;
    bool menu_active_=false,menu_closing_=false,menu_cancel_seen_=false;
    Z80 menu_caller_{};
    std::uint16_t menu_return_=0;
    std::array<std::uint8_t,6> menu_saved_{};
    std::unique_ptr<TempoAudio> tempo_audio_;
    std::shared_ptr<const Memory::Rom> sound_rom_;
    std::unique_ptr<FmAudio> fm_audio_;
    bool prefer_fm_=false,tempo_consumed_tick_=false;
    std::optional<std::string> fm_error_;
    void reset_fm();
    unsigned speed_=1;
    bool audio_failed_=false;
    std::optional<std::uint8_t> sound_request_;
    std::unique_ptr<WideView> wide_;
    // The image the wide view reads (it follows rom_ at the next frame end).
    const Memory::Rom* wide_rom_=nullptr;
    // The last frame end's memory while its cartridge RAM is not yet written.
    std::optional<Memory> unwritten_;
    std::optional<std::string> stopped_;
};
// What a session that ended returns to the system (milestone 14): 1 when the
// engine stopped the game or the last save failed, 0 otherwise.
inline int session_status(bool save_failed,bool stopped) { return save_failed || stopped ? 1 : 0; }
}
