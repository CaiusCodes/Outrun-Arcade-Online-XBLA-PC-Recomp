#pragma once
namespace rex::ui { class ImGuiDrawer; }
namespace outrun::menu_affordances {
void Initialize(rex::ui::ImGuiDrawer* drawer);
void Shutdown();
}
