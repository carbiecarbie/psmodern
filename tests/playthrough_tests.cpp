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
//     the links replay side by side from their reference start images. Then
//     what the route shows is asserted (route_assertions below): the
//     companion's rescue, the party after the first final boss and at the
//     finale, and the last link through the credits' end marker to the title.
//   playthrough_tests links <rom> <folder> [<reference folder>]
//     The chain test without the route assertions (synthetic chains).
//   playthrough_tests apply <rom> <link-N.psrec> <start image|empty> <link-(N+1).edits> <new image>
//     The developer tool: replays link N from its start image, applies the next
//     link's edits with the chain test's own checks and writes the next start
//     image (never over an existing file); prints its SHA-1.
//   playthrough_tests plain <rom> <link.psrec> <reference link folder> <work folder>
//     The link as psmodern.exe plays it: the app's frame driver with its save
//     folder, the original settings and no verification. A stop, a failed
//     save or a failed final flush fails; the last frame's seven components
//     must equal the reference manifest's. The work folder must be new or one
//     this tool made (own_work_folder): nothing else is ever removed.
//   playthrough_tests rules <rom> <work folder>
//     The rules' own test (normal suite, no long replay): synthetic save
//     images built from the player's ROM and one-frame links, each rule shown
//     refusing its negative case and admitting its positive one; the plain
//     runner's folder ownership and final flush check. Exit 77 without a ROM.
//   playthrough_tests inventory <inventory> <recordings folder> <routes file>
//     The group's committed inventory (tests/playthrough_inventory.txt) must
//     list exactly the links, part 3 recordings and routes found.
//   playthrough_tests records <rom> <image>
//     Prints the used save slots of an image and, for each active party record,
//     its values and whether they satisfy the reachable-value rule.
//   playthrough_tests extra <rom> <extra/<name>.psrec> <reference folder> [<new image>]
//     Part 3's recordings (recordings/<us|br>/playthrough/extra/<name>.psrec)
//     reach rare branches from a chain link's saved game with any bytes edited.
//     <name>.start beside it says how its start image is made and what its
//     class is (read_extra_start: version 2, 'from link-NN', 'class', 'basis',
//     then 'byte' lines as in an edits file). Link NN of the chain (the folder
//     above extra/) is replayed from its reference start image, its end is
//     checked against the cart-ram of the last frame of link NN's reference
//     manifest, the bytes are set, and the SHA-1 must be the recording's
//     cart-ram header. With a new image path the image is written there (never
//     over a file). A stray file in extra/ fails. The by-hand way to make a new
//     recording's start image.
//   playthrough_tests start <rom> <extra/<name>.psrec> <reference folder>
//     The start-image test (the _start tests): what `extra` establishes, with
//     link NN's end rebuilt from the reference start images instead of
//     replayed (rebuilt_end; replayed when that is not possible) and
//     authenticated the same way before use. Seconds instead of a replay.
//   playthrough_tests start-rules <work folder>
//     The start file's form and the start-image test's checks shown refusing
//     their negative cases on a synthetic chain (normal suite, no ROM).
//
// Every missing input fails: this group never skips (exit 77 is used only by
// `rules`, which belongs to the normal suite). Offsets and addresses only; the
// level tables and the experience awards are read from the player's ROM.
//
// What the checks establish (review of 2026-11-25). Every check below is a
// necessary condition on INDIVIDUAL EDITED FIELDS: a value no play could
// produce is refused. None of them shows that a real play path produces the
// COMPLETE SAVE IMAGE an edit makes (for example the spell counts of a raised
// level, which the allow-list keeps as link N left them; development.md,
// "Playthrough chain", states the image-level limits).
#include "app/frame_driver.h"
#include "app/save_store.h"
#include "game/dungeon.h"
#include "game/routines.h"
#include "replay/checkpoint.h"
#include "replay/recording.h"
#include "replay/verification.h"
#include "rom/sha1.h"
#include "rom/sha256.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <functional>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <span>
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

// A byte or word of the ROM at bank:address (slots 0 and 1 hold banks 0 and 1;
// an address from 8000h is in `bank`).
std::uint8_t rom_at(const Memory::Rom& rom, unsigned bank, std::uint16_t address) {
    const std::size_t offset = address < 0x8000 ? address : bank * 0x4000u + (address - 0x8000u);
    return rom.at(offset);
}
std::uint16_t rom_word(const Memory::Rom& rom, unsigned bank, std::uint16_t address) {
    return static_cast<std::uint16_t>(rom_at(rom, bank, address) | rom_at(rom, bank, static_cast<std::uint16_t>(address + 1)) << 8);
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
    std::uint8_t at(unsigned bank, std::uint16_t address) const { return rom_at(rom_, bank, address); }
    std::uint16_t word(unsigned bank, std::uint16_t address) const { return rom_word(rom_, bank, address); }
    const Memory::Rom& rom_;
    unsigned bank_ = 0, top_ = 0;
    std::array<std::uint16_t, 4> base_{};
};

