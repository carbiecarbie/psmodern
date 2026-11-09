// SPDX-License-Identifier: GPL-3.0-or-later
#include "machine/machine.h"

namespace psmodern {
std::vector<FieldDifference> Machine::differences(const Machine& o) const {
    DifferenceList out;
    cpu_.differences(o.cpu_,out);
    out.add("machine.cycle",cycle_,o.cycle_,false);
    out.add("machine.opcode_fetches",opcode_fetches_,o.opcode_fetches_,false);
    out.add("machine.scheduled_nmi",scheduled_nmi_,o.scheduled_nmi_,false);
    out.add("machine.io_control",io_control_,o.io_control_,true);
    out.add("machine.open_bus",open_bus_,o.open_bus_,true);
    out.add("machine.reset",reset_,o.reset_,false);
    out.add("machine.pause",pause_,o.pause_,false);
    out.add("machine.pending_reset",pending_reset_,o.pending_reset_,false);
    out.add("machine.pending_pause",pending_pause_,o.pending_pause_,false);
    out.add("machine.input_latched",input_latched_,o.input_latched_,false);
    out.add("machine.sampled_int",sampled_int_,o.sampled_int_,false);
    out.add("machine.sampled_nmi",sampled_nmi_,o.sampled_nmi_,false);
    out.add("machine.pause_nmi_level",pause_nmi_level_,o.pause_nmi_level_,false);
    for (unsigned i=0;i<2;++i) {
        const auto name="machine.pads["+std::to_string(i)+"].";
        out.add(name+"up",pads_[i].up,o.pads_[i].up);
        out.add(name+"down",pads_[i].down,o.pads_[i].down);
        out.add(name+"left",pads_[i].left,o.pads_[i].left);
        out.add(name+"right",pads_[i].right,o.pads_[i].right);
        out.add(name+"one",pads_[i].one,o.pads_[i].one);
        out.add(name+"two",pads_[i].two,o.pads_[i].two);
    }
    for (unsigned i=0;i<2;++i) {
        const auto name="machine.pending_pads["+std::to_string(i)+"].";
        out.add(name+"up",pending_pads_[i].up,o.pending_pads_[i].up);
        out.add(name+"down",pending_pads_[i].down,o.pending_pads_[i].down);
        out.add(name+"left",pending_pads_[i].left,o.pending_pads_[i].left);
        out.add(name+"right",pending_pads_[i].right,o.pending_pads_[i].right);
        out.add(name+"one",pending_pads_[i].one,o.pending_pads_[i].one);
        out.add(name+"two",pending_pads_[i].two,o.pending_pads_[i].two);
    }
    out.add("memory.rom",memory_.rom_==o.memory_.rom_,true,true);
    out.add("memory.ram",memory_.ram_,o.memory_.ram_,true);
    out.add("memory.cartridge_ram",memory_.cartridge_ram_,o.memory_.cartridge_ram_,true);
    out.add("memory.banks",memory_.banks_,o.memory_.banks_,true);
    out.add("memory.mapper_control",memory_.mapper_control_,o.memory_.mapper_control_,true);
    out.add("memory.memory_control",memory_.memory_control_,o.memory_.memory_control_,true);
    out.add("memory.cartridge_ram_dirty",memory_.cartridge_ram_dirty_,o.memory_.cartridge_ram_dirty_,false);
    out.add("vdp.vram",vdp_.vram_,o.vdp_.vram_,true);
    out.add("vdp.cram",vdp_.cram_,o.vdp_.cram_,true);
    out.add("vdp.regs",vdp_.regs_,o.vdp_.regs_,true);
    out.add("vdp.cycle",vdp_.cycle_,o.vdp_.cycle_,false);
    out.add("vdp.address",vdp_.address_,o.vdp_.address_,true);
    out.add("vdp.code",vdp_.code_,o.vdp_.code_,true);
    out.add("vdp.buffer",vdp_.buffer_,o.vdp_.buffer_,true);
    out.add("vdp.line_counter",vdp_.line_counter_,o.vdp_.line_counter_,true);
    out.add("vdp.h_latch",vdp_.h_latch_,o.vdp_.h_latch_,false);
    out.add("vdp.control_latch",vdp_.control_latch_,o.vdp_.control_latch_,false);
    out.add("vdp.frame_flag",vdp_.frame_flag_,o.vdp_.frame_flag_,false);
    out.add("vdp.line_flag",vdp_.line_flag_,o.vdp_.line_flag_,false);
    out.add("vdp.frame_visible",vdp_.frame_visible_,o.vdp_.frame_visible_,false);
    out.add("vdp.overflow",vdp_.overflow_,o.vdp_.overflow_,false);
    out.add("vdp.collision",vdp_.collision_,o.vdp_.collision_,false);
    out.add("vdp.drawing_picture",vdp_.drawing_picture_,o.vdp_.drawing_picture_,true);
    out.add("vdp.completed_picture",vdp_.completed_picture_,o.vdp_.completed_picture_,true);
    out.add("vdp.horizontal_scroll",vdp_.horizontal_scroll_,o.vdp_.horizontal_scroll_,true);
    out.add("vdp.vertical_scroll",vdp_.vertical_scroll_,o.vdp_.vertical_scroll_,true);
    out.add("vdp.collisions",vdp_.collisions_,o.vdp_.collisions_,false);
    out.add("vdp.evaluation_index",vdp_.evaluation_index_,o.vdp_.evaluation_index_,false);
    out.add("vdp.evaluation_count",vdp_.evaluation_count_,o.vdp_.evaluation_count_,false);
    out.add("vdp.prepared_count",vdp_.prepared_count_,o.vdp_.prepared_count_,false);
    out.add("vdp.evaluation_done",vdp_.evaluation_done_,o.vdp_.evaluation_done_,false);
    out.add("vdp.overflow_pending",vdp_.overflow_pending_,o.vdp_.overflow_pending_,false);
    out.add("vdp.write_pending",vdp_.write_pending_,o.vdp_.write_pending_,false);
    out.add("vdp.read_pending",vdp_.read_pending_,o.vdp_.read_pending_,false);
    out.add("vdp.processed_dot",vdp_.processed_dot_,o.vdp_.processed_dot_,false);
    out.add("vdp.tile_name",vdp_.tile_name_,o.vdp_.tile_name_,true);
    out.add("vdp.tile_pattern",vdp_.tile_pattern_,o.vdp_.tile_pattern_,true);
    out.add("vdp.tile_planes",vdp_.tile_planes_,o.vdp_.tile_planes_,true);
    out.add("vdp.background",vdp_.background_,o.vdp_.background_,true);
    out.add("vdp.sprites",vdp_.sprites_,o.vdp_.sprites_,true);
    out.add("vdp.fine_scroll",vdp_.fine_scroll_,o.vdp_.fine_scroll_,true);
    out.add("psg.periods",psg_.periods_,o.psg_.periods_,true);
    out.add("psg.timers",psg_.timers_,o.psg_.timers_,true);
    out.add("psg.outputs",psg_.outputs_,o.psg_.outputs_,false);
    out.add("psg.attenuations",psg_.attenuations_,o.psg_.attenuations_,true);
    out.add("psg.lfsr",psg_.lfsr_,o.psg_.lfsr_,false);
    out.add("psg.noise_timer",psg_.noise_timer_,o.psg_.noise_timer_,true);
    out.add("psg.latch",psg_.latch_,o.psg_.latch_,true);
    out.add("psg.noise_control",psg_.noise_control_,o.psg_.noise_control_,true);
    out.add("psg.noise_clock",psg_.noise_clock_,o.psg_.noise_clock_,false);
    out.add("psg.noise_output",psg_.noise_output_,o.psg_.noise_output_,false);
    out.add("psg.cycle",psg_.cycle_,o.psg_.cycle_,false);
    out.add("psg.write_frame",psg_.write_frame_,o.psg_.write_frame_,false);
    out.add("psg.frame_write_count",psg_.frame_write_count_,o.psg_.frame_write_count_,false);
    for (unsigned i=0;i<8;++i) {
        const auto name="vdp.preparing_sprites["+std::to_string(i)+"].";
        out.add(name+"index",vdp_.preparing_sprites_[i].index,o.vdp_.preparing_sprites_[i].index,false);
        out.add(name+"row",vdp_.preparing_sprites_[i].row,o.vdp_.preparing_sprites_[i].row,false);
        out.add(name+"pattern",vdp_.preparing_sprites_[i].pattern,o.vdp_.preparing_sprites_[i].pattern,true);
        out.add(name+"x",vdp_.preparing_sprites_[i].x,o.vdp_.preparing_sprites_[i].x,true);
        out.add(name+"planes",vdp_.preparing_sprites_[i].planes,o.vdp_.preparing_sprites_[i].planes,true);
    }
    for (unsigned i=0;i<8;++i) {
        const auto name="vdp.prepared_sprites["+std::to_string(i)+"].";
        out.add(name+"index",vdp_.prepared_sprites_[i].index,o.vdp_.prepared_sprites_[i].index,false);
        out.add(name+"row",vdp_.prepared_sprites_[i].row,o.vdp_.prepared_sprites_[i].row,false);
        out.add(name+"pattern",vdp_.prepared_sprites_[i].pattern,o.vdp_.prepared_sprites_[i].pattern,true);
        out.add(name+"x",vdp_.prepared_sprites_[i].x,o.vdp_.prepared_sprites_[i].x,true);
        out.add(name+"planes",vdp_.prepared_sprites_[i].planes,o.vdp_.prepared_sprites_[i].planes,true);
    }
    out.add("psg.writes.size",psg_.writes_.size(),o.psg_.writes_.size());
    for (std::size_t i=0;i<psg_.writes_.size() && i<o.psg_.writes_.size();++i) {
        const auto name="psg.writes["+std::to_string(i)+"].";
        out.add(name+"cycle",psg_.writes_[i].cycle,o.psg_.writes_[i].cycle);
        out.add(name+"byte",psg_.writes_[i].byte,o.psg_.writes_[i].byte,true);
    }
    out.add("psg.samples.first_cycle",psg_.samples_.first_cycle,o.psg_.samples_.first_cycle);
    out.add("psg.samples.values",psg_.samples_.values,o.psg_.samples_.values,true);
    return std::move(out.fields);
}
}
