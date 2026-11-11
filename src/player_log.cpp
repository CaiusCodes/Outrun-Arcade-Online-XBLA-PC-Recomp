// Player log for bug reports (player_log.h, docs/PLAYER_LOG.md).
#include "generated/default/outrun_init.h"
#include "player_log.h"
#include "test_script.h"  // (before xinput.h, whose macros clash with rex/input)
#include "outrun_build_label.h"
#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>
#include <imgui.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dxgi1_6.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <gdiplus.h>
#include <xinput.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#if OUTRUN_HAS_NATIVE
namespace outrun {  // native_gpu.cpp
bool NativeRendererActive();
bool RequestMarkTrace(const std::string& path);
}
#endif
#include "pc_menu.h"

REXCVAR_DEFINE_BOOL(outrun_log_files, false, "OutRun/Diagnostics",
                    "Player log: also log every file the game opens (troubleshooting)");
REXCVAR_DEFINE_BOOL(outrun_log_breadcrumbs, true, "OutRun/Diagnostics",
                    "Player log: log the screen, stage, route and car as they change (bug reports)");
REXCVAR_DEFINE_BOOL(outrun_mark_key, true, "OutRun/Diagnostics",
                    "Player log: F8 writes a PLAYER MARK line (and a screenshot beside the log) for bug reports");
REXCVAR_DEFINE_BOOL(outrun_mark_screenshot, true, "OutRun/Diagnostics",
                    "Player log: save a PNG screenshot beside the log with each F8 mark");
REXCVAR_DEFINE_BOOL(outrun_mark_trace, true, "OutRun/Diagnostics",
                    "Player log: with each F8 mark, trace the next frame draw by draw into a file beside the log (Plume)");

