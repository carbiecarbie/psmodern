// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/joins.h"
#include "game/map.h"
#include "game/frame_updates.h"
#include "game/common.h"
#include "game/interaction_replies.h"
#include "game/interaction_setup.h"
#include "game/interaction_progress.h"
#include "game/interaction_companion.h"
#include "game/interaction_letter.h"
#include "game/interaction_rescue.h"
#include "game/interaction_progression.h"
#include "game/interaction_sales.h"
#include "game/interaction_trade.h"
#include "game/interaction_messages.h"
#include "game/interaction_prompts.h"
#include "game/interaction_party.h"
#include "game/interaction_state.h"
#include "game/interaction_story.h"
#include "game/interaction_handoffs.h"
#include "game/menu.h"
#include "game/sound.h"
#include "game/recovery.h"
#include "game/battle_commands.h"
#include "game/battle_turn.h"
#include "game/battle_magic.h"
#include "game/battle_sprites.h"
#include "game/battle_outcomes.h"
#include "game/items_field.h"
#include "game/items_story.h"
#include "game/party_helpers.h"
#include "game/battle_enemy_magic.h"
#include "game/battle_enemy.h"
#include "game/battle_actor.h"
#include "game/battle_damage.h"
#include "game/battle_status.h"
#include "game/field_magic.h"
#include "game/menu_remainder.h"
#include "game/scenes.h"
#include "game/routines.h"
#include "game/dungeon_view.h"
#include "game/video_transfer.h"
#include "machine/machine.h"
#include "oracle/native_machine.h"
#include "rom/rom.h"
#include "rom/sha1.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace psmodern;
namespace {
void check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
// Every registered entry must be one the game headers name; the routines
// already rewritten must be present. No count is fixed, so later parts only
// add their names.
bool declared(RoutineEntry entry, RoutineEntry unit) {
    auto in=[&](const auto& list) { return std::find(list.begin(),list.end(),entry)!=list.end(); };
    for (const auto list:game::joins::all)
        for (const auto& s:list) if (s.entry==entry) return s.unit==unit;
    if (entry==RoutineEntry{0,0x0429}) return unit==entry;
    if (in(game::map_button_resume_entries)) return unit==game::map::map_loop_unit;
    if (in(game::map_exit_copies)) return unit==entry;
    if (entry.bank==0 && std::ranges::find(game::map_display_entries,entry.address)!=game::map_display_entries.end())
        return unit==game::map::load_map_unit;
    if (entry.bank==1 && std::ranges::find(game::ice_display_entries,entry.address)!=game::ice_display_entries.end())
        return unit==game::map::ice_probe_unit;
    for (auto list:{game::word_product_entries,game::division_entries})
        if (entry.bank==0 && std::ranges::find(list,entry.address)!=list.end())
            return unit==RoutineEntry{0,list.front()};
    for (const auto& s:game::menu_remainder::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::scenes::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::battle_outcomes::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::items_field::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::items_story::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::party_helpers::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::battle_enemy_magic::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::battle_enemy::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (auto list:{std::span<const std::uint16_t>(game::battle_sprites::attack_entries),
                    std::span<const std::uint16_t>(game::battle_sprites::sequence_entries),
                    std::span<const std::uint16_t>(game::battle_sprites::chest_entries),
                    std::span<const std::uint16_t>(game::battle_sprites::moving_entries),
                    std::span<const std::uint16_t>(game::battle_sprites::projectile_entries)})
        if (entry.bank==1 && std::ranges::find(list,entry.address)!=list.end()) return unit==RoutineEntry{1,list.front()};
    for (const auto& s:game::battle_actor::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::battle_damage::segments)
        if (s.entry==entry && s.unit==unit) return true;
    if (in(game::dispatch_entries) || in(game::block_transfer_entries)) return entry==unit;
    if (entry.bank==1 && std::ranges::find(game::dungeon_view::overlay_entries,entry.address)!=game::dungeon_view::overlay_entries.end()) return unit==game::dungeon_view::overlays;
    for (auto s:game::video_transfer::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (auto list:{std::span<const std::uint16_t>(game::video_transfer::direct_packet_entries),
                    std::span<const std::uint16_t>(game::video_transfer::masked_packet_entries)})
        if (entry.bank==0 && std::ranges::find(list,entry.address)!=list.end())
            return unit==RoutineEntry{0,list.front()};
    if (in(game::window_row_entries)) return unit==(entry.address>=0x3A97 ? game::menu::draw_window_at_once_unit : game::menu::dialogue_scroll_unit);
    if (in(game::save_menu_entries)) return unit==game::save_menu_entries.front();
    if (entry==game::menu::menu_cancel_point) return unit==game::menu::selection_unit;
    for (auto pcs:{std::span<const std::uint16_t>(game::dungeon_view::spell_cell_entries),
                  std::span<const std::uint16_t>(game::dungeon_view::escape_choices),
                  std::span<const std::uint16_t>(game::dungeon_view::retreat_entries)})
        if (entry.bank==1 && std::ranges::find(pcs,entry.address)!=pcs.end()) return unit==RoutineEntry{1,pcs.front()};
    if (game::dungeon_view::contains(entry)) return unit==game::dungeon_view::cone;
    if (entry.bank==0 && std::ranges::find(game::dungeon_view::load_entries,entry.address)!=game::dungeon_view::load_entries.end())
        return unit==game::dungeon_view::load;
    if (entry.bank==0 && (entry.address==0x0FF3 || entry.address==0x1000 || entry.address==0x1008))
        return unit==game::dungeon_view::refresh;
    if (entry==RoutineEntry{0,0x0FFE}) return unit==entry;
    if (entry.bank==0 && (entry.address==0x0F48 || entry.address==0x0F72 || entry.address==0x0F7C))
        return unit==game::dungeon_check_unit;
    if (entry==game::dungeon_view::floor_music) return unit==entry;
    if (entry.bank==1 && std::ranges::find(game::dungeon_view::animation_entries,entry.address)!=game::dungeon_view::animation_entries.end())
        return unit==game::dungeon_view::animation;
    if (in(game::dungeon_layout_entries)) return unit==game::dungeon_layout_entries.front();
    if (in(game::dungeon_tile_entries)) return unit==game::dungeon_tile_entries.front();
    if (in(game::dungeon_pattern_entries)) return unit==game::dungeon_pattern_entries.front();
    for (auto pc:game::pitfall_video_copies)
        if (entry.bank==1 && (entry.address==pc || entry.address==pc+1 || entry.address==pc+4))
            return unit==game::dungeon::pitfall_unit;
    if (in(game::interaction_dialogue_handoffs) || in(game::interaction_item_check_entries)) return entry==unit;
    for (const auto& s:game::interaction_story_segments) if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::interaction_state_zero) if (s.entry==entry && entry==unit) return true;
    for (const auto& s:game::interaction_state_compare) if (s.entry==entry && entry==unit) return true;
    if (in(game::interaction_party_entries)) return entry==unit;
    for (const auto& p:game::interaction_prompts) if ((p.entry==entry || p.answer==entry) && p.entry==unit) return true;
    for (const auto& s:game::interaction_reply_segments) if (s.entry==entry && s.unit==unit) return true;
    for (const auto& f:game::interaction_message_followups) if (f.entry==entry && f.unit==unit) return true;
    for (const auto& f:game::interaction_dialogue_followups) if (f.entry==entry && f.unit==unit) return true;
    for (const auto& f:game::interaction_item_check_followups) if (f.entry==entry && f.unit==unit) return true;
    for (const auto& s:game::interaction_progress_segments) if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::interaction_dialogue_setup) if (s.entry==entry && entry==unit) return true;
    if (in(game::interaction_dispatch_entries)) return unit==game::interaction_dispatch_entries[0];
    if (in(game::interaction_enemy_setup)) return entry==unit;
    if (in(game::interaction_companion_entries)) return unit==RoutineEntry{1,0x49E6};
    if (entry==game::interaction_letter_copy) return unit==entry;
    if (in(game::interaction_letter_entries)) return unit==RoutineEntry{1,0x4C70};
    if (in(game::interaction_rescue_entries)) return unit==RoutineEntry{1,0x4C0D};
    if (in(game::interaction_progression_entries)) return unit==RoutineEntry{1,0x4DFB};
    for (const auto item:game::interaction_sale_entries) if (entry==item.entry) return unit==item.unit;
    for (const auto item:game::interaction_trade_entries) if (entry==item.entry) return unit==item.unit;
    if (in(game::interaction_message_entries)) return entry==unit;
    if (entry.bank==1 && entry.address>=0x589E && entry.address<0x599E && (entry.address-0x589E)%16==0)
        return unit==game::map::sprite_upload_unit;
    for (auto list:{std::span<const game::frame_updates::Transfer>(game::frame_updates::entries),
                    std::span<const game::frame_updates::Transfer>(game::frame_updates::calls),
                    std::span<const game::frame_updates::Transfer>(game::frame_updates::jumps)})
        for (auto t:list) if (entry==RoutineEntry{0,t.pc}) return unit==RoutineEntry{0,t.unit};
    for (const auto& s:game::common_entries) if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::sound::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::recovery::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::battle_commands::segments)
        if (s.entry==entry && s.unit==unit) return true;
    if (entry.bank==1 && std::ranges::find(game::dungeon_view::frame_entries,entry.address)!=game::dungeon_view::frame_entries.end()) return unit==game::dungeon_view::frame;
    for (const auto& s:game::field_magic::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::battle_status::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::battle_magic::segments)
        if (s.entry==entry && s.unit==unit) return true;
    for (const auto& s:game::battle_turn::segments)
        if (s.entry==entry && s.unit==unit) return true;
    auto dungeon=[&](const auto& list) { return std::any_of(list.begin(),list.end(),[&](const auto& s) { return s.entry==entry; }); };
    auto planned=[&](game::map::Part part) { const auto list=game::map::planned_segments(part); return std::any_of(list.begin(),list.end(),[&](const auto& s) { return s.entry==entry; }); };
    // The world map and towns (milestone 11): the six parts' planned segment lists.
    for (unsigned p=0;p<static_cast<unsigned>(game::map::Part::count);++p) if (planned(static_cast<game::map::Part>(p))) return true;
    // Menus and windows (milestone 12): a planned entry, or an entry a part adds
    // inside its own code ranges with a unit of its own planned list.
    for (unsigned p=0;p<static_cast<unsigned>(game::menu::Part::count);++p) {
        const auto part=static_cast<game::menu::Part>(p);
        const auto list=game::menu::planned_segments(part);
        if (std::any_of(list.begin(),list.end(),[&](const auto& s) { return s.entry==entry && s.unit==unit; })) return true;
        const bool own_unit=std::any_of(list.begin(),list.end(),[&](const auto& s) { return s.unit==unit; });
        const auto ranges=game::menu::part_ranges(part);
        const bool inside=std::any_of(ranges.begin(),ranges.end(),[&](const auto& range) {
            return entry.bank==range.bank && entry.address>=range.first && entry.address<=range.last; });
        if (own_unit && inside) return true;
    }
    return entry==game::controller_poll_entry || entry==game::random_entry || entry==game::money_add_unit || entry==game::text_indexed_entry ||
        in(game::encounter_entries) || in(game::experience_entries) || in(game::battle_setup_entries) || in(game::text_output_entries) || in(game::text_stream_entries) || in(game::text_dictionary_entries) || in(game::text_continuation_entries) || in(game::text_number_entries) || in(game::text_alternate_entries) ||
        dungeon(game::dungeon::cell_segments) || dungeon(game::dungeon::movement_segments) || dungeon(game::dungeon::object_segments);
}
void catalog() {
    RoutineRegistry unknown;
    check(!game::register_routines(unknown,"authored fixture") && unknown.empty(),"unknown image gets no game routines");
    const game::Options variants[]{{},{0,200},{200,400},{50,400}};
    for(auto digest:{"07fcf297be4f4c9d92cd3f119a7ac48467e06838","fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd"})
        for(const auto& options:variants) {
            RoutineRegistry r;
            check(game::register_routines(r,digest,options),"both versions get game routines");
            check(r.find(game::controller_poll_entry)!=nullptr,"controller entry exists");
            for (auto entry:game::dispatch_entries)
                check(r.find(entry)!=nullptr,"handler table dispatcher exists");
            for (auto entry:game::block_transfer_entries)
                check(r.find(entry)!=nullptr,"map or save block transfer exists");
            for (const auto& [entry,unit]:game::menu_remainder::segments)
                check(r.find(entry) && r.find(entry)->spec.unit==unit,"menu continuation has the expected owner");
            for (auto entry:game::dungeon_layout_entries)
                check(r.find(entry)!=nullptr,"dungeon floor loader exists");
            for (auto entry:game::dungeon_tile_entries)
                check(r.find(entry)!=nullptr,"dungeon tile decoder exists");
            for (auto entry:game::dungeon_pattern_entries)
                check(r.find(entry)!=nullptr,"dungeon pattern decoder exists");
            for (const auto& s:game::scenes::segments)
                check(r.find(s.entry) && r.find(s.entry)->spec.unit==s.unit,"scene registry matches its ownership table");
            for (const auto& s:game::sound::segments)
                check(r.find(s.entry) && r.find(s.entry)->spec.unit==s.unit,
                      "sound segment exists and belongs to its unit");
            const auto* random=r.find(game::random_entry);
            check(random!=nullptr && random->spec.unit==game::random_entry,"random-number entry exists");
            check(r.find(game::money_add_unit)!=nullptr,"shared money entry exists");
            for(const auto entry:game::battle_setup_entries)
                check(r.find(entry)!=nullptr && r.find(entry)->spec.unit==game::battle_setup_unit,
                      "battle-setup segment exists and belongs to its unit");
            // The dungeon cell readers (milestone 7); the movement and object
            // segments, once registered, belong to the units their lists name.
            for(const auto& [entry,unit]:game::dungeon::cell_segments)
                check(r.find(entry)!=nullptr && r.find(entry)->spec.unit==unit,"dungeon cell reader exists and is its own unit");
            auto unit_ok=[&](const auto& segments) {
                for(const auto& [entry,unit]:segments) if(const auto* found=r.find(entry)) check(found->spec.unit==unit,"dungeon segment belongs to its listed unit");
            };
            unit_ok(game::dungeon::movement_segments); unit_ok(game::dungeon::object_segments);
            for(const auto& [entry,routine]:r.entries()) check(declared(entry,routine.spec.unit),"registered entry is not declared by name");
        }
}
// Option ranges and their validation, shared with the command line.
void options() {
    using game::Options;
    check(game::original_mode(Options{}) && Options{}.encounter_percent==100 && Options{}.experience_percent==100,
          "options default to the original");
    const auto& encounters=game::encounter_percent_choices; const auto& experience=game::experience_percent_choices;
    check(encounters.count==5 && encounters.values[0]==0 && encounters.values[1]==25 && encounters.values[2]==50 && encounters.values[3]==100 && encounters.values[4]==200 &&
          experience.count==3 && experience.values[0]==100 && experience.values[1]==200 && experience.values[2]==400 &&
          encounters.original==100 && experience.original==100,"allowed option values");
    check(encounters.text()=="0, 25, 50, 100 or 200" && experience.text()=="100, 200 or 400","allowed values as text");
    check(game::switch_like_encounter_percent==25 && game::switch_like_experience_percent==400 &&
          encounters.contains(game::switch_like_encounter_percent) && experience.contains(game::switch_like_experience_percent),
          "the recommended values are allowed");
    auto refused=[](Options o) {
        try { game::validate(o); } catch(const std::invalid_argument&) { return true; }
        return false;
    };
    for(unsigned e:{0u,25u,50u,100u,200u}) for(unsigned x:{100u,200u,400u}) check(!refused(Options{e,x}),"every allowed combination is accepted");
    for(Options o:{Options{1,100},Options{24,100},Options{26,100},Options{49,100},Options{51,100},Options{99,100},Options{101,100},Options{150,100},Options{400,100},Options{401,100},Options{100,0},Options{100,25},
                   Options{100,50},Options{100,99},Options{100,101},Options{100,300},Options{100,1000},Options{100,1001}})
        check(refused(o),"values that are not allowed are refused");
    try { game::validate(Options{400,100}); check(false,"encounter 400 validated"); }
    catch(const std::invalid_argument& e) { check(std::string(e.what()).find("must be one of 0, 25, 50, 100 or 200 percent, got 400")!=std::string::npos,"encounter message lists the values"); }
    try { game::validate(Options{100,1000}); check(false,"experience 1000 validated"); }
    catch(const std::invalid_argument& e) { check(std::string(e.what()).find("must be one of 100, 200 or 400 percent, got 1000")!=std::string::npos,"experience message lists the values"); }
    RoutineRegistry r;
    try { (void)game::register_routines(r,"07fcf297be4f4c9d92cd3f119a7ac48467e06838",Options{100,0}); check(false,"invalid options registered"); }
    catch(const std::invalid_argument&) { check(r.empty(),"invalid options register nothing"); }
}
void compare(const std::shared_ptr<const Memory::Rom>& rom,RoutineRegistry& registry,unsigned current,unsigned previous,unsigned mode=0) {
    Machine cpp(rom);
    auto pad=[](unsigned bits) { return Pad{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8),bool(bits&16),bool(bits&32)}; };
    cpp.set_pad(0,pad(current)); cpp.set_pad(1,pad(current>>6));
    cpp.advance(1); // Latch pending input before entering the routine.
    auto expected_current=current;
    if(mode==1) { cpp.write_port(0x3f,0,cpp.cycles()); expected_current|=0x20; }
    if(mode==2) { cpp.memory().set_memory_control(cpp.memory().memory_control()|0x04); expected_current=0x23; }
    auto& cpu=cpp.cpu();
    cpu.pc.uint16_value=game::controller_poll_entry.address;
    const std::uint16_t stack=mode==3 ? 0xdfff : 0xc700;
    cpu.sp.uint16_value=stack;
    cpp.memory().write(stack,0x80);cpp.memory().write(static_cast<std::uint16_t>(stack+1),0x01);
    cpp.memory().write(0xc204,previous);cpp.memory().write(0xc205,0xa5);
    cpu.af.uint16_value=(previous<<8)|current;
    cpu.bc.uint16_value=0x12e3;cpu.hl.uint16_value=0x9876;
    cpu.r=previous;cpu.q=current;
    cpu.memptr.uint16_value=0xabcd;
    oracle::NativeMachine native(cpp);   // the original: the pinned core
    const auto calls=registry.find(game::controller_poll_entry)->counters.cpp_calls;
    const auto start=native.cycles(),fetches=native.opcode_fetches();
    for(unsigned instruction=0;instruction<10;++instruction) native.step();
    cpp.step(registry);
    // The documented equality of the oracle (the core's own per-step scratch is
    // not state: a rendered body leaves none), plus what that equality leaves
    // to the port and write traces: the sound writes and the cartridge RAM flag.
    if(!oracle::equal(cpp,native,oracle::Scope::whole)) {
        std::string fields;
        for(const auto& field:oracle::differences(cpp,native,oracle::Scope::whole)) fields+=' '+field;
        throw std::runtime_error("Controller state differs:"+fields);
    }
    check(cpp.psg().writes()==native.psg().writes(),"controller sound writes differ");
    check(cpp.memory().cartridge_ram_dirty()==native.memory().cartridge_ram_dirty(),"controller cartridge RAM flag differs");
    check(cpp.last_access()==native.last_access(),"controller port timestamp differs");
    check(cpp.cycles()-start==70 && cpp.opcode_fetches()-fetches==10,"controller timing or refresh differs");
    check(registry.find(game::controller_poll_entry)->counters.cpp_calls==calls+1,"C++ body did not run");
    check(cpp.cpu().pc.uint16_value==0x180 && cpp.cpu().sp.uint16_value==static_cast<std::uint16_t>(stack+2),"caller return differs");
    // Independent button-by-button expectation: press, hold and release have
    // distinct meanings. This checks the game result as well as equivalence.
    unsigned pressed=0;
    for(unsigned bit=0;bit<8;++bit) if((expected_current&(1u<<bit)) && !(previous&(1u<<bit))) pressed|=1u<<bit;
    check(cpp.memory().read(0xc204)==expected_current && cpp.memory().read(0xc205)==pressed,"held/pressed button results differ");
}
}
int main(int argc,char** argv) { try {
    catalog(); options();
    if(argc==1) return 0;
    if(argc!=3) return 1;
    if(!*argv[1] || !std::filesystem::exists(argv[1])) return 77;
    const std::string_view region=argv[2];
    check(region=="us" || region=="br","Expected ROM version us or br");
    check(identify_rom(argv[1])==(region=="us" ? RomVersion::us_rev_a : RomVersion::brazilian),"Wrong ROM version.");
    std::ifstream f(argv[1],std::ios::binary);
    auto rom=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
    RoutineRegistry registry;
    check(game::register_routines(registry,sha1(*rom)),"identified ROM registry");
    // Every possible controller byte and previous held byte, including the
    // second pad's two low bits, both R bit-7 states and all initial flags.
    for(unsigned current=0;current<256;++current)
        for(unsigned previous=0;previous<256;++previous) compare(rom,registry,current,previous);
    for(unsigned mode=1;mode<=3;++mode)
        for(unsigned current=0;current<256;++current) compare(rom,registry,current,255-current,mode);
    std::cout<<"66304 controller calls (65536 held/pressed transitions, 768 in the three special modes): "
               "complete native/C++ equivalence, port timing, cycles and fetches.\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
