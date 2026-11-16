// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/scenes.h"
#include "probe.h"
#include <iostream>
#include <algorithm>
#include <tuple>
#include <set>
#include <vector>
using namespace psmodern;
namespace {
struct Stopped {};
using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t,Memory::Kind>;
void persistence_observers(probe::Harness& h) {
    for(bool resume:{false,true})
    for(bool include_prefix:{false,true})
    for(unsigned slot=1;slot<=(resume ? 5u : 1u);++slot)
    for(unsigned control:{0u,8u,12u,0x88u})
    for(unsigned remaining:{1u,100u,1000u,30000u,59000u}) {
        if(!resume && include_prefix) continue;
        const RoutineEntry entry{0,static_cast<std::uint16_t>(resume ? (include_prefix ? 0x069D : 0x06A2) : 0x05E8)};
        const std::uint16_t stop=resume ? 0x06B9 : 0x05F4;
        const std::uint16_t watched=resume ? 0x06B4 : 0x05F4;
        probe::Setup setup; setup.cartridge_ram=true;
        auto actual=h.prepare(entry,setup);
        actual.memory().write(0xFFFC,static_cast<std::uint8_t>(control));
        actual.memory().write(0xC2C5,static_cast<std::uint8_t>(slot));
        actual.memory().write(0xFFFC,8);
        for(unsigned i=0;i<1024;++i)
            actual.memory().write(static_cast<std::uint16_t>(0x8000+slot*1024+i),
                static_cast<std::uint8_t>(i*17+slot));
        actual.memory().write(0xFFFC,static_cast<std::uint8_t>(control));
        const auto phase=actual.cycles()%Machine::frame_cycles;
        actual.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
        const auto instruction_start=actual.cycles();
        Machine native=actual;
        std::vector<Write> expected_writes,actual_writes;
        std::vector<std::pair<std::uint64_t,Memory::Kind>> expected_hooks,actual_hooks;
        auto run=[&](Machine& m,RoutineRegistry* registry,std::vector<Write>& writes,auto& hooks) {
            Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) {
                writes.emplace_back(address,value,m.cycles(),m.memory().mapping(0x8000).kind);
            };
            Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
                if(map.kind==Memory::Kind::rom && map.bank==0 && pc==watched)
                    hooks.emplace_back(m.cycles(),m.memory().mapping(0x8000).kind);
                if(map.kind==Memory::Kind::rom && map.bank==0 && pc==stop) throw Stopped{};
                return {};
            };
            try {
                for(unsigned frames=0;frames<4;++frames) m.run_frame({},registry,hook,observer);
            } catch(const Stopped&) { return; }
            throw std::runtime_error("persistence observer chain did not reach its completion boundary: resume="+std::to_string(resume)+" remaining="+std::to_string(remaining)+" native="+std::to_string(registry==nullptr)+" pc="+std::to_string(m.cpu().pc.uint16_value));
        };
        RoutineRegistry chain;
        for(const auto& [at,routine]:h.registry().entries()) {
            if(resume && at==RoutineEntry{0,stop}) continue;
            if(routine.spec.exits.empty()) chain.add(at,routine.bound,routine.body);
            else chain.add(at,routine.spec,routine.bound,routine.body);
        }
        chain.verifier=h.registry().verifier;
        run(native,nullptr,expected_writes,expected_hooks);
        run(actual,&chain,actual_writes,actual_hooks);
        probe::check(native.equivalent(actual),"initialization/Continue observer chain state");
        probe::check(expected_writes==actual_writes,"initialization/Continue write order, cycle, payload and mapping");
        probe::check(expected_hooks==actual_hooks && actual_hooks.size()==1,"initialization/Continue completion observed once at original cycle/mapping");
        if(resume && include_prefix) {
            const auto store=std::find_if(actual_writes.begin(),actual_writes.end(),[](const auto& w) {return std::get<0>(w)==0xFFFC;});
            probe::check(store!=actual_writes.end() && std::get<1>(*store)==8 && std::get<2>(*store)==instruction_start+7 &&
                std::get<3>(*store)==Memory::Kind::cartridge_ram,"Continue enables SRAM at the original store callback cycle S+7");
            probe::check(actual.peek(0xC300)==slot,"Continue prefix copies the selected SRAM slot even when ROM was initially visible");
        }
        if(resume) probe::check(actual.memory().mapping(0x8000).kind==Memory::Kind::rom,"Continue restores slot 2 ROM before completion");
    }
}
void cartridge_initialization_observers(probe::Harness& h) {
    for(unsigned control:{8u,12u})
    for(unsigned remaining:{1u,100u,1000u,30000u,59000u})
    for(bool power_loss:{false,true}) {
        probe::Setup setup; setup.cartridge_ram=true;
        auto actual=h.prepare({0,0x0847},setup);
        actual.memory().write(0xFFFC,static_cast<std::uint8_t>(control));
        for(unsigned i=0;i<8192;++i) actual.memory().write(static_cast<std::uint16_t>(0x8000+i),static_cast<std::uint8_t>(i+37));
        const auto phase=actual.cycles()%Machine::frame_cycles;
        actual.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
        Machine native=actual;
        std::vector<Write> expected,observed;
        auto run=[&](Machine& m,RoutineRegistry* registry,std::vector<Write>& writes) {
            Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) {
                writes.emplace_back(address,value,m.cycles(),m.memory().mapping(0x8000).kind);
            };
            Machine::Hook hook=[](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
                if(map.kind==Memory::Kind::rom && map.bank==0 && pc==0x086E) throw Stopped{};
                return {};
            };
            try {
                if(power_loss) {
                    m.run_frame({},registry,hook,observer);
                    // Re-enter initialization with the partially written SRAM,
                    // as after losing power before the header is restored.
                    m.cpu().pc.uint16_value=0x0847;
                }
                for(unsigned frame=0;frame<6;++frame) m.run_frame({},registry,hook,observer);
            } catch(const Stopped&) { return; }
            throw std::runtime_error("cartridge initialization did not finish");
        };
        run(native,nullptr,expected);
        run(actual,&h.registry(),observed);
        probe::check(actual.equivalent(native),"cartridge initialization/re-entry whole state");
        probe::check(expected==observed,"cartridge initialization/re-entry ordered byte writes, observer cycles and mapping");
        const auto first=std::find_if(observed.begin(),observed.end(),[](const Write& w) { return std::get<0>(w)==0x8000; });
        probe::check(first!=observed.end() && std::get<1>(*first)==0 && std::get<3>(*first)==Memory::Kind::cartridge_ram,"first initialization write seeds mapped SRAM with zero");
        probe::check(actual.memory().mapping(0x8000).kind==Memory::Kind::rom,"initialization restores ROM mapping");
        for(unsigned i=8188;i<8192;++i)
            probe::check(actual.memory().cartridge_ram()[i]==static_cast<std::uint8_t>(i+37),"initialization preserves the final four SRAM bytes");
    }
    auto disabled=h.prepare({0,0x0847},{});
    h.run_fallback(disabled,{0,0x0847},RoutineFallback::frame);
}

