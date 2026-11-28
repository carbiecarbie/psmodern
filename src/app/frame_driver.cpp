// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "app/host.h"
#include "app/retro_screen.h"
#include "app/retro_field_motion.h"
#include "game/routines.h"
#include "game/retro_events.h"
#include "rom/sha1.h"
#include "app/automap_store.h"
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
    return "Usage: psmodern.exe | psmodern.exe --version | psmodern.exe <rom-path> [--record <new-file>] "+game::OptionFlags::usage()+
        "\nRun without arguments for the start screen. Provide an uncompressed US Rev-A or Brazilian .sms file. Omitted rate flags use saved settings; new saved games default to the original game.";
}
LoadedRom load_rom(const std::filesystem::path& path) {
    auto rom=read_rom_image(path);
    const auto version=identify_digest(sha1(rom));
    return {version,std::make_shared<Memory::Rom>(std::move(rom))};
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
        // Milestone 14: --no-routines (the game on the Z80 core) is gone; the
        // program has no Z80 to run it on.
        if (arg=="--record" && !result.record_path && i+1<argc) result.record_path=utf8_path(argv[++i]);
        else throw std::runtime_error(usage);
    }
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
        if(const auto* r=registry.find(watched); r && !r->spec.observe_entry)
            throw std::logic_error("Host-observed instruction "+name(watched)+" requires observation before admitting its registered entry.");
        for(const auto& [entry,routine]:registry.entries())
            for(const auto& exit:routine.spec.exits)
                if(exit.address==watched.address && !(entry==watched && routine.spec.observe_entry && routine.spec.single_instruction))
                    throw std::logic_error("Host-observed instruction "+name(watched)+" is a declared exit of "+name(entry)+".");
    }
}
void require_observed_entries(const RoutineRegistry& registry) {
    guard_observed_instructions(registry);
    for(const auto watched:host_observed_instructions) {
        const auto* r=registry.find(watched);
        if(!r || !r->spec.observe_entry) {
            static constexpr char hex[]="0123456789ABCDEF";
            std::string text{hex[(watched.bank>>4)&15],hex[watched.bank&15],':'};
            for(int shift=12;shift>=0;shift-=4)text+=hex[(watched.address>>shift)&15];
            throw std::logic_error("Host-observed instruction "+text+" requires an observed segment beginning there.");
        }
    }
}
FrameDriver::FrameDriver(std::shared_ptr<const Memory::Rom> rom,SaveStore store,game::OptionOverrides overrides,bool original_only,
                         std::shared_ptr<const Memory::Rom> sound_rom)
    :rom_(std::move(rom)),store_(std::move(store)),machine_(std::make_unique<Machine>(rom_,store_.load())),original_only_(original_only) {
    chosen_=overrides.apply(store_.load_preferences(machine_->memory().cartridge_ram()));
    game::validate(chosen_);
    effective_interface_=resolve_interface(store_.interface_preference(),script_,original_only_);
    set_clean_background(effective_interface_==InterfaceMode::enhanced);
    if(options_restricted())chosen_={};
    activate(chosen_);
    // The automap: tables from the player's identified ROM; never in recording
    // mode (or a test driver without routines), which neither tracks nor reads the file.
    bool identified=false;
    try {
        source_version_=identify_digest(sha1(*rom_));
        language_=native_language(source_version_);
        identified=true;
    } catch(const std::exception&) {}
    if(identified && !options_restricted()) {
        tables_=std::make_unique<game::DungeonTables>(std::span<const std::uint8_t>(*rom_));
        tracker_=std::make_unique<game::ExplorationTracker>(*tables_);
        store_.load_automap();
        automap_=store_.automap_preference();
    }
    if(!options_restricted()) {
        sound_rom_=std::move(sound_rom);
        reset_fm();
    }
}
void FrameDriver::activate(const game::Options& options) {
    RoutineRegistry next;
    // An identified ROM registers the game: then every watched address must
    // begin its own observed segment (review R3 L1); an unidentified image
    // registers nothing.
    if(game::register_routines(next,sha1(*rom_),options)) require_observed_entries(next);
    else guard_observed_instructions(next);
    routines_=std::move(next);effective_=options;
}
namespace {
bool same_catalogue(const RoutineRegistry& a,const RoutineRegistry& b) {
    if(a.entries().size()!=b.entries().size())return false;
    for(const auto& [entry,routine]:a.entries()) {
        const auto* other=b.find(entry);if(!other)return false;
        const auto& x=routine.spec;const auto& y=other->spec;
        if(x.unit!=y.unit || x.slot2_mapper!=y.slot2_mapper || x.scratch_stack!=y.scratch_stack ||
           x.single_instruction!=y.single_instruction ||
           x.inherited_stack!=y.inherited_stack || x.observe_entry!=y.observe_entry ||
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
void FrameDriver::reset_fm() {
    fm_audio_.reset();fm_error_.reset();
    if(!sound_rom_)return;
    try { fm_audio_=std::make_unique<FmAudio>(sound_rom_); }
    catch(const std::exception& e) { fm_error_=std::string("FM unavailable until power cycle: ")+e.what(); }
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
    if(options_restricted())return "Settings cannot change while recording.";
    if(auto error=store_.choose(options,machine_->memory().cartridge_ram()))return error;
    chosen_=options;return {};
}
std::optional<std::string> FrameDriver::choose_options(const game::Options& options,bool automap,WidescreenMode widescreen) {
    return choose_options(options,automap,widescreen,store_.interface_preference());
}
std::optional<std::string> FrameDriver::choose_options(const game::Options& options,bool automap,WidescreenMode widescreen,
                                                      InterfaceMode interface_preference) {
    if(options_restricted())return "Settings cannot change while recording.";
    if(auto error=store_.choose(options,automap,widescreen,interface_preference,machine_->memory().cartridge_ram()))return error;
    chosen_=options;automap_=automap;return {};
}
std::optional<std::string> FrameDriver::choose_options(const game::Options& options,bool automap) {
    if(options_restricted())return "Settings cannot change while recording.";
    if(auto error=store_.choose(options,automap,machine_->memory().cartridge_ram()))return error;
    chosen_=options;automap_=automap;return {};
}
bool FrameDriver::request_menu(HostMenu purpose) {
    // One request per frame: a second (Options and the map pressed together)
    // never replaces the first.
    if (original_only_ || menu_active_ || menu_request_ || machine_->peek(0xc212) ||
        machine_->peek(0xc204) || machine_->peek(0xc205)) return false;
    auto wait=exploration_wait_;
    if (!wait && walking_update_ && machine_->cpu().sp.uint16_value<=exploration_sp_) {
        // A four-member party often ends the frame inside the post-wait
        // object draw. The call must still be on this walking pass's stack.
        const auto caller=machine_->peek(exploration_sp_) | (machine_->peek(exploration_sp_+1)<<8);
        if (caller==0x0b17 || caller==0x0b1a) wait=0x0b14;
    }
    if (!wait) return false;
    if (purpose==HostMenu::map && (!automap_enabled() || *wait!=game::dungeon::sample.address ||
        machine_->peek(game::dungeon::floor)>=game::dungeon::floor_count)) return false;
    if (machine_->peek(0xc202)!=(*wait==0x0b14 ? 9 : 11)) return false;
    if (*wait==0x0b14 && machine_->peek(0xc265)) return false;
    menu_request_=wait;requested_purpose_=purpose;
    return true;
}
void FrameDriver::speed(unsigned value) {
    if(value!=1 && value!=2 && value!=4)throw std::invalid_argument("Invalid speed.");
    speed_=original_only_ ? 1 : value;
    if(tempo_audio_)tempo_audio_->speed(speed_);
    if(fm_audio_)fm_audio_->speed(speed_,machine_->cycles());
}
void FrameDriver::set_clean_background(bool on) {
    on=on && !options_restricted();
    if (on==clean_enabled_) return;
    if (on) {
        if (!clean_) clean_=std::make_unique<CleanBackground>(*machine_);
        else clean_->reseed(*machine_);
    }
    clean_enabled_=on;
    machine_->set_video_observer(on ? clean_.get() : nullptr);
    // Keep the already published picture until the next frame observer.
}
void FrameDriver::set_widescreen(bool on) {
    if(on && !options_restricted()) { if(!wide_) { wide_=std::make_unique<WideView>(rom_);wide_rom_=rom_.get(); } }
    else wide_.reset();
}
bool FrameDriver::native_retro_scene() const {
    const auto mode=machine_->peek(0xc202)&31;
    return (mode<=3 && !opening_) || mode==16 || mode==17 || credits_;
}
bool FrameDriver::render_retro(std::span<std::uint32_t> pixels,const WidePicture* wide) {
    const auto* frame=clean_frame();
    if (!frame) return false;
    if (retro_shown_.native) {
        centre_pixels(pixels,frame->native,nullptr);
        return true;
    }
    centre_pixels(pixels,frame->background,nullptr);
    // The scrolling VDP masks its leftmost tile column. WideView reconstructs
    // it alongside the outside bands. Keep those eight pixels on a wide map.
    if (wide) for (int y=0;y<192;++y) for (int x=0;x<8;++x)
        pixels[y*256+x]=rgb((*wide)[y*wide_width+wide_centre_x+x]);
    const retro::Cartridge data(*frame->state.rom,frame->state.ram);
    const retro::FieldMotion field(data,frame->window_table,retro_shown_.windows.battle(data));
    if (!field.visible() && !retro_shown_.windows.paced_dialogue() && !retro_shown_.windows.open.any() &&
        !retro_shown_.windows.field_rows && !retro_shown_.windows.question_rows.visible() &&
        !retro_shown_.windows.stock_rows.visible() && !retro_shown_.windows.money_rows.visible() &&
        !retro_shown_.windows.menu_motion_visible() && retro_shown_.text.lines.empty()) return true;
    retro_layer_.assign(pixels.size(),0);
    if (retro_shown_.windows.question_rows.visible() || retro_shown_.windows.stock_rows.visible() ||
        retro_shown_.windows.show(retro::Window::saves))
        retro_shown_.windows.capture_window_arrows(data,frame->window_table);
    retro::render(retro_layer_,data,retro_shown_.windows,retro_shown_.text,retro_shown_.language,field);
    for (std::size_t i=0;i<pixels.size();++i) {
        const auto color=retro_layer_[i];
        if (!color) continue;
        // Black and white follow the same two palette entries as the ROM
        // font. Interpolate the helper colours between them at scan time.
        const auto palette=frame->palette[i];
        if (palette==(63u<<6)) { pixels[i]=color; continue; }
        const auto ink=rgb(palette>>6), paper=rgb(palette&63);
        std::uint32_t result=0xff000000;
        for (unsigned shift:{0u,8u,16u}) {
            const int lo=(paper>>shift)&255, hi=(ink>>shift)&255;
            const int shade=(color>>shift)&255;
            result|=static_cast<std::uint32_t>((lo*(255-shade)+hi*shade+127)/255)<<shift;
        }
        pixels[i]=result;
    }
    return true;
}
void FrameDriver::reset_retro() {
    opening_=credits_=false;
    retro_text_.reset();
    retro_windows_={};
    retro_next_={};
}
FrameOutput FrameDriver::run(const FrameInput& input,bool last_in_batch) {
    if(stopped_)throw std::logic_error("The game stopped; the session cannot continue.");
    std::optional<std::string> audio_error=std::move(fm_error_);
    fm_error_.reset();
    auto fm_failure=[&](const std::exception& error) {
        fm_audio_.reset();
        audio_error=std::string("FM unavailable until power cycle; using PSG: ")+error.what();
    };
    auto audio_failure=[&](const std::exception& error) {
        tempo_audio_.reset();audio_failed_=true;
        audio_error=std::string("Normal-tempo audio unavailable; accelerated audio is muted until power cycle: ")+error.what();
    };
    // Milestone 14: a hook can run inside a nested interrupt delivery (the
    // sound update at 0C:8043 runs in every interrupt, some of them inside a
    // segment). There it must be passive: Machine refuses a changed register,
    // PC, stack, ROM or registry; this refuses the rest (pads, RAM and the
    // host's menu state). The hooks that change the game watch main-program
    // addresses, which never run at depth.
    auto top_level=[&](const char* what) {
        if(machine_->depth())
            throw std::logic_error(std::string("The host's ")+what+" was reached inside a nested interrupt delivery.");
    };
    Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
        if (effective_interface()==InterfaceMode::enhanced && !machine_->depth() &&
            mapping==Memory::Mapping{Memory::Kind::rom,1}) {
            if (pc==0x45dd) credits_=true;
            if (pc==0x4676) credits_=false;
        }
        if (effective_interface()==InterfaceMode::enhanced && !machine_->depth() &&
            mapping==Memory::Mapping{Memory::Kind::rom,0}) {
            if (pc==0x00af) {
                retro_windows_.field_rows.reset();
            }
            if (pc==0x05f4) opening_=true;
            if (pc==0x00af && machine_->peek(0xc202)!=3) opening_=false;
            if (pc==0x0085) reset_retro();
            if (!native_retro_scene()) {
                if (game::retro_window_entry(pc)) retro_windows_.observe(*machine_,pc);
                if (auto resume=retro_text_.observe(*machine_,pc,retro_windows_)) return resume;
            }
        }
        if(tracker_ && mapping==Memory::Mapping{Memory::Kind::rom,0})track(pc);
        if(fm_audio_)try {
            if(mapping==Memory::Mapping{Memory::Kind::rom,12}) {
                if(pc==0x8043)fm_audio_->begin_tick(machine_->cycles());
                if(pc==0x82ef) {
                    const auto external=sound_request_ && *sound_request_==machine_->peek(0xc004)
                        ? sound_request_ : std::nullopt;
                    fm_audio_->consume(machine_->cycles(),external);
                }
            } else if(mapping==Memory::Mapping{Memory::Kind::rom,0}) {
                if(pc==0x02ed) { fm_audio_->initialize(machine_->cycles());sound_request_.reset(); }
                if(pc==0x02f5)fm_audio_->silence(machine_->cycles());
            }
        } catch(const std::exception& error) {fm_failure(error);}
        if(!original_only_ && !audio_failed_)try {
            if(pc==0x8043 && mapping==Memory::Mapping{Memory::Kind::rom,12}) {
                if(tempo_audio_)tempo_audio_->update(machine_->cycles(),0x80);
                else if(speed_>1) {
                    tempo_audio_=std::make_unique<TempoAudio>(*machine_);tempo_audio_->speed(speed_);
                    tempo_consumed_tick_=true; // FM still observes the original dispatch.
                }
            } else if(tempo_audio_) {
                if(pc==0x82ef && mapping==Memory::Mapping{Memory::Kind::rom,12}) {
                    if(!tempo_consumed_tick_ && sound_request_ && *sound_request_==machine_->peek(0xc004))
                        tempo_audio_->request(machine_->cycles(),*sound_request_);
                    tempo_consumed_tick_=false;
                } else if(mapping==Memory::Mapping{Memory::Kind::rom,0}) {
                    if(pc==0x02ed) {tempo_audio_->initialize(machine_->cycles());sound_request_.reset();}
                    if(pc==0x02f5)tempo_audio_->silence(machine_->cycles());
                }
            }
        } catch(const std::exception& error) {audio_failure(error);}
        if(pc==0x82ef && mapping==Memory::Mapping{Memory::Kind::rom,12})sound_request_.reset();
        auto& cpu=machine_->cpu(); auto& memory=machine_->memory();
        const auto sp=cpu.sp.uint16_value;
        // This token describes the currently executing native field wait, not
        // merely a game mode. It remains valid while its frame IRQ is nested.
        if (pc==0x00af && mapping==Memory::Mapping{Memory::Kind::rom,0}) walking_update_=false;
        if (pc==0x0085 && mapping==Memory::Mapping{Memory::Kind::rom,0}) {
            exploration_wait_.reset();menu_request_.reset();walking_update_=false;
            if (clean_enabled_ && clean_->shown()) clean_->reseed(*machine_);
        }
        if (pc==0x0055 && mapping==Memory::Mapping{Memory::Kind::rom,0} &&
            memory.mapping(sp).kind==Memory::Kind::ram && memory.mapping(static_cast<std::uint16_t>(sp+1)).kind==Memory::Kind::ram) {
            const auto ret=static_cast<std::uint16_t>(machine_->peek(sp)|(machine_->peek(static_cast<std::uint16_t>(sp+1))<<8));
            walking_update_=false;
            if (menu_request_ && ret!=*menu_request_) menu_request_.reset();
            if(ret==0x0b14 || ret==0x0f48) { exploration_wait_=ret;exploration_sp_=sp; }
            else exploration_wait_.reset();
        }
        const bool walking_return=exploration_wait_ && pc==*exploration_wait_ &&
            mapping==Memory::Mapping{Memory::Kind::rom,0} && sp==static_cast<std::uint16_t>(exploration_sp_+2);
        if (walking_return) {
            walking_update_=pc==0x0b14;
            exploration_wait_.reset();
        }
        if (menu_active_ && menu_closing_ && pc==0x2d93 && mapping==Memory::Mapping{Memory::Kind::rom,0} &&
            (machine_->peek(0xc205)&0x10)) {
            top_level("menu cancel");
            menu_cancel_seen_=true;
            machine_->set_pad(0,{});
        }
        if (menu_active_ && pc==menu_return_ && sp==menu_caller_.sp.uint16_value && mapping==Memory::Mapping{Memory::Kind::rom,0}) {
            top_level("menu return");
            cpu.af=menu_caller_.af;cpu.bc=menu_caller_.bc;cpu.de=menu_caller_.de;cpu.hl=menu_caller_.hl;
            cpu.af_=menu_caller_.af_;cpu.bc_=menu_caller_.bc_;cpu.de_=menu_caller_.de_;cpu.hl_=menu_caller_.hl_;
            cpu.ix_iy[0]=menu_caller_.ix_iy[0];cpu.ix_iy[1]=menu_caller_.ix_iy[1];cpu.memptr=menu_caller_.memptr;cpu.q=menu_caller_.q;
            memory.write(0xc204,menu_saved_[0]);memory.write(0xc205,menu_saved_[1]);
            memory.write(0xc800,menu_saved_[2]);memory.write(0xc900,menu_saved_[3]);
            memory.write(static_cast<std::uint16_t>(sp-2),menu_saved_[4]);memory.write(static_cast<std::uint16_t>(sp-1),menu_saved_[5]);
            machine_->set_pad(0,{});
            menu_active_=menu_closing_=menu_cancel_seen_=false;
            // Publish the interface and the cartridge language at the same verified return.
            effective_interface_=resolve_interface(store_.interface_preference(),script_,original_only_);
            if (clean_enabled_!=(effective_interface_==InterfaceMode::enhanced)) {
                reset_retro();
                set_clean_background(effective_interface_==InterfaceMode::enhanced);
            }
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
        if (menu_request_ && pc==*menu_request_ && walking_return) {
            top_level("menu request");
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
                walking_update_=false;
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
        if(options_pending() && pc==0x00af && mapping==Memory::Mapping{Memory::Kind::rom,0}) { top_level("options activation");activate(chosen_); }
        return {};
    };
    auto supplied=effective_interface()==InterfaceMode::enhanced ? retro_text_.input(input,machine_->peek(0xc212)!=0) : input;
    if(menu_active_ || menu_request_) { supplied={}; if(menu_closing_ && !menu_cancel_seen_)supplied.pads[0].one=true; }
    Machine::WriteObserver sound_writes=[&](std::uint16_t address,std::uint8_t value) {
        if((address&0x1fff)!=4 || machine_->memory().mapping(address).kind!=Memory::Kind::ram)return;
        // The writer's bank: inside a body the register file's PC is the
        // boundary PC (the instruction being rendered), written at every
        // instruction boundary of an exact body.
        const auto writer=machine_->memory().mapping(machine_->cpu().pc.uint16_value);
        // The driver's own fades and acknowledgements belong to its own clock.
        // Only external writes create a request to relay, even if the byte did
        // not change. A later internal write can consume/discard that request.
        if(writer==Memory::Mapping{Memory::Kind::rom,12})sound_request_.reset();
        else sound_request_=value;
    };
    // Milestone 14: run_frame may return after the step that crosses the
    // frame end (a C++ segment, with any interrupt handled inside it). Every
    // hand-off of the frame is taken at the frame observer, the first
    // instruction boundary at or after the frame end, where the original's
    // frame ends (and where run_frame returned before milestone 14): what
    // the player hears and sees, the end of a menu request and the save.
    std::vector<PsgWrite> writes;
    PsgSamples samples{};
    std::optional<PsgSamples> playback;
    unsigned playback_period=Psg::sample_cycles;
    std::optional<Memory> saved;
    const Machine::FrameObserver at_frame=[&] {
        if (clean_enabled_) clean_->capture(*machine_);
        else clean_.reset();
        if(wide_) {
            // A language swap earlier in this frame: the view reads the new
            // image from this capture on (its reset drops the old anchor). A
            // swap after the frame end is the next frame's, as it was.
            if(rom_.get()!=wide_rom_) { wide_->replace_rom(rom_);wide_rom_=rom_.get(); }
            wide_->capture(*machine_);
        }
        if (effective_interface()==InterfaceMode::enhanced) {
            retro_shown_=std::move(retro_next_);
            if (retro_shown_.windows.include_early_dialogue_row(retro_windows_,machine_->cycles())) {
                // The border can reach the beam before the next snapshot.
                // Use its final layout without exposing letters from a later scan.
                retro_shown_.text={};
                retro_shown_.text.lines.resize(retro_text_.view().lines.size());
            }
            retro_shown_.windows.include_early_menu_rows(retro_windows_,machine_->cycles());
            if (last_in_batch) retro_text_.presented(retro_shown_.text);
            retro_next_={retro_windows_,retro_text_.view(),language_,native_retro_scene()};
        }
        writes=machine_->take_psg_writes();
        samples=machine_->take_samples();
        if(tempo_audio_)try {playback=tempo_audio_->finish(machine_->cycles());}
            catch(const std::exception& error) {audio_failure(error);}
        if(fm_audio_)try {
            auto fm=fm_audio_->finish(machine_->cycles());
            if(prefer_fm_) {
                playback=std::move(fm);
                playback_period=Fm::sample_cycles;
            }
        } catch(const std::exception& error) {fm_failure(error);}
        // An unsuccessful request never opens later: a wait reached after the
        // frame end belongs to the next frame.
        menu_request_.reset();
        // The save file holds the cartridge RAM as it is at the frame end; a
        // write after it is the next frame's. The dirty flag is the host's
        // bookkeeping (like the sound queues taken above), not game state.
        if(machine_->memory().cartridge_ram_dirty() || store_.flush_pending()) {
            saved=machine_->memory();machine_->memory().clear_cartridge_ram_dirty();
        }
        if(frame_probe_)frame_probe_(*machine_);
    };
    try { machine_->run_frame(supplied,routines_,hook,sound_writes,at_frame); }
    catch(const std::exception& error) {
        // Planning decision 6: an error of the engine stops the game. The
        // save keeps the cartridge RAM of the last frame end (this frame's,
        // if its frame observer was reached, else the last one not yet
        // written); what the stopped frame wrote after it is not saved.
        stopped_=error.what();
        if(saved)unwritten_=std::move(saved);
        throw;
    }
    if(!playback && speed_>1)playback=PsgSamples{};
    std::optional<std::string> save_error;
    if(saved) {
        save_error=store_.flush(*saved);
        // A frame-end image the store could not write yet (a failed write
        // waiting for its retry) is kept for a stop before the next one.
        if(saved->cartridge_ram_dirty() || store_.flush_pending())unwritten_=std::move(saved);
        else unwritten_.reset();
    }
    return {std::move(writes),std::move(samples),std::move(save_error),std::move(playback),std::move(audio_error),playback_period};
}
// The two exits save different points on purpose (review R3 L3). A normal
// exit (or a power cycle) saves the machine as it is: the last frame end plus
// what the segment crossing that frame end wrote after it, writes the original
// makes at the start of the next frame, never undone, so the save is a later
// consistent point of the same run. A stop saves the image of the last frame
// end instead, since the stopped frame did not complete.
std::optional<std::string> FrameDriver::flush() {
    if(!stopped_)return store_.flush(machine_->memory(),true);
    return unwritten_ ? store_.flush(*unwritten_,true) : std::nullopt;
}
std::optional<std::string> FrameDriver::power_cycle() {
    if(stopped_)throw std::logic_error("The game stopped; the session cannot continue.");
    if(menu_active_ || menu_request_)return "Power cycle is unavailable while a host menu is open.";
    auto error=flush();unwritten_.reset();
    reset_retro();
    retro_shown_={};
    machine_=std::make_unique<Machine>(rom_,machine_->memory().cartridge_ram());
    walking_update_=false;exploration_wait_.reset();menu_request_.reset();menu_active_=menu_closing_=menu_cancel_seen_=false;
    tempo_audio_.reset();audio_failed_=tempo_consumed_tick_=false;sound_request_.reset();speed_=1;
    reset_fm();
    if(tracker_) { tracker_->reset();log({MapEvent::Kind::reset,0}); }
    if(wide_)wide_->reset();
    if(options_pending())activate(chosen_);
    effective_interface_=resolve_interface(store_.interface_preference(),script_,original_only_);
    set_clean_background(effective_interface_==InterfaceMode::enhanced);
    if (clean_enabled_) {
        clean_->reset(*machine_);
        machine_->set_video_observer(clean_.get());
    } else clean_.reset();
    return error;
}
}
