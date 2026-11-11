// The menus' "Connecting..." panel: scripts ask menu_manager to slide it in
// (message F6C608BC) while they wait on Xbox LIVE work, and out again
// (6ACA1888). On PC that work is local and done at once, so the panel only
// flashed in and out for about a second on the leaderboards, Host LAN Match
// and the other multiplayer screens (user report). Both messages are
// answered here without reaching menu_manager, so the panel never shows.
//
// Native 4 (sub_821920B8) sends a script message and waits: its first call
// sends and parks the VM (VM+36 = 2) until the receiver replies, then copies
// the reply to the call's result. Answering on the first call with an
// integer 0, as a reply would, lets the script carry straight on.
#include "generated/default/outrun_init.h"
#include <rex/hook.h>

namespace {
constexpr uint32_t kMenuManager = 0x1C39026A;
constexpr uint32_t kShowBusy = 0xF6C608BC, kHideBusy = 0x6ACA1888;
}

REX_HOOK_RAW(sub_821920B8) {
  const uint32_t call = ctx.r4.u32;
  if (REX_LOAD_U8(call + 4) && REX_LOAD_U32(call + 16) >= 2) {
    const uint32_t args = REX_LOAD_U32(call + 12);
    const uint32_t message = REX_LOAD_U32(args + 4);
    if (REX_LOAD_U32(args) == kMenuManager && (message == kShowBusy || message == kHideBusy)) {
      REX_STORE_U32(call + 20, 0);
      REX_STORE_U32(call + 24, 2);  // VM integer
      return;
    }
  }
  __imp__sub_821920B8(ctx, base);
}
