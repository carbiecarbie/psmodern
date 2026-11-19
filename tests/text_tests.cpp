// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/text.h"
#include "probe.h"
#include <algorithm>
#include <functional>
#include <iostream>

using namespace psmodern;
int main(int argc,char** argv) {
    // The original runs of this test (no registry) step the oracle's core.
    oracle::Scaffold scaffold(oracle::Scaffold::originals);
    try {
        RoutineRegistry registry;
        game::register_text(registry);
        const auto* entry=registry.find(game::text_indexed_entry);
        probe::check(entry && entry->spec.unit==game::text_indexed_entry && entry->spec.slot2_mapper,
                     "indexed message loader is registered with mapper access");
        auto check_group=[&](const auto& entries,RoutineEntry unit) {
            for (auto address:entries) {
                const auto* segment=registry.find(address);
                probe::check(segment && segment->spec.unit==unit && !segment->spec.exits.empty(),"text segment declares its unit and bounded exits");
            }
        };
        check_group(game::text_output_entries,game::text_output_unit);
        check_group(game::text_stream_entries,game::text_stream_unit);
        check_group(game::text_continuation_entries,game::text_stream_unit);
        check_group(game::text_number_entries,game::text_number_unit);
        check_group(game::text_alternate_entries,game::text_alternate_unit);
        // Exercise the actual identified-ROM registration path as well as the
        // text registrar: a stale or missing integration must fail without a ROM.
        for (auto digest:{"07fcf297be4f4c9d92cd3f119a7ac48467e06838",
                          "fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd"}) {
            RoutineRegistry integrated;
            probe::check(game::register_routines(integrated,digest),"identified image registration");
            probe::check(integrated.find(game::text_indexed_entry),"indexed loader missing from game registry");
            auto integrated_group=[&](const auto& entries) {
                for (auto address:entries)
                    probe::check(integrated.find(address),"text segment missing from game registry");
            };
            integrated_group(game::text_output_entries);
            integrated_group(game::text_stream_entries);
            integrated_group(game::text_dictionary_entries);
            integrated_group(game::text_continuation_entries);
            integrated_group(game::text_number_entries);
            integrated_group(game::text_alternate_entries);
        }
        if (argc==1) return 0;
        probe::check(argc==3,"expected ROM and region");
        auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        probe::Harness harness(std::move(*rom));
        for (unsigned index=0;index<256;++index) {
            probe::Setup setup;
            setup.seed=index;
            setup.r=static_cast<std::uint8_t>(index);
            setup.banks[2]=static_cast<std::uint8_t>(index%32);
            auto machine=harness.prepare(game::text_indexed_entry,setup);
            machine.cpu().hl.uint16_value=static_cast<std::uint16_t>(index*2);
            const auto result=harness.run_cpp(machine,game::text_indexed_entry);
            probe::check(result.cycles==75,"indexed message loader timing");
            probe::check(machine.cpu().pc.uint16_value==0x31d4,"indexed message loader transfers to stream");
        }
        for (auto entry:game::text_output_entries) for (unsigned seed=0;seed<64;++seed) {
            probe::Setup setup; setup.seed=seed; setup.r=static_cast<std::uint8_t>(seed);
            auto machine=harness.prepare(entry,setup);
            machine.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xc800+seed);
            machine.memory().write(machine.cpu().hl.uint16_value,static_cast<std::uint8_t>(seed*5));
            machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(seed%3);
            harness.run_cpp(machine,entry);
        }
        for (auto entry:game::text_stream_entries) for (unsigned value=0;value<256;++value) {
            probe::Setup setup; setup.seed=value;
            auto machine=harness.prepare(entry,setup);
            machine.cpu().hl.uint16_value=0xc800;
            machine.memory().write(0xc800,static_cast<std::uint8_t>(value));
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
            harness.run_cpp(machine,entry);
        }
        for (auto entry:game::text_dictionary_entries) for (unsigned value=0;value<128;++value) {
            probe::Setup setup; setup.seed=value;
            auto machine=harness.prepare(entry,setup);
            machine.cpu().hl.uint16_value=0xc800;
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value*2);
            machine.memory().write(0xc800,static_cast<std::uint8_t>(value*2));
            machine.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(value%3);
            harness.run_cpp(machine,entry);
        }
        for (auto entry:game::text_continuation_entries) for (unsigned value=0;value<32;++value) {
            probe::Setup setup; setup.seed=value;
            auto machine=harness.prepare(entry,setup);
            machine.memory().write(0xc2d3,static_cast<std::uint8_t>(value));
            harness.run_cpp(machine,entry);
        }
        for (auto entry:game::text_number_entries) for (unsigned value=0;value<64;++value) {
            probe::Setup setup; setup.seed=value;
            auto machine=harness.prepare(entry,setup);
            machine.cpu().hl.uint16_value=static_cast<std::uint16_t>(value*997);
            machine.memory().write(0xc2c5,static_cast<std::uint8_t>(value*97));
            machine.memory().write(0xc2c6,static_cast<std::uint8_t>(value*19));
            harness.run_cpp(machine,entry);
        }
        for (auto entry:game::text_alternate_entries) for (unsigned value=0;value<128;++value) {
            probe::Setup setup; setup.seed=value;
            auto machine=harness.prepare(entry,setup);
            machine.cpu().hl.uint16_value=0xc800;
            machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value*2);
            machine.memory().write(0xc800,static_cast<std::uint8_t>(value*2));
            harness.run_cpp(machine,entry);
        }
        // Interrupt sweeps (milestone 14): the text bodies have their own port bus (the LiveText of text.cpp), so every
        // instruction family is swept: loads and stores, the port writes of the tile printers, the divisions of the
        // number printer, the pushes and pops, and every final. An IRQ and an NMI at every cycle of each segment of
        // more than one instruction (every n-th of the longest), from the states the probes above use.
        auto sweep_group=[&](const auto& entries,const std::function<void(Machine&,unsigned)>& state) {
            for (auto entry:entries) {
                if (harness.routine(entry).spec.single_instruction) continue;
                for (unsigned value:{3u,34u,77u,120u}) {
                    probe::Setup setup; setup.seed=value; setup.banks[2]=static_cast<std::uint8_t>(value%32);
                    auto pilot=harness.prepare(entry,setup);
                    state(pilot,value);
                    const auto length=harness.routine(entry).bound(pilot);
                    const unsigned stride=static_cast<unsigned>(std::max<std::uint64_t>(1,length/300));
                    probe::sweep(harness,entry,setup,[&](Machine& m) { state(m,value); },stride);
                }
            }
        };
        sweep_group(game::text_output_entries,[](Machine& m,unsigned value) {
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(0xc800+value);
            m.memory().write(m.cpu().hl.uint16_value,static_cast<std::uint8_t>(value*5));
            m.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(value%3);
        });
        sweep_group(game::text_stream_entries,[](Machine& m,unsigned value) {
            m.cpu().hl.uint16_value=0xc800; m.memory().write(0xc800,static_cast<std::uint8_t>(value));
            m.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value);
        });
        sweep_group(game::text_dictionary_entries,[](Machine& m,unsigned value) {
            m.cpu().hl.uint16_value=0xc800; m.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value*2);
            m.memory().write(0xc800,static_cast<std::uint8_t>(value*2)); m.cpu().bc.uint8_values.at_1=static_cast<std::uint8_t>(value%3);
        });
        sweep_group(game::text_continuation_entries,[](Machine& m,unsigned value) { m.memory().write(0xc2d3,static_cast<std::uint8_t>(value)); });
        sweep_group(game::text_number_entries,[](Machine& m,unsigned value) {
            m.cpu().hl.uint16_value=static_cast<std::uint16_t>(value*997);
            m.memory().write(0xc2c5,static_cast<std::uint8_t>(value*97)); m.memory().write(0xc2c6,static_cast<std::uint8_t>(value*19));
        });
        sweep_group(game::text_alternate_entries,[](Machine& m,unsigned value) {
            m.cpu().hl.uint16_value=0xc800; m.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(value*2);
            m.memory().write(0xc800,static_cast<std::uint8_t>(value*2));
        });
        for (auto entry:{RoutineEntry{0,0x33d7},RoutineEntry{0,0x33da},RoutineEntry{0,0x3308}}) {
            probe::Setup setup;
            auto near_interrupt=harness.prepare(entry,setup);
            near_interrupt.schedule_nmi(near_interrupt.cycles()+3);
            harness.run_fallback(near_interrupt,entry,RoutineFallback::nmi);
            auto near_frame=harness.prepare(entry,setup);
            near_frame.advance(Machine::frame_cycles-near_frame.cycles()-1);
            harness.run_fallback(near_frame,entry,RoutineFallback::frame);
            setup.sp=0xfffc;
            auto unsafe_stack=harness.prepare(entry,setup);
            harness.run_fallback(unsafe_stack,entry,RoutineFallback::unmapped_stack);
        }
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
