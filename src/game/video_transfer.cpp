// SPDX-License-Identifier: GPL-3.0-or-later
// Video memory transfers of the fixed bank: the VDP address helper (00:0008),
// the page copy and fill (00:0346, 00:0363), the row copies with and without
// a tile attribute (00:0377, 00:0390), the VDP register set-up (00:03A4) and
// the two four-plane graphics packet decoders (00:03CD, and 00:03FA, which
// masks interrupts around each address set-up and data write). Milestone 14:
// every instruction of every segment is rendered, its final included; the
// joining jumps between them are segments of their own.
#include "game/map_kit.h"
#include "game/video_transfer.h"

namespace psmodern::game::video_transfer {
namespace {
namespace k=map_kit;
// The longest OTIR slice (sixteen repeated iterations): the bound of a slice
// whose source the estimate cannot read (unmapped), which changes nothing in
// its path or cost.
constexpr std::uint64_t output_slice_maximum=16*21;
// The bound of the other segments when the estimate cannot know the path (a
// read of unmapped memory): longer than any of their paths, shorter than a
// frame, so the body still runs and reads the open bus as in the original.
constexpr std::uint64_t segment_maximum=1000;
template<class Z>
void partial_page(Z& z,std::uint16_t at) {
    z.copy(at,z.r.a,z.r.c);
    z.or_a(at+1);
    if (!z.relative_if(at+2,k::Condition::z)) z.increment(at+4,z.r.b);
}
template<class Z>
k::Exit fill_word(Z& z) {
    z.copy(0x0369,z.r.a,z.r.l);
    z.output(0x036A);
    z.push(0x036C,z.r.a,z.r.f);
    z.pop(0x036D,z.r.a,z.r.f);
    z.copy(0x036E,z.r.a,z.r.h);
    z.output(0x036F);
    z.decrement(0x0371,z.r.c);
    return z.final_relative_if(0x0372,k::Condition::nz);
}
template<class Z>
k::Exit tile_with_attribute(Z& z) {
    z.output_increment(0x037C);
    z.load_a_direct(0x037E);
    z.nop(0x0381);
    // out (c),a: the tile's attribute byte after its name byte.
    z.bus().fetch(0x0382);
    z.bus().fetch(0x0383);
    z.r.wz=static_cast<std::uint16_t>(z.bc()+1);
    z.bus().out(z.bc(),z.r.a,11);
    z.bus().advance(1);
    return z.final_relative_if(0x0384,k::Condition::nz);
}
template<class Logic>
void add(RoutineRegistry& registry,unsigned pc,unsigned unit,std::vector<SegmentExit> exits,
         k::Permissions permissions,Logic logic) {
    k::add_segment(registry,{0,static_cast<std::uint16_t>(pc)},
        k::segment({0,static_cast<std::uint16_t>(unit)},std::move(exits),permissions),segment_maximum,logic);
}
// A segment that is its single final instruction (a restart, a mask change
// or a joining jump), with its fixed cost.
template<class Logic>
void single(RoutineRegistry& registry,std::uint16_t pc,std::uint16_t unit,SegmentExit exit,unsigned cycles,
            k::Permissions permissions,Logic logic) {
    auto spec=k::segment({0,unit},{exit},permissions);
    spec.single_instruction=true;
    k::add_segment(registry,{0,pc},std::move(spec),[cycles](const Machine&) -> std::uint64_t { return cycles; },logic);
}
}
void register_packets(RoutineRegistry& registry) {
    // Each stream supplies one byte of every four in VRAM. The second
    // decoder additionally masks interrupts from address setup through OUT.
    for (bool masked:{false,true}) {
        const std::uint16_t unit=masked ? 0x03FA : 0x03CD;
        const std::uint16_t header=unit+13, address=header+11;
        const std::uint16_t data=masked ? 0x0414 : 0x03E6;
        const std::uint16_t stride=masked ? 0x041C : 0x03ED;
        // The four planes: each call decodes one plane's stream.
        for (auto pc:{unit,static_cast<std::uint16_t>(unit+2)}) {
            add(registry,pc,unit,{k::core_final_at(unit+4,-4)},{.scratch_stack=6},[=](auto& z) {
                if (pc==unit) z.load_immediate(unit,z.r.b);
                z.push(unit+2,z.r.b,z.r.c);
                z.push(unit+3,z.r.d,z.r.e);
                return z.final_call(unit+4);
            });
        }
        add(registry,unit+7,unit,{k::conditional_at(unit+10,4)},{.inherited_stack=4},[=](auto& z) {
            z.pop(unit+7,z.r.d,z.r.e);
            z.increment_pair(unit+8,z.r.d,z.r.e);
            z.pop(unit+9,z.r.b,z.r.c);
            return z.final_decrement_jump(unit+10);
        });
        add(registry,unit+12,unit,{k::returned_at(unit+12)},{},[=](auto& z) {
            return z.final_return(unit+12);
        });
        auto begin_address=k::core_final_at(address);
        if (masked) begin_address.interrupt_mask=InterruptMask::disable;
        // A packet header: zero ends the plane; bit 7 selects a literal run,
        // the low seven bits are the count.
        const auto packet=[=](auto& z) {
            z.load_from_hl(header,z.r.a);
            z.increment_hl(header+1);
            z.or_a(header+2);
            if (auto exit=z.final_return_if(header+3,k::Condition::z)) return *exit;
            z.copy(header+4,z.r.c,z.r.a);
            z.and_immediate(header+5);
            z.copy(header+7,z.r.b,z.r.a);
            z.copy(header+8,z.r.a,z.r.c);
            z.and_immediate(header+9);
            return masked ? z.final_disable_interrupts(address) : z.final_restart(address,0x08);
        };
        const k::Permissions stack{.scratch_stack=masked ? 0u : 2u};
        add(registry,header,unit,{k::returned_at(header+3),begin_address},stack,packet);
        // Each byte begins with the VDP address set-up (masked: after DI).
        if (masked) {
            single(registry,address,unit,begin_address,4,stack,[=](auto& z) { return z.final_disable_interrupts(address); });
            single(registry,address+1,unit,k::core_final_at(address+1),k::restart_cycles,{.scratch_stack=2},
                   [=](auto& z) { return z.final_restart(address+1,0x08); });
        } else single(registry,address,unit,begin_address,k::restart_cycles,stack,
                      [=](auto& z) { return z.final_restart(address,0x08); });
        auto written=masked ? k::core_final_at(data+3) : k::conditional_at(data+3);
        if (masked) written.interrupt_mask=InterruptMask::enable;
        add(registry,data,unit,{written},{},[=](auto& z) {
            z.load_from_hl(data,z.r.a);
            z.output(data+1);
            return masked ? z.final_enable_interrupts(data+3) : z.final_jump_if(data+3,k::Condition::z);
        });
        // Masked: the literal-or-repeat branch follows EI in its own
        // instruction slot (no interrupt is taken before it).
        if (masked)
            single(registry,data+4,unit,k::conditional_at(data+4),k::jump_cycles,{},
                   [=](auto& z) { return z.final_jump_if(data+4,k::Condition::z); });
        // The conditional jump after EI executes in its original protected
        // instruction slot. A literal consumes its byte immediately.
        for (const std::uint16_t pc:{static_cast<std::uint16_t>(stride-1),stride}) {
            add(registry,pc,unit,{k::conditional_at(stride+4)},{},[=](auto& z) {
                if (pc!=stride) z.increment_hl(pc);
                for (unsigned i=0;i<4;++i) z.increment_pair(stride+i,z.r.d,z.r.e);
                return z.final_decrement_jump(stride+4);
            });
        }
        const std::uint16_t end=stride+6;
        // After a literal run the next header follows at once; after a
        // repeated run the stream first steps past the repeated byte.
        single(registry,end,unit,k::conditional_at(end),k::jump_cycles,{},
               [=](auto& z) { return z.final_jump_if(end,k::Condition::nz); });
        add(registry,end+3,unit,{k::core_final_at(end+4)},{},[=](auto& z) {
            z.increment_hl(end+3);
            return z.final_jump(end+4);
        });
    }
}
void register_transfers(RoutineRegistry& registry) {
    // The VDP register set-up: register writes from a ROM table (its address
    // and the count are the operands at 00:03A4 and 00:03A7), then two of the
    // table's values copied to the register mirror in work RAM.
    add(registry,0x03A4,0x03A4,{k::core_final_at(0x03A7)},{},[](auto& z) {
        z.load_hl(0x03A4);
        z.load_bc(0x03A7);
        return k::fall_through(0x03A7,0x03AA);
    });
    const SegmentExit registers{RoutineExit::output_repeat,0x03AA};
    k::add_segment(registry,{0,0x03AA},k::segment({0,0x03A4},{registers}),output_slice_maximum,
                   [](auto& z) { return z.final_output_repeat(0x03AA); });
    add(registry,0x03AC,0x03A4,{k::returned_at(0x03B8)},{},[](auto& z) {
        z.load_a_direct(0x03AC);
        z.store_a_direct(0x03AF);
        z.load_a_direct(0x03B2);
        z.store_a_direct(0x03B5);
        return z.final_return(0x03B8);
    });
    // 00:0008, the VDP address helper the game calls with RST 08: E then D
    // to the control port. Writes to the interrupt-control registers 0, 1
    // and 10 run in C++ too (milestone 14): the body is exact, so an
    // interrupt the write raises is taken at the next instruction boundary,
    // as in the original.
    k::add_segment(registry,{0,8},k::segment({0,8},{k::returned_at(0x000E)}),[](const Machine&) -> std::uint64_t { return 40; },
        [](auto& z) {
            z.copy(0x0008,z.r.a,z.r.e);
            z.output(0x0009);
            z.copy(0x000B,z.r.a,z.r.d);
            z.output(0x000C);
            return z.final_return(0x000E);
        });
    for (const std::uint16_t pc:{0x0346,0x0363})
        single(registry,pc,pc,k::core_final_at(pc),k::restart_cycles,{.scratch_stack=2},
               [pc](auto& z) { return z.final_restart(pc,0x08); });
    // A page copy: B bytes of the first page (a partial page counts as one
    // more), then A further pages of 256.
    for (auto pc:{0x0347,0x034C})
        add(registry,pc,0x0346,{k::conditional_at(0x0352)},{},[pc](auto& z) {
            if (pc==0x0347) partial_page(z,pc);
            z.copy(0x034C,z.r.a,z.r.b);
            z.copy(0x034D,z.r.b,z.r.c);
            z.load_immediate(0x034E,z.r.c);
            z.output_increment(0x0350);
            return z.final_jump_if(0x0352,k::Condition::nz);
        });
    add(registry,0x0350,0x0346,{k::conditional_at(0x0352)},{},[](auto& z) {
        z.output_increment(0x0350);
        return z.final_jump_if(0x0352,k::Condition::nz);
    });
    add(registry,0x0355,0x0346,{k::conditional_at(0x0356)},{},[](auto& z) {
        z.decrement(0x0355,z.r.a);
        return z.final_jump_if(0x0356,k::Condition::nz);
    });
    add(registry,0x035A,0x0363,{k::core_final_at(0x0363)},{.scratch_stack=2},[](auto& z) {
        z.load_de(0x035A);
        z.load_bc(0x035D);
        z.load_hl(0x0360);
        return z.final_restart(0x0363,0x08);
    });
    for (auto pc:{0x0364,0x0369})
        add(registry,pc,0x0363,{k::conditional_at(0x0372)},{.scratch_stack=2},[pc](auto& z) {
            if (pc==0x0364) partial_page(z,pc);
            return fill_word(z);
        });
    // DJNZ for the next page can also be the final iteration of the fill.
    add(registry,0x0374,0x0363,{k::returned_at(0x0376),k::conditional_at(0x0372)},
        {.scratch_stack=2},[](auto& z) {
            if (z.decrement_jump(0x0374)) return fill_word(z);
            return z.final_return(0x0376);
        });
    for (bool attributes:{true,false}) {
        const std::uint16_t unit=attributes ? 0x0377 : 0x0390;
        const auto loop=unit+5, next_row=attributes ? 0x0386 : 0x039A;
        const auto branch=attributes ? 0x0384 : 0x0397;
        add(registry,unit,unit,{k::core_final_at(unit+1,-2)},{.scratch_stack=4},[unit](auto& z) {
            z.push(unit,z.r.b,z.r.c);
            return z.final_restart(unit+1,0x08);
        });
        auto transfer=[=](auto& z) {
            if (attributes) return tile_with_attribute(z);
            z.output_increment(loop);
            return z.final_jump_if(branch,k::Condition::nz);
        };
        add(registry,unit+2,unit,{k::conditional_at(branch)},{},[=](auto& z) {
            z.copy(unit+2,z.r.b,z.r.c);
            z.load_immediate(unit+3,z.r.c);
            return transfer(z);
        });
        add(registry,loop,unit,{k::conditional_at(branch)},{},transfer);
        // The next row is 64 bytes further in VRAM.
        add(registry,next_row,unit,{k::conditional_at(next_row+7,2)},{.inherited_stack=2},[=](auto& z) {
            z.exchange_de_hl(next_row);
            z.load_bc(next_row+1);
            z.add_to_hl(next_row+4,z.bc());
            z.exchange_de_hl(next_row+5);
            z.pop(next_row+6,z.r.b,z.r.c);
            return z.final_decrement_jump(next_row+7);
        });
    }
    for (auto [pc,unit]:{std::pair{0x0359,0x0346},{0x0376,0x0363},{0x038F,0x0377},{0x03A3,0x0390}})
        add(registry,pc,unit,{k::returned_at(pc)},{},[pc](auto& z) { return z.final_return(pc); });
}
}
