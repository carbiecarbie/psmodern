// SPDX-License-Identifier: GPL-3.0-or-later
// The frame interrupt's kinds of work (00:0159-02E2), the reset sequence's
// calls (00:00A2-00AE) and the main loop's jump (00:00BB): the original calls
// and jumps between the game operations, each a single-instruction segment,
// and the two vertical-blank uploads (name table and tiles at 00:01EF, the
// sprite and palette variant at 00:025F). Milestone 14: every final is
// rendered; no instruction of this file is left unrendered.
#include "game/frame_updates.h"
#include "game/map_kit.h"

namespace psmodern::game::frame_updates {
namespace {
namespace k=map_kit;
constexpr unsigned transfer_slice=104;
// The bound of a segment when the estimate cannot know the path (a read of
// unmapped memory): longer than any path here, shorter than a frame, so the
// body still runs and reads the open bus as in the original. The paths of these
// segments depend only on registers.
constexpr std::uint64_t segment_maximum=1000;
// The tile upload: OUTI to the data port until B runs out, then the next of
// A pages. A slice ends at the loop's jump once `transfer_slice` cycles
// passed (the next slice continues there).
template<class Z> k::Exit transfer_tiles(Z& z,std::uint16_t pc) {
    for (;;) {
        z.output_increment(pc);
        if (z.loop_exit(transfer_slice)) return z.final_jump_if(pc+2,k::Condition::nz);
        if (!z.jump_if(pc+2,k::Condition::nz)) break;
    }
    z.decrement(pc+5,z.r.a);
    return z.final_jump_if(pc+6,k::Condition::nz);
}
}
void register_updates(RoutineRegistry& registry) {
    auto bridge=[&](Transfer t,SegmentExit exit,unsigned cost,bool call,auto logic) {
        auto spec=k::segment({0,t.unit},{exit},{.scratch_stack=call ? 2u : 0u});
        spec.single_instruction=true;
        spec.observe_entry=t.pc==0x0B14;
        k::add_segment(registry,{0,t.pc},std::move(spec),[cost](const Machine&) -> std::uint64_t { return cost; },logic);
    };
    for (auto t:calls) bridge(t,k::core_final_at(t.pc),k::call_cycles,true,[pc=t.pc](auto& z) { return z.final_call(pc); });
    for (auto t:jumps) bridge(t,k::core_final_at(t.pc),k::jump_cycles,false,[pc=t.pc](auto& z) { return z.final_jump(pc); });
    // The returns of the display switch (00:0051) and of the frame wait
    // (00:005B), rendered (milestone 14 worked example).
    for (auto t:{entries[0],entries[1]})
        k::add_segment(registry,{0,t.pc},k::segment({0,t.unit},{k::returned_at(t.pc)}),
                       [t](auto& z){return z.final_return(t.pc);});
    // The reset sequence enables interrupts before the main loop.
    auto enable=k::core_final_at(0x00AE);
    enable.interrupt_mask=InterruptMask::enable;
    bridge({0x00AE,0x0085},enable,4,false,[](auto& z) { return z.final_enable_interrupts(0x00AE); });
    for (std::uint16_t start:{0x01EF,0x025F}) {
        const bool palette=start==0x025F;
        const RoutineEntry unit{0,start};
        auto add=[&](std::uint16_t pc,std::vector<SegmentExit> exits,k::Permissions perms,auto logic) {
            k::add_segment(registry,{0,pc},k::segment(unit,std::move(exits),perms),segment_maximum,logic);
        };
        // The scroll registers from their mirrors; then the name-table kind
        // points HL at the name-table buffer, the palette kind calls the
        // sprite-table upload. An interrupt-control register write (an odd
        // latch state) runs in C++ too: the body is exact.
        add(start,{k::core_final_at(start+18)},{.scratch_stack=palette ? 2u : 0u},[=](auto& z) {
            k::write_scroll_registers(z,start);
            if (palette) return z.final_call(start+18);
            z.load_hl(start+18);
            return k::fall_through(start+18,start+21);
        });
        if (palette) {
            add(0x0274,{k::core_final_at(0x027A)},{.scratch_stack=2},[](auto& z) {
                z.load_hl(0x0274);
                z.load_de(0x0277);
                return z.final_restart(0x027A,0x08);
            });
            add(0x027B,{k::core_final_at(0x027D)},{.scratch_stack=2},[](auto& z) {
                z.load_immediate(0x027B,z.r.c);
                return z.final_call(0x027D);
            });
        }
        const std::uint16_t loop=palette ? 0x0290 : 0x0226;
        // The VDP address of the tile upload, then the first run's count.
        const auto begin=[=](auto& z) {
            const auto clear=palette ? 0x0283 : 0x0204;
            if (palette) z.load_hl(0x0280);
            z.xor_a(clear);
            z.output(clear+1);
            z.load_immediate(clear+3,z.r.a);
            z.output(clear+5);
            z.load_immediate(clear+7,z.r.c);
            if (!palette) return z.final_call(0x020D);
            z.load_immediate(0x028C,z.r.a);
            z.load_immediate(0x028E,z.r.b);
            return k::fall_through(0x028E,0x0290);
        };
        const auto entry=static_cast<std::uint16_t>(palette ? 0x0280 : 0x0204);
        add(entry,{k::core_final_at(palette ? 0x028E : 0x020D)},{.scratch_stack=palette ? 0u : 2u},begin);
        if (!palette) add(0x0222,{k::core_final_at(0x0224)},{},[](auto& z) {
            z.load_immediate(0x0222,z.r.a);
            z.load_immediate(0x0224,z.r.b);
            return k::fall_through(0x0224,0x0226);
        });
        add(loop,{k::conditional_loop_at(loop+2,transfer_slice),k::conditional_at(loop+6)},{},
            [loop](auto& z) { return transfer_tiles(z,loop); });
        add(loop+5,{k::conditional_at(loop+6)},{},[loop](auto& z) {
            z.decrement(loop+5,z.r.a);
            return z.final_jump_if(loop+6,k::Condition::nz);
        });
    }
}
}
