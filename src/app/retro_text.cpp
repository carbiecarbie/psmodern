// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/retro_text.h"
#include "app/retro_font.h"
#include <algorithm>
#include <iterator>

namespace psmodern::app::retro {
Script CartridgeText::resolve(TextKey key,const TextArguments& args) const {
    Script script;
    if (key.event==TextEvent::close) return script;
    const bool alternate=key.event==TextEvent::alternate_layout;
    // These two-row prompts share the load/delete layout. Read the stream
    // pointers from the scene so both cartridges use their own wording.
    constexpr std::array title_prompts{0x067a,0x0685,0x0695,0x06c6,0x06d1,0x06e5,0x06f0};
    const bool title_prompt=!alternate && key.bank==data_.byte(0x31d0) &&
        std::ranges::any_of(title_prompts,[&](unsigned p) { return data_.word(p)==key.address; });
    const auto columns=title_prompt ? data_.byte(0x31f9) : 0u;
    std::string line;
    // The identified cartridges have short, terminated streams. Bound both
    // walks independently so a bad dynamic pointer cannot run off its bank.
    for (unsigned i=0;i<4096 && key.address+i<=0xffff;++i) {
        const auto c=data_.byte(key.address+i,key.bank);
        // The US choice fills the first native row and starts the next
        // label without a space or an explicit newline.
        if (columns && !script.second_title_row && line.size()==columns && (c<0x5f || c>=128)) {
            line+='\n';
            script.second_title_row=key.address+i;
        }
        if (c>=128) {
            const auto table=data_.word(0x3343);
            const auto word=data_.word(table+2*(c&127),2);
            for (unsigned j=0;j<256;++j) {
                const auto letter=data_.byte(word+j,2);
                if (letter==0x65) break;
                line+=cartridge_character(letter);
            }
        } else if (c==0x60) {
            if (title_prompt) {
                line+='\n';
                script.second_title_row=key.address+i+1;
            }
            else if (!line.empty() && line.back()!=' ') line+=' ';
        } else if (c==0x61 || c==0x5f) {
            if (c==0x5f && line.find_first_not_of(' ')==std::string::npos) { line.clear();continue; }
            script.breaks.push_back({static_cast<unsigned>(script.pages.size()),key.address+i,c==0x61});
            script.pages.push_back(std::move(line));
            line.clear();
        } else if ((!alternate && c>=0x62) || (alternate && (c==0x63 || c==0x65))) {
            if (c>=0x65) script.ending=TextEnd::button;
            else if (alternate || c==0x64) script.ending=TextEnd::delay;
            else if (c==0x63) script.ending=TextEnd::interruptible;
            break;
        } else if (!alternate && c==0x5b) line+=args.member;
        else if (!alternate && c==0x5c) line+=args.enemy;
        else if (!alternate && c==0x5d) line+=args.item;
        else if (!alternate && c==0x5e) line+=std::to_string(args.number);
        else line+=cartridge_character(c);
    }
    script.pages.push_back(std::move(line));
    return script;
}

void RetroTextSession::begin(Machine& m,TextKey key,const WindowState& windows) {
    ++events_[unsigned(key.event)];
    Cartridge data(*m.memory().image(),m.memory().ram());
    CartridgeText cartridge(data);
    const auto& source=source_ ? *source_ : static_cast<const TextSource&>(cartridge);
    const auto script=source.resolve(key,{data.member_name(data.byte(0xc2c2)),data.enemy_name(),
                                          data.item_name(data.byte(0xc2c4)),data.word(0xc2c5)});
    alternate_=key.event==TextEvent::alternate_layout;
    const bool battle=!alternate_ && windows.battle(data);
    const unsigned lines=battle ? 2u : 4u;
    native_pacing_=!source_;
    native_closing_=false;
    native_append_=native_pacing_ && key.event==TextEvent::append;
    title_scroll_at_=native_append_ ? script.second_title_row : 0;
    pages_.clear();
    clears_.clear();
    native_pages_.clear();
    std::string retained;
    if (key.event==TextEvent::append && !view_.lines.empty()) retained=view_.lines.back();
    for (unsigned i=0;i<script.pages.size();++i) {
        auto part=paginate(script.pages[i],battle ? 224 : 230,lines);
        if (part.empty()) part.emplace_back();
        const auto first=pages_.size();
        // A native scroll keeps the previous last line above the new words.
        const bool keep=!retained.empty() && part.front().size()<lines;
        if (keep) part.front().insert(part.front().begin(),std::move(retained));
        retained.clear();
        pages_.insert(pages_.end(),std::make_move_iterator(part.begin()),std::make_move_iterator(part.end()));
        if (native_pacing_) {
            native_pages_.resize(pages_.size());
            native_pages_[first].retained=static_cast<unsigned>(keep);
        }
        for (const auto& turn:script.breaks) {
            if (turn.page!=i) continue;
            if (native_pacing_) {
                native_pages_.back().next_break=turn.address;
                native_pages_.back().button=turn.button;
                if (turn.button && !alternate_ && !pages_.back().empty()) retained=pages_.back().back();
            } else if (!turn.button) {
                clears_.push_back({static_cast<unsigned>(pages_.size()-1),turn.address,false});
            }
        }
    }
    if (pages_.empty()) pages_.emplace_back();
    page_=0;
    printer_=true;
    caller_sp_=m.cpu().sp.uint16_value;
    ending_=script.ending;
    if (!native_pacing_) {
        update_page();
        return;
    }
    pending_=awaiting_picture_=false;
    if (!native_append_) show_native_page();
}
void RetroTextSession::update_page() {
    automatic_=false;
    for (const auto& clear:clears_) {
        if (clear.page!=page_ || page_+1>=pages_.size()) continue;
        automatic_=true;
        break;
    }
    pending_=page_+1<pages_.size() || ending_==TextEnd::button;
    view_={pages_[page_],!automatic_ && page_+1<pages_.size(),alternate_,++revision_};
    awaiting_picture_=true;
}
void RetroTextSession::show_native_page() {
    const auto& layout=pages_[page_];
    native_line_=native_pages_[page_].retained;
    view_={std::vector<std::string>(layout.size()),false,alternate_,++revision_};
    if (native_line_) view_.lines.front()=layout.front();
    native_remaining_=0;
    for (auto line=layout.begin()+native_line_; line!=layout.end(); ++line)
        native_remaining_+=static_cast<unsigned>(std::count_if(line->begin(),line->end(),[](char c) { return c!=' '; }));
}
void RetroTextSession::reveal_native_glyph() {
    if (!native_remaining_) return;
    const auto& layout=pages_[page_];
    for (; native_line_<layout.size(); ++native_line_) {
        auto& shown=view_.lines[native_line_];
        // The cartridge decoder produces one byte per character. Layout can
        // discard or insert spaces, so a visible glyph claims the next letter.
        const auto next=layout[native_line_].find_first_not_of(' ',shown.size());
        if (next==std::string::npos) continue;
        shown.assign(layout[native_line_],0,next+1);
        view_.revision=++revision_;
        break;
    }
    const bool extra_page=page_+1<pages_.size() && !native_pages_[page_].next_break;
    if (--native_remaining_==0 && extra_page)
        view_.more=pending_=awaiting_picture_=true;
}
std::optional<std::uint16_t> RetroTextSession::observe_native(Machine& m,unsigned pc) {
    if (!printer_) return {};
    const auto& cpu=m.cpu();
    const auto hl=cpu.hl.uint16_value;
    if (pc==0x2d2a && wait_ && cpu.sp.uint16_value==wait_->sp) {
        if (pending_) return 0x2d25;
        const auto resume=wait_->resume;
        wait_->restore(m);
        wait_.reset();
        return resume;
    }
    if (pending_ && (pc==0x3416 || pc==0x3330)) {
        // The last glyph of a proportional page has reached VRAM and its
        // native frame wait has completed. At this boundary IRQs are enabled
        // and the next glyph can stay unprinted until the displayed advance.
        wait_=Wait{cpu.sp.uint16_value,cpu.af.uint16_value,cpu.memptr.uint16_value,cpu.q,static_cast<std::uint16_t>(pc)};
        return 0x2d25;
    }
    if (pc==0x340f) {
        // Both tile writes are complete and HL has advanced past this glyph.
        if (cartridge_character(m.peek(static_cast<std::uint16_t>(hl-1)))!=' ') reveal_native_glyph();
    } else if (pc==0x332b) {
        reveal_native_glyph(); // the number printer has completed one digit
    } else if (pc==0x3218 || pc==0x33b9) {
        view_.more=false; // the cartridge accepted the interstitial press
    } else if (alternate_ && pc==0x338e) {
        const auto& current=native_pages_[page_];
        if (current.button && hl==current.next_break) view_.more=true;
    } else if (alternate_ && pc==0x33c8) {
        // The portrait printer has completed the physical clear. Its next
        // entry resumes the same message at the next address.
        if (hl==native_pages_[page_].next_break) {
            ++page_;
            show_native_page();
        }
    } else if (!alternate_ && pc==0x31fa && cpu.sp.uint16_value==caller_sp_) {
        if (native_append_) {
            native_append_=false;
            show_native_page(); // the append's native scroll has finished
        }
        if (title_scroll_at_ && hl==title_scroll_at_ && native_pages_[page_].retained) {
            // The native window just scrolled again. Retire the previous
            // prompt and keep the first line of the current instruction.
            pages_[page_].erase(pages_[page_].begin());
            view_.lines.erase(view_.lines.begin());
            native_pages_[page_].retained=0;
            --native_line_;
            title_scroll_at_=0;
            view_.revision=++revision_;
        }
        const auto& current=native_pages_[page_];
        if (current.next_break && hl==current.next_break+1) {
            ++page_;
            show_native_page();
        } else if (current.button && hl==current.next_break) {
            view_.more=true;
        }
        const auto code=m.peek(hl);
        if (code>=0x62 && code<0x80) printer_=false;
    }
    return {};
}
void RetroTextSession::presented(const TextPage& page) {
    if (page.revision!=view_.revision) return;
    awaiting_picture_=false;
    if (native_pacing_ || !automatic_) return;
    for (const auto& clear:clears_) {
        if (clear.page==page_ && clear.reached) {
            ++page_;
            update_page();
            break;
        }
    }
}
void RetroTextSession::native_clear(unsigned address) {
    for (auto& clear:clears_) {
        if (clear.address!=address) continue;
        clear.reached=true;
        if (clear.page==page_ && !awaiting_picture_) {
            ++page_;
            update_page();
        }
        break;
    }
}
FrameInput RetroTextSession::input(FrameInput input,bool paused) {
    auto& p=input.pads[0];
    const bool button=p.one || p.two;
    const bool edge=button && !last_button_;
    last_button_=button;
    if (!button) held_=false;
    if (pending_ && (native_pacing_ || !automatic_) && !awaiting_picture_ && edge && !paused) {
        held_=true;
        if (native_pacing_) {
            pending_=false;
            ++page_;
            show_native_page();
        } else {
            pressed_mask_=static_cast<std::uint8_t>((p.one ? 0x10 : 0) | (p.two ? 0x20 : 0));
            if (page_+1<pages_.size()) { ++page_;update_page(); }
            else {pending_=false;}
        }
    }
    if (pending_ || held_) p.one=p.two=false;
    return input;
}
void RetroTextSession::reset() {
    const auto revision=revision_;
    *this=RetroTextSession(source_);
    revision_=revision;
}
void RetroTextSession::Wait::restore(Machine& m) const {
    auto& cpu=m.cpu();
    cpu.af.uint16_value=af;
    cpu.memptr.uint16_value=wz;
    cpu.q=q;
}
void RetroTextSession::button_result(Machine& m) const {
    // The native AND keeps the two button bits in A and sets H and parity.
    auto& cpu=m.cpu();
    cpu.af.uint8_values.at_1=pressed_mask_;
    cpu.af.uint8_values.at_0=static_cast<std::uint8_t>(0x10 | (pressed_mask_&0x20) | (pressed_mask_==0x30 ? 4 : 0));
    cpu.q=cpu.af.uint8_values.at_0;
}
std::uint16_t RetroTextSession::finish_terminal(Machine& m) {
    printer_=false;
    wait_.reset();
    if (ending_==TextEnd::button) button_result(m);
    if (alternate_) {
        // The layout origin is still on the stack. Its native final CALL
        // provides a real return to the POP DE/RET for either countdown.
        if (ending_==TextEnd::interruptible || ending_==TextEnd::delay) {
            shorten_alternate_delay_=ending_==TextEnd::interruptible;
            return 0x33d1;
        }
        view_={};
        return 0x33cf;
    }
    switch (ending_) {
    case TextEnd::button: return 0x337a; // bare RET to the printer's actual caller
    case TextEnd::interruptible: return 0x2d33;
    case TextEnd::delay: return 0x2d47;
    case TextEnd::immediate:
        m.cpu().af.uint8_values.at_1=0x62;
        return 0x3204; // finish with the native comparison and its flags
    }
    throw std::logic_error("Unknown dialogue ending");
}
std::optional<std::uint16_t> RetroTextSession::observe(Machine& m,unsigned pc,const WindowState& windows) {
    if (m.depth()) return {};
    auto& cpu=m.cpu();
    const auto sp=cpu.sp.uint16_value;
    if (pc==0x0085) { reset();return {}; }
    if (native_closing_ && !windows.dialogue_visible()) {
        view_={};
        native_pacing_=native_closing_=false;
    }
    if (pc==0x3464) {
        ++events_[unsigned(TextEvent::close)];
        if (source_) (void)source_->resolve({TextEvent::close,0,0},{});
        native_closing_=native_pacing_ && windows.dialogue_visible();
        if (!native_closing_) {
            view_={};
            native_pacing_=false;
        }
        pages_.clear();pending_=printer_=false;wait_.reset();
        return {};
    }
    if (pc==0x2d47 && shorten_alternate_delay_) {
        shorten_alternate_delay_=false;
        return 0x2d33;
    }
    if (pc==0x33cf || pc==0x33d4) {
        view_={};
        printer_=false;
        return {};
    }
    if (pc==0x33c8) {
        reentry_=printer_ && alternate_;
        if (native_pacing_) return observe_native(m,pc);
        return {};
    }
    if (pc==0x337d && reentry_) { reentry_=false;return {}; }
    if (pc==0x31d4 || pc==0x337d) {
        const bool alternate=pc==0x337d;
        const auto event=alternate ? TextEvent::alternate_layout : m.peek(0xc2d3) ? TextEvent::append : TextEvent::fresh;
        begin(m,{event,alternate ? 2u : m.memory().mapping(0x8000).bank,cpu.hl.uint16_value},windows);
        return {};
    }
    if (native_pacing_) return observe_native(m,pc);
    if (!printer_) return {};
    if (pc==0x2d2a) {
        if (wait_ && sp==wait_->sp) {
            if (pending_ || awaiting_picture_) return 0x2d25;
            wait_->restore(m);
            return finish_terminal(m);
        }
        const auto ret=m.peek(sp)|(m.peek(static_cast<std::uint16_t>(sp+1))<<8);
        if (sp!=caller_sp_ && (ret==0x3218 || ret==0x33b9)) {
            button_result(m);
            return 0x337a;
        }
        return {};
    }
    if (pc==0x31fa && !alternate_) {
        const auto code=m.peek(cpu.hl.uint16_value);
        if (code==0x5f) native_clear(cpu.hl.uint16_value);
        if (code<0x62 || code>=0x80) return {};
    } else if (pc==0x338e && alternate_) {
        const auto code=cpu.af.uint8_values.at_1;
        if (code==0x5f) native_clear(cpu.hl.uint16_value);
        if (code!=0x63 && code!=0x65) return {};
    } else return {};

    // The native stream is finished. Drive precisely the wait requested by
    // the selected text, even if its cartridge counterpart ends differently.
    // 00:2D25 waits with normal IRQs and no change to the printer's stack.
    if (!pending_ && !awaiting_picture_) return finish_terminal(m);
    wait_=Wait{sp,cpu.af.uint16_value,cpu.memptr.uint16_value,cpu.q};
    return 0x2d25;
}
}
