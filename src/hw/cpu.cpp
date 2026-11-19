// SPDX-License-Identifier: GPL-3.0-or-later
#include "hw/cpu.h"
namespace psmodern {
void register_differences(const Z80& a, const Z80& b, DifferenceList& out) {
    // The pinned core replaces data[0] before dispatch and data[1], data[3]
    // before any prefix use, and writes data[2] before its RETN use: after
    // an unprefixed instruction these three bytes are dead. Keep all bytes
    // at prefixes and resumptions.
    auto prefix=[](std::uint8_t op) { return op==0xcb || op==0xed || (op&0xdf)==0xdd; };
    const bool data_live=a.resume || b.resume || prefix(a.data.uint8_array[0]) || prefix(b.data.uint8_array[0]);
    out.add("cpu.data[0]",a.data.uint8_array[0],b.data.uint8_array[0],true);
    if (data_live) for (unsigned i=1;i<4;++i)
        out.add("cpu.data["+std::to_string(i)+"]",a.data.uint8_array[i],b.data.uint8_array[i],true);
    out.add("cpu.ix_iy[0]",a.ix_iy[0].uint16_value,b.ix_iy[0].uint16_value,true);
    out.add("cpu.ix_iy[1]",a.ix_iy[1].uint16_value,b.ix_iy[1].uint16_value,true);
    out.add("cpu.pc",a.pc.uint16_value,b.pc.uint16_value);
    out.add("cpu.sp",a.sp.uint16_value,b.sp.uint16_value);
    out.add("cpu.memptr",a.memptr.uint16_value,b.memptr.uint16_value);
    out.add("cpu.a",a.af.uint8_values.at_1,b.af.uint8_values.at_1,true);
    out.add("cpu.f",a.af.uint8_values.at_0,b.af.uint8_values.at_0);
    out.add("cpu.a_",a.af_.uint8_values.at_1,b.af_.uint8_values.at_1,true);
    out.add("cpu.f_",a.af_.uint8_values.at_0,b.af_.uint8_values.at_0);
    out.add("cpu.bc",a.bc.uint16_value,b.bc.uint16_value,true);
    out.add("cpu.bc_",a.bc_.uint16_value,b.bc_.uint16_value,true);
    out.add("cpu.de",a.de.uint16_value,b.de.uint16_value,true);
    out.add("cpu.de_",a.de_.uint16_value,b.de_.uint16_value,true);
    out.add("cpu.hl",a.hl.uint16_value,b.hl.uint16_value,true);
    out.add("cpu.hl_",a.hl_.uint16_value,b.hl_.uint16_value,true);
    out.add("cpu.r",a.r,b.r,true);
    out.add("cpu.i",a.i,b.i,true);
    out.add("cpu.im",a.im,b.im);
    out.add("cpu.request",a.request,b.request);
    out.add("cpu.resume",a.resume,b.resume);
    out.add("cpu.iff1",a.iff1,b.iff1);
    out.add("cpu.iff2",a.iff2,b.iff2);
    out.add("cpu.q",a.q,b.q);
    out.add("cpu.int_line",a.int_line,b.int_line);
    out.add("cpu.halt_line",a.halt_line,b.halt_line);
}
bool same_registers(const Z80& a, const Z80& b) {
    DifferenceList out; register_differences(a,b,out); return out.fields.empty();
}
}
