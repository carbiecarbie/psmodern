// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "game/routines.h"
#include "rom/sha1.h"
#include <stdexcept>

namespace psmodern::app {
AppArguments parse_app_arguments(int argc,const char* const* argv) {
    const auto usage="Usage: psmodern.exe <rom-path> [--record <new-file>] [--no-routines] "+game::OptionFlags::usage()+
        "\nProvide an uncompressed US Rev-A or Brazilian .sms file. Omitted rate flags use saved settings; a new profile defaults to the original game.";
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
    result.overrides=flags.overrides();
    return result;
}
FrameDriver::FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,bool routines_enabled,game::OptionOverrides overrides,bool original_only)
    :rom_(std::move(rom)),store_(std::move(store)),machine_(std::make_unique<Machine>(rom_,store_.load())),original_only_(original_only) {
    routines_.enabled=routines_enabled;
    chosen_=overrides.apply(store_.load_preferences(machine_->memory().cartridge_ram()));
    game::validate(chosen_);
    if(options_restricted())chosen_={};
    activate(chosen_);
}
void FrameDriver::activate(const game::Options& options) {
    RoutineRegistry next;next.enabled=routines_.enabled;next.verifier=routines_.verifier;
    game::register_routines(next,sha1(*rom_),options);
    if(next.find({0,0x00af}))throw std::logic_error("Options boundary must remain a native instruction.");
    routines_=std::move(next);effective_=options;
}
std::optional<std::string> FrameDriver::choose_options(const game::Options& options) {
    if(options_restricted())return "Settings cannot change while recording or with --no-routines.";
    if(auto error=store_.choose(options,machine_->memory().cartridge_ram()))return error;
    chosen_=options;return {};
}
bool FrameDriver::request_menu() {
    if (original_only_ || menu_active_ || !exploration_wait_ || machine_->peek(0xc212) ||
        machine_->peek(0xc204) || machine_->peek(0xc205)) return false;
    if (machine_->peek(0xc202)!=(*exploration_wait_==0x0b14 ? 9 : 11)) return false;
    if (*exploration_wait_==0x0b14 && machine_->peek(0xc265)) return false;
    menu_request_=exploration_wait_;
    return true;
}
FrameOutput FrameDriver::run(const FrameInput& input) {
    Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
        auto& cpu=machine_->cpu(); auto& memory=machine_->memory();
        const auto sp=cpu.sp.uint16_value;
        // This token describes the currently executing native field wait, not
        // merely a game mode. It remains valid while its frame IRQ is nested.
        if (pc==0x0085 && mapping==Memory::Mapping{Memory::Kind::rom,0}) exploration_wait_.reset();
        if (pc==0x0055 && mapping==Memory::Mapping{Memory::Kind::rom,0} &&
            memory.mapping(sp).kind==Memory::Kind::ram && memory.mapping(static_cast<std::uint16_t>(sp+1)).kind==Memory::Kind::ram) {
            const auto ret=static_cast<std::uint16_t>(machine_->peek(sp)|(machine_->peek(static_cast<std::uint16_t>(sp+1))<<8));
            if(ret==0x0b14 || ret==0x0f48) { exploration_wait_=ret;exploration_sp_=sp; }
            else exploration_wait_.reset();
        }
        if (exploration_wait_ && pc==*exploration_wait_ && mapping==Memory::Mapping{Memory::Kind::rom,0}) exploration_wait_.reset();
        if (menu_active_ && menu_closing_ && pc==0x2d93 && mapping==Memory::Mapping{Memory::Kind::rom,0} &&
            (machine_->peek(0xc205)&0x10)) {
            menu_cancel_seen_=true;
            machine_->set_pad(0,{});
        }
        if (menu_active_ && pc==menu_return_ && sp==menu_caller_.sp.uint16_value && mapping==Memory::Mapping{Memory::Kind::rom,0}) {
            cpu.af=menu_caller_.af;cpu.bc=menu_caller_.bc;cpu.de=menu_caller_.de;cpu.hl=menu_caller_.hl;
            cpu.af_=menu_caller_.af_;cpu.bc_=menu_caller_.bc_;cpu.de_=menu_caller_.de_;cpu.hl_=menu_caller_.hl_;
            cpu.ix_iy[0]=menu_caller_.ix_iy[0];cpu.ix_iy[1]=menu_caller_.ix_iy[1];cpu.memptr=menu_caller_.memptr;cpu.q=menu_caller_.q;
            memory.write(0xc204,menu_saved_[0]);memory.write(0xc205,menu_saved_[1]);
            memory.write(0xc800,menu_saved_[2]);memory.write(0xc900,menu_saved_[3]);
            memory.write(static_cast<std::uint16_t>(sp-2),menu_saved_[4]);memory.write(static_cast<std::uint16_t>(sp-1),menu_saved_[5]);
            machine_->set_pad(0,{});
            menu_active_=menu_closing_=menu_cancel_seen_=false;
        }
        if (menu_request_ && pc==*menu_request_ && mapping==Memory::Mapping{Memory::Kind::rom,0}) {
            menu_request_.reset();
            const auto lo=static_cast<std::uint16_t>(sp-2),hi=static_cast<std::uint16_t>(sp-1);
            bool stack_room=sp>=0xc080 && sp<=0xfffb;
            for(unsigned depth=1;stack_room && depth<=128;++depth)
                stack_room=memory.mapping(static_cast<std::uint16_t>(sp-depth)).kind==Memory::Kind::ram;
            if (stack_room && memory.mapping(lo).kind==Memory::Kind::ram && memory.mapping(hi).kind==Memory::Kind::ram &&
                lo<0xfffc && hi<0xfffc && sp==static_cast<std::uint16_t>(exploration_sp_+2) &&
                machine_->peek(0xc202)==(pc==0x0b14 ? 9 : 11) &&
                (pc!=0x0b14 || !machine_->peek(0xc265)) &&
                !machine_->peek(0xc212) && !machine_->peek(0xc204) && !machine_->peek(0xc205)) {
                menu_caller_=cpu;menu_return_=pc;
                menu_saved_={machine_->peek(0xc204),machine_->peek(0xc205),machine_->peek(0xc800),machine_->peek(0xc900),machine_->peek(lo),machine_->peek(hi)};
                memory.write(lo,pc&0xff);memory.write(hi,pc>>8);cpu.sp.uint16_value=lo;
                menu_active_=true;menu_closing_=menu_cancel_seen_=false;
                return 0x1be1;
            }
        }
        if(options_pending() && pc==0x00af && mapping==Memory::Mapping{Memory::Kind::rom,0})activate(chosen_);
        return {};
    };
    auto supplied=input;
    if(menu_active_ || menu_request_) { supplied={}; if(menu_closing_ && !menu_cancel_seen_)supplied.pads[0].one=true; }
    machine_->run_frame(supplied,routines_,hook);
    menu_request_.reset(); // An unsuccessful request never opens later.
    auto writes=machine_->take_psg_writes();
    auto samples=machine_->take_samples();
    return {std::move(writes),std::move(samples),flush()};
}
std::optional<std::string> FrameDriver::power_cycle() {
    auto error=flush();
    machine_=std::make_unique<Machine>(rom_,machine_->memory().cartridge_ram());
    exploration_wait_.reset();menu_request_.reset();menu_active_=menu_closing_=menu_cancel_seen_=false;
    if(options_pending())activate(chosen_);
    return error;
}
}
