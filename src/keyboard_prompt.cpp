// The game asks for text through the Xbox system keyboard (XShowKeyboardUI,
// wrapped by sub_8227DF38). On a 360 that is the Guide's on-screen keyboard,
// worked with the pad; the PC runtime's stand-in is a text box that needs a
// keyboard and mouse. For the Ranking screen's high-score initials this file
// lets the player write them straight into the game's own new table row, in
// the game's lettering, arcade style: D-pad up/down picks the letter (the
// one being set blinks), left/right or A/B move, START finishes; or type
// them and press Enter. A bar in the free space under the table shows the
// controls. It starts from the initials entered last time (else the online
// name's, multiplayer_settings) and remembers what is entered. Empty places
// are left out; no initials at all gives the game's own "----". The game
// waits on the call's overlapped as it would for the Guide, so nothing else
// moves meanwhile.
//
// The game's other prompt (16 characters, from the menu scripts) still shows
// the runtime's text box; its wording goes to the log.
#include "generated/default/outrun_init.h"
#include "keyboard_prompt.h"
#include "menu_pointer.h"
#include "prompt_bar.h"
#include "multiplayer_options.h"
#include "test_script.h"
#include "ui_font.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xtypes.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
// After the SDK's input header: Xinput.h's macros reuse its enum names.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <Xinput.h>

using rex::X_RESULT;  // the X_ERROR_ macros name it unqualified

namespace outrun::keyboard_prompt {
namespace {
// Return address of the Ranking screen's call (in sub_821C0B20), and what
// that code fills once the prompt returns: the new entry's rank at
// overlapped-304 and the table of 72-byte rows at overlapped-1096, whose
// name (four characters and a terminator) is at +20.
constexpr uint32_t kInitialsCall = 0x821C0C60;
constexpr uint32_t kRankingOverlapped = 0x828B0F90;
constexpr uint32_t kRankingRank = kRankingOverlapped - 304;
constexpr uint32_t kRankingRows = kRankingOverlapped - 1096;
constexpr uint32_t kRowSize = 72, kRowName = 20;
constexpr int kShownRows = 5;  // 1st to 5th on the Ranking screen
constexpr uint32_t kSystemUiNotification = 0x9;  // XN_SYS_UI
constexpr char kLetters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
constexpr int kBlank = 36, kChoices = 37;  // the letters and digits, then an empty place
constexpr int kMaxPlaces = 4;
constexpr int kBlinkMs = 600, kLitMs = 420;

// One request at a time: the hook fills it (guest thread), the overlay edits
// its own copy (UI thread) and hands back `result`, which the overlapped's
// completion (the kernel's dispatch thread) waits for.
std::mutex mutex;
std::condition_variable answered;
struct Request {
  std::string initial;
  int places = 4;
  uint32_t row = 0;  // the new row's name in guest memory; 0 when not on screen
  bool done = true;
  std::string result;
} request;
std::atomic<bool> active{false};

int Choice(char c) {
  for (int i = 0; i < kBlank; ++i)
    if (kLetters[i] == c) return i;
  return kBlank;
}

uint8_t* Guest(uint32_t address) {
  return address ? rex::system::kernel_state()->emulator()->memory()->TranslateVirtual(address) : nullptr;
}

// A guest string (UTF-16, big-endian) as ASCII.
std::string GuestText(const uint8_t* p) {
  std::string text;
  for (size_t i = 0; p && i < 120; ++i) {
    const uint16_t c = static_cast<uint16_t>((p[i * 2] << 8) | p[i * 2 + 1]);
    if (!c) break;
    text += c >= 0x20 && c < 0x7F ? static_cast<char>(c) : '?';
  }
  return text;
}

bool Scripted() { return !REXCVAR_GET(outrun_test_script).empty(); }

using outrun::prompt_bar::Bar;

class InitialsEntry final : public rex::ui::ImGuiDialog {
 public:
  InitialsEntry(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window) : ImGuiDialog(drawer), window_(window) {}
  bool WantsContinuousRedraw() const override { return active.load(); }

 private:
  rex::ui::Window* window_;
  bool open_ = false, waitRelease_ = true;
  int count_ = 4, cursor_ = 0;
  uint32_t row_ = 0;
  std::array<int, kMaxPlaces> places_{};
  WORD previous_ = 0;
  std::chrono::steady_clock::time_point heldSince_{}, lastRepeat_{}, blinkFrom_{};