namespace outrun::player_log {
namespace {
// ---------------------------------------------------------------- tables
struct Stage { const char* code; const char* name; int stage; int column; };
// Stage folders (Stage\XXXX) of OutRun2 SP and their place on the map (stage
// 1-5, column 0 = leftmost), from background runs (the fork signs each
// checkpoint loads, COMMON\obj_OBJ_<code>_BUNKI_TX, name the two next
// stages; docs/PLAYER_LOG.md). Names as the port's notes use them; MAYA,
// ALAS and FLOR keep the game's internal names (Text\English_US.bin).
constexpr Stage kStages[] = {
    {"BEAC", "Palm Beach", 1, 0},
    {"SANF", "Bay Area", 2, 0},         {"YOSE", "National Park", 2, 1},
    {"NIAG", "Water Falls", 3, 0},      {"SEQU", "Big Forest", 3, 1},     {"GRAN", "Canyon", 3, 2},
    {"MACH", "Lost City", 4, 0},        {"LASV", "Casino Town", 4, 1},    {"ALAS", "Alaska", 4, 2},
    {"AMAZ", "Jungle", 4, 3},
    {"EAST", "Giant Statues", 5, 0},    {"MAYA", "Maya", 5, 1},           {"PRIN", "Floral Village", 5, 2},
    {"FLOR", "Florida", 5, 3},          {"NEWY", "Skyscrapers", 5, 4},
};
const Stage* FindStage(const std::string& code) {
  for (const Stage& s : kStages)
    if (_stricmp(s.code, code.c_str()) == 0) return &s;
  return nullptr;
}
struct Script { uint32_t hash; const char* screen; };
// frontend.sbn's 27 scripts (name hash -> the screen it is), docs/MENU_SYSTEM.md.
constexpr Script kScripts[] = {
    {0x0E6B27A4, "title"}, {0x7FE653EA, "main menu"}, {0x6B185A6B, "help & options"},
    {0x1C39026A, nullptr},  // menu_manager: cards of whichever menu is open
    {0x0D4B66A5, "pause"}, {0xA711BBFF, "car select"}, {0x0732D2DF, "race options"},
    {0xA6BD9709, "music select"}, {0xBF7B11E0, "transmission select"}, {0xE6BC369C, "tuning"},
    {0x40CAC580, "leaderboards"}, {0x40EBD9BB, "achievements"}, {0xB470F0DF, "content download"},
    {0xC73FD6C0, nullptr}, {0x5FDDD366, nullptr}, {0xDAFA80FC, nullptr}, {0xDDD7D9B8, nullptr},
    {0x5BB0DBAC, nullptr},  // message boxes and saving: keep the screen under them
    {0xB723A00D, "multiplayer game pick"}, {0x220D0BCA, "multiplayer host"}, {0x5D9F7CC3, "multiplayer lobby"},
    {0xD6061DDF, "multiplayer"}, {0x760333A7, "multiplayer player"}, {0x18CCB6EC, "multiplayer quick match"},
    {0xD5CAB0DB, "multiplayer ranked"}, {0x2096F030, "multiplayer invitations"},
    {0xEA88C64B, "multiplayer results"},
};
const char* ScreenOf(uint32_t hash, bool& known) {
  for (const Script& s : kScripts)
    if (s.hash == hash) { known = true; return s.screen; }
  known = false;
  return nullptr;
}

// ---------------------------------------------------------------- state
using Clock = std::chrono::steady_clock;
double Now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }
// Game seconds (the guest clock, which the game's own timers follow).
double GuestNow() {
  return double(rex::chrono::Clock::QueryGuestTickCount()) / double(rex::chrono::Clock::guest_tick_frequency());
}

std::mutex g_mutex;
uint8_t* g_base = nullptr;
PPCFunc* g_nt_create_file = nullptr;
rex::ui::Window* g_window = nullptr;
std::filesystem::path g_logs_dir;
double g_session_start = 0;

std::string g_screen = "boot";       // logged screen
std::string g_candidate;             // screen waiting to be stable
double g_candidate_since = 0;
bool g_in_race = false;              // a stage has loaded since the front end
bool g_paused = false;
std::string g_game_mode = "single player";  // (OutRun and Heart Attack load the same files)
bool g_multiplayer = false;          // a multiplayer screen came before this race
bool g_time_attack = false;
std::string g_car;                   // last player car model loaded
const Stage* g_stage = nullptr;      // current stage
int g_stage_number = 0;              // stage within this race (1..5)
std::string g_route;                 // forks taken this race, L/R
bool g_goal = false;                 // the ending loaded this race
char g_goal_letter = 0;              // the goal sign of the name entry (A-E)
const Stage* g_pending = nullptr;    // next stage, loaded at the fork, not reached yet
double g_race_time = 0;              // seconds driving in this race (from the start line, not paused)
double g_stage_start = 0;            // g_race_time when the current stage began (its gate)
int g_lap = 1;                       // Continuous Mode: after stage 5 the race goes on from stage 1
// " lap N" from the second lap on.
std::string LapText() { return g_lap > 1 ? fmt::format(" lap {}", g_lap) : std::string(); }
bool g_clock_running = false;        // the start line (first BK section) has loaded
double g_last_file = 0;              // last file open (loading)
int g_races = 0;
std::set<std::string> g_stages_played;
int g_stage_entries = 0;
int g_marks = 0;

// Frame rate: 1-second windows of Swaps.
double g_fps = 0, g_fps_min_race = 1e9, g_window_start = 0, g_last_swap = 0, g_last_guest = 0;
uint64_t g_window_frames = 0, g_swaps = 0;
double g_first_swap = 0;
bool g_window_loading = false;

std::string Clock2(double s) {
  if (s < 0) s = 0;
  const int m = int(s / 60);
  char b[32];
  std::snprintf(b, sizeof(b), "%d:%05.2f", m, s - m * 60);
  return b;
}

std::string WhereLocked() {
  if (!g_in_race) return "screen " + g_screen;
  std::string w;
  if (g_stage)
    w = fmt::format("stage {} '{}' ({}){} route {}", g_stage_number, g_stage->name, g_stage->code, LapText(),
                    g_route.empty() ? "-" : g_route);
  else
    w = "race (stage loading)";
  w += fmt::format(", race {}", Clock2(g_race_time));
  if (g_paused) w += ", paused";
  return w;
}

std::string Lower(std::string s) {
  for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// ---------------------------------------------------------------- breadcrumbs
void LogScreen(const std::string& screen) {
  if (screen == g_screen) return;
  const std::string from = g_screen;
  g_screen = screen;
  if (!REXCVAR_GET(outrun_log_breadcrumbs)) return;
  REXLOG_INFO("Game: screen {} (was {})", screen, from);
}

void EndRace(const char* how) {
  if (!g_in_race) return;
  if (REXCVAR_GET(outrun_log_breadcrumbs))
    REXLOG_INFO("Game: race {} end ({}): reached stage {} '{}'{} route {}, {}, race {}", g_races, how, g_stage_number,
                g_stage ? g_stage->name : "?", LapText(), g_route.empty() ? "-" : g_route,
                g_goal ? (g_goal_letter ? fmt::format("goal {}", g_goal_letter) : std::string("goal")) : std::string("no goal"),
                Clock2(g_race_time));
  g_in_race = false;
  g_paused = false;
  g_stage = g_pending = nullptr;
  g_stage_number = 0;
  g_route.clear();
  g_goal = false;
}

void StartRace() {
  g_in_race = true;
  ++g_races;
  g_race_time = 0;
  g_clock_running = false;
  g_route.clear();
  g_lap = 1;
  g_stage = nullptr;
  g_stage_number = 0;
  g_goal = false;
  g_goal_letter = 0;
  const std::string mode = g_multiplayer ? "LAN/online" : g_time_attack ? "Time Attack" : g_game_mode;
  if (REXCVAR_GET(outrun_log_breadcrumbs))
    REXLOG_INFO("Game: race {} start: mode {}, car {}", g_races, mode, g_car.empty() ? "?" : g_car);
}

// The stage the car is now on (its gate, a BK section, has loaded).
void EnterStage(const Stage* s) {
  g_pending = nullptr;
  if (g_stage == s) return;
  // A fork: the next stage's column says which way (same column = left).
  if (g_stage && s->stage == g_stage->stage + 1)
    g_route += s->column == g_stage->column ? 'L' : s->column == g_stage->column + 1 ? 'R' : '?';
  else if (g_stage && s->stage < g_stage->stage) {  // past the goal and on again (Continuous Mode)
    ++g_lap;
    g_route.clear();
  }
  g_stage = s;
  g_stage_start = g_race_time;
  g_stage_number = s->stage;
  g_stages_played.insert(s->code);
  ++g_stage_entries;
  if (REXCVAR_GET(outrun_log_breadcrumbs))
    REXLOG_INFO("Game: stage {} '{}' ({}){} route {}, race {}", s->stage, s->name, s->code, LapText(),
                g_route.empty() ? "-" : g_route, Clock2(g_race_time));
}

// Called for each file the game opens (any guest thread). What the files say,
// from background runs down every route (docs/PLAYER_LOG.md): a stage's
// folder (Stage\XXXX) loads at its start and, after that, at the fork before
// the next checkpoint; the BK sections (the start line and each checkpoint
// gate, cs_BK_*) load as the car reaches them; obj_end_* is the ending and
// the ranking sprites the name entry after a race.
void OnFile(const std::string& path) {
  std::lock_guard lock(g_mutex);
  g_last_file = Now();
  g_window_loading = true;
  std::string p = Lower(path);
  std::replace(p.begin(), p.end(), char(92), '/');  // (backslashes as slashes)
  if (const size_t at = p.find("/stage/"); at != std::string::npos && p.size() >= at + 11) {
    if (const Stage* s = FindStage(p.substr(at + 7, 4))) {
      if (!g_in_race) {  // the first stage, loading before the start line
        StartRace();
        EnterStage(s);
      } else if (s != g_stage && s != g_pending) {
        if (g_pending) EnterStage(g_pending);  // (a gate not seen)
        g_pending = s;
        REXLOG_DEBUG("Game: next stage loading: {} '{}'", s->stage, s->name);
      }
    }
  }
  if (g_in_race && p.find("/bk/cs_bk_") != std::string::npos) {
    g_clock_running = true;
    if (g_pending) EnterStage(g_pending);
  }
  if (const size_t at = p.find("obj_plcar_"); at != std::string::npos) {
    std::string car = p.substr(at + 10);
    if (const size_t e = car.find("_smt"); e != std::string::npos) car.resize(e);
    for (char& c : car) c = char(std::toupper(static_cast<unsigned char>(c)));
    g_car = car;
  }
  if (p.find("/ghosts/gc_") != std::string::npos || p.find("sprani_taextra") != std::string::npos) g_time_attack = true;
  if (g_in_race && !g_goal && p.find("obj_end_") != std::string::npos && p.find("obj_end_property") == std::string::npos) {
    g_goal = true;
    if (REXCVAR_GET(outrun_log_breadcrumbs))
      REXLOG_INFO("Game: goal reached after stage {} '{}', race {}", g_stage_number, g_stage ? g_stage->name : "?",
                  Clock2(g_race_time));
  }
  // The name entry's goal sign (name5a..e) and ranking: the race is over.
  if (const size_t at = p.find("sprani_name5"); at != std::string::npos && at + 12 < p.size())
    g_goal_letter = char(std::toupper(static_cast<unsigned char>(p[at + 12])));
  if (p.find("ani_sprani_ranking") != std::string::npos) {  // (after the name5 sign)
    if (g_in_race) EndRace("ranking");
    LogScreen("ranking");
  }
}

// ---------------------------------------------------------------- toast
class Toast final : public rex::ui::ImGuiDialog {
 public:
  explicit Toast(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}
  bool WantsContinuousRedraw() const override { return false; }
  void Show(std::string text) {
    std::lock_guard lock(mutex_);
    text_ = std::move(text);
    until_ = Now() + 2.5;
  }
 private:
  void OnDraw(ImGuiIO& io) override {
    std::string text;
    {
      std::lock_guard lock(mutex_);
      if (Now() > until_) return;
      text = text_;
    }
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * std::max(1.f, std::round(io.DisplaySize.y / 720.f * 1.5f));
    const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0, text.c_str());
    const float pad = 6 * std::max(1.f, io.DisplaySize.y / 720.f);
    const ImVec2 at(12, 12);
    auto* draw = ImGui::GetForegroundDrawList();
    draw->AddRectFilled(at, ImVec2(at.x + extent.x + 2 * pad, at.y + extent.y + 2 * pad), IM_COL32(0, 0, 0, 170), 4);
    draw->AddText(font, size, ImVec2(at.x + pad, at.y + pad), IM_COL32(255, 230, 90, 255), text.c_str());
  }
  std::mutex mutex_;
  std::string text_;
  double until_ = 0;
};
std::unique_ptr<Toast> g_toast;

// ---------------------------------------------------------------- screenshot
bool PngEncoder(CLSID* clsid) {
  UINT count = 0, bytes = 0;
  Gdiplus::GetImageEncodersSize(&count, &bytes);
  if (!bytes) return false;
  std::vector<uint8_t> buffer(bytes);
  auto* codecs = reinterpret_cast<Gdiplus::ImageCodecInfo*>(buffer.data());
  Gdiplus::GetImageEncoders(count, bytes, codecs);
  for (UINT i = 0; i < count; ++i)
    if (wcscmp(codecs[i].MimeType, L"image/png") == 0) { *clsid = codecs[i].Clsid; return true; }
  return false;
}

// The game window's client area (PrintWindow takes the composed picture
// without the window in front), as a PNG. Worker thread.
void SaveScreenshot(HWND hwnd, std::filesystem::path file, std::string toast) {
  RECT r{};
  if (!GetClientRect(hwnd, &r) || r.right <= 0 || r.bottom <= 0) {
    if (g_toast) g_toast->Show(toast);
    return;
  }
  HDC screen = GetDC(hwnd);
  HDC mem = CreateCompatibleDC(screen);
  HBITMAP bmp = CreateCompatibleBitmap(screen, r.right, r.bottom);
  HGDIOBJ old = SelectObject(mem, bmp);
  const bool ok = PrintWindow(hwnd, mem, 3);  // PW_CLIENTONLY | PW_RENDERFULLCONTENT
  SelectObject(mem, old);
  DeleteDC(mem);
  ReleaseDC(hwnd, screen);
  if (g_toast) g_toast->Show(toast);  // (after the capture, so it is not in the picture)
  if (ok) {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok) {
      CLSID png;
      if (PngEncoder(&png)) {
        Gdiplus::Bitmap image(bmp, nullptr);
        if (image.Save(file.c_str(), &png, nullptr) == Gdiplus::Ok)
          REXLOG_INFO("Player mark screenshot: {}", file.string());
      }
      Gdiplus::GdiplusShutdown(token);
    }
  }
  DeleteObject(bmp);
}

