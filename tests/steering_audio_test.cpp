#include "generated/default/outrun_init.h"
#include <rex/cvar.h>
#include <windows.h>
#include <iostream>
REXCVAR_DECLARE(bool,outrun_audio_mute_steering_group);
static unsigned calls=0;static double left,right;
extern "C" REX_FUNC(__imp__sub_82118140){
  ++calls;left=ctx.f1.f64;right=ctx.f2.f64;
  if(ctx.f3.f64!=1.25 || ctx.r7.u32!=123)std::abort();
  ctx.r3.u64=456;
}
int main(){
  PPCContext ctx{};ctx.r3.u32=0x40001000;ctx.r4.u32=7;ctx.r7.u32=123;ctx.f3.f64=1.25;ctx.f1.f64=-200;ctx.f2.f64=-300;
  REXCVAR_GET(outrun_audio_mute_steering_group)=false;sub_82118140(ctx,nullptr);
  if(calls!=1 || left!=-200 || right!=-300 || ctx.r3.u32!=456)return 1;
  auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS));
  if(!base || !VirtualAlloc(base+0x82570000,65536,MEM_COMMIT,PAGE_READWRITE) || !VirtualAlloc(base+0x82810000,65536,MEM_COMMIT,PAGE_READWRITE) || !VirtualAlloc(base+0x40000000,65536,MEM_COMMIT,PAGE_READWRITE))return 2;
  constexpr uint32_t group=0x82810F28+3*608,descriptor=0x40000100,layers=0x40000200;
  REX_STORE_U32(group+28,descriptor);REX_STORE_U32(descriptor+8,2);REX_STORE_U32(descriptor+16,layers);
  REX_STORE_U32(layers,0);REX_STORE_U32(layers+204,1);
  REX_STORE_U32(0x82573038,0x40001000);REX_STORE_U32(0x82573038+8,0x40002000);
  REX_STORE_U32(group+64,7);REX_STORE_U32(group+100,8);
  REXCVAR_GET(outrun_audio_mute_steering_group)=true;
  ctx.r3.u32=0x40001000;ctx.r4.u32=9;sub_82118140(ctx,base); // Shared sample, other group's handle.
  if(left!=-200 || right!=-300)return 3;
  ctx.r3.u32=0x40002000;ctx.r4.u32=7;sub_82118140(ctx,base); // Wrong object for active handle.
  if(left!=-200 || right!=-300)return 4;
  ctx.r3.u32=0x40001000;ctx.r4.u32=7;sub_82118140(ctx,base);
  if(left!=-10000 || right!=-10000 || ctx.r3.u32!=456)return 5;
  ctx.r3.u32=0x40002000;ctx.r4.u32=8;sub_82118140(ctx,base);
  if(left!=-10000 || right!=-10000)return 6;
  // Retired/reused voice handles must immediately stop matching.
  REX_STORE_U32(group+64,10);ctx.r3.u32=0x40001000;ctx.r4.u32=7;ctx.f1.f64=-400;ctx.f2.f64=-500;sub_82118140(ctx,base);
  if(left!=-400 || right!=-500)return 7;
  REX_STORE_U32(descriptor+8,17);ctx.r3.u32=0x40001000;ctx.r4.u32=10;sub_82118140(ctx,base);
  if(left!=-400 || right!=-500)return 8;
  REX_STORE_U32(descriptor+8,2);ctx.r4.u32=0xffffffff;sub_82118140(ctx,base);
  if(left!=-400 || right!=-500)return 9;
  VirtualFree(base,0,MEM_RELEASE);
  std::cout<<"PASS: actual steering group hook, exact handle/object scope, other-group preservation, retired handles and bounds, original call/result\n";
}
