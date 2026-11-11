#include "generated/default/outrun_init.h"
#include "audio_diagnostics.h"
#include "audio_engine_attack.h"
#include <rex/cvar.h>
#include <windows.h>
#include <cstring>
#include <iostream>
static unsigned updates=0,stops=0,observations=0;
static PPCContext expected{};
static bool envelopeCheck=false;
static uint32_t observedGroup=0;
REXCVAR_DECLARE(bool,outrun_audio_engine_attack);
namespace outrun::diagnostics {
void LoopTransition(const char* stage,uint32_t group,uint32_t layer,uint32_t object,int32_t handle,double left,double right){
  if(group!=observedGroup || layer!=4 || object!=0x12345678 || handle!=7){std::cerr<<"Observation mismatch group="<<group<<" expected="<<observedGroup<<" layer="<<layer<<" handle="<<handle<<'\n';std::abort();}
  if(!std::strcmp(stage,"loop-volume") && (left!=-200 || right!=-300))std::abort();
  ++observations;
}
}
extern "C" REX_FUNC(__imp__sub_82118140){
  auto target=expected;
  if(envelopeCheck){
    if(ctx.f1.f64>expected.f1.f64 || ctx.f2.f64>expected.f2.f64)std::abort();
    target.f1=ctx.f1;target.f2=ctx.f2;
  }
  if(std::memcmp(&ctx,&target,sizeof(ctx))){std::cerr<<"Native gain register mismatch\n";std::abort();}
  ++updates;ctx.r3.u64=42;
}
extern "C" REX_FUNC(__imp__sub_82117F08){
  if(std::memcmp(&ctx,&expected,sizeof(ctx))){std::cerr<<"Native stop register mismatch\n";std::abort();}
  ++stops;ctx.r3.u64=43;
}
extern "C" REX_FUNC(__imp__sub_8210C7F8){
  if(envelopeCheck && REX_LOAD_U32(0x82810F28+4*36+48)==REX_LOAD_U32(0x82810F28+4*36+36))std::abort();
  ctx=expected;sub_82118140(ctx,base);
}
int main(){
  expected.r3.u32=0x12345678;expected.r4.u32=7;expected.lr=0x8210CA0C;
  expected.f1.f64=-200;expected.f2.f64=-300;expected.r7.u32=111;
  PPCContext ctx{};ctx.r3.u32=0;ctx.r4.u32=4;
  sub_8210C7F8(ctx,nullptr);
  if(updates!=1 || observations!=1 || ctx.r3.u64!=42)return 1;
  // A direct call after scope exit must not inherit the previous group.
  ctx=expected;sub_82118140(ctx,nullptr);
  if(updates!=2 || observations!=1 || ctx.r3.u64!=42)return 2;
  ctx={};ctx.r3.u32=2;ctx.r4.u32=4;sub_8210C7F8(ctx,nullptr);
  if(updates!=3 || observations!=1)return 3;
  expected.lr=0x8210C400;expected.r25.u32=0;expected.r29.u32=4;
  ctx=expected;sub_82117F08(ctx,nullptr);
  if(stops!=1 || observations!=2 || ctx.r3.u64!=43)return 4;
  expected.lr=0;ctx=expected;sub_82117F08(ctx,nullptr);
  if(stops!=2 || observations!=2)return 5;
  // Restored tire loops are observed, never faded or otherwise adjusted.
  observedGroup=3;expected.lr=0x8210CA0C;
  REXCVAR_GET(outrun_audio_engine_attack)=true;
  ctx={};ctx.r3.u32=3;ctx.r4.u32=4;sub_8210C7F8(ctx,nullptr);
  if(updates!=4 || observations!=3 || ctx.r3.u64!=42)return 8;
  expected.lr=0x8210C400;expected.r25.u32=3;expected.r29.u32=4;
  ctx=expected;sub_82117F08(ctx,nullptr);
  if(stops!=3 || observations!=4 || ctx.r3.u64!=43)return 9;
  observedGroup=0;
  auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS));
  if(!base || !VirtualAlloc(base+0x82810000,65536,MEM_COMMIT,PAGE_READWRITE))return 6;
  const uint32_t state=0x82810F28+4*36;
  REX_STORE_U32(state+64,7);REX_STORE_U32(state+36,123);REX_STORE_U32(state+48,123);
  expected.lr=0x8210CA0C;
  REXCVAR_GET(outrun_audio_engine_attack)=true;envelopeCheck=true;
  outrun::audio::RecordEngineStart(0,4,0x12345678,7);
  ctx={};ctx.r3.u32=0;ctx.r4.u32=4;sub_8210C7F8(ctx,base);
  if(ctx.r3.u64!=42 || REX_LOAD_U32(state+48)!=123 || REX_LOAD_U32(state+36)!=123 || REX_LOAD_U32(state+64)!=7)return 7;
  VirtualFree(base,0,MEM_RELEASE);
  std::cout<<"PASS: actual volume/stop hooks preserve original arguments/results, scope teardown and unrelated callers\n";
}