// The experience awards the game can give (party.md, "Giving experience after
// a battle"; ROM static, the same code in both cartridges). The only store to
// the reward at C2D0 is the battle setup 01:5FFE: it selects the enemy's
// 32-byte record (the bank is the operand of the store at 01:6001, the table
// base that of the load at 01:602C, the identifier is 1-127), takes the count
// from the record's byte 20 (bit 7 set: the fixed count in the low four bits,
// 01:6065; clear: a random count from 1 to the low four bits, at most twice the
// party size and at most 8, 01:6069-607F), multiplies the record's experience
// word (bytes 28-29) by the count (01:60D0 calls the multiplication at
// 00:044C, which keeps the low 16 bits) and stores the product (01:60D3). The
// award (00:170D) gives that whole reward to every active record, not a share
// (00:1754), and the addition at 00:1773 stops at 65,535; each award raises
// the level by one at most (00:1774). So a record's experience grows only by
// sums of these products (every count from 1 to the cap is taken: a superset
// of what a party of any size meets, so the check is a necessary condition).
class Awards {
public:
    explicit Awards(const Memory::Rom& rom) {
        auto at = [&](std::uint16_t address) { return rom_at(rom, 1, address); };
        auto word = [&](std::uint16_t address) { return rom_word(rom, 1, address); };
        check(at(0x6001) == 0x36 && at(0x602C) == 0x11 && at(0x6027) == 0x29 && at(0x6028) == 0x29 && at(0x6029) == 0x29 &&
                  at(0x602A) == 0x29 && at(0x602B) == 0x29,
              "01:5FFE no longer selects a 32-byte enemy record");
        check(at(0x6065) == 0xCB && at(0x6066) == 0x7F && at(0x6069) == 0xE6 && at(0x606A) == 0x0F && at(0x6078) == 0xE6 &&
                  at(0x6079) == 0x07 && at(0x607F) == 0xE6 && at(0x6080) == 0x0F,
              "01:6065 no longer chooses the enemy count");
        check(at(0x60D0) == 0xCD && word(0x60D1) == 0x044C && at(0x60D3) == 0x22 && word(0x60D4) == 0xC2D0,
              "01:60D0 no longer multiplies the experience by the count and stores the reward at C2D0");
        const unsigned bank = at(0x6002);
        const std::uint16_t base = word(0x602D);
        std::set<unsigned> values;
        for (unsigned id = 1; id < 128; ++id) {
            const auto record = static_cast<std::uint16_t>(base + 32 * id);
            const std::uint8_t count = rom_at(rom, bank, static_cast<std::uint16_t>(record + 20));
            const unsigned experience = rom_word(rom, bank, static_cast<std::uint16_t>(record + 28));
            const unsigned low = count & 0x0F;
            const unsigned first = count & 0x80 ? low : 1, last = count & 0x80 ? low : std::min(low, 8u);
            for (unsigned n = first; n >= 1 && n <= last; ++n)
                if (const unsigned reward = experience * n & 0xFFFF) values.insert(reward);
        }
        check(!values.empty(), "No experience award found in the enemy records");
        awards_.assign(values.begin(), values.end());
        // most_[s]: the largest number of awards whose sum is exactly s, or -1.
        most_.assign(0x10000, -1);
        most_[0] = 0;
        for (unsigned s = 1; s < most_.size(); ++s)
            for (const auto a : awards_) {
                if (a > s) break;
                if (most_[s - a] >= 0) most_[s] = std::max(most_[s], most_[s - a] + 1);
            }
    }
    // The largest number of awards whose sum is exactly `gain`, or -1 when no
    // sum of awards is `gain`.
    int most(unsigned gain) const { return gain < most_.size() ? most_[gain] : -1; }
    unsigned smallest() const { return awards_.front(); }
    std::size_t count() const { return awards_.size(); }

private:
    std::vector<unsigned> awards_;
    std::vector<int> most_;
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

// The reachable-value rule for one record (development.md, "Playthrough
// chain"). Empty when it holds. Each line checks one field (or the fields one
// table row fixes); what each establishes:
// - level within 1 and the top level, never lower than link N's: the game has
//   no other levels and never lowers one;
// - experience never lower than link N's, and the gain from link N's zero or
//   a sum of awards the game gives (Awards): the gain is not one the game
//   cannot give. It does not show that those battles were fought, nor that
//   their order allowed the levels gained (one an award at most);
// - experience inside the level's band (at least the threshold of the level,
//   below the next one): a policy stricter than the game, which keeps any
//   surplus of a large award until the next award (party.md);
// - maximum HP and MP, attack and defense equal what the refresh 00:17BA
//   computes for that level and equipment: what any refresh leaves;
// - current HP from 1 to the maximum, MP from 0 to the maximum.
std::vector<std::string> unreachable(const Levels& levels, const Image& image, unsigned slot, unsigned k,
                                     const Image* before = nullptr, const Awards* awards = nullptr) {
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
        // An award that passes 65,535 stores 65,535 (00:1773): the ceiling is
        // reachable from any lower total once anything is gained.
        if (awards && exp > old_exp && exp != 0xFFFF && awards->most(exp - old_exp) < 0)
            problems.push_back("the experience gained since link N, " + std::to_string(exp - old_exp) +
                               ", is not a sum of awards the game gives (enemy experience times the count; the smallest is " +
                               std::to_string(awards->smallest()) + ")");
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
// The text of an edits file (links) or of a start file (part 3's recordings):
// LF only and a final LF.
std::string line_text(const std::filesystem::path& path, const std::string& what) {
    const auto text = read_text(path, what);
    check(!text.empty() && text.back() == '\n', path.string() + ": must end with a line feed");
    check(text.find('\r') == std::string::npos, path.string() + ": carriage returns are not allowed");
    return text;
}
// The byte lines that end both formats, from line number `first`: offsets
// strictly increasing inside the cartridge RAM.
Edits read_byte_lines(std::istringstream& lines, const std::filesystem::path& path, unsigned first) {
    std::string line;
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
// A link's edits file: line 1 'psmodern-link-edits 1', then byte lines.
Edits read_edits(const std::filesystem::path& path) {
    std::istringstream lines(line_text(path, "The edits file"));
    std::string line;
    std::getline(lines, line);
    check(line == "psmodern-link-edits 1", path.string() + ": line 1 must be 'psmodern-link-edits 1'");
    return read_byte_lines(lines, path, 2);
}

// The start file of a part 3 recording, extra/<name>.start (version 2,
// development.md, "Part 3's recordings"): printable ASCII and LF only, a final
// LF, no comment and no blank line, in this order:
//   psmodern-extra-start 2
//   from link-NN
//   class <chain-save|edited|artificial>
//   basis <one line: where the start image comes from>
//   byte <4 lowercase hex> <2 lowercase hex>      (zero or more, as in an edits file)
// The class is `chain-save` exactly when there is no byte line. The basis is
// not empty, has no leading or trailing space and holds no extracted location
// data: location_data below is the rule LOCATION of tools/generate_coverage.py,
// character for character (tests/coverage_tests.py compares the two). Version
// 1 (no class and basis lines; the class lived in a separate table) is refused.
constexpr const char* extra_start_header = "psmodern-extra-start 2";
const std::set<std::string> extra_classes{"chain-save", "edited", "artificial"};
const std::regex& location_data() {
    static const std::regex rule(
        R"(\bE\d+:|\brow \d+ col \d+|\bcell [0-9A-F]{2}\b|\(\s*[0-9A-F]{2}\s*,\s*[0-9A-F]{2}\s*\)|\blocation \d|\barea \d)");
    return rule;
}
struct ExtraStart {
    std::string from;            // link-NN
    std::string start_class;     // one of extra_classes
    std::string basis;
    Edits edits;
};
ExtraStart read_extra_start(const std::filesystem::path& path) {
    const auto text = line_text(path, "The start file");
    const auto name = path.string();
    check(std::all_of(text.begin(), text.end(), [](char c) { return c == '\n' || (c >= 0x20 && c <= 0x7E); }),
          name + ": printable ASCII and line feeds only");
    std::istringstream lines(text);
    std::string line;
    std::getline(lines, line);
    check(line != "psmodern-extra-start 1", name + ": version 1 (no class and basis lines) is no longer read; line 1 must be '" +
                                                extra_start_header + "'");
    check(line == extra_start_header, name + ": line 1 must be '" + extra_start_header + "'");
    ExtraStart start;
    static const std::regex from_line("from (link-[0-9]{2})");
    std::smatch m;
    check(std::getline(lines, line) && std::regex_match(line, m, from_line), name + " line 2: expected 'from link-NN'");
    start.from = m[1];
    check(std::getline(lines, line) && line.starts_with("class "),
          name + " line 3: expected 'class <chain-save|edited|artificial>' (the class is required)");
    start.start_class = line.substr(6);
    check(extra_classes.contains(start.start_class),
          name + " line 3: unknown class '" + start.start_class + "' (chain-save, edited or artificial)");
    check(std::getline(lines, line) && line.starts_with("basis "), name + " line 4: expected 'basis <text>' (the basis is required)");
    start.basis = line.substr(6);
    check(!start.basis.empty() && start.basis.front() != ' ' && start.basis.back() != ' ',
          name + " line 4: the basis is empty or has a leading or trailing space");
    check(!std::regex_search(start.basis, location_data()),
          name + " line 4: the basis holds extracted location data (keep it in the private study)");
    start.edits = read_byte_lines(lines, path, 5);
    check((start.start_class == "chain-save") == start.edits.empty(),
          name + ": class " + start.start_class + (start.edits.empty() ? " needs at least one byte line" : " has byte lines") +
              " (chain-save is exactly a start file with no byte line)");
    return start;
}

// The save slots whose bytes differ between two images.
std::vector<unsigned> changed_slots(const Image& start, const Image& end) {
    std::vector<unsigned> changed;
    for (unsigned slot = 1; slot <= d::save_slots; ++slot)
        if (!std::equal(start.begin() + d::slot_offset(slot), start.begin() + d::slot_offset(slot + 1), end.begin() + d::slot_offset(slot)))
            changed.push_back(slot);
    return changed;
}
// The one-slot rule of a link that another link continues: link N must write
// exactly one save slot (the save link N+1 continues from), marked used. It
// holds for every nonterminal link, whether or not the next edits file is
// empty (review 2026-11-25: two one-frame links from the empty image joined
// by an empty edits file were accepted). Returns the slot.
unsigned written_slot(const Image& start, const Image& end) {
    const auto changed = changed_slots(start, end);
    check(changed.size() == 1, "A link that the next link continues must write exactly one save slot; " +
                                   std::to_string(changed.size()) + " were written");
    const unsigned slot = changed.front();
    check(end[d::slot_used_flag(slot)] != 0, "The save slot written (" + std::to_string(slot) + ") is not marked used");
    return slot;
}

// The start image of link N+1 from link N's start and end images and the
// edits, or an exception naming the first refused edit. The rule:
// - the slot in use is the one save slot whose bytes changed during link N;
//   its used flag must be set (written_slot, checked even with no edits);
// - only its meseta (C4E0-C4E1) and, in its ACTIVE party records (bit 0 of the
//   flags set in link N's output), current HP and MP, experience, level, maximum
//   HP and MP, attack and defense may change; a dead member's record is
//   inactive (zero HP clears the flags), so it is never revived;
// - every edited record must satisfy the reachable-value rule, with no level
//   or experience lower than in link N's output and the experience gained a
//   sum of awards (unreachable above).
Image apply_edits(const Levels& levels, const Awards& awards, const Image& start, const Image& end, const Edits& edits) {
    const unsigned slot = written_slot(start, end);
    Image next = end;
    for (const auto& [offset, value] : edits) {
        check(end[offset] != value, "Edit at " + hex(offset, 4) + " repeats link N's own value " + hex(value, 2));
        next[offset] = value;
    }
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
        const auto problems = unreachable(levels, next, slot, k, &end, &awards);
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
// ---- what the route shows ---------------------------------------------------------
// Machine-state observations of one link's replay, taken at every frame end
// from the work RAM and from the link's own registry (calls of a segment
// entry, or of one of its exits), never from frame numbers: the same criteria
// hold for both cartridges. The entries (scenes.md, interactions.md,
// battle.md): 01:5538 the item place's call of the field menu (the petrified
// companion's place is one of its six events); 00:2435 Alsulin, which unlocks
// the companion's record; 01:5524 the store of the won flag C516 after the
// fixed encounter of 01:5502, the first of the two final bosses (its enemy
// record is the one named Lassic: Inference from the player's ROM, the name
// is not stored here), reached only when the battle did not end in defeat
// (01:54F2 leaves the interaction on a defeat); 01:454E the finale (its final
// conversation, then the credits route); 01:4668 the credits route's end
// marker; 01:4676 the finale storing the title's game mode.
constexpr RoutineEntry item_place{1, 0x5538}, alsulin{0, 0x2435}, first_boss_won{1, 0x5524}, finale_entry{1, 0x454E},
    credits_end_marker{1, 0x4668}, finale_done{1, 0x4676};
constexpr unsigned companion = 2;               // the third party record (records from C400, 16 bytes each)
constexpr std::uint8_t title_mode = 3;          // the title waiting for its menu (as the party-defeat checkpoint observes it)
struct LinkEvents {
    std::optional<std::uint64_t> companion;     // the first frame end with the companion's record active
    bool companion_place = false, companion_item = false;   // 01:5538 and 00:2435 had run by then
    std::optional<std::uint64_t> boss;          // the first frame end after the first final boss was won
    bool boss_party = false;                    // all four records active with HP above zero there
    std::optional<std::uint64_t> finale;        // the first frame end after the finale began
    bool finale_party = false;
    std::optional<std::uint64_t> end_marker;    // the first frame end after the credits' end marker
    std::optional<std::uint64_t> title;         // the first frame end after that in the title mode
};
bool party_of_four(const Machine& m) {
    for (std::uint16_t k = 0; k < 4; ++k) {
        const auto record = static_cast<std::uint16_t>(records + record_bytes * k);
        if (!(m.peek(record) & 1) || !m.peek(static_cast<std::uint16_t>(record + hp))) return false;
    }
    return true;
}

// The cartridge RAM at the last frame end of a plain replay (C++ segments
// only, no oracle): where the program saves it and the reference samples it.
Image replay_end(const Game& game, const replay::Recording& recording, const Image& start, LinkEvents* events = nullptr) {
    check(recording.rom_sha1 == game.sha1, "The link was recorded on another cartridge");
    check(recording.frames > 0, "A link has at least one frame");
    RoutineRegistry routines;
    check(game::register_routines(routines, game.sha1), "The ROM is not an identified game image");
    auto entry = [&](RoutineEntry at) -> const RegisteredRoutine& {
        const auto* found = routines.find(at);
        check(found != nullptr, "The route assertions need the registered entry " + hex(at.bank, 2) + ":" + hex(at.address, 4));
        return *found;
    };
    const auto &place = entry(item_place), &item = entry(alsulin), &boss = entry(first_boss_won), &finale = entry(finale_entry),
               &marker = entry(credits_end_marker), &done = entry(finale_done);
    // The shape of the first final boss's encounter in the player's ROM: 01:54FB
    // reads the won flag C516, 01:5502 stores the enemy group and calls the
    // battle setup 01:5FFE, 01:5524 sets the flag.
    {
        auto at = [&](std::uint16_t address) { return rom_at(*game.rom, 1, address); };
        check(at(0x54FB) == 0x3A && rom_word(*game.rom, 1, 0x54FC) == 0xC516 && at(0x5502) == 0x3E && at(0x5504) == 0x32 &&
                  rom_word(*game.rom, 1, 0x5505) == 0xC2E6 && at(0x5507) == 0xCD && rom_word(*game.rom, 1, 0x5508) == 0x5FFE &&
                  at(0x5526) == 0x32 && rom_word(*game.rom, 1, 0x5527) == 0xC516,
              "01:54FB-5528 is no longer the fixed encounter with its won flag C516");
    }
    Machine machine(game.rom, start);
    Image end{};
    const auto companion_record = static_cast<std::uint16_t>(records + record_bytes * companion);
    for (std::uint64_t n = 0; n < recording.frames; ++n) {
        const bool last = n + 1 == recording.frames;
        machine.run_frame(recording.input_at(n), routines, {}, {}, [&] {
            (void)machine.take_psg_writes(); (void)machine.take_samples();
            if (last) end = machine.memory().cartridge_ram();
            if (!events) return;
            const auto frame = n + 1;
            auto& e = *events;
            if (!e.companion && (machine.peek(companion_record) & 1)) {
                e.companion = frame;
                e.companion_place = place.counters.cpp_calls > 0;
                e.companion_item = item.counters.cpp_calls > 0;
            }
            if (!e.boss && boss.counters.cpp_calls > 0) { e.boss = frame; e.boss_party = party_of_four(machine); }
            if (!e.finale && finale.counters.cpp_calls > 0) { e.finale = frame; e.finale_party = party_of_four(machine); }
            if (!e.end_marker && marker.counters.cpp_calls > 0) e.end_marker = frame;
            if (e.end_marker && !e.title && done.counters.cpp_calls > 0 && machine.peek(d::game_mode) == title_mode) e.title = frame;
        });
    }
    return end;
}

// The committed assertions of what the route shows (review 2026-11-25,
// optional hardening), over the links in order:
// - the companion's rescue: the first link in which the companion's record is
//   active at a frame end had run the item place 01:5538 and Alsulin 00:2435
//   before it (a save that merely holds the companion would not);
// - the victory over the first of the two final bosses (01:5524) leaves all
//   four records active with HP above zero, and so does the start of the
//   finale (after the second);
// - the last link, and only it, runs the credits route to its end marker and
//   then reaches the title mode after the finale stores it: a defeat that
//   returns to the title, or a chain cut at a valid save, fails.
void route_assertions(const std::vector<LinkEvents>& events, const std::vector<std::string>& labels) {
    const unsigned count = static_cast<unsigned>(events.size()) - 1;
    auto first = [&](auto has) -> unsigned {
        for (unsigned n = 1; n <= count; ++n) if (has(events[n])) return n;
        return 0;
    };
    const auto rescue = first([](const LinkEvents& e) { return e.companion.has_value(); });
    check(rescue != 0, "Route: the companion's record never becomes active");
    check(events[rescue].companion_place && events[rescue].companion_item,
          "Route: " + labels[rescue] + " has the companion active without the item place 01:5538 and Alsulin 00:2435 before it");
    std::cout << "Route: the companion joins in " << labels[rescue] << " at frame " << *events[rescue].companion
              << ", after the item place 01:5538 and Alsulin.\n";
    const auto boss = first([](const LinkEvents& e) { return e.boss.has_value(); });
    check(boss != 0, "Route: the first final boss (01:5502) is never won");
    check(events[boss].boss_party, "Route: after the first final boss in " + labels[boss] +
                                       " not all four records are active with HP above zero");
    std::cout << "Route: the first final boss (01:5502) won in " << labels[boss] << " (frame " << *events[boss].boss
              << "), all four members active with HP above zero.\n";
    const auto finale = first([](const LinkEvents& e) { return e.finale.has_value(); });
    check(finale != 0, "Route: the finale 01:454E never begins");
    check(events[finale].finale_party, "Route: at the finale in " + labels[finale] +
                                           " not all four records are active with HP above zero");
    for (unsigned n = 1; n < count; ++n)
        check(!events[n].end_marker, "Route: " + labels[n] + " reaches the credits' end marker but is not the last link");
    const auto& end = events[count];
    check(end.end_marker.has_value(), "Route: the last link, " + labels[count] + ", does not reach the credits' end marker 01:4668");
    check(end.title.has_value(), "Route: the last link does not reach the title after the credits");
    std::cout << "Route: the finale begins in " << labels[finale] << " (frame " << *events[finale].finale
              << ") with all four members active; " << labels[count] << " reaches the credits' end marker at frame "
              << *end.end_marker << " and the title at frame " << *end.title << ".\n";
}

// ---- modes ---------------------------------------------------------------------
// With `reference` (the region's reference folder) the links are replayed side
// by side, each from its reference start image (whose SHA-1 must be the link's
// header); the chain is then checked in order exactly as without it, since an
// image with the header's SHA-1 is the image the chain computes.
// The last link has no next link: it may end without saving (the real chains
// end on the title after the credits, the cartridge RAM unchanged); a save it
// does make must still be one slot (written_slot). Every other link must write
// one slot whether or not the next edits file is empty.
int chain(const Game& game, const std::filesystem::path& folder, const std::optional<std::filesystem::path>& reference,
          bool assert_route) {
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
    std::vector<LinkEvents> events(count + 1);
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
                    try { ends[n] = replay_end(game, recordings[n], starts[n], &events[n]); }
                    catch (...) { errors[n] = std::current_exception(); }
                }
            });
        for (auto& worker : workers) worker.join();
        for (const auto& error : errors) if (error) std::rethrow_exception(error);
    }
    const Levels levels(*game.rom);
    const Awards awards(*game.rom);
    std::vector<std::string> labels(count + 1);
    for (unsigned n = 1; n <= count; ++n) {
        const auto label = labels[n] = links[n].filename().string();
        if (!reference) ends[n] = replay_end(game, recordings[n], starts[n], &events[n]);
        std::cout << label << ": " << recordings[n].frames << " frames from " << recordings[n].cart_ram_sha1 << ", ends "
                  << psmodern::sha1(ends[n]) << '\n';
        if (n == count) {
            if (changed_slots(starts[n], ends[n]).empty()) std::cout << "  the last link: the cartridge RAM is unchanged\n";
            else std::cout << "  the last link: save slot " << written_slot(starts[n], ends[n]) << " written\n";
            break;
        }
        const auto next = apply_edits(levels, awards, starts[n], ends[n], edits[n + 1]);
        check(psmodern::sha1(next) == recordings[n + 1].cart_ram_sha1,
              links[n + 1].filename().string() + ": its cart-ram header " + recordings[n + 1].cart_ram_sha1 +
                  " is not link " + std::to_string(n) + "'s end with its edits, " + psmodern::sha1(next));
        std::cout << "  " << edit_files[n + 1].filename().string() << ": " << edits[n + 1].size() << " bytes edited\n";
        if (!reference) starts[n + 1] = next;
    }
    std::cout << "The chain of " << count << " links holds.\n";
    if (assert_route) route_assertions(events, labels);
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
    const Awards awards(*game.rom);
    const auto next = apply_edits(levels, awards, start, end, read_edits(edits_path));
    write_new_image(out, next);
    std::cout << psmodern::sha1(next) << '\n';
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

// ---- part 3's recordings ----------------------------------------------------------------
// A part 3 recording (extra/<name>.psrec), its start file and the chain link it
// starts from, read and checked for form: the extra folder holds only
// <name>.psrec and <name>.start pairs, and the recording and the link were made
// on the cartridge whose SHA-1 is `rom_sha1`.
struct Extra {
    std::filesystem::path path, folder, link_path;
    replay::Recording recording, link;
    ExtraStart start;
};
Extra read_extra(const std::string& rom_sha1, const std::filesystem::path& recording_path) {
    Extra x;
    x.path = recording_path;
    x.folder = recording_path.parent_path();
    check(x.folder.filename() == "extra", "A part 3 recording lives in the playthrough folder's extra/: " + recording_path.string());
    check(std::filesystem::is_directory(x.folder), "The extra folder is missing: " + x.folder.string());
    static const std::regex extra_name("([a-z0-9]+(-[a-z0-9]+)*)[.](psrec|start)");
    std::set<std::string> recordings, starts;
    for (const auto& file : std::filesystem::directory_iterator(x.folder)) {
        const auto name = file.path().filename().string();
        std::smatch m;
        check(file.is_regular_file() && std::regex_match(name, m, extra_name), "A stray file in the extra folder: " + name);
        (m[3] == "psrec" ? recordings : starts).insert(m[1]);
    }
    check(recordings == starts, "Every recording in the extra folder needs its .start file and every .start file its recording");
    check(recordings.contains(recording_path.stem().string()) && recording_path.extension() == ".psrec",
          "The recording is missing or misnamed: " + recording_path.string());
    x.recording = replay::read(recording_path);
    check(x.recording.rom_sha1 == rom_sha1, "The recording was made on another cartridge");
    auto start_path = recording_path;
    start_path.replace_extension(".start");
    x.start = read_extra_start(start_path);
    x.link_path = x.folder.parent_path() / (x.start.from + ".psrec");
    check(std::filesystem::is_regular_file(x.link_path), "The link the start file names is missing: " + x.link_path.string());
    x.link = replay::read(x.link_path);
    check(x.link.rom_sha1 == rom_sha1, x.start.from + " was recorded on another cartridge");
    return x;
}

// The SHA-1 of the cartridge RAM at link NN's last frame end, read from the
// link's reference manifest (<reference>/link-NN/manifest.txt) after checking
// that it is a reference of the pinned build made from exactly this link file
// (its recording-sha256 line), with one well-formed row per frame. The link's
// own strict test matches this hash at its last frame. A manifest of another
// link, or one left from an older version of the link, is refused here.
std::string reference_end_sha1(const std::filesystem::path& manifest_path, const std::filesystem::path& link_path,
                               const replay::Recording& link) {
    std::istringstream lines(read_text(manifest_path, "The reference manifest"));
    const auto name = manifest_path.string();
    std::string line;
    auto next = [&](const std::string& what) -> const std::string& {
        check(bool(std::getline(lines, line)), name + ": " + what + " is missing");
        return line;
    };
    check(next("the header") == "frame picture ram vram cram cart-ram cpu psg", name + ": invalid header");
    check(next("line 2") == "reference mesence 1", name + ": not a reference manifest (line 2)");
    check(next("the build line") == std::string("build-sha256 ") + PSMODERN_REFERENCE_SHA256,
          name + ": not made by the pinned reference build");
    check(next("the build label").starts_with("build-label \"") && line.back() == '"', name + ": invalid build label line");
    const auto bytes = read_text(link_path, "The link");
    const auto digest = psmodern::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    check(next("the recording line") == "recording-sha256 " + digest,
          name + ": made from another recording than " + link_path.filename().string() +
              " (the reference of another link, or of an older version of this one)");
    auto lowercase_hash = [](const std::string& text) {
        return text.size() == 40 && text.find_first_not_of("0123456789abcdef") == std::string::npos;
    };
    std::string cart_ram;
    std::uint64_t rows = 0;
    while (std::getline(lines, line)) {
        std::istringstream row(line);
        std::vector<std::string> fields;
        for (std::string field; row >> field;) fields.push_back(field);
        check(fields.size() == 8 && fields[0] == std::to_string(rows + 1) &&
                  std::all_of(fields.begin() + 1, fields.end(), lowercase_hash),
              name + ": invalid row for frame " + std::to_string(rows + 1));
        cart_ram = fields[5];   // frame picture ram vram cram cart-ram cpu psg
        ++rows;
    }
    check(rows == link.frames, name + ": " + std::to_string(rows) + " frames, the link " + std::to_string(link.frames));
    return cart_ram;
}

// Every link end used below is checked against that hash first, whatever made
// it (a replay or the rebuild), and so it is never trusted as a file.
void authenticate_end(const Image& end, const std::string& reference_sha1, const std::string& from, const std::string& how) {
    check(psmodern::sha1(end) == reference_sha1, "The end of " + from + " " + how +
                                                     " is not the cartridge RAM at the last frame of its reference manifest: " +
                                                     psmodern::sha1(end) + ", the reference " + reference_sha1);
}

// A part 3 recording's start image: link NN's authenticated end with exactly the
// start file's bytes set. Any byte may change (these recordings exist to reach
// what the route cannot), but each edit must change its byte, and the result's
// SHA-1 must be the recording's cart-ram header.
Image extra_image(const Extra& x, const Image& end) {
    Image image = end;
    for (const auto& [offset, value] : x.start.edits) {
        check(end[offset] != value, "Edit at " + hex(offset, 4) + " repeats " + x.start.from + "'s own value " + hex(value, 2));
        image[offset] = value;
    }
    check(psmodern::sha1(image) == x.recording.cart_ram_sha1,
          x.path.filename().string() + ": its cart-ram header " + x.recording.cart_ram_sha1 + " is not " + x.start.from +
              "'s end with its start file's " + std::to_string(x.start.edits.size()) + " edits, " + psmodern::sha1(image));
    return image;
}

// Link NN's end replayed from its reference start image (the empty image for
// link 01): the by-hand mode, and the start-image test when no rebuild is possible.
Image replayed_end(const Game& game, const Extra& x, const std::filesystem::path& reference) {
    std::optional<Image> given;
    if (x.start.from != "link-01") given = read_image(reference / x.start.from / "cart-ram.bin");
    return replay_end(game, x.link, start_image_of(x.link, given));
}

// Link NN's end rebuilt without a replay, for the start-image test. Off the
// start file's offsets it is the extra's own reference start image
// (<reference>/extra/<name>/cart-ram.bin); at those offsets it is the next
// link's reference start image, which is link NN's end with the next link's
// edits; where those edits changed the byte too (at most one such byte), every
// value is tried against the reference SHA-1. These files only supply the
// candidate bytes: what the caller uses is the rebuilt end once its SHA-1 is the
// reference's (authenticate_end). Empty when no rebuild is possible (no next
// link, or more than one byte unknown): then the link is replayed.
std::optional<Image> rebuilt_end(const Extra& x, const std::filesystem::path& reference, const std::string& reference_sha1) {
    Image end = read_image(reference / "extra" / x.path.stem() / "cart-ram.bin");
    char next[16];
    std::snprintf(next, sizeof next, "link-%02lu", std::stoul(x.start.from.substr(5)) + 1);
    if (!std::filesystem::exists(x.folder.parent_path() / (std::string(next) + ".psrec"))) return std::nullopt;
    const auto next_start = read_image(reference / next / "cart-ram.bin");
    std::set<std::uint16_t> next_edited;
    for (const auto& edit : read_edits(x.folder.parent_path() / (std::string(next) + ".edits"))) next_edited.insert(edit.first);
    std::vector<std::uint16_t> unknown;
    for (const auto& edit : x.start.edits) {
        if (next_edited.contains(edit.first)) unknown.push_back(edit.first);
        else end[edit.first] = next_start[edit.first];
    }
    if (unknown.size() > 1) return std::nullopt;
    for (unsigned value = 0; value < 256 && !unknown.empty(); ++value) {
        end[unknown[0]] = static_cast<std::uint8_t>(value);
        if (psmodern::sha1(end) == reference_sha1) break;
    }
    return end;
}

// The start-image test (`start`, the _start tests): the same image as the
// by-hand mode establishes, with link NN's end rebuilt instead of replayed when
// possible. `replay` stands in for replayed_end (the ROM-free rules test passes
// its own). Returns the image.
using Replay = std::function<Image(const Extra&)>;
Image start_check(const std::string& rom_sha1, const std::filesystem::path& recording_path, const std::filesystem::path& reference,
                  const Replay& replay) {
    const auto x = read_extra(rom_sha1, recording_path);
    const auto want = reference_end_sha1(reference / x.start.from / "manifest.txt", x.link_path, x.link);
    auto end = rebuilt_end(x, reference, want);
    std::string how = "rebuilt from the reference start images";
    if (!end) {
        how = "replayed";
        end = replay(x);
    }
    authenticate_end(*end, want, x.start.from, how);
    const auto image = extra_image(x, *end);
    std::cout << x.path.filename().string() << ": class " << x.start.start_class << "; " << x.start.from << "'s end " << how
              << " and authenticated against its reference manifest, with " << x.start.edits.size() << " bytes edited: "
              << psmodern::sha1(image) << '\n';
    return image;
}

// The by-hand mode (`extra`), the documented way to make a new recording's
// start image: link NN replayed, its end authenticated, the bytes set.
int extra(const Game& game, const std::filesystem::path& recording_path, const std::filesystem::path& reference,
          const std::optional<std::filesystem::path>& out) {
    const auto x = read_extra(game.sha1, recording_path);
    const auto want = reference_end_sha1(reference / x.start.from / "manifest.txt", x.link_path, x.link);
    const auto end = replayed_end(game, x, reference);
    authenticate_end(end, want, x.start.from, "replayed");
    const auto image = extra_image(x, end);
    if (out) write_new_image(*out, image);
    std::cout << x.path.filename().string() << ": class " << x.start.start_class << "; starts from " << x.start.from
              << "'s end with " << x.start.edits.size() << " bytes edited, " << psmodern::sha1(image) << '\n';
    return 0;
}

// ---- the plain run's work folder ----------------------------------------------------
// The plain run (and the rules test) never removes a folder it did not make
// (review 2026-11-25: `plain` removed whatever path it was given). A new path
// is created with the marker file below, written before anything else. An
// existing path is reused only when it is a folder this tool made, holding
// the marker: then everything in it but the marker is removed (a link is
// removed, never followed). Anything else (a file, a folder without the
// marker) is refused with nothing removed. CTest reruns use the same path,
// which the first run made.
constexpr const char* work_marker = "psmodern-playthrough-work";
constexpr const char* work_marker_text = "psmodern-playthrough-work 1\n";
bool marked(const std::filesystem::path& folder) {
    const auto marker = folder / work_marker;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(marker))) return false;
    std::ifstream in(marker, std::ios::binary);
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return text == work_marker_text;
}
void own_work_folder(const std::filesystem::path& work) {
    const auto status = std::filesystem::symlink_status(work);
    if (status.type() == std::filesystem::file_type::not_found) {
        std::filesystem::create_directories(work);
    } else {
        check(std::filesystem::is_directory(status), "The work folder " + work.string() + " exists and is not a folder");
        check(marked(work), "The work folder " + work.string() + " exists and was not made by this tool (no " + work_marker +
                                " marker): it is never removed or reused");
        for (const auto& item : std::filesystem::directory_iterator(work))
            if (item.path().filename() != work_marker) std::filesystem::remove_all(item.path());
    }
    std::ofstream out(work / work_marker, std::ios::binary);
    out << work_marker_text;
    out.close();
    check(bool(out), "Cannot write the marker of the work folder " + work.string());
}

