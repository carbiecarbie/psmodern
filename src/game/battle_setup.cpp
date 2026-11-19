// SPDX-License-Identifier: GPL-3.0-or-later
// The enemy-group setup that starts a battle (01:5FFE-60FA), rewritten as
// segments. Every segment renders the original instructions from its entry,
// its final instruction included (milestone 14); the calls it makes (graphics
// copy, random-number routine, object update) are segments or original code
// of their own. Written once as a sequence of instruction renderings of the
// shared menu kit, which runs it on the live bus and on an estimating bus (the
// segment's bound).
#include "game/money.h"
#include "game/menu_kit.h"
#include <utility>

namespace psmodern::game {
namespace {
namespace k=menu_kit;
using k::Index;

template<class Z> k::Exit setup_start(Z& z) {
    auto& r=z.r;
    z.load_hl(0x5FFE); z.store_to_hl(0x6001);
    z.load_hl(0x6003); z.load_de(0x6006); z.load_bc(0x6009); z.store_to_hl(0x600C); z.block_copy(0x600E);
    z.load_hl(0x6010); z.load_de(0x6013); z.load_bc(0x6016); z.store_to_hl(0x6019); z.block_copy(0x601B);
    z.load_a_direct(0x601D); z.copy(0x6020,r.a,r.a); z.and_immediate(0x6021);
    if(auto exit=z.final_return_if(0x6023,k::Condition::z)) return *exit;
    z.copy(0x6024,r.l,r.a); z.load_immediate(0x6025,r.h);
    z.add_to_hl(0x6027,z.hl()); z.add_to_hl(0x6028,z.hl()); z.add_to_hl(0x6029,z.hl());
    z.add_to_hl(0x602A,z.hl()); z.add_to_hl(0x602B,z.hl()); z.load_de(0x602C); z.add_to_hl(0x602F,z.de());
    z.load_de(0x6030); z.load_bc(0x6033); z.block_copy(0x6036);
    z.load_de(0x6038); z.load_bc(0x603B); z.block_copy(0x603E);
    z.load_from_hl(0x6040,r.b); z.increment_hl(0x6041); z.load_from_hl(0x6042,r.a);
    z.increment_hl(0x6043);
    return k::fall_through(0x6043,0x6044);
}
template<class Z> k::Exit setup_picture(Z& z) {
    auto& r=z.r;
    z.load_from_hl(0x6045,r.h); z.copy(0x6046,r.l,r.a); z.copy(0x6047,r.a,r.b);
    z.store_a_direct(0x6048); z.load_de(0x604B);
    return z.final_call(0x604E);
}
template<class Z> k::Exit setup_picture_done(Z& z) {
    z.increment_hl(0x6052); z.load_immediate(0x6053,z.r.a); z.store_a_direct(0x6055);
    z.load_from_hl(0x6058,z.r.a);
    return k::fall_through(0x6058,0x6059);
}
template<class Z> void setup_count_and_records(Z& z) {
    auto& r=z.r;
    z.and_immediate(0x607F); z.copy(0x6081,r.b,r.a); z.store_a_direct(0x6082);
    z.increment_hl(0x6085); z.load_from_hl(0x6086,r.a); z.increment_hl(0x6087);
    z.load_from_hl(0x6088,r.d); z.increment_hl(0x6089); z.load_from_hl(0x608A,r.e);
}
template<class Z> k::Exit setup_count_start(Z& z) {
    auto& r=z.r;
    z.increment_hl(0x605E); z.load_immediate(0x605F,r.a); z.store_a_direct(0x6061);
    z.load_from_hl(0x6064,r.a); z.test_bit(0x6065,7,r.a);
    if(!z.relative_if(0x6067,k::Condition::nz)) {
        z.and_immediate(0x6069); z.copy(0x606B,r.b,r.a); z.load_a_direct(0x606C);
        z.increment(0x606F,r.a); z.add_register(0x6070,r.a); z.compare_register(0x6071,r.b);
        if(!z.relative_if(0x6072,k::Condition::nc)) z.copy(0x6074,r.b,r.a);
        return z.final_call(0x6075);
    }
    setup_count_and_records(z);
    z.increment_hl(0x608B);
    return k::fall_through(0x608B,0x608C);
}
template<class Z> k::Exit setup_count_drawn(Z& z) {
    z.and_immediate(0x6078); z.compare_register(0x607A,z.r.b);
    if(z.jump_if(0x607B,k::Condition::nc)) return z.final_call(0x6075);
    z.increment(0x607E,z.r.a); setup_count_and_records(z);
    z.increment_hl(0x608B);
    return k::fall_through(0x608B,0x608C);
}
template<class Z> k::Exit setup_records(Z& z) {
    auto& r=z.r;
    z.exchange_de_hl(0x608D); z.load_index(0x608E,Index::ix); z.load_de(0x6092);
    do {
        z.store_immediate_indexed(0x6095,Index::ix); z.store_to_indexed(0x6099,Index::ix,r.a);
        z.store_to_indexed(0x609C,Index::ix,r.a); z.store_to_indexed(0x609F,Index::ix,r.h);
        z.store_to_indexed(0x60A2,Index::ix,r.l); z.add_to_index(0x60A5,Index::ix,z.de());
    } while(z.decrement_jump(0x60A7));
    z.pop(0x60A9,r.h,r.l); z.load_from_hl(0x60AA,r.a); z.store_a_direct(0x60AB);
    z.increment_hl(0x60AE); z.load_from_hl(0x60AF,r.e); z.increment_hl(0x60B0);
    z.load_from_hl(0x60B1,r.d); z.push(0x60B2,r.h,r.l); z.load_a_direct(0x60B3);
    z.copy(0x60B6,r.c,r.a); z.load_immediate(0x60B7,r.b);
    return z.final_call(0x60B9);
}
template<class Z> k::Exit setup_experience(Z& z) {
    auto& r=z.r;
    z.pop(0x60BF,r.h,r.l); z.increment_hl(0x60C0); z.load_from_hl(0x60C1,r.a); z.store_a_direct(0x60C2);
    z.increment_hl(0x60C5); z.load_from_hl(0x60C6,r.e); z.increment_hl(0x60C7); z.load_from_hl(0x60C8,r.d);
    z.push(0x60C9,r.h,r.l); z.load_a_direct(0x60CA); z.copy(0x60CD,r.c,r.a); z.load_immediate(0x60CE,r.b);
    return z.final_call(0x60D0);
}
template<class Z> k::Exit setup_finish_fields(Z& z) {
    z.increment_hl(0x60D7); z.load_from_hl(0x60D8,z.r.a); z.store_a_direct(0x60D9);
    z.increment_hl(0x60DC); z.load_from_hl(0x60DD,z.r.a); z.store_a_direct(0x60DE);
    z.load_hl(0x60E1); z.store_hl_direct(0x60E4);
    return z.final_call(0x60E7);
}
template<class Z> k::Exit setup_finish(Z& z) {
    z.load_hl(0x60ED); z.load_de(0x60F0); z.load_bc(0x60F3); z.block_copy(0x60F6);
    z.load_immediate(0x60F8,z.r.a);
    return z.final_jump(0x60FA);
}
// The maxima apply only if the path cannot be estimated (a read of an unmapped
// address, whose value is the open bus); the live body renders such a read
// exactly. Each is the cost of the longest path of its segment.
void add(RoutineRegistry& registry,RoutineEntry entry,std::vector<SegmentExit> exits,
         bool mapper,unsigned scratch,std::uint64_t maximum,auto logic,unsigned inherited=0) {
    k::add_segment(registry,entry,k::segment(battle_setup_unit,std::move(exits),
        {.slot2_mapper=mapper,.scratch_stack=scratch,.inherited_stack=inherited}),maximum,logic);
}
} // namespace
void register_battle_setup(RoutineRegistry& r,const Options& options) {
    using E=SegmentExit; const auto ret=RoutineExit::returned, fin=RoutineExit::core_final;
    add(r,battle_setup_unit,{E{ret,0x6023},E{fin,0x6043}},true,0,10000,[](auto& z){return setup_start(z);});
    add(r,{1,0x6045},{E{fin,0x604E}},true,0,128,[](auto& z){return setup_picture(z);});
    add(r,{1,0x6052},{E{fin,0x6058}},true,0,128,[](auto& z){return setup_picture_done(z);});
    add(r,{1,0x605E},{E{fin,0x6075},E{fin,0x608B}},true,0,256,[](auto& z){return setup_count_start(z);});
    add(r,{1,0x6078},{E{fin,0x6075},E{fin,0x608B}},false,0,192,[](auto& z){return setup_count_drawn(z);});
    add(r,{1,0x608D},{E{fin,0x60B9}},false,2,40000,[](auto& z){return setup_records(z);});
    // The enemy descriptor pointer is held across the graphics and palette calls.
    add(r,{1,0x6044},{E{fin,0x604E,-2}},true,4,160,[](auto& z){
        z.push(0x6044,z.r.h,z.r.l);
        return setup_picture(z);
    });
    add(r,{1,0x6051},{E{fin,0x6058,2}},true,0,160,[](auto& z){
        z.pop(0x6051,z.r.h,z.r.l);
        return setup_picture_done(z);
    },2);
    add(r,{1,0x6059},{E{fin,0x605A,-2}},false,4,28,[](auto& z){
        z.push(0x6059,z.r.h,z.r.l);
        return z.final_call(0x605A);
    });
    add(r,{1,0x605D},{E{fin,0x6075,2},E{fin,0x608B,2}},true,0,272,[](auto& z){
        z.pop(0x605D,z.r.h,z.r.l);
        return setup_count_start(z);
    },2);
    add(r,{1,0x608C},{E{fin,0x60B9,-2}},false,4,40016,[](auto& z){
        z.push(0x608C,z.r.h,z.r.l);
        return setup_records(z);
    });
    add(r,{1,0x60D3},{E{fin,0x60E7,2}},false,0,160,[](auto& z){
        z.store_hl_direct(0x60D3);
        z.pop(0x60D6,z.r.h,z.r.l);
        return setup_finish_fields(z);
    },2);
    register_battle_money_store(r,options);
    add(r,{1,0x60BF},{E{fin,0x60D0}},false,2,128,[](auto& z){return setup_experience(z);});
    add(r,{1,0x60D7},{E{fin,0x60E7}},false,0,128,[](auto& z){return setup_finish_fields(z);});
    // The object update called at 60E7 returns here; the call after it follows.
    {
        auto spec=k::segment(battle_setup_unit,{E{fin,0x60EA}},{.scratch_stack=2});
        spec.single_instruction=true;
        k::add_segment(r,{1,0x60EA},std::move(spec),[](auto& z){return z.final_call(0x60EA);});
    }
    add(r,{1,0x60ED},{E{fin,0x60FA}},false,0,1000,[](auto& z){return setup_finish(z);});
}
}
