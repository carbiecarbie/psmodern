// SPDX-License-Identifier: GPL-3.0-or-later
#include "oracle/native_machine.h"
#include "app/frame_driver.h"
#include "game/routines.h"
#include "replay/recording.h"
#include "rom/sha1.h"
#include "temporary_cleanup.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace psmodern;
int main(int argc,char**argv){
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try{
    if(argc!=3)return 1;
    if(!*argv[1] || !std::filesystem::is_regular_file(argv[1]))return 77;
    const auto recording=replay::read(argv[2]);
    std::ifstream file(argv[1],std::ios::binary);auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    if(sha1(*rom)!=recording.rom_sha1 || sha1(Memory::CartridgeRam{})!=recording.cart_ram_sha1)throw std::runtime_error("Battle test expects matching ROM and zero initial SRAM");
    Machine original(rom);RoutineRegistry registry;game::register_routines(registry,recording.rom_sha1);
    bool battle=false;unsigned long long frame=0;
    // Watch the battle entry during warm-up at every instruction boundary
    // (milestone 14: the boundary observer sees it inside the segments, and
    // the machine stays at a segment boundary between frames).
    const Machine::BoundaryObserver observe=[&](const Machine::Boundary& at){
        if(at.pc==0x100f && original.memory().mapping(at.pc)==Memory::Mapping{Memory::Kind::rom,0})battle=true;
    };
    original.set_boundary_observer(&observe);
    for(;frame<recording.frames && !battle;++frame){
        original.run_frame(recording.input_at(frame),registry);
        original.take_samples();
        original.take_psg_writes();
        original.memory().clear_cartridge_ram_dirty();
    }
    original.set_boundary_observer(nullptr);
    if(!battle)throw std::runtime_error("Recording did not enter a battle");
    const auto root=std::filesystem::temp_directory_path()/("psmodern-pending-battle-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    unsigned deferred=0;bool awarded=false;
    {
        const auto version=identify_digest(recording.rom_sha1);
        app::atomic_save(root/app::save_name(version),original.memory().cartridge_ram());
        app::FrameDriver driver(rom,app::SaveStore(root,version));driver.machine()=original;
        if(driver.choose_options({50,400}))throw std::runtime_error("Pending Apply failed");
        for(;frame<recording.frames && driver.options_pending();++frame){
            // Milestone 14: the driver takes a frame's sound at the frame
            // observer (a run can end after the segment crossing the frame end).
            const auto input=recording.input_at(frame);
            std::vector<PsgWrite> writes;PsgSamples samples{};
            original.run_frame(input,registry,{},{},[&]{writes=original.take_psg_writes();samples=original.take_samples();});
            original.memory().clear_cartridge_ram_dirty();
            const auto output=driver.run(input);if(output.save_error)throw std::runtime_error(*output.save_error);
            if(driver.options_pending()){
                ++deferred;
                if(!driver.machine().equivalent(original) || output.writes!=writes || output.samples!=samples)
                    throw std::runtime_error("Pending choice changed battle state, timing, rewards or sound");
                if(const auto* entry=driver.routines().find(game::experience_award_unit);entry && entry->counters.cpp_calls)awarded=true;
            }
        }
        if(driver.options_pending() || driver.effective_options()!=game::Options{50,400} || !awarded || deferred<100)
            throw std::runtime_error("Apply did not wait across battle awards then activate at dispatcher");
    }
    test::remove_temporary_folder(root);
    std::cout<<"Pending Apply preserved "<<deferred<<" complete battle frames and experience awards before activation.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