void Mark() {
  int n;
  std::string line;
  // What draws the picture: renderer and API, render scale, anti-aliasing.
  const auto flag = [](const char* name) { return rex::cvar::GetFlagByName(name); };
#if OUTRUN_HAS_NATIVE
  const bool native = outrun::NativeRendererActive();
#else
  const bool native = false;
#endif
  const std::string render = fmt::format(
      "{} {}, scale {}x, AA {}x", native ? "Plume" : "Classic", flag("gpu") == "vulkan" ? "Vulkan" : "D3D12",
      flag("resolution_scale").empty() ? "1" : flag("resolution_scale"),
      native ? (flag("gpu_native_msaa").empty() ? "4" : flag("gpu_native_msaa")) : std::string("-"));
  {
    std::lock_guard lock(g_mutex);
    n = ++g_marks;
    g_window_loading = true;  // (the traced frame is slow: not the session's lowest second)
    std::string stage_time;
    if (g_in_race && g_stage) stage_time = fmt::format(", {} into the stage", Clock2(g_race_time - g_stage_start));
    line = fmt::format("=== PLAYER MARK #{}: {}{}, fps {:.0f}, {} ===", n, WhereLocked(), stage_time, g_fps, render);
  }
  REXLOG_INFO("{}", line);
  rex::FlushLogging();
  const std::string toast = fmt::format("Bug mark #{} saved to the log", n);
  SYSTEMTIME t;
  GetLocalTime(&t);
  char stem[64];
  std::snprintf(stem, sizeof(stem), "outrun_mark_%02d_%04d%02d%02d_%02d%02d%02d", n, t.wYear, t.wMonth, t.wDay, t.wHour,
                t.wMinute, t.wSecond);
#if OUTRUN_HAS_NATIVE
  // The next frame, draw by draw, in its own file beside the log (renderer bugs).
  if (REXCVAR_GET(outrun_mark_trace) && !g_logs_dir.empty() &&
      !outrun::RequestMarkTrace((g_logs_dir / (std::string(stem) + "_frame.txt")).string()))
    REXLOG_INFO("Player mark: no frame trace (the classic renderer draws this game)");
#endif
  if (REXCVAR_GET(outrun_mark_screenshot) && g_window && !g_logs_dir.empty()) {
    if (HWND hwnd = static_cast<HWND>(g_window->GetNativeWindowHandle())) {
      std::thread(SaveScreenshot, hwnd, g_logs_dir / (std::string(stem) + ".png"), toast).detach();
      return;
    }
  }
  if (g_toast) g_toast->Show(toast);
}

