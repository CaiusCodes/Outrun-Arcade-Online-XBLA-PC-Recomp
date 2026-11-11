#pragma once
#include <chrono>
#include <rex/cvar.h>
#include <windows.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include "button_pulse.h"
#include "keyboard_context.h"
#include "menu_pointer.h"
#include "menu_pointer_geometry.h"
#include "audio_diagnostics.h"
#include "audio_pcm_capture.h"
#include "test_script.h"

REXCVAR_DECLARE(int32_t, mnk_user_index);

// Preserve button taps that begin and end between guest input polls. Continuous
// steering and triggers still use the SDK's live state without added latency.
namespace rex::input {
class OutrunKeyboardInput : public InputDriver,
                            public ui::WindowInputListener,
                            public ui::WindowListener {
 public:
  OutrunKeyboardInput() : InputDriver(nullptr, 0), keyboard_(nullptr, 1) {
    keyboard_.set_is_active_callback([this] { return is_active(); });
  }
  ~OutrunKeyboardInput() override { Detach(); }
  X_STATUS Setup() override { return keyboard_.Setup(); }
  X_RESULT GetCapabilities(uint32_t user, uint32_t flags, X_INPUT_CAPABILITIES* caps) override {
    return keyboard_.GetCapabilities(user, flags, caps);
  }
  X_RESULT SetState(uint32_t user, X_INPUT_VIBRATION* vibration) override {
    return keyboard_.SetState(user, vibration);
  }
  X_RESULT GetKeystroke(uint32_t user, uint32_t flags, X_INPUT_KEYSTROKE* stroke) override {
    return keyboard_.GetKeystroke(user, flags, stroke);
  }
  void OnWindowAvailable(ui::Window* window) override {
    // Forward non-reserved keys ourselves, so the context keys cannot also
    // generate the SDK's saved binding (for example Enter=Start).
    attached_ = window;
    if (window) {
      window->AddInputListener(this, 0); // Forward keys after higher-priority UI listeners.
      window->AddListener(this);
    }
  }

  void OnKeyDown(rex::ui::KeyEvent& event) override {
    const auto key=static_cast<uint16_t>(event.virtual_key());
    if(is_active())outrun::diagnostics::Key(key,true,event.prev_state());
    if(is_active() && !event.prev_state())outrun::diagnostics::ArmPCM(key,outrun::keyboard::menuActive.load(),outrun::pointer::titleActive.load());
    if(is_active() && outrun::pointer::titleActive.load() && !outrun::pointer::controllerConnected.load() && !event.is_alt_pressed() && !event.is_ctrl_pressed() && !event.is_super_pressed()) {
      // Ignore modifiers and OS/window shortcuts. Ordinary keys start the title.
      if(key>=0x08 && key!=0x10 && key!=0x11 && key!=0x12 && key!=0x5B && key!=0x5C && key!=0xA0 && key!=0xA1 && key!=0xA2 && key!=0xA3 && key!=0xA4 && key!=0xA5) pulse_.Press(0x0010,Now());
      return;
    }
    if (outrun::keyboard::ContextKeys::Index(key)>=0) {
      if(is_active())contextKeys_.Down(key,outrun::keyboard::menuActive.load(),Now());
      return;
    }
    keyboard_.OnKeyDown(event);
    rex::input::X_INPUT_STATE state{};
    if (keyboard_.GetState(REXCVAR_GET(mnk_user_index), &state) == X_ERROR_SUCCESS && is_active()) {
      pulse_.Press(static_cast<uint16_t>(state.gamepad.buttons), Now());
    }
  }
  void OnKeyUp(rex::ui::KeyEvent& event) override {
    const auto key=static_cast<uint16_t>(event.virtual_key());
    if(is_active())outrun::diagnostics::ArmPCMRelease(key,outrun::keyboard::menuActive.load(),outrun::pointer::titleActive.load());
    if(is_active())outrun::diagnostics::Key(key,false,false);
    if(outrun::keyboard::ContextKeys::Index(key)>=0)contextKeys_.Up(key);
    else keyboard_.OnKeyUp(event);
  }
  void OnGotFocus(rex::ui::UISetupEvent& event) override { keyboard_.OnGotFocus(event); }
  void OnMouseDown(rex::ui::MouseEvent& event) override {
    if(is_active() && (outrun::keyboard::menuActive.load() || outrun::pointer::titleActive.load())) {
      using Button=rex::ui::MouseEvent::Button;
      if(event.button()==Button::kLeft || event.button()==Button::kRight) {
        if(outrun::pointer::titleActive.load()){if(event.button()==Button::kLeft)pulse_.Press(0x0010,Now());}
        else if(attached_) {
          if(auto point=MouseCanvas(event))outrun::pointer::Push({point->x,point->y,event.button()==Button::kRight});
        }
        event.set_handled(true);return;
      }
    }
    keyboard_.OnMouseDown(event);
  }
  void OnMouseUp(rex::ui::MouseEvent& event) override { keyboard_.OnMouseUp(event); }
  void OnMouseMove(rex::ui::MouseEvent& event) override {
    if(is_active() && outrun::keyboard::menuActive.load() && attached_) {
      if(auto point=MouseCanvas(event))outrun::pointer::Move(point->x,point->y);
      else outrun::pointer::Move(-1,-1); // Leaving the canvas cancels hover navigation.
      event.set_handled(true);return;
    }
    keyboard_.OnMouseMove(event);
  }
  void OnMouseWheel(rex::ui::MouseEvent& event) override {
    if(is_active() && outrun::keyboard::menuActive.load()) {
      wheelRemainder_+=event.scroll_y();
      const int steps=wheelRemainder_/int(rex::ui::MouseEvent::kScrollPerDetent);
      wheelRemainder_%=int(rex::ui::MouseEvent::kScrollPerDetent);
      outrun::pointer::Scroll(-steps); // Away = previous; towards = next.
      event.set_handled(true);return;
    }
    wheelRemainder_=0;
    keyboard_.OnMouseWheel(event);
  }

  X_RESULT GetState(uint32_t user, rex::input::X_INPUT_STATE* state) override {
    // Background renderer tests (outrun_test_script): a scripted pad on this
    // slot, read whether or not the window has focus.
    if (user == static_cast<uint32_t>(REXCVAR_GET(mnk_user_index)) && outrun::test_script::Read(state)) {
      if (outrun::pointer::inputHeld) state->gamepad = {};
      return X_ERROR_SUCCESS;
    }
    // This wrapper owns the host window. Forward SDK keyboard/mouse state
    // without its automatic cursor warp, which prevents absolute UI clicks.
    auto result = keyboard_.GetState(user, state);
    if (user == static_cast<uint32_t>(REXCVAR_GET(mnk_user_index))) {
      if (result == X_ERROR_SUCCESS && state && is_active()) {
        state->gamepad.buttons = static_cast<uint16_t>(state->gamepad.buttons) | pulse_.Read(Now()) | contextKeys_.Read(Now());
        // These menus read stick directions rather than the D-pad. Mirror arrow
        // button state onto otherwise idle keyboard axes, including short taps.
        const uint16_t buttons = state->gamepad.buttons;
        if (state->gamepad.thumb_lx == 0) {
          state->gamepad.thumb_lx =
              ((buttons & X_INPUT_GAMEPAD_DPAD_RIGHT) ? 32767 : 0) -
              ((buttons & X_INPUT_GAMEPAD_DPAD_LEFT) ? 32767 : 0);
        }
        if (state->gamepad.thumb_ly == 0) {
          state->gamepad.thumb_ly =
              ((buttons & X_INPUT_GAMEPAD_DPAD_UP) ? 32767 : 0) -
              ((buttons & X_INPUT_GAMEPAD_DPAD_DOWN) ? 32767 : 0);
        }
        if (outrun::pointer::inputHeld) { state->gamepad = {}; pulse_.Clear(); contextKeys_.Clear(); }
        outrun::diagnostics::Input("keyboard",user,*state);
      } else {
        pulse_.Clear();
        contextKeys_.Clear();
      }
    }
    return result;
  }

  void OnLostFocus(rex::ui::UISetupEvent& event) override {
    keyboard_.OnLostFocus(event);
    pulse_.Clear();
    contextKeys_.Clear();
    outrun::pointer::Clear();
    wheelRemainder_=0;
  }
  void OnClosing(ui::UIEvent&) override { Detach(); }

 private:
  std::optional<outrun::pointer::Point> MouseCanvas(const rex::ui::MouseEvent& event) const {
    // SDL relative-mode coordinates may stay centred. Read the actual client
    // cursor for both click and hover, then share the DPI/letterbox transform.
    POINT point{};int x=event.x(),y=event.y();
    const auto handle=static_cast<HWND>(attached_->GetNativeWindowHandle());
    if(handle && GetCursorPos(&point) && ScreenToClient(handle,&point)){x=point.x;y=point.y;}
    RECT client{};float w=float(attached_->GetActualPhysicalWidth()),h=float(attached_->GetActualPhysicalHeight());
    if(handle && GetClientRect(handle,&client)){w=float(client.right);h=float(client.bottom);}
    return outrun::pointer::ToCanvas(float(x),float(y),w,h,outrun::pointer::fourByThree.load());
  }
  ButtonPulse pulse_;
  outrun::keyboard::ContextKeys contextKeys_;
  static int64_t Now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
  }
  mnk::MnkInputDriver keyboard_;
  ui::Window* attached_ = nullptr;
  int wheelRemainder_=0;
  void Detach() {
    ui::UIEvent close(attached_);keyboard_.OnClosing(close);
    if (attached_) {
      attached_->RemoveInputListener(this);
      attached_->RemoveListener(this);
      attached_ = nullptr;
    }
  }
};
} // namespace rex::input
