// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_cartridge.h"
#include "probe.h"
#include <iostream>

using namespace psmodern;
using namespace psmodern::app::retro;
namespace {
void compare_with_game(const probe::Rom& rom) {
    RoutineRegistry routines;
    game::register_routines(routines,rom.sha1,{});
    Machine machine(rom.image);
    auto& ram=machine.memory();
    auto& cpu=machine.cpu();
    Cartridge data(*rom.image,ram.ram());
    const auto execute=[&](std::uint16_t pc,std::uint16_t stop=0x4000) {
        cpu.pc.uint16_value=pc;cpu.sp.uint16_value=0xd500;
        ram.write(0xd500,0);ram.write(0xd501,0x40);
        for (unsigned steps=0;cpu.pc.uint16_value!=stop;++steps) {
            probe::check(steps<200,"cartridge reader probe did not return");
            machine.step(routines);
        }
        machine.take_psg_writes();machine.take_samples();
    };
    constexpr std::array<unsigned,4> level_tables{0x2b5d,0x2b68,0x2b74,0x2b80};
    for (unsigned member=0;member<4;++member) {
        const auto record=0xc400+member*16;
        for (unsigned i=0;i<4;++i) ram.write(0xc400+16*i,i==member);
        ram.write(0xc267,member);ram.write(record+2,80);
        for (unsigned id=0;id<19;++id) {
            cpu.af.uint8_values.at_1=id;
            execute(0x1b87);
            probe::check(80-data.spell_cost(id)==cpu.af.uint8_values.at_1,"MP cost differs from the actual debit");
        }
        for (unsigned level=1;level<=30;++level) {
            ram.write(record+5,level);ram.write(record+3,0xfe);ram.write(record+4,0x11);
            const auto next=data.next_experience(member);
            cpu.af.uint8_values.at_1=member;
            cpu.ix_iy[1].uint16_value=record;
            cpu.de.uint16_value=data.word(level_tables[member]);
            execute(0x2b84,0x31cf);
            probe::check(next.has_value()==(level<30) && (!next || *next==data.word(0xc2c5)),
                         "next-level subtraction or cap differs from the church report");
            for (unsigned item=1;item<64;++item) {
                ram.write(record+10,14);ram.write(record+11,19);ram.write(record+12,31);
                execute(0x17ba);
                const auto preview=data.compare(member,item);
                if (!preview.eligible) continue;
                ram.write(record+10+preview.slot,item);
                execute(0x17ba);
                probe::check(preview.after==machine.peek(record+(preview.slot==0 ? 8 : 9)),
                             "equipment preview differs from the actual byte-sized refreshed stat");
            }
        }
    }
}
}
int main(int argc,char** argv) {
    try {
        probe::check(argc==3,"Expected ROM and region");
        auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        compare_with_game(*rom);
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
