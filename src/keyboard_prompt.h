#pragma once
namespace rex::ui { class ImGuiDrawer; class Window; }
// The Ranking screen's high-score initials entry (keyboard_prompt.cpp).
namespace outrun::keyboard_prompt {
void Initialize(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window);
void Shutdown();
// Pad buttons (XINPUT_GAMEPAD_*, the left stick as the D-pad): the background
// tests' scripted pad when one is set, else the first connected controller
// (only while the window has focus).
unsigned short PadButtons(bool focused);
}
