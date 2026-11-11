#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/input_system.h>
#include <iostream>
#include <cstdlib>
#include "keyboard_input.h"

REXCVAR_DECLARE(bool, mnk_mode);
REXCVAR_DECLARE(std::string, keybind_right_trigger);
REXCVAR_DECLARE(std::string, keybind_left_trigger);
REXCVAR_DECLARE(std::string, keybind_lstick_left);
REXCVAR_DECLARE(std::string, keybind_lstick_right);
REXCVAR_DECLARE(std::string, keybind_dpad_right);
REXCVAR_DECLARE(std::string, keybind_a);
REXCVAR_DECLARE(std::string, keybind_b);
REXCVAR_DECLARE(std::string, keybind_x);
REXCVAR_DECLARE(std::string, keybind_y);
REXCVAR_DECLARE(std::string, keybind_left_shoulder);
REXCVAR_DECLARE(std::string, keybind_right_shoulder);
REXCVAR_DECLARE(std::string, keybind_lstick_up);
REXCVAR_DECLARE(std::string, keybind_lstick_down);
REXCVAR_DECLARE(std::string, keybind_lstick_press);
REXCVAR_DECLARE(std::string, keybind_rstick_press);

// Reproduce a connected, idle controller sharing slot 0 with the keyboard.
class IdlePad : public rex::input::InputDriver {
 public:
  IdlePad(uint16_t buttons=0) : InputDriver(nullptr, 0), buttons_(buttons) {}
  rex::X_STATUS Setup() override { return 0; }
  rex::X_RESULT GetCapabilities(uint32_t, uint32_t, rex::input::X_INPUT_CAPABILITIES*) override { return 0; }
  rex::X_RESULT GetState(uint32_t, rex::input::X_INPUT_STATE* state) override { *state = {}; state->gamepad.buttons=buttons_; return 0; }
  rex::X_RESULT SetState(uint32_t, rex::input::X_INPUT_VIBRATION*) override { return 0; }
  rex::X_RESULT GetKeystroke(uint32_t, uint32_t, rex::input::X_INPUT_KEYSTROKE*) override { return 0; }
 private:
  uint16_t buttons_;
};