// Test seams of the plain run, for the rules test only: the save writer and a
// step just before the final flush. The program's own writer by default.
struct PlainSeams {
    app::SaveStore::Writer writer = app::atomic_save;
    std::function<void(app::FrameDriver&)> before_final_flush;
};
// The link played as psmodern.exe plays it, in an owned work folder; the
// seven components of the last frame. A stop, a failed save of any frame or
// a failed final flush fails.
replay::Manifest play_plain(const Game& game, const replay::Recording& recording, const std::optional<Image>& given,
                            const std::filesystem::path& work, const PlainSeams& seams = {}) {
    const auto start = start_image_of(recording, given);
    own_work_folder(work);
    if (given) write_new_image(work / app::save_name(game.version), start);
    // As psmodern.exe plays: the frame driver with its save folder and the
    // original settings (no options file), routines and the host's hooks on.
    app::FrameDriver driver(game.rom, app::SaveStore(work, game.version, seams.writer));
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
    if (seams.before_final_flush) seams.before_final_flush(driver);
    // The flush the program makes at exit: whatever the last frame end left
    // unwritten (review 2026-11-25: its error was ignored).
    if (const auto error = driver.flush()) fail("The final save flush failed: " + *error);
    return *at;
}

int plain(const Game& game, const std::filesystem::path& link, const std::filesystem::path& reference,
          const std::filesystem::path& work) {
    const auto recording = replay::read(link);
    check(recording.rom_sha1 == game.sha1, "The link was recorded on another cartridge");
    const auto expected = last_reference_frame(reference / "manifest.txt", recording.frames);
    std::optional<Image> given;
    if (recording.cart_ram_sha1 != psmodern::sha1(Image{}) || std::filesystem::exists(reference / "cart-ram.bin"))
        given = read_image(reference / "cart-ram.bin");
    const auto at = std::optional<replay::Manifest>(play_plain(game, recording, given, work));
    constexpr const char* names[]{"picture", "ram", "vram", "cram", "cart-ram", "cpu", "psg"};
    std::string differing;
    for (std::size_t i = 0; i < expected.size(); ++i)
        if ((*at)[i] != expected[i]) differing += std::string(" ") + names[i];
    check(differing.empty(), "The last frame differs from the reference in:" + differing);
    std::cout << link.filename().string() << ": " << recording.frames << " frames played as the program plays them, no stop; "
              << "the last frame matches the reference (cart-ram " << (*at)[4] << ").\n";
    return 0;
}

