#include "generated/default/outrun_init.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>

REXCVAR_DEFINE_BOOL(outrun_audio_mute_steering_group,false,"OutRun",
                   "Test: suppress the sound group correlated with steering taps")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {
bool SteeringVoice(uint8_t* base,uint32_t object,uint32_t handle){
  if(!object || handle>=315)return false;
  constexpr uint32_t group=0x82810F28+3*608,objects=0x82573038;
  const auto descriptor=REX_LOAD_U32(group+28);
  if(!descriptor || uint64_t(descriptor)+20>0x100000000ull)return false;
  const auto count=REX_LOAD_U32(descriptor+8),layers=REX_LOAD_U32(descriptor+16);
  if(!layers || count>16 || uint64_t(layers)+count*204>0x100000000ull)return false;
  for(unsigned layer=0;layer<count;++layer){
    // C278 owns the current per-layer active handle at offset64. Match both
    // handle and object, so shared samples in other groups are unaffected.
    if(REX_LOAD_U32(group+64+layer*36)!=handle)continue;
    const auto sample=REX_LOAD_U32(layers+layer*204);
    if(sample<576 && REX_LOAD_U32(objects+sample*8)==object)return true;
  }
  return false;
}
}
REX_HOOK_RAW(sub_82118140){
  if(REXCVAR_GET(outrun_audio_mute_steering_group) && SteeringVoice(base,ctx.r3.u32,ctx.r4.u32)){
    ctx.f1.f64=-10000;ctx.f2.f64=-10000;
    static std::atomic<bool> reported{false};
    if(!reported.exchange(true))REXLOG_INFO("[steering_audio] Group3 active voice volume suppressed; other groups preserved");
  }
  __imp__sub_82118140(ctx,base);
}
