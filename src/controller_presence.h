#pragma once
#include <rex/input/sdl/sdl_input_driver.h>
#include "menu_pointer.h"
#include "audio_diagnostics.h"
namespace rex::input {
class OutrunControllerInput:public InputDriver {
 public:
  OutrunControllerInput():InputDriver(nullptr,0),pad_(nullptr,0){pad_.set_is_active_callback([this]{return is_active();});}
  X_STATUS Setup() override{return pad_.Setup();}
  X_RESULT GetState(uint32_t user,X_INPUT_STATE* state)override{auto result=pad_.GetState(user,state);if(user<4){const auto bit=1u<<user;if(result==X_ERROR_SUCCESS){connected_.fetch_or(bit);if(state && outrun::pointer::inputHeld)state->gamepad={};if(state)outrun::diagnostics::Input("controller",user,*state);}else connected_.fetch_and(~bit);outrun::pointer::controllerConnected=connected_.load()!=0;}return result;}
  X_RESULT GetCapabilities(uint32_t u,uint32_t f,X_INPUT_CAPABILITIES* c)override{return pad_.GetCapabilities(u,f,c);}
  X_RESULT SetState(uint32_t u,X_INPUT_VIBRATION* v)override{return pad_.SetState(u,v);}
  X_RESULT GetKeystroke(uint32_t u,uint32_t f,X_INPUT_KEYSTROKE* k)override{return pad_.GetKeystroke(u,f,k);}
  void OnWindowAvailable(ui::Window* w)override{pad_.OnWindowAvailable(w);}
 private:sdl::SDLInputDriver pad_;std::atomic<uint32_t> connected_{0};
};
}