// ---- the rules' own test --------------------------------------------------------------
// Each rule refuses its negative case for its own reason and admits its
// positive case, on synthetic save images built at run time from the
// player's ROM (nothing of the ROM is stored) and on one-frame links.
void expect_refused(const std::string& what, const std::function<void()>& run, const std::string& reason) {
    try { run(); }
    catch (const std::exception& e) {
        const std::string message = e.what();
        check(message.find(reason) != std::string::npos,
              "rules: " + what + " was refused for another reason than '" + reason + "': " + message);
        std::cout << "rules: " << what << ": refused (" << message.substr(0, message.find('\n')) << ")\n";
        return;
    }
    fail("rules: " + what + " was admitted; it must be refused (" + reason + ")");
}
void expect_admitted(const std::string& what, const std::function<void()>& run) {
    try { run(); }
    catch (const std::exception& e) { fail("rules: " + what + " was refused; it must be admitted: " + e.what()); }
    std::cout << "rules: " << what << ": admitted\n";
}
void set_field(Image& image, unsigned slot, unsigned k, unsigned f, unsigned value) {
    image[slot_offset(slot, static_cast<std::uint16_t>(records + record_bytes * k + f))] = static_cast<std::uint8_t>(value);
}
// A record of character k at `lv` with the level's experience threshold plus
// `surplus`, no equipment, full HP and MP: what the game's refresh leaves.
void set_record(const Levels& levels, Image& image, unsigned slot, unsigned k, unsigned lv, unsigned surplus) {
    const unsigned exp = levels.reaches(k, lv) + surplus;
    set_field(image, slot, k, flags, 1);
    set_field(image, slot, k, level, lv);
    set_field(image, slot, k, exp_low, exp & 255);
    set_field(image, slot, k, exp_high, exp >> 8);
    set_field(image, slot, k, max_hp, levels.row(k, lv, 0));
    set_field(image, slot, k, hp, levels.row(k, lv, 0));
    set_field(image, slot, k, max_mp, levels.row(k, lv, 3));
    set_field(image, slot, k, mp, levels.row(k, lv, 3));
    set_field(image, slot, k, attack, (levels.row(k, lv, 1) + levels.boost(0)) & 255);
    set_field(image, slot, k, defense, (levels.row(k, lv, 2) + levels.boost(0) + levels.boost(0)) & 255);
}
// The edits that make record k of slot 1 of `from` equal that of `to`.
Edits record_edits(const Image& from, const Image& to, unsigned k) {
    Edits edits;
    for (unsigned f = hp; f <= defense; ++f) {
        const auto offset = static_cast<std::uint16_t>(slot_offset(1, static_cast<std::uint16_t>(records + record_bytes * k + f)));
        if (from[offset] != to[offset]) edits.emplace_back(offset, to[offset]);
    }
    return edits;
}
void write_one_frame_link(const Game& game, const std::filesystem::path& path) {
    replay::Recording recording;
    recording.rom_sha1 = game.sha1;
    recording.cart_ram_sha1 = psmodern::sha1(Image{});
    recording.frames = 1;
    std::ofstream out(path, std::ios::binary);
    out << replay::write(recording);
    out.close();
    check(bool(out), "Cannot write " + path.string());
}
void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
    out.close();
    check(bool(out), "Cannot write " + path.string());
}

