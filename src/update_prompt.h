#pragma once
namespace rex::ui { class ImGuiDrawer; class Window; }
// Help & Options > Check for Updates: the box that asks GitHub for the latest
// release and, when the player chooses Update, starts the launcher's update
// window and closes the game (update_prompt.cpp; docs/UPDATES.md).
namespace outrun::update_prompt {
void Initialize(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window);
// Opens the box and starts the check (any thread); the menus get no input
// until it is closed.
void Show();
// Help & Options > Setup: the same box asks "Close the game and open Setup?";
// Open Setup starts "OutRun Online Arcade.exe --manage" and closes the game.
void ShowSetup();
void Shutdown();
}
