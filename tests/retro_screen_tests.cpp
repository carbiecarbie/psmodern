// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_cartridge.h"
#include "app/retro_font.h"
#include "app/retro_screen.h"
#include "app/retro_text.h"
#include "app/retro_windows.h"
#include "rom/rom.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace psmodern;
using namespace psmodern::app;
using namespace psmodern::app::retro;

namespace {
constexpr std::uint32_t scene = 0xff185328;
using Image = std::array<std::uint32_t, width * height>;
void check(bool condition, const char* why) { if (!condition) throw std::runtime_error(why); }

struct Frame {
    Image pixels{};
    std::array<std::uint8_t, 8192> ram{};
    Cartridge cart;
    WindowState state;
    TextPage page;
    Language language;
    explicit Frame(const std::vector<std::uint8_t>& rom, Language lang)
        : cart(rom, ram), language(lang) {}
    std::uint8_t& at(unsigned address) { return ram.at(address - 0xc000); }
    void open(Window w) { state.open.set(static_cast<unsigned>(w)); }
    void draw(const FieldMotion& field={}) {
        pixels.fill(scene);
        render(pixels, cart, state, page, language, field);
    }
    // Compare ink against the complete glyph at its design position. This
    // catches both truncation and accidentally substituting a host label.
    void ink(int x, int y, std::string_view word, std::uint32_t color, Align align = Align::left) const {
        Image mask{};
        text(mask, x, y, word, color, align);
        check(std::count(mask.begin(), mask.end(), color) > 0, "empty ink expectation");
        for (unsigned i = 0; i < mask.size(); ++i)
            if (mask[i] == color && pixels[i] != color)
                throw std::runtime_error("Missing rendered ink for " + std::string(word) +
                    " at (" + std::to_string(i % width) + "," + std::to_string(i / width) + ")");
    }
    void party() {
        at(0xc4f0) = 3;
        for (unsigned i = 0; i < 4; ++i) {
            const auto record = 0xc400 + 16 * i;
            at(record) = 1;
            at(record + 1) = 11 + i;
            at(record + 2) = 12 + i;
            at(record + 5) = 5;
            at(record + 6) = 25;
            at(record + 7) = 24;
        }
    }
};

void battle(const std::vector<std::uint8_t>& rom, Language language) {
    Frame f(rom, language);
    f.draw();
    check(std::all_of(f.pixels.begin(), f.pixels.end(), [](auto p) { return p == scene; }), "persistent overlay");
    f.party();
    f.at(0xc202) = 13;
    f.at(0xc29d) = 0xff;
    f.at(0xc2c7) = 8;
    f.at(0xc268) = 0xff;
    f.at(0xc26d) = 8;
    for (unsigned i = 0; i < 8; ++i) {
        f.at(0xc440 + i * 16) = 1;
        f.at(0xc441 + i * 16) = 21 + i;
    }
    f.open(Window::party);
    f.open(Window::enemy);
    f.open(Window::magic);
    f.state.spell_count = 5;
    f.state.spell_group = 0;
    f.draw();
    f.ink(15, 63, f.cart.spell_name(0, 4), white);
    f.ink(90, 63, std::to_string(f.cart.spell_cost(f.cart.spell_id(0, 4))), cyan, Align::right);
    f.ink(246, 73, "28", white, Align::right);
    check(f.pixels[75 * width + 50] == white && f.pixels[76 * width + 50] == scene, "five-spell box height");
    check(f.pixels[10 * width + 96] == scene && f.pixels[10 * width + 99] == scene, "gap before enemy name");
    f.page = {{"ONE", "TWO"}, true, false};
    f.draw();
    f.ink(11, 121, "ONE", white);
    f.ink(11, 133, "TWO", white);
    f.ink(240, 136, "▼", gold);
    for (int i = 0; i < 4; ++i)
        f.ink(4 + 62 * i + 54, 167, std::to_string(11 + i), white, Align::right);
    check(f.pixels[147 * width + 50] == scene && f.pixels[150 * width + 50] == gold,
          "message must leave all heroes visible");
    f.page = {};
    f.open(Window::battle_member);
    f.state.spell_row = 1;
    f.at(0xc26b) = 3;
    f.draw();
    f.ink(10, 10, f.cart.spell_name(0, 1), cyan);
    f.ink(16, 53, f.cart.member_name(3), gold);
    check(f.pixels[150 * width + 230] == gold, "ally target strip tracks vertical selection");
    f.state.open.reset();
    f.page = {{"FIRST", "SECOND", "THIRD", "FOURTH"}, true, true};
    f.draw();
    f.ink(12, 160, "FOURTH", white);
    check(f.pixels[150 * width + 2] == scene, "portrait after battle");
}

void inventory(const std::vector<std::uint8_t>& rom, Language language) {
    Frame f(rom, language);
    f.party();
    f.at(0xc4e2) = 18;
    f.at(0xc4c0) = 1;
    f.at(0xc4c8) = 0x24;
    f.at(0xc268) = 0xff;
    f.at(0xc26d) = 8;
    f.open(Window::items);
    f.draw();
    f.ink(153, 10, "18/24 · 1/3", gold, Align::right); // first cursor row turns the page
    f.at(0xc299) = 8;
    f.at(0xc26b) = 1;
    f.draw();
    f.ink(153, 10, "18/24 · 2/3", grey, Align::right);
    f.ink(17, 23, f.cart.item_name(0x24), gold);
    f.open(Window::actions);
    f.open(Window::member);
    f.state.equipping = true;
    f.state.item_row = 0;
    f.state.action_row = 1;
    f.at(0xc4c8) = 1;
    f.at(0xc26b) = 2;
    f.draw();
    f.ink(177, 105, f.cart.member_name(2), gold);
}

void shop(const std::vector<std::uint8_t>& rom, Language language) {
    Frame f(rom, language);
    f.party();
    f.at(0xc4e2) = 18;
    f.at(0xc4e0) = 100;
    f.open(Window::shop);
    f.open(Window::money);
    f.state.shop_browse = true;
    f.draw();
    f.ink(246, 10, "100", white, Align::right);
    f.ink(246, 21, "18/24", white, Align::right);
    f.state.shop_browse = false;
    f.open(Window::question);
    f.at(0xc4e0) = 75;
    f.at(0xc4e2) = 19;
    f.page = {{"ONE"}, false, false};
    f.draw();
    f.ink(246, 10, "75", cyan, Align::right);
    f.ink(246, 21, "19/24", cyan, Align::right);
}

void parolit_motion(const std::vector<std::uint8_t>& rom, Language language) {
    Frame f(rom, language);
    f.at(0xc2db)=4;
    f.state.dialogue_active=f.state.shop_dialogue=true;
    f.state.stock_rows={8};
    f.state.money_rows={3};
    f.state.question_rows={5};
    f.open(Window::shop); // the native buffer is saved before any stock is visible
    f.state.dialogue_rows.wrote(0);
    f.draw();
    check(f.pixels[136 * width + 12]==white && f.pixels[145 * width + 12]==scene,
          "first greeting row must not fill the dialogue");
    check(f.pixels[4 * width + 20]==scene, "opening dialogue must leave the stock hidden");
    f.state.dialogue_rows.wrote(5);
    f.page={{"ONE"},false,false};
    f.state.question_rows.wrote(0);
    f.draw();
    f.ink(12,146,"ONE",white);
    check(f.pixels[100 * width + 220]==white && f.pixels[108 * width + 220]==scene,
          "first question row must leave both choices hidden");
    f.state.question_rows.wrote(4);
    f.state.question_rows.restoring=true;
    f.state.question_rows.wrote(0);
    std::array<std::uint8_t,64*24> table{};
    auto base=f.cart.word(0x37b6);
    f.at(0xc269)=base&255;
    f.at(0xc26a)=base>>8;
    table[base&0x7ff]=f.cart.byte(0x2e6e);
    f.state.capture_window_arrows(f.cart,table);
    f.draw();
    f.ink(204,107,"▸",gold);
    check(f.pixels[100 * width + 220]==scene && f.pixels[131 * width + 220]==white,
          "question restoration must leave its lower rows in place");
    f.state.question_rows.wrote(4);
    f.state.money_rows.wrote(0);
    f.draw();
    check(f.pixels[4 * width + 220]==white && f.pixels[10 * width + 220]==scene,
          "money border must precede the amount and stock");
    f.state.money_rows.wrote(1);
    f.state.displayed_meseta=100;
    f.at(0xc4e2)=18;
    f.state.stock_rows.wrote(2);
    f.at(0xc4e0)=75; // the money has changed in RAM, but its printer has yet to run
    f.state.shop_browse=true;
    f.draw();
    f.ink(246,10,"100",white,Align::right);
    f.ink(246,21,"18/24",white,Align::right);
    f.ink(17,11,f.cart.item_name(f.cart.shop_item(0).id),white);
    f.ink(159,11,std::to_string(f.cart.shop_item(0).price),white,Align::right);
    check(f.pixels[23 * width + 70]==scene && f.pixels[143 * width + 28]==black,
          "unwritten stock and helper must stay hidden");
    f.state.stock_rows.wrote(7);
    f.state.stock_arrow=0;
    f.at(0xc4e2)=19;
    f.at(0xc268)=0xff;
    f.at(0xc26d)=8;
    f.draw();
    f.ink(10,11,"\u25b8",gold);
    check(f.pixels[136 * width + 100]==white, "active stock must show its helper");
    f.state.shop_browse=false;
    f.state.money_rows.restoring=true;
    f.state.money_rows.wrote(0);
    f.draw();
    f.ink(246,10,"100",white,Align::right);
    check(f.pixels[4 * width + 220]==scene && f.pixels[12 * width + 220]==black,
          "top border restoration must preserve the native money row");
    f.state.money_rows.wrote(2);
    f.state.dialogue_rows.restoring=true;
    f.state.dialogue_rows.wrote(0);
    f.draw();
    f.ink(12,146,"ONE",white);
    check(f.pixels[136 * width + 100]==scene && f.pixels[187 * width + 100]==white,
          "dialogue close must restore the scene a row at a time");
    f.state.dialogue_rows.wrote(5);
    f.draw();
    check(f.pixels[187 * width + 100]==scene, "dialogue overlay must leave after final restoration");
}

void npc_conversation(const std::vector<std::uint8_t>& rom, Language language) {
    Frame f(rom,language);
    f.state.dialogue_active=true;
    f.state.question_rows={5};
    f.state.dialogue_rows.wrote(0);
    f.draw();
    check(f.pixels[136*width+30]==white && f.pixels[154*width+30]==scene,
          "an NPC's first dialogue row must precede the body of the window");
    f.state.dialogue_rows.wrote(5);
    f.state.question_rows.wrote(0);
    f.page={{"HEL"},false,false};
    f.draw();
    f.ink(12,146,"HEL",white);
    check(f.pixels[100*width+220]==white && f.pixels[115*width+220]==scene,
          "the question should begin opening over a finished dialogue window");
    check(f.pixels[4*width+40]==scene && f.pixels[4*width+220]==scene,
          "a map conversation must leave the shop and money areas clear");
}

void field_motion(const std::vector<std::uint8_t>& rom, Language language) {
    Frame f(rom,language);
    f.party();
    f.open(Window::field);
    FieldMotion field;
    field.active=true;
    f.state.field_rows=WindowRows{11,11};
    field.cards[0]=7;
    f.draw(field);
    check(f.pixels[170*width+20]==scene && f.pixels[151*width+80]==scene,
          "unprinted stats and the second party card must stay hidden");
    field.cards[0]=15;
    field.party_hp[0]=93;
    f.draw(field);
    f.ink(58,167,"93",white,Align::right);
    check(f.pixels[178*width+20]==scene,"MP must wait for its own row");
    field.cards[0]=31;
    field.party_mp[0]=48;
    f.draw(field);
    f.ink(58,177,"48",white,Align::right);
    field.cards[0]=0b111000;
    f.draw(field);
    check(f.pixels[150*width+20]==scene && f.pixels[160*width+11]==scene,
          "a restored card head must reveal the original background");
    f.ink(58,177,"48",white,Align::right);
}

void nested_battle_picker(const std::vector<std::uint8_t>& rom,Language language) {
    Frame f(rom,language);
    f.party();
    f.at(0xc202)=13;
    f.at(0xc29d)=0xff;
    f.open(Window::magic);
    f.state.spell_count=5;
    Machine m(std::make_shared<const Memory::Rom>(rom));
    m.memory().write(0xc4f0,3);
    m.cpu().sp.uint16_value=0xc900;
    m.memory().write(0xc900,f.cart.byte(0x368e));
    m.memory().write(0xc902,0x63);
    m.memory().write(0xc903,0x3a);
    f.state.observe(m,0x3682);
    const auto row=[&](unsigned i) {
        m.cpu().de.uint16_value=static_cast<std::uint16_t>(f.cart.word(0x368b)+64*(i+1));
        f.state.observe(m,0x3a79);
        f.draw();
    };
    row(0);
    f.ink(15,63,f.cart.spell_name(0,4),white);
    for (unsigned i=1;i<9;++i) row(i);
    f.state.observe(m,0x36cc);
    row(0);
    f.ink(10,10,f.cart.member_name(0),gold);
    f.ink(16,53,f.cart.member_name(3),white);

    // Field magic keeps its original caster picker while choosing a target.
    f.at(0xc29d)=0;
    f.at(0xc202)=9;
    f.open(Window::member);
    f.state.observe(m,0x3682);
    row(0);
    check(f.pixels[20*width+251]==white,"caster picker must remain beside the magic stats");
    row(1);
    check(f.pixels[36*width+160]==white,"caster picker border disappeared under the opening target");
    f.state=WindowState{};
    f.open(Window::items);
    f.open(Window::member);
    f.open(Window::equipment);
    f.draw();
    check(f.pixels[80*width+164]==white,"background equip picker must retain its inventory position");
}

void status_spell_columns(const std::vector<std::uint8_t>& rom, Language language) {
    Frame f(rom,language);
    f.open(Window::status);
    Machine machine(std::make_shared<const Memory::Rom>(rom));
    auto& cpu=machine.cpu();
    cpu.af.uint8_values.at_1=0;
    cpu.bc.uint8_values.at_1=5;
    cpu.sp.uint16_value=0xc900;
    f.state.observe(machine,0x3478);
    machine.memory().write(0xc900,f.cart.byte(0x3481));
    machine.memory().write(0xc902,0x63);
    machine.memory().write(0xc903,0x3a);
    const auto write_row=[&](unsigned row) {
        cpu.de.uint16_value=static_cast<std::uint16_t>(f.cart.word(0x347e)+64*(row+1));
        f.state.observe(machine,0x3a79);
        f.draw();
    };
    const auto fourth=f.cart.spell_name(0,3);
    const auto fifth=f.cart.spell_name(0,4);
    for (unsigned row=0;row<12;++row) {
        write_row(row);
        if (row<8) check(f.pixels[146*width+139]==scene,"right column arrived before its native row");
        if (row>=8) f.ink(134,145,fourth,white);
        if (row>=10) f.ink(134,157,fifth,white);
    }
    f.state.observe(machine,0x34c9);
    for (unsigned row=0;row<12;++row) {
        write_row(row);
        if (row<8) f.ink(134,145,fourth,white);
        else check(f.pixels[146*width+139]==scene,"fourth spell remained after its row was restored");
        if (row<10) f.ink(134,157,fifth,white);
    }
}

void status_and_menus(const std::vector<std::uint8_t>& rom, RomVersion source, Language language) {
    const bool br = source == RomVersion::brazilian;
    Frame f(rom, language);
    f.party();
    f.open(Window::commands);
    f.at(0xc26b) = 0;
    f.draw();
    f.ink(17, 23, f.cart.tile_row(f.cart.word(0x30d6) + 14, 4), gold);
    f.state.open.reset();
    f.open(Window::member); // left underneath both status and field magic by the native caller
    f.open(Window::equipment);
    f.open(Window::status);
    f.at(0xc405) = 30;
    f.draw();
    f.ink(245, 21, br ? "NV 30" : "LV 30", white, Align::right);
    f.ink(139, 104, br ? "PRÓX." : "NEXT", cyan);
    f.ink(245, 104, "—", cyan, Align::right);
    f.state.open.reset();
    f.open(Window::member);
    f.open(Window::magic);
    f.state.spell_count = 2;
    f.state.spell_group = 3;
    f.draw();
    f.ink(17, 23, f.cart.spell_name(3, 0), gold);
    f.ink(218, 21, "12/24", cyan, Align::right);
    f.state.open.reset();
    f.open(Window::choice);
    f.page = {{"A","B","C","D"}, true, false};
    f.draw();
    f.ink(199, 85, f.cart.tile_row(f.cart.word(0x3a33) + 50, 6, 18), white);
    f.ink(12, 124, "A", white);
}
}

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    if (!std::filesystem::is_regular_file(argv[1])) return 77;
    try {
        const auto source = identify_rom(argv[1]);
        check(source == (std::string_view(argv[2]) == "br" ? RomVersion::brazilian : RomVersion::us_rev_a), "wrong source cartridge");
        const auto rom = read_rom_image(argv[1]);
        // Deliberately render each source cartridge in the other host locale.
        const auto language = source == RomVersion::brazilian ? Language::english : Language::portuguese;
        battle(rom, language);
        inventory(rom, language);
        shop(rom, language);
        parolit_motion(rom, language);
        npc_conversation(rom,language);
        field_motion(rom,language);
        nested_battle_picker(rom,language);
        status_spell_columns(rom,language);
        status_and_menus(rom, source, language);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
