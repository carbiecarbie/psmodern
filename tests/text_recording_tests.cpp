// SPDX-License-Identifier: GPL-3.0-or-later
#include "game/routines.h"
#include "machine/machine.h"
#include "replay/recording.h"
#include "replay/verification.h"
#include "rom/sha1.h"
#include "probe.h"
#include <array>
#include <iostream>

using namespace psmodern;
int main(int argc,char** argv) {
    try {
        probe::check(argc==4,"expected ROM, region and recording");
        auto rom=probe::load(argv[1],argv[2]); if (!rom) return 77;
        const auto recording=replay::read(argv[3]);
        probe::check(recording.rom_sha1==rom->sha1,"recording ROM mismatch");
        Memory::CartridgeRam cart{};
        probe::check(sha1(cart)==recording.cart_ram_sha1,"recording must start with empty cartridge RAM");
        Machine original(rom->image,cart), replacement(rom->image,cart);
        RoutineRegistry registry; game::register_routines(registry,rom->sha1);
        replay::CallVerifier verifier("text recording"); registry.verifier=&verifier;
        replay::FrameWrites original_writes, replacement_writes;
        std::array<unsigned,256> dialogue{}, alternate{};
        for (std::uint64_t frame=0;frame<recording.frames;++frame) {
            auto hook=[&](std::uint16_t pc,Memory::Mapping mapping)->std::optional<std::uint16_t> {
                if (mapping.kind==Memory::Kind::rom && mapping.bank==0) {
                    if (pc==0x31fa) ++dialogue[original.peek(original.cpu().hl.uint16_value)];
                    if (pc==0x3389) ++alternate[original.peek(original.cpu().hl.uint16_value)];
                }
                return std::nullopt;
            };
            auto input=recording.input_at(frame);
            original.run_frame(input,nullptr,hook); replacement.run_frame(input,registry);
            auto a=original_writes.take(frame+1,original.take_psg_writes()); original.take_samples();
            auto b=replacement_writes.take(frame+1,replacement.take_psg_writes()); replacement.take_samples();
            replay::compare_manifests("text recording",frame+1,replay::manifest(original,a),replay::manifest(replacement,b));
        }
        for (unsigned n=0x5b;n<=0x65;++n)
            std::cout<<std::hex<<n<<std::dec<<" dialogue="<<dialogue[n]<<" alternate="<<alternate[n]<<'\n';
        probe::check(alternate[0x60] && alternate[0x61] && alternate[0x65],
                     "recording did not reach line, wait and end controls");
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
