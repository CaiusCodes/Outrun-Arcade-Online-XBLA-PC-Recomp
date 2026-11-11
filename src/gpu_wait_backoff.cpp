#include "gpu_wait_backoff.h"
#include <chrono>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace outrun::gpu_wait {
void Backoff(uint32_t address,uint32_t stamp,bool waiting,WaitSample* sample,uint32_t sleep_us) {
  struct Measurement {
    WaitSample* sample;
    std::chrono::steady_clock::time_point start;
    ~Measurement(){if(sample)sample->elapsed_us=std::chrono::duration<double,std::micro>(
        std::chrono::steady_clock::now()-start).count();}
  } measurement{sample,sample?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{}};
  struct State {
    Budget budget;
    HANDLE timer=CreateWaitableTimerExW(nullptr,nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_MODIFY_STATE|SYNCHRONIZE);
    ~State(){if(timer)CloseHandle(timer);}
  };
  thread_local State state;
  if(!waiting){state.budget.Reset();return;}
  const auto now=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
  if(state.budget.Poll(address,stamp,now)==Action::Yield || !state.timer) {
    SwitchToThread();return;
  }
  // Yield first, then request a bounded host sleep. Never skip the next original
  // fence check, force completion, or touch the game's timeout/device flags.
  sleep_us=sleep_us<50?50:(sleep_us>500?500:sleep_us);
  LARGE_INTEGER due;due.QuadPart=-static_cast<LONGLONG>(sleep_us)*10;
  if(SetWaitableTimer(state.timer,&due,0,nullptr,nullptr,FALSE)) {
    if(sample)sample->action=Action::Sleep;
    if(WaitForSingleObject(state.timer,INFINITE)!=WAIT_OBJECT_0)SwitchToThread();
  } else SwitchToThread();
}
}