int rules(const std::filesystem::path& rom, const std::filesystem::path& work) {
    if (rom.empty() || !std::filesystem::exists(rom)) {
        std::cout << "Skipping: ROM missing.\n";
        return 77;
    }
    const auto game = load_game(rom);
    const Levels levels(*game.rom);
    const Awards awards(*game.rom);
    std::cout << "rules: " << awards.count() << " distinct experience awards in the enemy records, the smallest "
              << awards.smallest() << '\n';
    // Link N's start (the empty image) and end: slot 1 saved and used, the
    // first character active at level 1.
    const Image start{};
    Image end{};
    end[d::slot_used_flag(1)] = 1;
    set_record(levels, end, 1, 0, 1, 0);
    check(unreachable(levels, end, 1, 0).empty(), "rules: the synthetic record does not satisfy the rule");
    expect_admitted("a link that writes one used slot, no edits", [&] { (void)apply_edits(levels, awards, start, end, {}); });
    // (1a) The one-slot rule holds with an empty edits file too.
    expect_refused("a link that writes no slot, empty edits", [&] { (void)apply_edits(levels, awards, start, start, {}); },
                   "must write exactly one save slot; 0 were written");
    Image two = end;
    two[d::slot_offset(2)] = 1;
    expect_refused("a link that writes two slots, empty edits", [&] { (void)apply_edits(levels, awards, start, two, {}); },
                   "must write exactly one save slot; 2 were written");
    Image unused = end;
    unused[d::slot_used_flag(1)] = 0;
    expect_refused("a link whose written slot is not marked used", [&] { (void)apply_edits(levels, awards, start, unused, {}); },
                   "is not marked used");
    // (1b) Experience: the smallest gain no sum of awards makes (1 on both
    // cartridges: the smallest award is 2) is refused, the smallest award admitted.
    unsigned gap = 1;
    while (awards.most(gap) >= 0) ++gap;
    check(levels.reaches(0, 1) + gap < levels.reaches(0, 2) && levels.reaches(0, 1) + awards.smallest() < levels.reaches(0, 2),
          "rules: the experience cases do not fit below level 2");
    Image after = end;
    set_record(levels, after, 1, 0, 1, gap);
    expect_refused("experience gained " + std::to_string(gap) + " (no sum of awards)",
                   [&] { (void)apply_edits(levels, awards, start, end, record_edits(end, after, 0)); }, "is not a sum of awards");
    set_record(levels, after, 1, 0, 1, awards.smallest());
    expect_admitted("experience gained " + std::to_string(awards.smallest()) + " (the smallest award)",
                    [&] { (void)apply_edits(levels, awards, start, end, record_edits(end, after, 0)); });
    set_record(levels, after, 1, 0, 5, 0);
    expect_admitted("level 5 at its threshold", [&] { (void)apply_edits(levels, awards, start, end, record_edits(end, after, 0)); });
    set_record(levels, after, 1, 0, 1, 0);
    set_field(after, 1, 0, max_hp, levels.row(0, 1, 0) + 1);
    expect_refused("maximum HP above the level's", [&] { (void)apply_edits(levels, awards, start, end, record_edits(end, after, 0)); },
                   "maximum HP");

    // The chain: every nonterminal link writes one slot, even with empty edits;
    // the last link may end with the cartridge RAM unchanged.
    own_work_folder(work);
    const auto two_links = work / "two-one-frame-links";   // inside the owned folder, which removes it on the next run
    std::filesystem::create_directories(two_links);
    write_one_frame_link(game, two_links / "link-01.psrec");
    write_one_frame_link(game, two_links / "link-02.psrec");
    write_file(two_links / "link-02.edits", "psmodern-link-edits 1\n");
    expect_refused("two one-frame links from the empty image, empty edits (the review's chain)",
                   [&] { (void)chain(game, two_links, std::nullopt, false); }, "must write exactly one save slot; 0 were written");
    std::filesystem::remove(two_links / "link-02.psrec");
    std::filesystem::remove(two_links / "link-02.edits");
    expect_admitted("one one-frame link (the last link may leave the cartridge RAM unchanged)",
                    [&] { (void)chain(game, two_links, std::nullopt, false); });

    // (4) The plain run's folder: a folder it did not make is refused and kept.
    const auto foreign = work / "foreign";
    std::filesystem::create_directories(foreign);
    write_file(foreign / "keep.sav", "keep\n");
    expect_refused("reusing a folder the tool did not make", [&] { own_work_folder(foreign); }, "was not made by this tool");
    check(std::filesystem::exists(foreign / "keep.sav"), "rules: the refused folder lost its file");
    std::filesystem::remove_all(foreign);   // made by this test above
    const auto owned = work / "owned";
    own_work_folder(owned);
    write_file(owned / "old.sav", "old\n");
    expect_admitted("reusing a folder the tool made", [&] { own_work_folder(owned); });
    check(!std::filesystem::exists(owned / "old.sav") && marked(owned), "rules: the reused folder was not emptied");
    write_file(work / "a-file", "x\n");
    expect_refused("a work folder path that is a file", [&] { own_work_folder(work / "a-file"); }, "is not a folder");
    std::filesystem::remove(work / "a-file");

    // (4) The final flush's error fails the plain run. A one-frame link from
    // the empty image saves nothing; the cartridge RAM is then changed after
    // the last frame, so only the final flush writes, through a failing writer.
    replay::Recording one;
    one.rom_sha1 = game.sha1;
    one.cart_ram_sha1 = psmodern::sha1(Image{});
    one.frames = 1;
    PlainSeams seams;
    seams.writer = [](const std::filesystem::path&, const Memory::CartridgeRam&) { throw std::runtime_error("injected write failure"); };
    expect_admitted("a plain run with nothing to flush, through the failing writer",
                    [&] { (void)play_plain(game, one, std::nullopt, work / "plain", seams); });
    seams.before_final_flush = [](app::FrameDriver& driver) {
        auto& memory = driver.machine().memory();
        memory.write(0xFFFC, 0x08);                                       // cartridge RAM in slot 2
        memory.write(0x8000, static_cast<std::uint8_t>(~memory.cartridge_ram()[0]));
        check(memory.cartridge_ram_dirty(), "rules: the cartridge RAM change did not mark it unwritten");
    };
    expect_refused("a plain run whose final flush fails", [&] { (void)play_plain(game, one, std::nullopt, work / "plain", seams); },
                   "The final save flush failed");
    std::cout << "The rules hold their cases.\n";
    return 0;
}

