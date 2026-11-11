// Help & Options > Check for Updates (docs/UPDATES.md). Nothing is checked
// at start or in the background: only when the player picks the card, a
// worker thread asks GitHub's releases/latest for this project
// (outrun_update.h; OUTRUN_UPDATE_API points it elsewhere for tests) while this
// box says so. The answer: up to date, could not check (with the reason), or
// "Version X is available. You have Y." with Update and Cancel. Update starts
// the launcher's update window ("OutRun Online Arcade.exe --update", one
// folder up from this exe), which waits for the game to exit, downloads and
// checks the release, replaces the program files and starts the game again;
// then this window closes as its close button does. From a race (Help &
// Options opened from the pause menu) the player is asked to quit to the main
// menu first.
//
// The box is drawn like the restart prompt (restart_prompt.cpp): the game's
// option popup frame, capsule rows with the orange highlight, heavy italic
// lettering and the footer's key caps or pad buttons. While it shows, the
// menus get no input (pointer::inputHeld), and it is held until the answering
// press is released.
#include "update_prompt.h"
#include "keyboard_prompt.h"
#include "menu_footer.h"
#include "menu_pointer.h"
#include "prompt_bar.h"
#include "test_script.h"
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
// After the SDK's headers: Xinput.h's macros reuse its input enum names.
#include "outrun_update.h"
#include <Xinput.h>

#ifndef OUTRUN_VERSION_TEXT
#define OUTRUN_VERSION_TEXT "0.0.0"
#endif

namespace outrun::update_prompt {
namespace {
using outrun::prompt_bar::Bar;

enum class Stage { Checking, UpToDate, Failed, Available, QuitFirst, LaunchFailed, Setup, SetupFailed };

// Written by the check's worker, read by the box. A new check (or closing the
// box) moves `generation` on, so a late answer is dropped.
struct Shared {
  std::mutex lock;
  int generation = 0;
  Stage stage = Stage::Checking;
  std::string latest, error;
};
std::shared_ptr<Shared> shared = std::make_shared<Shared>();
std::atomic<bool> pending{false};
std::atomic<bool> fromRace{false};
std::atomic<bool> setupMode{false};  // the Setup card's box rather than the update check

void StartCheck() {
  int generation;
  {
    std::lock_guard guard(shared->lock);
    generation = ++shared->generation;
    shared->stage = Stage::Checking;
    shared->latest.clear();
    shared->error.clear();
  }
  std::thread([state = shared, generation] {
    outrun::update::Release release;
    std::string error;
    const bool ok = outrun::update::LatestRelease(
        release, L"OutRun-Online-Arcade-PC/" + outrun::update::Widen(OUTRUN_VERSION_TEXT), error);
    std::lock_guard guard(state->lock);
    if (state->generation != generation) return;
    if (!ok) {
      REXLOG_WARN("Check for Updates: could not check: {}", error);
      state->stage = Stage::Failed;
      state->error = error;
      return;
    }
    REXLOG_INFO("Check for Updates: latest release {}, this copy {}", release.version, OUTRUN_VERSION_TEXT);
    state->latest = release.version;
    state->stage = outrun::update::CompareVersions(release.version, OUTRUN_VERSION_TEXT) <= 0 ? Stage::UpToDate
                   : fromRace.load()                                                         ? Stage::QuitFirst
                                                                                             : Stage::Available;
  }).detach();
}

// "OutRun Online Arcade.exe <argument>" (--update or --manage), beside the
// resources folder this exe lives in. It inherits OUTRUN_UPDATE_API.
bool StartLauncher(const wchar_t* argument, std::string& error) {
  std::wstring exe(32768, L'\0');
  exe.resize(GetModuleFileNameW(nullptr, exe.data(), DWORD(exe.size())));
  const auto root = std::filesystem::path(exe).parent_path().parent_path();
  const auto launcher = root / L"OutRun Online Arcade.exe";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(launcher, ec)) {
    error = "OutRun Online Arcade.exe was not found beside the resources folder";
    return false;
  }
  std::wstring command = L"\"" + launcher.wstring() + L"\" " + argument;
  STARTUPINFOW startup{sizeof(startup)};
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(launcher.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &startup,
                      &process)) {
    error = "OutRun Online Arcade.exe could not be started (error " + std::to_string(GetLastError()) + ")";
    return false;
  }
  REXLOG_INFO("Started OutRun Online Arcade.exe {} (pid {}); closing the game",
              std::string(argument, argument + wcslen(argument)), process.dwProcessId);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

constexpr float kPanelW = 800, kRowH = 56, kRowGap = 14, kLine = 36, kDetailLine = 30;

class Prompt final : public rex::ui::ImGuiDialog {
 public:
  Prompt(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window) : ImGuiDialog(drawer), window_(window) {}
  bool WantsContinuousRedraw() const override { return open_ || pending.load(); }

