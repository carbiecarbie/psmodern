// SPDX-License-Identifier: GPL-3.0-or-later
#include "rom/sha1.h"
#include <array>
#include <bit>
#include <iomanip>
#include <sstream>
#include <vector>

namespace psmodern {
std::string sha1(std::span<const std::uint8_t> bytes) {
    std::vector<std::uint8_t> data(bytes.begin(), bytes.end());
    const auto bits = static_cast<std::uint64_t>(data.size()) * 8;
    data.push_back(0x80);
    while (data.size() % 64 != 56) data.push_back(0);
    for (int shift = 56; shift >= 0; shift -= 8) data.push_back(static_cast<std::uint8_t>(bits >> shift));
    std::array<std::uint32_t, 5> h{0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    for (std::size_t offset = 0; offset < data.size(); offset += 64) {
        std::array<std::uint32_t, 80> w{};
        for (int i = 0; i < 16; ++i) {
            const auto p = offset + 4 * i;
            w[i] = (std::uint32_t(data[p]) << 24) | (std::uint32_t(data[p+1]) << 16) |
                   (std::uint32_t(data[p+2]) << 8) | std::uint32_t(data[p+3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = std::rotl(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
        auto [a, b, c, d, e] = h;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
            else { f = b ^ c ^ d; k = 0xca62c1d6; }
            const auto temp = std::rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = std::rotl(b, 30); b = a; a = temp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto word : h) out << std::setw(8) << word;
    return out.str();
}
}
