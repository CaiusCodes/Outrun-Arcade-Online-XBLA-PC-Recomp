#include "generated/default/outrun_init.h"
#include "controller_prompts.h"
#include "menu_pointer.h"
#include <rex/hook.h>
#include <array>
#include <mutex>
#include <windows.h>

namespace outrun::prompts {
namespace {
// menu_manager footer dictionary, script offsets 0x146–0x1DA. These are
// icon-only widgets; the adjacent text widgets and navigation arrows survive.
constexpr std::array<uint32_t,8> icons{0x9868EE0B,0xC50AFA73,0x902291BB,0xF21B780A,
  0x42DA0DDD,0xE4FD06B4,0x8984B30A,0x48BB497F}; // Back/Select text widgets.
bool CarScreen(){return GetTickCount64()<pointer::carFooterUntil.load();}
// The icons stay: without a controller the renderer draws keyboard keys into
// them (native_gpu.cpp keyglyphs), so nothing needs hiding any more.
bool Hide(int i,bool connected,bool car){return false;}
struct Visibility { bool wanted=false; uint32_t manager=0,recursive=1; };
std::array<Visibility,8> visibility{};
std::mutex mutex;
bool previousConnected=false,previousCar=false;
int Index(uint32_t widget) {
  for(unsigned i=0;i<icons.size();++i)if(icons[i]==widget)return int(i);
  return -1;
}
void Remember(PPCContext& ctx,bool shown) {
  const int i=Index(ctx.r4.u32);
  if(i<0)return;
  std::lock_guard lock(mutex);
  visibility[i]={shown,ctx.r3.u32,ctx.r5.u32};
  // Icons 0-3 are the Back, Select, Y and X prompts (seen in each menu's footer).
  if(i<4){if(shown)pointer::footerWidgets.fetch_or(1u<<i);else pointer::footerWidgets.fetch_and(~(1u<<i));}
}
}
void Refresh(PPCContext& ctx,uint8_t* base) {
  const bool connected=pointer::controllerConnected.load(),car=CarScreen();
  std::array<Visibility,8> snapshot;
  bool wasConnected,wasCar;
  {
    std::lock_guard lock(mutex);
    if(connected==previousConnected && car==previousCar)return;
    wasConnected=previousConnected;wasCar=previousCar;
    previousConnected=connected;previousCar=car;snapshot=visibility;
  }
  for(unsigned i=0;i<icons.size();++i) {
    if(!snapshot[i].wanted || Hide(int(i),connected,car)==Hide(int(i),wasConnected,wasCar))continue;
    // Run on the native scheduler thread. Reserve the PPC argument home area
    // below its stack and keep every register/result of the scheduler intact.
    auto call=ctx;
    call.r1.u32-=128;
    REX_STORE_U32(call.r1.u32,ctx.r1.u32);
    call.r3.u32=snapshot[i].manager;call.r4.u32=icons[i];
    call.r5.u32=snapshot[i].recursive;
    if(!Hide(int(i),connected,car))__imp__sub_822400A8(call,base);
    else __imp__sub_82240118(call,base);
  }
}
}

REX_HOOK_RAW(sub_822400A8) {
  outrun::prompts::Remember(ctx,true);
  if(outrun::prompts::Index(ctx.r4.u32)>=0 && outrun::prompts::Hide(outrun::prompts::Index(ctx.r4.u32),outrun::pointer::controllerConnected.load(),outrun::prompts::CarScreen()))
    __imp__sub_82240118(ctx,base);
  else __imp__sub_822400A8(ctx,base);
}
REX_HOOK_RAW(sub_82240118) {
  outrun::prompts::Remember(ctx,false);
  __imp__sub_82240118(ctx,base);
}
