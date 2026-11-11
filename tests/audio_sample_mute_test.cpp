#include "generated/default/outrun_init.h"
#include <rex/cvar.h>
#include <windows.h>
#include <iostream>
#include "audio_isolation.h"
REXCVAR_DECLARE(bool,outrun_audio_mute_sample28);
static unsigned calls=0;static double left,right;
extern "C" REX_FUNC(__imp__sub_82118140) {
  ++calls;left=ctx.f1.f64;right=ctx.f2.f64;
  // The wrapper must leave handle/pitch/register inputs and original result intact.
  if(ctx.r4.u32!=7 || ctx.f3.f64!=1.25 || ctx.r7.u32!=123)std::abort();
  ctx.r3.u64=456;
}
int main(){
  PPCContext ctx{};ctx.r3.u32=0x40001000;ctx.r4.u32=7;ctx.r7.u32=123;
  ctx.f1.f64=-200;ctx.f2.f64=-300;ctx.f3.f64=1.25;
  REXCVAR_GET(outrun_audio_mute_sample28)=false;
  sub_82118140(ctx,nullptr);
  if(calls!=1 || left!=-200 || right!=-300 || ctx.r3.u32!=456)return 1;
  auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<32,MEM_RESERVE,PAGE_NOACCESS));
  if(!base || !VirtualAlloc(base+0x82570000,65536,MEM_COMMIT,PAGE_READWRITE))return 2;
  if(!VirtualAlloc(base+0x82810000,65536,MEM_COMMIT,PAGE_READWRITE) || !VirtualAlloc(base+0x40000000,65536,MEM_COMMIT,PAGE_READWRITE))return 16;
  constexpr uint32_t group=0x82810F28,descriptor=0x40000100,layers=0x40000200;
  REX_STORE_U32(group+28,descriptor);REX_STORE_U32(descriptor+8,3);REX_STORE_U32(descriptor+16,layers);
  REX_STORE_U32(layers,96);REX_STORE_U32(layers+204,98);REX_STORE_U32(layers+408,28);
  REX_STORE_U32(group+48,11);REX_STORE_U32(group+84,22);REX_STORE_U32(group+120,33);
  constexpr auto entry=0x82573038+28*8;
  REX_STORE_U32(entry,0x40001000);
  REXCVAR_GET(outrun_audio_mute_sample28)=true;
  ctx.r3.u32=0x40002000;sub_82118140(ctx,base);
  if(calls!=2 || left!=-200 || right!=-300)return 3;
  ctx.r3.u32=0x40001000;sub_82118140(ctx,base);
  if(calls!=3 || left!=-10000 || right!=-10000 || ctx.r3.u32!=456)return 4;
  // A reloaded bank changes the pointer: old objects stop matching immediately.
  REX_STORE_U32(entry,0x40003000);
  ctx.r3.u32=0x40001000;ctx.f1.f64=-400;ctx.f2.f64=-500;sub_82118140(ctx,base);
  if(calls!=4 || left!=-400 || right!=-500)return 5;
  ctx.r3.u32=0x40003000;sub_82118140(ctx,base);
  if(calls!=5 || left!=-10000 || right!=-10000)return 6;
  REX_STORE_U32(entry,0);ctx.r3.u32=0x40003000;ctx.f1.f64=-600;ctx.f2.f64=-700;sub_82118140(ctx,base);
  if(calls!=6 || left!=-600 || right!=-700)return 7;
  REXCVAR_GET(outrun_audio_mute_sample28)=false;
  // The actual hook must mute B/C together, preserve A and permit a live
  // comparison without restarting; held-key repeat cannot flip the mode.
  REX_STORE_U32(0x82573038+96*8,0x40004000);REX_STORE_U32(0x82573038+98*8,0x40005000);
  REXCVAR_GET(outrun_audio_isolate_bc)=true;
  ctx.r3.u32=0x40004000;sub_82118140(ctx,base);
  if(left!=-10000 || right!=-10000)return 8;
  ctx.r3.u32=0x40005000;sub_82118140(ctx,base);
  if(left!=-10000 || right!=-10000)return 9;
  ctx.r3.u32=0x40001000;ctx.f1.f64=-200;ctx.f2.f64=-300;sub_82118140(ctx,base);
  if(left!=-200 || right!=-300)return 10;
  if(!outrun::audio::IsolationKey(0x75,false) || outrun::audio::bcMuted.load())return 11;
  outrun::audio::RefreshIsolation(base);
  if(REX_LOAD_U32(group+48)!=0xEFFFFFFF || REX_LOAD_U32(group+84)!=0xEFFFFFFF || REX_LOAD_U32(group+120)!=33)return 17;
  REX_STORE_U32(group+48,44);outrun::audio::RefreshIsolation(base);
  if(REX_LOAD_U32(group+48)!=44)return 18; // No repeated per-frame invalidation.
  if(!outrun::audio::IsolationKey(0x75,true) || outrun::audio::bcMuted.load())return 12;
  ctx.r3.u32=0x40004000;sub_82118140(ctx,base);
  if(left!=-200 || right!=-300)return 13;
  if(!outrun::audio::IsolationKey(0x75,false) || !outrun::audio::bcMuted.load())return 14;
  REXCVAR_GET(outrun_audio_isolate_bc)=false;
  outrun::audio::RefreshIsolation(nullptr); // Default-off never reads guest memory.
  if(outrun::audio::IsolationKey(0x75,false))return 15;
  VirtualFree(base,0,MEM_RELEASE);
  std::cout<<"PASS: actual volume hook default-off, exact sample isolation, bank reload/null guards, original call/register/result preservation\n";
}
