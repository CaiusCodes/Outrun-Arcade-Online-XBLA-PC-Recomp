#include "generated/default/outrun_init.h"
#include "audio_diagnostics.h"
#include <cstring>
#include <iostream>
static PPCContext expected{};
static unsigned observed=0,called=0;
namespace outrun::diagnostics {
void Sound(uint32_t event,uint32_t caller,const char* stage){
 if(event!=expected.r3.u32 || caller!=uint32_t(expected.lr) || std::strcmp(stage,"queue-request"))std::abort();
 ++observed;
}
}
extern "C" REX_FUNC(__imp__sub_82100780){
 if(std::memcmp(&ctx,&expected,sizeof(ctx)) || observed!=called+1)std::abort();
 ++called;ctx.r3.u64=123;ctx.r11.u64=456;
}
int main(){
 for(uint32_t event:{0u,81u,327u,~0u}){
  expected={};expected.r3.u32=event;expected.r7.u32=17;expected.lr=0x820FADCC;
  auto ctx=expected;sub_82100780(ctx,nullptr);
  if(ctx.r3.u64!=123 || ctx.r11.u64!=456)return 1;
 }
 if(called!=4 || observed!=4)return 2;
 std::cout<<"PASS: queued event caller observed without changing native arguments/results/playback path\n";
}
