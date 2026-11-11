#pragma once
// Optional diagnostics only. No guest clock, scheduling or simulation writes.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <rex/cvar.h>
#include <rex/logging.h>
#if defined(_WIN32)
#include <windows.h>
#endif
namespace outrun::timing {
class Stream {
 public:
  ~Stream() { worker_.request_stop(); wake_.notify_all(); }
  void Tick(const char* name) {
    const bool enabled=rex::cvar::GetFlagByName("vblank_timing_log")=="true";
    const auto now=Clock::now();
    std::lock_guard lock(mutex_);
    if(!enabled) { Reset(now); active_=false; return; }
    if(!active_) { Reset(now); active_=true; }
    if(!worker_.joinable()) {
      name_=name;
      worker_=std::jthread([this](std::stop_token stop){ Run(stop); });
    }
    ++frames_;
    if(previous_!=Clock::time_point{})
      samples_.push_back(std::chrono::duration<double,std::milli>(now-previous_).count());
    previous_=now;
  }
 private:
  using Clock=std::chrono::steady_clock;
  void Reset(Clock::time_point now) { previous_={}; start_=now; frames_=0; samples_.clear(); }
  void Run(std::stop_token stop) {
    std::unique_lock lock(mutex_);
    while(!stop.stop_requested()) {
      if(wake_.wait_for(lock,std::chrono::seconds(10),[&]{return stop.stop_requested();}))break;
      const auto now=Clock::now();
      if(rex::cvar::GetFlagByName("vblank_timing_log")!="true") { Reset(now); active_=false; continue; }
      if(!active_)continue;
      const double elapsed=std::chrono::duration<double>(now-start_).count();
      auto samples=samples_; const auto frames=frames_;
      samples_.clear();frames_=0;start_=now;
      lock.unlock();
      double sum=0,sq=0,lo=0,hi=0;
      if(!samples.empty())lo=hi=samples[0];
      for(double v:samples){sum+=v;sq+=v*v;lo=std::min(lo,v);hi=std::max(hi,v);}
      const double mean=samples.empty()?0:sum/samples.size();
      const double sd=samples.empty()?0:std::sqrt(std::max(0.0,sq/samples.size()-mean*mean));
      size_t slow=0;for(double v:samples)if(v>1.5*mean)++slow;
      bool focused=false;
#if defined(_WIN32)
      DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);focused=pid==GetCurrentProcessId();
#endif
      // With count=0 spacing is undefined and encoded as zero; FPS is really zero.
      REXLOG_INFO("[frame_timing] {} focused={} fullscreen={} count={} fps={:.3f} mean_ms={:.6f} sd_ms={:.6f} min_ms={:.6f} max_ms={:.6f} over_1_5_mean={}",
        name_,focused,rex::cvar::GetFlagByName("fullscreen"),frames,frames/elapsed,mean,sd,lo,hi,slow);
      lock.lock();
    }
  }
  std::mutex mutex_;
  std::condition_variable wake_;
  Clock::time_point previous_{},start_{};
  std::vector<double> samples_;
  uint64_t frames_=0;
  bool active_=false;
  std::string name_;
  // Destroy/join first so every referenced field is still alive during shutdown.
  std::jthread worker_;
};
}
