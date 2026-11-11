#include "generated/default/outrun_init.h"
#include "audio_loop_suppression.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <atomic>

REXCVAR_DEFINE_BOOL(outrun_audio_disable_steering_loops,false,"OutRun",
                   "Force-block the steering-correlated loop group (older setting; outrun_audio_steering_loops is preferred)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(outrun_audio_steering_loops,"auto","OutRun",
                     "Steering loop sounds: auto (play with the native renderer, blocked with the classic Xenos renderer, "
                     "where they popped), on, off")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_audio_disable_throttle_layer,false,"OutRun",
                   "Diagnostic: prevent throttle-correlated group0 layer2 playback")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_audio_disable_throttle_layers,false,"OutRun",
                   "Diagnostic: block group0 layers2..15; may thin engine audio")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_UINT32(outrun_audio_throttle_layer_mask,0,"OutRun",
                     "Diagnostic: selected group0 layers2..15 to block; base layers protected")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// The steering loops (group 3) popped in October with the classic renderer;
// with the native one they play cleanly (user-tested 11 Nov), so "auto"
// blocks them only when gpu_native = false. "on"/"off" force it, and the older
// outrun_audio_disable_steering_loops = true still blocks under auto.
bool outrun::audio::SteeringLoopsBlocked() {
  const std::string& mode = REXCVAR_GET(outrun_audio_steering_loops);
  if (mode == "off") return true;
  if (mode == "on") return false;
  return REXCVAR_GET(outrun_audio_disable_steering_loops) || rex::cvar::GetFlagByName("gpu_native") == "false";
}
std::string outrun::audio::SteeringLoopsSetting() {
  const std::string mode = REXCVAR_GET(outrun_audio_steering_loops);
  const char* renderer = rex::cvar::GetFlagByName("gpu_native") == "false" ? "classic renderer" : "native renderer";
  return mode + " -> " + (SteeringLoopsBlocked() ? "blocked" : "playing") +
         (mode == "on" || mode == "off" ? std::string() : std::string(" (") + renderer +
             (REXCVAR_GET(outrun_audio_disable_steering_loops) ? ", outrun_audio_disable_steering_loops = true" : "") + ")");
}

bool outrun::audio::SuppressSteeringLoopStart(PPCContext& ctx) {
  // Only the verified C278 loop allocator call has r25=group/r29=layer.
  // Do not match shared sample objects in unrelated playback paths.
  if(uint32_t(ctx.lr)!=0x8210C39C || ctx.r29.u32>=16 ||
     ctx.r4.u32!=0x800)return false;
  const bool steering=ctx.r25.u32==3 && SteeringLoopsBlocked();
  // Trace maps group0/layer2 to sample28. Suppress this layer only, not the
  // entire engine group or every use of the shared sample in other groups.
  const bool throttle=REXCVAR_GET(outrun_audio_disable_throttle_layer) &&
                      ctx.r25.u32==0 && ctx.r29.u32==2;
  // W transitions restart this family together. Keep the two base layers
  // (0/1) and all other groups; this broad isolation is NOT a release default.
  const bool throttleFamily=REXCVAR_GET(outrun_audio_disable_throttle_layers) &&
                            ctx.r25.u32==0 && ctx.r29.u32>=2;
  const bool selected=ctx.r25.u32==0 &&
      ((REXCVAR_GET(outrun_audio_throttle_layer_mask)&0xfffcu)&(1u<<ctx.r29.u32));
  if(!steering && !throttle && !throttleFamily && !selected)return false;
  // C278 stores this native allocation-failure sentinel. Both subsequent
  // C7F8 volume and CA20 pitch updates explicitly skip negative handles.
  // No voice, decoder, buffer or guest memory is allocated or modified.
  ctx.r3.s64=-1;
  if(selected){
    static std::atomic<unsigned> layersReported{0};
    const unsigned layerBit=1u<<ctx.r29.u32;
    if(!(layersReported.fetch_or(layerBit)&layerBit))
      REXLOG_INFO("[audio_source_off] Selected group0 layer{} blocked before allocation",ctx.r29.u32);
    return true;
  }
  static std::atomic<unsigned> reported{0};
  const unsigned bit=steering?1:throttleFamily?4:2;
  if(!(reported.fetch_or(bit)&bit))
    REXLOG_INFO("[audio_source_off] {} blocked before allocation; not a volume mute",
                steering?"Group3 loops":throttleFamily?"Group0 layers2..15 throttle family":"Group0 layer2 throttle candidate");
  return true;
}
