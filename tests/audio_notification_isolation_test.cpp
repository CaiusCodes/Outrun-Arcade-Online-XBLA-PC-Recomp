#include "generated/default/outrun_init.h"
#include "audio_notification_isolation.h"
#include "audio_diagnostics.h"
#include <rex/cvar.h>
#include <cstring>
#include <iostream>
#include <thread>
REXCVAR_DEFINE_BOOL(outrun_audio_trace,false,"Test","Trace stub");
REXCVAR_DECLARE(bool,outrun_audio_isolate_notifications);
REXCVAR_DECLARE(bool,outrun_audio_isolate_checkpoint_voice);
static PPCContext eventArgs{},allocationArgs{},startArgs{};
static bool muted=false;
static unsigned nativeEvents=0,nativeAllocations=0,starts=0,gains=0,observations=0;
namespace outrun::audio {
bool SuppressSteeringLoopStart(PPCContext&){return false;}
void RecordEngineStart(uint32_t,uint32_t,uint32_t,int32_t){}
}
namespace outrun::diagnostics {
void Sound(uint32_t event,uint32_t caller,const char* stage){
  if(event!=eventArgs.r3.u32)std::abort();
  if(!std::strcmp(stage,"sample-start") && caller!=0x12345678)std::abort();
  ++observations;
}
void Voice(uint32_t,uint32_t,uint32_t,double,double,double,double,double){}
void LoopVoice(uint32_t,uint32_t,uint32_t,int32_t){}
}
extern "C" REX_FUNC(__imp__sub_82118140){
  auto expected=startArgs;expected.r4=startArgs.r7;
  if(!muted || starts || std::memcmp(&ctx,&expected,sizeof(ctx)))std::abort();
  ++gains;ctx={};
}
extern "C" REX_FUNC(__imp__sub_82118BD0){
  if(std::memcmp(&ctx,&startArgs,sizeof(ctx)) || gains!=unsigned(muted))std::abort();
  ++starts;ctx.r3.u64=123;
}
extern "C" REX_FUNC(__imp__sub_82117B58){
  auto expected=allocationArgs;
  if(muted)expected.f1.f64=expected.f2.f64=-10000;
  if(std::memcmp(&ctx,&expected,sizeof(ctx)))std::abort();
  ++nativeAllocations;
  startArgs=expected;startArgs.lr=0x82117C5C;startArgs.r4.u32=0;startArgs.r7.u32=7;
  auto start=startArgs;sub_82118BD0(start,base);
  if(start.r3.u64!=123)std::abort();
  ctx.r3.u64=7;ctx.r11.u64=456;
}
extern "C" REX_FUNC(__imp__sub_8210C040){
  if(std::memcmp(&ctx,&eventArgs,sizeof(ctx)))std::abort();
  ++nativeEvents;
  auto allocation=allocationArgs;sub_82117B58(allocation,base);
  if(allocation.r3.u64!=7 || allocation.r11.u64!=456)std::abort();
  ctx.r3.u64=99;ctx.r12.u64=321;
}
static void Run(uint32_t event,bool isolate,uint32_t caller=0x8210C020){
  eventArgs={};eventArgs.r3.u32=event;eventArgs.lr=caller;eventArgs.r8.u64=12345;
  allocationArgs={};allocationArgs.r3.u32=0x40420000;allocationArgs.r4.u32=0;
  allocationArgs.lr=0x8210C168;allocationArgs.r25.u32=~0u;
  allocationArgs.f1.f64=-888.59326171875;allocationArgs.f2.f64=-588.59326171875;
  muted=isolate;nativeEvents=nativeAllocations=starts=gains=observations=0;
  auto ctx=eventArgs;sub_8210C040(ctx,nullptr);
  if(nativeEvents!=1 || nativeAllocations!=1 || starts!=1 || observations!=(isolate?2:1) ||
     ctx.r3.u64!=99 || ctx.r12.u64!=321)std::abort();
}
int main(){
  outrun::audio::dispatchCaller=0x12345678;
  for(uint32_t event:{0x4Du,0x2147u,0x105Au})Run(event,false);
  REXCVAR_GET(outrun_audio_isolate_notifications)=true;
  for(uint32_t event:{0x4Du,0x2147u,0x105Au,0x2148u,0x1059u,0x105Bu,0x2145u})Run(event,true);
  // Announcer, gears, collisions and similarly numbered but differently
  // flagged events retain their exact volumes and complete playback path.
  for(uint32_t event:{0x16Cu,0x1E5u,0x51u,0x4Bu,0x4Au,0x8Bu,0x91u,0x104Du,0x147u})Run(event,false);
  Run(0x4D,false,0);Run(0x2147,false,0x82100684);
  REXCVAR_GET(outrun_audio_isolate_notifications)=false;
  REXCVAR_GET(outrun_audio_isolate_checkpoint_voice)=true;
  for(uint32_t event:{0x16Cu,0x1E5u})Run(event,true);
  for(uint32_t event:{0x4Du,0x2147u,0x105Au,0x2148u,0x1059u,0x105Bu,0x2145u,0x51u,0x4Bu,0x4Au})Run(event,false);
  Run(0x16C,false,0);Run(0x1E5,false,0x82100684);
  REXCVAR_GET(outrun_audio_isolate_checkpoint_voice)=false;
  if(outrun::audio::notificationIsolation)std::abort();
  {
    outrun::audio::NotificationIsolationScope outer(true);
    Run(0x16C,false);
    if(!outrun::audio::notificationIsolation)std::abort();
    std::thread other([]{if(outrun::audio::notificationIsolation)std::abort();});other.join();
  }
  if(outrun::audio::notificationIsolation)std::abort();
  std::cout<<"PASS: exact-event isolation, voice retention, pre-start silence, native registers/results, nested/thread scopes\n";
}
