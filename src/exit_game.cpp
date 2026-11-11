// Exit Game. The main menu's footer has the game's own B prompt "Return to
// Game Library" (TRC 0xC99563C8, now "Exit Game"). B asks the game's Yes/No
// box (message_boxes box 0xD5BAC512, now "Exit OutRun Online Arcade?"); Yes
// makes the main menu call native 66 with 52, which starts a thread at
// 0x82193BE0 that calls XamLoaderLaunchTitle("", 0) through 0x8227EB20, the
// console's return to the dashboard. On PC that left the game running, so the
// thread instead closes the window as its close button does: the runtime saves
// nothing on the way out (settings, names and the game's save are written when
// they change), terminates the title and ends the process ("Title terminated;
// hard-exiting process.").
#include "generated/default/outrun_init.h"
#include "exit_game.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>
#include <atomic>
#include <cstdlib>

namespace outrun::exit_game {
namespace {
std::atomic<rex::ui::Window*> window{nullptr};
std::atomic<bool> requested{false};
}  // namespace
void Initialize(rex::ui::Window* target) { window = target; }
void Shutdown() { window = nullptr; }
}  // namespace outrun::exit_game

REX_HOOK_RAW(sub_82193BE0) {
  using namespace outrun::exit_game;
  (void)base;
  ctx.r3.u64 = 0;
  if (requested.exchange(true)) return;
  auto* target = window.load();
  REXLOG_INFO("Exit Game: confirmed on the main menu; closing the game");
  if (!target) {
    // No window to close (should not happen): end as the runtime would.
    rex::FlushLogging();
    std::_Exit(0);
  }
  target->app_context().CallInUIThreadDeferred([target] { target->RequestClose(); });
}