  void Cycle(int step) { places_[cursor_] = (places_[cursor_] + step + kChoices) % kChoices; }
  void Move(int step) { cursor_ = std::clamp(cursor_ + step, 0, count_ - 1); }
  void Type(char c) {
    places_[cursor_] = Choice(c);
    Move(1);
  }
  void Erase() {
    if (places_[cursor_] == kBlank && cursor_ > 0) --cursor_;
    places_[cursor_] = kBlank;
  }
  std::string Text() const {
    std::string text;
    for (int i = 0; i < count_; ++i)
      if (places_[i] != kBlank) text += kLetters[places_[i]];
    return text;
  }
  // The initials as shown: the place being set blinks, and shows a dash
  // while empty so it can be found.
  char Shown(int i, bool lit) const {
    if (i == cursor_ && !lit) return ' ';
    return places_[i] == kBlank ? (i == cursor_ ? '-' : ' ') : kLetters[places_[i]];
  }
  void ShowInRow(bool lit) const {
    uint8_t* name = Guest(row_);
    if (!name) return;
    for (int i = 0; i < count_; ++i) name[i] = uint8_t(Shown(i, lit));
    name[count_] = 0;
  }
  void Finish() {
    const std::string text = Text();
    if (uint8_t* name = Guest(row_)) {  // no blink left behind while the game takes over
      for (int i = 0; i <= count_; ++i) name[i] = uint8_t(i < int(text.size()) ? text[i] : 0);
    }
    {
      std::lock_guard lock(mutex);
      request.result = text;
      request.done = true;
    }
    active = false;
    open_ = false;
    answered.notify_all();
    outrun::pointer::Clear();  // nothing on the way out reaches the menus
  }

