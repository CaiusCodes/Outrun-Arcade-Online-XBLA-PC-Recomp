#include "generated/default/outrun_init.h"
#include "audio_loop_suppression.h"
#include <rex/cvar.h>
#include <cstring>
#include <iostream>
REXCVAR_DECLARE(bool,outrun_audio_disable_steering_loops);
REXCVAR_DECLARE(bool,outrun_audio_disable_throttle_layer);
REXCVAR_DECLARE(bool,outrun_audio_disable_throttle_layers);
REXCVAR_DECLARE(uint32_t,outrun_audio_throttle_layer_mask);
int main(){
  PPCContext ctx{};ctx.lr=0x8210C39C;ctx.r25.u32=3;ctx.r29.u32=0;
  ctx.r4.u32=0x800;ctx.r3.u32=0x40427BE0;ctx.f1.f64=-10000;ctx.r1.u32=0x40001000;
  const auto initial=ctx;
  REXCVAR_GET(outrun_audio_disable_steering_loops)=false;
  if(outrun::audio::SuppressSteeringLoopStart(ctx) || std::memcmp(&ctx,&initial,sizeof(ctx)))return 1;
  REXCVAR_GET(outrun_audio_disable_steering_loops)=true;
  for(unsigned group=0;group<17;++group){
    ctx=initial;ctx.r25.u32=group;auto expected=ctx;
    if(group==3)expected.r3.s64=-1;
    if(outrun::audio::SuppressSteeringLoopStart(ctx)!=(group==3) || std::memcmp(&ctx,&expected,sizeof(ctx)))return 2;
  }
  for(unsigned layer=0;layer<17;++layer){
    ctx=initial;ctx.r29.u32=layer;auto expected=ctx;if(layer<16)expected.r3.s64=-1;
    if(outrun::audio::SuppressSteeringLoopStart(ctx)!=(layer<16) || std::memcmp(&ctx,&expected,sizeof(ctx)))return 3;
  }
  ctx=initial;ctx.lr=0x8210C168;auto expected=ctx;
  if(outrun::audio::SuppressSteeringLoopStart(ctx) || std::memcmp(&ctx,&expected,sizeof(ctx)))return 4;
  ctx=initial;ctx.r4.u32=0;expected=ctx;
  if(outrun::audio::SuppressSteeringLoopStart(ctx) || std::memcmp(&ctx,&expected,sizeof(ctx)))return 5;
  // Exhaustively check both independent toggles across every group/layer.
  for(unsigned mode=0;mode<8;++mode){
    REXCVAR_GET(outrun_audio_disable_steering_loops)=bool(mode&1);
    REXCVAR_GET(outrun_audio_disable_throttle_layer)=bool(mode&2);
    REXCVAR_GET(outrun_audio_disable_throttle_layers)=bool(mode&4);
    for(unsigned group=0;group<17;++group)for(unsigned layer=0;layer<17;++layer){
      ctx=initial;ctx.r25.u32=group;ctx.r29.u32=layer;auto want=ctx;
      bool blocked=layer<16 && (((mode&1) && group==3) || ((mode&2) && group==0 && layer==2) || ((mode&4) && group==0 && layer>=2));
      if(blocked)want.r3.s64=-1;
      if(outrun::audio::SuppressSteeringLoopStart(ctx)!=blocked || std::memcmp(&ctx,&want,sizeof(ctx)))return 6;
    }
  }
  REXCVAR_GET(outrun_audio_disable_throttle_layer)=false;
  REXCVAR_GET(outrun_audio_disable_throttle_layers)=false;
  for(uint32_t mask: {0u,248u,32512u,0xffffffffu}){
    REXCVAR_GET(outrun_audio_throttle_layer_mask)=mask;
    for(unsigned group=0;group<17;++group)for(unsigned layer=0;layer<33;++layer){
      ctx=initial;ctx.r25.u32=group;ctx.r29.u32=layer;auto want=ctx;
      const bool blocked=layer<16 && (group==3 || (group==0 && layer>=2 && (mask&(1u<<layer))));
      if(blocked)want.r3.s64=-1;
      if(outrun::audio::SuppressSteeringLoopStart(ctx)!=blocked || std::memcmp(&ctx,&want,sizeof(ctx)))return 7;
    }
  }
  std::cout<<"PASS: independent toggles/masks, protected base layers, groups/bounds, caller/flags, register preservation\n";
}
