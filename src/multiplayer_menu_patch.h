#pragma once
#include <cstdint>
#include <span>
#include <vector>
namespace outrun::multiplayer {
inline constexpr uint32_t kScriptHash = 0xD6061DDF;
inline constexpr uint32_t kScriptSize = 1748;
inline constexpr uint32_t kTitleId = 0xFE504320;
inline constexpr uint32_t kDescriptionId = 0xFE504321;
inline constexpr uint32_t kOpenCommand = 0x7F01;
std::vector<uint8_t> BuildMenuScript(std::span<const uint8_t> original);
inline constexpr uint32_t kLobbyHash = 0x5D9F7CC3;
inline constexpr uint32_t kLobbySize = 11386;
std::vector<uint8_t> BuildLobbyScript(std::span<const uint8_t> original);
}
