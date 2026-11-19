// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/routines.h"
#include "machine/machine.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace psmodern {
void RoutineRegistry::validate(RoutineEntry entry, const SegmentSpec& spec) const {
    bool valid=!spec.exits.empty() && spec.unit.bank<32 && spec.unit.address<0xc000;
    for (const auto& exit:spec.exits) {
        valid=valid && exit.address<0xc000 && std::count(spec.exits.begin(),spec.exits.end(),exit)==1;
        if (exit.sprite_outputs) valid=valid && exit.sprite_outputs<=8 && entry.bank==1 &&
            entry.address>=0x589e && (entry.address&1)==0 && exit.address<=0x599c &&
            exit.address==entry.address+2*(exit.sprite_outputs-1) && spec.unit==RoutineEntry{1,0x588b} &&
            spec.exits.size()==1 && exit.kind==RoutineExit::core_final && !exit.stack &&
            spec.single_instruction==(exit.sprite_outputs==1) && !spec.slot2_mapper &&
            !spec.scratch_stack && !spec.inherited_stack && !spec.observe_entry &&
            !exit.conditional && !exit.after && exit.interrupt_mask==InterruptMask::none &&
            !exit.cartridge_control && !exit.wram_seed && !exit.cartridge_flags && !exit.reset_stack &&
            !exit.return_from_nmi && !exit.native_frame_tail && !exit.window_bank_store && !exit.mapper_immediate;
        if (exit.window_bank_store) valid=valid && entry==RoutineEntry{0,0x3A89} &&
            spec.unit==RoutineEntry{0,0x3A83} && exit.kind==RoutineExit::core_final && exit.address==entry.address &&
            spec.exits.size()==1 && spec.single_instruction && !spec.slot2_mapper &&
            !spec.scratch_stack && !spec.inherited_stack && !spec.observe_entry &&
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
                !exit.return_from_nmi && !exit.mapper_immediate &&
                !spec.slot2_mapper && !spec.single_instruction && !spec.observe_entry &&
                !spec.inherited_stack && !spec.scratch_stack;
        }
        if (exit.interrupt_mask!=InterruptMask::none)
            valid=valid && (exit.interrupt_mask==InterruptMask::enable || exit.interrupt_mask==InterruptMask::disable) &&
                exit.kind==RoutineExit::core_final && !exit.conditional && !exit.after &&
                !spec.slot2_mapper;
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
                exit.interrupt_mask==InterruptMask::none &&
                (!spec.slot2_mapper || irq_restore);
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
            !spec.slot2_mapper;
        if (!exit.wram_seed) valid=valid && exit.seed_target==SeedTarget::work_ram;
        if (exit.cartridge_flags) {
            const auto range=*exit.cartridge_flags;
            valid=valid && range.first>=0x8000 && range.first<=range.last && range.last<0xC000 &&
                exit.kind==RoutineExit::core_final && !exit.cartridge_control && !exit.wram_seed &&
                exit.interrupt_mask==InterruptMask::none && !exit.conditional && !exit.after &&
                !spec.slot2_mapper;
        }
        if (exit.return_from_nmi)
            valid=valid && exit.kind==RoutineExit::core_final && !exit.conditional && !exit.after &&
                exit.interrupt_mask==InterruptMask::none && !exit.cartridge_control && !exit.wram_seed &&
                !exit.cartridge_flags && !exit.reset_stack && !exit.mapper_immediate &&
                !spec.slot2_mapper && !spec.single_instruction;
        if (exit.reset_stack || exit.mapper_immediate) {
            valid=valid && exit.kind==RoutineExit::core_final && !exit.conditional && !exit.after &&
                exit.interrupt_mask==InterruptMask::none && !exit.cartridge_control && !exit.wram_seed &&
                !exit.cartridge_flags && !exit.return_from_nmi && !spec.slot2_mapper &&
                !spec.scratch_stack && !spec.inherited_stack && !exit.stack &&
                !(exit.reset_stack && exit.mapper_immediate);
            if (exit.reset_stack) valid=valid && spec.single_instruction && spec.observe_entry;
            if (exit.mapper_immediate) valid=valid && exit.mapper_immediate->address>=0xfffc;
        }
        if (exit.kind==RoutineExit::output_repeat)
            valid=valid && !spec.observe_entry; // 00:03AA (legacy) or a rendered OTIR slice
        if (exit.kind==RoutineExit::copy_repeat || exit.kind==RoutineExit::output_repeat)
            valid=valid && spec.exits.size()==1 && exit.address==entry.address &&
                !exit.stack && !exit.conditional && !exit.after &&
                !spec.single_instruction && !spec.slot2_mapper &&
                !spec.scratch_stack && !spec.inherited_stack;
        // A single-instruction segment is its final instruction at the entry.
        if (spec.single_instruction) valid=valid && exit.address==entry.address;
    }
    // A core_final exit at the entry address is the whole segment: it must say so.
    for (const auto& exit:spec.exits)
        if (exit.kind==RoutineExit::core_final && exit.address==entry.address) valid=valid && spec.single_instruction;
    // The scratch stack is a few pushes; the bound keeps the dispatch scan short.
    valid=valid && spec.scratch_stack<=max_scratch_stack;
    // Held and inherited stack words: even, within the scratch stack below and
    // the inherited bytes above the entry SP; a return pops no held word.
    const bool irq_register_restore=entry==RoutineEntry{0,0x014f} && spec.unit==entry &&
        spec.inherited_stack==12 && spec.exits.size()==1 &&
        spec.exits[0].kind==RoutineExit::core_final && spec.exits[0].address==0x0157 &&
        spec.exits[0].stack==12 && spec.exits[0].interrupt_mask==InterruptMask::enable &&
        !spec.exits[0].conditional && !spec.exits[0].after && !spec.exits[0].cartridge_control &&
        !spec.exits[0].wram_seed && !spec.exits[0].cartridge_flags && !spec.exits[0].reset_stack &&
        !spec.exits[0].return_from_nmi && !spec.exits[0].mapper_immediate && !spec.scratch_stack &&
        !spec.single_instruction && !spec.slot2_mapper;
    valid=valid && spec.inherited_stack%2==0 &&
        (spec.inherited_stack<=max_inherited_stack || irq_register_restore);
    if (spec.single_instruction) valid=valid && spec.inherited_stack==0;
    for (const auto& exit:spec.exits) {
        const int stack=exit.stack;
        valid=valid && stack%2==0 && stack>=-static_cast<int>(spec.scratch_stack) &&
            stack<=static_cast<int>(spec.inherited_stack);
        if (exit.kind==RoutineExit::returned) valid=valid && stack>=0 && !exit.conditional && exit.after==0;
        // A single instruction has no loop to declare (it may be a
        // conditional transfer).
        if (spec.single_instruction) valid=valid && stack==0 && exit.after==0;
        valid=valid && exit.after<=max_exit_after;
    }
    if (!valid) throw std::invalid_argument("Invalid registered segment");
}
namespace {
std::uint64_t next_generation() {
    static std::uint64_t generation=0;
    return ++generation;
}
}
RoutineRegistry::RoutineRegistry(const RoutineRegistry& o)
    : routines_(o.routines_), observed_pcs_(o.observed_pcs_), census_(o.census_),
      generation_(next_generation()) {}