void scene_copy_native_tails(probe::Harness& h) {
    unsigned cases=0;
    for (const auto address:{0x61B9,0x61C2})
    for (const unsigned remaining:{0u,1u,2u,3u,8u,16u,17u})
    for (unsigned left=1;left<=70;++left)
    for (unsigned event=0;event<4;++event) {
        const RoutineEntry entry{1,static_cast<std::uint16_t>(address)};
        auto machine=h.prepare(entry,{});
        machine.cpu().bc.uint16_value=static_cast<std::uint16_t>(remaining);
        machine.cpu().hl.uint16_value=0xC400;
        machine.cpu().de.uint16_value=0xC401;
        for (unsigned i=0;i<32;++i) machine.memory().write(0xC400+i,static_cast<std::uint8_t>(i*19+7));
        const auto phase=machine.cycles()%Machine::frame_cycles;
        machine.advance((Machine::frame_cycles-left+Machine::frame_cycles-phase)%Machine::frame_cycles);
        const unsigned total=remaining ? remaining : 65536;
        const auto duration=[&](unsigned n) {return 21*n-(n==total ? 5u : 0u);};
        unsigned frame_count=0;
        for (unsigned n=1;n<=16 && n<=total;++n)
            if (duration(n)<=left || (n>=2 && 21*(n-1)<left)) frame_count=n;
        const unsigned nominal=duration(frame_count ? frame_count : 1);
        const unsigned deadline=event==1 ? nominal-1 : event==2 ? nominal : nominal+1;
        if (event) machine.schedule_nmi(machine.cycles()+deadline);
        unsigned expected=0;
        for (unsigned n=1;n<=16 && n<=total;++n)
            if ((duration(n)<=left || (n>=2 && 21*(n-1)<left)) && (!event || duration(n)<=deadline)) expected=n;
        Machine original=machine;
        for (unsigned n=0;n<(expected ? expected : 1);++n) original.step();
        Machine unchecked=machine;
        RoutineRegistry plain;
        game::register_block_transfer(plain,entry,true);
        if (expected) {
            const auto start=machine.cycles();
            const auto result=h.run_cpp(machine,entry);
            probe::check(result.cycles==duration(expected),"scene copy chooses independent complete-transfer count");
            probe::check(machine.cpu().bc.uint16_value==static_cast<std::uint16_t>(remaining-expected),"scene copy retains exact BC including zero wrap");
            if (result.cycles>left) probe::check(expected>=2 && 21*(expected-1)<left && start+21*(expected-1)<start+left,"scene copy crosses frame only in a real native tail after C++ work");
        } else {
            Machine native=machine;
            native.step();
            h.run_fallback(machine,entry,event && deadline<duration(1) ? RoutineFallback::nmi : RoutineFallback::frame);
            probe::check(machine.equivalent(native),"scene copy refusal preserves the original instruction and late NMI");
        }
        unchecked.step(plain);
        probe::check(unchecked.equivalent(machine),"scene copy admission and full state are identical with verifier disabled");
        const auto* unverified=plain.find(entry);
        probe::check(unverified && unverified->counters.cpp_calls==(expected ? 1u : 0u) && !unverified->counters.forced_calls,"scene copy does not force admission with verifier disabled");
        machine.step();
        original.step();
        probe::check(machine.equivalent(original),"scene copy preserves late NMI acceptance on the following instruction");
        ++cases;
    }
    std::cout<<"Scene native copy tails, final/repeat/zero BC and NMI boundaries: "<<cases<<"\n";
}

void scene_copy_irq_windows(probe::Harness& h) {
    unsigned cases=0;
    for (const auto address:{0x61B9,0x61C2})
    for (const unsigned remaining:{0u,1u,2u,3u,8u,16u,17u})
    for (unsigned distance=1;distance<=70;++distance) {
        const RoutineEntry entry{1,static_cast<std::uint16_t>(address)};
        probe::Setup setup;
        setup.iff1=true;
        setup.iff2=true;
        auto machine=h.prepare(entry,setup);
        machine.cpu().bc.uint16_value=static_cast<std::uint16_t>(remaining);
        machine.cpu().hl.uint16_value=0xC400;
        machine.cpu().de.uint16_value=0xC401;
        const auto irq=machine.next_irq();
        probe::check(irq && *irq>machine.cycles()+distance,"scene copy has an upcoming real VDP IRQ");
        machine.advance(*irq-machine.cycles()-distance);
        const unsigned total=remaining ? remaining : 65536;
        unsigned expected=0;
        for (unsigned n=1;n<=16 && n<=total;++n)
            if (21*n-(n==total ? 5u : 0u)<=distance) expected=n;
        Machine native=machine;
        for (unsigned n=0;n<(expected ? expected : 1);++n) native.step();
        if (expected) {
            const auto result=h.run_cpp(machine,entry);
            probe::check(result.cycles==21*expected-(expected==total ? 5u : 0u),"scene copy IRQ window retains complete bound");
        } else h.run_fallback(machine,entry,RoutineFallback::irq);
        probe::check(machine.equivalent(native),"scene copy matches at IRQ boundary including late sampling");
        machine.step();
        native.step();
        probe::check(machine.equivalent(native),"scene copy preserves IRQ acceptance on the following instruction");
        ++cases;
    }
    for (const auto address:{0x61B9,0x61C2})
    for (const std::uint16_t destination:{0xFFFB,0xFFFC,0xFFFF}) {
        const RoutineEntry entry{1,static_cast<std::uint16_t>(address)};
        auto machine=h.prepare(entry,{});
        machine.cpu().bc.uint16_value=3;
        machine.cpu().hl.uint16_value=0xC400;
        machine.cpu().de.uint16_value=destination;
        h.run_fallback(machine,entry,RoutineFallback::mapper_write);
    }
    std::cout<<"Scene copy real VDP IRQ windows and continuation states: "<<cases<<"\n";
}

