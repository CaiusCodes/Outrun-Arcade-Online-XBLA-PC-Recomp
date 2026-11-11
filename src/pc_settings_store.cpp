#include "pc_settings_store.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace outrun::pc_menu {
namespace {
constexpr unsigned kSamples[] = {1, 2, 4, 8};
void Validate(const SettingsValues& v) {
  if (v[0] > 1 || v[1] > 2 || v[2] > 1 || v[3] > 3 || v[4] > 2 || v[5] > 1 || v[6] > 1 || v[7] > 1 || v[8] > 4 ||
      v[9] > 3 || v[10] > 2 || v[11] > 2 || v[12] > 2 || v[13] > 3)
    throw std::runtime_error("Invalid PC settings value");
}
std::string Trim(std::string s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return s.substr(first, s.find_last_not_of(" \t\r\n")-first+1);
}
[[noreturn]] void WinError(const char* action, DWORD code) {
  throw std::system_error(int(code), std::system_category(), action);
}
}
const char* const kSaturation[3] = {"1.0", "1.15", "1.3"};
const char* const kContrast[3] = {"0.9", "1.0", "1.15"};
const char* const kBlackLevel[3] = {"0.0", "0.03", "0.06"};
SettingsValues DefaultSettings() {
  SettingsValues v{};
  v[8] = 2;
  v[9] = 2;
  v[11] = 1;
  return v;
}
unsigned NearestChoice(const char* const choices[3], double value) {
  unsigned best = 0;
  for (unsigned i = 1; i < 3; ++i)
    if (std::abs(std::atof(choices[i])-value) < std::abs(std::atof(choices[best])-value)) best = i;
  return best;
}
std::optional<SettingsValues> LoadSettings(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) return {};
  if (std::filesystem::file_size(path) > 4096) throw std::runtime_error("PC settings file is too large");
  std::ifstream file(path);
  if (!file) throw std::runtime_error("Cannot read PC settings");
  SettingsValues result = DefaultSettings();
  unsigned seen = 0;
  std::string line;
  while (std::getline(file, line)) {
    const auto comment = line.find('#');
    if (comment != std::string::npos) line.resize(comment);
    line = Trim(line);
    if (line.empty()) continue;
    const auto equals = line.find('=');
    if (equals == std::string::npos) throw std::runtime_error("Malformed PC settings");
    const auto key = Trim(line.substr(0, equals));
    const auto value = Trim(line.substr(equals+1));
    size_t index;
    if (key == "fullscreen") index = 0;
    else if (key == "resolution_scale") index = 1;
    else if (key == "show_fps") index = 2;
    else if (key == "outrun_window_resolution") index = 3;
    else if (key == "outrun_present_filter") index = 4;
    else if (key == "outrun_aspect_ratio") index = 5;
    else if (key == "gpu_native") index = 6;
    else if (key == "gpu") index = 7;
    else if (key == "gpu_native_texture_filtering") index = 8;
    else if (key == "gpu_native_msaa") index = 9;
    else if (key == "color_saturation") index = 10;
    else if (key == "color_contrast") index = 11;
    else if (key == "color_black_level") index = 12;
    else if (key == "outrun_hud") index = 13;
    else if (key == "presentation_vsync") index = 31;
    else throw std::runtime_error("Unknown PC settings field");
    if (seen & (1u << index)) throw std::runtime_error("Duplicate PC settings field");
    seen |= 1u << index;
    if (index == 1) {
      if (value != "1" && value != "2" && value != "3") throw std::runtime_error("Invalid resolution scale");
      result[index] = unsigned(value[0]-'1');
    } else if (index == 3) {
      if (value.size()!=1 || value[0]<'0' || value[0]>'3') throw std::runtime_error("Invalid window resolution");
      result[3]=unsigned(value[0]-'0');
    } else if (index == 4) {
      if (value.size()!=1 || value[0]<'0' || value[0]>'2') throw std::runtime_error("Invalid spatial filter");
      result[4]=unsigned(value[0]-'0');
    } else if (index == 5) {
      if(value!="0" && value!="1") throw std::runtime_error("Invalid aspect ratio");
      result[5]=unsigned(value[0]-'0');
    } else if (index == 6) {
      if (value != "true" && value != "false") throw std::runtime_error("Invalid renderer");
      result[6] = value == "true" ? 0 : 1;
    } else if (index == 7) {
      if (value != "\"d3d12\"" && value != "\"vulkan\"") throw std::runtime_error("Invalid graphics API");
      result[7] = value == "\"vulkan\"" ? 1 : 0;
    } else if (index == 8) {
      if (value.size()!=1 || value[0]<'0' || value[0]>'4') throw std::runtime_error("Invalid texture filtering");
      result[8]=unsigned(value[0]-'0');
    } else if (index == 9) {
      const auto at = std::find(std::begin(kSamples), std::end(kSamples), unsigned(std::atoi(value.c_str())));
      if (at == std::end(kSamples)) throw std::runtime_error("Invalid anti-aliasing");
      result[9] = unsigned(at-std::begin(kSamples));
    } else if (index == 13) {
      if (value.size()!=1 || value[0]<'0' || value[0]>'3') throw std::runtime_error("Invalid HUD setting");
      result[13]=unsigned(value[0]-'0');
    } else if (index >= 10 && index <= 12) {
      // Numbers as the cvars take them; a hand-edited value takes the nearest choice.
      char* end = nullptr;
      const double number = std::strtod(value.c_str(), &end);
      if (value.empty() || *end) throw std::runtime_error("Invalid colour setting");
      result[index] = NearestChoice(index == 10 ? kSaturation : index == 11 ? kContrast : kBlackLevel, number);
    } else {
      if (value != "true" && value != "false") throw std::runtime_error("Invalid PC settings switch");
      // Older prototypes saved host VSync. Validate but discard this removed field.
      if (index < result.size()) result[index] = value == "true";
    }
  }
  if (file.bad() || (seen & 7) != 7) throw std::runtime_error("Incomplete PC settings file");
  if (!(seen & 8)) result[3] = result[1] == 0 ? 0 : result[1]+1; // Preserve old window size.
  Validate(result);
  return result;
}
void SaveSettings(const std::filesystem::path& path, const SettingsValues& values) {
  Validate(values);
  std::ostringstream contents;
  contents << "# OutRun PC display settings.\n"
           << "fullscreen = " << (values[0] ? "true" : "false") << '\n'
           << "resolution_scale = " << values[1]+1 << '\n'
           << "show_fps = " << (values[2] ? "true" : "false") << '\n'
           << "outrun_window_resolution = " << values[3] << '\n'
           << "outrun_present_filter = " << values[4] << '\n'
           << "outrun_aspect_ratio = " << values[5] << '\n'
           << "gpu = " << (values[7] ? "\"vulkan\"" : "\"d3d12\"") << '\n'
           << "gpu_native_texture_filtering = " << values[8] << '\n'
           << "gpu_native_msaa = " << kSamples[values[9]] << '\n'
           << "color_saturation = " << kSaturation[values[10]] << '\n'
           << "color_contrast = " << kContrast[values[11]] << '\n'
           << "color_black_level = " << kBlackLevel[values[12]] << '\n'
           << "outrun_hud = " << values[13] << '\n';
  const auto bytes = contents.str();
  static std::atomic<unsigned> sequence{};
  std::filesystem::path staging;
  HANDLE file = INVALID_HANDLE_VALUE;
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    staging = path;
    staging += L".tmp-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(sequence++);
    file = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file != INVALID_HANDLE_VALUE) break;
    const auto error = GetLastError();
    if (error != ERROR_FILE_EXISTS) WinError("Create PC settings", error);
  }
  if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create PC settings staging file");
  DWORD written = 0;
  DWORD error = ERROR_SUCCESS;
  if (!WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr)) error = GetLastError();
  else if (written != bytes.size()) error = ERROR_WRITE_FAULT;
  else if (!FlushFileBuffers(file)) error = GetLastError();
  if (!CloseHandle(file) && error == ERROR_SUCCESS) error = GetLastError();
  if (error == ERROR_SUCCESS &&
      !MoveFileExW(staging.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    error = GetLastError();
  }
  if (error != ERROR_SUCCESS) {
    DeleteFileW(staging.c_str()); // Only this invocation's exclusively created staging file.
    WinError("Save PC settings", error);
  }
}
}
