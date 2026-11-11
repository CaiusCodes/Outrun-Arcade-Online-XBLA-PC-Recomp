#include "renderer_fence_wait.h"
#include <atomic>
#include <future>
#include <iostream>
struct Fence {
  std::atomic<uint64_t> completed{0};
  HANDLE event=nullptr;
  HRESULT result=S_OK;
  bool finish_on_arm=false,signal_early=false;
  std::promise<void> armed;
  uint64_t GetCompletedValue(){return completed;}
  HRESULT SetEventOnCompletion(uint64_t target,HANDLE h){
    event=h;
    if(finish_on_arm)completed=target;
    if(finish_on_arm || signal_early)SetEvent(h);
    armed.set_value();return result;
  }
};
int main(){
  try {
    Fence ready;ready.completed=8;outrun::WaitForQueryFence(&ready,8);
    if(ready.event)throw std::runtime_error("Already complete fence armed");
    Fence during;during.finish_on_arm=true;outrun::WaitForQueryFence(&during,8);
    Fence delayed;
    auto task=std::async(std::launch::async,[&]{outrun::WaitForQueryFence(&delayed,8);});
    delayed.armed.get_future().wait();delayed.completed=8;SetEvent(delayed.event);task.get();
    for(int mode=0;mode<4;++mode){
      Fence bad;
      if(mode==0)bad.completed=UINT64_MAX;
      if(mode==1)bad.result=E_FAIL;
      if(mode==2)bad.signal_early=true;
      bool rejected=false;
      try{outrun::WaitForQueryFence(&bad,8,1);}catch(const std::runtime_error&){rejected=true;}
      if(!rejected)throw std::runtime_error("Fence failure accepted");
    }
    std::cout<<"PASS: completed/racing/delayed events, device loss, arm failure, premature signal, timeout\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