  void OnDraw(ImGuiIO& io) override {
    if (!active) return;
    const auto now = std::chrono::steady_clock::now();
    if (!open_) {
      std::lock_guard lock(mutex);
      count_ = std::clamp(request.places, 1, kMaxPlaces);
      row_ = request.row;
      places_.fill(kBlank);
      for (int i = 0; i < count_ && i < int(request.initial.size()); ++i) places_[i] = Choice(request.initial[i]);
      cursor_ = 0;
      waitRelease_ = true;  // the press that ended the race screen
      previous_ = 0;
      blinkFrom_ = now;
      open_ = true;
    }
    // Pad: presses, with the D-pad repeating while held.
    const WORD buttons = PadButtons(window_ && window_->HasFocus());
    if (!buttons) waitRelease_ = false;
    WORD pressed = waitRelease_ ? 0 : WORD(buttons & ~previous_);
    constexpr WORD kRepeating = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT |
                                XINPUT_GAMEPAD_DPAD_RIGHT;
    if (pressed & kRepeating) heldSince_ = lastRepeat_ = now;
    else if (!waitRelease_ && (buttons & kRepeating) && now - heldSince_ > std::chrono::milliseconds(400) &&
             now - lastRepeat_ > std::chrono::milliseconds(110)) {
      pressed |= buttons & kRepeating;
      lastRepeat_ = now;
    }
    previous_ = buttons;
    bool finish = (pressed & XINPUT_GAMEPAD_START) != 0;
    if (pressed & XINPUT_GAMEPAD_DPAD_UP) Cycle(1);
    if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) Cycle(-1);
    if (pressed & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_B)) Move(-1);
    if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) Move(1);
    if (pressed & XINPUT_GAMEPAD_A) {
      if (cursor_ < count_ - 1) Move(1);
      else finish = true;
    }
    // Keyboard.
    bool typed = false;
    for (int i = 0; i < 26; ++i)
      if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_A + i))) Type(char('A' + i)), typed = true;
    for (int i = 0; i < 10; ++i)
      if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_0 + i)) || ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_Keypad0 + i)))
        Type(char('0' + i)), typed = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) Erase(), typed = true;
    if (ImGui::IsKeyPressed(ImGuiKey_Delete)) places_[cursor_] = kBlank, typed = true;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) Move(-1), typed = true;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) Move(1), typed = true;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) Cycle(1), typed = true;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) Cycle(-1), typed = true;
    finish |= ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
    if (pressed || typed) blinkFrom_ = now;  // a change shows at once
    if (finish) {
      Finish();
      return;
    }
    const bool lit =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - blinkFrom_).count() % kBlinkMs < kLitMs;
    ShowInRow(lit);

    // The controls bar, in the free strip under the table (game picture
    // coordinates, letterboxed like the other overlays).
    const float gameW = outrun::pointer::fourByThree ? 960.f : 1280.f;
    const float scale = std::min(io.DisplaySize.x / gameW, io.DisplaySize.y / 720.f);
    if (scale <= 0) return;
    const ImVec2 origin((io.DisplaySize.x - gameW * scale) * .5f, (io.DisplaySize.y - 720 * scale) * .5f);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const Bar bar(draw, scale);
    const float y = std::floor(origin.y + 668 * scale);
    const bool pad = outrun::pointer::controllerConnected.load() || Scripted();
    std::string own;  // a row off the screen's table: the initials lead the bar
    if (!row_) {
      own = "Initials ";
      for (int i = 0; i < count_; ++i) own += Shown(i, lit);
    }
    const ImU32 white = IM_COL32(255, 255, 255, 255);
    auto layout = [&](float x, bool draw) {
      const float start = x;
      if (!own.empty()) x += bar.Text(x, y, own.c_str(), IM_COL32(255, 222, 40, 255), draw) + bar.Spacing();
      if (pad) {
        x += bar.Button(x, y, 'D', draw) + bar.Gap();
        x += bar.Text(x, y, "Letter", white, draw) + bar.Spacing();
        x += bar.Button(x, y, 'A', draw) + bar.Gap();
        x += bar.Text(x, y, "Next", white, draw) + bar.Spacing();
        x += bar.Button(x, y, 'B', draw) + bar.Gap();
        x += bar.Text(x, y, "Back", white, draw) + bar.Spacing();
        x += bar.Button(x, y, 'S', draw) + bar.Gap();
        x += bar.Text(x, y, "Done", white, draw);
      } else {
        x += bar.Text(x, y, "Type your initials", white, draw) + bar.Spacing();
        x += bar.Key(x, y, "Backspace", draw) + bar.Gap();
        x += bar.Text(x, y, "Erase", white, draw) + bar.Spacing();
        x += bar.Key(x, y, "Enter", draw) + bar.Gap();
        x += bar.Text(x, y, "Done", white, draw);
      }
      return x - start;
    };
    const float width = layout(0, false), padX = 26 * scale, half = 23 * scale;
    const float left = std::floor(origin.x + (gameW * scale - width) / 2);
    const ImVec2 a(left - padX, y - half), b(left + width + padX, y + half);
    draw->AddRectFilled(a, b, IM_COL32(8, 12, 26, 170), 8 * scale);
    draw->AddRectFilled(ImVec2(a.x + 2 * scale, a.y + 2 * scale), ImVec2(b.x - 2 * scale, y), IM_COL32(255, 255, 255, 22),
                        6 * scale, ImDrawFlags_RoundCornersTop);
    draw->AddRect(a, b, IM_COL32(235, 240, 250, 200), 8 * scale, 0, 2 * scale);
    layout(left, true);
  }
};
std::unique_ptr<InitialsEntry> entry;

// The answer into the game's buffer: UTF-16, big-endian, zero-filled.
void WriteAnswer(uint8_t* out, uint32_t length, const std::string& text) {
  for (uint32_t i = 0; i < length; ++i) {
    out[i * 2] = 0;
    out[i * 2 + 1] = static_cast<uint8_t>(i < text.size() ? text[i] : '\0');
  }
}
}  // namespace

// Pad buttons (XINPUT_GAMEPAD_*, sticks as the D-pad): the background tests'
// scripted pad when one is set, else the first connected controller.
uint16_t PadButtons(bool focused) {
  rex::input::X_INPUT_STATE scripted{};
  if (outrun::test_script::Read(&scripted)) {
    WORD buttons = WORD(scripted.gamepad.buttons);
    if (scripted.gamepad.thumb_ly > 16000) buttons |= XINPUT_GAMEPAD_DPAD_UP;
    if (scripted.gamepad.thumb_ly < -16000) buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
    return buttons;
  }
  if (!focused) return 0;
  XINPUT_STATE state{};
  for (DWORD i = 0; i < 4; ++i) {
    if (XInputGetState(i, &state) != ERROR_SUCCESS) continue;
    WORD buttons = state.Gamepad.wButtons;
    if (state.Gamepad.sThumbLY > 16000) buttons |= XINPUT_GAMEPAD_DPAD_UP;
    if (state.Gamepad.sThumbLY < -16000) buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
    if (state.Gamepad.sThumbLX > 16000) buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (state.Gamepad.sThumbLX < -16000) buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
    return buttons;
  }
  return 0;
}

