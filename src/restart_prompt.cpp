// After Accept changes a setting that only applies at the next launch
// (Render Scale, Aspect Ratio, Graphics API), the player is asked whether to
// restart now. The prompt is drawn like the game's own option popup: white
// frame, blue striped panel, capsule rows with the orange highlight, heavy
// italic lettering and the footer's key caps or pad buttons. While it shows,
// the menus get no buttons, clicks or hovers (pointer::inputHeld), and it is
// held until the answering press is released, so it never reaches the menu
// underneath. Restart Now starts the same executable again (same command
// line, folder and window settings) and closes the window as the close button
// does. The runtime ends the process hard once the window closes (no later
// hook runs), so the new copy is started first and told, through
// OUTRUN_RESTART_WAIT_PID, to wait for this one to exit before it begins.
#include "restart_prompt.h"
#include "keyboard_prompt.h"
#include "menu_pointer.h"
#include "menu_footer.h"
#include "prompt_bar.h"
#include "test_script.h"
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
// After the SDK's input header: Xinput.h's macros reuse its enum names.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <Xinput.h>

namespace outrun::restart_prompt {
namespace {
using outrun::prompt_bar::Bar;
std::atomic<bool> pending{false};
std::atomic<bool> restart{false};
constexpr wchar_t kWaitVariable[] = L"OUTRUN_RESTART_WAIT_PID";

bool Relaunch() {
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size())));
  std::wstring command = GetCommandLineW();
  STARTUPINFOW startup{};
  GetStartupInfoW(&startup);  // the same window show settings as this launch
  startup.cb = sizeof(startup);
  startup.lpReserved = nullptr;
  startup.cbReserved2 = 0;
  startup.lpReserved2 = nullptr;
  startup.dwFlags &= ~DWORD(STARTF_USESTDHANDLES);
  PROCESS_INFORMATION process{};
  // The new copy inherits this, waits for this process to end, then clears it.
  SetEnvironmentVariableW(kWaitVariable, std::to_wstring(GetCurrentProcessId()).c_str());
  const auto folder = std::filesystem::path(exe).parent_path();
  const bool started = CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder.c_str(),
                                     &startup, &process);
  const DWORD error = GetLastError();
  SetEnvironmentVariableW(kWaitVariable, nullptr);
  if (started) {
    REXLOG_INFO("Restart prompt: started the game again (pid {})", process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
  }
  REXLOG_ERROR("Restart prompt: could not start the game again (error {})", error);
  return false;
}

// A copy started by Restart Now waits (at most 20 s) for the old one to exit
// before anything else runs: window, audio device, settings and log files.
[[maybe_unused]] const bool waitedForPrevious = [] {
  wchar_t text[32] = {};
  if (!GetEnvironmentVariableW(kWaitVariable, text, 32)) return false;
  SetEnvironmentVariableW(kWaitVariable, nullptr);
  if (HANDLE previous = OpenProcess(SYNCHRONIZE, FALSE, DWORD(std::wcstoul(text, nullptr, 10)))) {
    WaitForSingleObject(previous, 20000);
    CloseHandle(previous);
  }
  return true;
}();

constexpr const char* kRows[] = {"Restart Now", "Later"};
constexpr float kPanelW = 660, kPanelH = 270, kRowH = 56, kRowGap = 14;

class Prompt final : public rex::ui::ImGuiDialog {
 public:
  Prompt(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window) : ImGuiDialog(drawer), window_(window) {}
  bool WantsContinuousRedraw() const override { return open_ || pending.load(); }

 private:
  rex::ui::Window* window_;
  bool open_ = false, closing_ = false, waitRelease_ = true;
  int choice_ = 0;
  WORD previous_ = 0;
  ImVec2 lastMouse_{-1, -1};
  std::chrono::steady_clock::time_point heldSince_{}, lastRepeat_{};

  void Decide(int row) {
    REXLOG_INFO("Restart prompt: {}", row == 0 ? "restart now" : "later");
    closing_ = true;
    if (row != 0) return;
    restart = true;
    if (!Relaunch()) {  // stay, rather than close with nothing to come back
      restart = false;
      return;
    }
    if (window_) {
      auto* window = window_;
      window_->app_context().CallInUIThreadDeferred([window] { window->RequestClose(); });
    }
  }

