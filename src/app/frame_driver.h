// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/save_store.h"
#include "machine/machine.h"

namespace psmodern::app {
struct FrameOutput {
    std::vector<PsgWrite> writes;
    PsgSamples samples;
    std::optional<std::string> save_error;
};
class FrameDriver {
public:
    FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,bool routines_enabled=true);
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
