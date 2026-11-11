#include "audio_diagnostics.h"
#include "keyboard_context.h"
#include "menu_pointer.h"
#include "audio_trace_budget.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <windows.h>
#include <atomic>
#include <array>
#include <mutex>
#include <cstring>
#include <string_view>
REXCVAR_DEFINE_BOOL(outrun_audio_trace,false,"OutRun",
                   "Optional bounded input/sound trace for exhaust-pop diagnosis")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(outrun_audio_trace_focus,false,"OutRun",
                   "Diagnostic: separate traffic/drift lifecycle and sampled tire gain trace")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
namespace outrun::diagnostics {
namespace {
std::atomic<uint64_t> start{0};
std::atomic<uint64_t> raceStart{0},steerUntil{0};
std::atomic<unsigned> records{0};
TraceBudget focusedBudget;
std::mutex inputMutex;
struct Snapshot{bool valid=false;uint64_t last=0;rex::input::X_INPUT_GAMEPAD pad{};};
std::array<Snapshot,8> snapshots;
bool Racing(){return !outrun::keyboard::menuActive.load() && !outrun::pointer::titleActive.load();}
void Arm() {
  if(!REXCVAR_GET(outrun_audio_trace) || !Racing())return;
  const auto now=GetTickCount64();uint64_t zero=0;
  if(raceStart.compare_exchange_strong(zero,now))REXLOG_INFO("[audio_trace] armed on driving input; title waiting does not consume capture time");
}
bool Permit(uint64_t& time,bool sampledGain=false) {
  if(!REXCVAR_GET(outrun_audio_trace))return false;
  const auto now=GetTickCount64(),armed=raceStart.load();
  if(!armed || now-armed>300000)return false;
  time=AudioTraceTime();
  if(REXCVAR_GET(outrun_audio_trace_focus))return sampledGain || focusedBudget.Event();
  const auto n=records.fetch_add(1);
  if(n==10000)REXLOG_INFO("[audio_trace] record limit reached; tracing stops, gameplay unchanged");
  return n<10000;
}
}
uint64_t AudioTraceTime() {
  const auto now=GetTickCount64();uint64_t zero=0;start.compare_exchange_strong(zero,now);return now-start.load();
}
void Key(uint16_t key,bool down,bool repeat) {
  if(down && !repeat && (key==0x57 || key==0x41 || key==0x44))Arm();
  if(!repeat && (key==0x41 || key==0x44 || key==0x57 || key==0x53) && Racing() && raceStart.load())steerUntil=GetTickCount64()+200;
  uint64_t time;if(!Permit(time))return;
  REXLOG_INFO("[audio_trace] ms={} key={:X} down={} repeat={} menu={}",time,key,down,repeat,outrun::keyboard::menuActive.load());
}
void Input(const char* source,uint32_t user,const rex::input::X_INPUT_STATE& state) {
  if(!REXCVAR_GET(outrun_audio_trace) || user>=4)return;
  if(source[0]!='k' && state.gamepad.right_trigger>0)Arm();
  uint64_t time;
  {
    std::lock_guard lock(inputMutex);
    const auto now=GetTickCount64();auto& old=snapshots[user+(source[0]=='k'?4:0)];
    if(old.valid && std::memcmp(&old.pad,&state.gamepad,sizeof(old.pad))==0)return;
    // Controller analogue jitter is rate-limited; discrete keyboard changes
    // remain exact. No controller state, key mapping or game audio is changed.
    if(source[0]!='k' && old.valid && now-old.last<50 && old.pad.buttons==state.gamepad.buttons && old.pad.right_trigger==state.gamepad.right_trigger && old.pad.left_trigger==state.gamepad.left_trigger)return;
    old={true,now,state.gamepad};
    if(source[0]!='k' && Racing() && raceStart.load() && (state.gamepad.thumb_lx>4000 || state.gamepad.thumb_lx<-4000))steerUntil=now+200;
    if(!Permit(time))return;
  }
  const auto& p=state.gamepad;
  REXLOG_INFO("[audio_trace] ms={} source={} user={} buttons={:04X} LT={} RT={} LX={} LY={} RX={} RY={} menu={}",time,source,user,uint16_t(p.buttons),unsigned(p.left_trigger),unsigned(p.right_trigger),int16_t(p.thumb_lx),int16_t(p.thumb_ly),int16_t(p.thumb_rx),int16_t(p.thumb_ry),outrun::keyboard::menuActive.load());
}
void Sound(uint32_t event,uint32_t caller,const char* stage) {
  uint64_t time;if(!Permit(time))return;
  REXLOG_INFO("[audio_trace] ms={} sound-stage={} event={:08X} sample={} caller={:08X} menu={}",time,stage,event,event&0x7ff,caller,outrun::keyboard::menuActive.load());
}
void Voice(uint32_t voice,uint32_t flags,uint32_t caller,double p1,double p2,double p3,double p4,double p5) {
  uint64_t time;if(!Permit(time))return;
  REXLOG_INFO("[audio_trace] ms={} sound-stage=voice-start voice={:08X} flags={:08X} caller={:08X} p1={} p2={} p3={} p4={} p5={}",time,voice,flags,caller,p1,p2,p3,p4,p5);
}
void Parameter(uint32_t index,uint32_t control,uint32_t value,uint32_t caller) {
  // Parameters can update continuously. Capture short windows around steering
  // instead of exhausting the bounded record budget during idle driving.
  if(!REXCVAR_GET(outrun_audio_trace) || REXCVAR_GET(outrun_audio_trace_focus) || GetTickCount64()>steerUntil.load())return;
  uint64_t time;if(!Permit(time))return;
  REXLOG_INFO("[audio_trace] ms={} sound-stage=parameter index={} control={:08X} value={:08X} caller={:08X}",time,index,control,value,caller);
}
void LoopVoice(uint32_t group,uint32_t layer,uint32_t sample,int32_t result) {
  uint64_t time;if(!Permit(time))return;
  REXLOG_INFO("[audio_trace] ms={} sound-stage=loop-result group={} layer={} sample={} result={}",time,group,layer,sample,result);
}
bool AudioTraceRecording(){return REXCVAR_GET(outrun_audio_trace) && raceStart.load()!=0;}
bool AudioTransitionWindow(){return AudioTraceRecording() && GetTickCount64()<=steerUntil.load();}
void LoopTransition(const char* stage,uint32_t group,uint32_t layer,uint32_t object,int32_t handle,double left,double right){
  const bool focus=REXCVAR_GET(outrun_audio_trace_focus);
  if(focus ? !AudioTraceRecording() : !AudioTransitionWindow())return;
  const bool gain=focus && std::string_view(stage)=="loop-volume";
  if(gain && (group!=3 || !focusedBudget.TireGain(layer,object,handle,GetTickCount64())))return;
  uint64_t time;if(!Permit(time,gain))return;
  REXLOG_INFO("[audio_trace] ms={} sound-stage={} group={} layer={} object={:08X} handle={} left={} right={}",time,stage,group,layer,object,handle,left,right);
}
void LoopMapping(uint32_t group,uint32_t layer,uint32_t sample,uint32_t voice) {
  uint64_t time;if(!Permit(time))return;
  REXLOG_INFO("[audio_trace] ms={} sound-stage=loop-map group={} layer={} sample={} voice={:08X}",time,group,layer,sample,voice);
}
}
