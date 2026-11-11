#pragma once
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace outrun::pc_menu {
inline constexpr uint32_t kOptionsHash = 0x6B185A6B;
inline constexpr uint32_t kOriginalSize = 7410;
inline constexpr uint32_t kFirstSetting = 1024;
// Help & Options' sixth card, Check for Updates, calls the setter (native 64)
// with this id; pc_menu.cpp opens the update box (update_prompt.cpp).
inline constexpr uint32_t kUpdateSetting = 1100;
inline constexpr uint32_t kUpdateText = 0xFE504318, kUpdateDescription = 0xFE504319;
// The seventh card, Setup, sends this id; pc_menu.cpp asks to close the game
// and open the launcher's setup screen (update_prompt.cpp ShowSetup).
inline constexpr uint32_t kSetupSetting = 1101;
inline constexpr uint32_t kSetupText = 0xFE50431A, kSetupDescription = 0xFE50431E;
struct Text { uint32_t id; std::string_view value; };
// Port-owned IDs, checked against the original English text table by the research test.
inline constexpr Text kTexts[] = {
    {0xFE504300, "Display Mode"}, {0xFE504301, "Windowed"},
    {0xFE504302, "Fullscreen"}, {0xFE504303, "Render Scale (restart)"},
    {0xFE504304, "1x"}, {0xFE504305, "2x"},
    {0xFE504306, "3x"}, {0xFE504307, "Show FPS"},
    {0xFE504308, "Off"}, {0xFE504309, "On"},
    {0xFE50430A, "Window Resolution"}, {0xFE50430B, "1280 x 720"},
    {0xFE50430C, "1920 x 1080"}, {0xFE50430D, "2560 x 1440"},
    {0xFE50430E, "3840 x 2160"},
    {0xFE50430F, "Image Filter"}, {0xFE504310, "Standard"},
    {0xFE504311, "Sharpen"}, {0xFE504312, "FSR"},
    {0xFE504313, "Aspect Ratio (restart)"},
    {0xFE504314, "16:9"}, {0xFE504315, "4:3"},
    {0xFE504316, "Graphics"},
    {0xFE504317, "Display, resolution, rendering, filtering and anti-aliasing."},
    {0xFE50431B, "Graphics API (restart)"}, {0xFE50431C, "Direct3D 12"}, {0xFE50431D, "Vulkan"},
    {0xFE504330, "Texture Filtering"}, {0xFE504331, "4x"}, {0xFE504332, "8x"}, {0xFE504333, "16x"},
    {0xFE504334, "Anti-aliasing"}, {0xFE504335, "Vivid Colors"}, {0xFE504336, "Low"},
    {0xFE504337, "High"}, {0xFE504338, "Contrast"}, {0xFE504339, "Normal"},
    {0xFE50433D, "HUD"}, {0xFE50433E, "Original"}, {0xFE50433F, "Sharp"}, {0xFE504340, "HD"},
    {0xFE50433A, "Black Level"}, {0xFE50433B, "Deeper"}, {0xFE50433C, "Deepest"},
    {kUpdateText, "Check for Updates"},
    // pc_menu.cpp adds this copy's version on another line.
    {kUpdateDescription, "See whether a newer version of the PC port is available."},
    {kSetupText, "Setup"},
    {kSetupDescription, "Close the game and open the setup screen: game files, save folder, fullscreen and desktop shortcut."},
};
// Help & Options with a fifth card, Graphics, whose native rows are the PC
// display settings (Settings keeps the game's own five rows), and a sixth,
// Check for Updates, and a seventh, Setup.
struct OptionsScript {
  std::vector<uint8_t> bytes;
  // Where the patched script's shared row editor starts, and where native 47
  // (menu input) returns to inside it: menu_pointer reads the VM's position.
  uint32_t editorStart = 0, spinnerPc = 0;
};
// Returns a new script. Never modifies input; rejects every unrecognized version.
OptionsScript BuildOptionsScript(std::span<const uint8_t> original);
}
