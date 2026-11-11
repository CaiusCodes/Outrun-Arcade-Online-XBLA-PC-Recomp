#include "ui_font.h"
#include <rex/logging.h>
#include <imgui.h>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace outrun::ui_font {
namespace {
ImFont* display = nullptr;
}
void Load(ImFontAtlas* atlas) {
  if (!atlas) return;
  const char* windows = std::getenv("WINDIR");
  const std::filesystem::path fonts = std::filesystem::path(windows ? windows : "C:\\Windows") / "Fonts";
  static const ImWchar ranges[] = {0x0020, 0x00FF, 0};
  for (const char* name : {"seguibli.ttf", "segoeuiz.ttf", "arialbi.ttf"}) {
    const auto path = fonts / name;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) continue;
    ImFontConfig config;
    config.OversampleH = config.OversampleV = 2;
    // Baked large and drawn smaller: the overlay atlas is filtered linearly.
    display = atlas->AddFontFromFileTTF(path.string().c_str(), 48.0f, &config, ranges);
    if (display) {
      REXLOG_INFO("Overlay display font: {}", name);
      return;
    }
  }
  REXLOG_WARN("No heavy italic Windows font found; overlays use the default font");
}
ImFont* Display() { return display; }
}  // namespace outrun::ui_font
