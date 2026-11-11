// Headless test of the actual runtime's timer API, not a simulated scheduler.
#include <rex/thread/timer_queue.h>
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <future>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <cmath>

using namespace std::chrono_literals;
using rex::thread::TimerQueueWaitItem;
using Clock = TimerQueueWaitItem::clock;

static uint64_t CpuTime(HANDLE thread) {
  FILETIME creation, exit, kernel, user;
  if (!GetThreadTimes(thread, &creation, &exit, &kernel, &user)) std::abort();
  ULARGE_INTEGER k{}, u{};
  k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
  return k.QuadPart + u.QuadPart;
}
static void Check(bool ok, const char* name) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}

int main() {
  std::vector<Clock::time_point> ticks;
  ticks.reserve(15000);
  std::promise<HANDLE> started;
  auto handle = started.get_future();
  auto recurring = rex::thread::QueueTimerRecurring([&](void*) {
    if (ticks.empty()) {
      HANDLE real = nullptr;
      Check(DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                            &real, THREAD_QUERY_INFORMATION, FALSE, 0), "thread handle");
      started.set_value(real);
    }
    ticks.push_back(Clock::now());
  }, nullptr, Clock::now()+10ms, 1ms).lock();
  Check(handle.wait_for(2s)==std::future_status::ready, "recurring start");
  HANDLE timer_thread = handle.get();
  auto start = Clock::now();
  auto cpu_start = CpuTime(timer_thread);
  std::this_thread::sleep_for(8s);
  auto cpu_end = CpuTime(timer_thread);
  auto end = Clock::now();
  recurring->Disarm();
  CloseHandle(timer_thread);
  const auto count_after_disarm = ticks.size();
  std::this_thread::sleep_for(20ms);
  Check(ticks.size()==count_after_disarm, "recurring disarm");
  double sum=0, squares=0, max_ms=0;
  for (size_t i=1;i<ticks.size();++i) {
    double dt=std::chrono::duration<double,std::milli>(ticks[i]-ticks[i-1]).count();
    sum+=dt; squares+=dt*dt; max_ms=std::max(max_ms,dt);
  }
  const double n=double(ticks.size()-1), mean=sum/n;
  const double seconds=std::chrono::duration<double>(end-start).count();
  std::printf("{\"cpu_percent_one_core\":%.6f,\"elapsed_seconds\":%.6f,"
              "\"callbacks\":%zu,\"mean_ms\":%.6f,\"sd_ms\":%.6f,\"max_ms\":%.6f}\n",
              double(cpu_end-cpu_start)*1e-5/seconds, seconds,ticks.size(),mean,
              std::sqrt(std::max(0.0,squares/n-mean*mean)),max_ms);
  Check(ticks.size()>6000 && ticks.size()<11000, "recurring rate");

  // An earlier timer inserted while the queue waits on a distant deadline.
  std::atomic<int> cancelled{0};
  auto distant=rex::thread::QueueTimerOnce([&](void*){++cancelled;},nullptr,Clock::now()+5s).lock();
  std::promise<void> early; auto early_done=early.get_future();
  rex::thread::QueueTimerOnce([&](void*){early.set_value();},nullptr,Clock::now()+10ms);
  Check(early_done.wait_for(200ms)==std::future_status::ready,"earlier insertion");
  distant->Disarm();
  Check(cancelled==0,"cancelled timer untouched");

  std::atomic<int> self_count{0};
  std::shared_ptr<TimerQueueWaitItem> self;
  std::atomic<bool> self_ready{false};
  std::promise<void> self_finished; auto self_done=self_finished.get_future();
  self=rex::thread::QueueTimerRecurring([&](void*) {
    if (!self_ready.load(std::memory_order_acquire)) return;
    ++self_count; self->Disarm(); self_finished.set_value();
  },nullptr,Clock::now()+20ms,1ms).lock();
  self_ready.store(true,std::memory_order_release);
  Check(self_done.wait_for(200ms)==std::future_status::ready,"self disarm callback");
  std::this_thread::sleep_for(20ms);
  Check(self_count==1,"self disarm stops recurring");

  // Disarm must wait for an already-running callback to finish.
  std::promise<void> entered, release;
  auto entered_future=entered.get_future(); auto release_future=release.get_future();
  auto blocking=rex::thread::QueueTimerOnce([&](void*) {
    entered.set_value(); release_future.wait();
  },nullptr,Clock::now()+20ms).lock();
  Check(entered_future.wait_for(200ms)==std::future_status::ready,"blocking callback start");
  auto disarmed=std::async(std::launch::async,[&]{blocking->Disarm();});
  Check(disarmed.wait_for(20ms)==std::future_status::timeout,"disarm waits for callback");
  release.set_value();
  Check(disarmed.wait_for(200ms)==std::future_status::ready,"disarm finishes after callback");
  std::puts("PASS: recurring, cancellation, earlier insertion, self-disarm, in-flight disarm; exiting");
}