// ---------------------------------------------------------------- startup header
std::string Flag(const char* name) { return rex::cvar::GetFlagByName(name); }

std::string OsVersion() {
  using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
  OSVERSIONINFOW v{sizeof(v)};
  if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")))
    fn(&v);
  std::string display;
  HKEY key;
  if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) == 0) {
    wchar_t buf[64];
    DWORD size = sizeof(buf), ubr = 0, ubr_size = sizeof(ubr);
    if (RegQueryValueExW(key, L"DisplayVersion", nullptr, nullptr, reinterpret_cast<BYTE*>(buf), &size) == 0)
      for (const wchar_t* c = buf; *c; ++c) display += char(*c);
    RegQueryValueExW(key, L"UBR", nullptr, nullptr, reinterpret_cast<BYTE*>(&ubr), &ubr_size);
    RegCloseKey(key);
    const bool eleven = v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000;
    return fmt::format("Windows {} {} (build {}.{})", eleven ? "11" : std::to_string(v.dwMajorVersion),
                       display.empty() ? "" : display, v.dwBuildNumber, ubr);
  }
  return fmt::format("Windows {}.{} build {}", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
}

std::string Narrow(const wchar_t* w) {
  std::string s;
  for (; *w; ++w) s += *w < 128 ? char(*w) : '?';
  return s;
}

