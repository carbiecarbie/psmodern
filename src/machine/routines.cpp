// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/routines.h"
#include "machine/machine.h"
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace psmodern {
void RoutineRegistry::add(RoutineEntry entry, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body) {
    insert(entry,SegmentSpec{entry,{}},std::move(bound),std::move(body));
}
void RoutineRegistry::add(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body) {
    bool valid=!spec.exits.empty() && spec.unit.bank<32 && spec.unit.address<0xc000;
    for (const auto& exit:spec.exits) {
        valid=valid && exit.address<0xc000 && std::count(spec.exits.begin(),spec.exits.end(),exit)==1;
        if (exit.sprite_outputs) valid=valid && exit.sprite_outputs<=8 && entry.bank==1 &&
            entry.address>=0x589e && (entry.address&1)==0 && exit.address<=0x599c &&
            exit.address==entry.address+2*(exit.sprite_outputs-1) && spec.unit==RoutineEntry{1,0x588b} &&
            spec.exits.size()==1 && exit.kind==RoutineExit::core_final && !exit.stack &&
            spec.single_instruction==(exit.sprite_outputs==1) && !spec.force_admit && !spec.slot2_mapper &&
            !spec.absolute_wram_store && !spec.scratch_stack && !spec.inherited_stack && !spec.observe_entry &&
            !exit.conditional && !exit.after && exit.interrupt_mask==InterruptMask::none &&
            !exit.cartridge_control && !exit.wram_seed && !exit.cartridge_flags && !exit.reset_stack &&
            !exit.return_from_nmi && !exit.native_frame_tail && !exit.window_bank_store && !exit.mapper_immediate;
        if (exit.window_bank_store) valid=valid && entry==RoutineEntry{0,0x3A89} &&
            spec.unit==RoutineEntry{0,0x3A83} && exit.kind==RoutineExit::core_final && exit.address==entry.address &&
            spec.exits.size()==1 && spec.single_instruction && !spec.force_admit && !spec.slot2_mapper &&
            !spec.absolute_wram_store && !spec.scratch_stack && !spec.inherited_stack && !spec.observe_entry &&
            !exit.conditional && !exit.after && !exit.stack && exit.interrupt_mask==InterruptMask::none &&
            !exit.cartridge_control && !exit.wram_seed && !exit.cartridge_flags && !exit.reset_stack &&
            !exit.return_from_nmi && !exit.native_frame_tail && !exit.mapper_immediate;
        if (exit.native_frame_tail && exit.kind==RoutineExit::copy_repeat) {
            valid=valid && (entry==RoutineEntry{1,0x61B9} || entry==RoutineEntry{1,0x61C2}) &&
                spec.unit==entry && !spec.observe_entry && !exit.cartridge_control &&
                !exit.wram_seed && !exit.cartridge_flags && !exit.reset_stack &&
                !exit.return_from_nmi && !exit.window_bank_store && !exit.mapper_immediate &&
                !exit.sprite_outputs && exit.interrupt_mask==InterruptMask::none;
        } else if (exit.native_frame_tail) {
            valid=valid && entry==RoutineEntry{0,0x3451} && spec.unit==RoutineEntry{0,0x3439} &&
                spec.exits.size()==1 && exit.kind==RoutineExit::core_final && exit.address==0x3454 &&
                !exit.stack && !exit.conditional && !exit.after && exit.interrupt_mask==InterruptMask::none &&
                !exit.cartridge_control && !exit.wram_seed && !exit.cartridge_flags && !exit.reset_stack &&
                !exit.return_from_nmi && !exit.mapper_immediate && !spec.absolute_wram_store &&
                !spec.slot2_mapper && !spec.force_admit && !spec.single_instruction && !spec.observe_entry &&
                !spec.inherited_stack && !spec.scratch_stack;
        }
        if (exit.interrupt_mask!=InterruptMask::none)
            valid=valid && (exit.interrupt_mask==InterruptMask::enable || exit.interrupt_mask==InterruptMask::disable) &&
                exit.kind==RoutineExit::core_final && !exit.conditional && !exit.after &&
                !spec.force_admit && !spec.slot2_mapper && !spec.absolute_wram_store;
        if (exit.cartridge_control) {
            // Only the fixed-bank interrupt epilogue may combine the earlier
            // slot-2 mapper restore with its final cartridge-control restore.
            const bool irq_restore=spec.slot2_mapper && spec.unit==RoutineEntry{0,0x0143} &&
                (entry==RoutineEntry{0,0x0143} || entry==RoutineEntry{0,0x0147}) &&
                spec.exits.size()==1 && exit.address==0x014c && exit.stack==4 &&
                spec.inherited_stack==4 && !spec.scratch_stack && !spec.single_instruction &&
                !exit.wram_seed && !exit.cartridge_flags && !exit.reset_stack &&
                !exit.return_from_nmi && !exit.mapper_immediate;
            valid=valid && exit.kind==RoutineExit::core_final && !exit.conditional && !exit.after &&
                exit.interrupt_mask==InterruptMask::none && !spec.force_admit &&
                (!spec.slot2_mapper || irq_restore) && !spec.absolute_wram_store;
        }
        if (exit.wram_seed) valid=valid && exit.kind==RoutineExit::core_final &&
            ((exit.seed_target==SeedTarget::work_ram && *exit.wram_seed>=0xc000 && *exit.wram_seed<0xe000) ||
             (exit.seed_target==SeedTarget::cartridge_ram && *exit.wram_seed==0x8000) ||
             (exit.seed_target==SeedTarget::sprite_shadow_zero && *exit.wram_seed==0xc800 &&
              ((entry==RoutineEntry{0,0x0ff3} && spec.unit==entry) ||
               (entry==RoutineEntry{0,0x15dc} && spec.unit==RoutineEntry{0,0x15d9}) ||
               (entry==RoutineEntry{0,0x16d6} && spec.unit==RoutineEntry{0,0x16b2})) &&
              exit.address==entry.address+9 &&
              spec.exits.size()==1 && !exit.stack && !spec.inherited_stack && !spec.scratch_stack &&
              !spec.single_instruction && !spec.observe_entry && !exit.native_frame_tail)) && !exit.cartridge_control &&
            exit.interrupt_mask==InterruptMask::none && !exit.conditional && !exit.after &&
            !spec.force_admit && !spec.slot2_mapper && !spec.absolute_wram_store;
        if (!exit.wram_seed) valid=valid && exit.seed_target==SeedTarget::work_ram;
        if (exit.cartridge_flags) {
            const auto range=*exit.cartridge_flags;
            valid=valid && range.first>=0x8000 && range.first<=range.last && range.last<0xC000 &&
                exit.kind==RoutineExit::core_final && !exit.cartridge_control && !exit.wram_seed &&
                exit.interrupt_mask==InterruptMask::none && !exit.conditional && !exit.after &&
                !spec.force_admit && !spec.slot2_mapper && !spec.absolute_wram_store;
        }
        if (exit.return_from_nmi)
            valid=valid && exit.kind==RoutineExit::core_final && !exit.conditional && !exit.after &&
                exit.interrupt_mask==InterruptMask::none && !exit.cartridge_control && !exit.wram_seed &&
                !exit.cartridge_flags && !exit.reset_stack && !exit.mapper_immediate &&
                !spec.force_admit && !spec.slot2_mapper && !spec.absolute_wram_store && !spec.single_instruction;
        if (exit.reset_stack || exit.mapper_immediate) {
            valid=valid && exit.kind==RoutineExit::core_final && !exit.conditional && !exit.after &&
                exit.interrupt_mask==InterruptMask::none && !exit.cartridge_control && !exit.wram_seed &&
                !exit.cartridge_flags && !exit.return_from_nmi && !spec.force_admit && !spec.slot2_mapper && !spec.absolute_wram_store &&
                !spec.scratch_stack && !spec.inherited_stack && !exit.stack &&
                !(exit.reset_stack && exit.mapper_immediate);
            if (exit.reset_stack) valid=valid && spec.single_instruction && spec.observe_entry;
            if (exit.mapper_immediate) valid=valid && exit.mapper_immediate->address>=0xfffc;
        }
        if (exit.kind==RoutineExit::output_repeat)
            valid=valid && entry==RoutineEntry{0,0x03AA} && !spec.observe_entry;
        if (exit.kind==RoutineExit::copy_repeat || exit.kind==RoutineExit::output_repeat)
            valid=valid && spec.exits.size()==1 && exit.address==entry.address &&
                !exit.stack && !exit.conditional && !exit.after &&
                !spec.single_instruction && !spec.force_admit && !spec.slot2_mapper &&
                !spec.scratch_stack && !spec.inherited_stack && !spec.absolute_wram_store;
        // A single-instruction segment is its final instruction at the entry.
        if (spec.single_instruction) valid=valid && exit.address==entry.address;
    }
    // A core_final exit at the entry address is the whole segment: it must say so.
    for (const auto& exit:spec.exits)
        if (exit.kind==RoutineExit::core_final && exit.address==entry.address) valid=valid && spec.single_instruction;
    // The scratch stack is a few pushes; the bound keeps the dispatch scan short.
    valid=valid && spec.scratch_stack<=max_scratch_stack;
    if (spec.force_admit) valid=valid && spec.single_instruction && spec.exits.size()==1 &&
        spec.exits[0]==SegmentExit{RoutineExit::core_final,entry.address};
    if (spec.absolute_wram_store) valid=valid && spec.single_instruction &&
        *spec.absolute_wram_store>=0xc000 && *spec.absolute_wram_store<0xdfff;
    // Held and inherited stack words: even, within the scratch stack below and
    // the inherited bytes above the entry SP; a return pops no held word.
    const bool irq_register_restore=entry==RoutineEntry{0,0x014f} && spec.unit==entry &&
        spec.inherited_stack==12 && spec.exits.size()==1 &&
        spec.exits[0].kind==RoutineExit::core_final && spec.exits[0].address==0x0157 &&
        spec.exits[0].stack==12 && spec.exits[0].interrupt_mask==InterruptMask::enable &&
        !spec.exits[0].conditional && !spec.exits[0].after && !spec.exits[0].cartridge_control &&
        !spec.exits[0].wram_seed && !spec.exits[0].cartridge_flags && !spec.exits[0].reset_stack &&
        !spec.exits[0].return_from_nmi && !spec.exits[0].mapper_immediate && !spec.scratch_stack &&
        !spec.single_instruction && !spec.force_admit && !spec.slot2_mapper && !spec.absolute_wram_store;
    valid=valid && spec.inherited_stack%2==0 &&
        (spec.inherited_stack<=max_inherited_stack || irq_register_restore);
    if (spec.single_instruction || spec.force_admit) valid=valid && spec.inherited_stack==0;
    for (const auto& exit:spec.exits) {
        const int stack=exit.stack;
        valid=valid && stack%2==0 && stack>=-static_cast<int>(spec.scratch_stack) &&
            stack<=static_cast<int>(spec.inherited_stack);
        if (exit.kind==RoutineExit::returned) valid=valid && stack>=0 && !exit.conditional && exit.after==0;
        // A single instruction has no loop and no branch to declare.
        if (spec.single_instruction || spec.force_admit) valid=valid && stack==0 && !exit.conditional && exit.after==0;
        valid=valid && exit.after<=max_exit_after;
    }
    if (!valid) throw std::invalid_argument("Invalid registered segment");
    insert(entry,std::move(spec),std::move(bound),std::move(body));
}
void RoutineRegistry::insert(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body) {
    if (entry.bank>=32 || entry.address>=0xc000 || !bound || !body)
        throw std::invalid_argument("Invalid registered routine");
    const bool observe=spec.observe_entry;
    if (!routines_.emplace(entry,RegisteredRoutine{std::move(bound),std::move(body),std::move(spec),{}}).second)
        throw std::invalid_argument("Duplicate registered routine");
    if (observe) observed_pcs_.set(entry.address);
}
RegisteredRoutine* RoutineRegistry::find(RoutineEntry entry) {
    auto it=routines_.find(entry); return it==routines_.end() ? nullptr : &it->second;
}
const RegisteredRoutine* RoutineRegistry::find(RoutineEntry entry) const {
    auto it=routines_.find(entry); return it==routines_.end() ? nullptr : &it->second;
}
namespace {
// Length of an allowed plain core_final instruction, or 0 when it is refused:
// prefixes, I/O, memory writes, HALT/EI/DI and every control transfer.
unsigned plain_length(std::uint8_t op) {
    constexpr std::uint8_t refused[]{0x02,0x10,0x12,0x18,0x20,0x22,0x28,0x30,0x32,0x34,0x35,0x36,0x38,
        0xc3,0xc9,0xcb,0xcd,0xd3,0xdb,0xdd,0xe3,0xe9,0xed,0xf3,0xfb,0xfd};
    if (std::find(std::begin(refused),std::end(refused),op)!=std::end(refused)) return 0;
    if ((op&0xf8)==0x70) return 0; // LD (HL),r and HALT
    // Instructions that change SP would end the segment with a different stack.
    if (op==0x31 || op==0x33 || op==0x3b || op==0xf9 || (op&0xcf)==0xc1) return 0; // LD SP,nn, INC/DEC SP, LD SP,HL, POP rr
    // RET cc, JP cc, CALL cc, PUSH and RST; the other columns are listed above.
    if (op>=0xc0 && ((op&7)==0 || (op&7)==2 || (op&7)==4 || (op&7)==5 || (op&7)==7)) return 0;
    if ((op&0xc7)==0x06 || (op&0xc7)==0xc6) return 2;
    if ((op&0xcf)==0x01 || op==0x2a || op==0x3a) return 3;
    return 1;
}
}
RoutineContext::RoutineContext(Machine& m, std::uint64_t bound, const SegmentSpec& spec, bool forced)
    : machine_(m), spec_(spec), entry_(m.cycles()), bound_(bound), stack_(m.cpu().sp.uint16_value),
      initial_bc_(m.cpu().bc.uint16_value), initial_de_(m.cpu().de.uint16_value),
      initial_hl_(m.cpu().hl.uint16_value), forced_(forced) {}
void RoutineContext::active() const {
    if (finished_) throw std::logic_error("Registered routine already returned");
}
void RoutineContext::bus() const {
    active();
    // Nothing precedes the core step of a single-instruction segment.
    if (spec_.single_instruction)
        throw std::runtime_error("Registered single-instruction segment cannot use the bus or advance time");
}
void RoutineContext::check_bound() const {
    if (elapsed()>bound_) throw std::runtime_error("Registered routine exceeded its declared cycle bound");
}
// A forced body may adjust registers again after its core step.
Z80& RoutineContext::cpu() { if (!forced_) active(); return machine_.cpu(); }
std::uint64_t RoutineContext::elapsed() const { return machine_.cycles()-entry_; }
void RoutineContext::advance(std::uint64_t cycles) {
    bus();
    if (elapsed()>bound_ || cycles>bound_-elapsed())
        throw std::runtime_error("Registered routine exceeded its declared cycle bound");
    machine_.advance(cycles);
}
std::uint8_t RoutineContext::read(std::uint16_t address) { bus(); return machine_.read(address); }
void RoutineContext::write(std::uint16_t address, std::uint8_t value) {
    bus();
    if (address>=0xfffc && !(address==0xffff && spec_.slot2_mapper))
        throw std::runtime_error("Registered routine cannot write the mapper");
    if (address==0xffff && spec_.slot2_mapper && spec_.exits.size()==1 && spec_.exits.front().cartridge_control) {
        if (irq_mapper_written_) throw std::runtime_error("Interrupt mapper may be restored only once");
        irq_mapper_written_=true;
    }
    machine_.write(address,value);
}
void RoutineContext::account_fetches(std::uint64_t count) {
    bus();
    auto& r=machine_.cpu().r;
    r=static_cast<std::uint8_t>((r&0x80)|((r+count)&0x7f));
    machine_.opcode_fetches_+=count;
}
std::uint8_t RoutineContext::fetch_opcode(std::uint16_t address) {
    const auto value=read(address); account_fetches(1); return value;
}
void RoutineContext::at_offset(std::uint64_t offset) {
    bus();
    if (offset<elapsed()) throw std::invalid_argument("Registered routine port offset is in the past");
    advance(offset-elapsed());
}
std::uint8_t RoutineContext::read_port(std::uint16_t port, std::uint64_t offset) {
    at_offset(offset); return machine_.read_port(port,machine_.cycles());
}
std::uint8_t RoutineContext::input_increment(std::uint16_t port, std::uint16_t destination) {
    bus();
    // INI exposes its I/O read at T12, but the original memory observer
    // sees the store at the instruction entry. Keep this operation atomic.
    if (elapsed()>bound_ || 16>bound_-elapsed())
        throw std::runtime_error("Registered routine exceeded its declared cycle bound");
    if (destination>=0xfffc && !(destination==0xffff && spec_.slot2_mapper))
        throw std::runtime_error("Registered routine cannot write the mapper");
    const auto value=machine_.read_port(port,machine_.cycles()+12);
    write(destination,value);
    advance(16);
    return value;
}
void RoutineContext::write_port(std::uint16_t port, std::uint8_t value, std::uint64_t offset) {
    bus();
    const auto low=static_cast<std::uint8_t>(port);
    if (low<0x40 && !(low&1)) throw std::runtime_error("Registered routine cannot change memory mapping");
    if (low>=0x80 && low<0xc0 && (low&1)) {
        auto reg=machine_.vdp().control_write_register(value);
        if (reg && (*reg==0 || *reg==1 || *reg==10))
            throw std::runtime_error("Registered routine cannot write an interrupt control register");
    }
    at_offset(offset); machine_.write_port(port,value,machine_.cycles());
}
SegmentExit RoutineContext::prepare_final(SegmentExit exit) {
    active();
    // Exits are found by kind and address; the declaration found carries the
    // stack offset, conditional flag and loop threshold.
    if (spec_.exits.empty() && exit.kind==RoutineExit::returned) return exit;
    const auto found=std::find(spec_.exits.begin(),spec_.exits.end(),exit);
    if (found==spec_.exits.end()) throw std::runtime_error("Registered routine exit is not declared");
    return *found;
}
void RoutineContext::final_step() {
    // Admission keeps interrupts out of the window: the core must execute the
    // final instruction itself, never an interrupt response.
    if (machine_.interrupt_accepted_at_boundary())
        throw std::runtime_error("Registered routine reached an accepted interrupt before its final instruction");
    stepped_=true;
    do { machine_.step(); check_bound(); } while (machine_.cpu().resume==Z80_RESUME_XY);
}
void RoutineContext::finish_return(std::uint16_t address) {
    const auto exit=prepare_final({RoutineExit::returned,address});
    const auto before=static_cast<std::uint16_t>(stack_+exit.stack);
    // Validate the instruction without bus side effects; the core performs all
    // real fetches and stack reads below. Ignored index prefixes are supported.
    auto opcode_address=address;
    auto peek=[&](std::uint16_t pc) {
        if (machine_.memory().mapping(pc).kind!=Memory::Kind::rom)
            throw std::runtime_error("Registered routine return must be in ROM");
        return machine_.memory().read(pc);
    };
    auto code=peek(opcode_address);
    std::uint64_t prefixes=0;
    while ((code&0xdf)==0xdd) {
        if (++prefixes>bound_/4) throw std::runtime_error("Registered routine return exceeds bound");
        code=peek(++opcode_address);
    }
    bool returning=code==0xc9;
    if ((code&0xc7)==0xc0) {
        constexpr unsigned bits[]{6,0,2,7};
        const auto condition=(code>>3)&7;
        const auto flag=(machine_.cpu().af.uint16_value>>bits[condition/2])&1;
        returning=flag==(condition&1);
    }
    if (!returning || machine_.cpu().sp.uint16_value!=before)
        throw std::runtime_error(exit.stack ? "Registered routine must execute its returning instruction at its declared stack offset" :
            "Registered routine must execute its returning instruction with the entry stack");
    machine_.cpu().pc.uint16_value=address;
    final_step();
    // The validated taken return performs the real bus reads, including open
    // bus if the stack is unmapped. Peeking Memory here would predict a
    // different target in that case; the core alone determines the caller PC.
    if (machine_.cpu().sp.uint16_value!=static_cast<std::uint16_t>(before+2))
        throw std::runtime_error("Registered routine did not return to its caller");
    exit_=exit;
    finished_=true;
}
void RoutineContext::finish_core(std::uint16_t address) {
    const auto exit=prepare_final({RoutineExit::core_final,address});
    if (machine_.memory().mapping(address).kind!=Memory::Kind::rom)
        throw std::runtime_error("Registered routine final instruction must be in ROM");
    const auto code=machine_.memory().read(address);
    if (exit.native_frame_tail && (code!=0x01 || elapsed()!=10 ||
        entry_/Machine::frame_cycles!=machine_.cycles()/Machine::frame_cycles))
        throw std::runtime_error("Dialogue row final load must start before the frame boundary");
    if (exit.sprite_outputs) {
        const auto previous=exit.sprite_outputs-1;
        const auto& cpu=machine_.cpu();
        if (elapsed()!=16u*previous || cpu.hl.uint16_value!=static_cast<std::uint16_t>(initial_hl_+previous) ||
            cpu.bc.uint16_value!=static_cast<std::uint16_t>(initial_bc_-256u*previous) ||
            (cpu.bc.uint16_value&255)!=0xbe || code!=0xed || machine_.peek(address+1)!=0xa3)
            throw std::runtime_error("Sprite output slice did not reach its last OUTI in the expected state");
    }
    const bool mask=exit.interrupt_mask!=InterruptMask::none;
    if (mask && code!=static_cast<std::uint8_t>(exit.interrupt_mask))
        throw std::runtime_error("Declared interrupt mask does not match the mapped instruction");
    const bool call=code==0xcd, call_cc=(code&0xc7)==0xc4, jump=code==0xc3, restart=code==0xcf;
    const bool jump_relative=code==0x18 && !spec_.force_admit && !exit.conditional;
    const bool jump_hl=code==0xe9 && !spec_.force_admit;
    // Conditional transfers, accepted only at an exit declared conditional:
    // JP cc,nn; JR cc,e and DJNZ (relative, displacement peeked below).
    const bool jump_cc=(code&0xc7)==0xc2, relative=code==0x10 || code==0x20 || code==0x28 || code==0x30 || code==0x38;
    const auto operand=jump_hl ? machine_.cpu().hl.uint16_value :
        static_cast<std::uint16_t>(machine_.peek(static_cast<std::uint16_t>(address+1))|
        (machine_.peek(static_cast<std::uint16_t>(address+2))<<8));
    const bool store=spec_.absolute_wram_store && code==0x22 && operand==*spec_.absolute_wram_store &&
        machine_.memory().mapping(operand).kind==Memory::Kind::ram &&
        machine_.memory().mapping(static_cast<std::uint16_t>(operand+1)).kind==Memory::Kind::ram;
    if (exit.cartridge_control && spec_.slot2_mapper && !irq_mapper_written_)
        throw std::runtime_error("Interrupt mapper must be restored before cartridge control");
    const bool cartridge_control=exit.cartridge_control && code==0x32 && operand==0xfffc &&
        machine_.memory().mapping(static_cast<std::uint16_t>(address+1)).kind==Memory::Kind::rom &&
        machine_.memory().mapping(static_cast<std::uint16_t>(address+2)).kind==Memory::Kind::rom;
    if (exit.cartridge_control && !cartridge_control)
        throw std::runtime_error("Registered cartridge control exit must store A to FFFC");
    const bool sprite_seed=exit.seed_target==SeedTarget::sprite_shadow_zero;
    const auto mapped_code=Memory::Mapping{Memory::Kind::rom,0};
    if (exit.window_bank_store && (code!=0x32 || operand!=0xFFFF ||
        machine_.memory().mapping(address)!=mapped_code || machine_.memory().mapping(address+1)!=mapped_code ||
        machine_.memory().mapping(address+2)!=mapped_code))
        throw std::runtime_error("Registered window bank store must execute LD (FFFF),A at bank 0:3A89");
    const bool seed_opcode=sprite_seed ?
        code==0x36 && machine_.memory().mapping(address)==mapped_code &&
        machine_.memory().mapping(address+1)==mapped_code && machine_.peek(address+1)==0 &&
        elapsed()==30 && machine_.cpu().de.uint16_value==0xc801 && machine_.cpu().bc.uint16_value==0x00ff :
        code==0x75;
    const bool seed=exit.wram_seed && seed_opcode && machine_.cpu().hl.uint16_value==*exit.wram_seed &&
        machine_.memory().mapping(*exit.wram_seed).kind==
            (exit.seed_target==SeedTarget::cartridge_ram ? Memory::Kind::cartridge_ram : Memory::Kind::ram);
    if (exit.wram_seed && !seed) throw std::runtime_error("Registered seed exit does not match its declared opcode, value or mapped address");
    const auto hl=machine_.cpu().hl.uint16_value;
    const bool publish=exit.cartridge_flags && code==0x36 &&
        machine_.memory().mapping(static_cast<std::uint16_t>(address+1)).kind==Memory::Kind::rom &&
        machine_.peek(static_cast<std::uint16_t>(address+1))==1 &&
        hl>=exit.cartridge_flags->first && hl<=exit.cartridge_flags->last &&
        machine_.memory().mapping(hl).kind==Memory::Kind::cartridge_ram;
    if (exit.cartridge_flags && !publish)
        throw std::runtime_error("Registered save publication must set one mapped cartridge flag");
    const bool retn=exit.return_from_nmi && code==0xed &&
        machine_.memory().mapping(static_cast<std::uint16_t>(address+1)).kind==Memory::Kind::rom &&
        machine_.memory().read(static_cast<std::uint16_t>(address+1))==0x45;
    if (exit.return_from_nmi && !retn)
        throw std::runtime_error("NMI return must be exact ED 45 in ROM");
    const bool reset_stack=exit.reset_stack && code==0x31 && operand==0xcb00 &&
        machine_.memory().mapping(static_cast<std::uint16_t>(address+1)).kind==Memory::Kind::rom &&
        machine_.memory().mapping(static_cast<std::uint16_t>(address+2)).kind==Memory::Kind::rom &&
        machine_.memory().mapping(0xcb00).kind==Memory::Kind::ram;
    if (exit.reset_stack && !reset_stack)
        throw std::runtime_error("Reset stack must load the declared mapped address");
    const bool mapper_immediate=exit.mapper_immediate && code==0x36 &&
        machine_.memory().mapping(static_cast<std::uint16_t>(address+1)).kind==Memory::Kind::rom &&
        hl==exit.mapper_immediate->address && machine_.peek(static_cast<std::uint16_t>(address+1))==exit.mapper_immediate->value;
    if (exit.mapper_immediate && !mapper_immediate)
        throw std::runtime_error("Mapper immediate must match the declared address and value");
    const unsigned length=exit.conditional ? (jump_cc ? 3 : relative ? 2 : 0) :
        call || call_cc || jump || store || cartridge_control || exit.window_bank_store || reset_stack ? 3 : exit.sprite_outputs || sprite_seed || publish || jump_relative || mapper_immediate || retn ? 2 :
        restart || jump_hl || mask || seed ? 1 : plain_length(code);
    if (spec_.absolute_wram_store && !store) throw std::runtime_error("Registered final store must target its declared work RAM address");
    if (jump_relative && machine_.memory().mapping(static_cast<std::uint16_t>(address+1)).kind!=Memory::Kind::rom)
        throw std::runtime_error("Registered relative jump operand must be in ROM");
    if (!length) throw std::runtime_error("Registered routine final instruction is not allowed");
    const auto before=static_cast<std::uint16_t>(stack_+exit.stack);
    if (machine_.cpu().sp.uint16_value!=before)
        throw std::runtime_error(exit.stack ? "Registered routine must execute its final instruction at its declared stack offset" :
            "Registered routine must execute its final instruction with the entry stack");
    // Text calls the native VDP address helper with RST 08. The core owns
    // its fetch, timing and stack writes, just as for CALL. Declare the two
    // extra pushed bytes as scratch, including any words held by the body.
    // Other RST vectors and forced admission remain outside this contract.
    if (restart) {
        if (spec_.force_admit || spec_.scratch_stack<static_cast<unsigned>(std::max(0,2-static_cast<int>(exit.stack))))
            throw std::runtime_error("Registered RST 08 requires an ordinary segment with declared stack space");
        for (unsigned depth=1;depth<=2;++depth) {
            const auto pushed=static_cast<std::uint16_t>(before-depth);
            if (pushed>=0xfffc || machine_.memory().mapping(pushed).kind==Memory::Kind::unmapped)
                throw std::runtime_error("Registered RST 08 stack is unmapped or overlaps mapper registers");
        }
    }
    // A loop exit is taken at the first arrival with its declared cycles
    // elapsed; the verifier stops the original copy by the same rule.
    if (elapsed()<exit.after)
        throw std::runtime_error("Registered routine took its loop exit before its declared cycles");
    const auto return_sp=machine_.cpu().sp.uint16_value;
    const auto return_sp_high=static_cast<std::uint16_t>(return_sp+1);
    if (retn && (return_sp>=0xfffc || return_sp_high>=0xfffc ||
                 machine_.memory().mapping(return_sp).kind==Memory::Kind::unmapped ||
                 machine_.memory().mapping(return_sp_high).kind==Memory::Kind::unmapped))
        throw std::runtime_error("NMI return stack is unmapped or overlaps mapper registers");
    const auto return_target=retn ? static_cast<std::uint16_t>(machine_.memory().read(return_sp) |
        (machine_.memory().read(return_sp_high)<<8)) : 0;
    const auto saved_iff2=machine_.cpu().iff2;
    // Side-effect-free operand peek; the core performs the real fetches.
    const auto target=restart ? std::uint16_t{0x0008} :
        relative || jump_relative ? static_cast<std::uint16_t>(address+2+static_cast<std::int8_t>(operand&0xff)) : operand;
    machine_.cpu().pc.uint16_value=address;
    final_step();
    const auto pc=machine_.cpu().pc.uint16_value, sp=machine_.cpu().sp.uint16_value;
    if (exit.sprite_outputs && (machine_.cpu().hl.uint16_value!=static_cast<std::uint16_t>(initial_hl_+exit.sprite_outputs) ||
        machine_.cpu().bc.uint16_value!=static_cast<std::uint16_t>(initial_bc_-256u*exit.sprite_outputs)))
        throw std::runtime_error("Sprite output slice finished with the wrong source or count");
    const bool taken=pc==target && sp==static_cast<std::uint16_t>(before-2);
    const bool next=pc==static_cast<std::uint16_t>(address+length) && sp==(reset_stack ? 0xcb00 : before);
    const bool returned_nmi=retn && pc==return_target && sp==static_cast<std::uint16_t>(before+2) &&
        machine_.cpu().iff1==saved_iff2;
    const bool reached=retn ? returned_nmi : exit.conditional ? sp==before && (pc==target || next) :
        call || restart ? taken : call_cc ? taken || next : jump || jump_hl || jump_relative ? pc==target && sp==before : next;
    if (!reached) throw std::runtime_error("Registered routine final instruction did not reach its declared result");
    exit_=exit;
    finished_=true;
}

void RoutineContext::finish_copy(std::uint16_t address) {
    const auto exit=prepare_final({RoutineExit::copy_repeat,address});
    const unsigned before_final=copy_count()-1;
    auto& cpu=machine_.cpu();
    if (elapsed()!=21*before_final || cpu.sp.uint16_value!=stack_ ||
        cpu.de.uint16_value!=static_cast<std::uint16_t>(initial_de_+before_final) ||
        cpu.hl.uint16_value!=static_cast<std::uint16_t>(initial_hl_+before_final) ||
        cpu.bc.uint16_value!=static_cast<std::uint16_t>(initial_bc_-before_final))
        throw std::runtime_error("Block copy reached its last transfer in the wrong state");
    if (exit.native_frame_tail && elapsed() &&
        entry_/Machine::frame_cycles!=machine_.cycles()/Machine::frame_cycles)
        throw std::runtime_error("Scene copy final transfer must start before the frame boundary");
    cpu.pc.uint16_value=address;
    final_step();
    const auto remaining=static_cast<std::uint16_t>(initial_bc_-before_final-1);
    if (cpu.bc.uint16_value!=remaining || cpu.sp.uint16_value!=stack_ ||
        cpu.pc.uint16_value!=(remaining ? address : address+2))
        throw std::runtime_error("Block copy did not reach its next transfer or continuation");
    exit_=exit;
    finished_=true;
}
void RoutineContext::finish_output(std::uint16_t address) {
    const auto exit=prepare_final({RoutineExit::output_repeat,address});
    const unsigned prior=bound_>21 ? 1u : 0u;
    const auto before=static_cast<std::uint16_t>(initial_bc_-prior*256);
    auto& cpu=machine_.cpu();
    if (elapsed()!=21*prior || cpu.sp.uint16_value!=stack_ || cpu.bc.uint16_value!=before ||
        cpu.hl.uint16_value!=static_cast<std::uint16_t>(initial_hl_+prior))
        throw std::runtime_error("Video register output reached its last write in the wrong state");
    cpu.pc.uint16_value=address;
    final_step();
    const auto after=static_cast<std::uint16_t>(before-256);
    if (cpu.bc.uint16_value!=after || cpu.sp.uint16_value!=stack_ ||
        cpu.hl.uint16_value!=static_cast<std::uint16_t>(initial_hl_+prior+1) ||
        cpu.pc.uint16_value!=(after>>8 ? address : address+2))
        throw std::runtime_error("Video register output did not reach its next write or continuation");
    exit_=exit;
    finished_=true;
}
}
