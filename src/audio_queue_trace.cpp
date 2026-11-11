#include "generated/default/outrun_init.h"
#include "audio_diagnostics.h"
#include <rex/hook.h>

// Observe the caller that REQUESTS a queued sound, not just the later generic
// queue drain at82100684. The native queue still owns filtering and playback.
REX_HOOK_RAW(sub_82100780) {
  outrun::diagnostics::Sound(ctx.r3.u32,uint32_t(ctx.lr),"queue-request");
  __imp__sub_82100780(ctx,base);
}
