#include "gpu_wait_backoff.h"
#include <chrono>
#include <cstdio>
#include <thread>
#include <initializer_list>
int main() {
  using namespace std::chrono_literals;
  bool passed=true;
  for(uint32_t requested_us : {500u,100u}) {
  bool run_passed=false;
  // Exercise the real host timer, including thread-local handle cleanup.
  std::thread worker([&] {
    outrun::gpu_wait::Backoff(128,7,true);
    std::this_thread::sleep_for(1ms);
    const auto start=std::chrono::steady_clock::now();
    double measured_us=0;
    for(unsigned i=0;i<100;++i){
      outrun::gpu_wait::WaitSample sample;
      outrun::gpu_wait::Backoff(128,7,true,&sample,requested_us);
      if(sample.elapsed_us<0)return;
      measured_us+=sample.elapsed_us;
    }
    const auto elapsed=std::chrono::steady_clock::now()-start;
    outrun::gpu_wait::Backoff(128,7,false);
    run_passed=elapsed>=5ms && elapsed<2s && measured_us>0 &&
      measured_us<=std::chrono::duration<double,std::micro>(elapsed).count();
    std::printf("100 host waits requesting %u us: %.3f ms\n",requested_us,std::chrono::duration<double,std::milli>(elapsed).count());
  });
  worker.join();
  passed=passed && run_passed;
  }
  return passed?0:1;
}