 private:
  rex::ui::Window* window_;
  bool open_ = false, closing_ = false, waitRelease_ = true, leaving_ = false;
  int choice_ = 0;
  Stage shown_ = Stage::Checking;
  std::string launchError_;
  WORD previous_ = 0;
  ImVec2 lastMouse_{-1, -1};
  std::chrono::steady_clock::time_point heldSince_{}, lastRepeat_{};

  // Rows of each answer, in order; the last is what Back does.
  static std::vector<const char*> Rows(Stage stage) {
    if (stage == Stage::Checking) return {};
    if (stage == Stage::Available) return {"Update", "Cancel"};
    if (stage == Stage::Setup) return {"Open Setup", "Cancel"};
    return {"OK"};
  }

  void Close() {
    closing_ = true;
    std::lock_guard guard(shared->lock);
    ++shared->generation;  // a check still under way is ignored
  }

  void Decide(int row) {
    if ((shown_ == Stage::Available || shown_ == Stage::Setup) && row == 0) {
      const bool setup = shown_ == Stage::Setup;
      REXLOG_INFO(setup ? "Setup: open setup chosen" : "Check for Updates: update chosen");
      if (!StartLauncher(setup ? L"--manage" : L"--update", launchError_)) {
        REXLOG_ERROR("{}: {}", setup ? "Setup" : "Check for Updates", launchError_);
        shown_ = setup ? Stage::SetupFailed : Stage::LaunchFailed;
        choice_ = 0;
        waitRelease_ = true;
        return;
      }
      leaving_ = true;
      closing_ = true;
      if (window_) {
        auto* window = window_;
        window_->app_context().CallInUIThreadDeferred([window] { window->RequestClose(); });
      }
      return;
    }
    REXLOG_INFO("{}: closed", setupMode.load() ? "Setup" : "Check for Updates");
    Close();
  }

  // Splits `text` into lines no wider than `width` (at spaces and at '\n').
  static std::vector<std::string> Wrap(const Bar& bar, const std::string& text, float width, float size) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush = [&] {
      if (word.empty()) return;
      const std::string tried = line.empty() ? word : line + " " + word;
      if (!line.empty() && bar.Text(0, 0, tried.c_str(), 0, false, size) > width) {
        lines.push_back(line);
        line = word;
      } else {
        line = tried;
      }
      word.clear();
    };
    for (char c : text) {
      if (c == ' ') {
        flush();
      } else if (c == '\n') {  // a line break of its own
        flush();
        lines.push_back(line);
        line.clear();
      } else {
        word += c;
      }
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
  }

