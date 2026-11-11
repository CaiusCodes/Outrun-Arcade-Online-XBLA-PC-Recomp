#pragma once
namespace rex::ui { class ImGuiDrawer; class Window; }
// "Restart now?" after Accept changed a setting that applies at the next
// launch (restart_prompt.cpp).
namespace outrun::restart_prompt {
void Initialize(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window);
// Opens the prompt (any thread); the menus get no input until it is answered.
void Show();
// From OnShutdown: starts the game again when the player chose Restart Now.
void Shutdown();
}