// ---- the start-image rules (normal suite, no ROM) ------------------------------------------
// The start file's form and the start-image test's checks, each shown refusing
// its negative case for its own reason, on a synthetic chain built here: no
// ROM is read and nothing is replayed (the recordings carry a stand-in
// cartridge SHA-1; where the test would replay a link, the injected replay
// returns the synthetic end). The chain folder holds link-01, link-02 (with its
// edits) and link-03, one frame each; extra/x (edited, three bytes, one of which
// link-02's edits also change) and extra/y (chain-save) start from link-01.
// The reference holds link-01's manifest, whose last frame has the synthetic
// end's SHA-1, link-02's and link-03's start images, link-03's manifest and the
// extras' start images.
void write_recording(const std::filesystem::path& path, const std::string& rom_sha1, const std::string& cart_sha1,
                     std::uint64_t frames) {
    replay::Recording recording;
    recording.rom_sha1 = rom_sha1;
    recording.cart_ram_sha1 = cart_sha1;
    recording.frames = frames;
    write_file(path, replay::write(recording));
}
void write_image(const std::filesystem::path& path, const Image& image) {
    std::filesystem::create_directories(path.parent_path());
    write_file(path, std::string(reinterpret_cast<const char*>(image.data()), image.size()));
}
// A reference manifest of the pinned build for the recording file `link`,
// `frames` rows, the cartridge RAM of the last one `end_sha1`.
void write_manifest(const std::filesystem::path& path, const std::filesystem::path& link, const std::string& end_sha1,
                    std::uint64_t frames) {
    const auto bytes = read_text(link, "The link");
    std::string text = std::string("frame picture ram vram cram cart-ram cpu psg\nreference mesence 1\nbuild-sha256 ") +
                       PSMODERN_REFERENCE_SHA256 + "\nbuild-label \"synthetic\"\nrecording-sha256 " +
                       psmodern::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size())) +
                       '\n';
    const std::string other(40, 'a');
    for (std::uint64_t n = 1; n <= frames; ++n) {
        text += std::to_string(n);
        for (unsigned i = 0; i < 7; ++i) text += " " + (i == 4 && n == frames ? end_sha1 : other);
        text += '\n';
    }
    std::filesystem::create_directories(path.parent_path());
    write_file(path, text);
}

