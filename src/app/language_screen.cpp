// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/language_screen.h"
#include "app/localization.h"

namespace psmodern::app {
namespace {
constexpr LanguagePreference choices[]{LanguagePreference::native,LanguagePreference::english,LanguagePreference::portuguese};
constexpr unsigned save_row=3,cancel_row=4,rows=5;
}
void LanguageScreen::open(LanguagePreference preference,bool us_available,bool br_available,Language) {
    // Validate enum input before opening, also for callers without a file.
    (void)language_label(preference);
    draft_=preference;us_=us_available;br_=br_available;
    selected_=unsigned(preference);visible_=true;armed_=failed_=false;previous_={};next_repeat_=0;
}
bool LanguageScreen::available(LanguagePreference preference) const {
    switch(preference) {
    case LanguagePreference::native:return us_ || br_;
    case LanguagePreference::english:return us_;
    case LanguagePreference::portuguese:return br_;
    }
    return false;
}
LanguageAction LanguageScreen::update(const MenuInput& input,double now) {
    if(!visible_)return LanguageAction::none;
    if(!input.focused) { armed_=false;previous_=input;return LanguageAction::none; }
    if(!armed_) { if(!input.any())armed_=true;previous_=input;return LanguageAction::none; }
    const bool cancel=(input.cancel && !previous_.cancel)||(input.toggle && !previous_.toggle);
    const bool accept=input.accept && !previous_.accept;
    const bool fresh=(input.up && !previous_.up)||(input.down && !previous_.down);
    const bool move=input.up!=input.down && (fresh || now>=next_repeat_);
    previous_=input;
    if(cancel) { close();return LanguageAction::close; }
    if(move) {
        next_repeat_=now+(fresh ? .35 : .12);
        selected_=(selected_+(input.down ? 1 : rows-1))%rows;
    }
    if(accept) {
        if(selected_==cancel_row) { close();return LanguageAction::close; }
        if(selected_==save_row)return available(draft_) ? LanguageAction::save : LanguageAction::none;
        if(available(choices[selected_])) { draft_=choices[selected_];failed_=false; }
    }
    return LanguageAction::none;
}
void LanguageScreen::render(std::span<std::uint32_t> pixels,const RomFont& font,Language language) const {
    constexpr int x=16,y=16,width=224,height=160,first=48,pitch=24;
    constexpr std::uint32_t white=0xffffffff,grey=0xffaaaaaa,yellow=0xffffff00;
    const auto title=ui_text(UiText::language,language);
    font.window(pixels,x,y,width,height,first+pitch*int(selected_));
    font.text(pixels,(256-int(title.size())*8)/2,24,title,white);
    for(unsigned i=0;i<3;++i) {
        const auto label=language_label(choices[i],language);
        const bool enabled=available(choices[i]);
        font.text(pixels,32,first+pitch*int(i),label,enabled ? (draft_==choices[i] ? yellow : white) : grey);
        // The saved choice remains visible even if its ROM is unavailable.
        if(draft_==choices[i])font.text(pixels,216,first+pitch*int(i),"-",enabled ? yellow : grey);
    }
    font.text(pixels,32,first+pitch*int(save_row),ui_text(UiText::save,language),available(draft_) ? white : grey);
    font.text(pixels,32,first+pitch*int(cancel_row),ui_text(UiText::cancel,language),white);
    auto status=ui_text(UiText::applies_to_app,language);
    if(failed_)status=ui_text(UiText::save_failed,language);
    else if(selected_==1 && !us_)status=ui_text(UiText::needs_us_rom,language);
    else if(selected_==2 && !br_)status=ui_text(UiText::needs_br_rom,language);
    else if(!available(draft_))status=ui_text(draft_==LanguagePreference::english ? UiText::needs_us_rom : UiText::needs_br_rom,language);
    font.text(pixels,32,160,status,white);
}
}
