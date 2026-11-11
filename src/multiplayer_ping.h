#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <thread>

namespace outrun::multiplayer {
bool ValidPingReply(std::span<const uint8_t> bytes);
// IPv4 parameters are in network byte order. Requires initialized Winsock.
std::optional<double> ProbePing(uint32_t local, uint32_t peer);
struct PingSnapshot { unsigned peers=0; std::optional<double> milliseconds; };
class PingMonitor {
 public:
  explicit PingMonitor(uint32_t local);
  ~PingMonitor();
  void Enable(bool enabled);
  void Observe(uint32_t peer);
  PingSnapshot Snapshot();
 private:
  using Clock=std::chrono::steady_clock;
  struct Peer { Clock::time_point traffic{}, sample{}; std::optional<double> milliseconds; };
  void Run();
  uint32_t local_;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::map<uint32_t,Peer> peers_;
  bool stop_=false, enabled_=false;
  uint64_t generation_=0;
  std::thread worker_;
};
}
