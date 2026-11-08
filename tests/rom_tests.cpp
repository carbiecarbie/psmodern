// SPDX-License-Identifier: GPL-3.0-or-later
#include "rom/rom.h"
#include "rom/sha1.h"
#include "rom/sha256.h"
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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
}
std::filesystem::path unique_folder(const std::filesystem::path& parent) {
    std::filesystem::create_directories(parent);
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    for (unsigned attempt=0;attempt<32;++attempt) {
        auto path=parent/("run-"+std::to_string(stamp)+"-"+std::to_string(attempt));
        if (std::filesystem::create_directory(path)) return path;
    }
    throw std::runtime_error("Cannot create a unique temporary test folder");
}
void cleanup(const std::filesystem::path& path) {
    for (unsigned attempt=0;attempt<6;++attempt) {
        std::error_code error;
        std::filesystem::remove_all(path,error);
        if (!error) return;
        if (attempt==5) throw std::filesystem::filesystem_error("Temporary test folder cleanup failed",path,error);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
}
// The handle-lock regression releases this gate only after the test body has
// finished and the parent owns a lock on this run's directory.
void wait_for_cleanup_gate(const std::filesystem::path& directory,const std::filesystem::path& gate) {
    auto ready=gate;ready+=".ready";
    auto staged=ready;staged+=".tmp";
    {
        std::ofstream out(staged,std::ios::binary);
        out<<directory.filename().string();
        if (!out) throw std::runtime_error("Cannot publish cleanup gate readiness");
    }
    std::filesystem::rename(staged,ready);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (!std::filesystem::exists(gate)) {
        if (std::chrono::steady_clock::now()>=deadline) throw std::runtime_error("Cleanup gate timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
}
int main(int argc, char** argv) {
    std::filesystem::path directory;
    try {
        if (argc!=2 && !(argc==4 && std::string_view(argv[2])=="--cleanup-gate"))
            throw std::runtime_error("Expected test directory [--cleanup-gate file]");
        directory=unique_folder(argv[1]);run(directory);
        if (argc==4) wait_for_cleanup_gate(directory,argv[3]);
        cleanup(directory);
        std::cout << "ROM and SHA-1 tests passed.\n";return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; // Always retain the original test failure.
        if (!directory.empty()) {
            try {cleanup(directory);} catch (const std::exception& error) {std::cerr<<error.what()<<'\n';}
        }
        return 1;
    }
}
