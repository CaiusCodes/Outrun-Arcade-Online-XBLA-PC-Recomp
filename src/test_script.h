#pragma once
// Test driver for background renderer checks: a scripted controller on the
// keyboard slot, read inside the game, so a test copy can reach a race in a
// small unfocused window with no input sent to the desktop. Off unless
// outrun_test_script is set (never in a release config).
//
// Script: comma-separated "<seconds>:<button>" taps (150 ms),
// "<seconds>+:<button>" holds from then on and "<from>-<to>:<button>" holds
// between two times, timed from the first poll.
// Buttons: start a b x y up down left right rt lt. "<seconds>:@<x>/<y>" moves
// the menu pointer once to that point of the 1280x720 canvas, as a mouse
// hover there would (menu_pointer.cpp), without touching the desktop cursor;
// the overlays see it as the mouse (pointer::testCursor). "<seconds>:!<x>/<y>"
// clicks there. "<seconds>:mark" makes one player-log bug mark, as F8 does
// (player_log.cpp), so a background run can test it without focus.
// "<seconds>:rdc" captures the next frame with RenderDoc when it is attached
// (gpu_native_rdc_capture); "<seconds>:trace" traces the next frame into the
// log (gpu_native_trace_frame = -1; native renderer only).
//
// outrun_test_route (e.g. "RLRL") picks the forks: from 40 game seconds into
// stage k the pad nudges to that side (0.9 s in every 4.5, so the car drifts
// over without scraping the wall at the split), timed from the stage's
// checkpoint, which the countdown hold (gpu_native_test_timer) sees as the
// time going up.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#include <rex/cvar.h>
#include <rex/input/input.h>
#include "menu_pointer.h"

REXCVAR_DECLARE(std::string, outrun_test_script);
REXCVAR_DECLARE(std::string, outrun_test_route);

// From the countdown hold (native_gpu.cpp): checkpoints passed (-1 until the
// countdown is found) and game seconds since the last one (or since it was found).
extern std::atomic<int> g_test_checkpoints;
extern std::atomic<double> g_test_stage_seconds;

namespace outrun::test_script {
inline std::atomic<bool> markRequested{false};  // (read by player_log.cpp)
struct Step {
  double at;
  double until;  // end of the press; infinite for "+" holds
  std::string button;
};

inline std::vector<Step> Parse(const std::string& text) {
  std::vector<Step> steps;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t end = text.find(',', pos);
    if (end == std::string::npos) end = text.size();
    const std::string item = text.substr(pos, end - pos);
    const size_t colon = item.find(':');
    if (colon != std::string::npos) {
      std::string when = item.substr(0, colon);
      const double at = std::atof(when.c_str());
      double until = at + 0.15;
      const size_t dash = when.find('-', 1);
      if (!when.empty() && when.back() == '+') until = 1e30;
      else if (dash != std::string::npos) until = std::atof(when.c_str() + dash + 1);
      steps.push_back({at, until, item.substr(colon + 1)});
    }
    pos = end + 1;
  }
  return steps;
}

// Fills `state` from the script; false when no script is set.
inline bool Read(rex::input::X_INPUT_STATE* state) {
  static const std::string text = REXCVAR_GET(outrun_test_script);
  if (text.empty() || !state) return false;
  static const std::vector<Step> steps = Parse(text);
  static std::vector<bool> moved(steps.size());
  static const auto start = std::chrono::steady_clock::now();
  static uint32_t packet = 0;
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  *state = {};
  state->packet_number = ++packet;
  uint16_t buttons = 0;
  for (size_t i = 0; i < steps.size(); ++i) {
    const Step& s = steps[i];
    if (s.button == "mark") {
      if (!moved[i] && now >= s.at) { moved[i] = true; markRequested = true; }
      continue;
    }
    if (s.button == "rdc" || s.button == "trace") {  // one RenderDoc capture / a frame trace into the log, next frame
      if (!moved[i] && now >= s.at) {
        moved[i] = true;
        if (s.button == "rdc") rex::cvar::SetFlagByName("gpu_native_rdc_capture", "true");
        else rex::cvar::SetFlagByName("gpu_native_trace_frame", "-1");
      }
      continue;
    }
    if (!s.button.empty() && (s.button[0] == '@' || s.button[0] == '!')) {
      if (!moved[i] && now >= s.at) {
        moved[i] = true;
        const size_t slash = s.button.find('/');
        if (slash != std::string::npos) {
          const float x = float(std::atof(s.button.c_str() + 1)), y = float(std::atof(s.button.c_str() + slash + 1));
          if (s.button[0] == '!') outrun::pointer::Push({x, y, false});
          else outrun::pointer::Move(x, y);
          outrun::pointer::testCursor = {x, y};
        }
      }
      continue;
    }
    if (now < s.at || now > s.until) continue;
    const std::string& b = s.button;
    if (b == "start") buttons |= rex::input::X_INPUT_GAMEPAD_START;
    else if (b == "a") buttons |= rex::input::X_INPUT_GAMEPAD_A;
    else if (b == "b") buttons |= rex::input::X_INPUT_GAMEPAD_B;
    else if (b == "x") buttons |= rex::input::X_INPUT_GAMEPAD_X;
    else if (b == "y") buttons |= rex::input::X_INPUT_GAMEPAD_Y;
    else if (b == "up") { buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_UP; state->gamepad.thumb_ly = 32767; }
    else if (b == "down") { buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_DOWN; state->gamepad.thumb_ly = -32767; }
    else if (b == "left") { buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_LEFT; state->gamepad.thumb_lx = -32767; }
    else if (b == "right") { buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT; state->gamepad.thumb_lx = 32767; }
    else if (b == "rt") state->gamepad.right_trigger = 255;
    else if (b == "lt") state->gamepad.left_trigger = 255;
  }
  // Forks: nudge to the route's side through the second half of each stage.
  static const std::string route = REXCVAR_GET(outrun_test_route);
  const int cp = g_test_checkpoints.load(std::memory_order_relaxed);
  const double stage_s = g_test_stage_seconds.load(std::memory_order_relaxed);
  if (cp >= 0 && size_t(cp) < route.size() && stage_s >= 40.0 && std::fmod(stage_s, 4.5) < 0.9) {
    if (route[cp] == 'L' || route[cp] == 'l') {
      buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_LEFT;
      state->gamepad.thumb_lx = -32767;
    } else if (route[cp] == 'R' || route[cp] == 'r') {
      buttons |= rex::input::X_INPUT_GAMEPAD_DPAD_RIGHT;
      state->gamepad.thumb_lx = 32767;
    }
  }
  state->gamepad.buttons = buttons;
  return true;
}
}  // namespace outrun::test_script
