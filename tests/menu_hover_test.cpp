#include "generated/default/outrun_init.h"
#include "menu_pointer.h"
#include <windows.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

static void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(){try{
 auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<30,MEM_RESERVE,PAGE_NOACCESS));
 Check(base && VirtualAlloc(base+0x10000000,65536,MEM_COMMIT,PAGE_READWRITE),"guest fixture allocation");
 const uint32_t vm=0x10000100,script=0x10001000,values=0x10002000,types=0x10003000;
 const uint32_t descriptor=0x10004000,data=0x10004800,items=0x10005000,pane=0x10005800,paneData=0x10005A00;
 REX_STORE_U32(vm+8,script);REX_STORE_U32(vm+12,values);REX_STORE_U32(vm+16,types);REX_STORE_U16(vm+22,13);
 REX_STORE_U32(values+16,descriptor);REX_STORE_U8(types+4,8);REX_STORE_U32(descriptor,7);REX_STORE_U32(descriptor+8,data);
 REX_STORE_U32(data+4*8,items);REX_STORE_U32(data+4*8+4,8);REX_STORE_U32(items,4);
 using namespace outrun::pointer;
 auto poll=[&](uint32_t input=0){return Input(vm,base,input);};
 auto select=[&](int current,int visible){REX_STORE_U32(data,current);REX_STORE_U32(data+8,visible);};
 auto main=[&]{Clear();RegisterScript(script,0x7FE653EA);REX_STORE_U32(vm+8,script);REX_STORE_U32(vm+4,script+0x50B);select(0,0);};
 main();Move(900,535);
 for(int i=0;i<3;++i){Check(poll()==8,"hover navigates to target card");select(i+1,i+1);}
 Check(poll()==0,"hover never confirms");Sleep(270);Check(poll()==0,"hover remains selection only after animation");
 Move(350,535);Check(poll()==4,"new hover replaces selection");
 Move(650,300);Check(poll()==0,"blank area cancels pending hover");
 Move(550,535);Check(poll()==4,"new pointer target");
 Check(poll(8)==8 && poll()==0,"controller/keyboard input cancels hover");
 Move(550,535);Check(poll()==0,"stationary cursor cannot reclaim selection");
 Move(551,535);Check(poll()==4,"actual mouse movement reclaims selection");
 Scroll(1);Check(poll()==8 && poll()==0,"wheel takes priority");
 Move(551,535);Check(poll()==0,"stationary cursor cannot undo wheel");
 Move(552,535);Clear();Check(poll()==0,"focus cancellation drops pending hover");
 Move(NAN,535);Move(INFINITY,535);Check(poll()==0,"nonfinite pointer ignored");
 main();Move(553,535);Check(poll()==8,"hover target for click override");Push({350,535,false});
 Check(poll()==0,"explicit click replaces hover");Sleep(270);Check(poll()==0x100,"explicit click still confirms");
 main();Move(554,535);Check(poll()==8,"pending hover before script change");
 RegisterScript(script+0x500,0xA711BBFF);REX_STORE_U32(vm+8,script+0x500);REX_STORE_U32(vm+4,script+0x550);
 Check(poll()==0,"hover cannot follow into another script");
 main();REX_STORE_U32(items,5);select(3,2);Move(351,535);Check(poll()==4,"scrolled viewport uses first visible item");
 select(2,1);Check(poll()==4,"scrolled viewport retains target");select(1,0);Check(poll()==0,"scrolled hover never activates");
 main();Move(555,535);Sleep(1050);Check(poll()==0,"stale motion expires");
 // Settings: hovering arrows/value regions must not change the setting.
 Clear();RegisterScript(script,0x6B185A6B);REX_STORE_U32(vm+4,script+optionsSpinnerPc.load());REX_STORE_U16(vm+22,25);
 REX_STORE_U8(types+9,8);REX_STORE_U32(values+9*4,pane);REX_STORE_U32(pane,3);REX_STORE_U32(pane+8,paneData);
 REX_STORE_U32(paneData+16,items);REX_STORE_U32(items,8);REX_STORE_U32(values+23*4,0);
 Move(980,490);
 for(int i=0;i<7;++i){Check(poll()==2,"settings hover only moves highlight");REX_STORE_U32(values+23*4,i+1);}
 Check(poll()==0 && poll()==0,"settings value never altered by hover");
 // Five-row Pause and the No/Yes modal use their own native selection slots.
 Clear();RegisterScript(script,0x0D4B66A5);REX_STORE_U32(vm+4,script+0x317);REX_STORE_U16(vm+22,11);
 REX_STORE_U32(values+6*4,3);REX_STORE_U32(values+7*4,pane);REX_STORE_U32(paneData+8,items);REX_STORE_U32(items,5);
 Move(450,380);Check(poll()==2,"pause hover selects Exit");REX_STORE_U32(values+6*4,4);
 Check(poll()==0 && poll()==0,"pause hover cannot exit race");
 Clear();RegisterScript(script,0xC73FD6C0);REX_STORE_U32(vm+4,script+0x4AA);REX_STORE_U16(vm+22,8);
 REX_STORE_U32(values+4,2);REX_STORE_U32(values+8,0);Move(650,410);
 Check(poll()==2,"modal hover selects Yes");REX_STORE_U32(values+8,1);Check(poll()==0,"modal hover cannot confirm Yes");
 Push({650,410,false});Check(poll()==0x100,"modal click confirms hovered Yes");
 Clear();RegisterScript(script,0xBF7B11E0);REX_STORE_U32(values+8,0);Move(780,315);
 Check(poll()==8,"transmission hover selects MT");REX_STORE_U32(values+8,1);Check(poll()==0,"transmission hover does not accept");
 VirtualFree(base,0,MEM_RELEASE);
 std::cout<<"PASS: hover selects cards/settings/Pause/modal/AT-MT without activating; physical input, wheel, blank space and stale-motion cancellation\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
