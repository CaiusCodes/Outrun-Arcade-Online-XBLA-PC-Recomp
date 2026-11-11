#pragma once
struct ImFont;
struct ImFontAtlas;
// A heavy italic face for overlay text that sits in the game's own screens
// (the game's lettering is a heavy italic sans): Segoe UI Black Italic from
// Windows, else its bold italic, else Arial Bold Italic.
namespace outrun::ui_font {
void Load(ImFontAtlas* atlas);  // from ReXApp::OnConfigureFonts, before the atlas is built
ImFont* Display();              // null when none of the faces was found
}
