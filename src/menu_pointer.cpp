#include "generated/default/outrun_init.h"
#include "menu_pointer.h"
#include "keyboard_context.h"
#include "menu_footer.h"
#include <unordered_map>
#include <optional>
#include <cmath>
#include <algorithm>
#include <windows.h>
namespace outrun::pointer {
namespace {
std::unordered_map<uint32_t,uint32_t> scripts;
bool titlePolled=false;
struct PreviousColour {uint32_t vm=0,script=0;int count=0;uint64_t epoch=0,deadline=0;} previousColour;
// Original singleplayer constant5: number of colour choices per car card.
constexpr int colourCounts[]{8,4,8,8,4,5,4,6,6,4};
uint64_t wheelDeadline=0,wheelReadyAt=0;
uint32_t wheelScript=0;
struct Hover {float x,y;uint64_t deadline;};
std::optional<Hover> hover;
std::optional<Hover> lastPosition;
// Rows the shared native Options popup draws; later list rows are not shown.
constexpr int kShownRows=9;
std::mutex footerMutex;
std::vector<FooterButton> published;
uint64_t publishedUntil=0;
void Publish(std::vector<FooterButton> items) {
  std::lock_guard lock(footerMutex);
  published=std::move(items);publishedUntil=GetTickCount64()+200;
}
std::vector<FooterButton> CardFooter(uint32_t hash) {
  const auto shown=footerWidgets.load();
  std::vector<FooterButton> items;
  if(shown&1)items.push_back(hash==0x7FE653EA?kMainMenuExit:kCardBack);
  if(shown&2)items.push_back(kCardSelect);
  // singleplayer checks held Colour (0x8000), not pressed X (0x400): one poll
  // followed by release is its short-tap cycle.
  if(shown&8){auto lower=kCardLower;lower.action=hash==0xA711BBFF?0x8000:0x400;items.push_back(lower);}
  else if(shown&4){auto lower=kCardLower;lower.action=0x800;items.push_back(lower);}
  return items;
}
uint32_t FooterAction(const std::vector<FooterButton>& items,const Click& click) {
  for(const auto& item:items)if(item.Contains(click.x,click.y))return item.action;
  return 0;
}
struct Plan {uint32_t script=0;int target=-1;uint64_t deadline=0,readyAt=0;uint32_t action=0x100;bool vertical=false;uint64_t epoch=0;} plan;
uint32_t ArrayEntry(uint8_t* base,uint32_t array,int index,uint32_t type=~0u) {
  if(array<0x10000000 || array>0x80000000 || index<0 || REX_LOAD_U32(array)>64 || uint32_t(index)>=REX_LOAD_U32(array))return 0;
  const auto data=REX_LOAD_U32(array+8);
  if(data<0x10000000 || data>0x80000000)return 0;
  const auto entry=data+index*8;
  if(type!=~0u && REX_LOAD_U32(entry+4)!=type)return 0;
  return REX_LOAD_U32(entry);
}
}
// The native short-tap path has already computed next colour when command78
// runs at PC764. Replace that one selection with previous, and keep local4 in
// sync for the native swatch refresh. The original command still applies it.
void AdjustColour(uint32_t vm,uint32_t call,uint8_t* base) {
  if(!previousColour.count)return;
  const auto pending=previousColour;
  if(pending.epoch!=cancelEpoch.load() || GetTickCount64()>pending.deadline || controllerConnected){previousColour={};return;}
  if(vm!=pending.vm || REX_LOAD_U32(vm+8)!=pending.script ||
     REX_LOAD_U32(vm+4)!=pending.script+0x764 || REX_LOAD_U32(call+16)!=2)return;
  const auto args=REX_LOAD_U32(call+12),values=REX_LOAD_U32(vm+12),types=REX_LOAD_U32(vm+16);
  if(REX_LOAD_U32(args)!=78)return;
  previousColour={};
  const auto next=REX_LOAD_U32(args+4);
  const auto kind=REX_LOAD_U8(types+4);
  if(next>=uint32_t(pending.count) || (kind!=2 && kind!=3) || REX_LOAD_U32(values+16)!=next)return;
  const auto previous=(next+pending.count-2)%pending.count;
  REX_STORE_U32(args+4,previous);REX_STORE_U32(values+16,previous);
}
std::vector<FooterButton> Footer(){
  std::lock_guard lock(footerMutex);
  if(GetTickCount64()>publishedUntil)return {};
  return published;
}
void Clear(){std::lock_guard lock(mutex);clicks.clear();wheel.clear();hover.reset();wheelDeadline=0;wheelReadyAt=0;wheelScript=0;++cancelEpoch;}
void Push(Click c){if(inputHeld)return;std::lock_guard lock(mutex);hover.reset();if(clicks.size()<8)clicks.push_back(c);}
void Move(float x,float y){
  if(inputHeld)return;
  std::lock_guard lock(mutex);
  if(!std::isfinite(x) || !std::isfinite(y))return;
  if(lastPosition && lastPosition->x==x && lastPosition->y==y)return;
  lastPosition=Hover{x,y,0};hover=Hover{x,y,GetTickCount64()+1000};
}
void Scroll(int steps){
  if(inputHeld)return;
  std::lock_guard lock(mutex);
  clicks.clear();hover.reset();++cancelEpoch;
  wheelDeadline=GetTickCount64()+1000;
  while(steps && wheel.size()<16){wheel.push_back(steps>0?1:-1);steps+=steps>0?-1:1;}
}
void RegisterScript(uint32_t address,uint32_t hash){scripts[address]=hash;}
bool FastHoverAnimation(uint32_t vm,uint8_t* base) {
  if(plan.target<0 || plan.action!=0 || plan.vertical ||
     plan.epoch!=cancelEpoch.load() || GetTickCount64()>plan.deadline)return false;
  const auto script=REX_LOAD_U32(vm+8),pc=REX_LOAD_U32(vm+4)-script;
  const auto found=scripts.find(script);
  // menu_manager's card-navigation branch only. Page entry/exit animations,
  // dialogs and all physical-input navigation retain their normal waits.
  return found!=scripts.end() && found->second==0x1C39026A && pc>=0x510 && pc<0x7E8;
}
void EndFrame(){titleActive=titlePolled;titlePolled=false;if(plan.target>=0 && GetTickCount64()>plan.deadline){Clear();plan={};}}
uint32_t Input(uint32_t vm,uint8_t* base,uint32_t original) {
  if(inputHeld){Clear();return 0;}
  if(plan.target>=0 && plan.epoch!=cancelEpoch.load())plan={};
  const auto script=REX_LOAD_U32(vm+8),pc=REX_LOAD_U32(vm+4)-script;
  const auto found=scripts.find(script);if(found==scripts.end())return original;
  const auto hash=found->second;
  polledScript=hash;
  if(hash==0x0D4B66A5)pausePolledAt=GetTickCount64();
  else if(hash==0x7FE653EA)mainMenuPolledAt=GetTickCount64();
  if(hash==0xA711BBFF)carFooterUntil=GetTickCount64()+150;
  else if(hash!=0x1C39026A){carFooterUntil=0;previousColour={};}
  if(hash==0x0E6B27A4){titlePolled=true;return original;}
  const auto values=REX_LOAD_U32(vm+12),types=REX_LOAD_U32(vm+16);
  const bool pause=hash==0x0D4B66A5;
  auto selectHover=[&](bool vertical,auto hitTest) {
    std::lock_guard lock(mutex);
    if(!hover)return;
    const auto point=*hover;hover.reset();
    // Finish an explicit click before accepting incidental pointer motion.
    if(plan.target>=0 && plan.action!=0)return;
    plan={};
    if(GetTickCount64()>point.deadline)return;
    const int target=hitTest(point.x,point.y);
    if(target>=0)plan={script,target,GetTickCount64()+1500,0,0,vertical,cancelEpoch.load()};
  };
  auto scroll=[&](bool vertical)->uint32_t {
    std::lock_guard lock(mutex);
    const auto now=GetTickCount64();
    if(now>wheelDeadline || (!wheel.empty() && wheelScript && wheelScript!=script))wheel.clear();
    if(wheel.empty()){wheelScript=0;return 0;}
    wheelScript=script;
    if(now<wheelReadyAt)return 0;
    const int step=wheel.front();wheel.pop_front();wheelReadyAt=now+100;
    plan={};return vertical?(step>0?2:1):(step>0?8:4);
  };
  if(original){plan={};Clear();return original;}
  // Shared message-box selection and button count use absolute slots2/1.
  // native47 resumes at4AA; the two-choice No/Yes pane uses pitch44.
  if(hash==0xC73FD6C0 && pc==0x4AA && REX_LOAD_U16(vm+22)>=8) {
    const int count=int(REX_LOAD_U32(values+4));
    const int current=int(REX_LOAD_U32(values+8));
    if(count==2 && current>=0 && current<count) {
      if(auto action=scroll(true))return action;
      {
        std::lock_guard lock(mutex);
        if(!clicks.empty()) {
          const auto click=clicks.front();clicks.pop_front();plan={};wheel.clear();
          if(click.back)return 0x200;
          if(click.x>=390 && click.x<=940 && click.y>=344 && click.y<432)
            plan={script,int((click.y-344)/44),GetTickCount64()+1500,0,0x100,true,cancelEpoch.load()};
        }
      }
      selectHover(true,[&](float x,float y){return x>=390 && x<=940 && y>=344 && y<432?int((y-344)/44):-1;});
      if(plan.target<0)return 0;
      if(plan.script!=script || !plan.vertical || GetTickCount64()>plan.deadline){plan={};return 0;}
      if(current==plan.target){const auto action=plan.action;plan={};return action;}
      return current<plan.target?2:1;
    }
  }
  // Pause owns a scalar selection above its active list, rather than the
  // seven-field horizontal menu descriptor. native47 returns at PC 0x317.
  if(pause && pc==0x317) {
    const auto top=REX_LOAD_U16(vm+22);
    const int current=top>=5?int(REX_LOAD_U32(values+(top-5)*4)):-1;
    const auto list=top>=4?REX_LOAD_U32(values+(top-4)*4):0;
    const auto rows=ArrayEntry(base,list,1);
    const int count=rows>=0x10000000 && rows<=0x80000000?int(REX_LOAD_U32(rows)):0;
    if(count>=1 && count<=11 && current>=0 && current<count) {
      if(auto action=scroll(true))return action;
      const int firstTop=246-16*count,footerTop=firstTop+48*count;
      const std::vector<FooterButton> footer{PauseBack(float(footerTop)),PauseSelect(float(footerTop))};
      Publish(footer);
      {
        std::lock_guard lock(mutex);
        if(!clicks.empty()) {
          const auto click=clicks.front();clicks.pop_front();plan={};wheel.clear();
          if(click.back)return 0x200;
          if(const auto action=FooterAction(footer,click))return action;
          if(click.x>=255 && click.x<=1005 && click.y>=firstTop && click.y<footerTop)
            plan={script,int((click.y-firstTop)/48),GetTickCount64()+1500,0,0x100,true,cancelEpoch.load()};
        }
      }
      selectHover(true,[&](float x,float y){return x>=255 && x<=1005 && y>=firstTop && y<footerTop?int((y-firstTop)/48):-1;});
      if(plan.target<0)return 0;
      if(plan.script!=script || !plan.vertical || GetTickCount64()>plan.deadline){plan={};return 0;}
      if(current==plan.target){const auto action=plan.action;plan={};return action;}
      return current<plan.target?2:1;
    }
    plan={};Clear();return 0;
  }
  // The native Options spinner function has its own frame-local selection,
  // above the persistent tab descriptors. Its rows are the active pane's
  // descriptor[2]; opcode07 reads the highlighted row relative to the top.
  if(hash==0x6B185A6B && pc==optionsSpinnerPc.load() && REX_LOAD_U8(types+9)==8) {
    const auto rows=ArrayEntry(base,REX_LOAD_U32(values+9*4),2);
    const auto top=REX_LOAD_U16(vm+22);
    const int count=rows>=0x10000000 && rows<=0x80000000?int(REX_LOAD_U32(rows)):0;
    // SMOS opcode07 indexes backwards from the current stack top. Immediately
    // before native47 pushes its result, the highlighted row is top minus 2.
    const int current=top>=2?int(REX_LOAD_U32(values+(top-2)*4)):-1;
    if(count>=2 && count<=11 && current>=0 && current<count) {
      if(auto action=scroll(true))return action;
      // Shared native popup changes its origin with the number of rows it
      // shows, at most nine: the PC Settings list holds eleven, and the two
      // after Render Scale are never drawn. Verified against the nine shown
      // Settings rows and the two-row Audio pane.
      const int shown=std::min(count,kShownRows);
      const int firstTop=264-16*shown,footerTop=firstTop+48*shown;
      const std::vector<FooterButton> footer{PopupCancel(float(footerTop)),PopupAccept(float(footerTop))};
      Publish(footer);
      if(original){plan={};Clear();return original;}
      {
        std::lock_guard lock(mutex);
        if(!clicks.empty()) {
          const auto click=clicks.front();clicks.pop_front();plan={};
          if(click.back)return 0x200;
          if(const auto action=FooterAction(footer,click))return action;
          if(click.x>=255 && click.x<=1005 && click.y>=firstTop && click.y<footerTop) {
            const int row=int((click.y-firstTop)/48);
            const uint32_t action=click.x>=945?8:click.x>=595&&click.x<=645?4:click.x>=650?8:0;
            plan={script,row,GetTickCount64()+1500,0,action,true,cancelEpoch.load()};
          }
        }
      }
      selectHover(true,[&](float x,float y){return x>=255 && x<=1005 && y>=firstTop && y<footerTop?int((y-firstTop)/48):-1;});
      if(plan.target<0)return original;
      if(plan.script!=script || !plan.vertical || GetTickCount64()>plan.deadline){plan={};return original;}
      if(current==plan.target){const auto action=plan.action;plan={};return action;}
      return current<plan.target?2:1;
    }
  }
  int slot=-1;
  const bool transmission=hash==0xBF7B11E0;
  if(hash==0x7FE653EA)slot=4;
  if(hash==0x6B185A6B && pc<optionsEditorStart.load())slot=1;
  if(slot<0 && !transmission && !(hash==0x6B185A6B && pc>=optionsEditorStart.load())) {
    for(int i=0;i<14 && i<REX_LOAD_U16(vm+22);i++) {
      if(REX_LOAD_U8(types+i)!=8)continue;
      const auto candidate=REX_LOAD_U32(values+i*4);
      if(candidate<0x10000000 || candidate>0x80000000 || REX_LOAD_U32(candidate)!=7)continue;
      const auto items=ArrayEntry(base,candidate,4);
      if(items>=0x10000000 && items<=0x80000000 && REX_LOAD_U32(items)>0 && REX_LOAD_U32(items)<=16)slot=i;
    }
  }
  // The card menus and other screens under menu_manager's footer; the
  // transmission popup covers it.
  const auto footer=transmission?std::vector<FooterButton>{}:CardFooter(hash);
  Publish(footer);
  if(slot<0 && !transmission) {
    if(auto action=scroll(true))return action;
    std::lock_guard lock(mutex);
    if(clicks.empty())return original;
    const auto click=clicks.front();clicks.pop_front();plan={};
    if(!click.back)if(const auto action=FooterAction(footer,click))return original|action;
    return original | (click.back?0x200:0x100);
  }
  if(!transmission && REX_LOAD_U8(types+slot)!=8)return original;
  const auto descriptor=transmission?0:REX_LOAD_U32(values+slot*4);
  const auto current=transmission?int(REX_LOAD_U32(values+8)):int(ArrayEntry(base,descriptor,0));
  // Field 1 is the selected slot (0..3) within the visible viewport. The
  // viewport stays scrolled when moving backwards, so current-3 is not enough.
  const int visibleSlot=transmission?current:int(ArrayEntry(base,descriptor,1));
  const int first=current-visibleSlot;
  const auto items=transmission?0:ArrayEntry(base,descriptor,4);
  if(!transmission && (items<0x10000000 || items>0x80000000))return original;
  const auto count=transmission?2:int(REX_LOAD_U32(items));
  if(count<1 || count>16 || current<0 || current>=count || visibleSlot<0 || visibleSlot>3 || first<0 || first>=count)return original;
  if(auto action=scroll(false))return action;
  if(original){plan={};Clear();return original;}
  {
    std::lock_guard lock(mutex);
    if(!clicks.empty()) {
      const auto click=clicks.front();clicks.pop_front();plan={};previousColour={};
      if(click.back)return 0x200;
      if(transmission) {
        if(click.y>=270 && click.y<=445) {
          const int target=click.x>=380&&click.x<=615?0:click.x>=680&&click.x<=885?1:-1;
          if(target>=0)plan={script,target,GetTickCount64()+1500,0,0x100,false,cancelEpoch.load()};
        }
      } else {
      if(const auto action=FooterAction(footer,click))return action;
      if(click.y>=490 && click.y<=608) {
        if(click.x>=998 && click.x<=1060)return 8;
        if(click.x>=210 && click.x<265)return 4;
        if(click.x>=265 && click.x<998) {
          const int index=int((click.x-265)/183);
          if(first+index<count)plan={script,first+index,GetTickCount64()+1500,0,0x100,false,cancelEpoch.load()};
        }
      }
      }
    }
  }
  selectHover(false,[&](float x,float y){
    if(transmission)return y>=270 && y<=445?(x>=380 && x<=615?0:x>=680 && x<=885?1:-1):-1;
    if(y<490 || y>608 || x<265 || x>=998)return -1;
    const int target=first+int((x-265)/183);
    return target<count?target:-1;
  });
  if(plan.target<0)return original;
  if(plan.script!=script || GetTickCount64()>plan.deadline){plan={};return original;}
  // Native tab animation temporarily rejects A. Let the selected highlight
  // settle before activating; otherwise the click only changes the tab.
  if(current==plan.target){if(plan.action==0){plan={};return 0;}if(!plan.readyAt)plan.readyAt=GetTickCount64()+250;if(GetTickCount64()<plan.readyAt)return 0;const auto action=plan.action;plan={};return action;}
  plan.readyAt=0;
  return current<plan.target ? 8 : 4;
}
}
