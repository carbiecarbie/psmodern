// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include <iostream>

using namespace psmodern;
int main(int argc,char** argv) {
    try {
        if (argc!=3) return 1;
        auto rom=probe::load(argv[1],argv[2]);
        if (!rom) return 77;
        probe::Harness harness(*rom);
        for (const auto entry:game::dispatch_entries) {
            for (unsigned selector=0;selector<256;++selector) {
                probe::Setup setup;
                setup.seed=selector;
                setup.banks[2]=selector%32;
                auto machine=harness.prepare(entry,setup);
                // In particular test a word spanning two mapper slots or
                // wrapping from FFFF to 0000 as well as a word in work RAM.
                constexpr std::array<std::uint16_t,4> bases{0x3fff,0x7fff,0xffff,0xc71f};
                const auto base=bases[selector%4];
                machine.cpu().af.uint8_values.at_1=static_cast<std::uint8_t>(selector);
                machine.cpu().hl.uint16_value=base;
                machine.cpu().de.uint16_value=base;
                machine.memory().write(0xc208,static_cast<std::uint8_t>(selector));
                harness.run_cpp(machine,entry);
            }
        }
        return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
