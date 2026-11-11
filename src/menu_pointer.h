#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <deque>
namespace outrun::pointer {
struct Click {float x,y;bool back;};
inline std::mutex mutex;
inline std::deque<Click> clicks;
inline std::deque<int> wheel;
inline std::atomic<bool> titleActive{false};
// The frontend script (name hash) that last read menu input this frame, for
// the player log's screen breadcrumbs (keyboard_context.cpp takes and clears it).
inline std::atomic<uint32_t> polledScript{0};
inline std::atomic<bool> controllerConnected{false};
// Active boot mode, never the uncommitted settings selection.
inline std::atomic<bool> fourByThree{false};
inline std::atomic<uint64_t> carFooterUntil{0};
// An overlay that owns the input (the restart prompt): menus see no buttons,
// clicks, hovers or wheel steps while it is set.
inline std::atomic<bool> inputHeld{false};
// The card menus' footer prompts menu_manager shows (controller_prompts.cpp):
// bit 0 Back, 1 Select, 2 Y, 3 X.
inline std::atomic<uint32_t> footerWidgets{0};
// The scripted test pointer (test_script.h), canvas coordinates; overlays use
// it in place of the mouse while a test script runs.
struct TestCursor {float x=-1,y=-1;};
inline std::atomic<TestCursor> testCursor{};
inline std::atomic<uint64_t> cancelEpoch{0};
// When the pause menu and the main menu last read the menu input
// (GetTickCount64): Help & Options opened from the pause menu means a race is
// under way (the update box asks the player to quit to the main menu first).
inline std::atomic<uint64_t> pausePolledAt{0}, mainMenuPolledAt{0};
// main_menu_options: where its shared row editor starts and where native 47
// returns inside it, original layout until pc_menu installs its patched copy.
inline std::atomic<uint32_t> optionsEditorStart{0x607}, optionsSpinnerPc{0xAA8};
void RegisterScript(uint32_t address,uint32_t hash);
uint32_t Input(uint32_t vm,uint8_t* base,uint32_t original);
void EndFrame();
void AdjustColour(uint32_t vm,uint32_t call,uint8_t* base);
void Clear();
void Push(Click c);
// New positions select only; stationary cursors never override other input.
void Move(float x,float y);
void Scroll(int steps);
// Only native card-hover transitions may omit intermediate animation waits.
bool FastHoverAnimation(uint32_t vm,uint8_t* base);
}
