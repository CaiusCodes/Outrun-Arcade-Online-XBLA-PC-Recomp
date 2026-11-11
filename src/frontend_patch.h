#pragma once
#include <cstdint>
#include <span>
#include <vector>
#include <filesystem>
namespace outrun::frontend {
inline constexpr uint32_t kMainMenuHash = 0x7FE653EA;
inline constexpr uint32_t kMainMenuSize = 2734;
std::vector<uint8_t> BuildMainMenuScript(std::span<const uint8_t> original);
inline constexpr uint32_t kPauseHash = 0x0D4B66A5;
inline constexpr uint32_t kPauseSize = 3902;
std::vector<uint8_t> BuildPauseScript(std::span<const uint8_t> original);
inline constexpr uint32_t kAutoloadHash = 0xDAFA80FC;
inline constexpr uint32_t kAutoloadSize = 4667;
// Fail closed when save content/metadata already exists or cannot be inspected.
bool CanCreateFirstGameData(const std::filesystem::path& userData);
std::vector<uint8_t> BuildAutoloadScript(std::span<const uint8_t> original, bool createFirstData=true);
inline constexpr uint32_t kLeaderboardHash = 0x40CAC580;
inline constexpr uint32_t kLeaderboardSize = 7562;
std::vector<uint8_t> BuildLeaderboardScript(std::span<const uint8_t> original);
}
