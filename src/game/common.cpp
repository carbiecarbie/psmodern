// SPDX-License-Identifier: GPL-3.0-or-later
// Common fixed-bank control flow, with host-observed boundaries preserved.
#include "game/common.h"
#include "game/routines.h"
#include "game/menu_kit.h"

namespace psmodern::game {
namespace {
namespace k=menu_kit;
// The loader waits for a frame before handing off to fade-in. Keep the
// original CALL native so its stack and the wait observer remain intact.
template<class Z> k::Exit wait_after_sprite_clear(Z& z) {
    z.load_immediate(0x0310,z.r.a);
    return k::continues(0x0312,k::call_cycles);
}
// Initialize and fill the sprite Y table with the hidden marker. The
// existing 0306 entry still handles callers that arrive at the fill itself.
template<class Z> k::Exit initialize_sprite_clear(Z& z) {
    z.load_hl(0x02FD);
    z.load_de(0x0300);
    z.load_bc(0x0303);
    z.store_to_hl(0x0306);
    z.block_copy(0x0308);
    return k::continues(0x030A,7);
}
// Wait for the pause flag to clear; every iteration remains interruptible.
template<class Z> k::Exit poll_pause_flag(Z& z) {
    z.load_a_direct(0x00F4);
    z.or_a(0x00F7);
    if (z.zero()) return k::returns(0x00F8,k::taken_conditional_return_cycles);
    z.return_not_taken(0x00F8);
    return k::continues(0x00F9,12);
}
// Recover the interrupted registers in stack order. EI executes on the core;
// its following RET remains a separate native instruction with EI delay.
template<class Z> k::Exit restore_interrupt_registers(Z& z) {
    z.pop_index(0x014F,k::Index::iy);
    z.pop_index(0x0151,k::Index::ix);
    z.pop(0x0153,z.r.h,z.r.l);
    z.pop(0x0154,z.r.d,z.r.e);
    z.pop(0x0155,z.r.b,z.r.c);
    z.pop(0x0156,z.r.a,z.r.f);
    SegmentExit exit{RoutineExit::core_final,0x0157,12};
    exit.interrupt_mask=InterruptMask::enable;
    return k::Exit{exit,4};
}
// Restore the slot-2 bank first; the second POP deliberately reads after
// that change. The core performs the final cartridge-control write.
template<class Z> k::Exit restore_interrupt_mapper(Z& z) {
    z.pop(0x0147,z.r.a,z.r.f);
    z.store_a_direct(0x0148);
    z.pop(0x014B,z.r.a,z.r.f);
    SegmentExit exit{RoutineExit::core_final,0x014C,4};
    exit.cartridge_control=true;
    return k::Exit{exit,13};
}
template<class Z> k::Exit finish_interrupt_frame(Z& z) {
    z.xor_a(0x0143);
    z.store_a_direct(0x0144);
    return restore_interrupt_mapper(z);
}
// Sample VDP status once per timing-loop iteration. The branch itself is
// native, allowing events and status side effects between iterations.
template<class Z> k::Exit sample_video_status(Z& z,std::uint16_t at) {
    z.bus().fetch(at);
    const auto low=z.bus().read(static_cast<std::uint16_t>(at+1));
    const auto port=k::pair(z.r.a,low);
    z.r.a=z.bus().in(port,10);
    z.r.wz=static_cast<std::uint16_t>(port+1);
    z.bus().advance(1);
    z.or_a(static_cast<std::uint16_t>(at+2));
    return k::jump_if_exit(static_cast<std::uint16_t>(at+3));
}
template<class Z> k::Exit begin_video_timing(Z& z) {
    z.load_hl(0x0318);
    return sample_video_status(z,0x031B);
}
template<class Z> k::Exit count_video_timing(Z& z) {
    z.increment_hl(0x0327);
    return sample_video_status(z,0x0328);
}
template<class Z> k::Exit finish_video_timing(Z& z) {
    z.xor_a(0x032E);
    z.load_de(0x032F);
    z.subtract_hl_with_carry(0x0332,z.de());
    z.subtract_with_carry(0x0334,z.r.a);
    z.store_a_direct(0x0335);
    return k::returns(0x0338,k::return_cycles);
}
// Start the video-timing delay, retaining the original DJNZ fetches and
// refresh counts. Later iterations remain individually interruptible.
template<class Z> k::Exit begin_video_delay(Z& z) {
    z.load_immediate(0x0124,z.r.b);
    for (unsigned i=0;i<15;++i) z.decrement_jump(0x0126);
    return k::Exit{k::conditional_loop_at(0x0126,202),13};
}
// Select the slow video-timing delay and the pause path in interrupt dispatch.
template<class Z> k::Exit classify_interrupt_state(Z& z,std::uint16_t at) {
    z.load_a_direct(at);
    z.or_a(static_cast<std::uint16_t>(at+3));
    return k::jump_if_exit(static_cast<std::uint16_t>(at+4));
}
// Both DJNZ delays wrap an initial zero count to 256 iterations. An IRQ,
// NMI or frame boundary during the remaining delay resumes at its own loop.
template<class Z> k::Exit finish_video_delay(Z& z,std::uint16_t pc) {
    if (pc==0x0126) while (z.decrement_jump(0x0126)) {}
    while (z.decrement_jump(0x0128)) {}
    return classify_interrupt_state(z,0x012A);
}
// Sample the reset-button edge once during interrupt dispatch. The I/O read
// occurs at the original cycle; the branch stays on the native core.
template<class Z> k::Exit check_reset_button(Z& z) {
    z.bus().fetch(0x010F);
    const auto low=z.bus().read(0x0110);
    const auto port=k::pair(z.r.a,low);
    z.r.a=z.bus().in(port,10);
    z.r.wz=static_cast<std::uint16_t>(port+1);
    z.bus().advance(1);
    z.and_immediate(0x0111);
    z.load_hl(0x0113);
    z.load_from_hl(0x0116,z.r.c);
    z.store_register_to_hl(0x0117,z.r.a);
    z.xor_register(0x0118,z.r.c);
    z.and_register(0x0119,z.r.c);
    return k::jump_if_exit(0x011A);
}
// Save the interrupt-visible registers and mapper state, then let the core
// select cartridge control at the original final instruction.
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
    SegmentExit exit{RoutineExit::core_final,0x010C,-14};
    exit.cartridge_control=true;
    return k::Exit{exit,13};
}
// Pause NMI only changes the flag in the four gameplay modes that allow it.
// Its original conditional branches remain the interruptible boundaries.
template<class Z> k::Exit classify_pause_nmi(Z& z) {
    z.load_a_direct(0x0067);
    z.compare_immediate(0x006A);
    return k::Exit{k::conditional_at(0x006C),z.zero() ? 12u : 7u};
}
template<class Z> k::Exit compare_pause_mode(Z& z,std::uint16_t at) {
    z.compare_immediate(at);
    const auto branch=static_cast<std::uint16_t>(at+2);
    return k::relative_if_exit(branch,at==0x0076 ? !z.zero() : z.zero());
}
// Toggle pause for eligible modes, then restore the AF saved by the NMI.
// RETN itself is always executed by the original core.
template<class Z> k::Exit restore_pause_nmi(Z& z) {
    z.pop(0x0081,z.r.a,z.r.f);
    SegmentExit exit{RoutineExit::core_final,0x0082,2};
    exit.return_from_nmi=true;
    return k::Exit{exit,14};
}
template<class Z> k::Exit toggle_pause_nmi(Z& z) {
    z.load_a_direct(0x007A);
    z.complement_a(0x007D);
    z.store_a_direct(0x007E);
    return restore_pause_nmi(z);
}
// The interrupt vector saves AF and acknowledges VDP status at its
// original port-read cycle, then transfers to the shared interrupt prologue.
template<class Z> k::Exit acknowledge_video_interrupt(Z& z) {
    z.push(0x0038,z.r.a,z.r.f);
    z.bus().fetch(0x0039);
    const auto low=z.bus().read(0x003A);
    const auto port=k::pair(z.r.a,low);
    z.r.a=z.bus().in(port,10);
    z.r.wz=static_cast<std::uint16_t>(port+1);
    z.bus().advance(1);
    return k::continues(0x003B,k::jump_cycles);
}
// Seed the work-RAM clear; the core performs the first write and the
// following block transfer remains separately interruptible.
template<class Z> k::Exit prepare_work_ram_clear(Z& z) {
    z.load_hl(0x0096);
    z.load_de(0x0099);
    z.load_bc(0x009C);
    return k::continues(0x009F,7);
}
// Reset initializes each mapper register in order. The core performs each
// declared immediate write, so the next instruction sees the new mapping.
template<class Z> k::Exit first_mapper_register(Z& z) {
    z.load_hl(0x0088);
    return k::continues(0x008B,10);
}
template<class Z> k::Exit next_mapper_register(Z& z,std::uint16_t at) {
    z.increment_hl(at);
    return k::continues(static_cast<std::uint16_t>(at+1),10);
}
SegmentSpec mapper_reset_spec(RoutineEntry entry,std::uint16_t pc,std::uint16_t address,std::uint8_t value) {
    SegmentExit exit{RoutineExit::core_final,pc};
    exit.mapper_immediate=SegmentExit::MapperImmediate{address,value};
    return SegmentSpec{entry,{exit}};
}
// Change the display bit in the VDP register mirror. The shared tail
// leaves RST 08 native, preserving its stack and the VDP port writes.
template<class Z> k::Exit display_register_tail(Z& z) {
    z.store_a_direct(0x004A);
    z.copy(0x004D,z.r.e,z.r.a);
    z.load_immediate(0x004E,z.r.d);
    return k::continues(0x0050,k::restart_cycles);
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
// One wait iteration is bounded by the original conditional branch. The
// host sees every arrival at the polling address, even when the loop repeats.
template<class Z> k::Exit poll_frame_wait(Z& z) {
    z.load_a_direct(0x0055);
    z.or_a(0x0058);
    return k::relative_if_exit(0x0059,!z.zero());
}
// Select the game-mode table entry; the native CALL performs the dispatch.
template<class Z> k::Exit select_game_mode(Z& z) {
    z.load_hl(0x00AF);
    z.load_from_hl(0x00B2,z.r.a);
    z.and_immediate(0x00B3);
    z.load_hl(0x00B5);
    return k::continues(0x00B8,k::call_cycles);
}
}
void register_common(RoutineRegistry& registry) {
    register_block_transfer(registry,{0,0x00A0});
    register_block_transfer(registry,{0,0x030E});
    for (std::uint16_t at:{0x0003,0x0085}) {
        SegmentExit stack_exit{RoutineExit::core_final,at};
        stack_exit.reset_stack=true;
        SegmentSpec stack_spec{{0,at},{stack_exit}};
        stack_spec.single_instruction=true;
        stack_spec.observe_entry=true;
        registry.add({0,at},std::move(stack_spec),[](const Machine&) { return 10; },
                     [at](RoutineContext& context) { context.finish_core(at); });
    }
    k::add_segment(registry,{0,0x0124},k::segment({0,0x0124},{k::conditional_loop_at(0x0126,202)}),[](auto& z) { return begin_video_delay(z); });
    for (std::uint16_t pc:{0x0126,0x0128})
        k::add_segment(registry,{0,pc},k::segment({0,0x0124},{k::conditional_at(0x012E)}),
                       [pc](auto& z) { return finish_video_delay(z,pc); });
    k::add_segment(registry,{0,0x0310},k::segment({0,0x0310},{k::core_final_at(0x0312)},{.scratch_stack=2}),[](auto& z) { return wait_after_sprite_clear(z); });
    k::add_segment(registry,{0,0x02FD},k::segment({0,0x02FD},{k::core_final_at(0x030A)}),[](auto& z) { return initialize_sprite_clear(z); });
    k::add_segment(registry,{0,0x00F4},k::segment({0,0x00F1},{k::returned_at(0x00F8),k::core_final_at(0x00F9)}),[](auto& z) { return poll_pause_flag(z); });
    SegmentExit enable_interrupts{RoutineExit::core_final,0x0157,12};
    enable_interrupts.interrupt_mask=InterruptMask::enable;
    SegmentSpec irq_return{{0,0x014F},{enable_interrupts}};
    irq_return.inherited_stack=12;
    k::add_segment(registry,{0,0x014F},irq_return,[](auto& z) { return restore_interrupt_registers(z); });
    SegmentExit mapper_exit{RoutineExit::core_final,0x014C,4};
    mapper_exit.cartridge_control=true;
    SegmentSpec mapper_restore{{0,0x0143},{mapper_exit}};
    mapper_restore.slot2_mapper=true;
    mapper_restore.inherited_stack=4;
    k::add_segment(registry,{0,0x0143},mapper_restore,[](auto& z) { return finish_interrupt_frame(z); });
    k::add_segment(registry,{0,0x0147},mapper_restore,[](auto& z) { return restore_interrupt_mapper(z); });
    k::add_segment(registry,{0,0x0318},k::segment({0,0x0318},{k::conditional_at(0x031E)}),[](auto& z) { return begin_video_timing(z); });
    k::add_segment(registry,{0,0x031B},k::segment({0,0x0318},{k::conditional_at(0x031E)}),[](auto& z) { return sample_video_status(z,0x031B); });
    k::add_segment(registry,{0,0x0321},k::segment({0,0x0318},{k::conditional_at(0x0324)}),[](auto& z) { return sample_video_status(z,0x0321); });
    k::add_segment(registry,{0,0x0327},k::segment({0,0x0318},{k::conditional_at(0x032B)}),[](auto& z) { return count_video_timing(z); });
    k::add_segment(registry,{0,0x032E},k::segment({0,0x0318},{k::returned_at(0x0338)}),[](auto& z) { return finish_video_timing(z); });
    for (std::uint16_t at:{0x011D,0x012A})
        k::add_segment(registry,{0,at},k::segment({0,at},{k::conditional_at(static_cast<std::uint16_t>(at+4))}),[at](auto& z) { return classify_interrupt_state(z,at); });
    k::add_segment(registry,{0,0x010F},k::segment({0,0x010F},{k::conditional_at(0x011A)}),[](auto& z) { return check_reset_button(z); });
    SegmentExit control{RoutineExit::core_final,0x010C,-14};
    control.cartridge_control=true;
    SegmentSpec irq_entry{{0,0x00FB},{control}};
    irq_entry.scratch_stack=14;
    k::add_segment(registry,{0,0x00FB},irq_entry,[](auto& z) { return save_interrupt_context(z); });
    SegmentExit retn{RoutineExit::core_final,0x0082,2};
    retn.return_from_nmi=true;
    SegmentSpec nmi_tail{{0,0x0066},{retn}};
    nmi_tail.inherited_stack=2;
    k::add_segment(registry,{0,0x007A},nmi_tail,[](auto& z) { return toggle_pause_nmi(z); });
    k::add_segment(registry,{0,0x0081},nmi_tail,[](auto& z) { return restore_pause_nmi(z); });
    k::add_segment(registry,{0,0x0067},k::segment({0,0x0066},{k::conditional_at(0x006C)}),[](auto& z) { return classify_pause_nmi(z); });
    for (std::uint16_t at:{0x006E,0x0072,0x0076})
        k::add_segment(registry,{0,at},k::segment({0,0x0066},{k::conditional_at(static_cast<std::uint16_t>(at+2))}),[at](auto& z) { return compare_pause_mode(z,at); });
    k::add_segment(registry,{0,0x0038},k::segment({0,0x0038},{k::core_final_at(0x003B,-2)},{.scratch_stack=2}),[](auto& z) { return acknowledge_video_interrupt(z); });
    SegmentExit clear_seed{RoutineExit::core_final,0x009F};
    clear_seed.wram_seed=0xC000;
    k::add_segment(registry,{0,0x0096},SegmentSpec{{0,0x0096},{clear_seed}},[](auto& z) { return prepare_work_ram_clear(z); });
    k::add_segment(registry,{0,0x0088},mapper_reset_spec({0,0x0088},0x008B,0xFFFC,0x80),[](auto& z) { return first_mapper_register(z); });
    k::add_segment(registry,{0,0x008D},mapper_reset_spec({0,0x008D},0x008E,0xFFFD,0),[](auto& z) { return next_mapper_register(z,0x008D); });
    k::add_segment(registry,{0,0x0090},mapper_reset_spec({0,0x0090},0x0091,0xFFFE,1),[](auto& z) { return next_mapper_register(z,0x0090); });
    k::add_segment(registry,{0,0x0093},mapper_reset_spec({0,0x0093},0x0094,0xFFFF,2),[](auto& z) { return next_mapper_register(z,0x0093); });
    k::add_segment(registry,{0,0x003E},k::segment({0,0x003E},{k::core_final_at(0x0050)},{.scratch_stack=2}),[](auto& z) { return disable_display(z); });
    k::add_segment(registry,{0,0x0045},k::segment({0,0x0045},{k::core_final_at(0x0050)},{.scratch_stack=2}),[](auto& z) { return enable_display(z); });
    k::add_segment(registry,{0,0x004A},k::segment({0,0x0045},{k::core_final_at(0x0050)},{.scratch_stack=2}),[](auto& z) { return display_register_tail(z); });
    auto wait_spec=k::segment({0,0x0055},{k::conditional_at(0x0059)});
    wait_spec.observe_entry=true;
    k::add_segment(registry,{0,0x0055},std::move(wait_spec),[](auto& z) { return poll_frame_wait(z); });
    auto spec=k::segment({0,0x00AF},{k::core_final_at(0x00B8)},{.scratch_stack=2});
    spec.observe_entry=true;
    k::add_segment(registry,{0,0x00AF},std::move(spec),[](auto& z) { return select_game_mode(z); });
}
}
