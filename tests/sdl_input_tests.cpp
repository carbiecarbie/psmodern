// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/sdl_host.h"
#include <iostream>
#include <stdexcept>

namespace {
std::array<Uint8,SDL_NUM_SCANCODES> keys{};
SDL_Window* focused=nullptr;
const Uint8* snapshot(int*) {return keys.data();}
SDL_Window* focus() {return focused;}
void pump() {}
void check(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
void neutral(const psmodern::FrameInput& input,const char* message) {
    check(input.pads==std::array<psmodern::Pad,2>{}&&!input.pause&&!input.reset,message);
}
}
// Substitute only SDL's physical keyboard snapshot, exercising the real sampler.
#define SDL_GetKeyboardState snapshot
#define SDL_GetKeyboardFocus focus
#define SDL_PumpEvents pump
#include "../src/app/sdl_host.cpp"
#undef SDL_GetKeyboardState
#undef SDL_GetKeyboardFocus
#undef SDL_PumpEvents

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
    check(menu.update(sample_menu_input(window,controller),now++)==MenuAction::close,"fresh keyboard X closes after focus regain");
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

    // Use actual SDL virtual-controller state, mapping and added/removed
    // events rather than scripting sample_menu_input or Controller::sample.
    // Keep a player's attached hardware and real window focus outside this
    // test: only this process's virtual VID/PID may be selected by refresh.
    check(SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,"0xffff/0x5053",SDL_HINT_OVERRIDE)==SDL_TRUE,"isolated virtual-controller filter");
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1",SDL_HINT_OVERRIDE);
    check(SDL_Init(SDL_INIT_GAMECONTROLLER)==0,"controller subsystem");
    {
        SDL_VirtualJoystickDesc descriptor{};descriptor.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        descriptor.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;descriptor.naxes=2;descriptor.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
        descriptor.axis_mask=(1u<<SDL_CONTROLLER_AXIS_LEFTX)|(1u<<SDL_CONTROLLER_AXIS_LEFTY);
        descriptor.vendor_id=0xffff;descriptor.product_id=0x5053;
        descriptor.button_mask=(1u<<SDL_CONTROLLER_BUTTON_MAX)-1;descriptor.name="PSModern input test controller";
        const auto index=SDL_JoystickAttachVirtualEx(&descriptor);check(index>=0,"virtual controller attachment");
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
        check(real_controller.fast_forward(),"right shoulder reaches speed sampler");
        neutral(sample_input(window,real_controller,enter),"right shoulder does not leak to gameplay");
        button(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,false);
        check(!real_controller.fast_forward(),"right shoulder release reaches speed sampler");
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
        check(menu.update(sample_menu_input(window,real_controller),now++)==MenuAction::close,"keyboard closes modal after controller removal");
        menu.close();release.consume();input=sample_menu_input(window,real_controller);
        neutral(release.filter(sample_input(window,real_controller,enter),input.any(),input.focused),"keyboard close does not leak after detach");
        keys={};input=sample_menu_input(window,real_controller);release.filter(sample_input(window,real_controller,enter),input.any(),input.focused);
        keys[SDL_SCANCODE_UP]=keys[SDL_SCANCODE_Z]=1;game=release.filter(sample_input(window,real_controller,enter));
        check(game.pads[0].up&&game.pads[0].one&&!game.pause&&!game.reset,"gameplay recovers without stuck inputs after detach and close");
        SDL_JoystickClose(joystick);
    }
    SDL_Quit();
    std::cout<<"SDL real sampler: shortcuts, focus/held release and virtual-controller detach passed.\n";return 0;
}catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