void visual_scene_copy_windows(probe::Harness& h) {
    RoutineRegistry chain;
    for (const auto& [entry,routine]:h.registry().entries()) {
        if (entry==RoutineEntry{1,0x61D9}) continue;
        if (routine.spec.exits.empty()) chain.add(entry,routine.bound,routine.body);
        else chain.add(entry,routine.spec,routine.bound,routine.body);
    }
    chain.verifier=h.registry().verifier;
    unsigned cases=0;
    for (unsigned event=0;event<3;++event)
    for (unsigned remaining=1;remaining<=128;++remaining) {
        probe::Setup setup;
        setup.iff1=event==2;
        setup.iff2=event==2;
        auto actual=h.prepare({1,0x617D},setup);
        actual.cpu().af.uint8_values.at_1=1;
        const auto phase=actual.cycles()%Machine::frame_cycles;
        actual.advance((Machine::frame_cycles-remaining+Machine::frame_cycles-phase)%Machine::frame_cycles);
        if (event==1) actual.schedule_nmi(actual.cycles()+remaining/2);
        if (event==2) probe::check(actual.vdp().irq(),"scene window IRQ variant has a pending VDP interrupt");
        Machine native=actual;
        Machine verified=actual;
        std::vector<Write> expected,observed,verified_writes;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        FrameInput input;
        input.pause=(remaining&1)!=0;
        input.pads[0].one=(remaining&2)!=0;
        auto run=[&](Machine& machine,RoutineRegistry* registry,std::vector<Write>& writes) {
            Machine::WriteObserver observer=[&](std::uint16_t address,std::uint8_t value) {
                writes.emplace_back(address,value,machine.cycles(),machine.memory().mapping(address).kind);
            };
            Machine::Hook hook=[](std::uint16_t pc,Memory::Mapping map)->std::optional<std::uint16_t> {
                if (map.kind==Memory::Kind::rom && map.bank==1 && pc==0x61D9) throw Stopped{};
                return {};
            };
            try {
                for (unsigned frame=0;frame<8;++frame) machine.run_frame(input,registry,hook,observer);
            } catch (const Stopped&) { return; }
            throw std::runtime_error("scene copies did not reach their continuation");
        };
        run(native,nullptr,expected);
        run(verified,&chain,verified_writes);
        // A nested per-call trace owns the ports during verification. A
        // separate identical run lets the outer trace capture the whole chain.
        chain.verifier=nullptr;
        run(actual,&chain,observed);
        chain.verifier=h.registry().verifier;
        probe::check(verified.equivalent(native) && verified_writes==expected,
            "verified scene chains preserve full state and every callback");
        probe::check(actual.equivalent(native),"scene adaptive copies preserve whole state across frame windows");
        probe::check(expected==observed,"scene adaptive copies preserve every seed/copy write and callback cycle");
        if (expected_ports.accesses()!=actual_ports.accesses()) {
            std::string detail="scene chain ports event="+std::to_string(event)+" left="+std::to_string(remaining)+" expected="+std::to_string(expected_ports.accesses().size())+" actual="+std::to_string(actual_ports.accesses().size());
            for (unsigned i=0;i<std::min(expected_ports.accesses().size(),actual_ports.accesses().size());++i) {
                const auto& a=expected_ports.accesses()[i];
                const auto& b=actual_ports.accesses()[i];
                if (a==b) continue;
                detail+=" first="+std::to_string(i)+" expected cycle/port/value="+std::to_string(a.cycle)+"/"+std::to_string(a.port)+"/"+std::to_string(a.value)+" actual="+std::to_string(b.cycle)+"/"+std::to_string(b.port)+"/"+std::to_string(b.value);
                break;
            }
            throw std::runtime_error(detail);
        }
        ++cases;
    }
    std::cout<<"Visual scene frame windows and ordered seed/copy writes: "<<cases<<"\n";
}

