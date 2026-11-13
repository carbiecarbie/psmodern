// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/bindings.h"
#include "app/save_store.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace psmodern::app {
namespace {
constexpr std::string_view action_tokens[action_count]={"up","down","left","right","button1","button2","pause","reset",
    "speed","options","map","power","fullscreen"};
constexpr std::string_view action_labels[action_count]={"UP","DOWN","LEFT","RIGHT","BUTTON 1","BUTTON 2","PAUSE","RESET",
    "SPEED","OPTIONS","MAP","POWER CYCLE","FULLSCREEN"};
constexpr std::string_view pad_tokens[pad_input_count]={"A","B","X","Y","BACK","GUIDE","START","LEFTSTICK","RIGHTSTICK",
    "LEFTSHOULDER","RIGHTSHOULDER","DPUP","DPDOWN","DPLEFT","DPRIGHT","MISC1","PADDLE1","PADDLE2","PADDLE3","PADDLE4",
    "TOUCHPAD","LEFTTRIGGER","RIGHTTRIGGER"};
constexpr std::string_view pad_labels[pad_input_count]={"A","B","X","Y","BACK","GUIDE","START","LSTICK","RSTICK",
    "LB","RB","D UP","D DOWN","D LEFT","D RIGHT","MISC","PADDLE1","PADDLE2","PADDLE3","PADDLE4","TOUCH","LT","RT"};
constexpr bool sorted_table() {
    for (std::size_t i=1;i<key_table_size;++i) if (key_table[i-1].key>=key_table[i].key) return false;
    return key_table[key_table_size-1].key<key_slots;
}
static_assert(sorted_table(),"the key table is ascending and inside key_slots");
constexpr Key key_enter=40, key_escape=41, key_backspace=42, key_tab=43, key_x=27, key_z=29, key_m=16, key_f5=62,
    key_right=79, key_left=80, key_down=81, key_up=82;
bool same_word(std::string_view a,std::string_view b) {
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](char x,char y) {
        auto lower=[](char c) { return c>='A' && c<='Z' ? char(c-'A'+'a') : c; };
        return lower(x)==lower(y);
    });
}
template<class T,class Column> ChangeResult assign(Column& column,Action action,T value,bool keys) {
    auto& own=column[action];
    if (own==value) return {Change::unchanged};
    for (std::size_t i=0;i<action_count;++i) {
        if (i==std::size_t(action) || column[i]!=value) continue;
        const auto other=Action(i);
        if (keys && !own && key_required(other)) return {Change::refused_in_use,other};
        column[i]=own; own=value;
        return {Change::swapped,other};
    }
    own=value;
    return {Change::set};
}
bool held_key(const Bindings& b,Action a,const InputSnapshot& s) { return s.focused && b.keys[a] && *b.keys[a]<key_slots && s.keys[*b.keys[a]]; }
bool held_pad(const Bindings& b,Action a,const InputSnapshot& s) { return s.controller && b.pads[a] && s.pad[std::size_t(*b.pads[a])]; }
bool held(const Bindings& b,Action a,const InputSnapshot& s) { return held_key(b,a,s) || held_pad(b,a,s); }
// The controller half as Controller::sample() built it: bound pad inputs plus
// the left stick, nothing without a controller.
Buttons pad_buttons(const Bindings& b,const InputSnapshot& s) {
    if (!s.controller) return {};
    auto p=[&](Action a) { return held_pad(b,a,s); };
    return controller({{p(Action::up),p(Action::down),p(Action::left),p(Action::right),p(Action::button1),p(Action::button2)},
                       p(Action::pause),p(Action::reset)},s.stick_x,s.stick_y);
}
MenuInput directions(const Bindings& b,const InputSnapshot& s) {
    const auto pad=pad_buttons(b,s).pad;
    MenuInput m;
    m.up=pad.up || held_key(b,Action::up,s); m.down=pad.down || held_key(b,Action::down,s);
    m.left=pad.left || held_key(b,Action::left,s); m.right=pad.right || held_key(b,Action::right,s);
    m.focused=s.focused;
    return m;
}
}

