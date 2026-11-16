// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/dungeon_tables.h"
#include "game/dungeon_view.h"
#include "probe.h"
#include <iostream>

using namespace psmodern;
namespace {
void pitfall_copy(probe::Harness& h) {
    for (auto pc:game::pitfall_video_copies) for (bool iff:{true,false}) {
        auto m=h.prepare({1,pc},{.iff1=iff,.iff2=iff,.seed=pc});
        m.cpu().de.uint16_value=0x4200;
        m.cpu().bc.uint16_value=7;
        m.cpu().hl.uint16_value=0xC600;
        for (unsigned i=0;i<7;++i) m.memory().write(0xC600+i,i+3);
        auto original=m;
        h.run_cpp(m,{1,pc});
        probe::check(!m.cpu().iff1,"pitfall video copy must mask interrupts");
        h.run_cpp(m,{1,static_cast<std::uint16_t>(pc+1)});
        for (unsigned i=0;m.cpu().pc.uint16_value!=pc+4;++i) {
            probe::check(i<100,"pitfall video call did not return");
            m.step(h.registry());
        }
        h.run_cpp(m,{1,static_cast<std::uint16_t>(pc+4)});
        m.step(h.registry());
        while (original.cycles()<m.cycles()) original.step();
        probe::check(m.equivalent(original) && m.cpu().iff1,"pitfall interrupt window or video bytes differ");
    }
}
void check_dungeon_escape(probe::Harness& h,const game::DungeonTables& tables) {
    namespace v=game::dungeon_view;
    for (auto pcs:{std::span<const std::uint16_t>(v::spell_cell_entries),
                  std::span<const std::uint16_t>(v::escape_choices),
                  std::span<const std::uint16_t>(v::retreat_entries)}) {
        for (auto pc:pcs) for (unsigned i=0;i<8;++i) {
            auto m=h.prepare({1,pc},{.seed=i});
            m.cpu().af.uint16_value=((i+7)<<8)|(i&1 ? 0x40 : 0);
            h.run_cpp(m,{1,pc});
        }
    }
    auto spell_cell=[&](unsigned floor,unsigned cell,unsigned type,bool accepted) {
        auto m=h.prepare({1,0x684F},{.seed=floor+cell});
        m.cpu().hl.uint16_value=0xC000+cell;
        m.cpu().af.uint8_values.at_1=type;
        m.memory().write(game::dungeon::floor,floor);
        auto native=m;
        h.run_cpp(m,{1,0x684F});
        for (unsigned i=0;m.cpu().pc.uint16_value==0x6863;++i) {
            probe::check(i<=tables.objects().size(),"dungeon object scan did not terminate");
            h.run_cpp(m,{1,0x6863});
        }
        while (native.cycles()<m.cycles()) native.step();
        probe::check(m.equivalent(native) && m.cpu().af.uint8_values.at_1==(accepted ? 255 : 0),
                     "spell eligibility differs for dungeon object record");
    };
    for (const auto& rec:tables.objects()) spell_cell(rec.floor,rec.cell,8,false);
    for (unsigned floor=0;floor<game::dungeon::floor_count;++floor)
        spell_cell(floor,0,8,!tables.object_at(floor,0));
    for (unsigned type=0;type<256;++type) if (type!=8) spell_cell(0,0,type,false);
    for (unsigned draw=0;draw<256;++draw) {
        auto m=h.prepare({1,0x68A4},{.seed=draw});
        m.cpu().af.uint8_values.at_1=draw;
        h.run_cpp(m,{1,0x68A4});
        const auto call=draw&1 ? 0x68A7 : 0x68AC;
        const auto target=static_cast<std::uint16_t>(m.peek(call+1)|(m.peek(call+2)<<8));
        probe::check(m.cpu().pc.uint16_value==target,"escape selected the wrong turn");
    }
}
void check_stream(probe::Harness& h, bool patterns, unsigned count) {
    constexpr std::uint16_t source=0xC600;
    const auto entry=patterns ? game::dungeon_pattern_entries.front() : game::dungeon_tile_entries.front();
    auto m=h.prepare(entry,{.seed=count});
    m.cpu().hl.uint16_value=source;
    std::uint16_t cursor=source;
    std::vector<std::uint8_t> expected;
    auto put=[&](unsigned value) { m.memory().write(cursor++,static_cast<std::uint8_t>(value)); };
    const auto passes=patterns ? 1u : 2u;
    for (unsigned lane=0;lane<passes;++lane) {
        for (bool literal:{false,true}) {
            put((literal ? 0x80 : 0) | count);
            for (unsigned row=0;row<count;++row) {
                const unsigned index=literal ? row : 0;
                std::uint8_t merged=0;
                for (unsigned plane=0;plane<(patterns ? 3u : 1u);++plane) {
                    const auto value=static_cast<std::uint8_t>(count+17*index+61*plane+lane+literal);
                    if (literal || row==0) put(value);
                    merged|=value;
                    expected.push_back(value);
                }
                if (patterns) expected.push_back(merged);
            }
        }
        put(0);
    }
    if (patterns) {
        m.write_port(0xBF,0,m.cycles());
        m.write_port(0xBF,0x40,m.cycles());
    }
    Machine original=m;
    for (unsigned step=0;m.cpu().pc.uint16_value!=0x0180;++step) {
        probe::check(step<1600,"dungeon stream did not return");
        h.run_cpp(m,{1,m.cpu().pc.uint16_value});
    }
    while (original.cycles()<m.cycles()) original.step();
    probe::check(m.equivalent(original),"dungeon stream diverged on return");
    for (unsigned i=0;i<expected.size();++i) {
        const auto address=0xD000+2*(i%(2*count))+i/(2*count);
        const auto actual=patterns ? m.vdp().vram()[i] : m.peek(address);
        probe::check(actual==expected[i],"decoded dungeon tile or bitplane differs");
    }
}
void check_stream_boundaries(probe::Harness& h) {
    for (unsigned count:{1u,2u,3u,17u,63u,127u}) {
        check_stream(h,false,count);
        check_stream(h,true,count);
    }
    for (bool patterns:{false,true}) {
        const auto entry=patterns ? RoutineEntry{1,0x6BB1} : RoutineEntry{1,0x6B81};
        auto m=h.prepare(entry,{});
        m.cpu().af.uint16_value=0x8000;
        m.cpu().bc.uint16_value=0x00BE;
        m.cpu().de.uint16_value=0xD010;
        m.cpu().hl.uint16_value=0xC600;
        h.run_cpp(m,entry);
        probe::check((patterns ? m.cpu().bc.uint16_value>>8 : m.cpu().bc.uint16_value)
                     ==(patterns ? 255 : 65535),"zero literal count must wrap");
        m=h.prepare(entry,{});
        m.cpu().de.uint16_value=0xFFFF;
        m.cpu().bc.uint16_value=0;
        h.run_fallback(m,entry,RoutineFallback::frame);
    }
}
void check_views(probe::Harness& h) {
    namespace view=game::dungeon_view;
    // Vary center and side walls at each distance, facing, frame and cell
    // position. A complete return also checks the calls to the row copier.
    for (unsigned seed=0;seed<256;++seed) {
        auto m=h.prepare(view::cone,{.seed=seed});
        m.cpu().bc.uint16_value=seed%8;
        m.memory().write(game::dungeon::position,seed);
        m.memory().write(game::dungeon::facing,seed/8%4);
        for (unsigned cell=0;cell<256;++cell) {
            unsigned bits=(cell*167u+seed*97u)^((cell+seed)*37u>>3);
            m.memory().write(game::dungeon::layout+cell,(bits & 3)==0 ? (bits>>2 & 7) : 0);
        }
        Machine original=m;
        for (unsigned steps=0;m.cpu().pc.uint16_value!=0x0180;++steps) {
            probe::check(steps<40000,"dungeon cone did not return");
            m.step(h.registry());
        }
        while (original.cycles()<m.cycles()) original.step();
        probe::check(m.equivalent(original),"whole dungeon view differs");
    }
    // The secondary cone layer forces A to zero only for the saved kind zero.
    for (unsigned value:{0u,1u,7u,255u}) {
        auto m=h.prepare({1,0x6D17},{.seed=value});
        const auto sp=m.cpu().sp.uint16_value;
        m.memory().write(sp,0); m.memory().write(sp+1,value);
        m.cpu().hl.uint16_value=0xC600;
        h.run_cpp(m,{1,0x6D17});
    }
    auto exercised=[&](std::uint16_t pc) {
        probe::check(h.routine({1,pc}).counters.cpp_calls>0,"unexercised view entry "+std::to_string(pc));
    };
    for (auto pc:view::other_entries) exercised(pc);
    for (auto base:view::depth) {
        for (auto offset:view::depth_offsets) {
            exercised(base+offset);
        }
    }
}
void check_frame_setup(probe::Harness& h) {
    for (auto pc:game::dungeon_view::frame_entries) {
        RoutineEntry entry{1,pc};
        for (unsigned value=0;value<256;++value) {
            auto m=h.prepare(entry,{.seed=value});
            m.cpu().af.uint8_values.at_1=value;
            m.cpu().de.uint8_values.at_1=value & 1 ? 0x40 : 0x60;
            if (pc==0x6B1D && (value & 2)) m.write_port(0xBF,0,m.cycles());
            h.run_cpp(m,entry);
        }
    }
    for (unsigned high:{0x00u,0x80u,0x81u,0x8Au,0xFFu}) {
        auto m=h.prepare({1,0x6B1D},{});
        m.cpu().de.uint8_values.at_1=high;
        h.run_fallback(m,{1,0x6B1D},RoutineFallback::frame);
    }
    // This address is the first instruction after EI. Compare the entire
    // transition through CALL, including the stack and observer's address.
    auto m=h.prepare({1,0x6B1D},{});
    m.cpu().de.uint8_values.at_1=0x40;
    auto original=m;
    h.run_cpp(m,{1,0x6B1D});
    while (original.cycles()<m.cycles()) original.step();
    bool saw_call=false;
    m.step(h.registry(),[&](auto pc,auto)->std::optional<std::uint16_t> {
        saw_call=pc==0x6B24; return {};
    });
    original.step();
    probe::check(saw_call && m.cpu().pc.uint16_value==0x6B8E && m.equivalent(original),
                 "dungeon upload lost its instruction following EI");
}
void check_animation(probe::Harness& h) {
    namespace v=game::dungeon_view;
    for (unsigned selector=0;selector<256;++selector) {
        auto m=h.prepare(v::animation,{.seed=selector});
        m.cpu().af.uint8_values.at_1=selector;
        const auto table=static_cast<std::uint16_t>(0x6A76+2*selector);
        auto pointer=static_cast<std::uint16_t>(m.peek(table)|(m.peek(table+1)<<8));
        h.run_cpp(m,v::animation);
        probe::check(m.peek(0xC2D2)==0xFF,"dungeon motion did not mark the encounter check pending");
        if (selector<10) {
            // Check the frame stream at its renderer boundary, which is tested separately.
            for (unsigned n=0;m.cpu().pc.uint16_value!=0x0180;++n) {
                probe::check(n<64 && m.cpu().pc.uint16_value==0x6AE5 &&
                    m.cpu().af.uint8_values.at_1==m.peek(pointer),"wrong dungeon animation frame");
                m.cpu().pc.uint16_value=0x6A71;
                m.cpu().sp.uint16_value+=2; // the renderer returned to the sequence driver
                h.run_cpp(m,{1,0x6A71});
                ++pointer;
                h.run_cpp(m,{1,0x6A69});
            }
            probe::check(m.peek(pointer)==0xFF,"sequence ended before its terminator");
        }
        m=h.prepare(v::floor_music,{.seed=selector});
        m.memory().write(game::dungeon::floor,selector);
        h.run_cpp(m,v::floor_music);
        const auto expected=h.rom().byte(3,0xB61C+static_cast<std::uint8_t>(4*selector));
        probe::check(m.cpu().af.uint8_values.at_1==expected && m.cpu().pc.uint16_value==0x0C97,
                     "floor music did not use its cartridge record");
    }
    for (std::uint16_t pc:{0x6AE5,0x6AE8}) {
        auto m=h.prepare({1,pc},{});
        h.run_cpp(m,{1,pc});
    }
}
void check_dungeon_load(probe::Harness& h) {
    namespace v=game::dungeon_view;
    for (auto pc:v::load_entries) for (unsigned state=0;state<8;++state) {
        const RoutineEntry entry{0,pc};
        auto m=h.prepare(entry,{.seed=state});
        m.cpu().af.uint8_values.at_0=state&1 ? 0x40 : 0;
        m.memory().write(game::dungeon::light,state&2 ? 0xFF : 0);
        m.memory().write(game::dungeon::game_mode,0x0A);
        h.run_cpp(m,entry);
        if (pc==0x0F83) probe::check(m.peek(game::dungeon::game_mode)==0x0B,"load did not enter walking mode");
        if (pc==0x0FDC && !(state&2)) probe::check(m.peek(game::dungeon::game_mode)==8,"darkness escape did not return to the map");
    }
    for (auto pc:{0x0F48,0x0F72,0x0F7C,0x1000,0x1008}) {
        auto m=h.prepare({0,static_cast<std::uint16_t>(pc)},{});
        h.run_cpp(m,{0,static_cast<std::uint16_t>(pc)});
    }
    for (unsigned event=0;event<3;++event) for (unsigned distance=1;distance<=41;++distance) {
        auto m=h.prepare(v::refresh,{.iff1=event==1,.iff2=event==1,.seed=distance});
        const auto boundary=event==1 ? m.next_irq().value() : Machine::frame_cycles;
        if (event==2) m.schedule_nmi(boundary-128);
        m.advance(boundary-(event==2 ? 128 : 0)-distance-m.cycles());
        m.memory().write(0xC800,0xAB);
        auto original=m;
        m.step(h.registry());
        do {original.step();} while (original.cycles()<m.cycles());
        probe::check(m.equivalent(original),"sprite seed differs across interrupt or frame boundary");
    }
    auto m=h.prepare(v::refresh,{});
    for (unsigned i=0;i<=256;++i) m.memory().write(0xC800+i,0xA7);
    auto original=m;
    h.run_cpp(m,v::refresh);
    while (m.cpu().pc.uint16_value==0x0FFE) h.run_cpp(m,{0,0x0FFE});
    h.run_cpp(m,{0,0x1000});
    while (original.cycles()<m.cycles()) original.step();
    probe::check(m.equivalent(original) && m.peek(0xC900)==0xD0,"sprite terminator or clear differs");
    for (unsigned i=0;i<256;++i) probe::check(m.peek(0xC800+i)==0,"sprite shadow was not cleared");
    m=h.prepare(v::refresh,{.work_ram_disabled=true});
    h.run_fallback(m,v::refresh,RoutineFallback::unmapped_stack);
}
void check_overlays(probe::Harness& h) {
    for (auto pc:game::dungeon_view::overlay_entries) {
        RoutineEntry entry{1,pc};
        for (unsigned seed=0;seed<256;++seed) {
            auto m=h.prepare(entry,{.seed=seed});
            m.cpu().hl.uint16_value=0xC610;
            m.cpu().de.uint16_value=0xD220;
            m.cpu().af.uint8_values.at_1=seed;
            m.memory().write(0xC610,seed);
            m.memory().write(0xC2F5,seed);
            h.run_cpp(m,entry);
        }
    }
    // Include transparent and opaque cells, adjacent high tile bytes, and
    // non-contiguous row destinations. All 256 possible source IDs occur.
    auto m=h.prepare({1,0x6E20},{});
    m.cpu().hl.uint16_value=0xC600;
    m.cpu().bc.uint16_value=0x0200;
    unsigned cursor=0xC600;
    for (unsigned row=0;row<2;++row) {
        const auto dest=0xD100+0x300*row;
        m.memory().write(cursor++,dest&255);
        m.memory().write(cursor++,dest>>8);
        m.memory().write(cursor++,128);
        for (unsigned i=0;i<128;++i) {
            m.memory().write(cursor++,128*row+i);
            m.memory().write(dest+2*i,0x13);
            m.memory().write(dest+2*i+1,0x31);
        }
    }
    auto original=m;
    for (unsigned step=0;m.cpu().pc.uint16_value!=0x0180;++step) {
        probe::check(step<300,"fake wall did not finish both rows");
        h.run_cpp(m,{1,m.cpu().pc.uint16_value});
    }
    while (original.cycles()<m.cycles()) original.step();
    probe::check(m.equivalent(original),"fake wall row state diverged");
    for (unsigned src=0;src<256;++src) {
        const auto color=static_cast<std::uint8_t>(src+128);
        const auto dest=0xD100+0x300*(src/128)+2*(src%128);
        probe::check(m.peek(dest)==(color>=0xC0 ? color : 0x13) && m.peek(dest+1)==0x31,
                     "fake wall changed the wrong tilemap byte");
    }
}
}
int main(int argc,char** argv) {
    try {
        if (argc!=3) return 1;
        auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        probe::Harness h(*rom);
        pitfall_copy(h);
        check_stream_boundaries(h);
        check_views(h);
        check_frame_setup(h);
        check_animation(h);
        check_dungeon_load(h);
        check_overlays(h);
        game::DungeonTables tables(*rom->image);
        check_dungeon_escape(h,tables);
        const auto start=game::dungeon_layout_entries[0];
        for (unsigned floor=0;floor<game::dungeon::floor_count;++floor) {
            for (unsigned light:{0u,1u,255u}) {
                auto m=h.prepare(start,{.seed=floor+light});
                m.memory().write(game::dungeon::floor,floor);
                m.memory().write(game::dungeon::light,light);
                Machine baseline=m;
                for (unsigned pair=0;pair<128;++pair)
                    h.run_cpp(m,game::dungeon_layout_entries[pair ? 1 : 0]);
                h.run_cpp(m,game::dungeon_layout_entries[2]);
                const auto image=tables.layout(floor);
                for (unsigned i=0;i<image.size();++i)
                    probe::check(m.peek(game::dungeon::layout+i)==image[i],"dungeon contents differ");
                while (baseline.cycles()<m.cycles()) baseline.step();
                probe::check(m.equivalent(baseline),"whole layout load diverged");
            }
        }
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
