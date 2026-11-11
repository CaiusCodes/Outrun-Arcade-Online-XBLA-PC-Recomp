#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace outrun::pc_menu {
using SettingsValues = std::array<unsigned, 14>; // fullscreen, render scale index, FPS, output size, spatial filter;
                                                // next launch: aspect, renderer (0 Plume, 1 Classic; read from older
                                                // files, never saved: outrun.toml's gpu_native), API (0 Direct3D 12, 1 Vulkan);
                                                // texture filtering (off..16x), anti-aliasing (off, 2x, 4x, 8x),
                                                // vivid colours, contrast, black level (choice indices);
                                                // HUD (0 Original, 1 Sharp, 2 HD, 3 Off)
// Values when a key is absent (older files): 4x filtering and anti-aliasing, normal contrast.
SettingsValues DefaultSettings();
// The colour choices as the SDK's color_* cvars take them.
extern const char* const kSaturation[3];
extern const char* const kContrast[3];
extern const char* const kBlackLevel[3];
// The nearest choice to a cvar's number.
unsigned NearestChoice(const char* const choices[3], double value);
// A dedicated, validated TOML file; original outrun.toml is never rewritten.
std::optional<SettingsValues> LoadSettings(const std::filesystem::path& path);
void SaveSettings(const std::filesystem::path& path, const SettingsValues& values);

class SettingsSession {
 public:
  explicit SettingsSession(SettingsValues initial = {}) : committed_(initial), candidate_(initial) {}
  unsigned Read(size_t row) const { return committed_.at(row); }
  // A staged value that Accept would change.
  bool Changed(size_t row) const { return candidate_.at(row) != committed_.at(row); }
  void Stage(size_t row, unsigned value) {
    candidate_.at(row) = value;
  }
  bool Accept(const std::filesystem::path& path, std::string& error) {
    if (candidate_ == committed_) return true;
    try { SaveSettings(path, candidate_); committed_ = candidate_; return true; }
    catch (const std::exception& e) { error = e.what(); candidate_ = committed_; return false; }
  }
 private:
  SettingsValues committed_, candidate_;
};
}
