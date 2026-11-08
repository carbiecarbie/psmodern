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
    std::cout<<"SDL shortcut consumption passed.\n";return 0;
}catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
