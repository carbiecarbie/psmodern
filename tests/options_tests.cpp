// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/frame_driver.h"
#include "app/options_menu.h"
#include "rom/sha256.h"
#include "rom/sha1.h"
#include "temporary_cleanup.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
using namespace psmodern;
using namespace psmodern::app;
namespace {
void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
template<class F>void refuses(F operation){bool refused=false;try{operation();}catch(const std::exception&){refused=true;}check(refused,"invalid input accepted");}
struct Temporary {
 std::filesystem::path path=std::filesystem::temp_directory_path()/("psmodern-options-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 Temporary(){std::filesystem::create_directories(path);}~Temporary(){test::remove_temporary_folder(path);}
};
std::shared_ptr<Memory::Rom> program(){auto rom=std::make_shared<Memory::Rom>(512*1024,0);(*rom)[0]=0xc3;(*rom)[1]=0xaf;(*rom)[0xaf]=0xc3;(*rom)[0xb0]=0xaf;(*rom)[0x100]=0xc3;(*rom)[0x102]=1;return rom;}
void change(Memory& memory,unsigned value){memory.write(0xfffc,8);memory.write(0x8000,value);}
std::string read(const std::filesystem::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void codec(){
 const std::string a(64,'a'),b(64,'b'),c(64,'c');
 SettingsImage image{{a,{100,100}},SettingsRecord{b,{50,400}},SettingsRecord{c,{0,200}}};
 const auto text=write_settings(image);check(parse_settings(text)==image,"settings round trip");
 check(image.matching(a)==game::Options{} && image.matching(b)==game::Options{50,400} && image.matching(c)==game::Options{0,200},"digest selection");
 image.pending->digest=a;image.backup->digest=a;check(image.matching(a)==game::Options{50,400},"pending wins digest tie");
 image.pending.reset();check(image.matching(a)==game::Options{},"committed wins backup tie");
 refuses([&]{parse_settings("");});refuses([&]{parse_settings("psmodern-options 2\n");});refuses([&]{parse_settings(text+"junk\n");});
 refuses([&]{parse_settings("psmodern-options 4\ncommitted "+a+" 100 100 0 0\npending -\nbackup -\n");});
 // A saved 25 (the recommended encounter rate) and an older saved 50 are both valid; 24 is not.
 {
  SettingsImage rates{{a,{25,400},false},SettingsRecord{b,{50,400},false},std::nullopt};
  check(parse_settings(write_settings(rates))==rates,"25 and 50 both round trip");
  refuses([&]{parse_settings("psmodern-options 2\ncommitted "+a+" 24 400 0\npending -\nbackup -\n");});
 }
 // Format 2 is written while no record has widescreen on; the automap switch sits beside the options.
 check(text.rfind("psmodern-options 2\n",0)==0,"settings are written as format 2");
 {
  SettingsImage with_map{{a,{50,400},true},SettingsRecord{b,{0,200},false},std::nullopt};
  const auto written=write_settings(with_map);
  check(written=="psmodern-options 2\ncommitted "+a+" 50 400 1\npending "+b+" 0 200 0\nbackup -\n","format 2 records");
  check(parse_settings(written)==with_map && parse_settings(written).select(a)->automap && !parse_settings(written).select(b)->automap,"format 2 round trip keeps the automap switch");
  // Format 1 (release 0.1.0) reads as automap off.
  const auto old=parse_settings("psmodern-options 1\ncommitted "+a+" 50 400\npending -\nbackup "+c+" 0 200\n");
  check(old.committed==SettingsRecord{a,{50,400},false} && old.backup==SettingsRecord{c,{0,200},false},"format 1 reads with automap off");
  check(write_settings(old).rfind("psmodern-options 2\n",0)==0,"format 1 is rewritten as format 2");
  for(const auto& token:{std::string("2"),std::string("01"),std::string("-1"),std::string("on"),std::string("")}) {
   auto bad="psmodern-options 2\ncommitted "+a+" 50 400 "+token+"\npending -\nbackup -\n";
   refuses([&]{parse_settings(bad);});
  }
  refuses([&]{parse_settings("psmodern-options 2\ncommitted "+a+" 50 400 1 1\npending -\nbackup -\n");});
  refuses([&]{parse_settings("psmodern-options 1\ncommitted "+a+" 50 400 1\npending -\nbackup -\n");});
 }
 // Format 3 (milestone 10): the widescreen token. Formats 1 and 2 read as off;
 // format 2 stays the written format until some record has widescreen on.
 {
  const auto old2=parse_settings("psmodern-options 2\ncommitted "+a+" 50 400 1\npending -\nbackup "+c+" 0 200 0\n");
  check(!old2.committed.widescreen && !old2.backup->widescreen && old2.committed.automap,"format 2 reads with widescreen off");
  check(!parse_settings("psmodern-options 1\ncommitted "+a+" 50 400\npending -\nbackup -\n").committed.widescreen,"format 1 reads with widescreen off");
  check(write_settings(old2).rfind("psmodern-options 2\n",0)==0 && write_settings(old2).find(" 0 0\n")==std::string::npos,"widescreen off keeps format 2 for release 0.4.0");
  for(int slot=0;slot<3;++slot) {
   SettingsImage wide{{a,{50,400},true,slot==0},SettingsRecord{b,{0,200},false,slot==1},SettingsRecord{c,{100,100},true,slot==2}};
   const auto written=write_settings(wide);
   check(written.rfind("psmodern-options 3\n",0)==0,"any widescreen record writes format 3");
   check(parse_settings(written)==wide,"format 3 round trip");
   check(parse_settings(written).committed.widescreen==(slot==0) && parse_settings(written).pending->widescreen==(slot==1) &&
         parse_settings(written).backup->widescreen==(slot==2),"format 3 keeps each record's widescreen token");
  }
  SettingsImage wide{{a,{50,400},true,true},SettingsRecord{b,{0,200},false,false},std::nullopt};
  check(write_settings(wide)=="psmodern-options 3\ncommitted "+a+" 50 400 1 1\npending "+b+" 0 200 0 0\nbackup -\n","format 3 records");
  check(write_settings(parse_settings(write_settings(wide)))==write_settings(wide),"format 3 is stable");
  for(const auto& token:{std::string("2"),std::string("01"),std::string("-1"),std::string("on"),std::string("")})
   refuses([&]{parse_settings("psmodern-options 3\ncommitted "+a+" 50 400 1 "+token+"\npending -\nbackup -\n");});
  refuses([&]{parse_settings("psmodern-options 3\ncommitted "+a+" 50 400 1\npending -\nbackup -\n");});
  refuses([&]{parse_settings("psmodern-options 3\ncommitted "+a+" 50 400 1 0 0\npending -\nbackup -\n");});
  refuses([&]{parse_settings("psmodern-options 3\ncommitted "+a+" 50 400 1 1\npending "+b+" 0 200 0\nbackup -\n");});
 }
 for(const auto& bad:{std::string("-4294967196"),std::string("+100"),std::string("4294967396"),std::string("100x")}) {
  auto invalid=text;invalid.replace(invalid.find("100"),3,bad);refuses([&]{parse_settings(invalid);});
 }
 for(std::size_t length=0;length<text.size()-1;++length)refuses([&]{parse_settings(text.substr(0,length));});
 image.committed.digest="x";refuses([&]{write_settings(image);});
}
void persistence(){
 Temporary t;Memory::CartridgeRam initial{};initial[8]=19;const auto name=t.path/save_name(RomVersion::us_rev_a);atomic_save(name,initial);
 // A request made before the settings are loaded survives the lazy load inside choose().
 {
  Temporary lazy;atomic_save(lazy.path/save_name(RomVersion::us_rev_a),initial);
  {
   SaveStore store(lazy.path,RomVersion::us_rev_a);store.request_widescreen(true);
   check(!store.choose({50,400},true,initial) && store.widescreen_preference() && store.automap_preference(),"widescreen request survives the lazy settings load");
  }
  SaveStore again(lazy.path,RomVersion::us_rev_a);again.load_preferences(again.load());check(again.widescreen_preference(),"lazily requested widescreen persisted");
 }
 std::filesystem::path sidecar;
 {
  SaveStore store(t.path,RomVersion::us_rev_a);sidecar=store.settings_path();
  check(store.load_preferences(store.load())==game::Options{},"missing metadata original");
  check(!store.choose({50,400},initial),"settings-only apply");check(read(name).size()==8192 && store.load()==initial && store.writes()==0,"settings-only raw unchanged");
  check(!store.choose({0,200},initial),"second settings-only apply");
  check(!store.automap_preference() && !store.choose({0,200},true,initial) && store.automap_preference(),"automap switch applied beside the options");
  check(!store.choose({0,200},initial) && store.automap_preference(),"applying options alone keeps the automap switch");
  check(!store.widescreen_preference() && read(sidecar).rfind("psmodern-options 2\n",0)==0,"widescreen off keeps the file format 2");
  store.request_widescreen(true);check(!store.widescreen_preference(),"a requested switch is not stored until applied");
  check(!store.choose({0,200},true,initial) && store.widescreen_preference() && read(sidecar).rfind("psmodern-options 3\n",0)==0,"widescreen applied beside the options, file written as format 3");
  check(!store.choose({0,200},initial) && store.widescreen_preference() && store.automap_preference(),"applying options alone keeps the widescreen switch");
  store.request_widescreen(false);check(!store.choose({0,200},true,initial) && !store.widescreen_preference() && read(sidecar).rfind("psmodern-options 2\n",0)==0,"widescreen off again returns to format 2");
  store.request_widescreen(true);check(!store.choose({0,200},true,initial),"widescreen on for the restart");
 }
 {
  SaveStore store(t.path,RomVersion::us_rev_a);check(store.load_preferences(store.load())==game::Options{0,200} && store.automap_preference() && store.widescreen_preference(),"automap and widescreen switches survive restart");
  store.request_widescreen(false);
  check(!store.choose({0,200},false,store.load()) && !store.automap_preference(),"automap off applied");
 }
 {
  SaveStore store(t.path,RomVersion::us_rev_a);check(store.load_preferences(store.load())==game::Options{0,200} && !store.automap_preference(),"settings restart");
  Memory memory(program(),initial);change(memory,7);check(!store.flush(memory),"raw with settings");
  change(memory,8);check(!store.flush(memory),"second raw with settings");change(memory,9);check(!store.flush(memory),"third raw with settings");
  auto backup=store.path();backup+=".bak";check(read(backup)==std::string(reinterpret_cast<const char*>(initial.data()),initial.size()),"session backup raw");
  check(!store.choose({50,400},memory.cartridge_ram()),"settings after several saves");
 }
 // Restore only raw backup: the persistent backup association survives later
 // writes, explicit choices, process restart and retired committed records.
 auto backup=name;backup+=".bak";std::filesystem::copy_file(backup,name,std::filesystem::copy_options::overwrite_existing);
 {
  SaveStore store(t.path,RomVersion::us_rev_a);check(store.load_preferences(store.load())==game::Options{0,200},"restored session backup association");
 }
 // Importing different raw bytes has no recognized association.
 initial[4]=88;atomic_save(name,initial);const auto evidence=read(sidecar);
 {
  SaveStore store(t.path,RomVersion::us_rev_a);check(store.load_preferences(store.load())==game::Options{} && store.settings_warning(),"mismatch original and warning");
  Memory memory(program(),initial);change(memory,33);check(!store.flush(memory),"mismatch does not block raw saves");
  check(read(sidecar)==evidence,"mismatched companion preserved");
  check(!store.choose({50,400},memory.cartridge_ram()),"explicit action resolves mismatch");
 }
 {
  SaveStore store(t.path,RomVersion::us_rev_a);check(store.load_preferences(store.load())==game::Options{50,400},"resolved settings restart");
 }
 // Malformed/unsupported/oversized/non-file evidence stays untouched while
 // normal raw SRAM saves continue; explicit Apply to a directory still fails.
 for(const auto& bad:{std::string("invalid\n"),std::string("psmodern-options 99\n"),std::string(2048,'x')}) {
  {std::ofstream file(sidecar,std::ios::binary);file<<bad;}
  SaveStore store(t.path,RomVersion::us_rev_a);auto raw=store.load();check(store.load_preferences(raw)==game::Options{},"malformed original");
  Memory memory(program(),raw);change(memory,raw[0]+1);check(!store.flush(memory),"malformed metadata cannot block raw");check(read(sidecar)==bad,"malformed evidence unchanged");
 }
 std::filesystem::remove(sidecar);std::filesystem::create_directory(sidecar);
 {SaveStore store(t.path,RomVersion::us_rev_a);auto raw=store.load();store.load_preferences(raw);Memory memory(program(),raw);change(memory,raw[0]+1);check(!store.flush(memory),"directory companion raw save");check(store.choose({50,400},memory.cartridge_ram()).has_value(),"explicit directory refusal");}
}
void failures(){
 for(unsigned phase=0;phase<4;++phase) {
  Temporary t;const auto name=t.path/save_name(RomVersion::us_rev_a);Memory::CartridgeRam original{};original[0]=44;atomic_save(name,original);
  bool failing=false;unsigned calls=0;auto metadata=[&](const auto& path,const auto& text){if(failing && calls++==phase)throw std::runtime_error("injected companion failure");atomic_settings(path,text);};
  bool raw_fail=false;auto writer=[&](const auto& path,const auto& image){if(raw_fail)throw std::runtime_error("injected SRAM failure");atomic_save(path,image);};
  {
   SaveStore store(t.path,RomVersion::us_rev_a,writer,metadata);store.load_preferences(store.load());check(!store.choose({50,400},original),"initial choice");
   Memory memory(program(),original);change(memory,45);failing=true;raw_fail=true;
   check(store.flush(memory).has_value() && memory.cartridge_ram_dirty() && store.load()==original,"failure preserves disk and dirty image");
   failing=false;raw_fail=false;check(!store.flush(memory) && !memory.cartridge_ram_dirty() && store.load()[0]==45,"failed publication retry");
  }
  {SaveStore store(t.path,RomVersion::us_rev_a);check(store.load_preferences(store.load())==game::Options{50,400},"recovered staged options");}
 }
 Temporary t;SaveStore store(t.path,RomVersion::us_rev_a);auto zero=store.load();store.load_preferences(zero);
 check(!store.choose({50,400},zero) && !std::filesystem::exists(store.path()),"zero/no raw settings-only choice");
 Temporary equal;const auto name=equal.path/save_name(RomVersion::us_rev_a);atomic_save(name,zero);
 std::filesystem::path sidecar;
 {SaveStore same(equal.path,RomVersion::us_rev_a);sidecar=same.settings_path();same.load_preferences(same.load());check(!same.choose({50,400},zero),"equal digest first choice");check(!same.choose({0,200},zero),"equal digest second choice");}
 {SaveStore same(equal.path,RomVersion::us_rev_a);check(same.load_preferences(same.load())==game::Options{0,200},"equal digest loads latest choice");}
 auto companion_backup=sidecar;companion_backup+=".bak";std::filesystem::copy_file(companion_backup,sidecar,std::filesystem::copy_options::overwrite_existing);
 {SaveStore same(equal.path,RomVersion::us_rev_a);check(same.load_preferences(same.load())==game::Options{},"matching companion backup restores historical equal-digest choice");}
 Temporary blocked;auto backup_failure=[](const auto&,const auto&){throw std::runtime_error("metadata backup unavailable");};
 SaveStore failing(blocked.path,RomVersion::us_rev_a,atomic_save,backup_failure);Memory changed(program());change(changed,21);
 check(failing.flush(changed).has_value() && changed.cartridge_ram_dirty() && !std::filesystem::exists(failing.path()),"companion backup failure prevents raw publication");
 // A distinct raw-backup publication failure occurs after the companion
 // backup was published, before staging the main companion or live SRAM.
 Temporary raw_backup;const auto raw_name=raw_backup.path/save_name(RomVersion::us_rev_a);Memory::CartridgeRam original{};original[0]=44;atomic_save(raw_name,original);
 {SaveStore setup(raw_backup.path,RomVersion::us_rev_a);setup.load_preferences(setup.load());check(!setup.choose({50,400},original),"raw backup failure fixture settings");}
 auto obstructed=raw_name;obstructed+=".bak";std::filesystem::remove(obstructed);std::filesystem::create_directory(obstructed);
 auto main_companion=raw_name;main_companion+=".options";const auto before=read(main_companion);
 {
  SaveStore retry(raw_backup.path,RomVersion::us_rev_a);retry.load_preferences(retry.load());Memory memory(program(),original);change(memory,45);
  for(unsigned attempt=0;attempt<2;++attempt)check(retry.flush(memory).has_value() && memory.cartridge_ram_dirty() && retry.writes()==0 && retry.load()==original && read(main_companion)==before,"failed raw backup keeps live SRAM/main companion/dirty retry unchanged");
  for(const auto& item:std::filesystem::directory_iterator(raw_backup.path))check(item.path().filename().string().find(".tmp-")==std::string::npos,"raw backup failure cleans its temporary file");
  std::filesystem::remove(obstructed);check(!retry.flush(memory) && !memory.cartridge_ram_dirty() && retry.load()[0]==45,"raw backup publication retries after obstacle removal");
 }
 {SaveStore recovered(raw_backup.path,RomVersion::us_rev_a);check(recovered.load()[0]==45 && recovered.load_preferences(recovered.load())==game::Options{50,400},"raw backup failure recovery survives restart");}
}
void driver(){
 Temporary t;
 {
  FrameDriver driver(program(),SaveStore(t.path,RomVersion::us_rev_a));
  driver.machine().cpu().pc.uint16_value=0x100;
  check(!driver.choose_options({50,400}) && driver.options_pending() && driver.effective_options()==game::Options{},"Apply queued and durable");
  driver.run({});check(driver.options_pending(),"busy native path leaves old complete registry");
  check(!driver.choose_options({0,200}),"new Apply supersedes pending");
  driver.machine().cpu().pc.uint16_value=0xaf;driver.run({});check(!driver.options_pending() && driver.effective_options()==game::Options{0,200},"dispatcher activates newest choice");
  check(!driver.choose_options({50,400}) && !driver.power_cycle() && driver.effective_options()==game::Options{50,400},"F5 starts latest chosen settings");
  // The automap switch takes effect when applied and never makes routine
  // options pending; an unidentified image has no tracker, so no map.
  check(!driver.choose_options({50,400},true) && driver.automap_option() && !driver.options_pending(),"automap Apply never pending");
  check(!driver.automap_tracking() && !driver.automap_enabled() && !driver.request_menu(HostMenu::map),"no tracker without an identified image");
  check(!driver.choose_options({50,400},false) && !driver.automap_option(),"automap off applied");
 }
 for(bool routines:{true,false}) {
  game::OptionOverrides overrides;overrides.experience=100;
  FrameDriver driver(program(),SaveStore(t.path,RomVersion::us_rev_a),routines,overrides,!routines);
  check(driver.configured_options()==game::Options{50,400},"stored preferences retained");
  check(driver.effective_options()==(routines?game::Options{50,100}:game::Options{}),"per-field explicit100 or restricted originals");
  change(driver.machine().memory(),routines?1:2);check(!driver.flush(),"session SRAM flush");
  if(!routines)check(driver.choose_options({0,400}).has_value() && driver.choose_options({},true).has_value(),"restricted changes refused");
 }
 {
  FrameDriver recording(program(),SaveStore(t.path,RomVersion::us_rev_a),true,{},true);
  check(recording.effective_options()==game::Options{} && recording.configured_options()==game::Options{50,400},"recording forced original");
  change(recording.machine().memory(),3);check(!recording.flush(),"recording SRAM write");
 }
 {FrameDriver driver(program(),SaveStore(t.path,RomVersion::us_rev_a));check(driver.effective_options()==game::Options{50,400},"CLI and forced sessions never stored overrides");}
}
// The encounter row visits 0, 25, 50, 100 and 200 in order, in both directions.
void encounter_choices(){
 OptionsMenu menu;menu.open({},false,false);menu.update({},1);
 double now=2;
 for(unsigned expected:{200u,0u,25u,50u,100u}){menu.update({.right=true},now);now+=1;menu.update({},now);now+=1;check(menu.draft().encounter_percent==expected,"right walks the encounter choices");}
 for(unsigned expected:{50u,25u,0u,200u,100u}){menu.update({.left=true},now);now+=1;menu.update({},now);now+=1;check(menu.draft().encounter_percent==expected,"left walks the encounter choices back");}
 // A saved 50 from before the 25 choice opens on 50 and still steps to its neighbours.
 menu.open({50,400},false,false);menu.update({},now);now+=1;
 menu.update({.right=true},now);now+=1;menu.update({},now);now+=1;check(menu.draft().encounter_percent==100,"saved 50 steps to 100");
 menu.update({.left=true},now);now+=1;menu.update({},now);now+=1;menu.update({.left=true},now);now+=1;menu.update({},now);now+=1;
 check(menu.draft().encounter_percent==25,"saved 50 steps back to 25");
}
// The Language row (milestone 12): left and right step through the
// preferences that have a ROM and hand each change to the owner at once.
void language_row(){
 using P=LanguagePreference;
 OptionsMenu menu;double now=0;
 auto tap=[&](MenuInput input){menu.update({},now+=1);auto action=menu.update(input,now+=1);menu.update({},now+=1);return action;};
 menu.open({},false,false);menu.update({},now);
 check(menu.language_draft()==P::native,"the language defaults to AUTO");
 tap({.up=true});check(menu.selected()==options_layout::language_row,"up wraps to Language");
 check(tap({.right=true})==MenuAction::language && menu.language_draft()==P::english,"right: AUTO to ENGLISH, handed to the owner");
 menu.language_saved();
 check(tap({.right=true})==MenuAction::language && menu.language_draft()==P::portuguese,"right: ENGLISH to PORTUGUES");
 check(tap({.right=true})==MenuAction::language && menu.language_draft()==P::native,"right wraps to AUTO");
 check(tap({.left=true})==MenuAction::language && menu.language_draft()==P::portuguese,"left wraps back to PORTUGUES");
 check(menu.draft()==game::Options{} && !menu.automap_draft() && !menu.widescreen_draft(),"the language leaves the other values alone");
 // Held: one change at once, the next after the repeat delay.
 menu.update({},now+=1);check(menu.update({.left=true},now+=1)==MenuAction::language && menu.update({.left=true},now+=.1)==MenuAction::none &&
  menu.update({.left=true},now+=.3)==MenuAction::language && menu.language_draft()==P::native,"a held direction repeats like the other rows");
 // A failed save restores the previous preference and says so.
 tap({.right=true});check(menu.language_draft()==P::english,"draft before the failure");
 menu.language_failed(P::native);
 check(menu.language_draft()==P::native && menu.state(Language::english)=="SAVE FAILED - RETRY","a failed language save reverts and is shown");
 tap({.right=true});menu.language_saved();check(menu.state(Language::english)=="SAVES ON EXIT","a saved retry clears the failure");
 // A preference without its ROM is skipped, and the explanation names the ROM.
 menu.set_language(P::native,{true,true,false});
 check(tap({.right=true})==MenuAction::language && menu.language_draft()==P::english && tap({.right=true})==MenuAction::language && menu.language_draft()==P::native,
  "PORTUGUES without the BR ROM is skipped");
 check(!menu.available(options_layout::language_row,2) && menu.explanation_text(Language::english)[2]=="PORTUGUES NEEDS THE BR ROM.","the missing ROM is named");
 menu.set_language(P::english,{true,false,true});
 check(tap({.left=true})==MenuAction::language && menu.language_draft()==P::native,"a kept preference without its ROM can be left");
 check(tap({.right=true})==MenuAction::language && menu.language_draft()==P::portuguese,"and is not chosen again");
 menu.set_language(P::native,{true,false,false});
 check(tap({.right=true})==MenuAction::none && tap({.left=true})==MenuAction::none && menu.language_draft()==P::native,"with one choice nothing changes");
 // Leaving saves the other values as before; the language was saved already.
 menu.set_language(P::portuguese,{true,true,true});
 check(tap({.cancel=true})==MenuAction::apply && menu.language_draft()==P::portuguese,"leaving keeps the language");
 refuses([&]{menu.set_language(static_cast<P>(7),{true,true,true});});
}
// Layout in tiles, both languages, without the ROM: every label and value
// fits its window, the values window stays on the screen and above the
// explanation window, every explanation and state line fits.
void layout(){
 namespace l=options_layout;using menu_style::TileWindow;
 auto inside=[](const TileWindow& outer,const TileWindow& inner){return inner.column>=outer.column && inner.row>=outer.row &&
  inner.column+inner.width<=outer.column+outer.width && inner.row+inner.height<=outer.row+outer.height;};
 const TileWindow screen{0,0,menu_style::columns,menu_style::rows};
 check(inside(screen,l::list) && inside(screen,l::explanation) && l::list.row+l::list.height<=l::explanation.row,"list and explanation on the screen, apart");
 check(l::list.height==l::line_step*int(l::rows)+1 && l::line_row(0)==l::list.row+1,"a line every two tile rows, as the game's windows");
 check(l::explanation.height==l::line_step*int(l::explanation_lines+1)+1 && l::state_row==l::explanation.row+1+l::line_step*int(l::explanation_lines),"three lines and the state line");
 check(int(l::line_limit)<=l::explanation.width-2,"explanation lines fit the window");
 for(auto language:{Language::english,Language::portuguese})
 for(bool restricted:{false,true})for(bool pending:{false,true})for(bool recording:{false,true})
 for(auto choices:{game::Options{},game::Options{25,400},game::Options{50,200},game::Options{200,100}})
 for(bool on:{false,true})for(unsigned preference=0;preference<3;++preference) {
  OptionsMenu menu;menu.open(choices,restricted,pending,recording,on,!on);
  menu.set_language(LanguagePreference(preference),{true,preference!=2,preference!=1});
  const auto state=menu.state(language);
  check(!state.empty() && state.size()<=l::line_limit,"the state line fits");
  menu.failed();check(menu.state(language).size()<=l::line_limit,"the failure line fits");
  menu.open(choices,restricted,pending,recording,on,!on);menu.update({},0);
  for(unsigned row=0;row<l::rows;++row) {
   check(menu.selected()==row,"walking the rows");
   const auto label=OptionsMenu::label(row,language);const auto values=menu.choices(row,language);
   // Label, a blank column, the value right-aligned against the right border.
   std::size_t longest=0;for(const auto& value:values)longest=std::max(longest,value.size());
   check(label.size()+(values.empty() ? 0 : 1+longest)<=std::size_t(l::list.width-2),"the label and its value fit the list");
   check(values.empty()==(row==l::controls_row),"every row but Controls has values");
   const auto window=menu.values_window(language);
   check(window.has_value()==!values.empty(),"a values window for every row with values");
   if(window) {
    check(window->column==l::values_column && window->width==l::values_width && window->height==l::line_step*int(values.size())+1,"the values window's size");
    // Its first choice on the row's line, or moved up whole lines only as far as needed to end with the list.
    check((window->row+1-l::line_row(0))%l::line_step==0 && window->row<=l::line_row(row)-1 && window->row+window->height>=l::line_row(row)+1 &&
     (window->row==l::line_row(row)-1 || window->row+window->height+l::line_step>l::list.row+l::list.height),"the values window sits beside the row");
    check(inside(screen,*window) && window->row+window->height<=l::list.row+l::list.height,"the values window stays on the screen, apart from the explanation");
    check(longest<=std::size_t(window->width-2) && menu.current(row)<values.size(),"every value fits the values window");
   }
   for(const auto& line:menu.explanation_text(language)) {
    check(!line.empty() && line.size()<=l::line_limit,"explanation line fits");
    for(char c:line)check((c>='A'&&c<='Z')||(c>='0'&&c<='9')||std::string_view(" .,%/").find(c)!=std::string_view::npos,"explanations use plain uppercase");
   }
   menu.update({.down=true},1+row*2.0);menu.update({},2+row*2.0);
  }
 }
 check(OptionsMenu::label(1,Language::english)=="EXP / MESETA" && OptionsMenu::label(1,Language::portuguese)=="EXP / MESETA" &&
  OptionsMenu::label(3,Language::english)=="WIDESCREEN" && OptionsMenu::label(3,Language::portuguese)=="WIDESCREEN","the short labels");
 // The old suffixes left the values for the explanations.
 OptionsMenu menu;menu.open({25,400},false,false);
 check(menu.choices(0,Language::english)[menu.current(0)]=="25%" && menu.choices(1,Language::english)[menu.current(1)]=="400%","plain percentages");
 check(OptionsMenu::explanation(0,Language::english)[2]=="25% IS RECOMMENDED." && OptionsMenu::explanation(1,Language::english)[2]=="400% IS RECOMMENDED.","recommendations in the explanations");
 check(game::switch_like_encounter_percent==25 && game::switch_like_experience_percent==400,"the recommended values named in the texts");
}
void navigation(){
 OptionsMenu menu;menu.open({},false,false);
 check(menu.update({.accept=true},0)==MenuAction::none,"opening held buttons consumed");menu.update({},1);
 menu.update({.right=true},2);check(menu.draft().encounter_percent==200,"right cycles encounter choices");
 menu.update({.right=true},2.1);check(menu.draft().encounter_percent==200,"held input delay");
 menu.update({.right=true},2.4);check(menu.draft().encounter_percent==0,"held repeat bounded");menu.update({},3);
 menu.update({.down=true},4);menu.update({},5);menu.update({.right=true},6);check(menu.draft().experience_percent==200,"XP choices");
 check(menu.update({.accept=true},6.1)==MenuAction::apply,"confirm persists from experience row");
 menu.update({},6.2);menu.update({.down=true},6.3);check(menu.selected()==2 && !menu.automap_draft(),"third row is the automap switch, off by default");
 menu.update({},6.31);menu.update({.right=true},6.32);check(menu.automap_draft() && menu.draft()==game::Options{0,200},"right turns the automap on and leaves the rates");
 menu.update({},6.33);menu.update({.left=true},6.34);check(!menu.automap_draft(),"left turns it off");
 menu.update({},6.35);menu.update({.left=true},6.36);check(menu.automap_draft(),"either direction toggles");
 check(menu.update({.accept=true},6.37)==MenuAction::apply,"confirm from the automap row");
 menu.update({},6.375);menu.update({.down=true},6.376);check(menu.selected()==3 && !menu.widescreen_draft(),"the fourth row is the widescreen switch, off by default");
 menu.update({},6.377);menu.update({.right=true},6.378);check(menu.widescreen_draft() && menu.automap_draft() && menu.draft()==game::Options{0,200},"right turns widescreen on and leaves the rest");
 menu.update({},6.379);menu.update({.left=true},6.380);check(!menu.widescreen_draft(),"left turns it off");
 menu.update({},6.381);menu.update({.left=true},6.382);check(menu.widescreen_draft() && menu.automap_draft(),"either direction toggles");
 check(menu.update({.accept=true},6.383)==MenuAction::apply,"confirm from the widescreen row");
 menu.update({},6.384);menu.update({.down=true},6.385);check(menu.selected()==4,"the fifth row is CONTROLS");
 menu.update({},6.391);menu.update({.right=true},6.392);menu.update({},6.393);menu.update({.left=true},6.394);
 check(menu.automap_draft() && menu.widescreen_draft() && menu.draft()==game::Options{0,200} && menu.language_draft()==LanguagePreference::native,"left and right do nothing on CONTROLS");
 menu.update({},6.395);menu.update({.down=true},6.396);check(menu.selected()==5,"sixth row is Language");
 menu.update({},6.397);check(menu.update({.accept=true},6.398)==MenuAction::apply,"accept on Language saves and leaves, as on the value rows");
 menu.update({},6.399);menu.update({.down=true},6.40);check(menu.selected()==0,"six rows wrap down");
 menu.update({},6.41);menu.update({.up=true},6.42);check(menu.selected()==5,"six rows wrap up");
 menu.update({},6.43);menu.update({.up=true},6.44);check(menu.selected()==4,"up reaches Controls");
 menu.update({},6.445);menu.update({.up=true},6.446);
 menu.update({},6.45);menu.update({.up=true},6.46);menu.update({},6.47);menu.update({.up=true},6.48);menu.update({},6.49);menu.update({.up=true},6.50);
 check(menu.selected()==0,"up reaches the first row");
 menu.update({},6.51);check(menu.update({.map=true},6.52)==MenuAction::none,"the map key does nothing in Options");
 menu.update({},6.53);check(menu.update({.accept=true},6.54)==MenuAction::apply,"confirm persists from encounter row");
 menu.update({},6.6);check(menu.update({.toggle=true},6.7)==MenuAction::apply,"options toggle saves draft on exit");
 menu.open({},true,false);menu.update({},7);menu.update({.right=true},8);check(menu.draft()==game::Options{},"restricted menu read only");
 menu.update({},8.01);menu.update({.down=true},8.02);menu.update({},8.03);menu.update({.down=true},8.04);menu.update({},8.05);menu.update({.right=true},8.06);
 check(menu.selected()==2 && !menu.automap_draft(),"restricted automap row read only");
 menu.update({},8.07);menu.update({.down=true},8.08);menu.update({},8.09);menu.update({.right=true},8.095);
 check(menu.selected()==3 && !menu.widescreen_draft(),"restricted widescreen row read only");
 check(menu.update({.accept=true},8.1)==MenuAction::close,"read-only confirmation closes without applying");
 check(menu.update({.cancel=true},9)==MenuAction::close,"restricted menu escapable");
 // Controls remains usable in restricted sessions; Language cannot open.
 menu.open({},false,false);menu.update({},10);menu.update({.right=true},10.1);menu.update({},10.2);menu.update({.up=true},10.3);
 menu.update({},10.31);menu.update({.up=true},10.32);
 check(menu.selected()==4 && menu.update({.accept=true},10.4)==MenuAction::controls,"accept on Controls opens Controls");
 menu.resume();check(menu.visible() && menu.selected()==4 && menu.draft().encounter_percent==200,"resume keeps row and draft");
 check(menu.update({.toggle=true},10.5)==MenuAction::none && menu.update({.accept=true},10.6)==MenuAction::none,"resume waits for neutral input");
 menu.update({},10.7);check(menu.update({.accept=true},10.8)==MenuAction::controls,"re-armed after resume");
 menu.resume();menu.update({},10.9);check(menu.update({.toggle=true},11)==MenuAction::apply,"Options saves on exit after round trip");
 menu.open({},true,false);menu.update({},12);menu.update({.up=true},12.1);
 menu.update({},12.11);check(menu.update({.right=true},12.12)==MenuAction::none && menu.update({},12.13)==MenuAction::none && menu.update({.left=true},12.14)==MenuAction::none &&
  menu.language_draft()==LanguagePreference::native,"Language cannot change in a restricted session");
 menu.update({},12.15);check(menu.selected()==5 && menu.update({.accept=true},12.2)==MenuAction::close,"accept on Language only closes when read-only");
 menu.update({},12.3);menu.update({.up=true},12.4);
 check(menu.update({.accept=true},12.5)==MenuAction::controls,"Controls opens also when read-only");
 // Both exit bindings save all three displayed values from every row,
 // including the child-screen entries. Restricted sessions only close.
 for(bool restricted:{false,true})for(bool toggle:{false,true})for(unsigned row=0;row<6;++row) {
  menu.open({50,400},restricted,false,false,true,true);menu.update({},20);
  menu.update({.right=true},21);menu.update({},22);
  for(unsigned i=0;i<row;++i){menu.update({.down=true},23+i*2);menu.update({},24+i*2);}
  const MenuInput exit=toggle ? MenuInput{.toggle=true} : MenuInput{.cancel=true};
  check(menu.update(exit,40)==(restricted ? MenuAction::close : MenuAction::apply),"exit saves from every editable row and only closes when read-only");
  check(menu.draft()==game::Options{restricted ? 50u : 100u,400} && menu.automap_draft() && menu.widescreen_draft(),"exit retains the displayed rates, automap and widescreen");
 }
 InputRelease release;release.consume();FrameInput held;held.pause=true;held.reset=true;held.pads[0].one=true;
 auto consumed=release.filter(held);check(!consumed.pause&&!consumed.reset&&!consumed.pads[0].one,"menu pause reset actions consumed");
 release.filter({});check(release.filter(held).pause,"fresh game input restored");
}
using Frame=std::vector<std::uint32_t>;
void write_frame(const std::filesystem::path& folder,const std::string& name,const Frame& pixels){
 if(folder.empty())return;
 std::filesystem::create_directories(folder);std::ofstream f(folder/(name+".ppm"),std::ios::binary);
 f<<"P6\n256 192\n255\n";for(auto pixel:pixels)for(unsigned shift:{16,8,0})f.put(char((pixel>>shift)&255));
 check(bool(f),"frame written");
}
// Every lit pixel of the text drawn alone is in the frame, in that colour.
bool shows(const Frame& frame,const RomFont& font,int column,int row,std::string_view text,std::uint32_t color){
 Frame own(256*192,0);menu_style::text(own,font,column,row,text,color);
 bool any=false;
 for(std::size_t i=0;i<own.size();++i)if(own[i]){any=true;if(frame[i]!=color)return false;}
 return any;
}
// The real screen with the cartridge font: every row selected, both
// languages, every state; pixels outside the windows are the game's.
void options_frames(const RomFont& font,const std::filesystem::path& output){
 namespace l=options_layout;using namespace menu_style;
 constexpr std::uint32_t backdrop=0xff123456;
 struct State {const char* name;bool restricted,pending,recording,failed,language_failed;};
 constexpr State states[]{{"normal",false,false,false,false,false},{"pending",false,true,false,false,false},{"recording",true,false,true,false,false},
  {"z80",true,false,false,false,false},{"failed",false,false,false,true,false},{"language-failed",false,false,false,false,true}};
 for(auto language:{Language::english,Language::portuguese}) {
  const std::string suffix=language==Language::english ? "-en" : "-pt";
  for(const auto& state:states)for(unsigned row=0;row<l::rows;++row)for(bool missing:{false,true}) {
   OptionsMenu menu;menu.open({25,400},state.restricted,state.pending,state.recording,true,false);
   menu.set_language(LanguagePreference::english,{true,true,!missing});
   if(state.failed)menu.failed();
   if(state.language_failed)menu.language_failed(LanguagePreference::english);
   menu.update({},0);for(unsigned i=0;i<row;++i){menu.update({.down=true},1+2.0*i);menu.update({},2+2.0*i);}
   Frame pixels(256*192,backdrop);menu.render(pixels,font,language);
   for(int y=0;y<192;++y)for(int x=0;x<256;++x) {
    const auto p=pixels[y*256+x];
    if(!menu.drawn(x,y,language))check(p==backdrop,"Options draws only inside its windows");
    else check(p==black||p==white||p==grey||p==yellow||p==dim,"Options uses black, white, grey, yellow and dark grey");
   }
   // The middle of the screen shows the game.
   for(int y=0;y<8*l::explanation.row;++y)for(int x=0;x<256;++x)
    if(y>=8*(l::list.row+l::list.height) || x>=8*(l::values_column+l::values_width))check(pixels[y*256+x]==backdrop,"the scene shows at the right and between the windows");
   // Labels and grey values in the list, the cursor in its left border.
   for(unsigned r=0;r<l::rows;++r) {
    check(shows(pixels,font,l::list.column+1,l::line_row(r),OptionsMenu::label(r,language),white),"list label");
    const auto values=menu.choices(r,language);
    if(!values.empty()) {
     const auto& value=values[menu.current(r)];
     check(shows(pixels,font,l::list.column+l::list.width-1-int(value.size()),l::line_row(r),value,grey),"the current value repeated in grey");
    }
   }
   Frame cursor(256*192,backdrop);menu_style::window(cursor,font,l::list,l::line_row(row));
   for(int y=8*l::list.row;y<8*(l::list.row+l::list.height);++y)for(int x=8*l::list.column;x<8*(l::list.column+1);++x)
    check(pixels[y*256+x]==cursor[y*256+x],"the cursor sits in the list's border on the selected row");
   // The values window: current yellow, the others grey (dark grey without their ROM).
   if(const auto window=menu.values_window(language)) {
    const auto values=menu.choices(row,language);
    for(std::size_t i=0;i<values.size();++i)
     check(shows(pixels,font,window->column+window->width-1-int(values[i].size()),window->row+1+l::line_step*int(i),values[i],
      i==menu.current(row) ? yellow : menu.available(row,i) ? grey : dim),"a choice in the values window");
   } else check(row==l::controls_row,"only Controls has no values window");
   const auto lines=menu.explanation_text(language);
   for(unsigned i=0;i<l::explanation_lines;++i)check(shows(pixels,font,l::explanation.column+1,l::explanation.row+1+l::line_step*int(i),lines[i],white),"explanation line");
   const bool quiet=!state.restricted && !state.pending && !state.failed && !state.language_failed;
   check(shows(pixels,font,l::explanation.column+1,l::state_row,menu.state(language),quiet ? grey : white),"the state line");
   for(const auto& line:lines)for(char c:line)check(font.supports(c),"explanation in the ROM font");
   for(char c:menu.state(language))check(font.supports(c),"state in the ROM font");
   for(unsigned r=0;r<l::rows;++r){for(char c:OptionsMenu::label(r,language))check(font.supports(c),"label in the ROM font");
    for(const auto& value:menu.choices(r,language))for(char c:value)check(font.supports(c),"value in the ROM font");}
   if(!missing && (std::string_view(state.name)=="normal" || row==0))
    write_frame(output,"options"+suffix+"-row"+std::to_string(row)+(std::string_view(state.name)=="normal" ? "" : "-"+std::string(state.name)),pixels);
   if(missing && row==l::language_row && std::string_view(state.name)=="normal")write_frame(output,"options"+suffix+"-language-no-br",pixels);
  }
 }
}
void rom(const char* path,const char* output){
 std::ifstream file(path,std::ios::binary);auto bytes=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());RomFont font(*bytes);
 for(char c='A';c<='Z';++c)check(font.supports(c),"missing uppercase menu glyph");
 for(char c='0';c<='9';++c)check(font.supports(c),"missing numeric menu glyph");
 check(font.supports(' ')&&font.supports('-'),"missing menu separator glyph");
 check(font.supports('%')&&font.supports('/')&&font.supports('(')&&font.supports(')'),"required host punctuation composed");
 check(!font.supports(':'),"unneeded punctuation stays unsupported");
 check(font.supports('.') && font.supports(','),"the cartridge's period and comma for the explanations");
 Machine original(bytes);original.memory().write(0xffff,16);auto& cpu=original.cpu();cpu.pc.uint16_value=0x03cd;cpu.hl.uint16_value=0xbad8;cpu.de.uint16_value=0x5800;cpu.sp.uint16_value=0xdf00;cpu.iff1=cpu.iff2=0;
 original.memory().write(0xdf00,0x80);original.memory().write(0xdf01,0x01);
 unsigned count=0;while(cpu.pc.uint16_value!=0x180 && count++<100000)original.step();check(cpu.pc.uint16_value==0x180,"original font loader returned");
 for(unsigned code=32;code<=95;++code) {
  if(code=='%'||code=='/'||code=='('||code==')')continue;
  const auto tile=bytes->at(0x7d17+(code-32)*2+1);
  for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){unsigned color=0;for(unsigned p=0;p<4;++p)color|=((original.vdp().vram()[tile*32+y*4+p]>>(7-x))&1)<<p;
   check(bool(font.glyphs()[code-32][y]&(1u<<(7-x)))==(color==2),"font differs from original loaded VRAM");}
 }
 // Composed punctuation uses only the documented original source strokes.
 const auto& g=font.glyphs();
 for(unsigned y=0;y<8;++y) {
  unsigned left=0,right=0,slash=0;const unsigned source_y=1+y*5/7;
  for(unsigned x=0;x<3;++x)if(g['C'-32][y]&(1u<<(7-x))){left|=1u<<(6-x);right|=1u<<(1+x);}
  for(unsigned x=6-source_y;x<=7-source_y;++x)if(g['Z'-32][source_y]&(1u<<(7-x)))slash|=1u<<(7-x);
  check(g['('-32][y]==left&&g[')'-32][y]==right&&g['/'-32][y]==slash,"punctuation preserves ROM source strokes");
 }
 // Independently execute the original second loader for window/cursor art.
 cpu.pc.uint16_value=0x03cd;cpu.hl.uint16_value=0xbd58;cpu.de.uint16_value=0x7e00;cpu.sp.uint16_value=0xdf00;
 count=0;while(cpu.pc.uint16_value!=0x180 && count++<100000)original.step();check(cpu.pc.uint16_value==0x180,"original window loader returned");
 std::array<std::uint32_t,256*192> pixels{};pixels.fill(0xff123456);font.window(pixels,24,40,208,120,72);
 for(unsigned tile_y=0;tile_y<15;++tile_y)for(unsigned tile_x=0;tile_x<26;++tile_x) {
  const bool edge_x=tile_x==0||tile_x==25,edge_y=tile_y==0||tile_y==14;
  if(!edge_x&&!edge_y)continue;
  const unsigned index=edge_y ? (edge_x ? 1 : 2) : tile_x==0&&tile_y==4 ? 0 : 3;
  for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
   unsigned color=0,px=tile_x==25 ? 7-x : x,py=tile_y==14 ? 7-y : y;
   for(unsigned plane=0;plane<4;++plane)color|=((original.vdp().vram()[(0x1f0+index)*32+py*4+plane]>>(7-px))&1)<<plane;
   check(pixels[(40+tile_y*8+y)*256+24+tile_x*8+x]==(color==2 ? 0xffffffff : 0xff000000),"window/cursor differs from original loaded VRAM");
  }
 }
 options_frames(font,output ? std::filesystem::path(output) : std::filesystem::path());
 Temporary temp;FrameDriver app(bytes,SaveStore(temp.path,identify_digest(sha1(*bytes))));
 check(!app.choose_options({50,400}),"real ROM Apply");app.machine().cpu().pc.uint16_value=0xaf;app.run({});check(app.effective_options()==game::Options{50,400},"real ROM dispatcher applies");
}
int crash_worker(const char* folder,std::string_view phase){
 const std::filesystem::path root(folder);const auto raw=root/save_name(RomVersion::us_rev_a);
 auto crash=[&](std::string_view point){if(phase==point)std::_Exit(23);};
 auto metadata=[&](const auto& path,const auto& text){
  const bool backup=path.extension()==".bak";
  crash(backup?"backup-before":"metadata-before");atomic_settings(path,text);
  crash(backup?"backup-after":"metadata-after");
 };
 auto writer=[&](const auto& path,const auto& bytes){crash("raw-before");atomic_save(path,bytes);crash("raw-after");};
 SaveStore store(root,RomVersion::us_rev_a,writer,metadata);auto bytes=store.load();
 check(store.load_preferences(bytes)==game::Options{50,400},"crash fixture preferences");
 Memory memory(program(),bytes);change(memory,45);check(!store.flush(memory),"crash worker flush");return 0;
}
}
int main(int argc,char**argv){try{
 if(argc==4 && std::string_view(argv[1])=="--crash-worker")return crash_worker(argv[2],argv[3]);
 if(argc>1){if(!std::filesystem::is_regular_file(argv[1]))return 77;rom(argv[1],argc>2?argv[2]:nullptr);}else{codec();persistence();failures();driver();encounter_choices();navigation();language_row();layout();}
 std::cout<<"Options tests passed.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
