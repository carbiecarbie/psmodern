// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_text.h"
#include "app/retro_font.h"
#include "game/routines.h"
#include "game/retro_events.h"
#include "probe.h"
#include <iostream>

using namespace psmodern;
using namespace psmodern::app::retro;
using probe::check;
namespace {
constexpr std::uint16_t stack=0xdfd0, stream=0xce20, caller=0x0b14;
struct Alternative : TextSource {
    Script script;
    mutable std::vector<TextKey> keys;
    mutable TextArguments last;
    Script resolve(TextKey key,const TextArguments& args) const override {
        keys.push_back(key);last=args;return script;
    }
};
struct Run {
    Machine machine;
    RoutineRegistry& routines;
    Alternative source;
    RetroTextSession session;
    WindowState windows;
    unsigned ticks=0, nmis=0, physical_edges=0, frames=0, overflow_pauses=0;
    std::vector<std::string> presented_lines;
    unsigned paused_frames=0;
    bool changed_page_during_pause=false, saw_native_advance=false, track_windows=false;
    explicit Run(const Machine& boot,RoutineRegistry& registry,bool cartridge_text=false)
        :machine(boot),routines(registry),session(cartridge_text ? nullptr : &source) {}
    void start(unsigned entry,std::initializer_list<std::uint8_t> message,bool append=false) {
        auto& c=machine.cpu();
        c.pc.uint16_value=entry; c.hl.uint16_value=entry==0x575a ? 0 : stream;
        c.sp.uint16_value=stack;
        machine.memory().write(stack,caller & 255); machine.memory().write(stack+1,caller>>8);
        machine.memory().write(0xc2d3,append ? 0xff : 0);
        machine.memory().write(0xc2c5,17);
        unsigned at=stream;
        for (const auto byte:message) machine.memory().write(static_cast<std::uint16_t>(at++),byte);
    }
    // The same physical state is supplied for all game frames of one host
    // update (one or four). Extra presses must not be needed by the printer.
    void play(unsigned speed,const std::vector<unsigned>& pressed,unsigned limit=420,bool pause=false,
              unsigned expected_sp=stack+2,std::array<unsigned,2> pause_hosts={6,12}) {
        auto previous=~std::uint64_t{0};
        TextPage previous_capture;
        bool held=false;
        Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
            if (pc==0x8043 && mapping.bank==12) ++ticks;
            if (mapping.kind!=Memory::Kind::rom || mapping.bank) return {};
            if (track_windows && !machine.depth() && game::retro_window_entry(pc)) windows.observe(machine,pc);
            const auto resume=session.observe(machine,pc,windows);
            if (resume==0x2d25 && (pc==0x3416 || pc==0x3330)) ++overflow_pauses;
            return resume;
        };
        Machine::BoundaryObserver trace=[&](const Machine::Boundary& b){ if (b.pc==0x0066) ++nmis; };
        machine.set_boundary_observer(&trace);
        while (machine.cpu().pc.uint16_value!=caller) {
            const auto frame=machine.cycles()/Machine::frame_cycles;
            if (frame!=previous) {
                if (frames%speed==0) {
                    session.presented(previous_capture);
                    if (!previous_capture.lines.empty()) presented_lines.push_back(previous_capture.lines.front());
                }
                previous_capture=session.view();
                saw_native_advance|=previous_capture.more;
                const auto host=frames/speed;
                FrameInput input;
                const bool down=std::find(pressed.begin(),pressed.end(),host)!=pressed.end();
                input.pads[0].two=down;
                if (down && !held) ++physical_edges;
                held=down;
                input.pause=pause && (host==pause_hosts[0] || host==pause_hosts[1]);
                const bool paused=machine.peek(0xc212)!=0;
                const auto revision=session.view().revision;
                machine.set_input(session.input(input,paused));
                if (paused) {
                    ++paused_frames;
                    changed_page_during_pause|=session.view().revision!=revision;
                }
                ++frames;previous=frame;
                machine.take_psg_writes();machine.take_samples();
                check(frames<limit,"the printer did not return to its actual caller, PC "+std::to_string(machine.cpu().pc.uint16_value));
            }
            machine.step(routines,hook);
        }
        machine.set_boundary_observer(nullptr);
        check(machine.cpu().sp.uint16_value==expected_sp,"printer returned with wrong SP");
    }
};
void start_long_battle_text(Run& run) {
    run.windows.open.set(static_cast<unsigned>(Window::enemy));
    run.start(0x31cf,{});
    run.machine.memory().write(0xc202,9);
    for (unsigned i=0;i<96;++i) run.machine.memory().write(stream+i,'W');
    run.machine.memory().write(stream+96,0x65);
}
void run(const probe::Rom& rom) {
    Machine boot(rom.image);
    // The two actions in the title prompt occupy separate printer rows.
    const Cartridge data(*rom.image,boot.memory().ram());
    const auto prompt=CartridgeText(data).resolve({TextEvent::fresh,data.byte(0x31d0),data.word(0x067a)},{});
    check(prompt.pages.size()==1,"the title choice should not require a page turn");
    const auto& labels=prompt.pages.front();
    const auto split=labels.find('\n');
    check(split==data.byte(0x31f9) && split+1<labels.size() &&
          labels.find('\n',split+1)==std::string::npos,"title choice labels must occupy two distinct rows");
    RoutineRegistry registry;
    game::register_routines(registry,rom.sha1,{});
    const auto observed=[&](unsigned bank,int pc) {
        const auto* routine=registry.find({static_cast<std::uint8_t>(bank),static_cast<std::uint16_t>(pc)});
        check(routine && routine->spec.observe_entry,"missing retro observation at "+std::to_string(pc));
    };
    for (const auto pc:game::retro_window_entries) observed(0,pc);
    for (const auto pc:{0x31d4,0x31fa,0x3218,0x332b,0x3330,0x337d,0x338e,0x33b9,0x33c8,0x33cf,0x33d4,
                        0x340f,0x3416,0x2d2a,0x2d47}) observed(0,pc);
    observed(1,0x45dd);observed(1,0x4676);
    // Initialize video interrupts, sound and RAM with the cartridge's boot.
    for (unsigned i=0;i<128;++i) { boot.run_frame({},registry);boot.take_samples();boot.take_psg_writes(); }
    for (unsigned selector_operand:{0x0685,0x06d1}) {
        Run title(boot,registry,true);
        title.start(0x31cf,{});
        title.machine.cpu().hl.uint16_value=data.word(0x067a);
        title.play(1,{});
        check(title.session.view().lines==wrap(labels,230),"title prompt must display both original rows");
        title.start(0x31cf,{},true);
        const auto address=data.word(selector_operand);
        title.machine.cpu().hl.uint16_value=address;
        title.play(1,{});
        const auto instruction=CartridgeText(data).resolve({TextEvent::append,data.byte(0x31d0),address},{});
        const auto expected=wrap(instruction.pages.front(),230);
        check(expected.size()==2 && title.session.view().lines==expected,
              "slot selection must replace the title choice with both current instruction rows");
    }
    Run progressive(boot,registry,true);
    progressive.start(0x31cf,{'A',' ','B',0x61,'C',0x5e,'D',0x65});
    progressive.play(1,{30,31,32,75,76,77});
    const auto& prefix=progressive.presented_lines;
    const auto first_a=std::find(prefix.begin(),prefix.end(),"A");
    const auto first_ab=std::find(prefix.begin(),prefix.end(),"A B");
    check(first_a!=prefix.end() && first_ab!=prefix.end() && first_a<first_ab &&
          progressive.saw_native_advance && progressive.physical_edges==2 && progressive.frames>75 &&
          progressive.session.view().lines==std::vector<std::string>{"A B","C17D"},
          "native glyphs, interstitial scroll, inserted digits and both button waits keep their order");

    // The map interaction dispatcher at 01:4752 calls a handler. This handler
    // jumps to the printer and returns through the map's dialogue close.
    Run npc(boot,registry,true);
    npc.track_windows=true;
    npc.start(0x31d4,{'A','B',0x61,'C','D',0x65});
    npc.machine.memory().write(0xffff,2);
    npc.machine.memory().write(0xc202,0x0d);
    npc.machine.memory().write(0xc29e,0x10);
    const auto write_word=[&](std::uint16_t address,std::uint16_t value) {
        npc.machine.memory().write(address,static_cast<std::uint8_t>(value));
        npc.machine.memory().write(address+1,static_cast<std::uint8_t>(value>>8));
    };
    write_word(stack,0x4758);
    write_word(stack+2,0x3bc5);
    write_word(stack+4,caller);
    npc.play(1,{30,31,32,75,76,77},420,false,stack+6);
    check(std::find(npc.presented_lines.begin(),npc.presented_lines.end(),"A")!=npc.presented_lines.end() &&
          npc.saw_native_advance && npc.overflow_pauses==0 && npc.physical_edges==2 && npc.frames>75 &&
          npc.session.events(TextEvent::fresh)==1 && npc.session.events(TextEvent::close)==1 &&
          npc.session.view().lines.empty(),
          "a map NPC reveals its glyphs progressively, waits for both presses and closes its dialogue");

    for (unsigned speed:{1u,4u}) {
        Run portrait(boot,registry,true);
        portrait.start(0x337d,{'A','B',0x61,'C','D',0x5f,'E','F',0x65});
        portrait.play(speed,{18,19,20,45,46,47});
        const auto& shown=portrait.presented_lines;
        const auto first=std::find(shown.begin(),shown.end(),"AB");
        const auto second=std::find(shown.begin(),shown.end(),"CD");
        const auto third=std::find(shown.begin(),shown.end(),"EF");
        check(first!=shown.end() && second!=shown.end() && third!=shown.end() &&
              first<second && second<third && portrait.physical_edges==2 && portrait.saw_native_advance &&
              portrait.session.events(TextEvent::alternate_layout)==1 && portrait.session.view().lines.empty(),
              "portrait clear preserves one progressive message and uses only its native presses");

        Run battle(boot,registry,true);
        start_long_battle_text(battle);
        std::vector<unsigned> held_press{104/speed};
        for (unsigned i=128/speed;i<211/speed;++i) held_press.push_back(i);
        held_press.push_back(240/speed);
        held_press.push_back(240/speed+1);
        battle.play(speed,held_press,300,true,stack+2,{96/speed,112/speed});
        const auto& last_page=battle.session.view().lines;
        check(battle.overflow_pauses==1 && battle.physical_edges==3 && battle.saw_native_advance &&
              battle.paused_frames>0 && !battle.changed_page_during_pause && battle.nmis==2 &&
              battle.frames>240 && !last_page.empty() && last_page.size()<=2 && !last_page.front().empty(),
              "paused battle overflow ignores confirm and requires a fresh native terminal press");

        Run direct(boot,registry);
        direct.source.script={{"One.","Two.","Three."},TextEnd::button,{}};
        direct.start(0x31cf,{'X',0x65});
        direct.play(speed,{3,4,5,8,9,10,17,18,19},360,true);
        check(direct.physical_edges==3 && direct.nmis>=1 && direct.ticks>0,"three physical presses, Pause and the sound IRQ");
        check(direct.source.keys==std::vector<TextKey>{{TextEvent::fresh,12,stream}},"direct stream dispatched once after mapping");
        check(direct.source.last.number==17 && direct.machine.peek(0xffff)==12 && direct.machine.peek(0xc2d3)!=0,
              "captured dynamic arguments, slot 2 and window ownership");
        // A close immediately follows the text, in the same host update.
        // The last automatic-end page must have made it to a paired picture.
        Run closing(boot,registry);
        closing.start(0x31cf,{'A',0x62});
        closing.machine.memory().write(stack,0x64);
        closing.machine.memory().write(stack+1,0x34);
        closing.machine.memory().write(stack+2,caller&255);
        closing.machine.memory().write(stack+3,caller>>8);
        closing.source.script={{"First","Last"},TextEnd::immediate,{}};
        closing.play(speed,{15,16},200,false,stack+4);
        check(std::find(closing.presented_lines.begin(),closing.presented_lines.end(),"Last")!=closing.presented_lines.end() &&
              closing.physical_edges==1 && closing.session.events(TextEvent::close)==1,
              "last page presented exactly before its actual close, with one advance");

        Run clearing(boot,registry);
        clearing.start(0x31cf,{'A',0x5f,'B',0x62});
        clearing.source.script={{"First\nSecond\nThird\nFourth\nLast","After clear"},TextEnd::immediate,{{0,stream+1,false}}};
        clearing.play(speed,{20,21},200);
        const auto last=std::find(clearing.presented_lines.begin(),clearing.presented_lines.end(),"Last");
        check(last!=clearing.presented_lines.end() &&
              std::find(last,clearing.presented_lines.end(),"After clear")!=clearing.presented_lines.end() &&
              clearing.physical_edges==1,"automatic clear waits for every visual page to reach the display");
        Run alternate(boot,registry);
        alternate.start(0x337d,{'A',0x61,'B',0x5f,'C',0x65});
        alternate.source.script={{"Once."},TextEnd::immediate,{}};
        alternate.play(speed,{});
        check(alternate.physical_edges==0 && alternate.source.keys.size()==1 &&
              alternate.source.keys[0].event==TextEvent::alternate_layout && alternate.machine.peek(0xffff)==2,
              "alternate page clears preserve the one event and leave bank 2");
        Run appended(boot,registry);
        appended.start(0x31cf,{'A',0x61,'B',0x62},true);
        appended.source.script={{"One."},TextEnd::immediate,{}};
        appended.play(speed,{});
        check(appended.source.keys==std::vector<TextKey>{{TextEvent::append,12,stream}},
              "an open dialogue yields one append and both printers share the actual return");
        Run indexed(boot,registry);
        indexed.start(0x575a,{});
        indexed.source.script={{"Choice."},TextEnd::immediate,{}};
        indexed.play(speed,{});
        check(indexed.source.keys.size()==1 && indexed.source.keys[0].address!=stream &&
              indexed.source.keys[0].event==TextEvent::fresh,"indexed loader reaches the common event once");
    }
    Run paused(boot,registry);
    paused.start(0x31cf,{'A',0x65});
    paused.machine.memory().write(0xc202,9); // an exploration mode that accepts console Pause
    paused.source.script={{"One","Two"},TextEnd::button,{}};
    paused.play(1,{8,16,22},120,true);
    check(paused.paused_frames>0 && !paused.changed_page_during_pause && paused.nmis==2 &&
          paused.physical_edges==3,"a press while paused leaves the open page intact");
    for (const bool native:{false,true}) {
        Run resetting(boot,registry,native);
        if (native) start_long_battle_text(resetting);
        else {
            resetting.start(0x31cf,{'A',0x65});
            resetting.source.script={{"Waiting"},TextEnd::button,{}};
        }
        bool saw_wait=false,saw_reset=false;
        for (unsigned i=0;i<180 && !saw_reset;++i) {
            FrameInput in;in.reset=saw_wait;
            resetting.machine.run_frame(resetting.session.input(in),registry,
                [&](std::uint16_t pc,Memory::Mapping at)->std::optional<std::uint16_t> {
                    if (at!=Memory::Mapping{Memory::Kind::rom,0}) return {};
                    saw_wait|=pc==0x2d2a;
                    saw_reset|=pc==0x0085;
                    return resetting.session.observe(resetting.machine,pc,resetting.windows);
                },{},[&]{resetting.session.presented(resetting.session.view());});
            resetting.machine.take_psg_writes();
            resetting.machine.take_samples();
        }
        check(saw_wait && saw_reset && resetting.session.view().lines.empty(),"physical Reset abandons a live page wait");
    }

    // All four script endings on each distinct native ending. The authored
    // timer starts at the same boundary even when the source cartridges
    // differ in whether the next prompt originally needed a button.
    for (const bool alt:{false,true}) {
        for (const unsigned native:{0x62u,0x63u,0x64u,0x65u}) {
            if (alt && native!=0x63 && native!=0x65) continue;
            std::array<unsigned,4> elapsed{};
            for (const auto ending:{TextEnd::immediate,TextEnd::button,TextEnd::interruptible,TextEnd::delay}) {
                Run r(boot,registry);
                r.start(alt ? 0x337d : 0x31cf,{'A',static_cast<std::uint8_t>(native)});
                r.source.script={{"Page."},ending,{}};
                r.play(1,ending==TextEnd::button ? std::vector<unsigned>{45,46,47} : std::vector<unsigned>{});
                elapsed[unsigned(ending)]=r.frames;
                check(r.physical_edges==(ending==TextEnd::button ? 1u : 0u),"cross-ending press count");
            }
            check(elapsed[1]>=45 && elapsed[1]<50,"exactly the authored final button");
            check(elapsed[2]>=elapsed[0]+29 && elapsed[2]<=elapsed[0]+31,"authored interruptible timer");
            check(elapsed[3]>=elapsed[0]+207 && elapsed[3]<=elapsed[0]+209,"authored fixed timer");
        }
    }
    // Cartridge page clears are automatic even in a stream that begins with
    // several clears. The middle clear keeps the first text on screen until
    // the printer reaches it. Its address is a cue, not a new text event.
    {
        auto m=boot;
        const std::array<unsigned char,8> msg{0x5f,0x5f,'A',0x5f,0x5f,'B',0x65,0x65};
        for(unsigned i=0;i<msg.size();++i)m.memory().write(stream+i,msg[i]);
        Cartridge cart(*rom.image,m.memory().ram());
        CartridgeText source(cart);
        for(auto event:{TextEvent::fresh,TextEvent::alternate_layout}) {
            const auto script=source.resolve({event,2,stream},{});
            check(script.pages==std::vector<std::string>{"A","B"} && script.breaks.size()==1 && !script.breaks[0].button &&
                  script.breaks[0].address==stream+3 && script.breaks[0].page==0,"no phantom button page for a cartridge clear");
        }
        if (rom.version==RomVersion::us_rev_a) {
            const auto ending=source.resolve({TextEvent::alternate_layout,2,cart.word(0x45d5)},{});
            check(!ending.pages.empty() && !ending.pages[0].empty(),"US finale starts on a visible message");
        }
        // Slot names are saved as two tile rows. A double quote has a blank
        // lower half in both cartridges, so inspecting only that tile loses it.
        auto sram=m.memory().cartridge_ram();
        sram[0x201]=0xff;
        for (unsigned j=0;j<5;++j) {
            const auto code=j==0 ? 'A' : j==1 ? '"' : ' ';
            sram[0x118+2*j]=m.peek(0x7d17+2*(code-32));
            sram[0x12a+2*j]=m.peek(0x7d18+2*(code-32));
        }
        Machine slots(rom.image,sram);
        WindowState windows;
        windows.observe(slots,0x39a5);
        check(windows.save_names[0]==std::string{'A','"'}, "saved name preserves the upper tile of a quotation mark");
        Alternative watcher;
        RetroTextSession session(&watcher);
        (void)session.observe(m,0x3464);
        check(watcher.keys==std::vector<TextKey>{{TextEvent::close,0,0}},"source sees a single close");
    }
    // Only interruptible waits accept a fresh button during their countdown.
    auto duration=[&](unsigned terminator,bool press) {
        Run r(boot,registry);
        r.start(0x31cf,{'A',static_cast<std::uint8_t>(terminator)});
        r.source.script={{"A"},terminator==0x63 ? TextEnd::interruptible : TextEnd::delay,{}};
        r.play(1,press ? std::vector<unsigned>{26,27} : std::vector<unsigned>{});
        return r.frames;
    };
    check(duration(0x63,true)<duration(0x63,false),"fresh press shortens the native interruptible countdown");
    check(duration(0x64,true)==duration(0x64,false),"fixed delay ignores buttons");
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"Expected ROM and region");
        auto rom=probe::load(argv[1],argv[2]); if (!rom) return 77;
        run(*rom); return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
