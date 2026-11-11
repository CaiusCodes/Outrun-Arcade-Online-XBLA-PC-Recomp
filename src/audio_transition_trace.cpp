#include "generated/default/outrun_init.h"
#include <rex/hook.h>
#include "audio_diagnostics.h"
#include "audio_engine_attack.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <windows.h>
REXCVAR_DEFINE_BOOL(outrun_audio_engine_attack,false,"OutRun",
                   "Test:80ms engine loop startup envelope, full volume thereafter")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
namespace {
thread_local outrun::audio::EngineAttack engineAttack;
thread_local uint32_t volumeGroup=~0u,volumeLayer=~0u;
struct VolumeScope {
  uint32_t oldGroup=volumeGroup,oldLayer=volumeLayer;
  VolumeScope(uint32_t group,uint32_t layer){volumeGroup=group;volumeLayer=layer;}
  ~VolumeScope(){volumeGroup=oldGroup;volumeLayer=oldLayer;}
};
}
void outrun::audio::RecordEngineStart(uint32_t group,uint32_t layer,uint32_t object,int32_t handle){
  if(REXCVAR_GET(outrun_audio_engine_attack))engineAttack.Start(group,layer,object,handle,GetTickCount64());
}
REX_HOOK_RAW(sub_8210C7F8){
  // Capture the function's explicit arguments, not callee-saved registers
  // belonging to a different stack frame. Nesting restores previous scope.
  VolumeScope scope(ctx.r3.u32,ctx.r4.u32);
  uint32_t cache=0,oldCache=0,forced=0;
  if(REXCVAR_GET(outrun_audio_engine_attack) && volumeGroup==0 && volumeLayer>=2 && volumeLayer<16){
    const uint32_t state=0x82810F28+volumeLayer*36;
    if(engineAttack.NeedsUpdate(int32_t(REX_LOAD_U32(state+64)))){
      // C7F8 can skip unchanged target volumes. Force its normal setter while
      // fading, including one final full-volume update, then restore cache.
      cache=state+48;oldCache=REX_LOAD_U32(cache);
      forced=REX_LOAD_U32(state+36)^1u;REX_STORE_U32(cache,forced);
    }
  }
  __imp__sub_8210C7F8(ctx,base);
  if(cache && REX_LOAD_U32(cache)==forced)REX_STORE_U32(cache,oldCache);
}
REX_HOOK_RAW(sub_82118140){
  if((volumeGroup==0 || volumeGroup==1 || volumeGroup==3) && volumeLayer<16 && uint32_t(ctx.lr)==0x8210CA0C)
    outrun::diagnostics::LoopTransition("loop-volume",volumeGroup,volumeLayer,
        ctx.r3.u32,ctx.r4.s32,ctx.f1.f64,ctx.f2.f64);
  if(REXCVAR_GET(outrun_audio_engine_attack) && volumeGroup==0 && volumeLayer>=2 && volumeLayer<16 && uint32_t(ctx.lr)==0x8210CA0C){
    if(engineAttack.Apply(ctx.r3.u32,ctx.r4.s32,GetTickCount64(),ctx.f1.f64,ctx.f2.f64)){
      static thread_local bool reported=false;
      if(!reported){reported=true;REXLOG_INFO("[engine_attack]80ms source startup envelope active; full engine targets retained");}
      outrun::diagnostics::LoopTransition("loop-volume-envelope",volumeGroup,volumeLayer,
          ctx.r3.u32,ctx.r4.s32,ctx.f1.f64,ctx.f2.f64);
    }
  }
  __imp__sub_82118140(ctx,base);
}
REX_HOOK_RAW(sub_82117F08){
  if(REXCVAR_GET(outrun_audio_engine_attack))engineAttack.Stop(ctx.r4.s32);
  // At this exact caller C278 still has its group/layer in r25/r29.
  if(uint32_t(ctx.lr)==0x8210C400 && (ctx.r25.u32==0 || ctx.r25.u32==1 || ctx.r25.u32==3) && ctx.r29.u32<16)
    outrun::diagnostics::LoopTransition("loop-stop",ctx.r25.u32,ctx.r29.u32,
        ctx.r3.u32,ctx.r4.s32,0,0);
  __imp__sub_82117F08(ctx,base);
}