  void OnDraw(ImGuiIO& io) override {
    if (!open_) {
      if (!pending.exchange(false)) return;
      open_ = true;
      closing_ = false;
      waitRelease_ = true;  // the press that accepted the settings
      choice_ = 0;
      previous_ = 0;
      lastMouse_ = io.MousePos;
      outrun::pointer::inputHeld = true;
      outrun::pointer::Clear();
    }
    const auto now = std::chrono::steady_clock::now();
    const WORD buttons = WORD(outrun::keyboard_prompt::PadButtons(window_ && window_->HasFocus()));
    if (closing_) {
      // Menus stay held until the answer's key or button is let go (for good
      // when restarting: the window is closing).
      const bool down = ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyDown(ImGuiKey_KeypadEnter) ||
                        ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Escape) ||
                        ImGui::IsKeyDown(ImGuiKey_Backspace) || ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1);
      if (!restart && !buttons && !down) {
        open_ = false;
        outrun::pointer::Clear();
        outrun::pointer::inputHeld = false;
      }
      return;
    }

    // Pad: presses, up/down repeating while held.
    if (!buttons) waitRelease_ = false;
    WORD pressed = waitRelease_ ? 0 : WORD(buttons & ~previous_);
    constexpr WORD kRepeating = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN;
    if (pressed & kRepeating) heldSince_ = lastRepeat_ = now;
    else if (!waitRelease_ && (buttons & kRepeating) && now - heldSince_ > std::chrono::milliseconds(400) &&
             now - lastRepeat_ > std::chrono::milliseconds(150)) {
      pressed |= buttons & kRepeating;
      lastRepeat_ = now;
    }
    previous_ = buttons;
    if (pressed & XINPUT_GAMEPAD_DPAD_UP) choice_ = std::max(choice_ - 1, 0);
    if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) choice_ = std::min(choice_ + 1, 1);
    if (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) return Decide(choice_);
    if (pressed & XINPUT_GAMEPAD_B) return Decide(1);
    // Keyboard, as in the menus: arrows, Enter/Space accept, Esc/Backspace back.
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) choice_ = 0;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) choice_ = 1;
    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_Space, false))
      return Decide(choice_);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) return Decide(1);

    // Game picture coordinates, letterboxed like the other overlays.
    const float gameW = outrun::pointer::fourByThree ? 960.f : 1280.f;
    const float scale = std::min(io.DisplaySize.x / gameW, io.DisplaySize.y / 720.f);
    if (scale <= 0) return;
    const ImVec2 origin((io.DisplaySize.x - gameW * scale) * .5f, (io.DisplaySize.y - 720 * scale) * .5f);
    auto at = [&](float x, float y) { return ImVec2(std::floor(origin.x + x * scale), std::floor(origin.y + y * scale)); };
    const float x0 = (gameW - kPanelW) / 2, y0 = (720 - kPanelH) / 2, x1 = x0 + kPanelW, y1 = y0 + kPanelH;
    auto rowTop = [&](int row) { return y0 + 78 + row * (kRowH + kRowGap); };

    // Mouse: moving over a row selects it, a click answers; right click is Later.
    float mx = (io.MousePos.x - origin.x) / scale, my = (io.MousePos.y - origin.y) / scale;
    if (const auto test = outrun::pointer::testCursor.load(); test.x >= 0) {  // background tests
      mx = test.x - (gameW < 1280 ? 160 : 0);
      my = test.y;
    }
    int under = -1;
    for (int row = 0; row < 2; ++row)
      if (mx >= x0 + 20 && mx <= x1 - 20 && my >= rowTop(row) && my <= rowTop(row) + kRowH) under = row;
    if (under >= 0 && (io.MousePos.x != lastMouse_.x || io.MousePos.y != lastMouse_.y)) choice_ = under;
    lastMouse_ = io.MousePos;
    if (under >= 0 && ImGui::IsMouseClicked(0)) return Decide(under);
    if (ImGui::IsMouseClicked(1)) return Decide(1);
    // The footer's Later and Select are clickable too, with the menus' highlight.
    const float footerTop = y1 - 46, footerBottom = y1 - 14;
    const float footerX[2][2] = {{x0 + 14, x0 + 150}, {x1 - 166, x1 - 14}};
    int footerUnder = -1;
    for (int i = 0; i < 2; ++i)
      if (mx >= footerX[i][0] && mx <= footerX[i][1] && my >= footerTop && my <= footerBottom) footerUnder = i;
    if (footerUnder >= 0 && ImGui::IsMouseClicked(0)) return Decide(footerUnder == 0 ? 1 : choice_);
    if (under >= 0 || footerUnder >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const Bar bar(draw, scale);
    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 8, 24, 120));
    // Frame and panel: white edge, sky-blue to deep blue, fine stripes.
    draw->AddRectFilled(at(x0 - 7, y0 - 7), at(x1 + 7, y1 + 7), IM_COL32(250, 252, 255, 255), 3 * scale);
    draw->AddRectFilledMultiColor(at(x0, y0), at(x1, y1), IM_COL32(88, 196, 250, 255), IM_COL32(88, 196, 250, 255),
                                  IM_COL32(24, 92, 210, 255), IM_COL32(24, 92, 210, 255));
    for (float y = y0 + 3; y < y1; y += 6)
      draw->AddLine(at(x0, y), at(x1, y), IM_COL32(255, 255, 255, 16), std::max(1.f, scale));
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    const char* message = "Restart now to apply the new settings?";
    const float messageW = bar.Text(0, 0, message, white, false, 28);
    bar.Text(std::floor(origin.x + (x0 + kPanelW / 2) * scale - messageW / 2), origin.y + (y0 + 40) * scale, message,
             white, true, 28);
    // Rows: capsules with a dark edge and a lighter upper half; orange when chosen.
    for (int row = 0; row < 2; ++row) {
      const float top = rowTop(row), bottom = top + kRowH;
      const bool lit = row == choice_;
      const float r = 24 * scale;
      draw->AddRectFilled(at(x0 + 17, top - 3), at(x1 - 17, bottom + 3), IM_COL32(8, 24, 80, 255), r + 3 * scale);
      draw->AddRectFilled(at(x0 + 20, top), at(x1 - 20, bottom), lit ? IM_COL32(255, 122, 0, 255) : IM_COL32(22, 70, 190, 255), r);
      draw->AddRectFilled(at(x0 + 22, top + 2), at(x1 - 22, top + kRowH / 2), lit ? IM_COL32(255, 196, 40, 200) : IM_COL32(70, 150, 245, 170),
                          r, ImDrawFlags_RoundCornersTop);
      bar.Text(origin.x + (x0 + 44) * scale, origin.y + (top + kRowH / 2) * scale, kRows[row], white, true, 32);
    }
    // The footer prompt under the mouse: the rows' orange capsule, behind it.
    if (outrun::pointer::kFooterHighlight && footerUnder >= 0) {
      const ImVec2 a = at(footerX[footerUnder][0], footerTop), b = at(footerX[footerUnder][1], footerBottom);
      const float round = (b.y - a.y) * .5f;
      draw->AddRectFilled(ImVec2(a.x - 2 * scale, a.y - 2 * scale), ImVec2(b.x + 2 * scale, b.y + 2 * scale),
                          IM_COL32(8, 24, 80, 255), round + 2 * scale);
      draw->AddRectFilled(a, b, IM_COL32(255, 122, 0, 255), round);
      draw->AddRectFilled(ImVec2(a.x + scale, a.y + scale), ImVec2(b.x - scale, (a.y + b.y) * .5f),
                          IM_COL32(255, 196, 40, 220), round, ImDrawFlags_RoundCornersTop);
    }
    // Footer, as the game's: back on the left (key or button first), select on the right.
    const bool pad = outrun::pointer::controllerConnected.load() || !REXCVAR_GET(outrun_test_script).empty();
    const float fy = origin.y + (y1 - 30) * scale;
    float x = origin.x + (x0 + 22) * scale;
    x += (pad ? bar.Button(x, fy, 'B', true) : bar.Key(x, fy, "Esc", true)) + bar.Gap();
    bar.Text(x, fy, "Later", white, true, 28);
    const float selectW = bar.Text(0, 0, "Select", white, false, 28);
    const float capW = pad ? bar.Button(0, 0, 'A', false) : bar.Key(0, 0, "Enter", false);
    float right = origin.x + (x1 - 22) * scale - capW;
    if (pad) bar.Button(right, fy, 'A', true);
    else bar.Key(right, fy, "Enter", true);
    bar.Text(right - bar.Gap() - selectW, fy, "Select", white, true, 28);
  }
};
std::unique_ptr<Prompt> prompt;
}  // namespace

void Initialize(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window) {
  if (drawer) prompt = std::make_unique<Prompt>(drawer, window);
}

void Show() {
  if (!prompt) return;
  REXLOG_INFO("Restart prompt: shown (a restart setting changed)");
  pending = true;
}

void Shutdown() {
  prompt.reset();
  outrun::pointer::inputHeld = false;
}
}  // namespace outrun::restart_prompt