Bindings Bindings::defaults() {
    Bindings b;
    const Key keys[]={key_up,key_down,key_left,key_right,key_z,key_x,key_enter,key_backspace,key_tab,key_escape,key_m,key_f5};
    for (std::size_t i=0;i<std::size(keys);++i) b.keys[i]=keys[i];
    using P=PadInput;
    const P pads[]={P::dpad_up,P::dpad_down,P::dpad_left,P::dpad_right,P::a,P::b,P::start,P::back,P::right_shoulder,P::y,P::x};
    for (std::size_t i=0;i<std::size(pads);++i) b.pads[i]=pads[i];
    return b;
}
bool key_required(Action a) { return a<=Action::button2; }
bool key_fixed(Action a) { return a==Action::fullscreen; }
bool key_bindable(Key key) { return !key_token(key).empty(); }
ChangeResult assign_key(Bindings& b,Action a,Key key) {
    if (key_fixed(a)) return {Change::fixed};
    if (!key_bindable(key)) return {Change::unchanged};
    return assign<std::optional<Key>>(b.keys,a,key,true);
}
ChangeResult assign_pad(Bindings& b,Action a,PadInput input) {
    if (input>=PadInput::count) return {Change::unchanged};
    return assign<std::optional<PadInput>>(b.pads,a,input,false);
}
ChangeResult clear_key(Bindings& b,Action a) {
    if (key_fixed(a)) return {Change::fixed};
    if (key_required(a)) return {Change::refused_required};
    if (!b.keys[a]) return {Change::unchanged};
    b.keys[a].reset();
    return {Change::set};
}
ChangeResult clear_pad(Bindings& b,Action a) {
    if (!b.pads[a]) return {Change::unchanged};
    b.pads[a].reset();
    return {Change::set};
}
std::string_view action_token(Action a) { return a<Action::count ? action_tokens[std::size_t(a)] : std::string_view{}; }
std::string_view action_label(Action a) { return a<Action::count ? action_labels[std::size_t(a)] : std::string_view{}; }
std::optional<Key> key_from_token(std::string_view token) {
    for (const auto& entry:key_table) if (same_word(entry.token,token)) return entry.key;
    return std::nullopt;
}
std::string_view key_token(Key key) {
    const auto found=std::lower_bound(std::begin(key_table),std::end(key_table),key,[](const KeyName& e,Key k) { return e.key<k; });
    return found!=std::end(key_table) && found->key==key ? found->token : std::string_view{};
}
std::optional<PadInput> pad_from_token(std::string_view token) {
    for (std::size_t i=0;i<pad_input_count;++i) if (same_word(pad_tokens[i],token)) return PadInput(i);
    return std::nullopt;
}
std::string_view pad_token(PadInput p) { return p<PadInput::count ? pad_tokens[std::size_t(p)] : std::string_view{}; }
std::string_view pad_label(PadInput p) { return p<PadInput::count ? pad_labels[std::size_t(p)] : std::string_view{}; }

FrameInput frame_input(const Bindings& b,const InputSnapshot& s) {
    Keys keys{};
    for (std::size_t i=0;i<console_action_count;++i) keys[i]=held_key(b,Action(i),s);
    return combine(keyboard(keys),pad_buttons(b,s));
}
MenuInput menu_input(const Bindings& b,const InputSnapshot& s) {
    auto m=directions(b,s);
    m.accept=held(b,Action::button1,s); m.cancel=held(b,Action::button2,s);
    m.toggle=held(b,Action::options,s); m.map=held(b,Action::map,s);
    return m;
}
MenuInput start_input(const InputSnapshot& s) {
    static const auto b=Bindings::defaults();
    auto m=directions(b,s);
    m.accept=held(b,Action::button1,s) || held(b,Action::pause,s);
    m.cancel=held_key(b,Action::options,s);
    return m;
}