  void OnDraw(ImGuiIO& io) override {
    if (!open_) {
      if (!pending.exchange(false)) return;
      open_ = true;
      closing_ = leaving_ = false;
      waitRelease_ = true;  // the press that chose the card
      choice_ = 0;
      previous_ = 0;
      lastMouse_ = io.MousePos;
      outrun::pointer::inputHeld = true;
      outrun::pointer::Clear();
      if (setupMode.load()) {
        shown_ = Stage::Setup;
      } else {
        shown_ = Stage::Checking;
        StartCheck();
      }
    }
    const auto now = std::chrono::steady_clock::now();
    const WORD buttons = WORD(outrun::keyboard_prompt::PadButtons(window_ && window_->HasFocus()));
    if (closing_) {
      const bool down = ImGui::IsKeyDown(ImGuiKey_Enter) || ImGui::IsKeyDown(ImGuiKey_KeypadEnter) ||
                        ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_Escape) ||
                        ImGui::IsKeyDown(ImGuiKey_Backspace) || ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1);
      if (!leaving_ && !buttons && !down) {
        open_ = false;
        outrun::pointer::Clear();
        outrun::pointer::inputHeld = false;
      }
      return;
    }
    std::string latest, error;
    {
      std::lock_guard guard(shared->lock);
      if (!setupMode.load() && shown_ == Stage::Checking && shared->stage != Stage::Checking) {
        shown_ = shared->stage;
        choice_ = 0;
      }
      latest = shared->latest;
      error = shared->error;
    }
    const auto rows = Rows(shown_);
    const int count = int(rows.size());
    const int back = count - 1;  // Cancel / OK; while checking, Back closes

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
    if (count) {
      if (pressed & XINPUT_GAMEPAD_DPAD_UP) choice_ = std::max(choice_ - 1, 0);
      if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) choice_ = std::min(choice_ + 1, count - 1);
      if (pressed & (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_START)) return Decide(choice_);
    }
    if (pressed & XINPUT_GAMEPAD_B) return Decide(back);
    // Keyboard, as in the menus: arrows, Enter/Space accept, Esc/Backspace back.
    if (count) {
      if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) choice_ = std::max(choice_ - 1, 0);
      if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) choice_ = std::min(choice_ + 1, count - 1);
      if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
          ImGui::IsKeyPressed(ImGuiKey_Space, false))
        return Decide(choice_);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
      return Decide(back);

    // The words of each answer.
    std::string title, detail;
    switch (shown_) {
      case Stage::Checking:
        title = "Checking for updates...";
        detail = "Asking GitHub for the latest release.";
        break;
      case Stage::UpToDate:
        title = "OutRun is up to date.";
        detail = "You have the latest version (" OUTRUN_VERSION_TEXT ").";
        break;
      case Stage::Failed:
        title = "Could not check for updates.";
        detail = error.empty() ? std::string("No answer came back.") : error;
        detail[0] = char(std::toupper(static_cast<unsigned char>(detail[0])));
        if (detail.back() != '.') detail += '.';
        break;
      case Stage::Available:
        title = "Version " + latest + " is available. You have " OUTRUN_VERSION_TEXT ".";
        detail = "Update closes the game, installs the new version and starts it again.\n"
                 "Your saves and settings are kept.";
        break;
      case Stage::QuitFirst:
        title = "Version " + latest + " is available. You have " OUTRUN_VERSION_TEXT ".";
        detail = "Quit to the main menu first, then choose Check for Updates again.";
        break;
      case Stage::Setup:
        title = "Close the game and open Setup?";
        detail = fromRace.load() ? "The race in progress ends. Start the game again with Play in Setup."
                                 : "Start the game again with Play in Setup.";
        break;
      case Stage::SetupFailed:
      case Stage::LaunchFailed:
        title = shown_ == Stage::SetupFailed ? "Setup could not start." : "The update could not start.";
        detail = launchError_;
        detail[0] = char(std::toupper(static_cast<unsigned char>(detail[0])));
        detail += '.';
        break;
    }

    // Game picture coordinates, letterboxed like the other overlays.
    const float gameW = outrun::pointer::fourByThree ? 960.f : 1280.f;
    const float scale = std::min(io.DisplaySize.x / gameW, io.DisplaySize.y / 720.f);
    if (scale <= 0) return;
    const ImVec2 origin((io.DisplaySize.x - gameW * scale) * .5f, (io.DisplaySize.y - 720 * scale) * .5f);
    auto at = [&](float x, float y) { return ImVec2(std::floor(origin.x + x * scale), std::floor(origin.y + y * scale)); };
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const Bar bar(draw, scale);
    const float panelW = std::min(kPanelW, gameW - 40);
    const auto titleLines = Wrap(bar, title, (panelW - 60) * scale, 30);
    const auto detailLines = Wrap(bar, detail, (panelW - 60) * scale, 25);
    const float textH = 30 + kLine * float(titleLines.size()) + 8 + kDetailLine * float(detailLines.size()) + 6;
    const float rowsH = count ? 8 + count * (kRowH + kRowGap) - kRowGap : 0;
    const float panelH = textH + rowsH + 74;
    const float x0 = (gameW - panelW) / 2, y0 = std::floor((720 - panelH) / 2), x1 = x0 + panelW, y1 = y0 + panelH;
    auto rowTop = [&](int row) { return y0 + textH + 8 + row * (kRowH + kRowGap); };

    // Mouse: moving over a row selects it, a click answers; right click is Back.
    float mx = (io.MousePos.x - origin.x) / scale, my = (io.MousePos.y - origin.y) / scale;
    if (const auto test = outrun::pointer::testCursor.load(); test.x >= 0) {  // background tests
      mx = test.x - (gameW < 1280 ? 160 : 0);
      my = test.y;
    }
    int under = -1;
    for (int row = 0; row < count; ++row)
      if (mx >= x0 + 20 && mx <= x1 - 20 && my >= rowTop(row) && my <= rowTop(row) + kRowH) under = row;
    if (under >= 0 && (io.MousePos.x != lastMouse_.x || io.MousePos.y != lastMouse_.y)) choice_ = under;
    lastMouse_ = io.MousePos;
    if (under >= 0 && ImGui::IsMouseClicked(0)) return Decide(under);
    if (ImGui::IsMouseClicked(1)) return Decide(back);
    // The footer's prompts are clickable too.
    const float footerTop = y1 - 46, footerBottom = y1 - 14;
    const float footerX[2][2] = {{x0 + 14, x0 + 190}, {x1 - 166, x1 - 14}};
    int footerUnder = -1;
    for (int i = 0; i < (count ? 2 : 1); ++i)
      if (mx >= footerX[i][0] && mx <= footerX[i][1] && my >= footerTop && my <= footerBottom) footerUnder = i;
    if (footerUnder >= 0 && ImGui::IsMouseClicked(0)) return Decide(footerUnder == 0 ? back : choice_);
    if (under >= 0 || footerUnder >= 0) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    draw->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 8, 24, 120));
    // Frame and panel: white edge, sky-blue to deep blue, fine stripes.
    draw->AddRectFilled(at(x0 - 7, y0 - 7), at(x1 + 7, y1 + 7), IM_COL32(250, 252, 255, 255), 3 * scale);
    draw->AddRectFilledMultiColor(at(x0, y0), at(x1, y1), IM_COL32(88, 196, 250, 255), IM_COL32(88, 196, 250, 255),
                                  IM_COL32(24, 92, 210, 255), IM_COL32(24, 92, 210, 255));
    for (float y = y0 + 3; y < y1; y += 6)
      draw->AddLine(at(x0, y), at(x1, y), IM_COL32(255, 255, 255, 16), std::max(1.f, scale));
    const ImU32 white = IM_COL32(255, 255, 255, 255), soft = IM_COL32(222, 238, 255, 255);
    float y = y0 + 30 + kLine / 2;
    for (const auto& line : titleLines) {
      const float w = bar.Text(0, 0, line.c_str(), white, false, 30);
      bar.Text(std::floor(origin.x + (x0 + panelW / 2) * scale - w / 2), origin.y + y * scale, line.c_str(), white, true, 30);
      y += kLine;
    }
    y += 8 + kDetailLine / 2 - kLine / 2;
    for (const auto& line : detailLines) {
      const float w = bar.Text(0, 0, line.c_str(), soft, false, 25);
      bar.Text(std::floor(origin.x + (x0 + panelW / 2) * scale - w / 2), origin.y + y * scale, line.c_str(), soft, true, 25);
      y += kDetailLine;
    }
    // Rows: capsules with a dark edge and a lighter upper half; orange when chosen.
    for (int row = 0; row < count; ++row) {
      const float top = rowTop(row), bottom = top + kRowH;
      const bool lit = row == choice_;
      const float r = 24 * scale;
      draw->AddRectFilled(at(x0 + 17, top - 3), at(x1 - 17, bottom + 3), IM_COL32(8, 24, 80, 255), r + 3 * scale);
      draw->AddRectFilled(at(x0 + 20, top), at(x1 - 20, bottom), lit ? IM_COL32(255, 122, 0, 255) : IM_COL32(22, 70, 190, 255), r);
      draw->AddRectFilled(at(x0 + 22, top + 2), at(x1 - 22, top + kRowH / 2), lit ? IM_COL32(255, 196, 40, 200) : IM_COL32(70, 150, 245, 170),
                          r, ImDrawFlags_RoundCornersTop);
      bar.Text(origin.x + (x0 + 44) * scale, origin.y + (top + kRowH / 2) * scale, rows[size_t(row)], white, true, 32);
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
    // Footer, as the game's: back on the left (key or button first), select on
    // the right (not while checking: there is nothing to select).
    const bool pad = outrun::pointer::controllerConnected.load() || !REXCVAR_GET(outrun_test_script).empty();
    const float fy = origin.y + (y1 - 30) * scale;
    float x = origin.x + (x0 + 22) * scale;
    x += (pad ? bar.Button(x, fy, 'B', true) : bar.Key(x, fy, "Esc", true)) + bar.Gap();
    bar.Text(x, fy, count ? "Back" : "Cancel", white, true, 28);
    if (!count) return;
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
  if (!prompt || pending.load()) return;
  // Help & Options reached from the pause menu: a race is under way.
  const bool race = outrun::pointer::pausePolledAt.load() > outrun::pointer::mainMenuPolledAt.load();
  fromRace = race;
  setupMode = false;
  REXLOG_INFO("Check for Updates: opened{}", race ? " (from the pause menu)" : "");
  pending = true;
}

void ShowSetup() {
  if (!prompt || pending.load()) return;
  const bool race = outrun::pointer::pausePolledAt.load() > outrun::pointer::mainMenuPolledAt.load();
  fromRace = race;
  setupMode = true;
  REXLOG_INFO("Setup: opened{}", race ? " (from the pause menu)" : "");
  pending = true;
}

void Shutdown() {
  prompt.reset();
  {
    std::lock_guard guard(shared->lock);
    ++shared->generation;
  }
  outrun::pointer::inputHeld = false;
}
}  // namespace outrun::update_prompt