// The adapter the runtime picks ("auto high-performance adapter") and its driver.
std::string Adapter() {
  IDXGIFactory6* factory = nullptr;
  if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return "unknown (no DXGI)";
  std::string out = "unknown";
  IDXGIAdapter1* adapter = nullptr;
  if (SUCCEEDED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) {
    DXGI_ADAPTER_DESC1 d{};
    adapter->GetDesc1(&d);
    LARGE_INTEGER umd{};
    std::string driver = "?";
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
      const unsigned a = HIWORD(umd.HighPart), b = LOWORD(umd.HighPart), c = HIWORD(umd.LowPart), e = LOWORD(umd.LowPart);
      driver = fmt::format("{}.{}.{}.{}", a, b, c, e);
      if (d.VendorId == 0x10DE) {  // NVIDIA's own numbering: last five digits
        const unsigned nv = (c % 10) * 10000 + e;
        driver += fmt::format(" (NVIDIA {}.{:02})", nv / 100, nv % 100);
      }
    }
    out = fmt::format("{} (vendor {:04X} device {:04X}, {} MB VRAM), driver {}", Narrow(d.Description), d.VendorId,
                      d.DeviceId, d.DedicatedVideoMemory / 1048576, driver);
    adapter->Release();
  }
  factory->Release();
  return out;
}

