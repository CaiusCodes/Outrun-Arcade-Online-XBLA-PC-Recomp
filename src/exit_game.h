#pragma once
namespace rex::ui { class Window; }
// The main menu's B prompt, the game's own "return to the Game Library"
// (relabelled Exit Game), closes the game on PC (exit_game.cpp).
namespace outrun::exit_game {
void Initialize(rex::ui::Window* window);
void Shutdown();
}
