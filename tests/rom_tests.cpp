// SPDX-License-Identifier: GPL-3.0-or-later
#include "rom/rom.h"
#include "rom/sha1.h"
#include "rom/sha256.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string hash(const std::string& s) {
    return psmodern::sha1({reinterpret_cast<const std::uint8_t*>(s.data()), s.size()});
}
void refused(const std::filesystem::path& path, const std::string& message) {
    try { (void)psmodern::identify_rom(path); }
    catch (const std::exception& e) { check(std::string(e.what()).find(message) != std::string::npos, "Wrong refusal message"); return; }
    throw std::runtime_error("Invalid input was accepted");
}
void run(const std::filesystem::path& dir) {
    const auto sha256=[](const std::string& s) {return psmodern::sha256({reinterpret_cast<const std::uint8_t*>(s.data()),s.size()});};
    check(sha256("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA-256 empty");
    check(sha256("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 abc");
    check(sha256(std::string(1000000,'a'))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA-256 multi-block");
    check(hash("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709", "SHA-1 empty");
    check(hash("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 abc");
    check(hash("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "84983e441c3bd26ebaae4aa1f95129e5e54670f1", "SHA-1 multi-block padding");
    check(hash(std::string(1000000, 'a')) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f", "SHA-1 million a");
    check(psmodern::identify_digest("07fcf297be4f4c9d92cd3f119a7ac48467e06838") == psmodern::RomVersion::us_rev_a, "US digest");
    check(psmodern::identify_digest("fd8dad6acb6fa75dc8e9bbaea2a7e9fd486fc2dd") == psmodern::RomVersion::brazilian, "Brazilian digest");
    std::filesystem::create_directories(dir);
    { std::ofstream out(dir / "short.sms", std::ios::binary); out << "synthetic"; }
    { std::ofstream out(dir / "unknown.sms", std::ios::binary); std::vector<char> zeros(512 * 1024); out.write(zeros.data(), zeros.size()); }
    refused(dir / "archive.ZIP", "unzip");
    refused(dir / "other.bin", ".sms");
    refused(dir / "missing.sms", "Cannot open");
    refused(dir / "short.sms", "Wrong ROM size");
    refused(dir / "unknown.sms", "Unknown ROM dump");
    std::filesystem::remove_all(dir);
}
}
int main(int argc, char** argv) {
    try { if (argc != 2) throw std::runtime_error("Missing test directory"); run(argv[1]); std::cout << "ROM and SHA-1 tests passed.\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
