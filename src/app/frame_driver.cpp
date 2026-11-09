// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "game/routines.h"
#include "rom/sha1.h"

namespace psmodern::app {
FrameDriver::FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,bool routines_enabled)
    :rom_(std::move(rom)),store_(std::move(store)),machine_(std::make_unique<Machine>(rom_,store_.load())) {
    routines_.enabled=routines_enabled;
    game::register_routines(routines_,sha1(*rom_));
}
FrameOutput FrameDriver::run(const FrameInput& input) {
    machine_->run_frame(input,routines_);
    auto writes=machine_->take_psg_writes();
    auto samples=machine_->take_samples();
    return {std::move(writes),std::move(samples),flush()};
}
std::optional<std::string> FrameDriver::power_cycle() {
    auto error=flush();
    machine_=std::make_unique<Machine>(rom_,machine_->memory().cartridge_ram());
    return error;
}
}
