#pragma once
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>
#include <cstdint>
REXCVAR_DECLARE(bool,outrun_audio_isolate_bc);
namespace outrun::audio {
inline std::atomic<bool> bcMuted{true};
inline std::atomic<uint64_t> comparisonRevision{0};
void RefreshIsolation(uint8_t* base);
inline bool IsolationKey(uint16_t key,bool repeat){
  if(key!=0x75 || !REXCVAR_GET(outrun_audio_isolate_bc))return false;
  if(!repeat){const bool muted=!bcMuted.load();bcMuted.store(muted);++comparisonRevision;REXLOG_INFO("[audio_isolation] B+C comparison: muted={} (F6 toggles)",muted);}
  return true;
}
}
