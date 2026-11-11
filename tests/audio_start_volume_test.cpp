#include "generated/default/outrun_init.h"
#include <rex/cvar.h>
#include <cstring>
#include <iostream>
#include <limits>
REXCVAR_DECLARE(bool,outrun_audio_initial_volume);
REXCVAR_DECLARE(bool,outrun_audio_initial_volume_extended);
REXCVAR_DECLARE(bool,outrun_audio_initial_volume_surface);
REXCVAR_DECLARE(bool,outrun_audio_initial_volume_oneshots);
static PPCContext expected{};
static unsigned gainCalls=0,startCalls=0;
static bool shouldInitialize=false;
extern "C" REX_FUNC(__imp__sub_82118140) {
  auto wanted=expected;wanted.r4=expected.r7;
  if(std::memcmp(&ctx,&wanted,sizeof(ctx)) || startCalls)std::abort();
  ++gainCalls;
  ctx={}; // The native setter may clobber volatile registers.
}
extern "C" REX_FUNC(__imp__sub_82118BD0) {
  if(std::memcmp(&ctx,&expected,sizeof(ctx)) || gainCalls!=unsigned(shouldInitialize))std::abort();
  ++startCalls;ctx.r3.u64=123;
}
static void Run(bool initialize){
  shouldInitialize=initialize;gainCalls=startCalls=0;
  auto ctx=expected;sub_82118BD0(ctx,nullptr);
  if(ctx.r3.u64!=123 || startCalls!=1)std::abort();
}
int main(){
  expected.r3.u32=0x40420000;expected.r4.u32=1;expected.r7.u32=7;
  expected.r25.u32=0;expected.lr=0x82117C5C;
  expected.f1.f64=expected.f2.f64=-10000;
  Run(false);
  REXCVAR_GET(outrun_audio_initial_volume)=true;Run(true);
  const auto valid=expected;
  for(unsigned group=1;group<17;++group){expected=valid;expected.r25.u32=group;Run(group==3);}
  expected=valid;expected.lr=0;Run(false);
  expected=valid;expected.r7.u32=315;Run(false);
  expected=valid;expected.r7.s32=-1;Run(false);
  expected=valid;expected.r4.u32=0;Run(false);
  expected=valid;expected.f1.f64=-200;Run(false);
  expected=valid;expected.f2.f64=-200;Run(false);
  auto pan=valid;pan.r4.u32=0;pan.r25.u32=~0u;pan.f2.f64=-888.59326171875;
  expected=pan;Run(false); // Candidate off preserves old one-shot path.
  REXCVAR_GET(outrun_audio_initial_volume_extended)=true;
  for(unsigned group=0;group<17;++group){expected=valid;expected.r25.u32=group;Run(group==0 || group==1 || group==3);}
  expected=pan;Run(true);
  expected=pan;expected.f1=pan.f2;expected.f2=pan.f1;Run(true);
  expected=pan;expected.f2.f64=0;Run(true);
  expected=pan;expected.f2.f64=10;Run(false);
  expected=pan;expected.f2.f64=-10001;Run(false);
  expected=pan;expected.f2.f64=std::numeric_limits<double>::quiet_NaN();Run(false);
  expected=pan;expected.f1.f64=-200;Run(false);
  expected=pan;expected.f2.f64=-10000;Run(false);
  expected=pan;expected.lr=0;Run(false);
  expected=pan;expected.r7.u32=315;Run(false);
  expected=pan;expected.r4.u32=2;Run(false);
  // Off-road capture repeatedly restarts groups4/5 at requested silence.
  // Keep group6 (observed beside an impact) and other paths unchanged.
  REXCVAR_GET(outrun_audio_initial_volume_surface)=true;
  for(unsigned group=0;group<17;++group){expected=valid;expected.r25.u32=group;Run(group==0 || group==1 || group==3 || group==4 || group==5);}
  for(unsigned group:{4u,5u}){
    expected=valid;expected.r25.u32=group;expected.f1.f64=-200;Run(false);
    expected=valid;expected.r25.u32=group;expected.lr=0;Run(false);
    expected=valid;expected.r25.u32=group;expected.r7.u32=315;Run(false);
    expected=valid;expected.r25.u32=group;expected.r4.u32=2;Run(false);
  }
  REXCVAR_GET(outrun_audio_initial_volume_extended)=false;
  expected=valid;expected.r25.u32=1;Run(false);
  expected=valid;expected.r25.u32=4;Run(true);
  expected=valid;expected.r25.u32=5;Run(true);
  expected=pan;Run(false);
  REXCVAR_GET(outrun_audio_initial_volume_oneshots)=true;
  for(double gain:{-1188.59326171875,-888.59326171875,-588.59326171875}){
    expected=pan;expected.f1.f64=expected.f2.f64=gain;Run(true);
  }
  expected=pan;expected.f1.f64=-300;expected.f2.f64=-600;Run(true);
  expected=pan;expected.f1.f64=0;Run(true);
  expected=pan;expected.f1.f64=expected.f2.f64=0;Run(false);
  expected=pan;expected.f1.f64=expected.f2.f64=-10000;Run(true);
  expected=pan;expected.f1.f64=std::numeric_limits<double>::quiet_NaN();Run(false);
  expected=pan;expected.f2.f64=std::numeric_limits<double>::infinity();Run(false);
  expected=pan;expected.f1.f64=-10001;Run(false);
  expected=pan;expected.f2.f64=1;Run(false);
  expected=pan;expected.lr=0;Run(false);
  expected=pan;expected.r7.u32=315;Run(false);
  expected=pan;expected.r4.u32=2;Run(false);
  expected=valid;expected.r25.u32=6;Run(false);
  REXCVAR_GET(outrun_audio_initial_volume)=false;
  expected=valid;expected.r25.u32=4;Run(false);
  expected=valid;expected.r25.u32=5;Run(false);
  expected=pan;Run(false);
  std::cout<<"PASS: initial gain precedes start; arguments/results and unrelated calls preserved\n";
}