BOOL CALLBACK OnDevice(const DIDEVICEINSTANCEW* d, void* list) {
  static_cast<std::vector<std::string>*>(list)->push_back(Narrow(d->tszProductName));
  return DIENUM_CONTINUE;
}

std::string Controllers() {
  std::vector<std::string> names;
  IDirectInput8W* di = nullptr;
  if (SUCCEEDED(DirectInput8Create(GetModuleHandleW(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8W,
                                   reinterpret_cast<void**>(&di), nullptr))) {
    di->EnumDevices(DI8DEVCLASS_GAMECTRL, OnDevice, &names, DIEDFL_ATTACHEDONLY);
    di->Release();
  }
  int xinput = 0;
  for (DWORD i = 0; i < 4; ++i) {
    XINPUT_CAPABILITIES caps{};
    if (XInputGetCapabilities(i, 0, &caps) == ERROR_SUCCESS) ++xinput;
  }
  std::string out;
  for (const auto& n : names) out += (out.empty() ? "" : ", ") + n;
  if (out.empty()) out = "none";
  return fmt::format("{} ({} XInput slot{} in use)", out, xinput, xinput == 1 ? "" : "s");
}

constexpr const char* kRowNames[14] = {"Display Mode", "Render Scale", "Show FPS", "Window Resolution", "Image Filter",
                                       "Aspect Ratio", "Renderer", "Graphics API", "Texture Filtering",
                                       "Anti-aliasing", "Vivid Colors", "Contrast", "Black Level", "HUD"};
std::string RowValue(size_t row, unsigned v) {
  static const std::vector<std::vector<const char*>> kValues = {
      {"Windowed", "Fullscreen"}, {"1x", "2x", "3x"}, {"Off", "On"},
      {"1280x720", "1920x1080", "2560x1440", "3840x2160"}, {"Standard", "Sharpen", "FSR"}, {"16:9", "4:3"},
      {"Plume", "Classic"}, {"Direct3D 12", "Vulkan"}, {"Off", "2x", "4x", "8x", "16x"}, {"Off", "2x", "4x", "8x"},
      {"Off", "Low", "High"}, {"Low", "Normal", "High"}, {"Normal", "Deeper", "Deepest"}, {"Original", "Sharp", "HD", "Off"}};
  if (row < kValues.size() && v < kValues[row].size()) return kValues[row][v];
  return std::to_string(v);
}

void LogHeader(rex::Runtime* runtime) {
  const std::string build = kOutrunBuildLabel;
#if OUTRUN_HAS_NATIVE
  const bool native = Flag("gpu_native") != "false";
#else
  const bool native = false;
#endif
  const std::string api = Flag("gpu").empty() ? "auto" : Flag("gpu");
  std::string window = "?";
  if (g_window) {
    RECT r{};
    if (HWND h = static_cast<HWND>(g_window->GetNativeWindowHandle()); h && GetClientRect(h, &r))
      window = fmt::format("{}x{}", r.right, r.bottom);
  }
  DEVMODEW dm{};
  dm.dmSize = sizeof(dm);
  std::string monitor = "?";
  if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm))
    monitor = fmt::format("{}x{} @ {} Hz", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
  std::string settings;
  if (pc_menu::PrototypeActive()) {
    const auto v = pc_menu::CurrentSettings();
    for (size_t i = 0; i < v.size(); ++i) {
      if (i == 6) continue;  // (renderer: outrun.toml's gpu_native, above)
      settings += fmt::format("{}{}={}", settings.empty() ? "" : ", ", kRowNames[i], RowValue(i, v[i]));
    }
  } else {
    settings = "PC menu settings off (pc_settings_prototype = false)";
  }
  auto opt = [](const char* name) { const std::string v = Flag(name); return v.empty() ? std::string("-") : v; };
  REXLOG_INFO("==== OutRun Online Arcade PC {} ====", build);
  REXLOG_INFO("  OS:        {}", OsVersion());
  REXLOG_INFO("  Renderer:  {}, {} (gpu = {})", native ? "Plume (native)" : "Xenos (classic)",
              api == "vulkan" ? "Vulkan" : native || api == "d3d12" ? "Direct3D 12" : "auto API", api);
  REXLOG_INFO("  GPU:       {}", Adapter());
  REXLOG_INFO("  Display:   {}, window client {}, monitor {}", Flag("fullscreen") == "true" ? "fullscreen" : "windowed",
              window, monitor);
  REXLOG_INFO("  Settings:  {}", settings);
  REXLOG_INFO("  Present:   vsync={} frame_limit={} fsr_quality={} letterbox={} tearing={} log_level={}", opt("vsync"),
              opt("present_frame_limit"), opt("present_fsr_quality_mode"), opt("present_letterbox"),
              opt("outrun_present_allow_tearing"), opt("log_level"));
  REXLOG_INFO("  Audio:     mute={} steering_loops_off={} initial_volume={}", opt("audio_mute"),
              opt("outrun_audio_disable_steering_loops"), opt("outrun_audio_initial_volume"));
  REXLOG_INFO("  Controls:  mnk_mode={}, controllers: {}", opt("mnk_mode"), Controllers());
  REXLOG_INFO("  Network:   private_match={} local_sessions={}; F8 marks a bug in this log", opt("net_private_match"),
              opt("outrun_local_sessions"));
  (void)runtime;
}

