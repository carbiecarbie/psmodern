// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app/language.h"
#include "app/options_menu.h"

namespace psmodern::app {
enum class LanguageAction { none,save,close };
// A separate global Save/Cancel, independent of the parent's settings draft.
// Physical navigation is supplied by the same MenuInput as Options; a neutral
// focused snapshot arms it. The owner persists draft(), then calls saved().
class LanguageScreen {
public:
    void open(LanguagePreference preference,bool us_available,bool br_available,Language language=Language::english);
    bool visible() const { return visible_; }
    void close() { visible_=false; }
    LanguagePreference draft() const { return draft_; }
    unsigned selected() const { return selected_; }
    bool available(LanguagePreference preference) const;
    LanguageAction update(const MenuInput& input,double now);
    void saved() { close(); }
    void failed() { failed_=true; }
    void render(std::span<std::uint32_t> pixels,const RomFont& font,Language language=Language::english) const;
private:
    LanguagePreference draft_=LanguagePreference::native;
    MenuInput previous_;
    unsigned selected_=0;
    bool visible_=false,armed_=false,failed_=false,us_=false,br_=false;
    double next_repeat_=0;
};
}
