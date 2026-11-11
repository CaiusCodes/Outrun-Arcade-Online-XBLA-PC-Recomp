#pragma once
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace outrun {
// One ordered worker. Drain is a lifetime barrier before GPU readback resources
// or guest memory may be released. A bounded backlog prevents unbounded growth.
class RendererWorkQueue {
 public:
  RendererWorkQueue() : thread_([this] { Run(); }) {}
  RendererWorkQueue(const RendererWorkQueue&) = delete;
  ~RendererWorkQueue() {
    Drain();
    { std::lock_guard lock(mutex_); stopping_ = true; }
    ready_.notify_one();
    thread_.join();
  }
  void Submit(std::function<void()> task) {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return tasks_.size() < 64; });
    if (error_) std::rethrow_exception(error_);
    tasks_.push_back(std::move(task));
    ready_.notify_one();
  }
  void Drain() {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return tasks_.empty() && !busy_; });
  }
  void CheckError() {
    std::lock_guard lock(mutex_);
    if (error_) std::rethrow_exception(error_);
  }
 private:
  void Run() {
    for (;;) {
      std::function<void()> task;
      {
        std::unique_lock lock(mutex_);
        ready_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
        if (stopping_) return;
        task = std::move(tasks_.front()); tasks_.pop_front(); busy_ = true;
      }
      changed_.notify_all();
      try { task(); }
      catch (...) { std::lock_guard lock(mutex_); if (!error_) error_ = std::current_exception(); }
      task = {}; // Release captured resources before Drain can return.
      { std::lock_guard lock(mutex_); busy_ = false; }
      changed_.notify_all();
    }
  }
  std::mutex mutex_;
  std::condition_variable ready_, changed_;
  std::deque<std::function<void()>> tasks_;
  bool stopping_ = false, busy_ = false;
  std::exception_ptr error_;
  std::thread thread_;
};
}
