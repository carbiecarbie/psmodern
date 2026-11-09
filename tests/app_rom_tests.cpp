// SPDX-License-Identifier: GPL-3.0-or-later
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
        app::FrameDriver driver(rom,app::SaveStore(folder,version)); Machine direct(rom);
        unsigned dirty_frames=0;
        for (unsigned n=0;n<1200;++n) {
            const auto before=driver.store().writes();
            direct.run_frame({}); const bool dirty=direct.memory().cartridge_ram_dirty();
            const auto output=driver.run({});
            check(!output.save_error,"unexpected save write error");
            check(output.writes==direct.take_psg_writes(),"PSG writes differ from Machine");
            check(output.samples==direct.take_samples(),"samples differ from Machine");
            check(driver.machine().picture()==direct.picture(),"picture differs from Machine");
            check(driver.machine().take_samples().values.empty() && driver.machine().take_psg_writes().empty(),"driver did not drain");
            check(driver.store().writes()==before+(dirty ? 1 : 0),"writes must occur only on dirty frames");
            if (dirty) { ++dirty_frames; check(driver.store().load()==direct.memory().cartridge_ram(),"saved image differs"); }
            direct.memory().clear_cartridge_ram_dirty();
            check(driver.machine().equivalent(direct),"complete machine state differs");
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
