// SPDX-License-Identifier: GPL-3.0-or-later
// Milestone 19: the playthrough chain (plan decisions 2, 3 and 5). A
// playthrough is a chain of links, recordings/<us|br>/playthrough/link-NN.psrec
// from NN = 01, each replayed from power-on. Link 01 starts from the empty
// cartridge RAM; link N+1 starts from the cartridge RAM at the end of link N's
// replay with the edits of link-(N+1).edits applied. Only strength and money
// may be edited, as checked by apply_edits below.
//
//   playthrough_tests chain <rom> <folder> [<reference folder>]
//     The chain test: every link of the folder replayed in order, each start
//     image computed from the previous link and its edits and checked against
//     the link's cart-ram header. No link, a gap, a stray file or a refused
//     edit fails. With the reference folder (<reference>/<us|br>/playthrough)
//     the links replay side by side from their reference start images.
//   playthrough_tests apply <rom> <link-N.psrec> <start image|empty> <link-(N+1).edits> <new image>
//     The developer tool: replays link N from its start image, applies the next
//     link's edits with the chain test's own checks and writes the next start
//     image (never over an existing file); prints its SHA-1.
//   playthrough_tests plain <rom> <link.psrec> <reference link folder> <new work folder>
//     The link as psmodern.exe plays it: the app's frame driver with its save
//     folder, the original settings and no verification. A stop fails; the
//     last frame's seven components must equal the reference manifest's.
//   playthrough_tests records <rom> <image>
//     Prints the used save slots of an image and, for each active party record,
//     its values and whether they satisfy the reachable-value rule.
//   playthrough_tests extra <rom> <extra/<name>.psrec> <reference folder> [<new image>]
//     Part 3's recordings (recordings/<us|br>/playthrough/extra/<name>.psrec)
//     reach rare branches from a chain link's saved game with any bytes edited.
//     <name>.start beside it says how its start image is made: line 1
//     'psmodern-extra-start 1', line 2 'from link-NN', then 'byte' lines as in
//     an edits file. Link NN of the chain (the folder above extra/) is replayed
//     from its reference start image, the bytes are set, and the SHA-1 must be
//     the recording's cart-ram header. With a new image path the image is
//     written there (never over a file). A stray file in extra/ fails.
//
// Every missing input fails: this group never skips (exit 77 is not used).
// Offsets and addresses only; the level tables are read from the player's ROM.
#include "app/frame_driver.h"
#include "app/save_store.h"
#include "game/dungeon.h"
#include "game/routines.h"
#include "replay/checkpoint.h"
#include "replay/recording.h"
#include "replay/verification.h"
#include "rom/sha1.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace psmodern;
namespace d = game::dungeon;
using Image = Memory::CartridgeRam;

