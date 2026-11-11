#include "generated/default/outrun_init.h"
#include "audio_voice_properties.h"
#include <rex/cvar.h>
#include <vector>
#include <cstring>
#include <iostream>
REXCVAR_DECLARE(bool,outrun_audio_property_trace);
static PPCContext expected{};
static unsigned calls=0;
namespace outrun::diagnostics {
bool AudioTraceRecording(){return true;}
uint64_t AudioTraceTime(){return 1234;}
}
extern "C" REX_FUNC(__imp__sub_82475578){
  if(std::memcmp(&ctx,&expected,sizeof(ctx)))std::abort();
  ++calls;ctx.r3.u64=99;ctx.r11.u64=123;
}
int main(){
  std::vector<uint8_t> bytes(1024);
  const auto put=[&](uint32_t at,uint32_t v){for(unsigned i=0;i<4;++i)bytes[at+i]=uint8_t(v>>(24-i*8));};
  bytes[16]=1;put(20,32);bytes[32]=0;put(36,48);bytes[48]=2;put(52,64);
  bytes[64]=0;bytes[72]=1;put(68,0x3f000000);put(76,0x3e800000);
  put(128,256);put(296,0x82401234);
  auto gain=outrun::diagnostics::ReadVoiceGain(bytes.data(),128,16,bytes.size());
  if(!gain || gain->target!=0x82401234 || gain->leftBits!=0x3f000000 || gain->rightBits!=0x3e800000)return 1;
  put(68,0x7fc00001); // Preserve nonfinite bits as evidence, never repair them.
  gain=outrun::diagnostics::ReadVoiceGain(bytes.data(),128,16,bytes.size());
  if(!gain || gain->leftBits!=0x7fc00001)return 2;
  for(uint32_t pointer:{0u,1018u,0xfffffffcu})
    if(outrun::diagnostics::ReadVoiceGain(bytes.data(),128,pointer,bytes.size()))return 3;
  bytes[72]=0;if(outrun::diagnostics::ReadVoiceGain(bytes.data(),128,16,bytes.size()))return 4;bytes[72]=1;
  put(52,1020);if(outrun::diagnostics::ReadVoiceGain(bytes.data(),128,16,bytes.size()))return 5;put(52,64);
  expected.r3.u32=128;expected.r4.u32=16;expected.r24.u32=512;expected.r31.u32=768;expected.lr=0x8211822C;
  const auto before=bytes;
  auto ctx=expected;sub_82475578(ctx,bytes.data());
  REXCVAR_GET(outrun_audio_property_trace)=true;
  ctx=expected;sub_82475578(ctx,bytes.data());
  expected.lr=0;ctx=expected;sub_82475578(ctx,nullptr);
  if(calls!=3 || ctx.r3.u64!=99 || ctx.r11.u64!=123 || bytes!=before)return 6;
  std::cout<<"PASS: gain-list endian/shape/bounds, nonfinite preservation and read-only actual hook passthrough\n";
}
