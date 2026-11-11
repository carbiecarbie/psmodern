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
 for(const auto& bad:{std::string("-4294967196"),std::string("+100"),std::string("4294967396"),std::string("100x")}) {
  auto invalid=text;invalid.replace(invalid.find("100"),3,bad);refuses([&]{parse_settings(invalid);});
 }
 for(std::size_t length=0;length<text.size()-1;++length)refuses([&]{parse_settings(text.substr(0,length));});
 image.committed.digest="x";refuses([&]{write_settings(image);});
}
void persistence(){
 Temporary t;Memory::CartridgeRam initial{};initial[8]=19;const auto name=t.path/save_name(RomVersion::us_rev_a);atomic_save(name,initial);
 std::filesystem::path sidecar;
 {
  SaveStore store(t.path,RomVersion::us_rev_a);sidecar=store.settings_path();
  check(store.load_preferences(store.load())==game::Options{},"missing metadata original");
  check(!store.choose({50,400},initial),"settings-only apply");check(read(name).size()==8192 && store.load()==initial && store.writes()==0,"settings-only raw unchanged");
  check(!store.choose({0,200},initial),"second settings-only apply");
 }
 {
  SaveStore store(t.path,RomVersion::us_rev_a);check(store.load_preferences(store.load())==game::Options{0,200},"settings restart");
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
 }
 for(bool routines:{true,false}) {
  game::OptionOverrides overrides;overrides.experience=100;
  FrameDriver driver(program(),SaveStore(t.path,RomVersion::us_rev_a),routines,overrides,!routines);
  check(driver.configured_options()==game::Options{50,400},"stored preferences retained");
  check(driver.effective_options()==(routines?game::Options{50,100}:game::Options{}),"per-field explicit100 or restricted originals");
  change(driver.machine().memory(),routines?1:2);check(!driver.flush(),"session SRAM flush");
  if(!routines)check(driver.choose_options({0,400}).has_value(),"restricted changes refused");
 }
 {
  FrameDriver recording(program(),SaveStore(t.path,RomVersion::us_rev_a),true,{},true);
  check(recording.effective_options()==game::Options{} && recording.configured_options()==game::Options{50,400},"recording forced original");
  change(recording.machine().memory(),3);check(!recording.flush(),"recording SRAM write");
 }
 {FrameDriver driver(program(),SaveStore(t.path,RomVersion::us_rev_a));check(driver.effective_options()==game::Options{50,400},"CLI and forced sessions never stored overrides");}
}
void navigation(){
 OptionsMenu menu;menu.open({},false,false);
 check(menu.update({.accept=true},0)==MenuAction::none,"opening held buttons consumed");menu.update({},1);
 menu.update({.right=true},2);check(menu.draft().encounter_percent==200,"right cycles encounter choices");
 menu.update({.right=true},2.1);check(menu.draft().encounter_percent==200,"held input delay");
 menu.update({.right=true},2.4);check(menu.draft().encounter_percent==0,"held repeat bounded");menu.update({},3);
 menu.update({.down=true},4);menu.update({},5);menu.update({.right=true},6);check(menu.draft().experience_percent==200,"XP choices");
 check(menu.update({.accept=true},6.1)==MenuAction::apply,"confirm persists from experience row");
 menu.update({},6.2);menu.update({.down=true},6.3);check(menu.selected()==0,"two settings wrap without action or preset rows");
 menu.update({},6.4);check(menu.update({.accept=true},6.5)==MenuAction::apply,"confirm persists from encounter row");
 menu.update({},6.6);check(menu.update({.toggle=true},6.7)==MenuAction::close,"options toggle cancels draft");
 menu.open({},true,false);menu.update({},7);menu.update({.right=true},8);check(menu.draft()==game::Options{},"restricted menu read only");
 check(menu.update({.accept=true},8.1)==MenuAction::close,"read-only confirmation closes without applying");
 check(menu.update({.cancel=true},9)==MenuAction::close,"restricted menu escapable");
 InputRelease release;release.consume();FrameInput held;held.pause=true;held.reset=true;held.pads[0].one=true;
 auto consumed=release.filter(held);check(!consumed.pause&&!consumed.reset&&!consumed.pads[0].one,"menu pause reset actions consumed");
 release.filter({});check(release.filter(held).pause,"fresh game input restored");
}
void rom(const char* path,const char* output){
 std::ifstream file(path,std::ios::binary);auto bytes=std::make_shared<Memory::Rom>(std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>());RomFont font(*bytes);
 for(char c='A';c<='Z';++c)check(font.supports(c),"missing uppercase menu glyph");
 for(char c='0';c<='9';++c)check(font.supports(c),"missing numeric menu glyph");
 check(font.supports(' ')&&font.supports('-'),"missing menu separator glyph");
 check(font.supports('%')&&font.supports('/')&&font.supports('(')&&font.supports(')'),"required host punctuation composed");
 check(!font.supports(':'),"unneeded punctuation stays unsupported");
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
 OptionsMenu menu;
 for(bool restricted:{false,true})for(bool pending:{false,true})for(auto choices:{game::Options{},game::Options{50,400},game::Options{0,200},game::Options{200,100}}){menu.open(choices,restricted,pending);menu.render(pixels,font);}
 pixels.fill(0xff123456);menu.open({50,400},false,false);menu.render(pixels,font);
 for(unsigned y=0;y<192;++y)for(unsigned x=0;x<256;++x) {
  const auto pixel=pixels[y*256+x];
  if(x<24||x>=232||y<40||y>=160)check(pixel==0xff123456,"menu preserves current backdrop outside its window");
  else check(pixel==0xff000000||pixel==0xffffffff,"menu retains original black and white palette");
 }
 menu.failed();menu.render(pixels,font);menu.open({50,400},false,false);menu.render(pixels,font);
 if(output){std::ofstream f(output,std::ios::binary);f<<"P6\n256 192\n255\n";for(auto pixel:pixels)for(unsigned shift:{16,8,0})f.put((pixel>>shift)&255);}
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
 if(argc>1){if(!std::filesystem::is_regular_file(argv[1]))return 77;rom(argv[1],argc>2?argv[2]:nullptr);}else{codec();persistence();failures();driver();navigation();}
 std::cout<<"Options tests passed.\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
