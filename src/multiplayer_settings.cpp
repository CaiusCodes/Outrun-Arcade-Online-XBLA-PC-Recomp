#include "multiplayer_settings.h"
#include "default_player_names.h"
#include <windows.h>
#include <atomic>
#include <charconv>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>

namespace outrun::multiplayer {
std::string ValidateHostAddress(std::string_view address) {
  if(address.empty())return {};
  unsigned parts[4]{};size_t start=0;
  for(int i=0;i<4;++i) {
    const size_t end=i==3?address.size():address.find('.',start);
    if(end==std::string_view::npos||end==start)return "Enter a host IPv4 address, or leave it blank for automatic discovery.";
    if(end-start>1&&address[start]=='0')return "Use IPv4 numbers without leading zeroes.";
    const auto result=std::from_chars(address.data()+start,address.data()+end,parts[i]);
    if(result.ec!=std::errc{}||result.ptr!=address.data()+end||parts[i]>255)return "Enter a valid host IPv4 address.";
    start=end+1;
  }
  if(!parts[0]||parts[0]>=224||(parts[0]==169&&parts[1]==254))return "Use a LAN or virtual-LAN host address.";
  return {};
}
std::string ValidateName(std::string_view name) {
  if (name.empty() || name.size() > 15 || name.front() == ' ' || name.back() == ' ')
    return "Use 1-15 characters, with no spaces at either end.";
  for (unsigned char c : name)
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_'))
      return "Use letters, numbers, spaces, hyphens or underscores.";
  return {};
}
std::string ValidateLocalName(std::string_view name) {
  if (name.size() > 4) return "Use up to 4 letters or numbers for the local name.";
  for (unsigned char c : name)
    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
      return "Use only letters and numbers for the local name.";
  return {};
}
std::string HighScoreInitials(const Preferences& preferences) {
  // A pool name (first launch) has its own local name; others give their first four.
  if (preferences.localName.empty())
    for (size_t i = 0; i < kDefaultPlayerNames.size(); ++i)
      if (kDefaultPlayerNames[i] == preferences.name) return std::string(kDefaultLocalNames[i]);
  std::string text;
  for (unsigned char c : preferences.localName.empty() ? preferences.name : preferences.localName)
    if (text.size() < 4 && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
      text += char(c >= 'a' && c <= 'z' ? c - 32 : c);
  return text;
}
std::optional<Preferences> LoadPreferences(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) return {};
  if (std::filesystem::file_size(path) > 1024) throw std::runtime_error("Multiplayer settings file is too large");
  std::ifstream file(path);
  if (!file) throw std::runtime_error("Cannot read multiplayer settings");
  Preferences result;
  unsigned seen = 0;
  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line.front() == '#') continue;
    if (line.starts_with("player_name = \"") && line.back() == '"' && !(seen & 1)) {
      result.name = line.substr(15, line.size()-16);
      seen |= 1;
    } else if (line.starts_with("show_ping = ") && !(seen & 2)) {
      const auto value = line.substr(12);
      if (value != "true" && value != "false") throw std::runtime_error("Invalid Show Ping value");
      result.showPing = value == "true";
      seen |= 2;
    } else if(line.starts_with("host_address = \"") && line.back()=='"' && !(seen&4)) {
      result.hostAddress=line.substr(16,line.size()-17);result.hostAddressSpecified=true;seen|=4;
    } else if(line.starts_with("local_name = \"") && line.back()=='"' && !(seen&8)) {
      result.localName=line.substr(14,line.size()-15);seen|=8;
    } else throw std::runtime_error("Malformed multiplayer settings");
  }
  if (file.bad() || (seen&3) != 3 || !ValidateName(result.name).empty() || !ValidateHostAddress(result.hostAddress).empty() ||
      !ValidateLocalName(result.localName).empty())
    throw std::runtime_error("Invalid multiplayer settings");
  return result;
}
static void WritePreferences(const std::filesystem::path& path, const Preferences& preferences, bool replace) {
  if (const auto error = ValidateName(preferences.name); !error.empty()) throw std::runtime_error(error);
  if (const auto error = ValidateHostAddress(preferences.hostAddress); !error.empty()) throw std::runtime_error(error);
  if (const auto error = ValidateLocalName(preferences.localName); !error.empty()) throw std::runtime_error(error);
  const std::string bytes = "# OutRun multiplayer settings.\nplayer_name = \"" + preferences.name +
      "\"\nshow_ping = " + (preferences.showPing ? "true\n" : "false\n") +
      "host_address = \""+preferences.hostAddress+"\"\n" +
      "local_name = \"" + preferences.localName + "\"\n";
  static std::atomic<unsigned> sequence{};
  std::filesystem::path staging;
  HANDLE file = INVALID_HANDLE_VALUE;
  for (unsigned attempt = 0; attempt < 32; ++attempt) {
    staging = path;
    staging += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(sequence++);
    file = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                       FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (file != INVALID_HANDLE_VALUE) break;
    if (GetLastError() != ERROR_FILE_EXISTS) throw std::system_error(int(GetLastError()), std::system_category());
  }
  if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create multiplayer settings staging file");
  DWORD written = 0, error = ERROR_SUCCESS;
  if (!WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr)) error = GetLastError();
  else if (written != bytes.size()) error = ERROR_WRITE_FAULT;
  else if (!FlushFileBuffers(file)) error = GetLastError();
  if (!CloseHandle(file) && error == ERROR_SUCCESS) error = GetLastError();
  if (error == ERROR_SUCCESS && !MoveFileExW(staging.c_str(), path.c_str(),
          (replace ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH)) error = GetLastError();
  if (error != ERROR_SUCCESS) {
    DeleteFileW(staging.c_str());
    throw std::system_error(int(error), std::system_category(), "Save multiplayer settings");
  }
}
void SavePreferences(const std::filesystem::path& path, const Preferences& preferences) {
  WritePreferences(path, preferences, true);
}
std::string RandomDefaultName() {
  std::random_device entropy;
  std::uniform_int_distribution<size_t> pick(0, kDefaultPlayerNames.size()-1);
  return std::string(kDefaultPlayerNames[pick(entropy)]);
}
Preferences LoadOrCreatePreferences(const std::filesystem::path& path,
                                    std::string_view configuredName,std::string_view legacyHost) {
  if (auto existing = LoadPreferences(path)) return *existing;
  Preferences first{configuredName.empty() ? RandomDefaultName() : std::string(configuredName), false};
  first.hostAddress=std::string(legacyHost);first.hostAddressSpecified=true;
  try {
    // Two first launches must not replace each other's chosen identity.
    WritePreferences(path, first, false);
  } catch (...) {
    if (auto winner = LoadPreferences(path)) return *winner;
    throw;
  }
  return first;
}
}