BindingsLoad parse_bindings(std::string_view text) {
    BindingsLoad result;
    if (text.size()>bindings_file_limit) { result.warning="the file is larger than 4096 bytes; using the default controls"; return result; }
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);   // a UTF-8 mark from a text editor
    auto next_line=[&]() {
        const auto end=text.find('\n');
        auto line=text.substr(0,end);
        text=end==std::string_view::npos ? std::string_view{} : text.substr(end+1);
        if (line.ends_with('\r')) line.remove_suffix(1);
        return line;
    };
    const bool more=!text.empty();
    if (!more || next_line()!="psmodern-controls 1") { result.warning="unknown format; using the default controls"; return result; }
    while (!text.empty()) {
        const auto line=next_line();
        std::vector<std::string_view> words;
        for (std::size_t i=0;i<line.size();) {
            if (line[i]==' ' || line[i]=='\t') { ++i; continue; }
            auto end=line.find_first_of(" \t",i);
            if (end==std::string_view::npos) end=line.size();
            words.push_back(line.substr(i,end-i)); i=end;
        }
        if (words.empty() || words[0].starts_with('#')) continue;
        auto ignore=[&] { ++result.ignored; };
        if (words.size()!=3) { ignore(); continue; }
        std::optional<Action> action;
        for (std::size_t i=0;i<action_count;++i) if (same_word(action_tokens[i],words[0])) action=Action(i);
        const bool key=same_word(words[1],"key"), pad=same_word(words[1],"pad");
        if (!action || (!key && !pad)) { ignore(); continue; }
        const bool none=same_word(words[2],"NONE");
        ChangeResult change{Change::unchanged};
        if (key) {
            const auto value=key_from_token(words[2]);
            if (!none && !value) { ignore(); continue; }
            change=none ? clear_key(result.bindings,*action) : assign_key(result.bindings,*action,*value);
        } else {
            const auto value=pad_from_token(words[2]);
            if (!none && !value) { ignore(); continue; }
            change=none ? clear_pad(result.bindings,*action) : assign_pad(result.bindings,*action,*value);
        }
        if (change.change==Change::refused_in_use || change.change==Change::refused_required || change.change==Change::fixed) ignore();
    }
    if (result.ignored)
        result.warning="ignored "+std::to_string(result.ignored)+(result.ignored==1 ? " line" : " lines")+" that could not be used";
    return result;
}
std::string format_bindings(const Bindings& b) {
    std::string text="psmodern-controls 1\n"
        "# PSModern controls. Change them on the CONTROLS screen, or edit this file.\n"
        "# Each line: <action> key|pad <name>, or NONE to leave it unset.\n"
        "# The left stick always moves; Alt+Enter always toggles fullscreen.\n";
    for (std::size_t i=0;i<action_count;++i) {
        const auto a=Action(i);
        if (!key_fixed(a)) ((text+=action_token(a))+=" key ")+=(b.keys[a] ? key_token(*b.keys[a]) : unbound_label),text+='\n';
        ((text+=action_token(a))+=" pad ")+=(b.pads[a] ? pad_token(*b.pads[a]) : unbound_label),text+='\n';
    }
    return text;
}
BindingsLoad load_bindings(const std::filesystem::path& folder) {
    const auto path=folder/bindings_file_name;
    std::error_code error;
    const auto status=std::filesystem::status(path,error);
    if (status.type()==std::filesystem::file_type::not_found) return {};
    BindingsLoad result;
    try {
        if (error) throw std::runtime_error(error.message());
        result=parse_bindings(read_text_file(path,bindings_file_limit,"controls file"));
    } catch (const std::exception& e) {
        result={}; result.warning=std::string("cannot read the file; using the default controls: ")+e.what();
    }
    if (result.warning) {
        const auto name=path.u8string();
        result.warning="Controls file "+std::string(name.begin(),name.end())+": "+*result.warning;
    }
    return result;
}
void save_bindings(const std::filesystem::path& folder,const Bindings& b) {
    atomic_text(folder/bindings_file_name,format_bindings(b));
}
}
