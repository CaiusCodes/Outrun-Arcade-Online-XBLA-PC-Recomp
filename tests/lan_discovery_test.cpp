#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include "lan_discovery.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace outrun::local_sessions;
void Check(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
uint32_t Address(const char* text){uint32_t n=0;Check(inet_pton(AF_INET,text,&n)==1,"bad fixture IP");return n;}
struct Host {
  SOCKET socket=INVALID_SOCKET;std::atomic<bool> stop{};std::jthread thread;
  Host(const char* address,uint8_t id) {
    socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);Check(socket!=INVALID_SOCKET,"Host socket failed");
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_port=htons(3076);local.sin_addr.s_addr=Address(address);
    Check(bind(socket,reinterpret_cast<sockaddr*>(&local),sizeof(local))==0,"Host bind failed");
    DWORD timeout=50;setsockopt(socket,SOL_SOCKET,SO_RCVTIMEO,reinterpret_cast<const char*>(&timeout),sizeof(timeout));
    thread=std::jthread([this,id,ina=local.sin_addr.s_addr?local.sin_addr.s_addr:Address("127.0.0.1")] {
      while(!stop) {
        uint8_t request[64]{};sockaddr_in peer{};int size=sizeof(peer);int n=recvfrom(socket,reinterpret_cast<char*>(request),sizeof(request),0,reinterpret_cast<sockaddr*>(&peer),&size);if(n!=8)continue;
        if(Read32(request)==0x50445151) {
          if(id==5)continue;
          std::array<uint8_t,52> qos{};Write32(qos.data(),0x50445141);Write32(qos.data()+4,1);Write32(qos.data()+8,40);qos[12]=id==4?0x11:0x87;
          sendto(socket,reinterpret_cast<const char*>(qos.data()),int(qos.size()),0,reinterpret_cast<sockaddr*>(&peer),size);continue;
        }
        std::array<uint8_t,136> packet{};Write32(packet.data(),0x50444D41);Write32(packet.data()+4,1);packet[8]=id;std::memcpy(packet.data()+16,&ina,4);packet[92]=1;packet[93]=6;
        sendto(socket,reinterpret_cast<const char*>(packet.data()),20,0,reinterpret_cast<sockaddr*>(&peer),size);
        auto wrong=packet;wrong[16]=26;sendto(socket,reinterpret_cast<const char*>(wrong.data()),int(wrong.size()),0,reinterpret_cast<sockaddr*>(&peer),size);
        for(int i=0;i<2;++i)sendto(socket,reinterpret_cast<const char*>(packet.data()),int(packet.size()),0,reinterpret_cast<sockaddr*>(&peer),size);
      }
    });
  }
  ~Host(){stop=true;thread.join();closesocket(socket);}
};
int main() {
  WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return 1;
  try {
    {
    Host alpha("127.0.0.1",1),bravo("127.0.0.3",2);
    std::array<DiscoveryRoute,2> routes{{{Address("127.0.0.2"),Address("127.0.0.1"),false},{Address("127.0.0.2"),Address("127.0.0.3"),true}}};
    auto matches=DiscoverRoutes(routes,6,300);Check(matches.size()==2,"Multiple hosts/deduplication failed");
    Check(matches[0].address!=matches[1].address,"Replies collapsed wrong host");
    std::array<uint8_t,200> guest{};guest.fill(0xCC);Check(SerializeMatches(guest,0x1000,matches),"Multi serialization failed");Check(Read32(guest.data())==2&&Read32(guest.data()+4)==0x1008&&guest[192]==0xCC,"Guest output layout/bounds wrong");
    auto untouched=guest;Check(!SerializeMatches(std::span(guest.data(),191),0x1000,matches)&&guest==untouched,"Short result buffer modified");
    auto one=DiscoverRoutes(routes,1,200);Check(one.size()==1,"Quick Match limit ignored");
    auto self=routes;self[0].source=Address("127.0.0.1");Check(DiscoverRoutes(std::span(self.data(),1),1,100).empty(),"Own host returned");
    std::array<DiscoveryRoute,1> absent{{{Address("127.0.0.2"),Address("127.0.0.4"),false}}};auto start=std::chrono::steady_clock::now();Check(DiscoverRoutes(absent,6,100).empty(),"Timeout invented lobby");Check(std::chrono::steady_clock::now()-start<std::chrono::seconds(1),"Unbounded discovery wait");
    bool rejected=false;try{BuildDiscoveryRoutes("","0.0.0.0");}catch(...){rejected=true;}Check(rejected,"Invalid manual target accepted");
    }
    {
      Host broadcastHost("0.0.0.0",3);
      std::array<DiscoveryRoute,1> automatic{{{Address("127.0.0.2"),Address("127.255.255.255"),true}}};
      auto discovered=DiscoverRoutes(automatic,6,400);Check(discovered.size()==1&&discovered[0].address=="127.0.0.1","Actual directed broadcast did not discover host");
    }
    {
      Host foreign("127.0.0.4",4),unready("127.0.0.5",5);
      std::array<DiscoveryRoute,2> other{{{Address("127.0.0.2"),Address("127.0.0.4"),false},{Address("127.0.0.2"),Address("127.0.0.5"),false}}};
      Check(DiscoverRoutes(other,6,200).empty(),"Other game or missing OutRun QoS offered as lobby");
    }
    std::cout<<"PASS: real UDP queries, multiple hosts, duplicates/malformed replies, self exclusion, guest bounds and bounded no-lobby timeout\n";
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';WSACleanup();return 1;}
  WSACleanup();return 0;
}
