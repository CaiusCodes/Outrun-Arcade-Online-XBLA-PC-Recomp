#pragma once
#include <array>
#include <filesystem>
namespace rex { class Runtime; namespace ui { class Window; } }
namespace outrun::pc_menu {
void InitializePrototype(rex::Runtime* runtime, rex::ui::Window* window);
void LoadPrototypeSettings(const std::filesystem::path& configPath);
void ShutdownPrototype();
// The committed PC settings (pc_settings_store.h order) and whether the PC
// menu is on, for the player log.
std::array<unsigned, 14> CurrentSettings();
bool PrototypeActive();
}
