#include "generated/default/outrun_init.h"
#include "menu_pointer.h"
#include <windows.h>
#include <cstring>
#include <stdexcept>
#include <iostream>
namespace {
PPCContext expected{};
uint32_t expectedWait=1;
unsigned calls=0;
void Check(bool ok){if(!ok)throw std::runtime_error("hover animation isolation failed");}
}
extern "C" REX_FUNC(__imp__sub_8223C548) {
  Check(std::memcmp(&ctx,&expected,sizeof(ctx))==0);
  const auto args=REX_LOAD_U32(ctx.r4.u32+12);
  Check(REX_LOAD_U32(args+20)==expectedWait);
  ++calls;ctx.r3.u32=123;ctx.r4.u32=456; // Preserve native return/clobbers.
}
int main(){try{
 auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<30,MEM_RESERVE,PAGE_NOACCESS));
 Check(base && VirtualAlloc(base+0x10000000,65536,MEM_COMMIT,PAGE_READWRITE));
 const uint32_t vm=0x10000100,manager=0x10000200,script=0x10001000,mscript=0x10001800;
 const uint32_t values=0x10002000,types=0x10003000,desc=0x10004000,data=0x10004800,items=0x10005000;
 const uint32_t call=0x10006000,args=0x10006100;
 REX_STORE_U32(vm+8,script);REX_STORE_U32(vm+4,script+0x50B);
 REX_STORE_U32(vm+12,values);REX_STORE_U32(vm+16,types);REX_STORE_U16(vm+22,13);
 REX_STORE_U32(values+16,desc);REX_STORE_U8(types+4,8);REX_STORE_U32(desc,7);REX_STORE_U32(desc+8,data);
 REX_STORE_U32(data+32,items);REX_STORE_U32(data+36,8);REX_STORE_U32(items,4);
 REX_STORE_U32(manager+8,mscript);REX_STORE_U32(manager+4,mscript+0x55D);
 REX_STORE_U32(call+12,args);REX_STORE_U32(call+16,6);REX_STORE_U32(args+20,1);
 using namespace outrun::pointer;
 RegisterScript(script,0x7FE653EA);RegisterScript(mscript,0x1C39026A);
 auto run=[&](bool fast){expected={};expected.r3.u32=manager;expected.r4.u32=call;expected.lr=99;
   auto ctx=expected;expectedWait=fast?0:1;const auto before=calls;sub_8223C548(ctx,base);
   Check(calls==before+1 && ctx.r3.u32==123 && ctx.r4.u32==456 && REX_LOAD_U32(args+20)==1);};
 run(false);Move(900,535);Check(Input(vm,base,0)==8);run(true);
 // Entry/exit animations, other scripts, Cancel and physical input stay native.
 REX_STORE_U32(manager+4,mscript+0x462);run(false);
 REX_STORE_U32(manager+4,mscript+0x7E8);run(false);
 REX_STORE_U32(manager+4,mscript+0x55D);RegisterScript(mscript,0x6B185A6B);run(false);
 RegisterScript(mscript,0x1C39026A);Check(Input(vm,base,8)==8);run(false);
 Move(901,535);Check(Input(vm,base,0)==8);Clear();run(false);
 Move(902,535);Check(Input(vm,base,0)==8);Push({900,535,false});Check(Input(vm,base,0)==8);run(false);
 VirtualFree(base,0,MEM_RELEASE);
 std::cout<<"PASS: hover bypasses only card-navigation waits; native arguments, playback, returns, clicks, physical input and page transitions preserved\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
