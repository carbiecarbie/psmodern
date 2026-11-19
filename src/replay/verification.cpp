// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay/verification.h"
#include "game/observed.h"
#include "rom/sha1.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace psmodern::replay {
Manifest manifest(const Console& m, const std::vector<std::uint8_t>& writes) {
    return {sha1(m.picture()),sha1(m.memory().ram()),sha1(m.vdp().vram()),
        sha1(m.vdp().cram()),sha1(m.memory().cartridge_ram()),
        sha1(cpu_bytes(m.cpu())),sha1(writes)};
}
void compare_manifests(const std::string& recording, std::uint64_t frame,
                       const Manifest& original, const Manifest& replacement) {
    for (std::size_t i=0;i<original.size();++i)
        if (original[i]!=replacement[i]) throw std::runtime_error(
            "Recording divergence: recording "+recording+", frame "+std::to_string(frame)+
            ", component "+manifest_components[i]);
}
}
