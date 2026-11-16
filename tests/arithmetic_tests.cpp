// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include <iostream>

using namespace psmodern;
int main(int argc,char** argv) {
    try {
        if (argc!=3) return 1;
        auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        probe::Harness h(*rom);
        constexpr std::array<unsigned,12> words{0,1,2,0x7F,0x80,0xFF,0x100,0x7FFF,0x8000,0xABCD,0xFFFE,0xFFFF};
        for (auto lhs:words) for (auto rhs:words) {
            auto m=h.prepare({0,0x044C},{.seed=lhs+rhs});
            m.cpu().de.uint16_value=lhs;
            m.cpu().bc.uint16_value=rhs;
            for (auto pc:game::word_product_entries) h.run_cpp(m,{0,pc});
            const auto product=(static_cast<std::uint32_t>(m.cpu().de.uint16_value)<<16)|m.cpu().hl.uint16_value;
            probe::check(product==lhs*rhs,"word product differs");
        }
        for (unsigned byte=0;byte<256;++byte) {
            for (auto word:words) {
                auto m=h.prepare({0,0x0429},{.seed=byte+word});
                m.cpu().hl.uint16_value=(word&255)<<8;
                m.cpu().de.uint16_value=byte;
                h.run_cpp(m,{0,0x0429});
                probe::check(m.cpu().hl.uint16_value==(word&255)*byte,"byte product differs");
                m=h.prepare({0,0x04FE},{.seed=byte+word});
                m.cpu().hl.uint16_value=word;
                m.cpu().de.uint16_value=byte;
                for (auto pc:game::division_entries) h.run_cpp(m,{0,pc});
                const auto quotient=byte ? word/byte : 0xFFFF;
                const auto remainder=byte ? word%byte : word&255;
                probe::check(m.cpu().hl.uint16_value==quotient && m.cpu().af.uint8_values.at_1==remainder,
                             "division result or remainder differs");
            }
        }
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
