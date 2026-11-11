// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "game/routines.h"
#include "rom/sha1.h"
#include <stdexcept>

namespace psmodern::app {
AppArguments parse_app_arguments(int argc,const char* const* argv) {
    const auto usage="Usage: psmodern.exe <rom-path> [--record <new-file>] [--no-routines] "+game::OptionFlags::usage()+
        "\nProvide an uncompressed US Rev-A or Brazilian .sms file. Leaving the rate flags out plays the original game.";
    if (argc<2) throw std::runtime_error(usage);
    AppArguments result; result.rom=argv[1];
    game::OptionFlags flags;
    for (int i=2;i<argc;++i) {
        const std::string_view arg=argv[i];
        if (flags.take(argc,argv,i)) continue;
        if (arg=="--no-routines" && result.routines_enabled) result.routines_enabled=false;
        else if (arg=="--record" && !result.record_path && i+1<argc) result.record_path=argv[++i];
        else throw std::runtime_error(usage);
    }
    if (!result.routines_enabled) flags.refuse_with("--no-routines","the options are carried out by the rewritten routines");
    if (result.record_path) flags.refuse_with("--record","recordings always play the original game");
    result.options=flags.options;
    return result;
}
FrameDriver::FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,bool routines_enabled,game::Options options)
    :rom_(std::move(rom)),store_(std::move(store)),machine_(std::make_unique<Machine>(rom_,store_.load())) {
    routines_.enabled=routines_enabled;
    game::register_routines(routines_,sha1(*rom_),options);
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
