// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "machine/machine.h"
#include <filesystem>
#include <string>
#include <vector>

namespace psmodern::replay {
struct Change { std::uint64_t frame; FrameInput input; };
struct Recording {
    std::string rom_sha1, cart_ram_sha1;
    std::vector<Change> changes;
    std::uint64_t frames=0;
    FrameInput input_at(std::uint64_t frame) const;
};
Recording parse(const std::string& text);
Recording read(const std::filesystem::path& path);
std::string write(const Recording& recording);
void write_atomic_new(const std::filesystem::path& path,const Recording& recording);
class Recorder {
public:
    Recorder(std::string rom_sha1,std::string cart_ram_sha1);
    void add(const FrameInput& input);
    const Recording& recording() const { return recording_; }
private:
    Recording recording_;
    FrameInput previous_{};
};
}