LPTOP_LEVEL_EXCEPTION_FILTER g_previous_filter = nullptr;
LONG WINAPI OnCrash(EXCEPTION_POINTERS* e) {
  std::string where;
  {
    std::unique_lock lock(g_mutex, std::try_to_lock);  // (never wait in a crash)
    where = lock.owns_lock() ? WhereLocked() : "state locked";
  }
  REXLOG_CRITICAL("CRASH: unhandled exception {:08X} at {}; last known: {}",
                  e && e->ExceptionRecord ? e->ExceptionRecord->ExceptionCode : 0,
                  e && e->ExceptionRecord ? fmt::ptr(e->ExceptionRecord->ExceptionAddress) : nullptr, where);
  rex::FlushLogging();
  return g_previous_filter ? g_previous_filter(e) : EXCEPTION_CONTINUE_SEARCH;
}

std::string GuestPath(uint8_t* base, uint32_t attributes) {
  if (!attributes) return {};
  const uint32_t name = REX_LOAD_U32(attributes + 4);
  if (!name) return {};
  const uint16_t length = REX_LOAD_U16(name);
  const uint32_t buffer = REX_LOAD_U32(name + 4);
  if (!buffer || length == 0 || length > 512) return {};
  return std::string(reinterpret_cast<const char*>(base + buffer), length);
}
}  // namespace

// NtCreateFile through the dispatch table (the game reaches it indirectly):
// the path is noted, the call goes on unchanged.
extern "C" REX_FUNC(outrun_player_log_NtCreateFile) {
  const std::string path = GuestPath(base, ctx.r5.u32);
  g_nt_create_file(ctx, base);
  if (path.empty()) return;
  if (REXCVAR_GET(outrun_log_files)) REXLOG_INFO("File: {} -> {:08X}", path, ctx.r3.u32);
  if (ctx.r3.u32 == 0) OnFile(path);
}

void Startup(rex::Runtime* runtime, rex::ui::Window* window, rex::ui::ImGuiDrawer* drawer) {
  g_window = window;
  g_session_start = Now();
  {
    wchar_t exe[32768] = {};
    GetModuleFileNameW(nullptr, exe, 32768);
    g_logs_dir = std::filesystem::path(exe).parent_path() / "logs";
  }
  if (drawer) g_toast = std::make_unique<Toast>(drawer);
  LogHeader(runtime);
  g_previous_filter = SetUnhandledExceptionFilter(OnCrash);
  if (!runtime || !runtime->memory()) return;
  g_base = runtime->memory()->virtual_membase();
  uint8_t* base = g_base;
  PPCFunc*& slot = REX_LOOKUP_FUNC(base, 0x824C95E4);
  if (slot != __imp__NtCreateFile) {
    REXLOG_WARN("Player log: stage tracking off (file dispatch slot {})", slot ? "differs" : "empty");
    return;
  }
  g_nt_create_file = slot;
  slot = outrun_player_log_NtCreateFile;
}

