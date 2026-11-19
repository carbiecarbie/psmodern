// SPDX-License-Identifier: GPL-3.0-or-later
// Common fixed-bank control flow, with host-observed boundaries preserved:
// power-on and reset (00:0000-00AF), the frame interrupt's entry, reset
// check, kind dispatch and exit (00:0038, 00:00FB-0158), the Pause NMI
// (00:0067-0082), the display switch, the frame wait, the main loop's mode
// selection and the video timing measurement. Milestone 14: every final
// instruction is rendered here like the instructions before it, and the
// joining instructions between C++ segments are single-instruction segments
// of their own; no instruction of this file is left unrendered.
#include "game/common.h"
#include "game/routines.h"
#include "game/menu_kit.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
// The bound of a segment when the estimate cannot know its path (a read of
// unmapped memory, for example with work RAM disabled): longer than any path
// of this file (the longest, the video-timing delay from 00:0124, takes about
// 6,700 cycles), shorter than a frame, so the body still runs and reads the
// open bus as in the original.
constexpr std::uint64_t segment_maximum=10000;
template<class Logic> void add(RoutineRegistry& registry, RoutineEntry entry, SegmentSpec spec, Logic logic) {
    k::add_segment(registry,entry,std::move(spec),segment_maximum,std::move(logic));
}
// A segment that is its single final instruction, with its fixed cost.
template<class Logic> void single(RoutineRegistry& registry, RoutineEntry entry, RoutineEntry unit, SegmentExit exit,
                                  std::uint64_t cycles, Logic logic, unsigned scratch=0, bool observed=false) {
    auto spec=k::segment(unit,{exit},{.scratch_stack=scratch});
    spec.single_instruction=true;
    spec.observe_entry=observed;
    k::add_segment(registry,entry,std::move(spec),[cycles](const Machine&) { return cycles; },std::move(logic));
}
// in a,(n): the port's high byte is A; the byte is read at the original
// T-state of the read (T10 of eleven).
template<class Z> void input_a(Z& z, std::uint16_t at) {
    z.bus().fetch(at);
    const auto port=k::pair(z.r.a,z.bus().read(static_cast<std::uint16_t>(at+1)));
    z.r.a=z.bus().in(port,10);
    z.r.wz=static_cast<std::uint16_t>(port+1);
    z.bus().advance(1);
}
// The loader waits for a frame before handing off to fade-in.
template<class Z> k::Exit wait_after_sprite_clear(Z& z) {
    z.load_immediate(0x0310,z.r.a);
    return z.final_call(0x0312);
}
// Initialize and fill the sprite Y table with the hidden marker. The
// existing 0306 entry still handles callers that arrive at the fill itself.
template<class Z> k::Exit initialize_sprite_clear(Z& z) {
    z.load_hl(0x02FD);
    z.load_de(0x0300);
    z.load_bc(0x0303);
    z.store_to_hl(0x0306);
    z.block_copy(0x0308);
    z.load_immediate(0x030A,z.r.c);
    return k::fall_through(0x030A,0x030C);
}
// Wait for the pause flag to clear; each pass through the wait is a segment.
template<class Z> k::Exit poll_pause_flag(Z& z) {
    z.load_a_direct(0x00F4);
    z.or_a(0x00F7);
    if (auto exit=z.final_return_if(0x00F8,k::Condition::z)) return *exit;
    return z.final_relative(0x00F9);
}
// Recover the interrupted registers in stack order, then enable interrupts
// again: the return after it (00:0158, its own segment) runs before any
// interrupt can be taken, as in the original.
template<class Z> k::Exit restore_interrupt_registers(Z& z) {
    z.pop_index(0x014F,k::Index::iy);
    z.pop_index(0x0151,k::Index::ix);
    z.pop(0x0153,z.r.h,z.r.l);
    z.pop(0x0154,z.r.d,z.r.e);
    z.pop(0x0155,z.r.b,z.r.c);
    z.pop(0x0156,z.r.a,z.r.f);
    return z.final_enable_interrupts(0x0157);
}
// Restore the slot-2 bank first; the second POP deliberately reads after
// that change. Then the cartridge control the interrupt found.
template<class Z> k::Exit restore_interrupt_mapper(Z& z) {
    z.pop(0x0147,z.r.a,z.r.f);
    z.store_a_direct(0x0148);
    z.pop(0x014B,z.r.a,z.r.f);
    z.store_a_direct(0x014C);
    return k::fall_through(0x014C,0x014F);
}
// Clear the frame-wait flag: the frame's work is done.
template<class Z> k::Exit finish_interrupt_frame(Z& z) {
    z.xor_a(0x0143);
    z.store_a_direct(0x0144);
    return restore_interrupt_mapper(z);
}
// Sample VDP status once per timing-loop iteration (until the status shows
// the frame's end); each iteration is a segment ending at the loop's branch.
template<class Z> k::Exit sample_video_status(Z& z,std::uint16_t at) {
    input_a(z,at);
    z.or_a(static_cast<std::uint16_t>(at+2));
    return z.final_jump_if(static_cast<std::uint16_t>(at+3),k::Condition::p);
}
template<class Z> k::Exit begin_video_timing(Z& z) {
    z.load_hl(0x0318);
    return sample_video_status(z,0x031B);
}
template<class Z> k::Exit count_video_timing(Z& z) {
    z.increment_hl(0x0327);
    return sample_video_status(z,0x0328);
}
// The count of status samples during one frame selects the slow timing
// (a flag at the operand of 00:0335).
template<class Z> k::Exit finish_video_timing(Z& z) {
    z.xor_a(0x032E);
    z.load_de(0x032F);
    z.subtract_hl_with_carry(0x0332,z.de());
    z.subtract_with_carry(0x0334,z.r.a);
    z.store_a_direct(0x0335);
    return z.final_return(0x0338);
}
// Start the video-timing delay of the slow interrupt path. The segment ends
// at the delay loop's 16th iteration; the next one continues it.
template<class Z> k::Exit begin_video_delay(Z& z) {
    z.load_immediate(0x0124,z.r.b);
    for (unsigned i=0;i<15;++i) z.decrement_jump(0x0126);
    return z.final_decrement_jump(0x0126);
}
// Select the slow video-timing delay and the pause path in interrupt dispatch.
template<class Z> k::Exit classify_interrupt_state(Z& z,std::uint16_t at) {
    z.load_a_direct(at);
    z.or_a(static_cast<std::uint16_t>(at+3));
    return z.final_jump_if(static_cast<std::uint16_t>(at+4),k::Condition::nz);
}
// Both DJNZ delays wrap an initial zero count to 256 iterations.
template<class Z> k::Exit finish_video_delay(Z& z,std::uint16_t pc) {
    if (pc==0x0126) while (z.decrement_jump(0x0126)) {}
    while (z.decrement_jump(0x0128)) {}
    return classify_interrupt_state(z,0x012A);
}
// Sample the Reset button once per frame interrupt: a new press (pressed
// now, released at the previous sample, kept in work RAM) jumps to the reset
// code at 00:0084. Taken inside a suspended body (an interrupt nested in a
// C++ segment), that jump abandons the body (Machine::deliver).
template<class Z> k::Exit check_reset_button(Z& z) {
    input_a(z,0x010F);
    z.and_immediate(0x0111);
    z.load_hl(0x0113);
    z.load_from_hl(0x0116,z.r.c);
    z.store_register_to_hl(0x0117,z.r.a);
    z.xor_register(0x0118,z.r.c);
    z.and_register(0x0119,z.r.c);
    return z.final_jump_if(0x011A,k::Condition::nz);
}
// Save the interrupt-visible registers and mapper state, then select the
// cartridge control the interrupt code runs with.
template<class Z> k::Exit save_interrupt_context(Z& z) {
    z.push(0x00FB,z.r.b,z.r.c);
    z.push(0x00FC,z.r.d,z.r.e);
    z.push(0x00FD,z.r.h,z.r.l);
    z.push_index(0x00FE,k::Index::ix);
    z.push_index(0x0100,k::Index::iy);
    z.load_a_direct(0x0102);
    z.push(0x0105,z.r.a,z.r.f);
    z.load_a_direct(0x0106);
    z.push(0x0109,z.r.a,z.r.f);
    z.load_immediate(0x010A,z.r.a);
    z.store_a_direct(0x010C);
    return k::fall_through(0x010C,0x010F);
}
// Pause NMI only changes the flag in the four gameplay modes that allow it.
template<class Z> k::Exit classify_pause_nmi(Z& z) {
    z.load_a_direct(0x0067);
    z.compare_immediate(0x006A);
    return z.final_relative_if(0x006C,k::Condition::z);
}
template<class Z> k::Exit compare_pause_mode(Z& z,std::uint16_t at) {
    z.compare_immediate(at);
    const auto branch=static_cast<std::uint16_t>(at+2);
    return z.final_relative_if(branch,at==0x0076 ? k::Condition::nz : k::Condition::z);
}
// Toggle pause for eligible modes, then restore the AF saved by the NMI
// and return from it (the IFF1 the NMI cleared comes back from IFF2).
template<class Z> k::Exit restore_pause_nmi(Z& z) {
    z.pop(0x0081,z.r.a,z.r.f);
    return z.final_return_from_nmi(0x0082);
}
template<class Z> k::Exit toggle_pause_nmi(Z& z) {
    z.load_a_direct(0x007A);
    z.complement_a(0x007D);
    z.store_a_direct(0x007E);
    return restore_pause_nmi(z);
}
// The interrupt vector saves AF and acknowledges VDP status at its
// original port-read cycle, then jumps to the shared interrupt prologue.
template<class Z> k::Exit acknowledge_video_interrupt(Z& z) {
    z.push(0x0038,z.r.a,z.r.f);
    input_a(z,0x0039);
    return z.final_jump(0x003B);
}
// Seed the work-RAM clear: its first byte, which the block copy after it
// propagates through the rest of work RAM.
template<class Z> k::Exit prepare_work_ram_clear(Z& z) {
    z.load_hl(0x0096);
    z.load_de(0x0099);
    z.load_bc(0x009C);
    z.store_register_to_hl(0x009F,z.r.l);
    return k::fall_through(0x009F,0x00A0);
}
// Reset initializes each mapper register in order (cartridge control, then
// the three slots); each store changes the mapping the next instruction sees.
template<class Z> k::Exit first_mapper_register(Z& z) {
    z.load_hl(0x0088);
    z.store_to_hl(0x008B);
    return k::fall_through(0x008B,0x008D);
}
template<class Z> k::Exit next_mapper_register(Z& z,std::uint16_t at) {
    z.increment_hl(at);
    z.store_to_hl(static_cast<std::uint16_t>(at+1));
    return k::fall_through(static_cast<std::uint16_t>(at+1),static_cast<std::uint16_t>(at+3));
}
SegmentSpec mapper_reset_spec(RoutineEntry entry,std::uint16_t pc,std::uint16_t address,std::uint8_t value) {
    SegmentExit exit{RoutineExit::core_final,pc};
    exit.mapper_immediate=SegmentExit::MapperImmediate{address,value};
    return SegmentSpec{entry,{exit}};
}
// Change the display bit in the VDP register mirror, then write the register
// through the VDP address helper (RST 08).
template<class Z> k::Exit display_register_tail(Z& z) {
    z.store_a_direct(0x004A);
    z.copy(0x004D,z.r.e,z.r.a);
    z.load_immediate(0x004E,z.r.d);
    return z.final_restart(0x0050,0x08);
}
template<class Z> k::Exit disable_display(Z& z) {
    z.load_a_direct(0x003E);
    z.and_immediate(0x0041);
    z.relative(0x0043);
    return display_register_tail(z);
}
template<class Z> k::Exit enable_display(Z& z) {
    z.load_a_direct(0x0045);
    z.or_immediate(0x0048);
    return display_register_tail(z);
}
// The frame wait (first worked example of milestone 14 Part 0b: every final
// rendered). The caller's A tells the frame interrupt's handler what to do
// this frame; it is published at C208 (00:0052), then the wait reads C208
// back (00:0055), tests it (00:0058) and goes back to the read while it is
// not zero (00:0059): the handler clears it when it has run.
template<class Z> k::Exit publish_frame_wait(Z& z) {
    z.store_a_direct(0x0052);
    return k::fall_through(0x0052,0x0055);
}
template<class Z> k::Exit read_frame_wait(Z& z) {
    z.load_a_direct(0x0055);
    return k::fall_through(0x0055,0x0058);
}
template<class Z> k::Exit test_frame_wait(Z& z) {
    z.or_a(0x0058);
    return k::fall_through(0x0058,0x0059);
}
template<class Z> k::Exit repeat_frame_wait(Z& z) {
    return z.final_relative_if(0x0059,k::Condition::nz);
}
// Select the game-mode table entry and call the table dispatcher.
template<class Z> k::Exit select_game_mode(Z& z) {
    z.load_hl(0x00AF);
    z.load_from_hl(0x00B2,z.r.a);
    z.and_immediate(0x00B3);
    z.load_hl(0x00B5);
    return z.final_call(0x00B8);
}
}
void register_common(RoutineRegistry& registry) {
    // Power-on: interrupts off, interrupt mode 1, the stack (observed by the
    // host), then the reset code at 00:0084 shared with the Reset button.
    auto mask_off=k::core_final_at(0x0000);
    mask_off.interrupt_mask=InterruptMask::disable;
    single(registry,{0,0x0000},{0,0x0000},mask_off,4,[](auto& z) { return z.final_disable_interrupts(0x0000); });
    single(registry,{0,0x0001},{0,0x0000},k::core_final_at(0x0001),8,[](auto& z) {
        z.interrupt_mode_one(0x0001);
        return k::fall_through(0x0001,0x0003);
    });
    single(registry,{0,0x0006},{0,0x0003},k::core_final_at(0x0006),12,[](auto& z) { return z.final_relative(0x0006); });
    auto reset_mask=k::core_final_at(0x0084);
    reset_mask.interrupt_mask=InterruptMask::disable;
    single(registry,{0,0x0084},{0,0x0084},reset_mask,4,[](auto& z) { return z.final_disable_interrupts(0x0084); });
    register_block_transfer(registry,{0,0x00A0});
    register_block_transfer(registry,{0,0x030E});
    for (std::uint16_t at:{0x0003,0x0085}) {
        SegmentExit stack_exit{RoutineExit::core_final,at};
        stack_exit.reset_stack=true;
        single(registry,{0,at},{0,at},stack_exit,10,[at](auto& z) {
            z.load_stack_pointer(at);
            return k::fall_through(at,static_cast<std::uint16_t>(at+3));
        },0,true);
    }
    add(registry,{0,0x0124},k::segment({0,0x0124},{k::conditional_loop_at(0x0126,202)}),[](auto& z) { return begin_video_delay(z); });
    for (std::uint16_t pc:{0x0126,0x0128})
        add(registry,{0,pc},k::segment({0,0x0124},{k::conditional_at(0x012E)}),[pc](auto& z) { return finish_video_delay(z,pc); });
    add(registry,{0,0x0310},k::segment({0,0x0310},{k::core_final_at(0x0312)},{.scratch_stack=2}),[](auto& z) { return wait_after_sprite_clear(z); });
    // After the wait, the loader goes on to the fade-in at 01:7B20.
    single(registry,{0,0x0315},{0,0x0310},k::core_final_at(0x0315),k::jump_cycles,[](auto& z) { return z.final_jump(0x0315); });
    add(registry,{0,0x02FD},k::segment({0,0x02FD},{k::core_final_at(0x030A)}),[](auto& z) { return initialize_sprite_clear(z); });
    // The second table's first byte, which the block copy after it spreads.
    single(registry,{0,0x030C},{0,0x02FD},k::core_final_at(0x030C),10,[](auto& z) {
        z.store_to_hl(0x030C);
        return k::fall_through(0x030C,0x030E);
    });
    add(registry,{0,0x00F4},k::segment({0,0x00F1},{k::returned_at(0x00F8),k::core_final_at(0x00F9)}),[](auto& z) { return poll_pause_flag(z); });
    SegmentExit enable_interrupts{RoutineExit::core_final,0x0157,12};
    enable_interrupts.interrupt_mask=InterruptMask::enable;
    SegmentSpec irq_return{{0,0x014F},{enable_interrupts}};
    irq_return.inherited_stack=12;
    add(registry,{0,0x014F},irq_return,[](auto& z) { return restore_interrupt_registers(z); });
    // The interrupt's return to the interrupted code, right after EI.
    single(registry,{0,0x0158},{0,0x014F},k::returned_at(0x0158),k::return_cycles,[](auto& z) { return z.final_return(0x0158); });
    SegmentExit mapper_exit{RoutineExit::core_final,0x014C,4};
    mapper_exit.cartridge_control=true;
    SegmentSpec mapper_restore{{0,0x0143},{mapper_exit}};
    mapper_restore.slot2_mapper=true;
    mapper_restore.inherited_stack=4;
    add(registry,{0,0x0143},mapper_restore,[](auto& z) { return finish_interrupt_frame(z); });
    add(registry,{0,0x0147},mapper_restore,[](auto& z) { return restore_interrupt_mapper(z); });
    add(registry,{0,0x0318},k::segment({0,0x0318},{k::conditional_at(0x031E)}),[](auto& z) { return begin_video_timing(z); });
    add(registry,{0,0x031B},k::segment({0,0x0318},{k::conditional_at(0x031E)}),[](auto& z) { return sample_video_status(z,0x031B); });
    add(registry,{0,0x0321},k::segment({0,0x0318},{k::conditional_at(0x0324)}),[](auto& z) { return sample_video_status(z,0x0321); });
    add(registry,{0,0x0327},k::segment({0,0x0318},{k::conditional_at(0x032B)}),[](auto& z) { return count_video_timing(z); });
    add(registry,{0,0x032E},k::segment({0,0x0318},{k::returned_at(0x0338)}),[](auto& z) { return finish_video_timing(z); });
    for (std::uint16_t at:{0x011D,0x012A})
        add(registry,{0,at},k::segment({0,at},{k::conditional_at(static_cast<std::uint16_t>(at+4))}),[at](auto& z) { return classify_interrupt_state(z,at); });
    add(registry,{0,0x010F},k::segment({0,0x010F},{k::conditional_at(0x011A)}),[](auto& z) { return check_reset_button(z); });
    SegmentExit control{RoutineExit::core_final,0x010C,-14};
    control.cartridge_control=true;
    SegmentSpec irq_entry{{0,0x00FB},{control}};
    irq_entry.scratch_stack=14;
    add(registry,{0,0x00FB},irq_entry,[](auto& z) { return save_interrupt_context(z); });
    SegmentExit retn{RoutineExit::core_final,0x0082,2};
    retn.return_from_nmi=true;
    SegmentSpec nmi_tail{{0,0x0066},{retn}};
    nmi_tail.inherited_stack=2;
    add(registry,{0,0x007A},nmi_tail,[](auto& z) { return toggle_pause_nmi(z); });
    add(registry,{0,0x0081},nmi_tail,[](auto& z) { return restore_pause_nmi(z); });
    // The NMI vector: save AF (the first instruction after the NMI response,
    // entered with the NMI-reject marker, which its dispatch clears).
    single(registry,{0,0x0066},{0,0x0066},k::core_final_at(0x0066),11,[](auto& z) {
        z.push(0x0066,z.r.a,z.r.f);
        return k::fall_through(0x0066,0x0067);
    },2);
    add(registry,{0,0x0067},k::segment({0,0x0066},{k::conditional_at(0x006C)}),[](auto& z) { return classify_pause_nmi(z); });
    for (std::uint16_t at:{0x006E,0x0072,0x0076})
        add(registry,{0,at},k::segment({0,0x0066},{k::conditional_at(static_cast<std::uint16_t>(at+2))}),[at](auto& z) { return compare_pause_mode(z,at); });
    add(registry,{0,0x0038},k::segment({0,0x0038},{k::core_final_at(0x003B,-2)},{.scratch_stack=2}),[](auto& z) { return acknowledge_video_interrupt(z); });
    SegmentExit clear_seed{RoutineExit::core_final,0x009F};
    clear_seed.wram_seed=0xC000;
    add(registry,{0,0x0096},SegmentSpec{{0,0x0096},{clear_seed}},[](auto& z) { return prepare_work_ram_clear(z); });
    add(registry,{0,0x0088},mapper_reset_spec({0,0x0088},0x008B,0xFFFC,0x80),[](auto& z) { return first_mapper_register(z); });
    add(registry,{0,0x008D},mapper_reset_spec({0,0x008D},0x008E,0xFFFD,0),[](auto& z) { return next_mapper_register(z,0x008D); });
    add(registry,{0,0x0090},mapper_reset_spec({0,0x0090},0x0091,0xFFFE,1),[](auto& z) { return next_mapper_register(z,0x0090); });
    add(registry,{0,0x0093},mapper_reset_spec({0,0x0093},0x0094,0xFFFF,2),[](auto& z) { return next_mapper_register(z,0x0093); });
    add(registry,{0,0x003E},k::segment({0,0x003E},{k::core_final_at(0x0050)},{.scratch_stack=2}),[](auto& z) { return disable_display(z); });
    add(registry,{0,0x0045},k::segment({0,0x0045},{k::core_final_at(0x0050)},{.scratch_stack=2}),[](auto& z) { return enable_display(z); });
    add(registry,{0,0x004A},k::segment({0,0x0045},{k::core_final_at(0x0050)},{.scratch_stack=2}),[](auto& z) { return display_register_tail(z); });
    // The frame wait (00:0052-005B; its return at 00:005B is registered with
    // the other interrupt-dispatch returns in frame_updates.cpp). Each step is
    // its own single-instruction segment (no instruction boundary inside), and
    // the host sees every arrival at the polling step 00:0055.
    auto wait_step=[&](std::uint16_t at, SegmentExit exit, std::uint64_t cycles, bool observed, auto logic) {
        single(registry,{0,at},{0,0x0055},exit,cycles,logic,0,observed);
    };
    wait_step(0x0052,k::core_final_at(0x0052),13,false,[](auto& z) { return publish_frame_wait(z); });
    wait_step(0x0055,k::core_final_at(0x0055),13,true,[](auto& z) { return read_frame_wait(z); });
    wait_step(0x0058,k::core_final_at(0x0058),4,false,[](auto& z) { return test_frame_wait(z); });
    wait_step(0x0059,k::conditional_at(0x0059),12,false,[](auto& z) { return repeat_frame_wait(z); });
    auto spec=k::segment({0,0x00AF},{k::core_final_at(0x00B8)},{.scratch_stack=2});
    spec.observe_entry=true;
    add(registry,{0,0x00AF},std::move(spec),[](auto& z) { return select_game_mode(z); });
}
}
