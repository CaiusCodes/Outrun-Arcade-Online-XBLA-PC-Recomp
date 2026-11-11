#include "generated/default/outrun_init.h"
#include "gpu_wait_backoff.h"
#include <rex/hook.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <chrono>
#include <algorithm>

REXCVAR_DEFINE_BOOL(outrun_gpu_wait_backoff,false,"OutRun/Performance",
                   "Reduce CPU spinning in the measured OutRun GPU fence wait")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_gpu_wait_timing_log,false,"OutRun/Diagnostics",
                   "Report host wait duration and target completion after wake every 10 seconds")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_INT32(outrun_gpu_wait_sleep_us,500,"OutRun/Performance",
                    "Experimental host wait interval in microseconds (clamped 50-500)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REX_HOOK_RAW(sub_822999B8) {
  const auto caller=uint32_t(ctx.lr);
  const auto address=ctx.r3.u32;
  // Saved nonvolatile registers in this specific caller hold its fence target.
  const auto device=ctx.r31.u32;
  const auto target=ctx.r30.u32;
  __imp__sub_822999B8(ctx,base);
  // Only the sampled fence-completion loop in sub_8229AC08. Other uses of
  // this native helper keep their existing scheduling. Original code always
  // runs first, with all guest return values, registers and stores preserved.
  if(REXCVAR_GET(outrun_gpu_wait_backoff) && caller==0x8229ACA0) {
    static thread_local const bool announced=[] {
      REXLOG_INFO("[gpu_wait] completion-aware host backoff enabled for OutRun fence caller 8229ACA0");
      return true;
    }();
    (void)announced;
    bool waiting=ctx.r3.u32==1;
    if(waiting) {
      // Mirror 8229ACA8's unsigned, wrap-aware comparison. The helper's 1
      // means 'continue polling', not 'fence incomplete'. Completion may
      // have occurred inside it. Never delay the caller's completed path.
      const auto fence=REX_LOAD_U32(device+10896);
      const auto issued=REX_LOAD_U32(device+10908);
      const auto completed=REX_LOAD_U32(fence);
      waiting=uint32_t(issued-target)<uint32_t(issued-completed);
    }
    const auto stamp=waiting ? REX_LOAD_U32(address+20) : 0;
    const bool measure=waiting && REXCVAR_GET(outrun_gpu_wait_timing_log);
    outrun::gpu_wait::WaitSample sample;
    const auto sleep_us=uint32_t(std::clamp(REXCVAR_GET(outrun_gpu_wait_sleep_us),50,500));
    outrun::gpu_wait::Backoff(address,stamp,waiting,measure?&sample:nullptr,sleep_us);
    if(measure) {
      struct Stats {
        std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
        uint64_t sleeps=0,yields=0,ready_after_sleep=0,over_1ms=0,over_2ms=0;
        double total_us=0,max_us=0;
      };
      static thread_local Stats stats;
      if(sample.action==outrun::gpu_wait::Action::Sleep) {
        ++stats.sleeps;stats.total_us+=sample.elapsed_us;
        stats.max_us=std::max(stats.max_us,sample.elapsed_us);
        stats.over_1ms+=sample.elapsed_us>1000;stats.over_2ms+=sample.elapsed_us>2000;
        const auto fence=REX_LOAD_U32(device+10896);
        const auto issued=REX_LOAD_U32(device+10908);
        const auto completed=REX_LOAD_U32(fence);
        stats.ready_after_sleep+=uint32_t(issued-target)>=uint32_t(issued-completed);
      } else ++stats.yields;
      const auto now=std::chrono::steady_clock::now();
      if(now-stats.start>=std::chrono::seconds(10)) {
        REXLOG_INFO("[gpu_wait_timing] sleeps={} yields={} requested_us={} mean_us={:.2f} max_us={:.2f} over_1ms={} over_2ms={} target_ready_after_sleep={}",
          stats.sleeps,stats.yields,sleep_us,stats.sleeps?stats.total_us/stats.sleeps:0,
          stats.max_us,stats.over_1ms,stats.over_2ms,stats.ready_after_sleep);
        stats=Stats{};
      }
    }
  }
}