RoutineRegistry& RoutineRegistry::operator=(const RoutineRegistry& o) {
    if (this!=&o) {
        if (pinned_) retired_.push_back(std::move(routines_));
        routines_=o.routines_; observed_pcs_=o.observed_pcs_; census_=o.census_;
        generation_=next_generation();
    }
    return *this;
}
RoutineRegistry::RoutineRegistry(RoutineRegistry&& o) noexcept
    : routines_(std::move(o.routines_)), observed_pcs_(o.observed_pcs_),
      census_(std::move(o.census_)), generation_(next_generation()) {}
RoutineRegistry& RoutineRegistry::operator=(RoutineRegistry&& o) noexcept {
    if (this!=&o) {
        // std::map nodes keep their addresses when the map moves, so a body
        // suspended at depth keeps a valid routine until the pin is released.
        if (pinned_) retired_.push_back(std::move(routines_));
        routines_=std::move(o.routines_); observed_pcs_=o.observed_pcs_;
        census_=std::move(o.census_); generation_=next_generation();
    }
    return *this;
}
void RoutineRegistry::add(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body,
                          std::source_location where) {
    validate(entry,spec);
    insert(entry,std::move(spec),std::move(bound),std::move(body),false,std::nullopt,where);
}
void RoutineRegistry::add_exact(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body,
                                std::source_location where) {
    validate(entry,spec);
    insert(entry,std::move(spec),std::move(bound),std::move(body),true,std::nullopt,where);
}
void RoutineRegistry::add_exact(RoutineEntry entry, SegmentSpec spec, EntryGuard guard, RegisteredRoutine::Bound bound,
                                RegisteredRoutine::Body body, std::source_location where) {
    validate(entry,spec);
    if (!guard.holds || guard.name.empty()) throw std::invalid_argument("Invalid entry guard");
    insert(entry,std::move(spec),std::move(bound),std::move(body),true,std::move(guard),where);
}
namespace {
// The file name of a registration site without its directories.
std::string_view file_name(const std::source_location& where) {
    const std::string_view path=where.file_name();
    const auto slash=path.find_last_of("/\\");
    return slash==std::string_view::npos ? path : path.substr(slash+1);
}
}
void RoutineRegistry::insert(RoutineEntry entry, SegmentSpec spec, RegisteredRoutine::Bound bound, RegisteredRoutine::Body body, bool exact,
                             std::optional<EntryGuard> guard, std::source_location where) {
    if (entry.bank>=32 || entry.address>=0xc000 || !bound || !body)
        throw std::invalid_argument("Invalid registered routine");
    const bool observe=spec.observe_entry;
    const auto exits=std::max<std::size_t>(1,spec.exits.size());
    exact=exact || spec.single_instruction;
    RegisteredRoutine routine{std::move(bound),std::move(body),std::move(spec),{},exact,{},
        std::vector<std::uint64_t>(exits),std::move(guard),file_name(where)};
    if (!routines_.emplace(entry,std::move(routine)).second)
        throw std::invalid_argument("Duplicate registered routine");
    if (observe) observed_pcs_.set(entry.address);
    generation_=next_generation();
}
RegisteredRoutine* RoutineRegistry::find(RoutineEntry entry) {
    auto it=routines_.find(entry); return it==routines_.end() ? nullptr : &it->second;
}
const RegisteredRoutine* RoutineRegistry::find(RoutineEntry entry) const {
    auto it=routines_.find(entry); return it==routines_.end() ? nullptr : &it->second;
}
RoutineContext::SyncScope::SyncScope(RoutineContext& context, RegisterSync& sync)
    : context_(context), previous_(context.sync_) {
    context.sync_=&sync;
    context.q_=sync.q_register();
    if (!context.exact_) ++context.routine_.counters.unused_sync;
}
RoutineContext::SyncScope::~SyncScope() {
    context_.sync_=previous_;
    context_.q_=previous_ ? previous_->q_register() : nullptr;
}
RoutineContext::RoutineContext(Machine& m, RegisteredRoutine& routine, std::uint64_t bound, bool exact)
    : machine_(m), routine_(routine), spec_(routine.spec), entry_(m.cycles()), bound_(bound),
      exact_(exact), instruction_start_(m.cycles()),
      entry_pc_(m.cpu().pc.uint16_value), entry_bank_(m.memory().mapping(m.cpu().pc.uint16_value).bank),
      checking_opcodes_(m.opcode_check_!=nullptr) {}
