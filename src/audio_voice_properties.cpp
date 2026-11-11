#include "generated/default/outrun_init.h"
#include "audio_voice_properties.h"
#include "audio_diagnostics.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <array>
#include <atomic>
#include <bit>
REXCVAR_DEFINE_BOOL(outrun_audio_property_trace,false,"OutRun",
                   "Diagnostic: observe downstream voice gain and setter, without changing playback")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
namespace {
struct Previous {uint32_t voice=0,left=0,right=0;uint64_t time=0;};
thread_local std::array<Previous,512> previous{};
std::atomic<unsigned> records{0};
}
REX_HOOK_RAW(sub_82475578){
  const auto caller=uint32_t(ctx.lr);
  if(REXCVAR_GET(outrun_audio_property_trace) && outrun::diagnostics::AudioTraceRecording() &&
     (caller==0x8211822C || caller==0x821182DC)){
    if(const auto gain=outrun::diagnostics::ReadVoiceGain(base,ctx.r3.u32,ctx.r4.u32)){
      const auto now=outrun::diagnostics::AudioTraceTime();
      auto& old=previous[(ctx.r3.u32>>4)%previous.size()];
      const bool same=old.voice==ctx.r3.u32;
      const bool changed=!same || old.left!=gain->leftBits || old.right!=gain->rightBits;
      const bool zeroTransition=same && ((old.left==0 && old.right==0)!=(gain->leftBits==0 && gain->rightBits==0));
      if(changed && (!same || now-old.time>=100 || zeroTransition)){
        old={ctx.r3.u32,gain->leftBits,gain->rightBits,now};
        const auto n=records.fetch_add(1);
        if(n<16000)REXLOG_INFO("[audio_property] ms={} voice={:08X} object={:08X} slot={:08X} setter={:08X} caller={:08X} requested_left={} requested_right={} actual_left={} actual_right={}",
            now,ctx.r3.u32,ctx.r24.u32,ctx.r31.u32,gain->target,caller,
            ctx.f29.f64,ctx.f28.f64,std::bit_cast<float>(gain->leftBits),std::bit_cast<float>(gain->rightBits));
        else if(n==16000)REXLOG_INFO("[audio_property] diagnostic limit reached; playback unchanged");
      }
    }
  }
  // Observe only: native code still owns synchronization, properties/results.
  __imp__sub_82475578(ctx,base);
}
