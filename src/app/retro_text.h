// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/retro_cartridge.h"
#include "app/retro_windows.h"
#include "machine/machine.h"
#include <optional>
#include <string>
#include <vector>

namespace psmodern::app::retro {
enum class TextEvent { fresh, append, alternate_layout, close };
struct TextKey {
    TextEvent event{};
    unsigned bank{}, address{};
    bool operator==(const TextKey&) const = default;
};
struct TextArguments {
    std::string member, enemy, item;
    unsigned number{};
};
enum class TextEnd { immediate, button, interruptible, delay };
struct Script {
    // Each break between paragraphs asks for one advance. The final pause
    // belongs to the ending; timed endings run the native countdown.
    std::vector<std::string> pages;
    TextEnd ending=TextEnd::immediate;
    struct Break { unsigned page, address; bool button; };
    // Native break commands between paragraphs, excluding a leading clear.
    std::vector<Break> breaks;
    // Stream address reached after the load/delete menu scrolls to its second row.
    unsigned second_title_row=0;
};
class TextSource {
public:
    virtual ~TextSource()=default;
    virtual Script resolve(TextKey key,const TextArguments& args) const=0;
};
class CartridgeText final : public TextSource {
public:
    explicit CartridgeText(const Cartridge& data):data_(data) {}
    Script resolve(TextKey key,const TextArguments& args) const override;
private:
    const Cartridge& data_;
};

struct TextPage {
    std::vector<std::string> lines;
    bool more=false, alternate=false;
    std::uint64_t revision=0;
};
class RetroTextSession {
public:
    // A custom source receives values, never a Machine. The native source
    // reads the current cartridge for each event, including after a swap.
    explicit RetroTextSession(const TextSource* source=nullptr):source_(source) {}
    std::optional<std::uint16_t> observe(Machine& machine,unsigned pc,const WindowState& windows={});
    FrameInput input(FrameInput input,bool paused=false);
    void presented(const TextPage& page);
    const TextPage& view() const {return view_;}
    void reset();
    unsigned events(TextEvent event) const {return events_[unsigned(event)];}
private:
    void begin(Machine& machine,TextKey key,const WindowState& windows);
    void update_page();
    void native_clear(unsigned address);
    void show_native_page();
    void reveal_native_glyph();
    std::optional<std::uint16_t> observe_native(Machine& machine,unsigned pc);
    std::uint16_t finish_terminal(Machine& machine);
    void button_result(Machine& machine) const;
    struct Wait {
        std::uint16_t sp,af,wz;
        std::uint8_t q;
        std::uint16_t resume=0;
        void restore(Machine& machine) const;
    };
    const TextSource* source_;
    std::vector<std::vector<std::string>> pages_;
    TextPage view_;
    std::array<unsigned,4> events_{};
    struct AutomaticPage { unsigned page,address; bool reached; };
    std::vector<AutomaticPage> clears_;
    struct NativePage { unsigned retained=0, next_break=0; bool button=false; };
    std::vector<NativePage> native_pages_;
    unsigned native_line_=0, native_remaining_=0, title_scroll_at_=0;
    bool native_pacing_=false, native_append_=false, native_closing_=false;
    unsigned page_=0;
    bool automatic_=false, awaiting_picture_=false;
    std::uint64_t revision_=0;
    bool printer_=false, alternate_=false, reentry_=false, pending_=false;
    bool last_button_=false, held_=false;
    TextEnd ending_=TextEnd::immediate;
    std::uint16_t caller_sp_=0;
    std::uint8_t pressed_mask_=0x20;
    std::optional<Wait> wait_;
    bool shorten_alternate_delay_=false;
};
}