void scene_copy_windows(probe::Harness& harness,const probe::Rom& rom) {
    auto image=std::make_shared<Memory::Rom>(*rom.image);
    const std::array<std::uint8_t,7> irq{0xF5,0xDB,0xBF,0xF1,0xFB,0xED,0x4D};
    std::copy(irq.begin(),irq.end(),image->begin()+0x0038);
    image->at(0x0066)=0xED;image->at(0x0067)=0x45;
    struct Case {RoutineEntry entry;std::uint16_t final;unsigned kind;};
    const std::array cases{Case{{0,0x075A},0x0793,0},Case{{0,0x08CE},0x08E4,0},
        Case{{0,0x08FF},0x090C,0},Case{{0,0x3D4E},0x0180,1},
        Case{{1,0x4048},0x4063,0},Case{{1,0x42D8},0x42FE,1},
        Case{{1,0x45F6},0x4611,0},Case{{0,0x3D36},0x0180,2},Case{{0,0x3D36},0x3D44,3},
        Case{{0,0x3D47},0x0180,5},Case{{0,0x3D47},0x0180,4}};
    std::set<RoutineEntry> seen_seeds;
    for(const auto& test:cases)
    for(unsigned event=0;event<3;++event)
    for(unsigned distance:{1u,64u,1000u}) {
        Machine actual(image);
        (void)actual.read_port(0xBF,actual.cycles());actual.advance(1);
        actual.memory().write(0xFFFC,0);actual.memory().write(0xFFFE,1);actual.memory().write(0xFFFF,2);
        for(unsigned a=0xC000;a<0xE000;++a) actual.memory().write(static_cast<std::uint16_t>(a),static_cast<std::uint8_t>(a*17+test.entry.address));
        actual.cpu().pc.uint16_value=test.entry.address;actual.cpu().sp.uint16_value=0xDF00;
        actual.cpu().hl.uint16_value=0xC300;actual.cpu().de.uint16_value=0xC400;actual.cpu().bc.uint16_value=1;
        actual.cpu().af.uint16_value=0xA5D7;actual.cpu().iff1=actual.cpu().iff2=0;
        actual.memory().write(0xDF00,0x80);actual.memory().write(0xDF01,1);
        if(test.entry.address==0x4048) actual.cpu().hl.uint16_value=static_cast<std::uint16_t>(rom.byte(1,0x4046)|(rom.byte(1,0x4047)<<8));
        if(test.entry.address==0x08FF) actual.cpu().hl.uint16_value=static_cast<std::uint16_t>(rom.byte(0,0x08FD)|(rom.byte(0,0x08FE)<<8));
        if(test.kind==2) actual.cpu().af.uint8_values.at_1=0xFF;
        if(test.kind==3) actual.cpu().af.uint8_values.at_1=0;
        if(test.entry.address==0x3D47) {
            const auto field=static_cast<std::uint16_t>(rom.byte(0,0x3D48)|(rom.byte(0,0x3D49)<<8));
            actual.memory().write(field,static_cast<std::uint8_t>(test.kind==5 ? 0xFF : 0));
        }
        const auto stop_address=test.kind==1 || test.kind==2 || test.kind==4 || test.kind==5 ? test.final :
            static_cast<std::uint16_t>(rom.byte(test.entry.bank,static_cast<std::uint16_t>(test.final+1))|
                (rom.byte(test.entry.bank,static_cast<std::uint16_t>(test.final+2))<<8));
        const RoutineEntry stop{stop_address<0x4000 ? 0u : 1u,stop_address};
        if(event==0) {
            const auto phase=actual.cycles()%Machine::frame_cycles;
            actual.advance((Machine::frame_cycles-distance+Machine::frame_cycles-phase)%Machine::frame_cycles);
        } else if(event==1) {
            actual.write_port(0xBF,0x20,actual.cycles());actual.write_port(0xBF,0x81,actual.cycles());
            const auto next=actual.vdp().next_irq();
            probe::check(next && *next>=actual.cycles()+distance,"scene copy chain has a future IRQ");
            actual.advance(*next-actual.cycles()-distance);actual.cpu().iff1=actual.cpu().iff2=1;
        } else actual.schedule_nmi(actual.cycles()+distance);
        Machine native=actual;
        RoutineRegistry declared,chain;game::register_scenes(declared);
        for(const auto& [entry,routine]:declared.entries()) if(entry!=stop)
            chain.add(entry,routine.spec,routine.bound,routine.body);
        using Event=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t,Memory::Mapping,Memory::Mapping>;
        std::vector<Event> expected,observed;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        auto run=[&](Machine& machine,RoutineRegistry* registry,std::vector<Event>& writes) {
            bool image_arm=false;
            std::uint16_t pc=0;unsigned bank=0;std::uint64_t instruction_start=0,instruction_fetches=0;
            Machine::WriteObserver observer=[&](std::uint16_t a,std::uint8_t value) {
                writes.emplace_back(a,value,machine.cycles(),machine.memory().mapping(a),machine.memory().mapping(0x8000));
                const RoutineEntry at{bank,pc};
                if(registry && machine.opcode_fetches()==instruction_fetches+1 && std::find(game::scenes::startup_native_seeds.begin(),game::scenes::startup_native_seeds.end(),at)!=game::scenes::startup_native_seeds.end() && a==machine.cpu().hl.uint16_value) {
                    probe::check(machine.cycles()==instruction_start,"scene seed callback pc="+std::to_string(pc)+" start="+std::to_string(instruction_start)+" actual="+std::to_string(machine.cycles())+" event="+std::to_string(event)+" distance="+std::to_string(distance));
                    const auto wanted=machine.peek(pc)==0x75 ? static_cast<std::uint8_t>(machine.cpu().hl.uint16_value) : machine.peek(static_cast<std::uint16_t>(pc+1));
                    probe::check(value==wanted,"isolated scene fill seed keeps its authenticated payload");
                    seen_seeds.insert(at);
                }
            };
            Machine::Hook hook=[&](std::uint16_t address,Memory::Mapping mapping)->std::optional<std::uint16_t> {
                pc=address;bank=mapping.bank;instruction_start=machine.cycles();instruction_fetches=machine.opcode_fetches();
                if(!registry && test.entry.address==0x3D47 && mapping==Memory::Mapping{Memory::Kind::rom,0} && address==(test.kind==5 ? 0x3D4E : 0x3D64)) image_arm=true;
                if(mapping.kind==Memory::Kind::rom && RoutineEntry{mapping.bank,address}==stop) {
                    if(!registry && test.entry.address==0x3D47) probe::check(image_arm,"real interaction-type field selects the expected 3D47 arm");
                    throw Stopped{};
                }
                return {};
            };
            try {for(unsigned frame=0;frame<16;++frame) {machine.run_frame({},registry,hook,observer);machine.take_samples();machine.take_psg_writes();}}
            catch(const Stopped&) {return;}
            throw std::runtime_error("scene copy chain did not reach its original continuation: "+std::to_string(test.entry.address));
        };
        run(native,nullptr,expected);run(actual,&chain,observed);
        probe::check(actual.equivalent(native),"split scene copy preserves CPU/WZ/R, buffers, mapping, peripherals and events");
        probe::check(expected==observed,"split scene copy preserves every write address, byte, order, cycle and mapping");
        probe::check(expected_ports.accesses()==actual_ports.accesses(),"split scene copy preserves every port operation and cycle");
    }
    for(const auto seed:game::scenes::startup_native_seeds) {
        probe::check(harness.registry().find(seed)==nullptr,"declared scene fill seed remains one isolated native instruction");
        probe::check(seen_seeds.contains(seed),"every declared scene fill seed has a payload/cycle observer proof");
    }
}
void hapsby_lookup_routes(probe::Harness& h,const probe::Rom& rom) {
    const auto item=rom.byte(1,0x5547);
    const auto inventory=static_cast<std::uint16_t>(rom.byte(0,0x282F)|(rom.byte(0,0x2830)<<8));
    const auto slots=rom.byte(0,0x2832);
    const auto current_item=static_cast<std::uint16_t>(rom.byte(1,0x5549)|(rom.byte(1,0x554A)<<8));
    const auto dialogue=static_cast<std::uint16_t>(rom.byte(1,0x5541)|(rom.byte(1,0x5542)<<8));
    const auto cleanup=static_cast<std::uint16_t>(rom.byte(1,0x5551)|(rom.byte(1,0x5552)<<8));
    for(unsigned scenario=0;scenario<3;++scenario) {
        auto actual=h.prepare({1,0x5546},{});
        for(unsigned slot=0;slot<slots;++slot) actual.memory().write(static_cast<std::uint16_t>(inventory+slot),0);
        if(scenario) actual.memory().write(static_cast<std::uint16_t>(inventory+(scenario==1 ? 0 : slots-1)),item);
        Machine native=actual;
        RoutineRegistry chain;
        for(const auto& [entry,routine]:h.registry().entries()) if(entry.address!=dialogue && entry.address!=cleanup) {
            if(routine.spec.exits.empty()) chain.add(entry,routine.bound,routine.body);
            else chain.add(entry,routine.spec,routine.bound,routine.body);
        }
        using Write=std::tuple<std::uint16_t,std::uint8_t,std::uint64_t>;
        std::vector<Write> expected,observed;
        Machine::PortTrace expected_ports(native),actual_ports(actual);
        std::uint16_t reached=0;
        auto run=[&](Machine& m,RoutineRegistry* registry,std::vector<Write>& writes) {
            Machine::WriteObserver observer=[&](std::uint16_t a,std::uint8_t v) {writes.emplace_back(a,v,m.cycles());};
            Machine::Hook hook=[&](std::uint16_t pc,Memory::Mapping)->std::optional<std::uint16_t> {
                if(pc==dialogue || pc==cleanup) {reached=pc;throw Stopped{};}
                return {};
            };
            try {m.run_frame({},registry,hook,observer);} catch(const Stopped&) {return;}
            throw std::runtime_error("Hapsby lookup did not reach its selected continuation");
        };
        run(native,nullptr,expected);const auto expected_target=reached;
        run(actual,&chain,observed);
        probe::check(actual.equivalent(native) && expected==observed,"Hapsby lookup routes preserve full state and all write callbacks");
        probe::check(expected_ports.accesses()==actual_ports.accesses(),"Hapsby lookup routes preserve every port operation");
        probe::check(reached==expected_target && reached==(scenario ? dialogue : cleanup),"Hapsby chooses already-present dialogue or new-item continuation");
        probe::check(actual.peek(current_item)==item,"Hapsby leaves the cartridge item request for the grant continuation");
        for(unsigned slot=0;slot<slots;++slot)
            probe::check(actual.peek(static_cast<std::uint16_t>(inventory+slot))==(scenario && slot==(scenario==1 ? 0u : slots-1) ? item : 0),"lookup does not alter inventory before the selected continuation");
    }
}
void regional_name_literal(probe::Harness& h,const probe::Rom& rom,std::uint8_t expected) {
    probe::check(rom.byte(1,0x4181)==expected,"identified release has its original name-layout literal");
    auto machine=h.prepare({1,0x4166},{});
    h.run_cpp(machine,{1,0x4166});
    for(const auto [first,count]:{std::pair{0xD2CCu,11u},std::pair{0xD34Cu,11u},std::pair{0xD3CCu,10u}})
        for(unsigned cell=0;cell<count;++cell)
            probe::check(machine.peek(static_cast<std::uint16_t>(first+4*cell))==expected,
                "4180 writes the regional literal into every incrementing-run destination cell");
}

}
int main(int argc,char** argv) {
 try {
  probe::check(argc==3,"Expected ROM and region");
  const auto rom=probe::load(argv[1],argv[2]); if(!rom) return 77;
  probe::Harness h(*rom); RoutineRegistry catalog; game::register_scenes(catalog);
  scene_copy_native_tails(h);
  scene_copy_irq_windows(h);
  visual_scene_copy_windows(h);
  scene_copy_windows(h,*rom);
  hapsby_lookup_routes(h,*rom);
  regional_name_literal(h,*rom,std::string_view(argv[2])=="us" ? 0xC0 : 0x20);
  unsigned calls=0;
  probe::check(catalog.entries().size()==game::scenes::segments.size(),"scene catalog size");
  for(const auto& [entry,unit]:game::scenes::segments) {
   probe::check(catalog.find(entry) && catalog.find(entry)->spec.unit==unit,"declared scene segment registration");
   for(unsigned seed=0;seed<32;++seed) {
    probe::Setup setup; setup.seed=seed; setup.r=static_cast<std::uint8_t>(seed*9);
    auto machine=h.prepare(entry,setup);
    if(entry.address==0x05F2 || entry.address==0x06B2 || entry.address==0x3F47 || entry.address==0x3F53 || entry.address==0x0851 || (entry.bank==1 && (entry.address==0x618F || entry.address==0x619C || entry.address==0x61B9 || entry.address==0x61C2 || entry.address==0x61D7))) {
     machine.cpu().hl.uint16_value=0xC300;
     machine.cpu().de.uint16_value=0xC301;
     machine.cpu().bc.uint16_value=seed+1;
    }
    if(entry.bank==1 && entry.address==0x413A) {
     machine.cpu().hl.uint16_value=0xD146;
     machine.cpu().de.uint16_value=0xC721;
    }
    if(entry.bank==1 && (entry.address==0x41C8 || entry.address==0x41D5 || entry.address==0x41B2 || entry.address==0x41AB)) {
     machine.cpu().hl.uint16_value=0xD146;
     machine.cpu().de.uint16_value=0xF303;
     machine.cpu().bc.uint16_value=0x1805;
    }
    if(entry.address==0x3FF2 || entry.address==0x4006) {
     machine.cpu().ix_iy[0].uint16_value=0xC784;
     machine.cpu().ix_iy[1].uint16_value=0xC784;
     machine.cpu().bc.uint16_value=0xC808;
     machine.cpu().de.uint16_value=2;
     machine.memory().write(0xC786,0x0A);
     machine.memory().write(0xC787,0xD3);
    }
    if(entry.address==0x641B) {
     machine.cpu().hl.uint16_value=0x6506;
     machine.cpu().bc.uint16_value=static_cast<std::uint16_t>(((seed%4)+1)<<8);
     machine.memory().write(0xFFFF,16);
    }
    if(entry.address==0x0847) machine.memory().write(0xFFFC,8);
    if(entry.address==0x07E2) machine.cpu().bc.uint16_value=0x1000;
    if(entry.bank==1 && entry.address==0x4107) machine.cpu().hl.uint16_value=0xC781;
    if(entry.address==0x615A || entry.address==0x615E) {
     machine.cpu().hl.uint16_value=0xC100;
     machine.cpu().de.uint16_value=0xD100;
     machine.cpu().bc.uint16_value=0x08FF;
     for(unsigned i=0;i<32;++i) machine.memory().write(static_cast<std::uint16_t>(0xC100+i),static_cast<std::uint8_t>((i+seed)%3 ? i+1 : 0));
    }
    if(entry.address==0x6427 || entry.address==0x6438 || entry.address==0x643D) {
     const auto sp=machine.cpu().sp.uint16_value;
     machine.memory().write(sp,entry.address==0x6427 ? 0 : 8);
     machine.memory().write(static_cast<std::uint16_t>(sp+1),entry.address==0x6427 ? 4 : 0x65);
     machine.memory().write(static_cast<std::uint16_t>(sp+2),0);
     machine.memory().write(static_cast<std::uint16_t>(sp+3),4);
     machine.cpu().hl.uint16_value=entry.address==0x6427 ? 0x6507 : 0x8000;
     machine.cpu().bc.uint16_value=static_cast<std::uint16_t>(((seed%128)+1)*256+0xBE);
     machine.memory().write(0xFFFF,16);
    }
    for(const auto& load:game::scenes::ei_delay_scaffolding) if(load.suffix==entry) {
     const auto word=static_cast<std::uint16_t>(rom->byte(load.instruction.bank,static_cast<std::uint16_t>(load.instruction.address+1))|
         (rom->byte(load.instruction.bank,static_cast<std::uint16_t>(load.instruction.address+2))<<8));
     if(load.opcode==0x21) machine.cpu().hl.uint16_value=word;
     if(load.opcode==0x11) machine.cpu().de.uint16_value=word;
     if(load.opcode==0x3A) machine.cpu().af.uint8_values.at_1=machine.peek(word);
     if(load.opcode==0x3E) machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(word);
    }
    h.run_cpp(machine,entry); ++calls;
   }
  }
  for(unsigned slot=0;slot<=5;++slot) {
   probe::Setup setup; setup.cartridge_ram=true;
   auto machine=h.prepare({0,0x0639},setup);
   for(unsigned i=1;i<=5;++i) machine.memory().write(static_cast<std::uint16_t>(0x8200+i),i==slot ? 1 : 0);
   h.run_cpp(machine,{0,0x0639}); ++calls;
   probe::check(machine.cpu().pc.uint16_value==(slot ? 0x0652 : 0x064A),"slot scan chooses correct path");
  }
  for(unsigned seed=0;seed<512;++seed) {
   for(const auto entry:{RoutineEntry{0,0x0998},RoutineEntry{0,0x09DB},RoutineEntry{0,0x09E9}}) {
    probe::Setup setup; setup.seed=seed; auto machine=h.prepare(entry,setup);
    std::mt19937_64 random(seed);
    for(auto address:{0xC304,0xC305,0xC306,0xC307,0xC2F2,0xC2F3,0xC2E9,0xC2F0})
     machine.memory().write(static_cast<std::uint16_t>(address),static_cast<std::uint8_t>(random()));
    machine.memory().write(0xC2E9,seed%4); h.run_cpp(machine,entry); ++calls;
   }
  }
  for(unsigned position=0;position<256;++position) {
   probe::Setup setup; setup.seed=position;
   auto machine=h.prepare({1,0x4117},setup);
   machine.memory().write(0xC781,static_cast<std::uint8_t>(position));
   h.run_cpp(machine,{1,0x4117}); ++calls;
   auto repeat=h.prepare({1,0x402C},setup);
   repeat.memory().write(0xC789,static_cast<std::uint8_t>(position));
   h.run_cpp(repeat,{1,0x402C}); ++calls;
  }
  for(const auto& [entry,unit]:game::scenes::segments) {
   probe::Setup setup;
   auto machine=h.prepare(entry,setup);
   if(entry.address==0x05F2 || entry.address==0x06B2 || entry.address==0x3F47 || entry.address==0x3F53 || entry.address==0x0851 || (entry.bank==1 && (entry.address==0x618F || entry.address==0x619C || entry.address==0x61B9 || entry.address==0x61C2 || entry.address==0x61D7))) {
    machine.cpu().hl.uint16_value=0xC300;
    machine.cpu().de.uint16_value=0xC301;
    machine.cpu().bc.uint16_value=1;
   }
   const auto phase=machine.cycles()%Machine::frame_cycles;
   machine.advance(Machine::frame_cycles-phase-1);
   Machine native=machine;
   native.step();
   h.run_fallback(machine,entry,RoutineFallback::frame);
   probe::check(machine.equivalent(native),"scene frame-edge fallback preserves original partial state");
  }
  for(unsigned buttons=0;buttons<256;++buttons) {
   for(unsigned held=0;held<16;++held) {
    auto machine=h.prepare({0,0x3F73},{});
    machine.memory().write(0xC205,static_cast<std::uint8_t>(buttons));
    machine.memory().write(0xC204,static_cast<std::uint8_t>(held));
    h.run_cpp(machine,{0,0x3F73}); ++calls;
   }
  }
  for(unsigned slot=1;slot<=5;++slot) {
   probe::Setup setup; setup.cartridge_ram=true;
   auto machine=h.prepare({0,0x06FA},setup);
   machine.memory().write(0xC2C5,static_cast<std::uint8_t>(slot));
   machine.memory().write(static_cast<std::uint16_t>(0x8200+slot),1);
   h.run_cpp(machine,{0,0x06FA}); ++calls;
   machine.memory().write(0xFFFC,8);
   probe::check(machine.peek(static_cast<std::uint16_t>(0x8200+slot))==0,"erase clears selected occupied flag");
  }
  for(unsigned counter=0;counter<256;++counter) {
   for(const auto entry:{RoutineEntry{1,0x64A3},RoutineEntry{1,0x64CF}}) {
    auto machine=h.prepare(entry,{});
    machine.memory().write(0xC202,13);
    machine.memory().write(0xC213,static_cast<std::uint8_t>(counter));
    machine.memory().write(0xC214,static_cast<std::uint8_t>(counter));
    h.run_cpp(machine,entry); ++calls;
   }
  }
  for(unsigned counter=0;counter<256;++counter) {
   for(const auto entry:{RoutineEntry{1,0x63D2},RoutineEntry{1,0x63F9},RoutineEntry{1,0x6442}}) {
    auto machine=h.prepare(entry,{});
    machine.memory().write(0xC2BC,static_cast<std::uint8_t>(counter));
    machine.memory().write(0xC2BD,static_cast<std::uint8_t>(counter));
    h.run_cpp(machine,entry); ++calls;
   }
  }
  persistence_observers(h);
  cartridge_initialization_observers(h);
  for(unsigned mismatch=0;mismatch<=32;++mismatch) {
   probe::Setup setup; setup.cartridge_ram=true;
   auto machine=h.prepare({0,0x07E2},setup);
   machine.cpu().bc.uint16_value=0x1000;
   for(unsigned i=0;i<32;++i) machine.memory().write(static_cast<std::uint16_t>(0x8001+i),rom->byte(0,static_cast<std::uint16_t>(0x0808+i)));
   if(mismatch<32) machine.memory().write(static_cast<std::uint16_t>(0x8001+mismatch),static_cast<std::uint8_t>(machine.peek(static_cast<std::uint16_t>(0x8001+mismatch))^0x80));
   h.run_cpp(machine,{0,0x07E2}); ++calls;
  }
  {
   probe::Setup setup; setup.banks={0,2,3};
   auto machine=h.prepare({0,0x3FF2},setup);
   machine.cpu().ix_iy[1].uint16_value=0xC784;
   machine.memory().write(0xC784,0x20);
   h.run_fallback(machine,{0,0x3FF2},RoutineFallback::frame);
   probe::check(machine.peek(0xC784)==0x20,"cross-bank cursor admission refuses a remapped second fixed bank before mutation");
  }
  for(unsigned pose=0;pose<9;++pose) {
   auto machine=h.prepare({1,0x46D5},{});
   const auto sp=machine.cpu().sp.uint16_value;
   machine.memory().write(sp,static_cast<std::uint8_t>(pose*23));
   machine.memory().write(static_cast<std::uint16_t>(sp+1),static_cast<std::uint8_t>(pose));
   h.run_cpp(machine,{1,0x46D5}); ++calls;
  }
  for(unsigned members=0;members<4;++members) {
   auto machine=h.prepare({1,0x4464},{});
   const auto first=static_cast<std::uint16_t>(rom->byte(1,0x446C)|(rom->byte(1,0x446D)<<8));
   const auto second=static_cast<std::uint16_t>(rom->byte(1,0x4475)|(rom->byte(1,0x4476)<<8));
   machine.memory().write(first,static_cast<std::uint8_t>(members&1));
   machine.memory().write(second,static_cast<std::uint8_t>((members>>1)&1));
   h.run_cpp(machine,{1,0x4464}); ++calls;
  }
  {
   const auto kind=static_cast<std::uint16_t>(rom->byte(0,0x3CAE)|(rom->byte(0,0x3CAF)<<8));
   for(unsigned environment:{0u,1u,7u,8u,10u,2u,255u})
   for(unsigned requested:{0u,1u,5u,9u,31u,32u,255u}) {
    auto machine=h.prepare({0,0x3C5A},{});
    machine.memory().write(0xC308,static_cast<std::uint8_t>(environment));
    machine.memory().write(kind,static_cast<std::uint8_t>(requested));
    h.run_cpp(machine,{0,0x3C5A}); ++calls;
    unsigned expected=requested;
    if(environment==0 && requested==5) expected=4;
    if((environment==1 || environment==7) && requested==1) expected=5;
    if(environment==8) expected=6;
    if(environment==10 && requested==9) expected=8;
    if(expected==0) expected=1;
    probe::check(machine.peek(kind)==expected,"environment selects original interaction kind");
   }
   for(unsigned vehicle:{0u,1u,0x80u,0x81u}) {
    auto machine=h.prepare({0,0x3BC5},{});
    machine.memory().write(0xC202,13);
    machine.memory().write(0xC2E9,static_cast<std::uint8_t>(vehicle));
    h.run_cpp(machine,{0,0x3BC5}); ++calls;
    probe::check(machine.peek(0xC202)==((vehicle&0x80) ? 4 : vehicle ? 14 : 8),"scene cleanup chooses the original return mode");
   }
  }
  for(unsigned pattern=0;pattern<3;++pattern)
  for(unsigned width:{1u,3u,31u})
  for(unsigned height:{1u,3u}) {
   auto actual=h.prepare({1,0x615A},{});
   actual.cpu().hl.uint16_value=0xC100;
   actual.cpu().de.uint16_value=0xD100;
   actual.cpu().bc.uint16_value=static_cast<std::uint16_t>((width<<8)|height);
   for(unsigned i=0;i<1024;++i) actual.memory().write(static_cast<std::uint16_t>(0xC100+i),static_cast<std::uint8_t>(pattern==0 || (pattern==2 && (i/2)%2==0) ? 0 : (i%127)+1));
   for(unsigned i=0;i<256;++i) actual.memory().write(static_cast<std::uint16_t>(0xD100+i),0x55);
   Machine native=actual;
   auto run=[](Machine& m,RoutineRegistry* registry) {
    for(unsigned step=0;step<20000;++step) {
     m.step({},registry);
     if(m.cpu().pc.uint16_value==0x0180 && m.cpu().sp.uint16_value==0xDF02) return;
    }
    throw std::runtime_error("visual overlay did not return");
   };
   run(native,nullptr);
   run(actual,&h.registry());
   probe::check(actual.equivalent(native),"transparent multi-row overlay complete chain");
   for(unsigned row=0;row<height;++row)
   for(unsigned column=0;column<width;++column) {
    const auto source=static_cast<std::uint16_t>(0xC100+2*(row*width+column));
    const auto target=static_cast<std::uint16_t>(0xD100+row*64+column*2);
    probe::check(actual.peek(target)==(actual.peek(source)==0 ? 0x55 : actual.peek(source)),"overlay preserves transparent cells and copies opaque cells");
   }
  }
  for(unsigned key:{0x41u,0x5Cu,0x5Du,0x5Eu})
  for(unsigned position:{0x24u,0x25u}) {
   auto machine=h.prepare({0,0x3ED9},{});
   machine.memory().write(0xC788,static_cast<std::uint8_t>(key));
   machine.memory().write(0xC781,static_cast<std::uint8_t>(position));
   h.run_cpp(machine,{0,0x3ED9}); ++calls;
  }
  for(unsigned health:{0u,1u,0x80u,0xFFu}) {
   auto machine=h.prepare({1,0x5657},{});
   machine.memory().write(0xC400,static_cast<std::uint8_t>(health));
   h.run_cpp(machine,{1,0x5657}); ++calls;
  }
  for(bool answer:{false,true}) {
   auto machine=h.prepare({1,0x568A},{});
   machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(answer ? 0x40 : 0);
   h.run_cpp(machine,{1,0x568A}); ++calls;
  }
  const auto money_address=static_cast<std::uint16_t>(rom->byte(1,0x5128)|(rom->byte(1,0x5129)<<8));
  const auto cake_cost=static_cast<std::uint16_t>(rom->byte(1,0x5125)|(rom->byte(1,0x5126)<<8));
  for(unsigned balance:{unsigned(cake_cost-1),unsigned(cake_cost),unsigned(cake_cost+1),65535u}) {
   auto machine=h.prepare({1,0x5124},{});
   machine.memory().write(money_address,static_cast<std::uint8_t>(balance));
   machine.memory().write(static_cast<std::uint16_t>(money_address+1),static_cast<std::uint8_t>(balance>>8));
   h.run_cpp(machine,{1,0x5124}); ++calls;
  }
  const auto capacity_address=static_cast<std::uint16_t>(rom->byte(1,0x5136)|(rom->byte(1,0x5137)<<8));
  const unsigned capacity=rom->byte(1,0x5139);
  for(unsigned used:{0u,capacity-1,capacity,capacity+1}) {
   auto machine=h.prepare({1,0x5135},{});
   machine.memory().write(capacity_address,static_cast<std::uint8_t>(used));
   h.run_cpp(machine,{1,0x5135}); ++calls;
  }
  const auto mode_address=static_cast<std::uint16_t>(rom->byte(1,0x54F3)|(rom->byte(1,0x54F4)<<8));
  for(unsigned mode:{0u,2u,3u,255u}) {
   auto machine=h.prepare({1,0x54F2},{});
   machine.memory().write(mode_address,static_cast<std::uint8_t>(mode));
   h.run_cpp(machine,{1,0x54F2}); ++calls;
  }
  const auto progress_address=static_cast<std::uint16_t>(rom->byte(1,0x51C3)|(rom->byte(1,0x51C4)<<8));
  for(unsigned state:{0u,1u,2u,255u}) {
   auto machine=h.prepare({1,0x51C2},{});
   machine.memory().write(progress_address,static_cast<std::uint8_t>(state));
   h.run_cpp(machine,{1,0x51C2}); ++calls;
  }
  const auto hp_address=static_cast<std::uint16_t>(rom->byte(1,0x54AE)|(rom->byte(1,0x54AF)<<8));
  for(unsigned hp:{0u,1u,128u,255u}) {
   auto machine=h.prepare({1,0x549F},{});
   machine.memory().write(hp_address,static_cast<std::uint8_t>(hp));
   h.run_cpp(machine,{1,0x549F}); ++calls;
  }
  const auto status_address=static_cast<std::uint16_t>(rom->byte(1,0x56A3)|(rom->byte(1,0x56A4)<<8));
  for(unsigned mask=0;mask<16;++mask) {
   auto machine=h.prepare({1,0x56A2},{});
   machine.memory().write(status_address,static_cast<std::uint8_t>(mask));
   h.run_cpp(machine,{1,0x56A2}); ++calls;
  }
  for(unsigned remaining:{1u,2u,4u}) for(bool alive:{false,true}) {
   auto machine=h.prepare({1,0x56C3},{});
   machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(remaining);
   machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(alive ? 0 : 0x40);
   h.run_cpp(machine,{1,0x56C3}); ++calls;
  }
  const auto command_address=static_cast<std::uint16_t>(rom->byte(1,0x574F)|(rom->byte(1,0x5750)<<8));
  const auto command_table=static_cast<std::uint16_t>(rom->byte(1,0x574A)|(rom->byte(1,0x574B)<<8));
  for(unsigned current=0;current<3;++current) for(unsigned chosen=0;chosen<3;++chosen)
  for(bool confirm:{false,true}) if(current!=chosen) {
   auto machine=h.prepare({1,0x573F},{});
   const auto sp=machine.cpu().sp.uint16_value;
   machine.memory().write(sp,static_cast<std::uint8_t>(current));
   machine.memory().write(static_cast<std::uint16_t>(sp+1),static_cast<std::uint8_t>(chosen));
   machine.memory().write(static_cast<std::uint16_t>(sp+2),0x80);
   machine.memory().write(static_cast<std::uint16_t>(sp+3),1);
   machine.memory().write(command_address,0x5A);
   machine.cpu().af.uint8_values.at_0=static_cast<std::uint8_t>(confirm ? 0x40 : 0);
   h.run_cpp(machine,{1,0x573F}); ++calls;
   probe::check(machine.peek(command_address)==(confirm ? rom->byte(1,static_cast<std::uint16_t>(command_table+current*3+chosen)) : 0x5A),
       "destination confirmation reads the cartridge request table; refusal preserves the pending request");
   if(confirm) probe::check(machine.cpu().pc.uint16_value==0x0180,"confirmed destination restores the caller stack");
  }
  std::cout<<calls<<" verified scene segments\n";
 } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
