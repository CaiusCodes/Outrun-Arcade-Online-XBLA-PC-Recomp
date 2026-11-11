#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include "multiplayer_ping.h"
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace outrun::multiplayer;
void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
int main(){
  WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 1;
  const auto local=htonl(0x7F00000A),peer=htonl(0x7F00000B);
  SOCKET server=INVALID_SOCKET;
  try {
    std::array<uint8_t,12> reply{0x50,0x44,0x51,0x41,0,0,0,1,0,0,0,0};
    Check(ValidPingReply(reply),"Valid reply rejected");
    Check(!ValidPingReply(std::span(reply).first(11)),"Truncated reply accepted");
    reply[11]=1;Check(!ValidPingReply(reply),"Invalid data length accepted");reply[11]=0;
    reply[7]=2;Check(!ValidPingReply(reply),"Unknown protocol accepted");reply[7]=1;
    server=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=peer;address.sin_port=htons(3076);
    Check(server!=INVALID_SOCKET && bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0,"Test endpoint unavailable");
    DWORD timeout=2000;setsockopt(server,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<char*>(&timeout),sizeof(timeout));
    std::thread responder([&]{
      std::array<char,16> query{};sockaddr_in sender{};int len=sizeof(sender);
      const auto received=recvfrom(server,query.data(),int(query.size()),0,reinterpret_cast<sockaddr*>(&sender),&len);
      if(received!=8 || query[3]!=0x51)return;
      std::this_thread::sleep_for(std::chrono::milliseconds(35));
      sendto(server,reinterpret_cast<char*>(reply.data()),int(reply.size()),0,reinterpret_cast<sockaddr*>(&sender),len);
    });
    const auto measured=ProbePing(local,peer);responder.join();
    Check(measured && *measured>=30 && *measured<200,"Real measured RTT missing/out of range");
    const auto begin=std::chrono::steady_clock::now();
    Check(!ProbePing(local,peer),"Silent peer invented a measurement");
    Check(std::chrono::steady_clock::now()-begin<std::chrono::seconds(1),"Probe timeout unbounded");
    {PingMonitor monitor(local);monitor.Observe(peer);Check(monitor.Snapshot().peers==0,"Disabled monitor retained peers");
      monitor.Enable(true);monitor.Observe(local);Check(monitor.Snapshot().peers==0,"Self peer accepted");
      monitor.Observe(peer);Check(monitor.Snapshot().peers==1 && !monitor.Snapshot().milliseconds,"Unmeasured peer fabricated RTT");
      monitor.Enable(false);Check(monitor.Snapshot().peers==0,"Off retained measurement");}
    closesocket(server);server=INVALID_SOCKET;WSACleanup();
    std::cout<<"PASS: real delayed loopback RTT, reply validation, bounded silence, no fabricated/off/self values\n";
  }catch(const std::exception& e){if(server!=INVALID_SOCKET)closesocket(server);WSACleanup();std::cerr<<e.what()<<'\n';return 1;}
}
