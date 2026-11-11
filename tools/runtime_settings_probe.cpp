#include <rex/cvar.h>
#include <windows.h>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
  wchar_t executable[32768]{};
  GetModuleFileNameW(nullptr, executable, 32768);
  const auto plugin = std::filesystem::path(executable).parent_path() / L"rexgpu-xenos.dll";
  if (!LoadLibraryW(plugin.c_str())) {
    std::cerr << "GPU plugin load failed: " << GetLastError() << '\n';
    return 1;
  }
  const auto report = [](const char* name) {
    const auto* flag = rex::cvar::GetFlagInfo(name);
    if (!flag) { std::cout << name << " | NOT REGISTERED\n"; return; }
    const auto lifecycle = flag->lifecycle == rex::cvar::Lifecycle::kHotReload ? "hot reload" :
        flag->lifecycle == rex::cvar::Lifecycle::kRequiresRestart ? "restart" : "initialization only";
    std::cout << name << " | " << flag->getter() << " | " << lifecycle << " | "
              << flag->description << '\n';
  };
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) report(argv[i]);
    return 0;
  }
  for (const char* name : {"fullscreen", "window_width", "window_height", "resolution",
                           "video_mode_width", "video_mode_height", "resolution_scale",
                           "draw_resolution_scale_x", "draw_resolution_scale_y",
                           "show_fps", "vsync", "bind_settings", "bind_debug"}) {
    report(name);
  }
}
