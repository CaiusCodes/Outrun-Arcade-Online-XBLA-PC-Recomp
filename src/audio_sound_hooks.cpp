#include "generated/default/outrun_init.h"
#include <rex/hook.h>
#include "audio_diagnostics.h"
#include "audio_pcm_capture.h"
#include "audio_loop_suppression.h"
#include "audio_engine_attack.h"
#include "audio_notification_isolation.h"
#include <windows.h>
#include <stdexcept>
#include <rex/cvar.h>
#include <atomic>
REXCVAR_DECLARE(bool,outrun_audio_trace);
namespace {
using outrun::audio::dispatchCaller;
struct CallerScope {
  uint32_t old;
  explicit CallerScope(uint32_t caller):old(dispatchCaller){dispatchCaller=caller;}
  ~CallerScope(){dispatchCaller=old;}
};
}
// Verified dispatch and sample-start paths; logging only, original calls always
// run unchanged. Sample IDs remain numeric until the actual trace identifies them.
REX_HOOK_RAW(sub_8210BF78) {
  CallerScope caller(uint32_t(ctx.lr));
  outrun::diagnostics::Sound(ctx.r3.u32,uint32_t(ctx.lr),"dispatch");
  __imp__sub_8210BF78(ctx,base);
}
REX_HOOK_RAW(sub_8210BE58) {
  outrun::diagnostics::Parameter(ctx.r3.u32,ctx.r4.u32,ctx.r5.u32,uint32_t(ctx.lr));
  __imp__sub_8210BE58(ctx,base);
}
REX_HOOK_RAW(sub_8210C278) {
  __imp__sub_8210C278(ctx,base);
  static std::atomic<bool> mapped{false};
  if(!outrun::diagnostics::AudioTraceRecording() || mapped.exchange(true))return;
  // Read the same 17 groups and layer descriptors used by C278. Each group's
  // fixed 608-byte state has room for at most 16 36-byte layer states.
  constexpr uint32_t groups=0x82810F28,voices=0x82573038;
  for(uint32_t group=0;group<17;++group) {
    const auto descriptor=REX_LOAD_U32(groups+group*608+28);
    if(!descriptor || uint64_t(descriptor)+20>0x100000000ull)continue;
    const auto count=REX_LOAD_U32(descriptor+8),layers=REX_LOAD_U32(descriptor+16);
    if(count>16 || !layers || uint64_t(layers)+count*204>0x100000000ull)continue;
    for(uint32_t layer=0;layer<count;++layer) {
      const auto sample=REX_LOAD_U32(layers+layer*204);
      // The asset loader bounds the table to 576 sample entries.
      if(sample>=576)continue;
      outrun::diagnostics::LoopMapping(group,layer,sample,REX_LOAD_U32(voices+sample*8));
    }
  }
}
REX_HOOK_RAW(__imp__XAudioSubmitRenderDriverFrame) {
  // Resolve the exact original export, just as the project LAN import wrapper
  // does. Preserve all registers/results and always submit the original frame.
  static auto original=reinterpret_cast<PPCFunc*>(GetProcAddress(GetModuleHandleW(L"rexruntime.dll"),"__imp__XAudioSubmitRenderDriverFrame"));
  if(!original || original==&__imp__XAudioSubmitRenderDriverFrame)throw std::runtime_error("Original audio submission export unavailable");
  const auto driver=ctx.r3.u32,address=ctx.r4.u32;
  // Snapshot before the original call may mutate registers. The memory buffer
  // is the same validated contract the original SDK reads (no new guest pointers).
  outrun::diagnostics::CapturePCM(driver,address,base);
  original(ctx,base);
}
