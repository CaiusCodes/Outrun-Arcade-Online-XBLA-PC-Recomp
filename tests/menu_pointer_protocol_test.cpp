#include "generated/default/outrun_init.h"
#include "menu_pointer.h"
#include "menu_footer.h"
#include "keyboard_context.h"
#include <windows.h>
#include <iostream>
int main(){
 auto* base=static_cast<uint8_t*>(VirtualAlloc(nullptr,size_t(1)<<30,MEM_RESERVE,PAGE_NOACCESS));
 if(!base || !VirtualAlloc(base+0x10000000,65536,MEM_COMMIT,PAGE_READWRITE))return 1;
 const uint32_t vm=0x10000100,script=0x10001000,values=0x10002000,types=0x10003000,descriptor=0x10004000,data=0x10004800,items=0x10005000;
 REX_STORE_U32(vm+8,script);REX_STORE_U32(vm+4,script+0x50B);REX_STORE_U32(vm+12,values);REX_STORE_U32(vm+16,types);REX_STORE_U16(vm+22,13);
 REX_STORE_U32(values+16,descriptor);REX_STORE_U8(types+4,8);REX_STORE_U32(descriptor,7);REX_STORE_U32(descriptor+8,data);REX_STORE_U32(data+4,2);REX_STORE_U32(data+8+4,2);REX_STORE_U32(data+4*8,items);REX_STORE_U32(data+4*8+4,8);REX_STORE_U32(items,5);
 using namespace outrun::pointer;RegisterScript(script,0x7FE653EA);Clear();Push({550,535,false});
 if(Input(vm,base,0)!=8)return 2;
 // Tab animation can suspend menu polling; pending activation must survive.
 outrun::keyboard::menuActive=false;EndFrame();
  REX_STORE_U32(data,1);REX_STORE_U32(data+8,1);REX_STORE_U32(data+4,3); // Native arithmetic may change scalar type.
 if(Input(vm,base,0)!=0)return 3;Sleep(270);
 if(Input(vm,base,0)!=0x100 || Input(vm,base,0)!=0)return 4;
 Push({350,535,false});if(Input(vm,base,0)!=4)return 5;
  Clear();REX_STORE_U32(data,0);REX_STORE_U32(data+8,0);if(Input(vm,base,0)!=0)return 6;
 Push({700,535,false});if(Input(vm,base,8)!=8 || Input(vm,base,0)!=0)return 7;
 Push({500,300,true});if(Input(vm,base,0)!=0x200)return 8;
 Push({500,100,false});if(Input(vm,base,0)!=0)return 9;
  // Car select's footer: Back, Select and Color (X) shown; each is clickable.
  footerWidgets=0b1011;
  RegisterScript(script,0xA711BBFF);Push({1007,654,false});
  if(Input(vm,base,0)!=0x8000 || Input(vm,base,0)!=0)return 20;
  if(Footer().size()!=3)return 89;
  Push({300,630,false});if(Input(vm,base,0)!=0x200)return 90;
  Push({960,630,false});if(Input(vm,base,0)!=0x100)return 91;
  Push({1080,660,false});if(Input(vm,base,0)!=0)return 92;
  Move(960,660);if(Input(vm,base,0)!=0)return 93;
  REX_STORE_U32(items,10);
  const uint32_t colourCall=0x10006200,colourArgs=0x10006300;
  REX_STORE_U32(colourCall+12,colourArgs);REX_STORE_U32(colourCall+16,2);REX_STORE_U32(colourArgs,78);
  // All palette sizes and positions: a click on the Color label is exactly one
  // pulse, and the native colour step it makes (next) is left as it is.
  const int paletteSizes[]{8,4,8,8,4,5,4,6,6,4};
  for(int car=0;car<10;++car)for(int old=0;old<paletteSizes[car];++old) {
    REX_STORE_U32(data,car);REX_STORE_U32(data+8,0);
    REX_STORE_U32(values+16,descriptor);REX_STORE_U8(types+4,8);
    REX_STORE_U32(vm+4,script+0x3A7);
    Push({960,652,false});if(Input(vm,base,0)!=0x8000 || Input(vm,base,0)!=0)return 94;
    const auto next=(old+1)%paletteSizes[car];
    REX_STORE_U32(values+16,next);REX_STORE_U8(types+4,2);REX_STORE_U32(colourArgs+4,next);
    REX_STORE_U32(vm+4,script+0x764);AdjustColour(vm,colourCall,base);
    if(REX_LOAD_U32(colourArgs+4)!=next || REX_LOAD_U32(values+16)!=next)return 95;
  }
  REX_STORE_U32(values+16,descriptor);REX_STORE_U8(types+4,8);REX_STORE_U32(data,0);REX_STORE_U32(data+8,0);
  Push({960,652,false});if(Input(vm,base,0)!=0x8000)return 98;
  Clear();if(Input(vm,base,0)!=0)return 99;
  REX_STORE_U32(vm+4,script+0x50B);
  REX_STORE_U32(items,5);
  footerWidgets=0b0111;  // a Y prompt under Select
  RegisterScript(script,0x40CAC580);Push({960,652,false});if(Input(vm,base,0)!=0x800)return 21;
  footerWidgets=0;
  RegisterScript(script,0x7FE653EA);
  // The viewport remains scrolled after moving left from item4 to item3.
  REX_STORE_U32(data,3);REX_STORE_U32(data+8,2);Push({350,535,false});
  if(Input(vm,base,0)!=4)return 15;REX_STORE_U32(data,2);REX_STORE_U32(data+8,1);if(Input(vm,base,0)!=4)return 16;Clear();
  const uint32_t pane=0x10005800,paneData=0x10005A00;
  RegisterScript(script,0x6B185A6B);REX_STORE_U32(vm+4,script+optionsSpinnerPc.load());REX_STORE_U16(vm+22,25);
  REX_STORE_U8(types+9,8);REX_STORE_U32(values+9*4,pane);REX_STORE_U32(pane,3);REX_STORE_U32(pane+8,paneData);REX_STORE_U32(paneData+16,items);REX_STORE_U32(items,8);
  REX_STORE_U32(values+23*4,0);Clear();Push({620,490,false});
  for(int row=0;row<7;++row){if(Input(vm,base,0)!=2)return 10;REX_STORE_U32(values+23*4,row+1);}
  if(Input(vm,base,0)!=4 || Input(vm,base,0)!=0)return 11;
  // A list longer than the popup (eleven rows, as PC Settings once had): nine
  // rows are drawn, so the ninth drawn row is row8 and the footer sits under it.
  Clear();REX_STORE_U32(items,11);REX_STORE_U32(values+23*4,0);Push({620,528,false});
  for(int row=0;row<8;++row){if(Input(vm,base,0)!=2)return 80;REX_STORE_U32(values+23*4,row+1);}
  if(Input(vm,base,0)!=4)return 81;
  Push({910,570,false});if(Input(vm,base,0)!=0x100)return 82;
  REX_STORE_U32(items,2);REX_STORE_U32(values+23*4,0);Push({620,299,false});
  if(Input(vm,base,0)!=2)return 12;REX_STORE_U32(values+23*4,1);if(Input(vm,base,0)!=4)return 13;
  Push({910,343,false});if(Input(vm,base,0)!=0x100)return 14;
  Clear();RegisterScript(script,0xBF7B11E0);REX_STORE_U32(values+8,0);Push({780,315,false});
  if(Input(vm,base,0)!=8)return 17;REX_STORE_U32(values+8,1);if(Input(vm,base,0)!=0)return 18;Sleep(270);if(Input(vm,base,0)!=0x100)return 19;
  // Recorded live Pause VM: native47 PC317, top11, highlight slot6,
  // active list slot7; its field1 is the displayed row array.
  Clear();RegisterScript(script,0x0D4B66A5);REX_STORE_U32(vm+4,script+0x317);REX_STORE_U16(vm+22,11);
  REX_STORE_U32(values+6*4,0);REX_STORE_U32(values+7*4,pane);REX_STORE_U8(types+7,8);
  REX_STORE_U32(paneData+8,items);REX_STORE_U32(paneData+12,8);REX_STORE_U32(items,6);
  Push({450,275,false}); // Help & Options, from Resume.
  if(Input(vm,base,0)!=2)return 22;
  REX_STORE_U32(values+6*4,1);if(Input(vm,base,0)!=2)return 23;
  REX_STORE_U32(values+6*4,2);if(Input(vm,base,0)!=0x100 || Input(vm,base,0)!=0)return 24;
  Push({450,100,false});if(Input(vm,base,0)!=0)return 25;
  Push({450,275,true});if(Input(vm,base,0)!=0x200)return 26;
  Push({950,460,false});if(Input(vm,base,0)!=0x100)return 27;
  Scroll(2);if(Input(vm,base,0)!=2 || Input(vm,base,0)!=0)return 28;
  Sleep(110);if(Input(vm,base,0)!=2 || Input(vm,base,0)!=0)return 29;
  Clear();Scroll(-1);if(Input(vm,base,0)!=1)return 30;
  Clear();Scroll(3);if(Input(vm,base,8)!=8 || Input(vm,base,0)!=0)return 31;
  Clear();RegisterScript(script,0x7FE653EA);REX_STORE_U32(vm+4,script+0x50B);REX_STORE_U32(data,0);REX_STORE_U32(data+8,0);
  Scroll(1);if(Input(vm,base,0)!=8)return 32;
  Clear();Scroll(-1);if(Input(vm,base,0)!=4)return 33;
  // Settings wheel changes highlight, never the selected setting's value.
  Clear();RegisterScript(script,0x6B185A6B);REX_STORE_U32(vm+4,script+optionsSpinnerPc.load());REX_STORE_U16(vm+22,25);REX_STORE_U32(values+23*4,0);
  Scroll(1);if(Input(vm,base,0)!=2)return 34;
  Clear();Scroll(-1);if(Input(vm,base,0)!=1)return 35;
  Clear();Scroll(1);Clear();if(Input(vm,base,0)!=0)return 36;
  Clear();RegisterScript(script,0xC73FD6C0);REX_STORE_U32(vm+4,script+0x4AA);REX_STORE_U16(vm+22,8);
  REX_STORE_U32(values+4,2);REX_STORE_U32(values+8,0);
  Push({650,410,false});if(Input(vm,base,0)!=2)return 37; // Yes from No.
  REX_STORE_U32(values+8,1);if(Input(vm,base,0)!=0x100 || Input(vm,base,0)!=0)return 38;
  Push({650,360,false});if(Input(vm,base,0)!=1)return 39;
  REX_STORE_U32(values+8,0);if(Input(vm,base,0)!=0x100)return 40;
  Push({650,300,false});if(Input(vm,base,0)!=0)return 41;
  Push({650,410,true});if(Input(vm,base,0)!=0x200)return 42;
  Scroll(1);if(Input(vm,base,0)!=2)return 43;Clear();
  // Five-row Pause after removing Achievements: Exit is the last row.
  RegisterScript(script,0x0D4B66A5);REX_STORE_U32(vm+4,script+0x317);REX_STORE_U16(vm+22,11);
  REX_STORE_U32(items,5);REX_STORE_U32(values+6*4,3);
  Push({450,380,false});if(Input(vm,base,0)!=2)return 44;
  REX_STORE_U32(values+6*4,4);if(Input(vm,base,0)!=0x100)return 45;
 VirtualFree(base,0,MEM_RELEASE);
 std::cout<<"PASS: cards/settings/AT-MT/Pause row selection, paced wheel directions, focus/controller cancellation, Back and blank-space rejection\n";
}
