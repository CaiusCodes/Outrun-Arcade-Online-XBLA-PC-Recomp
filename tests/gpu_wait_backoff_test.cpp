#include "generated/default/outrun_init.h"
#include "gpu_wait_backoff.h"
#include <rex/cvar.h>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
REXCVAR_DECLARE(bool,outrun_gpu_wait_backoff);
REXCVAR_DECLARE(int32_t,outrun_gpu_wait_sleep_us);
static PPCContext expected{},returned{};
static int native_calls=0,backoff_calls=0;
static bool expected_waiting=true;
static uint32_t expected_sleep_us=500;
static bool complete_during_native=false;
static void Store(uint8_t* base,uint32_t offset,uint32_t value) {
  base[offset]=uint8_t(value>>24);base[offset+1]=uint8_t(value>>16);
  base[offset+2]=uint8_t(value>>8);base[offset+3]=uint8_t(value);
}
extern "C" REX_FUNC(__imp__sub_822999B8) {
  if(std::memcmp(&ctx,&expected,sizeof(ctx)))std::abort();
  ++native_calls;ctx=returned;
  if(complete_during_native)Store(base,256,expected.r30.u32);
}
namespace outrun::gpu_wait {
void Backoff(uint32_t address,uint32_t stamp,bool waiting,WaitSample* sample,uint32_t sleep_us) {
  if(sleep_us!=expected_sleep_us)std::abort();
  if(sample)std::abort(); // diagnostics must be off by default
  if(address!=128 || waiting!=expected_waiting || stamp!=(waiting?0x12345678u:0u))std::abort();
  ++backoff_calls;
}
}
int main() {
  using namespace outrun::gpu_wait;
  Budget b;
  if(b.Poll(128,1,1000)!=Action::Yield || b.Poll(128,1,1499)!=Action::Yield ||
     b.Poll(128,1,1500)!=Action::Sleep)return 1;
  if(b.Poll(128,2,1600)!=Action::Yield || b.Poll(256,2,2100)!=Action::Yield)return 2;
  if(b.Poll(256,2,2600)!=Action::Sleep || b.Poll(256,2,8000)!=Action::Yield)return 3;
  b.Reset();if(b.Poll(256,2,9000)!=Action::Yield)return 4;
  if(b.Poll(256,2,8000)!=Action::Yield)return 5;
  std::array<uint8_t,16384> memory{};
  memory[148]=0x12;memory[149]=0x34;memory[150]=0x56;memory[151]=0x78;
  expected.r3.u32=128;expected.lr=0x8229ACA0;
  expected.r31.u32=512;expected.r30.u32=10;
  Store(memory.data(),512+10896,256);
  Store(memory.data(),512+10908,12);
  Store(memory.data(),256,9);
  returned=expected;returned.r3.u32=1;returned.r11.u64=0xDEADBEEF;returned.f1.f64=123.5;
  auto call=[&] {
    auto before=memory;
    if(complete_during_native)Store(before.data(),256,expected.r30.u32);
    auto ctx=expected;sub_822999B8(ctx,memory.data());
    if(std::memcmp(&ctx,&returned,sizeof(ctx)) || memory!=before)std::abort();
  };
  call();if(backoff_calls)return 6; // default off
  REXCVAR_GET(outrun_gpu_wait_backoff)=true;
  call();if(backoff_calls!=1)return 7;
  expected.lr=0x82299FFF;call();if(backoff_calls!=1)return 8;
  expected.lr=0x8229ACA0;returned.r3.u32=0;expected_waiting=false;
  call();if(backoff_calls!=2 || native_calls!=4)return 9;
  returned.r3.u32=1;complete_during_native=true;
  call();if(backoff_calls!=3)return 10; // completes inside original helper
  complete_during_native=false;
  call();if(backoff_calls!=4)return 11; // already complete, still returns 1
  Store(memory.data(),256,11);
  call();if(backoff_calls!=5)return 12; // beyond target
  expected.r30.u32=0;returned.r30.u32=0;
  Store(memory.data(),512+10908,2);Store(memory.data(),256,0xFFFFFFFE);
  expected_waiting=true;call();if(backoff_calls!=6)return 13;
  Store(memory.data(),256,0);expected_waiting=false;
  call();if(backoff_calls!=7)return 14; // wraparound completed
  expected_sleep_us=100;REXCVAR_GET(outrun_gpu_wait_sleep_us)=100;call();
  expected_sleep_us=50;REXCVAR_GET(outrun_gpu_wait_sleep_us)=-1;call();
  expected_sleep_us=500;REXCVAR_GET(outrun_gpu_wait_sleep_us)=1000000;call();
  std::puts("PASS: native state passthrough, default-off/exact-caller scope, completion reset, bounded backoff policy");
}
