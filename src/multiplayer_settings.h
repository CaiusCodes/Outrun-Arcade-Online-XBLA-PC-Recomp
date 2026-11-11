#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace outrun::multiplayer {
struct Preferences {
  std::string name;
  bool showPing = false;
  std::string hostAddress;
  bool hostAddressSpecified = false;
  // Local high-score initials (up to 4 letters or digits); blank follows the
  // online name above. Optional in the file (older files have none).
  std::string localName;
  bool operator==(const Preferences& other) const {return name==other.name && showPing==other.showPing && hostAddress==other.hostAddress && localName==other.localName;}
};
// Matches the game's 16-byte, zero-terminated gamertag buffer.
std::string ValidateName(std::string_view name);
std::string ValidateHostAddress(std::string_view address);
std::string ValidateLocalName(std::string_view name);
// The initials local high scores get: the local name, else the pool's local
// name for a pool online name, else the online name's first letters and
// digits, upper case (at most 4).
std::string HighScoreInitials(const Preferences& preferences);
std::optional<Preferences> LoadPreferences(const std::filesystem::path& path);
// Existing settings and explicitly configured names take precedence. A new
// default is saved once; failure does not overwrite unreadable/invalid settings.
Preferences LoadOrCreatePreferences(const std::filesystem::path& path,
                                    std::string_view configuredName,
                                    std::string_view legacyHost = {});
std::string RandomDefaultName();
void SavePreferences(const std::filesystem::path& path, const Preferences& preferences);
}