namespace {
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void check(bool condition, const std::string& message) { if (!condition) fail(message); }
std::string hex(unsigned value, int width) {
    std::ostringstream text;
    text << std::hex << std::setw(width) << std::setfill('0') << value;
    return text.str();
}

std::string read_text(const std::filesystem::path& path, const std::string& what) {
    std::ifstream in(path, std::ios::binary);
    check(bool(in), what + " is missing: " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
Image read_image(const std::filesystem::path& path) {
    const auto bytes = read_text(path, "The cartridge RAM image");
    check(bytes.size() == Image{}.size(), "A cartridge RAM image must be exactly 8192 bytes: " + path.string());
    Image image{};
    std::copy(bytes.begin(), bytes.end(), image.begin());
    return image;
}
void write_new_image(const std::filesystem::path& path, const Image& image) {
    check(!std::filesystem::exists(path), "The output image already exists (it is never replaced): " + path.string());
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    out.close();
    check(bool(out), "Cannot write " + path.string());
}

// ---- the ROM and its level tables -----------------------------------------------
struct Game {
    std::shared_ptr<Memory::Rom> rom;
    std::string sha1;
    RomVersion version;
};
Game load_game(const std::filesystem::path& path) {
    check(!path.empty() && std::filesystem::exists(path), "The ROM is missing: " + path.string());
    const auto loaded = app::load_rom(path);
    Game game{std::make_shared<Memory::Rom>(*loaded.rom), psmodern::sha1(*loaded.rom), loaded.version};
    check(game.version == RomVersion::us_rev_a || game.version == RomVersion::brazilian,
          "The playthrough runs on the US Rev-A or the Brazilian cartridge");
    return game;
}

// What the stat refresh at 00:17BA reads (party.md, "Recovering health"): the
// level rows of each character in one bank, eight bytes a level, and the
// equipment boosts. The operands are read from the refresh's own instructions.
class Levels {
public:
    explicit Levels(const Memory::Rom& rom) : rom_(rom) {
        check(at(0, 0x17BA) == 0x21 && word(0, 0x17BB) == 0xFFFF && at(0, 0x17BD) == 0x36,
              "00:17BA no longer maps the level bank");
        bank_ = at(0, 0x17BE);
        constexpr std::uint16_t loads[4]{0x17C3, 0x17CD, 0x17D7, 0x17E1};   // LD DE,table for each character
        for (unsigned k = 0; k < 4; ++k) {
            check(at(0, loads[k]) == 0x11, "00:17BA no longer loads a level table at " + hex(loads[k], 4));
            base_[k] = word(0, static_cast<std::uint16_t>(loads[k] + 1));
        }
        check(at(0, 0x1783) == 0xFE, "00:1783 no longer compares the level with the top level");
        top_ = at(0, 0x1784);
    }
    unsigned top() const { return top_; }
    // Byte i of the row of character k (record order) for level L.
    std::uint8_t row(unsigned k, unsigned level, unsigned i) const {
        return at(bank_, static_cast<std::uint16_t>(base_[k] + 8 * level + i));
    }
    // The experience total that reaches `level` (00:1774 compares the total
    // with the row of the level being reached).
    unsigned reaches(unsigned k, unsigned level) const { return row(k, level, 4) | row(k, level, 5) << 8; }
    std::uint8_t boost(std::uint8_t item) const { return at(0, static_cast<std::uint16_t>(0x183A + item)); }

private:
    std::uint8_t at(unsigned bank, std::uint16_t address) const {
        const std::size_t offset = address < 0x8000 ? address : bank * 0x4000u + (address - 0x8000u);
        return rom_.at(offset);
    }
    std::uint16_t word(unsigned bank, std::uint16_t address) const {
        return static_cast<std::uint16_t>(at(bank, address) | at(bank, static_cast<std::uint16_t>(address + 1)) << 8);
    }
    const Memory::Rom& rom_;
    unsigned bank_ = 0, top_ = 0;
    std::array<std::uint16_t, 4> base_{};
};

// ---- the save slots and the party records ----------------------------------------
// A slot is the 1,024-byte copy of work RAM C300-C6FF (game/dungeon.h).
constexpr std::uint16_t slot_base = 0xC300;
constexpr std::uint16_t records = 0xC400, record_bytes = 16, money = 0xC4E0;
enum Field : unsigned { flags = 0, hp = 1, mp = 2, exp_low = 3, exp_high = 4, level = 5, max_hp = 6, max_mp = 7,
                        attack = 8, defense = 9, weapon = 10, armor = 11, shield = 12 };
unsigned slot_offset(unsigned slot, std::uint16_t address) { return d::slot_offset(slot) + (address - slot_base); }
std::uint8_t field(const Image& image, unsigned slot, unsigned k, unsigned f) {
    return image[slot_offset(slot, static_cast<std::uint16_t>(records + record_bytes * k + f))];
}
bool active(const Image& image, unsigned slot, unsigned k) { return field(image, slot, k, flags) & 1; }

// The reachable-value rule for one record (interface: m19-wt/interface.md and
// development.md, "Playthrough"). Empty when it holds.
std::vector<std::string> unreachable(const Levels& levels, const Image& image, unsigned slot, unsigned k,
                                     const Image* before = nullptr) {
    std::vector<std::string> problems;
    auto f = [&](unsigned n) -> unsigned { return field(image, slot, k, n); };
    const unsigned lv = f(level), exp = f(exp_low) | f(exp_high) << 8;
    if (lv < 1 || lv > levels.top()) {
        problems.push_back("level " + std::to_string(lv) + " is outside 1-" + std::to_string(levels.top()));
        return problems;
    }
    if (before) {
        const unsigned old_level = field(*before, slot, k, level);
        const unsigned old_exp = field(*before, slot, k, exp_low) | field(*before, slot, k, exp_high) << 8;
        if (lv < old_level) problems.push_back("the level went down from " + std::to_string(old_level));
        if (exp < old_exp) problems.push_back("the experience went down from " + std::to_string(old_exp));
    }
    if (exp < levels.reaches(k, lv))
        problems.push_back("experience " + std::to_string(exp) + " is below the " + std::to_string(levels.reaches(k, lv)) +
                           " that reaches level " + std::to_string(lv));
    if (lv < levels.top() && exp >= levels.reaches(k, lv + 1))
        problems.push_back("experience " + std::to_string(exp) + " already reaches level " + std::to_string(lv + 1));
    const unsigned want_hp = levels.row(k, lv, 0), want_mp = levels.row(k, lv, 3);
    const unsigned want_attack = (levels.row(k, lv, 1) + levels.boost(f(weapon))) & 255;
    const unsigned want_defense = (levels.row(k, lv, 2) + levels.boost(f(armor)) + levels.boost(f(shield))) & 255;
    auto equal = [&](unsigned got, unsigned want, const char* what) {
        if (got != want) problems.push_back(std::string(what) + " " + std::to_string(got) + " is not the level's " + std::to_string(want));
    };
    equal(f(max_hp), want_hp, "maximum HP");
    equal(f(max_mp), want_mp, "maximum MP");
    equal(f(attack), want_attack, "attack");
    equal(f(defense), want_defense, "defense");
    if (f(hp) < 1 || f(hp) > f(max_hp)) problems.push_back("current HP " + std::to_string(f(hp)) + " is outside 1 to the maximum");
    if (f(mp) > f(max_mp)) problems.push_back("current MP " + std::to_string(f(mp)) + " is above the maximum");
    return problems;
}

// ---- edits ---------------------------------------------------------------------
using Edits = std::vector<std::pair<std::uint16_t, std::uint8_t>>;
// The byte lines of an edits file (links) or of a start file (part 3's
// recordings): ASCII, LF only, a final LF, the given header lines first.
// `from` receives the word after "from " of a start file's second line.
Edits read_edits(const std::filesystem::path& path, const std::string& header = "psmodern-link-edits 1",
                 std::string* from = nullptr) {
    const auto text = read_text(path, from ? "The start file" : "The edits file");
    check(!text.empty() && text.back() == '\n', path.string() + ": must end with a line feed");
    check(text.find('\r') == std::string::npos, path.string() + ": carriage returns are not allowed");
    std::istringstream lines(text);
    std::string line;
    std::getline(lines, line);
    check(line == header, path.string() + ": line 1 must be '" + header + "'");
    unsigned first = 2;
    if (from) {
        static const std::regex from_line("from (link-[0-9]{2})");
        std::smatch m;
        check(std::getline(lines, line) && std::regex_match(line, m, from_line),
              path.string() + " line 2: expected 'from link-NN'");
        *from = m[1];
        first = 3;
    }
    static const std::regex byte_line("byte ([0-9a-f]{4}) ([0-9a-f]{2})");
    Edits edits;
    for (unsigned number = first; std::getline(lines, line); ++number) {
        std::smatch m;
        const auto where = path.string() + " line " + std::to_string(number);
        check(std::regex_match(line, m, byte_line), where + ": expected 'byte <4 lowercase hex> <2 lowercase hex>'");
        const auto offset = static_cast<std::uint16_t>(std::stoul(m[1], nullptr, 16));
        check(offset < Image{}.size(), where + ": offset outside the 8192-byte cartridge RAM");
        check(edits.empty() || offset > edits.back().first, where + ": offsets must strictly increase");
        edits.emplace_back(offset, static_cast<std::uint8_t>(std::stoul(m[2], nullptr, 16)));
    }
    return edits;
}

// The start image of link N+1 from link N's start and end images and the
// edits, or an exception naming the first refused edit. The rule:
// - the slot in use is the one save slot whose bytes changed during link N;
//   its used flag must be set;
// - only its meseta (C4E0-C4E1) and, in its ACTIVE party records (bit 0 of the
//   flags set in link N's output), current HP and MP, experience, level, maximum
//   HP and MP, attack and defense may change; a dead member's record is
//   inactive (zero HP clears the flags), so it is never revived;
// - every edited record must satisfy the reachable-value rule, with no level
//   or experience lower than in link N's output.
Image apply_edits(const Levels& levels, const Image& start, const Image& end, const Edits& edits) {
    Image next = end;
    for (const auto& [offset, value] : edits) {
        check(end[offset] != value, "Edit at " + hex(offset, 4) + " repeats link N's own value " + hex(value, 2));
        next[offset] = value;
    }
    if (edits.empty()) return next;
    std::vector<unsigned> changed;
    for (unsigned slot = 1; slot <= d::save_slots; ++slot)
        if (!std::equal(start.begin() + d::slot_offset(slot), start.begin() + d::slot_offset(slot + 1), end.begin() + d::slot_offset(slot)))
            changed.push_back(slot);
    check(changed.size() == 1, "Edits need exactly one save slot written during link N; " +
                                   std::to_string(changed.size()) + " were written");
    const unsigned slot = changed.front();
    check(end[d::slot_used_flag(slot)] != 0, "The save slot in use (" + std::to_string(slot) + ") is not marked used");
    std::set<unsigned> edited_records;
    for (const auto& [offset, value] : edits) {
        const auto where = "Edit at " + hex(offset, 4) + " (slot " + std::to_string(slot) + "): ";
        check(offset >= d::slot_offset(slot) && offset < d::slot_offset(slot + 1), where + "outside the save slot in use");
        const auto address = static_cast<std::uint16_t>(slot_base + (offset - d::slot_offset(slot)));
        if (address == money || address == money + 1) continue;
        check(address >= records && address < records + 4 * record_bytes, where + "not meseta or a party record");
        const unsigned k = (address - records) / record_bytes, f = (address - records) % record_bytes;
        check(f >= hp && f <= defense, where + "field " + std::to_string(f) + " of party record " + std::to_string(k) +
                                            " is not HP, MP, experience, level, maximum HP or MP, attack or defense");
        check(active(end, slot, k), where + "party record " + std::to_string(k) +
                                        " is inactive (not joined, or dead: never revived)");
        edited_records.insert(k);
    }
    for (const auto k : edited_records) {
        const auto problems = unreachable(levels, next, slot, k, &end);
        if (!problems.empty()) {
            std::string text = "Party record " + std::to_string(k) + " after the edits is not reachable:";
            for (const auto& p : problems) text += "\n  " + p;
            fail(text);
        }
    }
    return next;
}

// ---- replay --------------------------------------------------------------------
Image start_image_of(const replay::Recording& recording, const std::optional<Image>& given) {
    const Image image = given.value_or(Image{});
    check(psmodern::sha1(image) == recording.cart_ram_sha1, "The start image's SHA-1 " + psmodern::sha1(image) +
                                                                " is not the link's cart-ram header " + recording.cart_ram_sha1);
    return image;
}
// The cartridge RAM at the last frame end of a plain replay (C++ segments
// only, no oracle): where the program saves it and the reference samples it.
Image replay_end(const Game& game, const replay::Recording& recording, const Image& start) {
    check(recording.rom_sha1 == game.sha1, "The link was recorded on another cartridge");
    check(recording.frames > 0, "A link has at least one frame");
    RoutineRegistry routines;
    check(game::register_routines(routines, game.sha1), "The ROM is not an identified game image");
    Machine machine(game.rom, start);
    Image end{};
    for (std::uint64_t n = 0; n < recording.frames; ++n) {
        const bool last = n + 1 == recording.frames;
        machine.run_frame(recording.input_at(n), routines, {}, {}, [&] {
            (void)machine.take_psg_writes(); (void)machine.take_samples();
            if (last) end = machine.memory().cartridge_ram();
        });
    }
    return end;
}

// ---- modes ---------------------------------------------------------------------
// With `reference` (the region's reference folder) the links are replayed side
// by side, each from its reference start image (whose SHA-1 must be the link's
// header); the chain is then checked in order exactly as without it, since an
// image with the header's SHA-1 is the image the chain computes.
int chain(const Game& game, const std::filesystem::path& folder, const std::optional<std::filesystem::path>& reference) {
    check(std::filesystem::is_directory(folder), "The playthrough folder is missing: " + folder.string());
    static const std::regex link_name("link-([0-9]{2})[.](psrec|edits)");
    std::map<unsigned, std::filesystem::path> links, edit_files;
    for (const auto& file : std::filesystem::directory_iterator(folder)) {
        const auto name = file.path().filename().string();
        if (name == "extra" && file.is_directory()) continue;   // part 3's recordings (mode extra)
        std::smatch m;
        check(file.is_regular_file() && std::regex_match(name, m, link_name), "A stray file in the playthrough folder: " + name);
        (m[2] == "psrec" ? links : edit_files)[static_cast<unsigned>(std::stoul(m[1]))] = file.path();
    }
    check(!links.empty(), "The chain is empty: no link in " + folder.string());
    const unsigned count = static_cast<unsigned>(links.size());
    check(links.begin()->first == 1 && links.rbegin()->first == count, "The links must be numbered from 01 with no gap");
    check(!edit_files.contains(1), "link-01 starts from the empty cartridge RAM and takes no edits");
    for (const auto& [n, path] : edit_files) check(links.contains(n), "Edits with no link: " + path.filename().string());
    // Everything is read and checked for form before the long replays.
    std::vector<replay::Recording> recordings(count + 1);
    std::vector<Edits> edits(count + 1);
    for (unsigned n = 1; n <= count; ++n) {
        recordings[n] = replay::read(links[n]);
        if (n == 1) continue;
        check(edit_files.contains(n), "The edits of link " + std::to_string(n) + " are missing (required even when empty)");
        edits[n] = read_edits(edit_files[n]);
    }
    check(recordings[1].cart_ram_sha1 == psmodern::sha1(Image{}),
          "link-01.psrec: its cart-ram header is not the empty image " + psmodern::sha1(Image{}));
    std::vector<Image> starts(count + 1), ends(count + 1);
    if (reference) {
        for (unsigned n = 2; n <= count; ++n) {
            char stem[8];
            std::snprintf(stem, sizeof stem, "link-%02u", n);
            starts[n] = start_image_of(recordings[n], read_image(*reference / stem / "cart-ram.bin"));
        }
        std::vector<std::thread> workers;
        std::vector<std::exception_ptr> errors(count + 1);
        std::atomic<unsigned> next{1};
        const unsigned threads = std::max(1u, std::min(count, std::thread::hardware_concurrency()));
        for (unsigned t = 0; t < threads; ++t)
            workers.emplace_back([&] {
                for (unsigned n; (n = next++) <= count;) {
                    try { ends[n] = replay_end(game, recordings[n], starts[n]); }
                    catch (...) { errors[n] = std::current_exception(); }
                }
            });
        for (auto& worker : workers) worker.join();
        for (const auto& error : errors) if (error) std::rethrow_exception(error);
    }
    const Levels levels(*game.rom);
    for (unsigned n = 1; n <= count; ++n) {
        const auto label = links[n].filename().string();
        if (!reference) ends[n] = replay_end(game, recordings[n], starts[n]);
        std::cout << label << ": " << recordings[n].frames << " frames from " << recordings[n].cart_ram_sha1 << ", ends "
                  << psmodern::sha1(ends[n]) << '\n';
        if (n == count) break;
        const auto next = apply_edits(levels, starts[n], ends[n], edits[n + 1]);
        check(psmodern::sha1(next) == recordings[n + 1].cart_ram_sha1,
              links[n + 1].filename().string() + ": its cart-ram header " + recordings[n + 1].cart_ram_sha1 +
                  " is not link " + std::to_string(n) + "'s end with its edits, " + psmodern::sha1(next));
        std::cout << "  " << edit_files[n + 1].filename().string() << ": " << edits[n + 1].size() << " bytes edited\n";
        if (!reference) starts[n + 1] = next;
    }
    std::cout << "The chain of " << count << " links holds.\n";
    return 0;
}

int apply(const Game& game, const std::filesystem::path& link, const std::string& start_path,
          const std::filesystem::path& edits_path, const std::filesystem::path& out) {
    const auto recording = replay::read(link);
    std::optional<Image> given;
    if (start_path != "empty") given = read_image(start_path);
    const auto start = start_image_of(recording, given);
    const auto end = replay_end(game, recording, start);
    const Levels levels(*game.rom);
    const auto next = apply_edits(levels, start, end, read_edits(edits_path));
    write_new_image(out, next);
    std::cout << psmodern::sha1(next) << '\n';
    return 0;
}

// A part 3 recording's start image: link NN's end (replayed from its reference
// start image, or the empty image for link 01) with the start file's bytes set.
// Any byte may change (these recordings exist to reach what the route cannot),
// but each edit must change its byte and the result must be the header's image.
int extra(const Game& game, const std::filesystem::path& recording_path, const std::filesystem::path& reference,
          const std::optional<std::filesystem::path>& out) {
    const auto folder = recording_path.parent_path();
    check(folder.filename() == "extra", "A part 3 recording lives in the playthrough folder's extra/: " + recording_path.string());
    static const std::regex extra_name("([a-z0-9]+(-[a-z0-9]+)*)[.](psrec|start)");
    std::set<std::string> recordings, starts;
    for (const auto& file : std::filesystem::directory_iterator(folder)) {
        const auto name = file.path().filename().string();
        std::smatch m;
        check(file.is_regular_file() && std::regex_match(name, m, extra_name), "A stray file in the extra folder: " + name);
        (m[3] == "psrec" ? recordings : starts).insert(m[1]);
    }
    check(recordings == starts, "Every recording in the extra folder needs its .start file and every .start file its recording");
    check(recordings.contains(recording_path.stem().string()) && recording_path.extension() == ".psrec",
          "The recording is missing or misnamed: " + recording_path.string());
    const auto recording = replay::read(recording_path);
    check(recording.rom_sha1 == game.sha1, "The recording was made on another cartridge");
    std::string from;
    auto start_path = recording_path;
    start_path.replace_extension(".start");
    const auto edits = read_edits(start_path, "psmodern-extra-start 1", &from);
    const auto link_path = folder.parent_path() / (from + ".psrec");
    const auto link = replay::read(link_path);
    std::optional<Image> given;
    if (from != "link-01") given = read_image(reference / from / "cart-ram.bin");
    const auto end = replay_end(game, link, start_image_of(link, given));
    Image image = end;
    for (const auto& [offset, value] : edits) {
        check(end[offset] != value, "Edit at " + hex(offset, 4) + " repeats " + from + "'s own value " + hex(value, 2));
        image[offset] = value;
    }
    check(psmodern::sha1(image) == recording.cart_ram_sha1, recording_path.filename().string() + ": its cart-ram header " +
              recording.cart_ram_sha1 + " is not " + from + "'s end with its start file's " + std::to_string(edits.size()) +
              " edits, " + psmodern::sha1(image));
    if (out) write_new_image(*out, image);
    std::cout << recording_path.filename().string() << ": starts from " << from << "'s end with " << edits.size()
              << " bytes edited, " << psmodern::sha1(image) << '\n';
    return 0;
}

// The seven components of the reference manifest's last frame, checking the
// frame count; the checkpoint run (a) validates the whole manifest.
replay::Manifest last_reference_frame(const std::filesystem::path& manifest_path, std::uint64_t frames) {
    std::ifstream in(manifest_path, std::ios::binary);
    check(bool(in), "The reference manifest is missing: " + manifest_path.string());
    std::string line, last;
    std::uint64_t rows = 0;
    for (unsigned number = 1; std::getline(in, line); ++number) {
        if (number == 1) check(line == "frame picture ram vram cram cart-ram cpu psg", "Invalid manifest header");
        if (number <= 5) continue;   // the header and the four provenance lines
        ++rows;
        last = line;
    }
    check(rows == frames, "The reference manifest has " + std::to_string(rows) + " frames, the link " + std::to_string(frames));
    std::istringstream row(last);
    std::string number;
    row >> number;
    replay::Manifest expected;
    for (auto& hash : expected) check(bool(row >> hash), "Invalid manifest row");
    return expected;
}

int plain(const Game& game, const std::filesystem::path& link, const std::filesystem::path& reference,
          const std::filesystem::path& work) {
    const auto recording = replay::read(link);
    check(recording.rom_sha1 == game.sha1, "The link was recorded on another cartridge");
    const auto expected = last_reference_frame(reference / "manifest.txt", recording.frames);
    std::optional<Image> given;
    if (recording.cart_ram_sha1 != psmodern::sha1(Image{}) || std::filesystem::exists(reference / "cart-ram.bin"))
        given = read_image(reference / "cart-ram.bin");
    const auto start = start_image_of(recording, given);
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);
    if (given) write_new_image(work / app::save_name(game.version), start);
    // As psmodern.exe plays: the frame driver with its save folder and the
    // original settings (no options file), routines and the host's hooks on.
    app::FrameDriver driver(game.rom, app::SaveStore(work, game.version));
    replay::FrameWrites writes;
    std::optional<replay::Manifest> at;
    for (std::uint64_t n = 0; n < recording.frames; ++n) {
        const bool last = n + 1 == recording.frames;
        driver.set_frame_probe(last ? app::FrameDriver::FrameProbe([&](const Machine& m) { at = replay::manifest(m, {}); })
                                    : app::FrameDriver::FrameProbe{});
        auto out = driver.run(recording.input_at(n));
        check(!driver.stopped(), "The game stopped at frame " + std::to_string(n + 1) + ": " + driver.stopped().value_or(""));
        check(!out.save_error, "The save failed at frame " + std::to_string(n + 1) + ": " + out.save_error.value_or(""));
        const auto frame_writes = writes.take(n + 1, std::move(out.writes));
        if (last) {
            check(at.has_value(), "The frame observer did not run");
            (*at)[6] = psmodern::sha1(frame_writes);
        }
    }
    (void)driver.flush();
    constexpr const char* names[]{"picture", "ram", "vram", "cram", "cart-ram", "cpu", "psg"};
    std::string differing;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if ((*at)[i] != expected[i]) differing += std::string(" ") + names[i];
    check(differing.empty(), "The last frame differs from the reference in:" + differing);
    std::cout << link.filename().string() << ": " << recording.frames << " frames played as the program plays them, no stop; "
              << "the last frame matches the reference (cart-ram " << (*at)[4] << ").\n";
    return 0;
}

int describe(const Game& game, const std::filesystem::path& image_path) {
    const auto image = read_image(image_path);
    const Levels levels(*game.rom);
    std::cout << image_path.filename().string() << " " << psmodern::sha1(image) << '\n';
    for (unsigned slot = 1; slot <= d::save_slots; ++slot) {
        if (!image[d::slot_used_flag(slot)]) continue;
        const auto meseta = image[slot_offset(slot, money)] | image[slot_offset(slot, money + 1)] << 8;
        std::cout << "slot " << slot << ": meseta " << meseta << '\n';
        for (unsigned k = 0; k < 4; ++k) {
            if (!active(image, slot, k)) { std::cout << "  record " << k << ": inactive\n"; continue; }
            auto f = [&](unsigned n) -> unsigned { return field(image, slot, k, n); };
            std::cout << "  record " << k << ": level " << f(level) << ", experience " << (f(exp_low) | f(exp_high) << 8)
                      << ", HP " << f(hp) << "/" << f(max_hp) << ", MP " << f(mp) << "/" << f(max_mp) << ", attack "
                      << f(attack) << ", defense " << f(defense) << ", equipment " << f(weapon) << " " << f(armor) << " "
                      << f(shield);
            const auto problems = unreachable(levels, image, slot, k);
            if (problems.empty()) std::cout << ": reachable\n";
            else for (const auto& p : problems) std::cout << "\n    NOT REACHABLE: " << p;
            if (!problems.empty()) std::cout << '\n';
        }
    }
    return 0;
}
}

int main(int argc, char** argv) {
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode == "chain" && (argc == 4 || argc == 5))
            return chain(load_game(argv[2]), argv[3], argc == 5 ? std::optional<std::filesystem::path>(argv[4]) : std::nullopt);
        if (mode == "apply" && argc == 7) return apply(load_game(argv[2]), argv[3], argv[4], argv[5], argv[6]);
        if (mode == "plain" && argc == 6) return plain(load_game(argv[2]), argv[3], argv[4], argv[5]);
        if (mode == "records" && argc == 4) return describe(load_game(argv[2]), argv[3]);
        if (mode == "extra" && (argc == 5 || argc == 6))
            return extra(load_game(argv[2]), argv[3], argv[4], argc == 6 ? std::optional<std::filesystem::path>(argv[5]) : std::nullopt);
        throw std::runtime_error("usage: playthrough_tests chain <rom> <folder> [<reference folder>] | apply <rom> <link> <start|empty> <edits> <new image> | "
                                 "plain <rom> <link> <reference folder> <work folder> | records <rom> <image> | "
                                 "extra <rom> <extra recording> <reference folder> [<new image>]");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