void Shutdown() { g_toast.reset(); }

void OnSchedulerFrame(bool menu, uint32_t script) {
  (void)menu;
  std::lock_guard lock(g_mutex);
  std::string screen;
  if (script) {
    bool known = false;
    const char* s = ScreenOf(script, known);
    if (s) screen = s;
    else if (!known) screen = fmt::format("script {:08X}", script);
    else return;  // a dialog or the card manager: the screen under it stays
  } else {
    if (!g_in_race) return;  // (menus poll on some frames only)
    screen = "race";
  }
  const double now = Now();
  if (screen != g_candidate) {
    g_candidate = screen;
    g_candidate_since = now;
  }
  // Stable for a moment, so a frame without a menu poll is no screen change.
  if (now - g_candidate_since < (screen == "race" ? 1.0 : 0.25)) return;
  if (screen == g_screen) return;
  if (screen == "pause") { g_paused = true; LogScreen(screen); return; }
  g_paused = false;
  if (screen.rfind("multiplayer", 0) == 0) g_multiplayer = true;
  if (screen == "main menu" || screen == "title") {  // (a new choice of mode)
    g_multiplayer = false;
    g_time_attack = false;
  }
  // Back in the front end: the race is over.
  if (g_in_race && screen != "race") EndRace(screen == "title" ? "back to title" : "back to menus");
  LogScreen(screen);
}

void OnSwap() {
  const double now = Now();
  bool mark = false;
  {
    std::lock_guard lock(g_mutex);
    if (!g_first_swap) { g_first_swap = now; g_window_start = now; }
    ++g_swaps;
    ++g_window_frames;
    const double guest = GuestNow();
    if (g_last_guest && g_in_race && g_clock_running && !g_paused) g_race_time += std::clamp(guest - g_last_guest, 0.0, 0.5);
    g_last_guest = guest;
    g_last_swap = now;
    if (now - g_window_start >= 1.0) {
      g_fps = g_window_frames / (now - g_window_start);
      // Lowest second while driving (loading seconds left out).
      if (g_in_race && !g_paused && !g_window_loading && g_screen == "race") g_fps_min_race = std::min(g_fps_min_race, g_fps);
      g_window_start = now;
      g_window_frames = 0;
      g_window_loading = false;
    }
  }
  if (REXCVAR_GET(outrun_mark_key)) {  // F8 with the game window in front
    static bool was_down = false;
    const bool down = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (down && !was_down) {
      DWORD pid = 0;
      GetWindowThreadProcessId(GetForegroundWindow(), &pid);
      mark = pid == GetCurrentProcessId();
    }
    was_down = down;
  }
  if (outrun::test_script::markRequested.exchange(false)) mark = true;
  if (mark) Mark();
}

void OnSettingsAccepted(const std::array<unsigned, 14>& before, const std::array<unsigned, 14>& after) {
  std::string changed;
  for (size_t i = 0; i < after.size(); ++i)
    if (before[i] != after[i])
      changed += fmt::format("{}{} {} -> {}", changed.empty() ? "" : ", ", kRowNames[i], RowValue(i, before[i]),
                             RowValue(i, after[i]));
  REXLOG_INFO("Settings changed: {}", changed.empty() ? "nothing" : changed);
}

void OnWindowClose() {
  std::lock_guard lock(g_mutex);
  const double play = Now() - g_session_start;
  const double avg = g_first_swap && g_last_swap > g_first_swap ? g_swaps / (g_last_swap - g_first_swap) : 0;
  std::string stages;
  for (const auto& s : g_stages_played) stages += (stages.empty() ? "" : " ") + s;
  REXLOG_INFO("Session summary: played {}, {} race{}, {} stage entr{} ({} different: {}), fps avg {:.1f}, lowest "
              "driving second {}, {} player mark{}; last: {}",
              Clock2(play), g_races, g_races == 1 ? "" : "s", g_stage_entries, g_stage_entries == 1 ? "y" : "ies",
              g_stages_played.size(), stages.empty() ? "-" : stages, avg,
              g_fps_min_race < 1e8 ? fmt::format("{:.0f}", g_fps_min_race) : std::string("-"), g_marks,
              g_marks == 1 ? "" : "s", WhereLocked());
  rex::FlushLogging();
}

std::string Where() {
  std::lock_guard lock(g_mutex);
  return WhereLocked();
}
}  // namespace outrun::player_log
