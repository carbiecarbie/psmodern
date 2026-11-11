// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "game/option_flags.h"
#include "app/save_store.h"
#include <filesystem>
#include "machine/machine.h"

namespace psmodern::app {
// The command line of psmodern.exe, already validated.
struct AppArguments {
    std::filesystem::path rom;
    std::optional<std::filesystem::path> record_path;
    bool routines_enabled=true;
    game::Options options;
};
// argv[0] is the program name. Throws std::runtime_error with a usage or
// refusal message; nothing is opened or run before this returns.
AppArguments parse_app_arguments(int argc,const char* const* argv);

struct FrameOutput {
    std::vector<PsgWrite> writes;
    PsgSamples samples;
    std::optional<std::string> save_error;
};
class FrameDriver {
public:
    FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,bool routines_enabled=true,
                game::Options options={});
    FrameOutput run(const FrameInput& next_input);
    std::optional<std::string> power_cycle();
    std::optional<std::string> flush() { return store_.flush(machine_->memory()); }
    Machine& machine() { return *machine_; }
    const SaveStore& store() const { return store_; }
    RoutineRegistry& routines() { return routines_; }
private:
    std::shared_ptr<const Memory::Rom> rom_;
    SaveStore store_;
    std::unique_ptr<Machine> machine_;
    RoutineRegistry routines_;
};
}
