#include "generated/default/outrun_init.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include "audio_isolation.h"
// Failed isolation experiments retained only for research tests. This source
// is excluded from OUTRUN_SOURCES and must not be added to release builds.
REXCVAR_DEFINE_BOOL(outrun_audio_isolate_bc,false,"OutRun",
                   "Research comparison: silence samples 96/98, F6 restores/reapplies")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_BOOL(outrun_audio_mute_sample28,false,"OutRun",
                   "Diagnostic: silence candidate A (sample 28) only")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

void outrun::audio::RefreshIsolation(uint8_t* base) {
  if(!REXCVAR_GET(outrun_audio_isolate_bc))return;
  static uint64_t applied=0;
  const auto revision=comparisonRevision.load();if(applied==revision)return;
  applied=revision;
  constexpr uint32_t groups=0x82810F28;
  for(unsigned group=0;group<17;++group){
    const auto state=groups+group*608,descriptor=REX_LOAD_U32(state+28);
    if(!descriptor || uint64_t(descriptor)+20>0x100000000ull)continue;
    const auto count=REX_LOAD_U32(descriptor+8),layers=REX_LOAD_U32(descriptor+16);
    if(count>16 || !layers || uint64_t(layers)+count*204>0x100000000ull)continue;
    for(unsigned layer=0;layer<count;++layer){
      const auto sample=REX_LOAD_U32(layers+layer*204);
      // The native volume setter otherwise skips unchanged cached values.
      // Use C278's own startup sentinel so F6 reapplies/restores the normal
      // computed volume on the guest audio thread, even at constant RPM.
      if(sample==96 || sample==98)REX_STORE_U32(state+48+layer*36,0xEFFFFFFF);
    }
  }
}

REX_HOOK_RAW(sub_82118140) {
  if(REXCVAR_GET(outrun_audio_isolate_bc) && outrun::audio::bcMuted.load() && ctx.r3.u32!=0) {
    constexpr uint32_t voices=0x82573038;
    const auto b=REX_LOAD_U32(voices+96*8),c=REX_LOAD_U32(voices+98*8);
    const unsigned match=(b && ctx.r3.u32==b)?1:(c && ctx.r3.u32==c)?2:0;
    if(match){
      ctx.f1.f64=-10000;ctx.f2.f64=-10000;
      static std::atomic<unsigned> reported{0};
      if(!(reported.fetch_or(match)&match))REXLOG_INFO("[audio_isolation] Candidate {} volume suppressed",match==1?"B/sample96":"C/sample98");
    }
  }
  // C278 resolves loop samples through this pointer table. Match the current
  // object rather than a session-specific voice address or a whole sound bus.
  // Native voice allocation, playback, pitch and lifetime still run normally;
  // the volume updates alone use the game's existing -10000 silence level.
  if(REXCVAR_GET(outrun_audio_mute_sample28) && ctx.r3.u32!=0) {
    constexpr uint32_t sampleObject=0x82573038+28*8;
    const auto target=REX_LOAD_U32(sampleObject);
    if(target && ctx.r3.u32==target) {
      ctx.f1.f64=-10000.0;ctx.f2.f64=-10000.0;
      static std::atomic<bool> reported{false};
      if(!reported.exchange(true))REXLOG_INFO("[audio_isolation] Candidate A/sample28 volume suppressed; other sample volumes unchanged");
    }
  }
  __imp__sub_82118140(ctx,base);
}
