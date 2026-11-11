#include "generated/default/outrun_init.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include "audio_notification_isolation.h"

REXCVAR_DEFINE_BOOL(outrun_audio_initial_volume,false,"OutRun",
                   "Initialize engine and steering loop gain before stream start")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_audio_initial_volume_extended,false,"OutRun",
                   "Candidate: initialize traced secondary loops and panned one-shots before playback")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_audio_initial_volume_surface,false,"OutRun",
                   "Candidate: initialize traced off-road loop gains before playback")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_audio_initial_volume_oneshots,false,"OutRun",
                   "Candidate: apply requested attenuated one-shot gain before playback")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_HOOK_RAW(sub_82118BD0) {
  // Exact allocator call: r7 is the new slot, r25 remains the scheduler's
  // group (B58 saves/uses r27..31, not r25). The stream-start implementation
  // does not consume f1/f2; B58 normally applies them only after it returns.
  // All voices have already been created by the vtable+16 call at this point.
  const bool extended=REXCVAR_GET(outrun_audio_initial_volume_extended);
  const bool loop=ctx.r4.u32==1 &&
      (ctx.r25.u32==0 || ctx.r25.u32==3 || (extended && ctx.r25.u32==1) ||
       (REXCVAR_GET(outrun_audio_initial_volume_surface) &&
        (ctx.r25.u32==4 || ctx.r25.u32==5))) &&
      ctx.f1.f64==-10000.0 && ctx.f2.f64==-10000.0;
  // Traced events2147/105A request one silent speaker and one attenuated
  // speaker. Preserve their actual pan/gain before the first frame, rather
  // than starting at the voice's default gain and correcting it afterward.
  const bool spatial=extended && ctx.r4.u32==0 &&
      ((ctx.f1.f64==-10000.0)!=(ctx.f2.f64==-10000.0)) &&
      ctx.f1.f64>=-10000.0 && ctx.f1.f64<=0.0 &&
      ctx.f2.f64>=-10000.0 && ctx.f2.f64<=0.0;
  // The later trace also contains centered notification calls at -1188,
  // -888 and -588 on both speakers. These use the same allocator ordering.
  // Initialize their actual requested gain; do not suppress or fade the sound.
  const bool attenuated=REXCVAR_GET(outrun_audio_initial_volume_oneshots) &&
      ctx.r4.u32==0 && ctx.f1.f64>=-10000.0 && ctx.f1.f64<=0.0 &&
      ctx.f2.f64>=-10000.0 && ctx.f2.f64<=0.0 &&
      (ctx.f1.f64<0.0 || ctx.f2.f64<0.0);
  const bool isolated=outrun::audio::notificationIsolation && ctx.r4.u32==0 &&
      ctx.f1.f64==-10000.0 && ctx.f2.f64==-10000.0;
  if(uint32_t(ctx.lr)==0x82117C5C && ctx.r7.u32<315 &&
     (isolated || (REXCVAR_GET(outrun_audio_initial_volume) && (loop || spatial || attenuated)))) {
    auto volume=ctx;
    volume.r4=ctx.r7;
    // Separate register context preserves every original start argument.
    // Native code applies the normal target again after start as before.
    __imp__sub_82118140(volume,base);
    // Group3 used to be blocked outright. Initialize its real voices instead,
    // preserving the native tire/steering loops and their later gain updates.
    static thread_local unsigned reported=0;
    const unsigned bit=isolated?(1u<<19):spatial?(1u<<17):attenuated?(1u<<18):1u<<ctx.r25.u32;
    if(!(reported&bit)){
      reported|=bit;
      if(isolated)REXLOG_INFO("[audio_initial_volume] Diagnostic notification silence applied before stream start");
      else if(spatial)REXLOG_INFO("[audio_initial_volume] Panned one-shot requested gain applied before stream start");
      else if(attenuated)REXLOG_INFO("[audio_initial_volume] Attenuated one-shot requested gain applied before stream start");
      else REXLOG_INFO("[audio_initial_volume] Group{} gain initialized before stream start",ctx.r25.u32);
    }
  }
  __imp__sub_82118BD0(ctx,base);
}
