// No window or OS input injection: virtual joysticks exist only in this process.
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/cvar.h>
#include <chrono>
#include <iostream>
#include <stdexcept>

namespace rex::input::sdl {
struct SDLInputDriverTestAccess {
  static void Start(SDLInputDriver& d) {
    d.sdl_events_initialized_ = d.SDL_Gamepad_initialized_ = true;
    SDL_AddEventWatch(&SDLInputDriver::WatchEvent,&d);
    d.event_watch_registered_ = true;
    d.StartJoystickUpdates();
  }
  static void StaleEvent(SDLInputDriver& d, SDL_JoystickID id) {
    SDL_Event event{};event.type=SDL_EVENT_GAMEPAD_AXIS_MOTION;
    event.gaxis.which=id;event.gaxis.axis=SDL_GAMEPAD_AXIS_LEFTX;
    auto lock=d.DrainAndLock(); d.ProcessEventLocked(event);
  }
  static void Stop(SDLInputDriver& d) { d.StopJoystickUpdates(); }
};
}
using rex::input::sdl::SDLInputDriver;
using rex::input::sdl::SDLInputDriverTestAccess;
using rex::X_STATUS;
using rex::X_RESULT;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_GAMEPAD_A;
static void Check(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
struct Updates { std::thread::id main; std::atomic<unsigned> worker{0}; };
static void SDLCALL Update(void* pointer) {
  auto& data=*static_cast<Updates*>(pointer);
  if(std::this_thread::get_id()!=data.main) ++data.worker;
}
template<class Predicate> void Eventually(Predicate predicate,const char* error) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  do { SDL_PumpEvents();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds(4)); }
  while(std::chrono::steady_clock::now()<end);
  throw std::runtime_error(error);
}
int main() {
  // Keep physical controllers out of this isolated fixture.
  for(const auto* hint : {SDL_HINT_JOYSTICK_HIDAPI, SDL_HINT_JOYSTICK_RAWINPUT,
                         SDL_HINT_JOYSTICK_DIRECTINPUT, SDL_HINT_JOYSTICK_WGI,
                         "SDL_XINPUT_ENABLED", "SDL_JOYSTICK_GAMEINPUT"}) SDL_SetHint(hint,"0");
  SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS,"0");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
  Check(SDL_InitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_EVENTS),"SDL initialization failed");
  Updates updates{std::this_thread::get_id()};
  SDL_JoystickID previous=0;
  for(int cycle=0;cycle<3;++cycle) {
    SDLInputDriver driver(nullptr,0);
    Check(driver.Setup()==X_STATUS_SUCCESS,"Driver setup failed");
    SDLInputDriverTestAccess::Start(driver);
    SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=6;desc.nbuttons=15;
    desc.axis_mask=0x3f;desc.button_mask=0x7fff;desc.name="OutRun isolated controller test";
    desc.userdata=&updates;desc.Update=&Update;
    const auto id=SDL_AttachVirtualJoystick(&desc);
    Check(id!=0 && id!=previous,"Reconnect did not obtain a fresh instance ID");
    auto* joystick=SDL_OpenJoystick(id);Check(joystick!=nullptr,"Virtual joystick open failed");
    X_INPUT_STATE state{};
    Eventually([&]{return driver.GetState(0,&state)==X_ERROR_SUCCESS;},"Hotplug was not detected");
    Check(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_LEFTX,12345),"Set steering failed");
    Check(SDL_SetJoystickVirtualAxis(joystick,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,32767),"Set trigger failed");
    Check(SDL_SetJoystickVirtualButton(joystick,SDL_GAMEPAD_BUTTON_SOUTH,true),"Set button failed");
    Eventually([&]{driver.GetState(0,&state);return state.gamepad.thumb_lx==12345 &&
      state.gamepad.right_trigger==255 && (state.gamepad.buttons&X_INPUT_GAMEPAD_A);},"Input mapping/update failed");
    const auto packet=state.packet_number;
    driver.set_is_active_callback([]{return false;});
    Check(driver.GetState(0,&state)==X_ERROR_SUCCESS && state.gamepad.buttons==0 &&
          state.gamepad.thumb_lx==0 && state.gamepad.right_trigger==0,"Inactive input was not neutral");
    driver.set_is_active_callback([]{return true;});
    Check(driver.GetState(0,&state)==X_ERROR_SUCCESS && state.gamepad.thumb_lx==12345 &&
          state.packet_number>packet,"Reactivation lost held input");
    SDL_CloseJoystick(joystick);
    Check(SDL_DetachVirtualJoystick(id),"Detach failed");
    Eventually([&]{return driver.GetState(0,&state)==X_ERROR_DEVICE_NOT_CONNECTED;},"Disconnect was not detected");
    SDLInputDriverTestAccess::StaleEvent(driver,id);
    const auto before=std::chrono::steady_clock::now();
    SDLInputDriverTestAccess::Stop(driver);
    Check(std::chrono::steady_clock::now()-before<std::chrono::seconds(1),"Worker did not stop promptly");
    previous=id;
  }
  Check(updates.worker>0,"Virtual controller was never updated on the worker");
  Check(rex::input::sdl::ConnectedGamepadCount()==0,"Controller count leaked after detach");
  SDL_QuitSubSystem(SDL_INIT_GAMEPAD|SDL_INIT_EVENTS);
  std::cout<<"PASS: worker polling, steering/trigger/button mapping, inactive state, reconnect, stale events and shutdown\n";
}