void RoutineContext::active() const {
    if (finished_) throw std::logic_error("Registered routine already returned");
}
void RoutineContext::bus() const {
    // A single-instruction segment renders its one instruction (no boundary
    // can fall inside it; fetch_opcode checks that there is no second one).
    active();
}
// The bound is the estimating dry run of the path. A handler delivered inside
// the body can change what the body reads afterwards, so an exact body is
// held to it only while nothing was delivered.
void RoutineContext::check_bound() const {
    // After a delivery the bound may no longer hold (the handler can change
    // what the body reads), but a runaway body is still refused.
    if (delivered_) {
        if (elapsed()>runaway_cap()) throw std::runtime_error("Registered routine ran away after an interrupt delivery");
        return;
    }
    if (elapsed()>bound_) throw std::runtime_error("Registered routine exceeded its declared cycle bound");
}
Z80& RoutineContext::cpu() { active(); return machine_.cpu(); }
std::uint64_t RoutineContext::elapsed() const { return machine_.cycles()-entry_-nested_cycles_; }
void RoutineContext::advance(std::uint64_t cycles) {
    bus();
    if (!delivered_ && (elapsed()>bound_ || cycles>bound_-elapsed()))
        throw std::runtime_error("Registered routine exceeded its declared cycle bound");
    if (delivered_ && elapsed()+cycles>runaway_cap())
        throw std::runtime_error("Registered routine ran away after an interrupt delivery");
    machine_.advance(cycles);
}
std::uint8_t RoutineContext::read(std::uint16_t address) { bus(); return machine_.read(address); }
// The mapper registers, port 3E and the video interrupt registers were
// refused while admission predicted the window of a whole segment; an exact
// body meets interrupts at its boundaries, so it may change all of them.
void RoutineContext::write(std::uint16_t address, std::uint8_t value) {
    bus();
    if (!exact_ && address>=0xfffc && !(address==0xffff && spec_.slot2_mapper))
        throw std::runtime_error("Registered routine cannot write the mapper");
    if (address==0xffff && spec_.slot2_mapper && spec_.exits.size()==1 && spec_.exits.front().cartridge_control) {
        if (irq_mapper_written_) throw std::runtime_error("Interrupt mapper may be restored only once");
        irq_mapper_written_=true;
    }
    if (address<0xfffc) { machine_.write(address,value); return; }
    note_remap();
    machine_.write(address,value);
}
namespace {
constexpr std::uint16_t region_address[5]{0x0000,0x0400,0x4000,0x8000,0xc000};
unsigned region(std::uint16_t address) { return address<0x0400 ? 0 : 1+(address>>14); }
}
void RoutineContext::note_remap() {
    if (before_remap_) return;
    before_remap_.emplace();
    for (unsigned i=0;i<5;++i) (*before_remap_)[i]=machine_.memory().mapping(region_address[i]);
}
void RoutineContext::code_stays_mapped(std::uint16_t address) const {
    if (!before_remap_ || machine_.memory().mapping(address)==(*before_remap_)[region(address)]) return;
    static constexpr char digits[]="0123456789ABCDEF";
    std::string at;
    for (int shift=12;shift>=0;shift-=4) at+=digits[(address>>shift)&15];
    throw UnhandledState("A rewritten segment remapped the memory holding its own code at "+at+
                         " (the original goes on in the new mapping)");
}
void RoutineContext::account_fetches(std::uint64_t count) {
    bus();
    auto& r=machine_.cpu().r;
    r=static_cast<std::uint8_t>((r&0x80)|((r+count)&0x7f));
    machine_.opcode_fetches_+=count;
}
std::uint8_t RoutineContext::fetch_opcode(std::uint16_t address) {
    bus();
    if (machine_.cycles()!=instruction_start_) {
        // Time advanced since the current instruction began: this fetch
        // begins the next one at `address`. Prefix fetches of one
        // instruction follow each other with no time between.
        if (spec_.single_instruction)
            throw std::logic_error("A single-instruction segment rendered a second instruction");
        instruction_start_=machine_.cycles();
        instruction_fetches_=0;
        machine_.body_boundary(*this,address,false);
    }
    if (!instruction_fetches_) code_stays_mapped(address);
    // Q: every instruction clears it unless it writes the flags, which sets
    // it to F once they are computed (Z80.c Q_0 and FLAGS). The boundary
    // above saw the previous instruction's Q.
    if (!instruction_fetches_) previous_q_=q_ ? *q_ : machine_.cpu().q;
    if (q_) *q_=0; else machine_.cpu().q=0;
    const auto value=machine_.read(address);
    // The opcode bytes of the last instruction are kept in data[0..1]; the
    // arbitration reads them (EI delay, RETN, the LD A,I/R rule).
    auto& data=machine_.cpu().data.uint8_array;
    if (instruction_fetches_<2) data[instruction_fetches_]=value;
    ++instruction_fetches_;
    account_fetches(1);
    return value;
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
    if (!delivered_ && (elapsed()>bound_ || 16>bound_-elapsed()))
        throw std::runtime_error("Registered routine exceeded its declared cycle bound");
    if (delivered_ && elapsed()+16>runaway_cap())
        throw std::runtime_error("Registered routine ran away after an interrupt delivery");
    if (!exact_ && destination>=0xfffc && !(destination==0xffff && spec_.slot2_mapper))
        throw std::runtime_error("Registered routine cannot write the mapper");
    const auto value=machine_.read_port(port,machine_.cycles()+12);
    write(destination,value);
    advance(16);
    return value;
}
void RoutineContext::write_port(std::uint16_t port, std::uint8_t value, std::uint64_t offset) {
    bus();
    const auto low=static_cast<std::uint8_t>(port);
    if (!exact_ && low<0x40 && !(low&1)) throw std::runtime_error("Registered routine cannot change memory mapping");
    if (!exact_ && low>=0x80 && low<0xc0 && (low&1)) {
        auto reg=machine_.vdp().control_write_register(value);
        if (reg && (*reg==0 || *reg==1 || *reg==10))
            throw std::runtime_error("Registered routine cannot write an interrupt control register");
    }
    at_offset(offset);
    if (low<0x40 && !(low&1)) note_remap();
    machine_.write_port(port,value,machine_.cycles());
}
void RoutineContext::finish(const SegmentExit& exit, std::uint16_t next_pc) {
    active();
    if (spec_.exits.empty()) throw std::runtime_error("A segment with an empty exit list cannot render its final instruction");
    const auto found=std::find(spec_.exits.begin(),spec_.exits.end(),exit);
    if (found==spec_.exits.end()) throw std::runtime_error("Registered routine exit is not declared");
    ++routine_.rendered[static_cast<std::size_t>(found-spec_.exits.begin())];
    machine_.cpu().pc.uint16_value=next_pc;
    exit_=*found;
    finished_=true;
}
void RoutineContext::check_opcode(std::uint16_t address, OpcodePattern pattern) {
    if (machine_.opcode_check_) (*machine_.opcode_check_)(machine_,address,pattern);
}
// EI and DI (Z80.c 1734-1751). The request word's INT bit follows the line as
// the core leaves it; the boundary arbitration recomputes it from the sample.
void RoutineContext::set_interrupts(bool enable) {
    active();
    auto& cpu=machine_.cpu();
    cpu.iff1=cpu.iff2=enable ? 1 : 0;
    if (!enable) cpu.request&=static_cast<std::uint8_t>(~Z80::request_int);
    else if (cpu.int_line) cpu.request|=Z80::request_int;
}
// RETN (Z80.c 1141-1146): DATA[2] keeps IFF1 from before, which blocks an INT
// at the next boundary only when it was 0; then IFF1 = IFF2.
void RoutineContext::return_from_nmi() {
    active();
    auto& cpu=machine_.cpu();
    cpu.data.uint8_array[2]=cpu.iff1;
    cpu.iff1=cpu.iff2;
    if (cpu.iff1 && cpu.int_line) cpu.request|=Z80::request_int;
}
void RoutineContext::guard_failed(std::string_view what) const {
    static constexpr char digits[]="0123456789ABCDEF";
    auto hex=[&](unsigned value, int width) {
        std::string out;
        for (int shift=4*(width-1);shift>=0;shift-=4) out+=digits[(value>>shift)&15];
        return out;
    };
    throw GuardFailure("Unhandled machine state in the segment at "+hex(entry_bank_,2)+":"+hex(entry_pc_,4)+
                       " (unit "+hex(spec_.unit.bank,2)+":"+hex(spec_.unit.address,4)+"): "+std::string(what));
}
}
