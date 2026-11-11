#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include "multiplayer_ping.h"
#include <algorithm>
#include <array>
#include <vector>

namespace outrun::multiplayer {
bool ValidPingReply(std::span<const uint8_t> b) {
  if (b.size()<12 || b[0]!=0x50 || b[1]!=0x44 || b[2]!=0x51 || b[3]!=0x41 ||
      b[4] || b[5] || b[6] || b[7]!=1) return false;
  const auto length=uint32_t(b[8])<<24 | uint32_t(b[9])<<16 | uint32_t(b[10])<<8 | b[11];
  return length==b.size()-12;
}
std::optional<double> ProbePing(uint32_t localAddress,uint32_t peerAddress) {
  const SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
  if (s==INVALID_SOCKET) return {};
  struct Close { SOCKET s; ~Close(){closesocket(s);} } close{s};
  sockaddr_in local{},target{};
  local.sin_family=target.sin_family=AF_INET;
  local.sin_addr.s_addr=localAddress;
  target.sin_addr.s_addr=peerAddress;
  target.sin_port=htons(3076);
  if (bind(s,reinterpret_cast<sockaddr*>(&local),sizeof(local))) return {};
  constexpr char query[]{0x50,0x44,0x51,0x51,0,0,0,1};
  const auto start=std::chrono::steady_clock::now();
  if (sendto(s,query,sizeof(query),0,reinterpret_cast<sockaddr*>(&target),sizeof(target))!=sizeof(query)) return {};
  fd_set read; FD_ZERO(&read); FD_SET(s,&read);
  timeval timeout{0,200000};
  if (select(0,&read,nullptr,nullptr,&timeout)<=0) return {};
  std::array<uint8_t,65507> bytes{};
  sockaddr_in source{}; int length=sizeof(source);
  const int received=recvfrom(s,reinterpret_cast<char*>(bytes.data()),int(bytes.size()),0,
      reinterpret_cast<sockaddr*>(&source),&length);
  const auto finish=std::chrono::steady_clock::now();
  if (received<12 || source.sin_addr.s_addr!=peerAddress || source.sin_port!=target.sin_port ||
      !ValidPingReply({bytes.data(),size_t(received)})) return {};
  return std::chrono::duration<double,std::milli>(finish-start).count();
}
PingMonitor::PingMonitor(uint32_t local):local_(local),worker_([this]{Run();}) {}
PingMonitor::~PingMonitor() {
  {std::lock_guard lock(mutex_);stop_=true;}
  changed_.notify_all();
  worker_.join();
}
void PingMonitor::Enable(bool enabled) {
  {std::lock_guard lock(mutex_);if(enabled_==enabled)return;enabled_=enabled;++generation_;peers_.clear();}
  changed_.notify_all();
}
void PingMonitor::Observe(uint32_t peer) {
  const auto first=ntohl(peer)>>24;
  if (!first || first>=224 || peer==local_) return;
  std::lock_guard lock(mutex_);
  if(!enabled_)return;
  const auto now=Clock::now();
  std::erase_if(peers_,[&](const auto& p){return now-p.second.traffic>std::chrono::seconds(5);});
  if (peers_.contains(peer) || peers_.size()<5) peers_[peer].traffic=now;
}
PingSnapshot PingMonitor::Snapshot() {
  std::lock_guard lock(mutex_);
  PingSnapshot result;
  if (!enabled_) return result;
  const auto now=Clock::now();
  bool missing=false;
  for (const auto& [address,peer]:peers_) {
    if (now-peer.traffic>std::chrono::seconds(5))continue;
    ++result.peers;
    if (!peer.milliseconds || now-peer.sample>std::chrono::seconds(3)) missing=true;
    else result.milliseconds=std::max(result.milliseconds.value_or(0),*peer.milliseconds);
  }
  // Don't present a good number while any active peer has stopped answering.
  if (missing) result.milliseconds.reset();
  return result;
}
void PingMonitor::Run() {
  WSADATA data{};
  if (WSAStartup(MAKEWORD(2,2),&data)) return;
  std::unique_lock lock(mutex_);
  while (!stop_) {
    changed_.wait_for(lock,std::chrono::seconds(1),[&]{return stop_;});
    if (stop_) break;
    if (!enabled_) continue;
    const auto generation=generation_;
    std::vector<uint32_t> addresses;
    const auto now=Clock::now();
    for (const auto& [address,peer]:peers_)
      if (now-peer.traffic<=std::chrono::seconds(5)) addresses.push_back(address);
    for (auto address:addresses) {
      lock.unlock();
      const auto measured=ProbePing(local_,address);
      lock.lock();
      if(stop_ || generation!=generation_)break;
      if (auto p=peers_.find(address);p!=peers_.end()) {
        p->second.milliseconds=measured;
        p->second.sample=Clock::now();
      }
    }
  }
  lock.unlock();
  WSACleanup();
}
}
