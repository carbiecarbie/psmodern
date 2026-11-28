// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/language_screen.h"
#include "app/localization.h"

namespace psmodern::app {
void LanguageScreen::open(LanguagePreference preference,bool us_available,bool br_available,Language,bool offer_pack) {
    choice_count_=offer_pack ? pack_language_count : cartridge_language_count;
    if(unsigned(preference)>=choice_count_)throw std::invalid_argument("Unavailable language screen entry.");
    draft_=preference;us_=us_available;br_=br_available;
    selected_=unsigned(preference);visible_=true;armed_=failed_=false;previous_={};next_repeat_=0;
}
bool LanguageScreen::available(LanguagePreference preference) const {
    switch(preference) {
    case LanguagePreference::native:return us_ || br_;
    case LanguagePreference::english:return us_;
    case LanguagePreference::portuguese:return br_;
    case LanguagePreference::sms_english:case LanguagePreference::sms_literal:return choice_count_==pack_language_count;
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
        const auto rows=choice_count_+2;
        selected_=(selected_+(input.down ? 1 : rows-1))%rows;
    }
    if(accept) {
        if(selected_==choice_count_+1) { close();return LanguageAction::close; }
        if(selected_==choice_count_)return available(draft_) ? LanguageAction::save : LanguageAction::none;
        const auto choice=static_cast<LanguagePreference>(selected_);
        if(available(choice)) { draft_=choice;failed_=false; }
    }
    return LanguageAction::none;
}
void LanguageScreen::render(std::span<std::uint32_t> pixels,const RomFont& font,Language language) const {
    constexpr int x=16,y=16,width=224,height=160,first=40,pitch=16;
    constexpr std::uint32_t white=0xffffffff,grey=0xffaaaaaa,yellow=0xffffff00;
    const auto title=ui_text(UiText::language,language);
    font.window(pixels,x,y,width,height,first+pitch*int(selected_));
    font.text(pixels,(256-int(title.size())*8)/2,24,title,white);
    for(unsigned i=0;i<choice_count_;++i) {
        const auto choice=static_cast<LanguagePreference>(i);
        const auto label=language_label(choice,language);
        const bool enabled=available(choice);
        font.text(pixels,32,first+pitch*int(i),label,enabled ? (draft_==choice ? yellow : white) : grey);
        // The saved choice remains visible even if its ROM is unavailable.
        if(draft_==choice)font.text(pixels,216,first+pitch*int(i),"-",enabled ? yellow : grey);
    }
    font.text(pixels,32,first+pitch*int(choice_count_),ui_text(UiText::save,language),available(draft_) ? white : grey);
    font.text(pixels,32,first+pitch*int(choice_count_+1),ui_text(UiText::cancel,language),white);
    auto status=ui_text(UiText::applies_to_app,language);
    if(failed_)status=ui_text(UiText::save_failed,language);
    else if(selected_==1 && !us_)status=ui_text(UiText::needs_us_rom,language);
    else if(selected_==2 && !br_)status=ui_text(UiText::needs_br_rom,language);
    else if(!available(draft_))status=ui_text(draft_==LanguagePreference::english ? UiText::needs_us_rom : UiText::needs_br_rom,language);
    font.text(pixels,32,160,status,white);
}
}
