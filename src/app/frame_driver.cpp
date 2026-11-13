// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "game/routines.h"
#include "rom/sha1.h"
#include "app/automap_store.h"
#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace psmodern::app {
namespace {
// Arguments arrive as UTF-8 (utf8_arguments on Windows); say so explicitly
// rather than rely on the library's narrow-string conversion.
std::filesystem::path utf8_path(std::string_view text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
}
std::string app_usage() {
    return "Usage: psmodern.exe | psmodern.exe --version | psmodern.exe <rom-path> [--record <new-file>] [--no-routines] "+game::OptionFlags::usage()+
        "\nRun without arguments for the start screen. Provide an uncompressed US Rev-A or Brazilian .sms file. Omitted rate flags use saved settings; new saved games default to the original game.";
}
LoadedRom load_rom(const std::filesystem::path& path) {
    const auto version=identify_rom(path);
    std::ifstream file(path,std::ios::binary);
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());
    if (file.bad() || rom->size()!=512*1024 || identify_digest(sha1(*rom))!=version)
        throw std::runtime_error("ROM changed or could not be read completely while loading.");
    return {version,std::move(rom)};
}
AppArguments parse_app_arguments(int argc,const char* const* argv) {
    const auto usage=app_usage();
    AppArguments result;
    if (argc<2) { result.mode=AppMode::launcher; return result; }
    if (std::string_view(argv[1])=="--version") {
        if (argc!=2) throw std::runtime_error(usage);
        result.mode=AppMode::version; return result;
    }
    result.rom=utf8_path(argv[1]);
    game::OptionFlags flags;
    for (int i=2;i<argc;++i) {
        const std::string_view arg=argv[i];
        if (flags.take(argc,argv,i)) continue;
        if (arg=="--no-routines" && result.routines_enabled) result.routines_enabled=false;
        else if (arg=="--record" && !result.record_path && i+1<argc) result.record_path=utf8_path(argv[++i]);
        else throw std::runtime_error(usage);
    }
    if (!result.routines_enabled) flags.refuse_with("--no-routines","the options are carried out by the rewritten routines");
    if (result.record_path) flags.refuse_with("--record","recordings always play the original game");
    result.options=flags.options;
    result.overrides=flags.overrides();
    return result;
}
void guard_observed_instructions(const RoutineRegistry& registry) {
    auto name=[](RoutineEntry entry) {
        static constexpr char hex[]="0123456789ABCDEF";
        std::string text{hex[(entry.bank>>4)&15],hex[entry.bank&15],':'};
        for(int shift=12;shift>=0;shift-=4)text+=hex[(entry.address>>shift)&15];
        return text;
    };
    for(const auto watched:host_observed_instructions) {
        if(registry.find(watched))
            throw std::logic_error("Host-observed instruction "+name(watched)+" must remain a native instruction (it is a registered entry).");
        for(const auto& [entry,routine]:registry.entries())
            for(const auto& exit:routine.spec.exits)
                if(exit.address==watched.address)
                    throw std::logic_error("Host-observed instruction "+name(watched)+" must remain a native instruction (it is a declared exit of "+name(entry)+").");
    }
}
FrameDriver::FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,bool routines_enabled,game::OptionOverrides overrides,bool original_only)
    :rom_(std::move(rom)),store_(std::move(store)),machine_(std::make_unique<Machine>(rom_,store_.load())),original_only_(original_only) {
    routines_.enabled=routines_enabled;
    chosen_=overrides.apply(store_.load_preferences(machine_->memory().cartridge_ram()));
    game::validate(chosen_);
    if(options_restricted())chosen_={};
    activate(chosen_);
    // The automap: tables from the player's identified ROM; never in recording
    // mode or with --no-routines, which neither track nor read the file.
    bool identified=false;
    try { identify_digest(sha1(*rom_));identified=true; } catch(const std::exception&) {}
    if(identified && !options_restricted()) {
        tables_=std::make_unique<game::DungeonTables>(std::span<const std::uint8_t>(*rom_));
        tracker_=std::make_unique<game::ExplorationTracker>(*tables_);
        store_.load_automap();
        automap_=store_.automap_preference();
    }
}
void FrameDriver::activate(const game::Options& options) {
    RoutineRegistry next;next.enabled=routines_.enabled;next.verifier=routines_.verifier;
    game::register_routines(next,sha1(*rom_),options);
    guard_observed_instructions(next);
    routines_=std::move(next);effective_=options;
}
namespace {
bool same_catalogue(const RoutineRegistry& a,const RoutineRegistry& b) {
    if(a.entries().size()!=b.entries().size())return false;
    for(const auto& [entry,routine]:a.entries()) {
        const auto* other=b.find(entry);if(!other)return false;
        const auto& x=routine.spec;const auto& y=other->spec;
        if(x.unit!=y.unit || x.slot2_mapper!=y.slot2_mapper || x.scratch_stack!=y.scratch_stack ||
           x.single_instruction!=y.single_instruction || x.force_admit!=y.force_admit ||
           x.absolute_wram_store!=y.absolute_wram_store || x.inherited_stack!=y.inherited_stack ||
           x.exits.size()!=y.exits.size() || !std::equal(x.exits.begin(),x.exits.end(),y.exits.begin(),
               [](const auto& first,const auto& second){return first.same_declaration(second);}))return false;
    }
    return true;
}
}
void FrameDriver::configure_languages(const SessionRoms& session) {
    if(languages_configured_)throw std::logic_error("Session languages are already configured");
    if(!session.image(session.active) || session.image(session.active)!=rom_)
        throw std::invalid_argument("Session active ROM does not match the running machine");
    if(options_restricted() && session.active!=native_language(session.source_version))
        throw std::invalid_argument("Restricted sessions require the startup ROM");
    // Validate every image and catalogue before publishing any availability.
    std::array<std::optional<Machine::RomReplacement>,2> prepared;
    for(unsigned i=0;i<2;++i)if(session.images[i]) {
        const auto digest=sha1(*session.images[i]);
        if(identify_digest(digest)!=(i ? RomVersion::brazilian : RomVersion::us_rev_a))
            throw std::invalid_argument("Language image is in the wrong session slot");
        RoutineRegistry target;game::register_routines(target,digest,effective_);guard_observed_instructions(target);
        if(!same_catalogue(routines_,target))throw std::logic_error("Language routine catalogues differ");
        prepared[i]=Machine::prepare_rom_replacement(session.images[i]);
    }
    replacements_=std::move(prepared);language_roms_=session.images;
    source_version_=session.source_version;language_=session.active;languages_configured_=true;
}
void FrameDriver::prepare_language(LanguagePreference preference) {
    prepared_language_.reset();
    if(!languages_configured_ || options_restricted() || !menu_active_ || menu_closing_ || menu_purpose_!=HostMenu::options)
        throw std::logic_error("Language can change only inside an ordinary Options hold");
    (void)format_language(preference); // refuse unknown enum values
    const auto target=resolve_language(preference,source_version_);
    const auto index=target==Language::english ? 0u : 1u;
    if(!replacements_[index])throw std::runtime_error("The selected language ROM is unavailable");
    RoutineRegistry candidate;game::register_routines(candidate,sha1(*language_roms_[index]),effective_);
    guard_observed_instructions(candidate);
    if(!same_catalogue(routines_,candidate))throw std::logic_error("Language routine catalogues differ");
    prepared_language_=target;
}
void FrameDriver::commit_language() noexcept {
    if(prepared_language_) {
        pending_language_=*prepared_language_==language_ ? std::nullopt : prepared_language_;
        prepared_language_.reset();
    }
}
// Passive: reads the machine with peek at native instructions of bank 0 and
// never writes machine state.
void FrameDriver::track(std::uint16_t pc) {
    namespace dungeon=game::dungeon;
    auto block=[&] {
        std::array<std::uint8_t,dungeon::game_block_bytes> bytes{};
        for(unsigned i=0;i<bytes.size();++i)bytes[i]=machine_->peek(static_cast<std::uint16_t>(dungeon::game_block+i));
        return bytes;
    };
    if(pc==dungeon::sample.address) {
        // 00:0F48 lies in the dungeon pass (00:0F3C), which the main loop
        // dispatches on the mode's low five bits; only a mode of exactly 0Bh
        // is a dungeon walk (tested with mode 2Bh in automap_session).
        if(machine_->peek(dungeon::game_mode)!=dungeon::dungeon_mode)return;
        game::DungeonSample sample{machine_->peek(dungeon::floor),machine_->peek(dungeon::position),
            static_cast<std::uint8_t>(machine_->peek(dungeon::facing)&3),{}};
        for(unsigned i=0;i<sample.layout.size();++i)sample.layout[i]=machine_->peek(static_cast<std::uint16_t>(dungeon::layout+i));
        tracker_->observe(sample);
        log({MapEvent::Kind::sample,machine_->cycles(),sample.floor,sample.cell,sample.facing});
    } else if(pc==dungeon::after_save.address) {
        auto digest=automap_digest(block());
        store_.stage_automap(digest,tracker_->exploration());
        log({MapEvent::Kind::stage,machine_->cycles(),0,0,0,std::move(digest)});
    } else if(pc==dungeon::after_load.address) {
        auto digest=automap_digest(block());
        const auto* saved=store_.automap_record(digest);
        if(saved)tracker_->replace(*saved); else tracker_->reset();
        log({MapEvent::Kind::replace,machine_->cycles(),0,0,0,std::move(digest),saved!=nullptr});
    } else if(pc==dungeon::after_new_game.address) {
        tracker_->reset();
        log({MapEvent::Kind::reset,machine_->cycles()});
    } else if(pc==dungeon::pass_end.address) {
        // Between two walking passes the mode stays 0Bh (0Ah on a reload);
        // anything else means the party left the dungeon.
        const auto mode=machine_->peek(dungeon::game_mode);
        if(mode!=dungeon::dungeon_mode && mode!=dungeon::dungeon_load_mode && tracker_->leave())
            log({MapEvent::Kind::leave,machine_->cycles()});
    }
}
std::optional<std::string> FrameDriver::choose_options(const game::Options& options) {
    if(options_restricted())return "Settings cannot change while recording or with --no-routines.";
    if(auto error=store_.choose(options,machine_->memory().cartridge_ram()))return error;
    chosen_=options;return {};
}
std::optional<std::string> FrameDriver::choose_options(const game::Options& options,bool automap) {
    if(options_restricted())return "Settings cannot change while recording or with --no-routines.";
    if(auto error=store_.choose(options,automap,machine_->memory().cartridge_ram()))return error;
    chosen_=options;automap_=automap;return {};
}
bool FrameDriver::request_menu(HostMenu purpose) {
    // One request per frame: a second (Options and the map pressed together)
    // never replaces the first.
    if (original_only_ || menu_active_ || menu_request_ || !exploration_wait_ || machine_->peek(0xc212) ||
        machine_->peek(0xc204) || machine_->peek(0xc205)) return false;
    if (purpose==HostMenu::map && (!automap_enabled() || *exploration_wait_!=game::dungeon::sample.address ||
        machine_->peek(game::dungeon::floor)>=game::dungeon::floor_count)) return false;
    if (machine_->peek(0xc202)!=(*exploration_wait_==0x0b14 ? 9 : 11)) return false;
    if (*exploration_wait_==0x0b14 && machine_->peek(0xc265)) return false;
    menu_request_=exploration_wait_;requested_purpose_=purpose;
    return true;
}
void FrameDriver::speed(unsigned value) {
    if(value!=1 && value!=2 && value!=4)throw std::invalid_argument("Invalid speed.");
    speed_=original_only_ ? 1 : value;
    if(tempo_audio_)tempo_audio_->speed(speed_);
}
FrameOutput FrameDriver::run(const FrameInput& input) {
    std::optional<std::string> audio_error;
    auto audio_failure=[&](const std::exception& error) {
        tempo_audio_.reset();audio_failed_=true;
        audio_error=std::string("Normal-tempo audio unavailable; accelerated audio is muted until power cycle: ")+error.what();
    };
    Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
        if(tracker_ && mapping==Memory::Mapping{Memory::Kind::rom,0})track(pc);
        if(!original_only_ && !audio_failed_)try {
            if(pc==0x8043 && mapping==Memory::Mapping{Memory::Kind::rom,12}) {
                if(tempo_audio_)tempo_audio_->update(machine_->cycles(),0x80);
                else if(speed_>1) {
                    tempo_audio_=std::make_unique<TempoAudio>(*machine_);tempo_audio_->speed(speed_);
                    sound_request_.reset(); // The captured first tick consumed it.
                }
            } else if(tempo_audio_) {
                if(pc==0x82ef && mapping==Memory::Mapping{Memory::Kind::rom,12}) {
                    if(sound_request_ && *sound_request_==machine_->peek(0xc004))
                        tempo_audio_->request(machine_->cycles(),*sound_request_);
                    sound_request_.reset();
                } else if(mapping==Memory::Mapping{Memory::Kind::rom,0}) {
                    if(pc==0x02ed) {tempo_audio_->initialize(machine_->cycles());sound_request_.reset();}
                    if(pc==0x02f5)tempo_audio_->silence(machine_->cycles());
                }
            }
        } catch(const std::exception& error) {audio_failure(error);}
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
            if(pending_language_) {
                const auto index=*pending_language_==Language::english ? 0u : 1u;
                machine_->replace_rom(*replacements_[index]);
                rom_=language_roms_[index];language_=*pending_language_;
                pending_language_.reset();++language_changes_;
                // The shared registration path is version independent and
                // reads through Machine. Keep its effective rates, counters
                // and verifier, as well as the audio/map/tracker instances.
            }
        }
        if (menu_request_ && pc==*menu_request_ && mapping==Memory::Mapping{Memory::Kind::rom,0}) {
            menu_request_.reset();
            const auto lo=static_cast<std::uint16_t>(sp-2),hi=static_cast<std::uint16_t>(sp-1);
            bool stack_room=sp>=0xc080 && sp<=0xfffb;
            for(unsigned depth=1;stack_room && depth<=128;++depth)
                stack_room=memory.mapping(static_cast<std::uint16_t>(sp-depth)).kind==Memory::Kind::ram;
            // The map's floor is checked with the rest of the admission, before
            // anything is held (request_menu already refused floor 61 or more).
            if (stack_room && memory.mapping(lo).kind==Memory::Kind::ram && memory.mapping(hi).kind==Memory::Kind::ram &&
                lo<0xfffc && hi<0xfffc && sp==static_cast<std::uint16_t>(exploration_sp_+2) &&
                (requested_purpose_!=HostMenu::map || machine_->peek(game::dungeon::floor)<game::dungeon::floor_count) &&
                machine_->peek(0xc202)==(pc==0x0b14 ? 9 : 11) &&
                (pc!=0x0b14 || !machine_->peek(0xc265)) &&
                !machine_->peek(0xc212) && !machine_->peek(0xc204) && !machine_->peek(0xc205)) {
                menu_caller_=cpu;menu_return_=pc;
                menu_saved_={machine_->peek(0xc204),machine_->peek(0xc205),machine_->peek(0xc800),machine_->peek(0xc900),machine_->peek(lo),machine_->peek(hi)};
                if(requested_purpose_==HostMenu::map) {
                    // Built once from the state at the wait, before the native
                    // field menu runs (it changes nothing the view reads).
                    namespace dungeon=game::dungeon;
                    const auto floor=machine_->peek(dungeon::floor);
                    game::DungeonLayout layout{};
                    for(unsigned i=0;i<layout.size();++i)layout[i]=machine_->peek(static_cast<std::uint16_t>(dungeon::layout+i));
                    std::array<std::uint8_t,dungeon::game_block_bytes> block{};
                    for(unsigned i=0;i<block.size();++i)block[i]=machine_->peek(static_cast<std::uint16_t>(dungeon::game_block+i));
                    map_view_=game::build_view(*tables_,tracker_->exploration().floors[floor],floor,layout,
                        machine_->peek(dungeon::position),static_cast<std::uint8_t>(machine_->peek(dungeon::facing)&3),block);
                }
                memory.write(lo,pc&0xff);memory.write(hi,pc>>8);cpu.sp.uint16_value=lo;
                menu_active_=true;menu_closing_=menu_cancel_seen_=false;menu_purpose_=requested_purpose_;
                return 0x1be1;
            }
        }
        if(options_pending() && pc==0x00af && mapping==Memory::Mapping{Memory::Kind::rom,0})activate(chosen_);
        return {};
    };
    auto supplied=input;
    if(menu_active_ || menu_request_) { supplied={}; if(menu_closing_ && !menu_cancel_seen_)supplied.pads[0].one=true; }
    Machine::WriteObserver sound_writes=[&](std::uint16_t address,std::uint8_t value) {
        if((address&0x1fff)!=4 || machine_->memory().mapping(address).kind!=Memory::Kind::ram)return;
        const auto writer=machine_->memory().mapping(machine_->cpu().pc.uint16_value);
        // The driver's own fades and acknowledgements belong to its own clock.
        // Only external writes create a request to relay, even if the byte did
        // not change. A later internal write can consume/discard that request.
        if(writer==Memory::Mapping{Memory::Kind::rom,12})sound_request_.reset();
        else sound_request_=value;
    };
    machine_->run_frame(supplied,routines_,hook,sound_writes);
    menu_request_.reset(); // An unsuccessful request never opens later.
    auto writes=machine_->take_psg_writes();
    auto samples=machine_->take_samples();
    std::optional<PsgSamples> playback;
    if(tempo_audio_)try {playback=tempo_audio_->finish(machine_->cycles());}
        catch(const std::exception& error) {audio_failure(error);}
    if(!playback && speed_>1)playback=PsgSamples{};
    return {std::move(writes),std::move(samples),store_.flush(machine_->memory()),std::move(playback),std::move(audio_error)};
}
std::optional<std::string> FrameDriver::power_cycle() {
    if(menu_active_ || menu_request_)return "Power cycle is unavailable while a host menu is open.";
    auto error=flush();
    machine_=std::make_unique<Machine>(rom_,machine_->memory().cartridge_ram());
    exploration_wait_.reset();menu_request_.reset();menu_active_=menu_closing_=menu_cancel_seen_=false;
    tempo_audio_.reset();audio_failed_=false;sound_request_.reset();speed_=1;
    if(tracker_) { tracker_->reset();log({MapEvent::Kind::reset,0}); }
    if(options_pending())activate(chosen_);
    return error;
}
}
