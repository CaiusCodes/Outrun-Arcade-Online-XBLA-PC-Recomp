#pragma once
// Save & user data folder. Default: <resources>\userdata beside outrun.exe.
// The launcher's "Save & user data" Browse writes another absolute folder as
// one UTF-8 line to <resources>\userdata-location.txt (setup/portable/
// PortableGame.cs SetUserData). A missing, empty or unusable entry falls back
// to the default folder, so existing saves are never lost to a bad path.
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace outrun::userdata_location {
inline std::filesystem::path Resolve(const std::filesystem::path& resources) {
  const auto fallback = resources / "userdata";
  std::ifstream file(resources / "userdata-location.txt", std::ios::binary);
  if (!file) return fallback;
  std::string line;
  std::getline(file, line);
  if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF &&
      static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF)
    line.erase(0, 3);
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ' || line.back() == '\t'))
    line.pop_back();
  if (line.empty()) return fallback;
  std::filesystem::path chosen;
  try {
    chosen = std::filesystem::path(std::u8string(line.begin(), line.end()));
  } catch (...) {
    return fallback;
  }
  if (!chosen.is_absolute()) return fallback;
  std::error_code error;
  std::filesystem::create_directories(chosen, error);
  if (!std::filesystem::is_directory(chosen, error)) return fallback;
  return chosen;
}
}  // namespace outrun::userdata_location
