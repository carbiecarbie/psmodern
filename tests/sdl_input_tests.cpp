// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::array<Uint8,SDL_NUM_SCANCODES> keys{};
SDL_Window* focused=nullptr;
const Uint8* snapshot(int*) {return keys.data();}
SDL_Window* focus() {return focused;}
void pump() {}
Uint32 window_id(SDL_Window* window) {return window ? 7 : 0;}
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void neutral(const psmodern::FrameInput& input,const char* message) {
    check(input.pads==std::array<psmodern::Pad,2>{}&&!input.pause&&!input.reset,message);
}
}
// Substitute only SDL's physical keyboard snapshot, exercising the real sampler.
#define SDL_GetKeyboardState snapshot
#define SDL_GetKeyboardFocus focus
#define SDL_PumpEvents pump
#define SDL_GetWindowID window_id
#include "../src/app/sdl_host.cpp"
#undef SDL_GetKeyboardState
#undef SDL_GetKeyboardFocus
#undef SDL_PumpEvents
#undef SDL_GetWindowID

int main() {try {
    using namespace psmodern::app;
    Controller controller;EnterKey enter;
    int window_token=0;
    auto window=reinterpret_cast<SDL_Window*>(&window_token);focused=window;
    keys[SDL_SCANCODE_RETURN]=keys[SDL_SCANCODE_LALT]=1;
    check(!sample_input(window,controller,enter).pause,"Alt+Enter snapshot must not pause before its queued key event is handled");
    check(enter.event(true,true,false),"Alt+Enter must toggle fullscreen");
    check(!sample_input(window,controller,enter).pause,"shortcut must consume Pause");
    keys[SDL_SCANCODE_LALT]=0;
    check(!sample_input(window,controller,enter).pause,"Alt release must not turn held Enter into Pause");
    keys[SDL_SCANCODE_RALT]=1;
    check(!sample_input(window,controller,enter).pause,"switching Alt keys must not release consumed Enter");
    keys[SDL_SCANCODE_RETURN]=0;enter.event(false,true,false);
    keys[SDL_SCANCODE_RALT]=0;keys[SDL_SCANCODE_RETURN]=1;enter.event(true,false,false);
    check(sample_input(window,controller,enter).pause,"fresh Enter press must pause");
    keys={};
    // Exercise the real sampler with only physical keyboard/focus snapshots
    // substituted. Menu actions and the release barrier are production code.
    OptionsMenu menu;InputRelease release;double now=0;
    keys[SDL_SCANCODE_ESCAPE]=keys[SDL_SCANCODE_RIGHT]=1;
    auto input=sample_menu_input(window,controller);
    check(input.toggle&&input.right&&input.focused,"actual menu sampler keyboard mapping");
    menu.open({},false,false);release.consume();menu.update(input,now++);
    focused=nullptr;input=sample_menu_input(window,controller);
    check(!input.any()&&!input.focused,"focus loss suppresses held keyboard menu inputs");
    menu.update(input,now++);release.filter(sample_input(window,controller,enter),input.any(),input.focused);
    focused=window;input=sample_menu_input(window,controller);
    check(menu.update(input,now++)==MenuAction::none && menu.draft()==psmodern::game::Options{},"regaining focus with opening keys held cannot close or change menu");
    keys={};menu.update(sample_menu_input(window,controller),now++);
    keys[SDL_SCANCODE_RIGHT]=1;menu.update(sample_menu_input(window,controller),now++);
    check(menu.draft().encounter_percent==200,"fresh focused direction edits one rate");
    focused=nullptr;menu.update(sample_menu_input(window,controller),now++);
    focused=window;menu.update(sample_menu_input(window,controller),now++);
    check(menu.draft().encounter_percent==200,"held direction does not repeat across focus regain");
    keys={};menu.update(sample_menu_input(window,controller),now++);
    keys[SDL_SCANCODE_X]=keys[SDL_SCANCODE_Z]=keys[SDL_SCANCODE_RETURN]=keys[SDL_SCANCODE_BACKSPACE]=1;
    check(menu.update(sample_menu_input(window,controller),now++)==MenuAction::apply,"fresh keyboard X saves on exit after focus regain");
    menu.close();release.consume();input=sample_menu_input(window,controller);
    neutral(release.filter(sample_input(window,controller,enter),input.any(),input.focused),"held close/game/Pause/Reset keys do not leak");
    focused=nullptr;input=sample_menu_input(window,controller);
    neutral(release.filter(sample_input(window,controller,enter),input.any(),input.focused),"unfocused snapshot remains consumed");
    focused=window;input=sample_menu_input(window,controller);
    neutral(release.filter(sample_input(window,controller,enter),input.any(),input.focused),"focus loss did not falsely release held game inputs");
    keys={};input=sample_menu_input(window,controller);release.filter(sample_input(window,controller,enter),input.any(),input.focused);
    keys[SDL_SCANCODE_Z]=keys[SDL_SCANCODE_RETURN]=1;
    auto game=release.filter(sample_input(window,controller,enter));check(game.pads[0].one&&game.pause,"fresh gameplay input works after focused release");
    keys={};
    InputRelease ordinary;psmodern::FrameInput background_controller;background_controller.pads[0].one=true;
    check(ordinary.filter(background_controller,false,false).pads[0].one,"ordinary controller sampling is unchanged without a menu-release barrier");
    {
        // Milestone 8: with the default bindings the real samplers give
        // 0.2.0's keyboard results, and the start screen gives them whatever
        // the bindings. The 0.2.0 bodies are kept here; every state of their
        // keys, Alt, focus and a consumed Return is compared.
        auto legacy_game=[](const Uint8* k,bool focus,const EnterKey& e) {
            Keys held{};
            if(focus)held={k[SDL_SCANCODE_UP]!=0,k[SDL_SCANCODE_DOWN]!=0,k[SDL_SCANCODE_LEFT]!=0,k[SDL_SCANCODE_RIGHT]!=0,
                k[SDL_SCANCODE_Z]!=0,k[SDL_SCANCODE_X]!=0,e.pause(k[SDL_SCANCODE_RETURN]) && !(k[SDL_SCANCODE_LALT]||k[SDL_SCANCODE_RALT]),k[SDL_SCANCODE_BACKSPACE]!=0};
            return combine(keyboard(held),Buttons{});
        };
        auto legacy_menu=[](const Uint8* k,bool focus,const EnterKey* e) {
            MenuInput m;m.focused=focus;
            if(!focus)return m;
            m.up=k[SDL_SCANCODE_UP];m.down=k[SDL_SCANCODE_DOWN];m.left=k[SDL_SCANCODE_LEFT];m.right=k[SDL_SCANCODE_RIGHT];
            if(!e){m.accept=k[SDL_SCANCODE_Z];m.cancel=k[SDL_SCANCODE_X];m.toggle=k[SDL_SCANCODE_ESCAPE];m.map=k[SDL_SCANCODE_M];}
            else{m.accept=k[SDL_SCANCODE_Z] || (e->pause(k[SDL_SCANCODE_RETURN]) && !(k[SDL_SCANCODE_LALT]||k[SDL_SCANCODE_RALT]));m.cancel=k[SDL_SCANCODE_ESCAPE];}
            return m;
        };
        auto same=[](const MenuInput& a,const MenuInput& b) {
            return a.up==b.up&&a.down==b.down&&a.left==b.left&&a.right==b.right&&a.accept==b.accept&&a.cancel==b.cancel&&
                a.toggle==b.toggle&&a.map==b.map&&a.focused==b.focused;
        };
        constexpr SDL_Scancode tried[]{SDL_SCANCODE_UP,SDL_SCANCODE_DOWN,SDL_SCANCODE_LEFT,SDL_SCANCODE_RIGHT,SDL_SCANCODE_Z,SDL_SCANCODE_X,
            SDL_SCANCODE_RETURN,SDL_SCANCODE_BACKSPACE,SDL_SCANCODE_ESCAPE,SDL_SCANCODE_M,SDL_SCANCODE_TAB,SDL_SCANCODE_F5,SDL_SCANCODE_LALT,SDL_SCANCODE_RALT};
        constexpr unsigned bits=std::size(tried);
        // Hostile bindings: no pad input at all, every clearable key cleared,
        // the directions and buttons swapped, Escape on Pause, Return on Options.
        auto hostile=Bindings::defaults();
        for(std::size_t i=0;i<action_count;++i)clear_pad(hostile,Action(i));
        for(const auto a:{Action::pause,Action::reset,Action::speed,Action::options,Action::map,Action::power})clear_key(hostile,a);
        check(assign_key(hostile,Action::up,SDL_SCANCODE_DOWN).change==Change::swapped&&assign_key(hostile,Action::left,SDL_SCANCODE_RIGHT).change==Change::swapped&&
              assign_key(hostile,Action::button1,SDL_SCANCODE_X).change==Change::swapped&&assign_key(hostile,Action::pause,SDL_SCANCODE_ESCAPE).change==Change::set&&
              assign_key(hostile,Action::options,SDL_SCANCODE_RETURN).change==Change::set&&assign_key(hostile,Action::map,SDL_SCANCODE_TAB).change==Change::set,"hostile bindings");
        for(unsigned state=0;state<(1u<<(bits+2));++state) {
            keys={};for(unsigned i=0;i<bits;++i)keys[tried[i]]=(state>>i)&1;
            focused=(state>>bits)&1 ? window : nullptr;
            EnterKey e;if((state>>(bits+1))&1)e.event(true,true,false);
            const auto game_now=sample_input(window,controller,e),game_then=legacy_game(keys.data(),focused==window,e);
            check(game_now.pads==game_then.pads&&game_now.pause==game_then.pause&&game_now.reset==game_then.reset,"default bindings give 0.2.0's game input");
            check(same(sample_menu_input(window,controller),legacy_menu(keys.data(),focused==window,nullptr)),"default bindings give 0.2.0's menu input");
            check(same(sample_start_input(window,controller,e),legacy_menu(keys.data(),focused==window,&e)),"default bindings give 0.2.0's start input");
            controller.set_bindings(hostile);
            check(same(sample_start_input(window,controller,e),legacy_menu(keys.data(),focused==window,&e)),"the start screen ignores hostile bindings");
            controller.set_bindings(Bindings::defaults());
        }
        keys={};focused=window;
    }

    // Use actual SDL virtual-controller state, mapping and added/removed
    // events rather than scripting sample_menu_input or Controller::sample.
    // Keep a player's attached hardware and real window focus outside this
    // test: only this process's virtual VID/PID may be selected by refresh.
    // The VID/PID filter alone does not hide a connected Xbox pad: SDL 2.32
    // still lists it (isGC=1) through the Windows RawInput driver, so refresh()
    // opened the player's hardware first. Switch off every hardware back-end
    // before SDL_Init; only the virtual device is then ever enumerated.
    for(const char* driver:{SDL_HINT_JOYSTICK_RAWINPUT,SDL_HINT_JOYSTICK_HIDAPI,SDL_HINT_JOYSTICK_WGI,SDL_HINT_XINPUT_ENABLED,SDL_HINT_DIRECTINPUT_ENABLED})
        check(SDL_SetHintWithPriority(driver,"0",SDL_HINT_OVERRIDE)==SDL_TRUE,"hardware joystick back-end disabled");
    check(SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0xffff/0x5053",SDL_HINT_OVERRIDE)==SDL_TRUE,"isolated virtual-controller filter");
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1",SDL_HINT_OVERRIDE);
    check(SDL_Init(SDL_INIT_GAMECONTROLLER)==0,"controller subsystem");
    {
        SDL_VirtualJoystickDesc descriptor{};descriptor.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        descriptor.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;descriptor.naxes=2;descriptor.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
        descriptor.axis_mask=(1u<<SDL_CONTROLLER_AXIS_LEFTX)|(1u<<SDL_CONTROLLER_AXIS_LEFTY);
        descriptor.vendor_id=0xffff;descriptor.product_id=0x5053;
        descriptor.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;descriptor.name="PSModern input test controller";
        check(SDL_NumJoysticks()==0,"no hardware joystick enumerated before the virtual attach");
        const auto index=SDL_JoystickAttachVirtualEx(&descriptor);check(index>=0,"virtual controller attachment");
        check(SDL_NumJoysticks()==1&&SDL_JoystickIsVirtual(index),"only the virtual controller is enumerated");
        auto joystick=SDL_JoystickOpen(index);check(joystick!=nullptr,"virtual joystick state access");
        Controller real_controller;bool added=false,removed=false;
        auto events=[&] {
            SDL_Event event;
            while(SDL_PollEvent(&event)) {
                if(event.type==SDL_CONTROLLERDEVICEADDED){added=true;real_controller.refresh();}
                if(event.type==SDL_CONTROLLERDEVICEREMOVED){removed=true;real_controller.refresh();}
            }
        };
        events();check(added,"real controller-added event handled");
        auto button=[&](SDL_GameControllerButton button,bool down){check(SDL_JoystickSetVirtualButton(joystick,button,down?1:0)==0,"virtual button update");SDL_JoystickUpdate();};
        button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,true);
        check(real_controller.held(*real_controller.bindings().pads[Action::speed]),"right shoulder reaches speed sampler");
        neutral(sample_input(window,real_controller,enter),"right shoulder does not leak to gameplay");
        button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,false);
        check(!real_controller.held(*real_controller.bindings().pads[Action::speed]),"right shoulder release reaches speed sampler");
        button(SDL_CONTROLLER_BUTTON_Y,true);button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,true);
        input=sample_menu_input(window,real_controller);
        check(input.toggle&&input.right,"real controller Y and d-pad reach menu sampler");
        game=sample_input(window,real_controller,enter);
        check(game.pads[0].right&&!game.pause&&!game.reset,"controller Y opens options without sending Pause or Reset");
        menu.open({},false,false);release.consume();menu.update(input,now++);
        button(SDL_CONTROLLER_BUTTON_Y,false);button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,false);
        menu.update(sample_menu_input(window,real_controller),now++);
        button(SDL_CONTROLLER_BUTTON_DPAD_DOWN,true);menu.update(sample_menu_input(window,real_controller),now++);
        check(menu.selected()==1,"real controller navigates armed modal");
        button(SDL_CONTROLLER_BUTTON_START,true);check(sample_input(window,real_controller,enter).pause,"real controller Start retains original Pause mapping");
        button(SDL_CONTROLLER_BUTTON_START,false);
        button(SDL_CONTROLLER_BUTTON_Y,true);
        check(SDL_JoystickDetachVirtual(index)==0,"detach while Y and direction held");events();check(removed,"real controller-removed event handled");
        input=sample_menu_input(window,real_controller);
        check(!input.any(),"detached controller leaves no stuck Y or direction");
        neutral(sample_input(window,real_controller,enter),"detached controller leaves no gameplay input");
        menu.update(input,now++);keys[SDL_SCANCODE_X]=1;
        check(menu.update(sample_menu_input(window,real_controller),now++)==MenuAction::apply,"keyboard saves on exit after controller removal");
        menu.close();release.consume();input=sample_menu_input(window,real_controller);
        neutral(release.filter(sample_input(window,real_controller,enter),input.any(),input.focused),"keyboard close does not leak after detach");
        keys={};input=sample_menu_input(window,real_controller);release.filter(sample_input(window,real_controller,enter),input.any(),input.focused);
        keys[SDL_SCANCODE_UP]=keys[SDL_SCANCODE_Z]=1;game=release.filter(sample_input(window,real_controller,enter));
        check(game.pads[0].up&&game.pads[0].one&&!game.pause&&!game.reset,"gameplay recovers without stuck inputs after detach and close");
        SDL_JoystickClose(joystick);
    }
    {
        // Milestone 8: the Controller helpers on a virtual controller with both
        // triggers, plus key_change and the Controls screen sampler.
        for(int i=0;i<SDL_CONTROLLER_BUTTON_MAX;++i) {
            std::string name=SDL_GameControllerGetStringForButton(SDL_GameControllerButton(i));
            for(auto& c:name)c=char(std::toupper(static_cast<unsigned char>(c)));
            check(name==pad_token(PadInput(i)),"pad token is SDL's button name uppercased");
        }
        check(std::string_view(SDL_GameControllerGetStringForAxis(SDL_CONTROLLER_AXIS_TRIGGERLEFT))=="lefttrigger"&&
              std::string_view(SDL_GameControllerGetStringForAxis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT))=="righttrigger"&&
              pad_token(PadInput::left_trigger)=="LEFTTRIGGER"&&pad_token(PadInput::right_trigger)=="RIGHTTRIGGER","trigger tokens are SDL's axis names");
        SDL_VirtualJoystickDesc descriptor{};descriptor.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        descriptor.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;descriptor.naxes=SDL_CONTROLLER_AXIS_MAX;descriptor.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
        descriptor.axis_mask=(1u<<SDL_CONTROLLER_AXIS_MAX)-1;descriptor.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;
        descriptor.vendor_id=0xffff;descriptor.product_id=0x5053;descriptor.name="PSModern trigger test controller";
        Controller pad;std::vector<Controller::PadChange> changes;
        InputSnapshot held;pad.snapshot(held);
        check(!pad.connected()&&!held.controller&&held.pad.none()&&!pad.held(PadInput::a)&&!pad.held(PadInput::left_trigger),"no controller, nothing held");
        auto events=[&] {
            SDL_Event event;
            while(SDL_PollEvent(&event)) {
                if(event.type==SDL_CONTROLLERDEVICEADDED||event.type==SDL_CONTROLLERDEVICEREMOVED)pad.refresh();
                else if(auto change=pad.translate(event))changes.push_back(*change);
            }
        };
        auto index=SDL_JoystickAttachVirtualEx(&descriptor);check(index>=0&&SDL_NumJoysticks()==1,"virtual trigger controller attachment");
        auto joystick=SDL_JoystickOpen(index);check(joystick!=nullptr,"virtual trigger joystick");
        events();changes.clear();check(pad.connected(),"trigger controller opened");
        auto axis=[&](int axis,Sint16 raw){check(SDL_JoystickSetVirtualAxis(joystick,axis,raw)==0,"virtual axis update");SDL_JoystickUpdate();events();};
        auto button=[&](int button,bool down){check(SDL_JoystickSetVirtualButton(joystick,button,down?1:0)==0,"virtual button update");SDL_JoystickUpdate();events();};
        auto value=[&](SDL_GameControllerAxis a){return int(SDL_GameControllerGetAxis(SDL_GameControllerFromInstanceID(SDL_JoystickInstanceID(joystick)),a));};
        auto one=[&](PadInput input,bool down,const char* message){
            check(changes.size()==1&&changes[0].input==input&&changes[0].down==down,message);changes.clear();
        };
        // The raw virtual axis spans -32768..32767; SDL maps it onto the
        // trigger's 0..32767, so the raw values below land on both sides of
        // trigger_threshold and trigger_release_threshold (checked through
        // SDL itself).
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);changes.clear();
        check(value(SDL_CONTROLLER_AXIS_TRIGGERLEFT)<=trigger_threshold&&!pad.held(PadInput::left_trigger),"released left trigger");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-12000);
        check(value(SDL_CONTROLLER_AXIS_TRIGGERLEFT)>0&&value(SDL_CONTROLLER_AXIS_TRIGGERLEFT)<trigger_threshold&&changes.empty()&&!pad.held(PadInput::left_trigger),"left trigger below the threshold is not pressed");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,12000);
        check(value(SDL_CONTROLLER_AXIS_TRIGGERLEFT)>trigger_threshold&&pad.held(PadInput::left_trigger),"left trigger above the threshold is held");
        one(PadInput::left_trigger,true,"crossing the threshold reports one press");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,20000);axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,13000);
        check(changes.empty(),"a held trigger moving above the threshold reports nothing more");
        pad.snapshot(held);check(held.controller&&held.pad[std::size_t(PadInput::left_trigger)]&&!held.pad[std::size_t(PadInput::right_trigger)],"snapshot thresholds the triggers");
        // Hysteresis: released only below trigger_release_threshold.
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-12000);
        check(value(SDL_CONTROLLER_AXIS_TRIGGERLEFT)>=trigger_release_threshold&&changes.empty()&&pad.held(PadInput::left_trigger),"falling between the release and press points keeps the trigger pressed");
        pad.snapshot(held);check(held.pad[std::size_t(PadInput::left_trigger)],"and the snapshot agrees");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-20000);check(value(SDL_CONTROLLER_AXIS_TRIGGERLEFT)<trigger_release_threshold,"raw -20000 is below the release point");
        one(PadInput::left_trigger,false,"falling below the release point reports one release");
        check(!pad.held(PadInput::left_trigger),"and the trigger is no longer held");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-12000);check(changes.empty()&&!pad.held(PadInput::left_trigger),"rising between the points is no press");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-20000);check(changes.empty(),"a released trigger reports nothing more");
        // A trigger trembling around the press point is one press, held throughout.
        for(int i=0;i<20;++i) {
            axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,i%2 ? -768 : 832);
            const int v=value(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
            check(i%2 ? v<=trigger_threshold&&v>=trigger_release_threshold : v>trigger_threshold,"jitter values straddle the press point");
            check(pad.held(PadInput::left_trigger),"a trembling trigger stays held");
            pad.snapshot(held);check(held.pad[std::size_t(PadInput::left_trigger)],"in the snapshot too");
        }
        one(PadInput::left_trigger,true,"jitter around the press point is one press");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-20000);one(PadInput::left_trigger,false,"then one release");
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,32767);one(PadInput::right_trigger,true,"right trigger press");
        check(pad.held(PadInput::right_trigger)&&!pad.held(PadInput::left_trigger),"right trigger held");
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);one(PadInput::right_trigger,false,"right trigger release");
        axis(SDL_CONTROLLER_AXIS_LEFTX,32767);axis(SDL_CONTROLLER_AXIS_RIGHTY,32767);
        check(changes.empty(),"stick motion is not a pad change");
        pad.snapshot(held);check(held.stick_x==32767&&held.stick_y==0&&held.pad.none(),"snapshot carries the left stick");
        axis(SDL_CONTROLLER_AXIS_LEFTX,0);
        button(SDL_CONTROLLER_BUTTON_A,true);one(PadInput::a,true,"button press");
        check(pad.held(PadInput::a),"button held");
        button(SDL_CONTROLLER_BUTTON_A,false);one(PadInput::a,false,"button release");
        button(SDL_CONTROLLER_BUTTON_PADDLE4,true);one(PadInput::paddle4,true,"paddle press");button(SDL_CONTROLLER_BUTTON_PADDLE4,false);changes.clear();
        SDL_Event foreign{};foreign.type=SDL_CONTROLLERBUTTONDOWN;foreign.cbutton.which=SDL_JoystickInstanceID(joystick)+1000;foreign.cbutton.button=SDL_CONTROLLER_BUTTON_A;
        check(!pad.translate(foreign),"a button of another controller is ignored");
        foreign.type=SDL_CONTROLLERAXISMOTION;foreign.caxis.which=SDL_JoystickInstanceID(joystick)+1000;foreign.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERLEFT;foreign.caxis.value=32767;
        check(!pad.translate(foreign),"a trigger of another controller is ignored");
        foreign.caxis.which=SDL_JoystickInstanceID(joystick);
        check(pad.translate(foreign)&&!pad.translate(foreign),"one press per crossing, however many events");
        foreign.caxis.value=0;check(pad.translate(foreign)&&!pad.translate(foreign),"one release per crossing");
        // Detach with the trigger held: the latch must not survive into the
        // next controller, whose first press would otherwise be lost.
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);one(PadInput::left_trigger,true,"trigger held before detach");
        SDL_JoystickClose(joystick);
        check(SDL_JoystickDetachVirtual(index)==0,"detach with the trigger held");events();
        check(!pad.connected()&&!pad.held(PadInput::left_trigger),"detached controller holds nothing");
        pad.snapshot(held);check(!held.controller&&held.pad.none()&&held.stick_x==0,"detached snapshot is empty");
        index=SDL_JoystickAttachVirtualEx(&descriptor);check(index>=0,"second trigger controller");
        joystick=SDL_JoystickOpen(index);check(joystick!=nullptr,"second trigger joystick");
        // Hot-plugged with the left trigger already held: the latch is seeded
        // from the device on opening, so it is held but reports no press.
        // (SDL 2 rests a new controller's triggers until the driver reports
        // again; without the read in refresh() this would be a press.)
        check(SDL_JoystickSetVirtualAxis(joystick,SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767)==0,"trigger held before the controller opens");SDL_JoystickUpdate();
        events();check(pad.connected(),"second controller opened");
        check(changes.empty()&&pad.held(PadInput::left_trigger)&&!pad.held(PadInput::right_trigger),"a trigger held at hot-plug is held without a press");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,20000);check(changes.empty(),"moving the held trigger is still no press");
        {   // A new Controller over an open device (the game after the start screen).
            Controller late;late.refresh();
            check(late.connected()&&late.held(PadInput::left_trigger),"a new controller sees the held trigger");
            SDL_Event motion{};motion.type=SDL_CONTROLLERAXISMOTION;motion.caxis.which=SDL_JoystickInstanceID(joystick);
            motion.caxis.axis=SDL_CONTROLLER_AXIS_TRIGGERLEFT;motion.caxis.value=30000;
            check(!late.translate(motion),"a trigger held when a controller opens is no press");
            motion.caxis.value=0;auto change=late.translate(motion);
            check(change&&change->input==PadInput::left_trigger&&!change->down&&!late.held(PadInput::left_trigger),"its release is reported");
            motion.caxis.value=30000;change=late.translate(motion);check(change&&change->down,"and the next press");
        }
        changes.clear();
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);changes.clear();
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);one(PadInput::left_trigger,true,"the latch is cleared on detach");
        // The bindings travel with the controller.
        check(pad.bindings()==Bindings::defaults(),"a controller starts with the default bindings");
        auto remapped=Bindings::defaults();assign_key(remapped,Action::button1,SDL_SCANCODE_K);pad.set_bindings(remapped);
        check(pad.bindings()==remapped,"bindings are kept by the controller");
        // The Controls screen sampler: every key while focused, Return
        // filtered as Pause is, then the controller.
        keys={};keys[SDL_SCANCODE_RETURN]=keys[SDL_SCANCODE_F12]=keys[SDL_SCANCODE_LGUI]=1;EnterKey fresh;
        auto raw=sample_controls_input(window,pad,fresh);
        check(raw.focused&&raw.keys[SDL_SCANCODE_RETURN]&&raw.keys[SDL_SCANCODE_F12]&&raw.keys[SDL_SCANCODE_LGUI]&&raw.keys.count()==3,"controls sampler reads every key");
        check(raw.controller&&raw.pad[std::size_t(PadInput::left_trigger)],"controls sampler reads the controller");
        keys[SDL_SCANCODE_RALT]=1;raw=sample_controls_input(window,pad,fresh);
        check(!raw.keys[SDL_SCANCODE_RETURN]&&raw.keys[SDL_SCANCODE_RALT],"Return is not seen while Alt is held");
        keys[SDL_SCANCODE_RALT]=0;fresh.event(true,true,false);raw=sample_controls_input(window,pad,fresh);
        check(!raw.keys[SDL_SCANCODE_RETURN],"Return consumed by Alt+Enter is not seen");
        focused=nullptr;raw=sample_controls_input(window,pad,fresh);
        check(!raw.focused&&raw.keys.none()&&raw.controller,"no keys without focus");
        // Milestone 8: remapped keys, buttons and triggers through
        // the real samplers; a remapped input no longer acts on its old action.
        focused=window;keys={};axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);changes.clear();
        auto bound=Bindings::defaults();
        check(assign_key(bound,Action::button1,SDL_SCANCODE_K).change==Change::set&&assign_pad(bound,Action::button1,PadInput::left_trigger).change==Change::set&&
              assign_pad(bound,Action::speed,PadInput::right_trigger).change==Change::set&&assign_key(bound,Action::options,SDL_SCANCODE_F1).change==Change::set,"remapped bindings");
        pad.set_bindings(bound);EnterKey idle;
        auto frame=sample_input(window,pad,idle);auto menu_now=sample_menu_input(window,pad);
        check(frame.pads[0].one&&!frame.pads[0].two&&!frame.pause&&menu_now.accept&&!menu_now.toggle,"a held trigger bound to button 1 reaches the game and the menus");
        check(!sample_start_input(window,pad,idle).accept,"a held trigger bound to button 1 does not accept on the fixed start screen");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);changes.clear();
        button(SDL_CONTROLLER_BUTTON_A,true);changes.clear();
        check(!sample_input(window,pad,idle).pads[0].one&&!sample_menu_input(window,pad).accept,"controller A no longer acts as button 1");
        check(sample_start_input(window,pad,idle).accept,"controller A still accepts on the start screen");
        button(SDL_CONTROLLER_BUTTON_A,false);changes.clear();
        keys[SDL_SCANCODE_Z]=1;check(!sample_input(window,pad,idle).pads[0].one&&!sample_menu_input(window,pad).accept&&sample_start_input(window,pad,idle).accept,"Z no longer acts as button 1 but still accepts on the start screen");
        keys[SDL_SCANCODE_Z]=0;keys[SDL_SCANCODE_K]=1;
        check(sample_input(window,pad,idle).pads[0].one&&sample_menu_input(window,pad).accept&&!sample_start_input(window,pad,idle).accept,"K is button 1 in play and menus, not on the start screen");
        keys[SDL_SCANCODE_K]=0;keys[SDL_SCANCODE_ESCAPE]=1;
        check(!sample_menu_input(window,pad).toggle&&sample_start_input(window,pad,idle).cancel,"Escape no longer opens Options but still leaves the start screen");
        keys[SDL_SCANCODE_ESCAPE]=0;keys[SDL_SCANCODE_F1]=1;
        check(sample_menu_input(window,pad).toggle&&!sample_start_input(window,pad,idle).cancel,"F1 opens Options but does not leave the start screen");
        neutral(sample_input(window,pad,idle),"the Options key does not reach the game");
        keys[SDL_SCANCODE_F1]=0;
        button(SDL_CONTROLLER_BUTTON_Y,true);changes.clear();check(sample_menu_input(window,pad).toggle,"controller Y still opens Options");
        button(SDL_CONTROLLER_BUTTON_Y,false);changes.clear();
        // Speed on the right trigger: one press per crossing for the event
        // path, the held state for SpeedShortcut::update, nothing for the game.
        SpeedShortcut speed;check(!speed.update(true,false,false,true),"speed shortcut armed");
        const auto speed_pad=*pad.bindings().pads[Action::speed];
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,32767);
        check(changes.size()==1&&changes[0].input==speed_pad&&changes[0].down&&pad.held(speed_pad),"the speed trigger reports one press");changes.clear();
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,30000);check(changes.empty(),"a held speed trigger reports nothing more");
        check(speed.update(true,false,pad.held(speed_pad),true)&&!speed.update(true,false,pad.held(speed_pad),true),"a held speed trigger is one speed press");
        neutral(sample_input(window,pad,idle),"the speed trigger does not reach the game");
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT,-32768);one(speed_pad,false,"the speed trigger reports one release");
        check(!speed.update(true,false,pad.held(speed_pad),true),"speed trigger released");
        button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,true);
        check(changes.size()==1&&changes[0].input!=speed_pad&&!speed.update(true,false,pad.held(speed_pad),true),"the right shoulder is no longer speed");
        button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,false);changes.clear();
        // Return bound to button 1 (Pause takes K by swap): Alt+Enter never
        // presses it, as it never pressed Pause.
        check(assign_key(bound,Action::button1,SDL_SCANCODE_RETURN).change==Change::swapped&&bound.keys[Action::pause]==SDL_SCANCODE_K,"Return swapped onto button 1");
        pad.set_bindings(bound);EnterKey alt_enter;
        keys[SDL_SCANCODE_RETURN]=keys[SDL_SCANCODE_LALT]=1;
        check(!sample_input(window,pad,alt_enter).pads[0].one&&!sample_menu_input(window,pad).accept&&!sample_start_input(window,pad,alt_enter).accept,"Return with Alt is not button 1");
        check(alt_enter.event(true,true,false),"Alt+Enter toggles fullscreen");keys[SDL_SCANCODE_LALT]=0;
        check(!sample_input(window,pad,alt_enter).pads[0].one&&!sample_start_input(window,pad,alt_enter).accept,"Return consumed by Alt+Enter is not button 1");
        keys[SDL_SCANCODE_RETURN]=0;alt_enter.event(false,false,false);keys[SDL_SCANCODE_RETURN]=1;
        frame=sample_input(window,pad,alt_enter);check(frame.pads[0].one&&!frame.pause,"a fresh Return is button 1, not Pause");
        keys[SDL_SCANCODE_RETURN]=0;keys[SDL_SCANCODE_K]=1;check(sample_input(window,pad,alt_enter).pause,"K is Pause after the swap");
        // The menus follow the same Return rule through the controller's own
        // EnterKey: Alt+Enter, then Alt released first, is not an accept.
        keys={};keys[SDL_SCANCODE_RETURN]=keys[SDL_SCANCODE_LALT]=1;
        check(!sample_menu_input(window,pad).accept,"Return with Alt does not accept in the menus");
        check(pad.enter().event(true,true,false),"Alt+Enter through the controller's EnterKey");keys[SDL_SCANCODE_LALT]=0;
        check(!sample_menu_input(window,pad).accept,"Return consumed by Alt+Enter does not accept in the menus after Alt is released");
        keys[SDL_SCANCODE_RETURN]=0;pad.enter().event(false,false,false);
        check(!sample_menu_input(window,pad).accept,"nothing held");
        keys[SDL_SCANCODE_RETURN]=1;pad.enter().event(true,false,false);
        check(sample_menu_input(window,pad).accept,"a fresh Return accepts in the menus");
        keys={};pad.enter().event(false,false,false);
        // Hostile bindings on the controller: no pad input bound, keys
        // swapped. The start screen still answers the fixed inputs only.
        {
            auto hostile=Bindings::defaults();
            for(std::size_t i=0;i<action_count;++i)clear_pad(hostile,Action(i));
            for(const auto a:{Action::pause,Action::reset,Action::speed,Action::options,Action::map,Action::power})clear_key(hostile,a);
            assign_key(hostile,Action::button1,SDL_SCANCODE_X);assign_key(hostile,Action::pause,SDL_SCANCODE_ESCAPE);assign_key(hostile,Action::options,SDL_SCANCODE_K);
            check(hostile.pads==PerAction<std::optional<PadInput>>{}&&hostile.keys[Action::button2]==SDL_SCANCODE_Z,"hostile controller bindings");
            pad.set_bindings(hostile);EnterKey none;
            auto start=[&]{return sample_start_input(window,pad,none);};
            for(const auto b:{SDL_CONTROLLER_BUTTON_A,SDL_CONTROLLER_BUTTON_START}) {
                button(b,true);changes.clear();check(start().accept&&!start().cancel,"A and Start accept with no pad input bound");
                button(b,false);changes.clear();check(!start().accept,"and release");
            }
            button(SDL_CONTROLLER_BUTTON_DPAD_DOWN,true);changes.clear();check(start().down&&!start().up,"the d-pad moves");
            button(SDL_CONTROLLER_BUTTON_DPAD_DOWN,false);changes.clear();
            axis(SDL_CONTROLLER_AXIS_LEFTY,-32768);check(start().up,"the left stick moves");axis(SDL_CONTROLLER_AXIS_LEFTY,0);
            for(const auto b:{SDL_CONTROLLER_BUTTON_B,SDL_CONTROLLER_BUTTON_X,SDL_CONTROLLER_BUTTON_Y,SDL_CONTROLLER_BUTTON_BACK,SDL_CONTROLLER_BUTTON_RIGHTSHOULDER}) {
                button(b,true);changes.clear();check(!start().any(),"other buttons do nothing on the start screen");
                button(b,false);changes.clear();
            }
            axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,32767);changes.clear();check(!start().any(),"nor do the triggers");
            axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT,-32768);changes.clear();
            keys[SDL_SCANCODE_Z]=1;check(start().accept,"Z accepts though it is button 2 now");keys[SDL_SCANCODE_Z]=0;
            keys[SDL_SCANCODE_X]=1;check(!start().any(),"X, now button 1, does not accept");keys[SDL_SCANCODE_X]=0;
            keys[SDL_SCANCODE_K]=1;check(!start().cancel,"K, now Options, does not quit");keys[SDL_SCANCODE_K]=0;
            keys[SDL_SCANCODE_ESCAPE]=1;check(start().cancel&&!start().accept,"Escape, now Pause, still quits and does not accept");keys[SDL_SCANCODE_ESCAPE]=0;
            keys[SDL_SCANCODE_RETURN]=1;check(start().accept,"Return accepts");keys={};
        }
        pad.set_bindings(Bindings::defaults());focused=window;keys={};
        SDL_JoystickClose(joystick);check(SDL_JoystickDetachVirtual(index)==0,"second detach");events();
        // key_change: non-repeat presses and releases of one key in this window.
        SDL_Event key_event{};key_event.type=SDL_KEYDOWN;key_event.key.windowID=7;key_event.key.keysym.scancode=SDL_SCANCODE_F5;
        check(key_change(key_event,window,SDL_SCANCODE_F5)==true&&!key_change(key_event,window,SDL_SCANCODE_TAB),"key press");
        key_event.key.repeat=1;check(!key_change(key_event,window,SDL_SCANCODE_F5),"repeat ignored");
        key_event.key.repeat=0;key_event.type=SDL_KEYUP;check(key_change(key_event,window,SDL_SCANCODE_F5)==false,"key release");
        key_event.key.windowID=8;check(!key_change(key_event,window,SDL_SCANCODE_F5),"another window ignored");
        key_event.type=SDL_CONTROLLERBUTTONDOWN;check(!key_change(key_event,window,SDL_SCANCODE_F5),"not a key event");
    }
    SDL_Quit();
    std::cout<<"SDL real sampler: shortcuts, focus/held release, virtual-controller detach, triggers and bindings passed.\n";return 0;
}catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
