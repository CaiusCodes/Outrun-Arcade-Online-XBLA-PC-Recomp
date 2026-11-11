#pragma once
namespace rex::ui { class ImGuiDrawer; }
namespace outrun::build_label {
// Small build label in the title screen's bottom-right corner (tools/BuildInfo.cmake).
void Initialize(rex::ui::ImGuiDrawer* drawer);
void Shutdown();
}
