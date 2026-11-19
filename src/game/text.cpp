// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/text.h"
#include "game/dungeon_kit.h"

namespace psmodern::game {
namespace {
// Port writes are timed at the same T3 boundary as the original CPU.
struct LiveText : kit::Live {
    explicit LiveText(RoutineContext& context) : kit::Live(context), context(context) {}
    void out(std::uint16_t port, std::uint8_t value) { context.write_port(port,value,elapsed()+10); advance(1); }
    RoutineContext& context;
};
struct EstimateText : kit::Estimate {
    explicit EstimateText(const Machine& machine) : kit::Estimate(machine) {}
    void out(std::uint16_t, std::uint8_t) { advance(11); }
};
template<class Z> void output(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.bus().fetch(at);
    const auto port=z.bus().read(static_cast<std::uint16_t>(at+1));
    r.wz=kit::pair(r.a,static_cast<std::uint8_t>(port+1));
    z.bus().out(kit::pair(r.a,port),r.a);
}
template<class Z> void tile(Z& z, std::uint16_t at) {
    auto& r=z.r;
    z.load_from_hl(at,r.a);
    z.subtract_immediate(at+1);
    z.add_register(at+3,r.a);
    z.load_bc(at+4);
    z.add_register(at+7,r.c);
    z.copy(at+8,r.c,r.a);
    z.add_with_carry(at+9,r.b);
    z.subtract_register(at+10,r.c);
    z.copy(at+11,r.b,r.a);
    z.load_a_from_pair(at+12,z.bc());
    output(z,at+13);
    z.push(at+15,r.a,r.f);
    z.pop(at+16,r.a,r.f);
    z.load_immediate(at+17,r.a);
    output(z,at+19);
}
template<class Z> void subtract_hl_de(Z& z, std::uint16_t at) {
    auto& r=z.r; const unsigned lhs=z.hl(), rhs=z.de(), borrow=r.f&kit::carry_flag;
    const unsigned diff=lhs-rhs-borrow; const auto result=static_cast<std::uint16_t>(diff);
    z.bus().fetch(at); z.bus().fetch(static_cast<std::uint16_t>(at+1)); z.bus().advance(15);
    r.f=static_cast<std::uint8_t>(kit::subtract_flag | ((result>>8)&(kit::sign_flag|kit::y_flag|kit::x_flag)) |
        (result==0 ? kit::zero_flag : 0) | (((lhs^rhs^result)&0x1000) ? kit::half_flag : 0) |
        (((lhs^rhs)&(lhs^result)&0x8000) ? kit::parity_flag : 0) | (diff>0xffff ? kit::carry_flag : 0)); r.q=r.f;
    r.wz=static_cast<std::uint16_t>(lhs+1); z.set_hl(result);
}
template<class Z> void divide_digit(Z& z,std::uint16_t at) {
    do { subtract_hl_de(z,at); z.increment(at+2,z.r.a); } while (z.relative_if(at+3,!z.carry()));
    z.add_to_hl(at+5,z.de());
}
template<class Logic> void add_text_port_segment(RoutineRegistry& registry, RoutineEntry entry,
        SegmentSpec spec, Logic logic) {
    registry.add_exact(entry,std::move(spec),[logic](const Machine& machine) -> std::uint64_t {
        EstimateText bus(machine); auto r=kit::load(machine.cpu()); kit::Instructions<EstimateText> z(bus,r);
        const auto exit=logic(z);
        return bus.uncertain ? 2*Machine::frame_cycles : bus.cycles()+exit.cycles;
    },[logic](RoutineContext& context) {
        LiveText bus(context); auto r=kit::load(context.cpu()); kit::Instructions<LiveText> z(bus,r);
        kit::Exit exit{};
        { kit::Sync<kit::Registers,&kit::store,&kit::load> sync(r); RoutineContext::SyncScope scope(context,sync); exit=logic(z); }
        kit::store(context.cpu(),r);
        kit::finish(context,exit);
    });
}
}
void register_text(RoutineRegistry& registry) {
    kit::add_segment(registry,text_indexed_entry,
        kit::segment(text_indexed_entry,{kit::core_final_at(0x5767)},{.slot2_mapper=true}),
        100,[](auto& z) {
            auto& r=z.r;
            z.load_immediate(0x575a,r.a);
            z.store_a_direct(0x575c);
            z.load_de(0x575f);
            z.add_to_hl(0x5762,z.de());
            z.load_from_hl(0x5763,r.a);
            z.increment_hl(0x5764);
            z.load_from_hl(0x5765,r.h);
            z.copy(0x5766,r.l,r.a);
            return z.final_jump(0x5767);
        });
    // The interrupt disable at the unit entry and the enable after the second tile are instruction boundaries that end a segment.
    kit::add_segment(registry,{0,0x33d7},kit::segment(text_output_unit,{kit::core_final_at(0x33d9,-4)},
        {.scratch_stack=6}),40,[](auto& z) {
            auto& r=z.r; z.push(0x33d7,r.b,r.c); z.push(0x33d8,r.d,r.e);
            return z.final_restart(0x33d9,0x08);
        });
    add_text_port_segment(registry,{0,0x33da},kit::segment(text_output_unit,{kit::core_final_at(0x33f5)},
        {.scratch_stack=2,.inherited_stack=4}),[](auto& z) {
            tile(z,0x33da); z.exchange_de_hl(0x33ef); z.load_bc(0x33f0);
            z.add_to_hl(0x33f3,z.bc()); z.exchange_de_hl(0x33f4);
            return z.final_restart(0x33f5,0x08);
        });
    add_text_port_segment(registry,{0,0x33f6},kit::segment(text_output_unit,{kit::core_final_at(0x340e,2)},
        {.scratch_stack=2,.inherited_stack=4}),[](auto& z) {
            tile(z,0x33f6); z.increment_hl(0x340b); z.pop(0x340c,z.r.d,z.r.e);
            z.increment_pair(0x340d,z.r.d,z.r.e);
            z.increment_pair(0x340e,z.r.d,z.r.e);
            return kit::fall_through(0x340e,0x340f);
        });
    kit::add_segment(registry,{0,0x3416},kit::segment(text_output_unit,
        {kit::returned_at(0x3417),kit::returned_at(0x341f),kit::core_final_at(0x3422)},{.scratch_stack=2}),150,[](auto& z) {
            auto& r=z.r; z.decrement(0x3416,r.b);
            if (auto exit=z.final_return_if(0x3417,kit::Condition::nz)) return *exit;
            z.load_from_hl(0x3418,r.a); z.or_a(0x3419);
            if (!z.jump_if(0x341a,z.sign())) {
                z.compare_immediate(0x341d);
                if (auto exit=z.final_return_if(0x341f,kit::Condition::nc)) return *exit;
            }
            z.copy(0x3420,r.a,r.c); z.or_a(0x3421);
            return z.final_call_if(0x3422,kit::Condition::nz);
        });
    kit::add_segment(registry,{0,0x3425},kit::segment(text_output_unit,{kit::returned_at(0x342b)}),40,[](auto& z) {
        z.load_de(0x3425); z.load_bc(0x3428); return z.final_return(0x342b);
    });
    kit::add_segment(registry,{0,0x3419},kit::segment(text_output_unit,
        {kit::returned_at(0x341f),kit::core_final_at(0x3422)},{.scratch_stack=2}),60,[](auto& z) {
            auto& r=z.r; z.or_a(0x3419);
            if (!z.jump_if(0x341a,z.sign())) {
                z.compare_immediate(0x341d);
                if (auto exit=z.final_return_if(0x341f,kit::Condition::nc)) return *exit;
            }
            z.copy(0x3420,r.a,r.c); z.or_a(0x3421); return z.final_call_if(0x3422,kit::Condition::nz);
        });
    kit::add_segment(registry,{0,0x31fa},kit::segment(text_stream_unit,
        {kit::core_final_at(0x3332),kit::conditional_at(0x3201)},{.scratch_stack=2}),80,[](auto& z) {
            z.load_from_hl(0x31fa,z.r.a); z.or_a(0x31fb);
            if (z.jump_if(0x31fc,z.sign())) return z.final_call(0x3332);
            z.compare_immediate(0x31ff); return z.final_jump_if(0x3201,kit::Condition::nc);
        });
    kit::add_segment(registry,{0,0x3204},kit::segment(text_stream_unit,
        {kit::returned_at(0x3206),kit::conditional_at(0x3209)}),50,[](auto& z) {
            z.compare_immediate(0x3204);
            if (auto exit=z.final_return_if(0x3206,kit::Condition::z)) return *exit;
            z.compare_immediate(0x3207);
            return z.final_jump_if(0x3209,kit::Condition::z);
        });
    kit::add_segment(registry,{0,0x320c},kit::segment(text_stream_unit,
        {kit::conditional_at(0x320e)}),20,[](auto& z) {
            z.compare_immediate(0x320c); return z.final_jump_if(0x320e,kit::Condition::z);
        });
    kit::add_segment(registry,{0,0x3211},kit::segment(text_stream_unit,
        {kit::core_final_at(0x3215),kit::core_final_at(0x3229,-2),kit::core_final_at(0x3237),
         kit::core_final_at(0x325a,-2),kit::core_final_at(0x326c,-2),kit::core_final_at(0x328a,-2),
         kit::conditional_at(0x3294)}, {.scratch_stack=4}),350,[](auto& z) {
            auto& r=z.r; z.compare_immediate(0x3211);
            if (!z.relative_if(0x3213,!z.zero())) return z.final_call(0x3215);
            z.compare_immediate(0x321b);
            if (!z.relative_if(0x321d,!z.zero())) {
                z.push(0x321f,r.h,r.l); z.load_hl(0x3220); z.load_de(0x3223); z.load_bc(0x3226);
                return z.final_call(0x3229);
            }
            z.compare_immediate(0x3231);
            if (!z.relative_if(0x3233,!z.zero())) {
                z.copy(0x3235,r.a,r.c); z.or_a(0x3236);
                return z.final_call_if(0x3237,kit::Condition::nz);
            }
            z.compare_immediate(0x3244);
            if (!z.relative_if(0x3246,!z.zero())) {
                z.push(0x3248,r.h,r.l); z.load_a_direct(0x3249); z.and_immediate(0x324c);
                z.add_register(0x324e,r.a); z.add_register(0x324f,r.a); z.load_hl(0x3250);
                z.add_register(0x3253,r.l); z.copy(0x3254,r.l,r.a); z.add_with_carry(0x3255,r.h);
                z.subtract_register(0x3256,r.l); z.copy(0x3257,r.h,r.a); z.load_immediate(0x3258,r.a);
                return z.final_call(0x325a);
            }
            z.compare_immediate(0x3262);
            if (!z.relative_if(0x3264,!z.zero())) {
                z.push(0x3266,r.h,r.l); z.load_hl(0x3267); z.load_immediate(0x326a,r.a);
                return z.final_call(0x326c);
            }
            z.compare_immediate(0x3274);
            if (!z.relative_if(0x3276,!z.zero())) {
                z.push(0x3278,r.h,r.l); z.load_a_direct(0x3279); z.copy(0x327c,r.l,r.a);
                z.load_immediate(0x327d,r.h); z.add_to_hl(0x327f,z.hl()); z.add_to_hl(0x3280,z.hl());
                z.add_to_hl(0x3281,z.hl()); z.push(0x3282,r.b,r.c); z.load_bc(0x3283);
                z.add_to_hl(0x3286,z.bc()); z.pop(0x3287,r.b,r.c); z.load_immediate(0x3288,r.a);
                return z.final_call(0x328a);
            }
            z.compare_immediate(0x3292);
            return z.final_relative_if(0x3294,kit::Condition::nz);
        });
    kit::add_segment(registry,{0,0x333e},kit::segment(text_dictionary_unit,{kit::core_final_at(0x3357,-4)},
        {.slot2_mapper=true,.scratch_stack=6}),180,[](auto& z) {
            auto& r=z.r; z.push(0x333e,r.h,r.l); z.and_immediate(0x333f); z.add_register(0x3341,r.a);
            z.load_hl(0x3342); z.add_register(0x3345,r.l); z.copy(0x3346,r.l,r.a);
            z.add_with_carry(0x3347,r.h); z.subtract_register(0x3348,r.l); z.copy(0x3349,r.h,r.a);
            z.load_a_direct(0x334a); z.push(0x334d,r.a,r.f); z.load_immediate(0x334e,r.a);
            z.store_a_direct(0x3350); z.load_from_hl(0x3353,r.a); z.increment_hl(0x3354);
            z.load_from_hl(0x3355,r.h); z.copy(0x3356,r.l,r.a); return z.final_call(0x3357);
        });
    kit::add_segment(registry,{0,0x335a},kit::segment(text_dictionary_unit,{kit::conditional_at(0x335d)},
        {.inherited_stack=4}),30,[](auto& z) {
            z.load_from_hl(0x335a,z.r.a); z.compare_immediate(0x335b);
            return z.final_relative_if(0x335d,kit::Condition::nz);
        });
    kit::add_segment(registry,{0,0x335f},kit::segment(text_dictionary_unit,
        {kit::returned_at(0x3367,4),kit::core_final_at(0x3369,4)},
        {.slot2_mapper=true,.inherited_stack=4}),90,[](auto& z) {
            auto& r=z.r; z.pop(0x335f,r.a,r.f); z.store_a_direct(0x3360); z.pop(0x3363,r.h,r.l);
            z.increment_hl(0x3364); z.copy(0x3365,r.a,r.b); z.or_a(0x3366);
            if (auto exit=z.final_return_if(0x3367,kit::Condition::nz)) return *exit;
            z.load_from_hl(0x3368,r.a); return z.final_jump(0x3369);
        });
    kit::add_segment(registry,{0,0x336c},kit::segment(text_name_unit,{kit::core_final_at(0x336d,-2)},
        {.scratch_stack=4}),30,[](auto& z) { z.push(0x336c,z.r.a,z.r.f); return z.final_call(0x336d); });
    kit::add_segment(registry,{0,0x3370},kit::segment(text_name_unit,
        {kit::conditional_at(0x3377,2),kit::returned_at(0x337c,2)}, {.inherited_stack=2}),70,[](auto& z) {
            auto& r=z.r; z.load_from_hl(0x3370,r.a); z.compare_immediate(0x3371);
            if (z.relative_if(0x3373,z.zero())) { z.pop(0x337b,r.a,r.f); return z.final_return(0x337c); }
            z.pop(0x3375,r.a,r.f); z.decrement(0x3376,r.a); return z.final_jump_if(0x3377,kit::Condition::nz);
        });
    // 00:337B is reached only when the terminator branch was taken by the segment before it.
    kit::add_segment(registry,{0,0x337b},kit::segment(text_name_unit,{kit::returned_at(0x337c,2)},
        {.inherited_stack=2}),20,[](auto& z) { z.pop(0x337b,z.r.a,z.r.f); return z.final_return(0x337c); });
    auto open=[](auto& z) { z.load_a_direct(0x31d4); z.or_a(0x31d7); return z.final_jump_if(0x31d8,kit::Condition::nz); };
    auto newline=[](auto& z) { z.copy(0x3235,z.r.a,z.r.c); z.or_a(0x3236); return z.final_call_if(0x3237,kit::Condition::nz); };
    kit::add_segment(registry,{0,0x31cb},kit::segment(text_stream_unit,{kit::core_final_at(0x3237)},
        {.scratch_stack=2}),50,[newline](auto& z) { z.decrement_hl(0x31cb); z.jump(0x31cc); return newline(z); });
    kit::add_segment(registry,{0,0x3235},kit::segment(text_stream_unit,{kit::core_final_at(0x3237)},
        {.scratch_stack=2}),25,newline);
    kit::add_segment(registry,{0,0x31cf},kit::segment(text_stream_unit,{kit::conditional_at(0x31d8)},
        {.slot2_mapper=true}),60,[open](auto& z) { z.load_immediate(0x31cf,z.r.a); z.store_a_direct(0x31d1); return open(z); });
    kit::add_segment(registry,{0,0x31d4},kit::segment(text_stream_unit,{kit::conditional_at(0x31d8)}),30,open);
    kit::add_segment(registry,{0,0x31db},kit::segment(text_stream_unit,{kit::core_final_at(0x31ea,-2)},
        {.scratch_stack=4}),90,[](auto& z) {
            z.load_immediate(0x31db,z.r.a); z.store_a_direct(0x31dd); z.push(0x31e0,z.r.h,z.r.l);
            z.load_hl(0x31e1); z.load_de(0x31e4); z.load_bc(0x31e7); return z.final_call(0x31ea);
        });
    kit::add_segment(registry,{0,0x31ed},kit::segment(text_stream_unit,{kit::core_final_at(0x31f0)},
        {.scratch_stack=2,.inherited_stack=2}),30,[](auto& z) { z.load_hl(0x31ed); return z.final_call(0x31f0); });
    kit::add_segment(registry,{0,0x31f3},kit::segment(text_stream_unit,{kit::core_final_at(0x31f7,2)},
        {.inherited_stack=2}),30,[](auto& z) { z.pop(0x31f3,z.r.h,z.r.l); z.load_de(0x31f4); z.load_bc(0x31f7); return kit::fall_through(0x31f7,0x31fa); });
    kit::add_segment(registry,{0,0x31f4},kit::segment(text_stream_unit,{kit::core_final_at(0x31f7)}),20,
        [](auto& z) { z.load_de(0x31f4); z.load_bc(0x31f7); return kit::fall_through(0x31f7,0x31fa); });
    for (auto entry:{0x322c,0x325d,0x326f,0x328d}) {
        const auto address=static_cast<std::uint16_t>(entry);
        kit::add_segment(registry,{0,address},kit::segment(text_stream_unit,{kit::core_final_at(static_cast<std::uint16_t>(address+2),2)},
            {.inherited_stack=2}),30,[address](auto& z) {
                z.pop(address,z.r.h,z.r.l); z.increment_hl(static_cast<std::uint16_t>(address+1));
                return z.final_jump(static_cast<std::uint16_t>(address+2));
            });
    }
    kit::add_segment(registry,{0,0x323a},kit::segment(text_stream_unit,{kit::core_final_at(0x3241)}),40,[](auto& z) {
        z.load_de(0x323a); z.load_bc(0x323d); z.increment_hl(0x3240); return z.final_jump(0x3241);
    });
    kit::add_segment(registry,{0,0x3296},kit::segment(text_number_unit,{kit::core_final_at(0x32a9,-4)},
        {.scratch_stack=6}),900,[](auto& z) {
            auto& r=z.r; z.push(0x3296,r.h,r.l); z.push(0x3297,r.b,r.c); z.push(0x3298,r.d,r.e);
            z.load_hl_direct(0x3299); z.load_de(0x329c); z.xor_a(0x329f); z.copy(0x32a0,r.c,r.a);
            z.decrement(0x32a1,r.a); divide_digit(z,0x32a2); z.pop(0x32a8,r.d,r.e); return z.final_call(0x32a9);
        });
    for (auto at:{std::uint16_t{0x32ac},std::uint16_t{0x32bc}}) {
        kit::add_segment(registry,{0,at},kit::segment(text_number_unit,
            {kit::core_final_at(static_cast<std::uint16_t>(at+13))},{.scratch_stack=2,.inherited_stack=4}),900,
            [at](auto& z) {
                auto& r=z.r; z.push(at,r.d,r.e); z.load_de(at+1); z.load_immediate(at+4,r.a);
                divide_digit(z,at+6); z.pop(at+12,r.d,r.e); return z.final_call(static_cast<std::uint16_t>(at+13));
            });
    }
    kit::add_segment(registry,{0,0x32cc},kit::segment(text_number_unit,{kit::core_final_at(0x32da)},
        {.scratch_stack=2,.inherited_stack=4}),1500,[](auto& z) {
            auto& r=z.r; z.push(0x32cc,r.d,r.e); z.load_immediate(0x32cd,r.d); z.copy(0x32cf,r.a,r.l);
            do { z.subtract_immediate(0x32d0); z.increment(0x32d2,r.d); } while (z.relative_if(0x32d3,!z.carry()));
            z.add_immediate(0x32d5); z.copy(0x32d7,r.l,r.a); z.copy(0x32d8,r.a,r.d);
            z.pop(0x32d9,r.d,r.e); return z.final_call(0x32da);
        });
    kit::add_segment(registry,{0,0x32dd},kit::segment(text_number_unit,{kit::core_final_at(0x32ea)},
        {.scratch_stack=2,.inherited_stack=4}),16000,[](auto& z) {
            auto& r=z.r; z.push(0x32dd,r.d,r.e); z.load_immediate(0x32de,r.d); z.copy(0x32e0,r.a,r.l);
            do { z.subtract_immediate(0x32e1); z.increment(0x32e3,r.d); } while (z.relative_if(0x32e4,!z.carry()));
            z.copy(0x32e6,r.a,r.d); z.load_immediate(0x32e7,r.c); z.pop(0x32e9,r.d,r.e);
            return z.final_call(0x32ea);
        });
    kit::add_segment(registry,{0,0x32ed},kit::segment(text_number_unit,{kit::core_final_at(0x32f2,4)},
        {.inherited_stack=4}),55,[](auto& z) {
            auto& r=z.r; z.copy(0x32ed,r.a,r.b); z.pop(0x32ee,r.b,r.c); z.copy(0x32ef,r.b,r.a);
            z.pop(0x32f0,r.h,r.l); z.increment_hl(0x32f1); return z.final_jump(0x32f2);
        });
    kit::add_segment(registry,{0,0x32fb},kit::segment(text_number_unit,
        {kit::returned_at(0x3301),kit::core_final_at(0x3302)}),50,[](auto& z) {
            z.or_a(0x32fb);
            if (!z.jump_if(0x32fc,!z.zero())) {
                z.test_bit(0x32ff,0,z.r.c);
                if (auto exit=z.final_return_if(0x3301,kit::Condition::z)) return *exit;
            }
            z.load_immediate(0x3302,z.r.c);
            return kit::fall_through(0x3302,0x3304);
        });
    kit::add_segment(registry,{0,0x3305},kit::segment(text_number_unit,{kit::core_final_at(0x3307,-4)},
        {.scratch_stack=6}),40,[](auto& z) {
            z.push(0x3305,z.r.d,z.r.e); z.push(0x3306,z.r.a,z.r.f); return z.final_restart(0x3307,0x08);
        });
    add_text_port_segment(registry,{0,0x3308},kit::segment(text_number_unit,{kit::core_final_at(0x331b)},
        {.scratch_stack=2,.inherited_stack=4}),[](auto& z) {
            auto& r=z.r; z.push(0x3308,r.a,r.f); z.pop(0x3309,r.a,r.f); z.load_immediate(0x330a,r.a);
            output(z,0x330c); z.push(0x330e,r.a,r.f); z.pop(0x330f,r.a,r.f); z.load_immediate(0x3310,r.a);
            output(z,0x3312); z.load_immediate(0x3314,r.a); z.add_register(0x3316,r.e); z.copy(0x3317,r.e,r.a);
            z.add_with_carry(0x3318,r.d); z.subtract_register(0x3319,r.e); z.copy(0x331a,r.d,r.a);
            return z.final_restart(0x331b,0x08);
        });
    add_text_port_segment(registry,{0,0x331c},kit::segment(text_number_unit,{kit::core_final_at(0x3329,4)},
        {.scratch_stack=2,.inherited_stack=4}),[](auto& z) {
            auto& r=z.r; z.pop(0x331c,r.a,r.f); z.add_immediate(0x331d); output(z,0x331f);
            z.push(0x3321,r.a,r.f); z.pop(0x3322,r.a,r.f); z.load_immediate(0x3323,r.a); output(z,0x3325);
            z.pop(0x3327,r.d,r.e); z.increment_pair(0x3328,r.d,r.e);
            z.increment_pair(0x3329,r.d,r.e); return kit::fall_through(0x3329,0x332a);
        });
    kit::add_segment(registry,{0,0x3330},kit::segment(text_number_unit,{kit::returned_at(0x3331)}),14,[](auto& z) {
        z.decrement(0x3330,z.r.b); return z.final_return(0x3331);
    });
    auto alternate_read=[](auto& z) {
        z.load_from_hl(0x3389,z.r.a); z.or_a(0x338a); return z.final_jump_if(0x338b,kit::Condition::m);
    };
    kit::add_segment(registry,{0,0x337d},kit::segment(text_alternate_unit,{kit::conditional_at(0x338b,-2)},
        {.slot2_mapper=true,.scratch_stack=2}),70,[alternate_read](auto& z) {
            z.load_immediate(0x337d,z.r.a); z.store_a_direct(0x337f); z.load_de(0x3382); z.load_bc(0x3385);
            z.push(0x3388,z.r.d,z.r.e); return alternate_read(z);
        });
    kit::add_segment(registry,{0,0x3388},kit::segment(text_alternate_unit,{kit::conditional_at(0x338b,-2)},
        {.scratch_stack=2}),35,[alternate_read](auto& z) { z.push(0x3388,z.r.d,z.r.e); return alternate_read(z); });
    kit::add_segment(registry,{0,0x3389},kit::segment(text_alternate_unit,{kit::conditional_at(0x338b)},
        {.inherited_stack=2}),25,alternate_read);
    // The control-code comparisons: a match jumps to the handler, except in the last, which jumps on a mismatch.
    for (auto at:{std::uint16_t{0x338e},std::uint16_t{0x3392},std::uint16_t{0x3396},std::uint16_t{0x339a}}) {
        kit::add_segment(registry,{0,at},kit::segment(text_alternate_unit,{kit::conditional_at(static_cast<std::uint16_t>(at+2))},
            {.inherited_stack=2}),25,[at](auto& z) {
                z.compare_immediate(at); return z.final_relative_if(static_cast<std::uint16_t>(at+2),kit::Condition::z);
            });
    }
    kit::add_segment(registry,{0,0x339e},kit::segment(text_alternate_unit,{kit::conditional_at(0x33a0)},
        {.inherited_stack=2}),25,[](auto& z) {
            z.compare_immediate(0x339e); return z.final_relative_if(0x33a0,kit::Condition::nz);
        });
    kit::add_segment(registry,{0,0x33a2},kit::segment(text_alternate_unit,{kit::core_final_at(0x33aa,2)},
        {.inherited_stack=2}),60,[](auto& z) {
            z.increment_hl(0x33a2); z.exchange_de_hl(0x33a3); z.pop(0x33a4,z.r.h,z.r.l); z.load_bc(0x33a5);
            z.add_to_hl(0x33a8,z.bc()); z.exchange_de_hl(0x33a9); return z.final_jump(0x33aa);
        });
    kit::add_segment(registry,{0,0x33b2},kit::segment(text_alternate_unit,{kit::core_final_at(0x33b6)},
        {.scratch_stack=2,.inherited_stack=2}),50,[](auto& z) {
            z.pop(0x33b2,z.r.d,z.r.e); z.push(0x33b3,z.r.h,z.r.l); z.compare_immediate(0x33b4);
            return z.final_call_if(0x33b6,kit::Condition::nz);
        });
    kit::add_segment(registry,{0,0x33b9},kit::segment(text_alternate_unit,{kit::core_final_at(0x33bf)},
        {.inherited_stack=2}),30,[](auto& z) { z.load_de(0x33b9); z.load_bc(0x33bc); z.load_hl(0x33bf); return kit::fall_through(0x33bf,0x33c2); });
    // After the alternate-text printer's interrupt enable: the pointer popped by the segment at
    // 00:33C7 (menu_remainder.cpp) moves on to the next character.
    kit::add_segment(registry,{0,0x33c8},kit::segment(text_alternate_unit,{kit::core_final_at(0x33c9)}),20,
        [](auto& z) { z.increment_hl(0x33c8); return z.final_jump(0x33c9); });
    for (auto at:{std::uint16_t{0x33cf},std::uint16_t{0x33d4}})
        kit::add_segment(registry,{0,at},kit::segment(text_alternate_unit,{kit::returned_at(static_cast<std::uint16_t>(at+1),2)},
            {.inherited_stack=2}),20,[at](auto& z) { z.pop(at,z.r.d,z.r.e); return z.final_return(static_cast<std::uint16_t>(at+1)); });
    // ---- The joining segments between the segments above (milestone 14) -------------------
    // Single instructions at the places where a segment's final hands over to
    // code that has no segment of its own: calls and jumps of the text
    // readers, and the return of the name writer.
    auto single=[&](std::uint16_t at,RoutineEntry unit,SegmentExit exit,unsigned scratch,std::uint64_t cycles,auto logic) {
        auto spec=kit::segment(unit,{exit},{.scratch_stack=scratch});
        spec.single_instruction=true;
        kit::add_segment(registry,{0,at},std::move(spec),[cycles](const Machine&) { return cycles; },logic);
    };
    single(0x3218,text_stream_unit,kit::core_final_at(0x3218),0,10,[](auto& z) { return z.final_jump(0x3218); });   // a control code goes to the line-break code
    single(0x3335,text_stream_unit,kit::core_final_at(0x3335),0,10,[](auto& z) { return z.final_jump(0x3335); });   // a dictionary word is done: back to the reader
    single(0x32f5,text_number_unit,kit::core_final_at(0x32f5),2,17,[](auto& z) { return z.final_call(0x32f5); });   // a digit's tile is written
    single(0x32f8,text_number_unit,kit::core_final_at(0x32f8),0,10,[](auto& z) { return z.final_jump(0x32f8); });   // and the reader goes on
    single(0x3338,text_alternate_unit,kit::core_final_at(0x3338),2,17,[](auto& z) { return z.final_call(0x3338); });   // a dictionary word in the alternate text
    single(0x333b,text_alternate_unit,kit::core_final_at(0x333b),0,10,[](auto& z) { return z.final_jump(0x333b); });   // then back to the next character
    single(0x3357,text_dictionary_unit,kit::core_final_at(0x3357),2,17,[](auto& z) { return z.final_call(0x3357); });  // a letter of the word is written
    single(0x337a,text_name_unit,kit::returned_at(0x337a),0,10,[](auto& z) { return z.final_return(0x337a); });       // the name is finished
    single(0x33ad,text_alternate_unit,kit::core_final_at(0x33ad),2,17,[](auto& z) { return z.final_call(0x33ad); });  // a letter of the alternate text is written
    single(0x33b0,text_alternate_unit,kit::core_final_at(0x33b0),0,12,[](auto& z) { return z.final_relative(0x33b0); }); // and the reader goes on
    single(0x33cc,text_alternate_unit,kit::core_final_at(0x33cc),2,17,[](auto& z) { return z.final_call(0x33cc); });  // wait for button 1 or 2
    single(0x33d1,text_alternate_unit,kit::core_final_at(0x33d1),2,17,[](auto& z) { return z.final_call(0x33d1); });  // wait the fixed number of frames
}
}