void Initialize(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window) {
  if (drawer) entry = std::make_unique<InitialsEntry>(drawer, window);
}
void Shutdown() {
  {
    std::lock_guard lock(mutex);
    request.done = true;  // releases a completion still waiting
  }
  active = false;
  answered.notify_all();
  entry.reset();
}
}  // namespace outrun::keyboard_prompt

// XShowKeyboardUI(user, flags, default text, title, description, result, result length, overlapped)
REX_HOOK_RAW(sub_8227DF38) {
  using namespace outrun::keyboard_prompt;
  auto* kernel = rex::system::kernel_state();
  auto* memory = kernel->emulator()->memory();
  const uint32_t buffer = ctx.r8.u32, length = ctx.r9.u32, overlapped = ctx.r10.u32;
  if (ctx.lr != kInitialsCall || !buffer || !length || !overlapped) {
    REXLOG_INFO("Keyboard prompt ({} characters): \"{}\" / \"{}\"", length ? length - 1 : 0,
                GuestText(ctx.r6.u32 ? memory->TranslateVirtual(ctx.r6.u32) : nullptr),
                GuestText(ctx.r7.u32 ? memory->TranslateVirtual(ctx.r7.u32) : nullptr));
    __imp__sub_8227DF38(ctx, base);
    return;
  }
  std::string initial = outrun::multiplayer::HighScoreInitials();
  initial.resize(std::min<size_t>(initial.size(), length - 1));
  if (!entry) {  // no overlay (headless): the initials at once
    WriteAnswer(memory->TranslateVirtual(buffer), length, initial);
    kernel->CompleteOverlappedImmediateEx(overlapped, X_ERROR_SUCCESS,
                                          initial.empty() ? X_ERROR_CANCELLED : X_ERROR_SUCCESS, 0);
    ctx.r3.u64 = X_ERROR_IO_PENDING;
    return;
  }
  // The new row on screen, when the entry ranks 1st to 5th.
  const int32_t rank = int32_t(REX_LOAD_U32(kRankingRank));
  const uint32_t row = overlapped == kRankingOverlapped && rank >= 0 && rank < kShownRows
                           ? kRankingRows + uint32_t(rank) * kRowSize + kRowName
                           : 0;
  {
    std::lock_guard lock(mutex);
    request = Request{initial, int(std::min<uint32_t>(length - 1, kMaxPlaces)), row, false, {}};
  }
  active = true;  // the entry starts on the overlay's next frame
  REXLOG_INFO("High-score initials: entry open (\"{}\", rank {})", initial, rank + 1);
  // As the runtime's dialogs complete: pending until answered, XN_SYS_UI
  // around it. The game polls the overlapped each frame meanwhile.
  kernel->CompleteOverlappedDeferredEx(
      [memory, buffer, length](uint32_t& extended_error, uint32_t& written) -> X_RESULT {
        std::string text;
        {
          std::unique_lock lock(mutex);
          answered.wait(lock, [] { return request.done; });
          text = request.result;
        }
        // Hand back once the finishing press is let go, so the Ranking
        // screen does not take it as its own.
        for (int i = 0; i < 150; ++i) {
          const bool held = PadButtons(true) || (GetAsyncKeyState(VK_RETURN) & 0x8000);
          if (!held) break;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        text.resize(std::min<size_t>(text.size(), length - 1));
        WriteAnswer(memory->TranslateVirtual(buffer), length, text);
        if (!text.empty()) outrun::multiplayer::SetLocalName(text);
        REXLOG_INFO("High-score initials entered: {}", text.empty() ? "none (the game's \"----\")" : text);
        extended_error = text.empty() ? X_ERROR_CANCELLED : X_ERROR_SUCCESS;
        written = 0;
        return X_ERROR_SUCCESS;
      },
      overlapped, [kernel] { kernel->BroadcastNotification(kSystemUiNotification, 1); },
      [kernel] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        kernel->BroadcastNotification(kSystemUiNotification, 0);
      });
  ctx.r3.u64 = X_ERROR_IO_PENDING;
}
