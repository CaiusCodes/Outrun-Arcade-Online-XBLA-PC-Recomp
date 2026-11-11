#include "generated/default/outrun_init.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include "keyboard_context.h"
#include "menu_pointer.h"
#include "controller_prompts.h"
#include "player_log.h"
#include <rex/cvar.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

REXCVAR_DEFINE_STRING(outrun_test_script, "", "OutRun/Diagnostics",
                      "Background renderer tests: scripted pad on the keyboard slot (test_script.h); "
                      "never set in a release")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(outrun_test_route, "", "OutRun/Diagnostics",
                      "Background renderer tests: the forks to take with outrun_test_script, L or R per fork "
                      "(e.g. RLRL); needs gpu_native_test_timer")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(outrun_thread_sample_log, false, "OutRun/Diagnostics",
                   "Log the scheduler thread's LR address for the read-only CPU sampler")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_HOOK_RAW(sub_82145EE0) {
  // One announcement per scheduler thread, only in an opted-in diagnostic
  // copy. The external observer reads this address; no sampling or waits are
  // inserted into guest execution. LR is a return address, not a sampled PC.
  static thread_local const bool announced = [&] {
    if (REXCVAR_GET(outrun_thread_sample_log))
      REXLOG_INFO("[thread_sample] pid={} tid={} lr_address={:X}", GetCurrentProcessId(), GetCurrentThreadId(),
                  reinterpret_cast<uintptr_t>(&ctx.lr));
    return true;
  }();
  (void)announced;
  // The scheduler polls guest controller state, then runs the scripts once.
  // Keep the preceding frame's context while it polls; observe which scripts
  // actually ask native 47 for menu input during this frame. Dormant menu
  // instances must not affect racing. No guest state or bytecode is modified.
  outrun::keyboard::menuPolled=false;
  __imp__sub_82145EE0(ctx,base);
  outrun::prompts::Refresh(ctx,base);
  const bool menu=outrun::keyboard::menuPolled;
  outrun::keyboard::menuActive=menu;
  {
    // Menus read input on some frames only, so the context flapped in the log
    // every few frames: it is logged once it has held for a second.
    static bool logged=false, pending=false, first=true;
    static uint64_t since=0;
    const uint64_t now=GetTickCount64();
    if(menu!=pending){pending=menu;since=now;}
    if((first || pending!=logged) && now-since>=1000) {
      first=false; logged=pending;
      REXLOG_INFO("Keyboard context: {}",logged ? "menu (Esc=Back, Enter=Accept)" : "race/title (Esc=Start)");
    }
  }
  outrun::player_log::OnSchedulerFrame(menu,outrun::pointer::polledScript.exchange(0));
  outrun::pointer::EndFrame();
}
