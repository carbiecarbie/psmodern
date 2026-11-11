// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "replay/checkpoint.h"
#include <string>

namespace psmodern::replay {
class CallVerifier final : public RoutineVerifier {
public:
    explicit CallVerifier(std::string recording) : recording_(std::move(recording)) {}
    void run(Machine&, RoutineEntry, const SegmentSpec&, bool forced, std::uint64_t bound,
             std::uint64_t call, const std::function<std::optional<SegmentExit>()>& body) override;
private:
    std::string recording_;
};
using Manifest = std::array<std::string,7>;
inline constexpr std::array<const char*,7> manifest_components{
    "picture","ram","vram","cram","cart-ram","cpu","psg"};
Manifest manifest(const Machine&, const std::vector<std::uint8_t>& frame_writes);
void compare_manifests(const std::string& recording, std::uint64_t frame,
                       const Manifest& original, const Manifest& replacement);
}
