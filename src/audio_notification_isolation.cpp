#include "generated/default/outrun_init.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include "audio_diagnostics.h"
#include "audio_engine_attack.h"
#include "audio_loop_suppression.h"
#include "audio_notification_isolation.h"
REXCVAR_DECLARE(bool,outrun_audio_trace);
REXCVAR_DEFINE_BOOL(outrun_audio_isolate_notifications,false,"OutRun",
                   "Diagnostic: silence traced traffic effects and checkpoint chime, retain voices")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_audio_isolate_checkpoint_voice,false,"OutRun",
                   "Diagnostic: silence checkpoint announcer only, retain traffic and chime")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
namespace {
bool Selected(uint32_t event){
  // Full event IDs from traffic queue caller8210276C; checkpoint effect4D
  // is separate from announcer16C/1E5. No sample-index masking here.
  switch(event){
    case 0x2147:case 0x105A:case 0x2148:case 0x1059:case 0x105B:case 0x2145:
    case 0x4D:return true;
    default:return false;
  }
}
}
REX_HOOK_RAW(sub_8210C040){
  const auto caller=outrun::audio::dispatchCaller;
  outrun::diagnostics::Sound(ctx.r3.u32,caller?caller:uint32_t(ctx.lr),"sample-start");
  const bool voice=ctx.r3.u32==0x16C || ctx.r3.u32==0x1E5;
  const bool isolate=uint32_t(ctx.lr)==0x8210C020 &&
      ((REXCVAR_GET(outrun_audio_isolate_notifications) && Selected(ctx.r3.u32)) ||
       (REXCVAR_GET(outrun_audio_isolate_checkpoint_voice) && voice));
  outrun::audio::NotificationIsolationScope scope(isolate);
  if(isolate)outrun::diagnostics::Sound(ctx.r3.u32,uint32_t(ctx.lr),"notification-isolation");
  __imp__sub_8210C040(ctx,base);
}
REX_HOOK_RAW(sub_82117B58){
  if(outrun::audio::SuppressSteeringLoopStart(ctx))return;
  if(outrun::audio::notificationIsolation && uint32_t(ctx.lr)==0x8210C168 && ctx.r4.u32==0){
    ctx.f1.f64=ctx.f2.f64=-10000.0;
  }
  outrun::diagnostics::Voice(ctx.r3.u32,ctx.r4.u32,uint32_t(ctx.lr),ctx.f1.f64,ctx.f2.f64,ctx.f3.f64,ctx.f4.f64,ctx.f5.f64);
  const bool loop=REXCVAR_GET(outrun_audio_trace) && uint32_t(ctx.lr)==0x8210C39C;
  const auto group=ctx.r25.u32,layer=ctx.r29.u32,object=ctx.r3.u32;
  const bool scheduler=uint32_t(ctx.lr)==0x8210C39C;
  const uint32_t sample=loop?REX_LOAD_U32(ctx.r30.u32):0;
  __imp__sub_82117B58(ctx,base);
  outrun::audio::RecordEngineStart(scheduler?group:~0u,layer,object,ctx.r3.s32);
  if(loop)outrun::diagnostics::LoopVoice(group,layer,sample,ctx.r3.s32);
}