int main() {
  using namespace rex::input;
  using namespace rex::ui;
  REXCVAR_GET(mnk_mode) = true;
  std::cout << "Default D-pad right=" << REXCVAR_GET(keybind_dpad_right) << '\n';
  mnk::MnkInputDriver driver(nullptr, 0);
  UISetupEvent focus;
  driver.OnGotFocus(focus);
  KeyEvent w(nullptr, VirtualKey::kW, 1, false, false, false, false, false);
  REXCVAR_GET(keybind_right_trigger) = "W,Up";
  driver.OnKeyDown(w);
  X_INPUT_STATE state{};
  driver.GetState(0, &state);
  std::cout << "Multiple-key binding RT=" << unsigned(state.gamepad.right_trigger) << '\n';
  REXCVAR_GET(keybind_right_trigger) = "W";
  driver.GetState(0, &state);
  if(state.gamepad.right_trigger != 255) return 1;
  driver.GetState(0, &state); // OutRun's second query in the same frame.
  if(state.gamepad.right_trigger != 255) return 2;
  driver.OnKeyUp(w);
  driver.GetState(0, &state);
  if(state.gamepad.right_trigger != 0) return 3;
  REXCVAR_GET(keybind_left_trigger) = "S";
  KeyEvent s(nullptr, VirtualKey::kS, 1, false, false, false, false, false);
  driver.OnKeyDown(s);
  driver.GetState(0, &state);
  if(state.gamepad.left_trigger != 255) return 4;
  driver.OnKeyUp(s);
  REXCVAR_GET(keybind_lstick_left) = "A";
  REXCVAR_GET(keybind_lstick_right) = "D";
  KeyEvent a(nullptr, VirtualKey::kA, 1, false, false, false, false, false);
  KeyEvent d(nullptr, VirtualKey::kD, 1, false, false, false, false, false);
  driver.OnKeyDown(a);
  driver.GetState(0, &state);
  if(state.gamepad.thumb_lx >= 0) return 5;
  driver.OnKeyUp(a);
  driver.OnKeyDown(d);
  driver.GetState(0, &state);
  if(state.gamepad.thumb_lx <= 0) return 6;
  driver.OnLostFocus(focus);
  driver.GetState(0, &state);
  if(state.gamepad.thumb_lx != 0 || state.gamepad.left_trigger != 0) return 7;
  std::cout << "PASS: held throttle, repeated queries, release, brake, steering, focus reset\n";
  InputSystem input(nullptr);
  input.AddDriver(std::make_unique<IdlePad>());
  auto keyboard = std::make_unique<mnk::MnkInputDriver>(nullptr, 0);
  keyboard->OnGotFocus(focus);
  keyboard->OnKeyDown(w);
  keyboard->OnKeyDown(d);
  input.AddDriver(std::move(keyboard));
  input.GetState(0, &state);
  std::cout << "Combined idle pad + keyboard: RT=" << unsigned(state.gamepad.right_trigger)
            << " LX=" << int16_t(state.gamepad.thumb_lx) << '\n';
  if(state.gamepad.right_trigger != 255 || state.gamepad.thumb_lx <= 0) return 8;
  driver.OnGotFocus(focus);
  KeyEvent right(nullptr, VirtualKey::kRight, 1, false, false, false, false, false);
  driver.OnKeyDown(right);
  driver.GetState(0, &state);
  std::cout << "Right arrow buttons=" << uint16_t(state.gamepad.buttons)
            << " LX=" << int16_t(state.gamepad.thumb_lx) << '\n';
  if(!(uint16_t(state.gamepad.buttons) & X_INPUT_GAMEPAD_DPAD_RIGHT)) return 9;

  // Exercise OutRun's actual wrapper against the shipped runtime, including
  // legacy Return=Start. Reserved keys must produce exactly one game button.
  REXCVAR_GET(mnk_user_index)=0;
  OutrunKeyboardInput contextual;
  contextual.OnGotFocus(focus);
  auto checkKey=[&](VirtualKey key,bool menu,uint16_t expected) {
    contextual.OnLostFocus(focus); contextual.OnGotFocus(focus);
    outrun::keyboard::menuActive=menu;
    KeyEvent event(nullptr,key,1,false,false,false,false,false);
    contextual.OnKeyDown(event);
    contextual.GetState(0,&state);
    if(uint16_t(state.gamepad.buttons)!=expected)return false;
    contextual.GetState(0,&state);
    if(uint16_t(state.gamepad.buttons)!=expected)return false;
    // Changing context while held (including OS repeat) cannot rebind the key.
    outrun::keyboard::menuActive=!menu;
    contextual.OnKeyDown(event);
    contextual.GetState(0,&state);
    if(uint16_t(state.gamepad.buttons)!=expected)return false;
    contextual.OnKeyUp(event);
    return true;
  };
  for(auto key:{VirtualKey::kReturn,VirtualKey::kSpace})
    if(!checkKey(key,true,X_INPUT_GAMEPAD_A))return 10;
  for(auto key:{VirtualKey::kEscape,VirtualKey::kBack})
    if(!checkKey(key,true,X_INPUT_GAMEPAD_B))return 11;
  if(!checkKey(VirtualKey::kEscape,false,X_INPUT_GAMEPAD_START))return 12;
  if(!checkKey(VirtualKey::kReturn,false,X_INPUT_GAMEPAD_START))return 13;
  contextual.OnLostFocus(focus); contextual.GetState(0,&state);
  if(state.gamepad.buttons)return 14;

  outrun::keyboard::ContextKeys taps;
  taps.Down(0x1B,false,100); taps.Up(0x1B);
  if(taps.Read(110)!=X_INPUT_GAMEPAD_START || taps.Read(151)!=0)return 15;
  taps.Down(0x0D,true,200); taps.Down(0x20,true,200); taps.Up(0x0D);
  if(taps.Read(251)!=X_INPUT_GAMEPAD_A)return 16;
  taps.Up(0x20); if(taps.Read(251))return 17;
  taps.Down(0x08,true,300); taps.Clear(); if(taps.Read(301))return 18;
  InputSystem mixed(nullptr);
  mixed.AddDriver(std::make_unique<IdlePad>(X_INPUT_GAMEPAD_B));
  auto aliases=std::make_unique<OutrunKeyboardInput>();
  aliases->OnGotFocus(focus);
  outrun::keyboard::menuActive=true;
  KeyEvent enter(nullptr,VirtualKey::kReturn,1,false,false,false,false,false);
  aliases->OnKeyDown(enter); aliases->OnKeyDown(w);
  mixed.AddDriver(std::move(aliases)); mixed.GetState(0,&state);
  if(uint16_t(state.gamepad.buttons)!=(X_INPUT_GAMEPAD_A|X_INPUT_GAMEPAD_B) ||
     state.gamepad.right_trigger!=255)return 19;
  OutrunKeyboardInput title;
  title.OnGotFocus(focus);outrun::pointer::titleActive=true;outrun::pointer::controllerConnected=false;
  KeyEvent g(nullptr,VirtualKey::kG,1,false,false,false,false,false);
  title.OnKeyDown(g);title.GetState(0,&state);
  if(!(uint16_t(state.gamepad.buttons)&X_INPUT_GAMEPAD_START))return 20;
  title.OnLostFocus(focus);title.OnGotFocus(focus);
  KeyEvent shortcut(nullptr,VirtualKey::kG,1,false,false,true,false,false);
  title.OnKeyDown(shortcut);title.GetState(0,&state);
  if(uint16_t(state.gamepad.buttons)&X_INPUT_GAMEPAD_START)return 21;
  title.OnLostFocus(focus);title.OnGotFocus(focus);outrun::pointer::controllerConnected=true;
  title.OnKeyDown(g);title.GetState(0,&state);
  if(uint16_t(state.gamepad.buttons)&X_INPUT_GAMEPAD_START)return 22;
  outrun::pointer::titleActive=false;outrun::pointer::controllerConnected=false;
  REXCVAR_GET(keybind_a)="Space";REXCVAR_GET(keybind_b)="Backspace";
  REXCVAR_GET(keybind_x)="Q";REXCVAR_GET(keybind_y)="E";
  REXCVAR_GET(keybind_left_shoulder)="Z";REXCVAR_GET(keybind_right_shoulder)="C";
  REXCVAR_GET(keybind_lstick_up)="";REXCVAR_GET(keybind_lstick_down)="";
  REXCVAR_GET(keybind_lstick_press)="";REXCVAR_GET(keybind_rstick_press)="";
  OutrunKeyboardInput driving;driving.OnGotFocus(focus);
  outrun::keyboard::menuActive=false;driving.OnKeyDown(w);
  for(int i=0;i<20;++i) {
    for(auto* key:{&a,&d}) {
      driving.OnKeyDown(*key);driving.GetState(0,&state);
      if(state.gamepad.buttons || state.gamepad.right_trigger!=255 || state.gamepad.left_trigger)return 23;
      driving.OnKeyUp(*key);driving.GetState(0,&state);
      if(state.gamepad.buttons || state.gamepad.right_trigger!=255 || state.gamepad.left_trigger || state.gamepad.thumb_lx)return 24;
    }
  }
  MouseEvent wheel(nullptr,MouseEvent::Button::kNone,0,0,0,-120);
  outrun::keyboard::menuActive=true;driving.OnMouseWheel(wheel);
  if(!wheel.is_handled() || outrun::pointer::wheel.size()!=1 || outrun::pointer::wheel.front()!=1)return 25;
  driving.OnLostFocus(focus);if(!outrun::pointer::wheel.empty())return 26;
  driving.OnGotFocus(focus);
  MouseEvent partial(nullptr,MouseEvent::Button::kNone,0,0,0,60);
  driving.OnMouseWheel(partial);if(!outrun::pointer::wheel.empty())return 27;
  driving.OnMouseWheel(partial);if(outrun::pointer::wheel.size()!=1 || outrun::pointer::wheel.front()!=-1)return 28;
  driving.OnLostFocus(focus);driving.OnGotFocus(focus);outrun::keyboard::menuActive=false;
  MouseEvent raceWheel(nullptr,MouseEvent::Button::kNone,0,0,0,-120);
  driving.OnMouseWheel(raceWheel);if(raceWheel.is_handled() || !outrun::pointer::wheel.empty())return 29;
  std::cout<<"PASS: W+A/D repeated presses preserve throttle, no brake/gear buttons; menu wheel, partial detents, focus reset and race forwarding\n";
  std::cout << "PASS: contextual menu A/B, race/title Start, held transitions,\n"
               "repeat suppression, short taps, overlapping keys and focus reset\n";
}