int start_rules(const std::filesystem::path& work) {
    const std::string rom_sha1(40, '0');
    own_work_folder(work);
    // Each case builds its own copy in a new folder (case-N), so nothing is
    // removed while the run goes on; the next run's own_work_folder empties them.
    std::filesystem::path chain_folder, extras, reference;
    unsigned cases = 0;
    // link-01's end, link-02's start (that end with link-02's edits), link-03's
    // start, and the extras' start images.
    Image end{};
    end[0x100] = 0x01;
    end[0x101] = 0x02;
    end[0x200] = 0x07;
    Image second = end;
    second[0x101] = 0x22;
    second[0x300] = 0x33;
    Image third{};
    third[0x100] = 0x09;
    third[0x400] = 0x55;
    Image x_image = end;
    x_image[0x100] = 0x11;
    x_image[0x101] = 0x12;
    x_image[0x400] = 0x44;
    const std::string x_lines = "byte 0100 11\nbyte 0101 12\nbyte 0400 44\n";
    const std::string x_start = std::string(extra_start_header) + "\nfrom link-01\nclass edited\nbasis Link 01's save with three bytes set.\n" + x_lines;
    auto build = [&] {
        const auto root = work / ("case-" + std::to_string(++cases));
        chain_folder = root / "chain";
        extras = chain_folder / "extra";
        reference = root / "reference";
        std::filesystem::create_directories(extras);
        write_recording(chain_folder / "link-01.psrec", rom_sha1, psmodern::sha1(Image{}), 1);
        write_recording(chain_folder / "link-02.psrec", rom_sha1, psmodern::sha1(second), 1);
        write_file(chain_folder / "link-02.edits", "psmodern-link-edits 1\nbyte 0101 22\nbyte 0300 33\n");
        write_recording(chain_folder / "link-03.psrec", rom_sha1, psmodern::sha1(third), 1);
        write_file(chain_folder / "link-03.edits", "psmodern-link-edits 1\n");
        write_recording(extras / "x.psrec", rom_sha1, psmodern::sha1(x_image), 1);
        write_file(extras / "x.start", x_start);
        write_recording(extras / "y.psrec", rom_sha1, psmodern::sha1(end), 1);
        write_file(extras / "y.start", std::string(extra_start_header) + "\nfrom link-01\nclass chain-save\nbasis Link 01's save.\n");
        write_manifest(reference / "link-01" / "manifest.txt", chain_folder / "link-01.psrec", psmodern::sha1(end), 1);
        write_image(reference / "link-02" / "cart-ram.bin", second);
        write_image(reference / "link-03" / "cart-ram.bin", third);
        write_manifest(reference / "link-03" / "manifest.txt", chain_folder / "link-03.psrec", psmodern::sha1(third), 1);
        write_image(reference / "extra" / "x" / "cart-ram.bin", x_image);
        write_image(reference / "extra" / "y" / "cart-ram.bin", end);
    };
    unsigned replays = 0;
    const Replay no_replay = [&](const Extra&) -> Image { ++replays; fail("start-rules: the test replayed a link it can rebuild"); };
    const Replay synthetic_replay = [&](const Extra&) { ++replays; return end; };
    auto check_x = [&](const Replay& replay) { (void)start_check(rom_sha1, extras / "x.psrec", reference, replay); };
    auto refused = [&](const std::string& what, const std::string& reason, const std::function<void()>& change,
                       const Replay& replay = Replay{}) {
        build();
        change();
        expect_refused(what, [&] { check_x(replay ? replay : no_replay); }, reason);
    };
    auto start_file = [&](const std::string& text) { return [&, text] { write_file(extras / "x.start", text); }; };
    auto flip = [&](const std::filesystem::path& relative, std::uint16_t offset) {   // a path inside the reference
        return [&, relative, offset] {
            const auto path = reference / relative;
            auto image = read_image(path);
            image[offset] ^= 1;
            write_image(path, image);
        };
    };

    // The positive cases: the rebuild (one byte also edited by link-02, found
    // by trying its values), a chain save, and the replay where no rebuild is
    // possible (no next link; more than one unknown byte).
    build();
    expect_admitted("an edited start image, link-01's end rebuilt (one byte tried)", [&] { check_x(no_replay); });
    expect_admitted("a chain-save start image", [&] { (void)start_check(rom_sha1, extras / "y.psrec", reference, no_replay); });
    build();
    std::filesystem::remove(chain_folder / "link-02.psrec");
    std::filesystem::remove(chain_folder / "link-02.edits");
    expect_admitted("no next link: link-01 replayed, its end authenticated", [&] { check_x(synthetic_replay); });
    build();
    write_file(chain_folder / "link-02.edits", "psmodern-link-edits 1\nbyte 0100 21\nbyte 0101 22\n");
    expect_admitted("two bytes unknown: link-01 replayed, its end authenticated", [&] { check_x(synthetic_replay); });
    check(replays == 2, "start-rules: the replay ran " + std::to_string(replays) + " times, not 2");

    // The start file's form.
    const std::string header = std::string(extra_start_header) + "\nfrom link-01\n";
    refused("a start file of version 1", "version 1 (no class and basis lines) is no longer read",
            start_file("psmodern-extra-start 1\nfrom link-01\n" + x_lines));
    refused("a start file of an unknown version", "line 1 must be 'psmodern-extra-start 2'",
            start_file("psmodern-extra-start 3\nfrom link-01\nclass edited\nbasis Some basis.\n" + x_lines));
    refused("a start file without its class line", "the class is required",
            start_file(header + "basis Link 01's save with three bytes set.\n" + x_lines));
    refused("an unknown class", "unknown class 'likely'", start_file(header + "class likely\nbasis Some basis.\n" + x_lines));
    refused("chain-save with a byte line", "class chain-save has byte lines",
            start_file(header + "class chain-save\nbasis Some basis.\n" + x_lines));
    refused("edited with no byte line", "class edited needs at least one byte line",
            start_file(header + "class edited\nbasis Some basis.\n"));
    refused("a start file without its basis line", "the basis is required", start_file(header + "class edited\n" + x_lines));
    refused("an empty basis", "the basis is empty", start_file(header + "class edited\nbasis \n" + x_lines));
    refused("a basis with location data", "extracted location data",
            start_file(header + "class edited\nbasis Moved to row 27 col 21.\n" + x_lines));
    refused("a basis that is not printable ASCII", "printable ASCII and line feeds only",
            start_file(header + "class edited\nbasis Moved\tto a town.\n" + x_lines));
    refused("a start file naming a link the chain does not have", "The link the start file names is missing",
            start_file(std::string(extra_start_header) + "\nfrom link-09\nclass edited\nbasis Some basis.\n" + x_lines));
    refused("the link recorded on another cartridge", "link-01 was recorded on another cartridge",
            [&] { write_recording(chain_folder / "link-01.psrec", std::string(40, '1'), psmodern::sha1(Image{}), 1); });
    refused("the extra recorded on another cartridge", "The recording was made on another cartridge",
            [&] { write_recording(extras / "x.psrec", std::string(40, '1'), psmodern::sha1(x_image), 1); });

    // The reference manifest that authenticates the link end.
    auto manifest_line = [&](const std::string& from, const std::string& to) {
        return [&, from, to] {
            const auto path = reference / "link-01" / "manifest.txt";
            auto text = read_text(path, "The manifest");
            const auto at = text.find(from);
            check(at != std::string::npos, "start-rules: the synthetic manifest has no '" + from + "'");
            write_file(path, text.replace(at, from.size(), to));
        };
    };
    refused("a manifest with another header", "invalid header", manifest_line("cpu psg\n", "cpu\n"));
    refused("a manifest that is not a reference (a runner's)", "not a reference manifest",
            manifest_line("reference mesence 1", "runner psmodern 1"));
    refused("a manifest of another reference build", "not made by the pinned reference build",
            manifest_line(PSMODERN_REFERENCE_SHA256, std::string(64, 'f')));
    refused("a manifest without its build label", "invalid build label line", manifest_line("build-label \"synthetic\"", "build-label"));
    refused("a manifest row with a hash missing", "invalid row for frame 1", manifest_line(" " + std::string(40, 'a') + "\n", "\n"));
    refused("a manifest with one frame more than the link", "2 frames, the link 1", [&] {
        write_manifest(reference / "link-01" / "manifest.txt", chain_folder / "link-01.psrec", psmodern::sha1(end), 2);
    });

    // The link end and the image.
    refused("one byte of the extra's reference start image changed, off the listed offsets (a tampered link end)",
            "is not the cartridge RAM at the last frame of its reference manifest",
            flip(std::filesystem::path("extra") / "x" / "cart-ram.bin", 0x200));
    refused("one byte of the next link's start image changed at a listed offset (a tampered link end)",
            "is not the cartridge RAM at the last frame of its reference manifest", flip(std::filesystem::path("link-02") / "cart-ram.bin", 0x100));
    refused("the next link's start image taken from another link (link-03's)",
            "is not the cartridge RAM at the last frame of its reference manifest", [&] {
                std::filesystem::copy_file(reference / "link-03" / "cart-ram.bin", reference / "link-02" / "cart-ram.bin",
                                           std::filesystem::copy_options::overwrite_existing);
            });
    refused("link-01's reference manifest taken from another link (link-03's)", "made from another recording than link-01.psrec", [&] {
        std::filesystem::copy_file(reference / "link-03" / "manifest.txt", reference / "link-01" / "manifest.txt",
                                   std::filesystem::copy_options::overwrite_existing);
    });
    refused("a stale reference manifest, left from an older version of link-01 (two frames)",
            "made from another recording than link-01.psrec", [&] {
                const auto older = work / "older-link-01.psrec";
                write_recording(older, rom_sha1, psmodern::sha1(Image{}), 2);
                write_manifest(reference / "link-01" / "manifest.txt", older, psmodern::sha1(end), 2);
            });
    refused("the extra's reference start image missing", "The cartridge RAM image is missing",
            [&] { std::filesystem::remove(reference / "extra" / "x" / "cart-ram.bin"); });
    refused("link-01's reference manifest missing", "The reference manifest is missing",
            [&] { std::filesystem::remove(reference / "link-01" / "manifest.txt"); });
    refused("a replayed end that differs from the reference", "replayed is not the cartridge RAM at the last frame", [&] {
        std::filesystem::remove(chain_folder / "link-02.psrec");
        std::filesystem::remove(chain_folder / "link-02.edits");
    }, [&](const Extra&) { Image wrong = end; wrong[0x200] ^= 1; return wrong; });
    refused("a byte line left out of the start file", "is not the cartridge RAM at the last frame of its reference manifest",
            start_file(header + "class edited\nbasis Some basis.\nbyte 0100 11\nbyte 0101 12\n"));
    refused("a byte line whose value makes another image", "is not link-01's end with its start file's 3 edits",
            start_file(header + "class edited\nbasis Some basis.\nbyte 0100 11\nbyte 0101 12\nbyte 0400 45\n"));
    refused("a byte line that repeats the link end's own byte", "repeats link-01's own value 07",
            start_file(header + "class edited\nbasis Some basis.\nbyte 0100 11\nbyte 0101 12\nbyte 0200 07\nbyte 0400 44\n"));
    std::cout << "The start-image rules hold their cases.\n";
    return 0;
}

