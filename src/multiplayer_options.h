#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
namespace rex::ui { class ImGuiDrawer; class Window; }
namespace outrun::multiplayer {
void Load(const std::filesystem::path& config);
void Initialize(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window);
void Shutdown();
void Open();
// Initials for local high scores: the last ones entered, else the online name's.
std::string HighScoreInitials();
// Remembers the initials entered at the Ranking screen (multiplayer-settings.toml).
void SetLocalName(const std::string& name);
}
