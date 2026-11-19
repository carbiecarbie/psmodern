// SPDX-License-Identifier: GPL-3.0-or-later
#include "oracle/native_machine.h"
#include "app/frame_driver.h"
#include "game/routines.h"
#include "temporary_cleanup.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace psmodern;
int main(int argc,char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    if (argc!=3) return 1;
    if (!*argv[1] || !std::filesystem::exists(argv[1])) return 77;
    const auto folder=std::filesystem::temp_directory_path()/
        ("psmodern-rom-app-"+std::string(argv[2])+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto check=[](bool ok,const char* text) { if (!ok) throw std::runtime_error(text); };
    try {
        const auto version=identify_rom(argv[1]);
        check(version==(std::string_view(argv[2])=="us" ? RomVersion::us_rev_a : RomVersion::brazilian),"wrong ROM");
        check(std::filesystem::create_directory(folder),"new temporary save folder");
        std::ifstream f(argv[1],std::ios::binary);
        auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
        {
        // The original is the oracle's native machine: since milestone 14 a
        // Machine without a registry accepts interrupts in C++ and is no
        // longer an independent original.
        app::FrameDriver driver(rom,app::SaveStore(folder,version)); oracle::NativeMachine direct(rom);
        unsigned dirty_frames=0;
        std::uint64_t observer_cycle=0;
        driver.set_frame_probe([&](const Machine& m) {
            // Everything the frame hands over was taken just before this
            // probe: the sound chip's queues are empty at the frame end.
            observer_cycle=m.cycles();
            check(m.psg().writes().empty(),"the driver left PSG writes of the frame in the machine");
            check(!m.memory().cartridge_ram_dirty(),"the driver left the frame's cartridge RAM change unsaved");
        });
        for (unsigned n=0;n<1200;++n) {
            const auto before=driver.store().writes();
            direct.run_frame({}); const bool dirty=direct.memory().cartridge_ram_dirty();
            const auto output=driver.run({});
            check(observer_cycle==direct.cycles(),"the frame observer is not at the original's frame end");
            check(!output.save_error,"unexpected save write error");
            check(output.writes==direct.take_psg_writes(),"PSG writes differ from the original");
            check(output.samples==direct.take_samples(),"samples differ from the original");
            check(driver.machine().picture()==direct.picture(),"picture differs from the original");
            check(driver.store().writes()==before+(dirty ? 1 : 0),"writes must occur only on dirty frames");
            if (dirty) { ++dirty_frames; check(driver.store().load()==direct.memory().cartridge_ram(),"saved image differs"); }
            direct.memory().clear_cartridge_ram_dirty();
            // The native machine catches up to where the driver's run ended
            // (up to one segment after the frame end, milestone 14).
            while (direct.cycles()<driver.machine().cycles()) direct.step();
            check(direct.cycles()==driver.machine().cycles(),"the original has no instruction boundary where the driver stopped");
            // The documented equality (the core's internal scratch excluded:
            // the driver's machine runs fewer core steps since milestone 14),
            // and the host queues it leaves out: what the sound chip was given
            // and generated after the frame end, and the cartridge RAM written
            // after it (the next frame's save), each the original's.
            if (!oracle::equal(driver.machine(),direct,oracle::Scope::whole)) {
                for (const auto& d:oracle::differences(driver.machine(),direct,oracle::Scope::whole)) std::cerr<<d<<'\n';
                check(false,"complete machine state differs");
            }
            check(driver.machine().psg().writes()==direct.psg().writes(),"PSG writes after the frame end differ (driver did not drain)");
            {
                Machine driver_copy(driver.machine()); oracle::NativeMachine direct_copy(direct);
                check(driver_copy.take_samples()==direct_copy.take_samples(),"samples after the frame end differ (driver did not drain)");
            }
            check(driver.machine().memory().cartridge_ram_dirty()==direct.memory().cartridge_ram_dirty(),
                  "cartridge RAM written after the frame end differs");
        }
        const auto before=driver.store().writes(); check(!driver.flush() && driver.store().writes()==before,"clean exit wrote save");
        check(driver.routines().find(game::controller_poll_entry)->counters.cpp_calls>100,
            "default frame driver did not run the game routine");
        std::cout<<"1200 frames: identical Machine state, picture, writes and samples; both queues drained every frame; "
                 <<dirty_frames<<" dirty writes, "<<1200-dirty_frames<<" unchanged frames without writes.\n";
        } // Release the OS save lock before removing the fixture.
        test::remove_temporary_folder(folder); return 0;
    } catch (const std::exception& e) {
        std::cerr<<e.what()<<'\n'; test::remove_temporary_folder(folder); return 1;
    }
}