// ---- the committed inventory --------------------------------------------------------------
// tests/playthrough_inventory.txt: ASCII, LF, lines starting with # and blank
// lines ignored; every other line `<us|br> link <NN>`, `<us|br> extra <name>`
// or `<us|br> route <name>`, each once. The links (link-NN.psrec), part 3's
// recordings (extra/<name>.psrec) and the routes (tests/playthrough_routes.txt)
// found must be exactly those listed: a missing or an unlisted item fails.
int inventory(const std::filesystem::path& list, const std::filesystem::path& recordings, const std::filesystem::path& routes) {
    const auto text = read_text(list, "The inventory");
    check(text.find('\r') == std::string::npos && (text.empty() || text.back() == '\n'),
          list.string() + ": ASCII with LF line endings and a final LF");
    static const std::regex item("(us|br) (link [0-9]{2}|(extra|route) [a-z0-9]+(-[a-z0-9]+)*)");
    std::set<std::string> listed, found;
    std::istringstream lines(text);
    std::string line;
    for (unsigned number = 1; std::getline(lines, line); ++number) {
        if (line.empty() || line.front() == '#') continue;
        check(std::regex_match(line, item), list.string() + " line " + std::to_string(number) + ": expected '<us|br> link <NN>', "
                                                "'<us|br> extra <name>' or '<us|br> route <name>'");
        check(listed.insert(line).second, list.string() + " line " + std::to_string(number) + ": listed twice: " + line);
    }
    static const std::regex link_name("link-([0-9]{2})[.]psrec"), extra_name("([a-z0-9]+(-[a-z0-9]+)*)[.]psrec");
    for (const std::string region : {"us", "br"}) {
        const auto folder = recordings / region / "playthrough";
        if (std::filesystem::is_directory(folder))
            for (const auto& file : std::filesystem::directory_iterator(folder)) {
                const auto name = file.path().filename().string();
                std::smatch m;
                if (std::regex_match(name, m, link_name)) found.insert(region + " link " + m[1].str());
            }
        if (std::filesystem::is_directory(folder / "extra"))
            for (const auto& file : std::filesystem::directory_iterator(folder / "extra")) {
                const auto name = file.path().filename().string();
                std::smatch m;
                if (std::regex_match(name, m, extra_name)) found.insert(region + " extra " + m[1].str());
            }
    }
    std::istringstream route_lines(read_text(routes, "The routes file"));
    while (std::getline(route_lines, line)) {
        if (line.empty() || line.front() == '#') continue;
        std::istringstream words(line);
        std::string region, name;
        words >> region >> name;
        found.insert(region + " route " + name);
    }
    std::string missing, unlisted;
    for (const auto& x : listed) if (!found.contains(x)) missing += "\n  " + x;
    for (const auto& x : found) if (!listed.contains(x)) unlisted += "\n  " + x;
    check(missing.empty() && unlisted.empty(), "The playthrough group differs from its inventory " + list.string() +
                                                   (missing.empty() ? "" : "\nlisted but not found:" + missing) +
                                                   (unlisted.empty() ? "" : "\nfound but not listed:" + unlisted));
    std::cout << "The playthrough inventory lists exactly the " << found.size() << " items found.\n";
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
        if ((mode == "chain" || mode == "links") && (argc == 4 || argc == 5))
            return chain(load_game(argv[2]), argv[3], argc == 5 ? std::optional<std::filesystem::path>(argv[4]) : std::nullopt,
                         mode == "chain");
        if (mode == "rules" && argc == 4) return rules(argv[2], argv[3]);
        if (mode == "inventory" && argc == 5) return inventory(argv[2], argv[3], argv[4]);
        if (mode == "apply" && argc == 7) return apply(load_game(argv[2]), argv[3], argv[4], argv[5], argv[6]);
        if (mode == "plain" && argc == 6) return plain(load_game(argv[2]), argv[3], argv[4], argv[5]);
        if (mode == "records" && argc == 4) return describe(load_game(argv[2]), argv[3]);
        if (mode == "extra" && (argc == 5 || argc == 6))
            return extra(load_game(argv[2]), argv[3], argv[4], argc == 6 ? std::optional<std::filesystem::path>(argv[5]) : std::nullopt);
        if (mode == "start" && argc == 5) {
            const auto game = load_game(argv[2]);
            const std::filesystem::path reference = argv[4];
            (void)start_check(game.sha1, argv[3], reference, [&](const Extra& x) { return replayed_end(game, x, reference); });
            return 0;
        }
        if (mode == "start-rules" && argc == 3) return start_rules(argv[2]);
        throw std::runtime_error("usage: playthrough_tests chain|links <rom> <folder> [<reference folder>] | "
                                 "apply <rom> <link> <start|empty> <edits> <new image> | "
                                 "plain <rom> <link> <reference folder> <work folder> | records <rom> <image> | "
                                 "extra <rom> <extra recording> <reference folder> [<new image>] | "
                                 "start <rom> <extra recording> <reference folder> | rules <rom> <work folder> | "
                                 "start-rules <work folder> | inventory <inventory> <recordings folder> <routes file>");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
