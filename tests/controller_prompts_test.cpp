#include "generated/default/outrun_init.h"
#include "controller_prompts.h"
#include "menu_pointer.h"
#include <windows.h>
#include <vector>
#include <cstring>
#include <iostream>
#include <stdexcept>
struct Event {uint32_t id,recursive,stack;bool show;};
std::vector<Event> events;
extern "C" REX_FUNC(__imp__sub_822400A8) {
  events.push_back({ctx.r4.u32,ctx.r5.u32,ctx.r1.u32,true});ctx.r3.u32=1;
}
extern "C" REX_FUNC(__imp__sub_82240118) {
  events.push_back({ctx.r4.u32,ctx.r5.u32,ctx.r1.u32,false});ctx.r3.u32=1;
}
void Check(bool ok){if(!ok)throw std::runtime_error("controller prompt isolation failed");}
int main(){try{
 auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 Check(base!=nullptr);PPCContext ctx{};ctx.r1.u32=32768;
 auto show=[&](uint32_t id){ctx.r3.u32=123;ctx.r4.u32=id;ctx.r5.u32=1;sub_822400A8(ctx,base);Check(ctx.r3.u32==1);};
 using outrun::pointer::controllerConnected;
 controllerConnected=false;
 show(0xC50AFA73);Check(!events.back().show); // A icon hidden.
 show(0x8984B30A);Check(!events.back().show); // Select label hidden without pad.
 show(0x9868EE0B);Check(!events.back().show);
 ctx.r4.u32=0x9868EE0B;sub_82240118(ctx,base); // B no longer wanted.
 auto before=ctx;events.clear();controllerConnected=true;
 outrun::prompts::Refresh(ctx,base);
 Check(std::memcmp(&before,&ctx,sizeof(ctx))==0);
 Check(events.size()==2 && events[0].show && events[0].id==0xC50AFA73 && events[1].show);
 Check(events[0].stack==before.r1.u32-128 && events[0].recursive==1);
 events.clear();outrun::prompts::Refresh(ctx,base);Check(events.empty());
 controllerConnected=false;outrun::prompts::Refresh(ctx,base);
 Check(events.size()==2 && !events[0].show && !events[1].show);
 Check(std::memcmp(&before,&ctx,sizeof(ctx))==0);
 events.clear();outrun::pointer::carFooterUntil=GetTickCount64()+1000;
 outrun::prompts::Refresh(ctx,base);
 Check(events.empty());
 events.clear();outrun::pointer::carFooterUntil=0;outrun::prompts::Refresh(ctx,base);
 Check(events.empty());
 show(0xDECBA934);Check(events.back().show); // Car-page arrow untouched.
 VirtualFree(base,0,MEM_RELEASE);
 std::cout<<"PASS: icon-only suppression, hotplug restoration, hidden-state preservation and scheduler context isolation\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
