// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "app/host.h"
#include "oracle/native_machine.h"
#include "replay/recording.h"
#include "rom/sha1.h"
#include "temporary_cleanup.h"
#include <algorithm>
#include <chrono>
#include <iostream>

using namespace psmodern;
using namespace psmodern::app;
namespace fs=std::filesystem;
namespace {
void check(bool valid,const char* what) { if(!valid)throw std::runtime_error(what); }

// The same input and initial SRAM drive the original processor, a game with
// no Japanese cartridge, and two ordinary sessions with the cartridge.
// Compare at the frame observer, including frames ending inside an interrupt.
int replay_session(const fs::path& game,const fs::path& jp,const fs::path& path,const fs::path& refs,bool speed_test) {
    if(!fs::is_regular_file(game) || !fs::is_regular_file(jp))return 77;
    const auto session=load_session_roms(game,{},LanguagePreference::native,false,jp);
    const auto& image=session.image(session.active);
    const auto recording=replay::read(path);
    check(sha1(*image)==recording.rom_sha1,"Wrong game for recording");
    Memory::CartridgeRam ram{};
    if(sha1(ram)!=recording.cart_ram_sha1) {
        if(refs.empty())return 77;
        const auto initial=read_text_file(refs/path.parent_path().filename()/path.stem()/"cart-ram.bin",ram.size(),"initial save");
        check(initial.size()==ram.size(),"Incomplete initial save");
        std::copy(initial.begin(),initial.end(),ram.begin());
        check(sha1(ram)==recording.cart_ram_sha1,"Wrong initial save");
    }
    const auto root=fs::temp_directory_path()/("psmodern-fm-"+path.stem().string()+"-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path path; ~Cleanup(){ test::remove_temporary_folder(path); } } cleanup{root};
    auto make=[&](const char* id,bool with_jp,bool recording_mode=false) {
        const auto dir=root/id;
        fs::create_directories(dir);
        atomic_save(dir/save_name(session.source_version),ram);
        auto selected=session;
        if(!with_jp)selected.sound_rom.reset();
        auto driver=std::make_unique<FrameDriver>(image,SaveStore(dir,session.source_version),game::OptionOverrides{},recording_mode,selected.sound_rom);
        driver->configure_languages(selected);
        if(driver->fm_available()!=(with_jp && !recording_mode)) {
            auto details=driver->run({}).audio_error;
            throw std::runtime_error(details.value_or("Incorrect FM availability"));
        }
        return driver;
    };
    auto bare=make("bare",false);
    auto psg=make("psg",true);
    auto fm=make("fm",true);
    fm->choose_fm(true);
    auto continuous=speed_test ? make("continuous",true) : nullptr;
    if(continuous)continuous->choose_fm(true);
    auto native=std::make_unique<oracle::NativeMachine>(image,ram);
    auto snapshot=std::make_unique<Console>(bare->machine());
    bare->set_frame_probe([&](const Machine& m){*snapshot=m;});
    auto compare=[&](const Machine& m){
        check(snapshot->equivalent(m),"FM changed the machine at a frame boundary");
    };
    psg->set_frame_probe(compare);fm->set_frame_probe(compare);
    bool audible=false;
    Resampler host(48000);
    const auto frames=speed_test ? std::min(recording.frames,std::uint64_t{1300}) : recording.frames;
    for(std::uint64_t frame=0;frame<frames;++frame)try {
        if(speed_test) {
            const unsigned speed=frame<273 ? 1 : frame<627 ? 2 : frame<1003 ? 4 : 1;
            for(auto* driver:{bare.get(),psg.get(),fm.get()})driver->speed(speed);
            continuous->speed(speed);
            fm->choose_fm(frame<400 || frame>=470);
        }
        const auto input=recording.input_at(frame);
        native->run_frame(input);
        const auto a=bare->run(input),b=psg->run(input),c=fm->run(input);
        if(c.audio_error)throw std::runtime_error(*c.audio_error);
        if(b.audio_error)throw std::runtime_error(*b.audio_error);
        check(!a.audio_error && !a.save_error && !b.save_error && !c.save_error,"Host failed during playback");
        check(a.writes==native->take_psg_writes() && a.samples==native->take_samples(),"Original PSG differs from the Z80");
        check(a.writes==b.writes && a.writes==c.writes && a.samples==b.samples && a.samples==c.samples,
              "Sound selection changed the game's PSG bus or PCM");
        check(a.playback==b.playback && a.playback_sample_cycles==b.playback_sample_cycles,"Silent JP engine changed the PSG heard");
        if(fm->fm_selected()) {
            check(c.playback && c.playback_sample_cycles==Fm::sample_cycles,"FM samples missing at the host");
            const auto heard=host.process(c.playback->values,1,1,c.playback_sample_cycles);
            audible|=std::any_of(heard.begin(),heard.end(),[](auto v){return v!=0;});
        } else check(c.playback==b.playback,"Switching back to PSG lost its timeline");
        if(continuous) {
            const auto uninterrupted=continuous->run(input);
            check(!uninterrupted.audio_error && uninterrupted.playback_sample_cycles==Fm::sample_cycles,
                  "Uninterrupted FM reference failed");
            if(fm->fm_selected())check(c.playback==uninterrupted.playback,"Returning to FM lost its continuously running voice or sample phase");
        }
    } catch(const std::exception& e) {
        throw std::runtime_error(path.filename().string()+" frame "+std::to_string(frame)+": "+e.what());
    }
    if(frames>120)check(audible,"The selected FM source was silent throughout the recording");
    for(auto* driver:{bare.get(),psg.get(),fm.get()}) {
        check(!driver->flush(),"Could not flush the save");
        check(driver->store().load()==bare->store().load(),"Sound selection changed the saved bytes");
    }
    if(speed_test) {
        fm->set_frame_probe({});
        check(!fm->power_cycle() && fm->fm_selected(),"Power cycle failed to restore FM");
        auto fresh=make("fresh",true);
        fresh->machine().memory()=fm->machine().memory();
        fresh->choose_fm(true);
        for(int i=0;i<180;++i) {
            const auto a=fm->run({}),b=fresh->run({});
            check(a.playback==b.playback && !a.audio_error && !b.audio_error,"Power cycle retained old FM state");
        }
        auto recorded=make("recorded",true,true);
        recorded->choose_fm(true);recorded->speed(4);
        const auto original=recorded->run({});
        check(!recorded->fm_available() && !original.playback,"Recording used private sound");
        for(unsigned speed:{1u,2u,4u}) {
            // The executable only holds immutable verified images. The test
            // owns this backing storage and can fault one port operand after
            // admission, without exposing fault injection on the public API.
            auto damaged=std::make_shared<Memory::Rom>(*session.sound_rom);
            FrameDriver failed(image,SaveStore(root/("failure"+std::to_string(speed)),session.source_version),{},false,damaged);
            FrameDriver fallback(image,SaveStore(root/("fallback"+std::to_string(speed)),session.source_version));
            failed.choose_fm(true);
            check(failed.fm_selected(),"Fault fixture never had an active FM engine");
            auto& port=damaged->at(12*0x4000+0x25);
            const auto good=port;
            port=0xf2;
            unsigned errors=0;
            failed.speed(speed);fallback.speed(speed);
            for(unsigned n=0;n<220;++n) {
                const auto bad=failed.run({}),safe=fallback.run({});
                if(bad.audio_error) {
                    check(bad.audio_error->find("unexpected port")!=std::string::npos,"Unrelated failure in FM fault fixture");
                    ++errors;
                }
                if(!failed.fm_available())check(bad.playback==safe.playback && bad.samples==safe.samples &&
                    bad.playback_sample_cycles==Psg::sample_cycles,"FM failure lost the normal PSG fallback");
            }
            check(errors==1 && !failed.fm_available() && !failed.stopped(),"FM failure did not stay isolated until power cycle");
            port=good;
            check(!failed.power_cycle() && failed.fm_selected(),"Power cycle did not recover the repaired FM source");
        }
    }
    return 0;
}
}
int main(int argc,char** argv)try {
    if(argc!=5 && argc!=6)return 1;
    return replay_session(argv[1],argv[2],argv[3],argv[4],argc==6);
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
