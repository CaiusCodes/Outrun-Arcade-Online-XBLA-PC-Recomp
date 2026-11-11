#pragma once
// The player log (outrun_00N.log) made useful for bug reports: a startup
// header (build, OS, GPU, effective settings, controllers), game-state
// breadcrumbs logged on change (screen, stage, route, car, race end), the F8
// "mark a bug" hotkey (one visible line, a screenshot beside the log, a short
// toast) and a session summary when the window closes. Read-only: it never
// changes guest state or gameplay (docs/PLAYER_LOG.md).
#include <array>
#include <cstdint>
#include <string>
namespace rex { class Runtime; }
namespace rex::ui { class ImGuiDrawer; class Window; }
namespace outrun::player_log {
void Startup(rex::Runtime* runtime, rex::ui::Window* window, rex::ui::ImGuiDrawer* drawer);
void Shutdown();
// Once per guest scheduler frame (keyboard_context.cpp): whether a menu read
// input and which frontend script did (name hash, 0 when none).
void OnSchedulerFrame(bool menu, uint32_t script);
// At every guest Swap, with either renderer (native_gpu.cpp).
void OnSwap();
// PC menu Accept: the named values that changed (pc_menu.cpp).
void OnSettingsAccepted(const std::array<unsigned, 14>& before, const std::array<unsigned, 14>& after);
// Window closing (the runtime hard-exits right after): session summary.
void OnWindowClose();
// "stage 4 'Casino Town' route LRL, race 1:23.4" or the screen; for other modules' lines.
std::string Where();
}  // namespace outrun::player_log
