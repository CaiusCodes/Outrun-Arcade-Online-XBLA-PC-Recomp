#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "lan_discovery.h"
#include <chrono>
#include <cctype>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace outrun::local_sessions {
namespace {
struct Winsock {Winsock(){WSADATA d{};if(WSAStartup(MAKEWORD(2,2),&d))throw std::runtime_error("LAN sockets unavailable");}~Winsock(){WSACleanup();}};
bool ParseIPv4(std::string_view text,uint32_t& value){std::string copy(text);return inet_pton(AF_INET,copy.c_str(),&value)==1;}
std::string Format(uint32_t value){char text[INET_ADDRSTRLEN]{};inet_ntop(AF_INET,&value,text,sizeof(text));return text;}
bool RealAddress(uint32_t value){uint32_t h=ntohl(value);return h>>24!=127&&h>>16!=0xA9FE&&h>>24!=0&&h>>24<224;}
std::string Narrow(const wchar_t* value){if(!value)return {};int n=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);if(n<=1)return {};std::string text(size_t(n),0);WideCharToMultiByte(CP_UTF8,0,value,-1,text.data(),n,nullptr,nullptr);text.pop_back();return text;}
bool HostOnly(std::string label){std::transform(label.begin(),label.end(),label.begin(),[](unsigned char c){return char(std::tolower(c));});for(auto n:{"virtualbox","vmware","hyper-v","wsl","host-only"})if(label.find(n)!=std::string::npos)return true;return false;}
struct Socket {SOCKET value=INVALID_SOCKET;Socket()=default;Socket(const Socket&)=delete;~Socket(){if(value!=INVALID_SOCKET)closesocket(value);}};
}
std::vector<NetworkInterface> LocalInterfaces() {
  ULONG size=16384;std::vector<uint8_t> buffer(size);ULONG result;
  for(int i=0;;++i){result=GetAdaptersAddresses(AF_INET,GAA_FLAG_SKIP_ANYCAST|GAA_FLAG_SKIP_MULTICAST|GAA_FLAG_SKIP_DNS_SERVER,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);if(result!=ERROR_BUFFER_OVERFLOW||i==2)break;buffer.resize(size);}
  if(result!=NO_ERROR)return {};std::vector<NetworkInterface> interfaces;
  for(auto* a=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());a;a=a->Next) {
    auto label=Narrow(a->FriendlyName);if(a->OperStatus!=IfOperStatusUp||a->IfType==IF_TYPE_SOFTWARE_LOOPBACK||HostOnly(label+" "+Narrow(a->Description)))continue;
    for(auto* u=a->FirstUnicastAddress;u;u=u->Next) {
      if(u->Address.lpSockaddr->sa_family!=AF_INET||u->OnLinkPrefixLength==0||u->OnLinkPrefixLength>32)continue;
      uint32_t address=reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr)->sin_addr.s_addr;if(!RealAddress(address))continue;
      uint32_t mask=0xFFFFFFFFu<<(32-u->OnLinkPrefixLength),broadcast=u->OnLinkPrefixLength>=31?0:htonl(ntohl(address)|~mask);
      if(std::none_of(interfaces.begin(),interfaces.end(),[&](const auto& n){return n.address==address;}))interfaces.push_back({address,broadcast,label});
    }
  }
  return interfaces;
}
std::vector<DiscoveryRoute> BuildDiscoveryRoutes(std::string_view sourceOverride,std::string_view hostOverride) {
  Winsock winsock;auto interfaces=LocalInterfaces();uint32_t source=0,target=0;
  if(!sourceOverride.empty()&&!ParseIPv4(sourceOverride,source))throw std::invalid_argument("Invalid local LAN address");
  if(!hostOverride.empty()&&!ParseIPv4(hostOverride,target))throw std::invalid_argument("Invalid host IPv4 address");
  if(!hostOverride.empty()) {auto bytes=reinterpret_cast<const uint8_t*>(&target);if(!IsUnicastAddress(std::span<const uint8_t,4>(bytes,4)))throw std::invalid_argument("Host must be a unicast IPv4 address");}
  if(!sourceOverride.empty()&&!source)throw std::invalid_argument("Invalid local LAN address");
  std::vector<DiscoveryRoute> routes;
  if(source) {
    if(target){if(source!=target)routes.push_back({source,target,false});return routes;}
    auto it=std::find_if(interfaces.begin(),interfaces.end(),[&](const auto& n){return n.address==source;});
    if(it!=interfaces.end()&&it->broadcast)routes.push_back({source,it->broadcast,true});
    else if(ntohl(source)>>24==127)routes.push_back({source,htonl(0x7FFFFFFF),true});
    return routes;
  }
  // Empty net_address keeps the runtime's automatic identity and all-interface
  // listener. Send separately through each active LAN/virtual-LAN interface.
  for(const auto& n:interfaces)if((target||n.broadcast)&&(!target||target!=n.address))routes.push_back({n.address,target?target:n.broadcast,!target});
  return routes;
}
std::vector<Match> DiscoverRoutes(std::span<const DiscoveryRoute> routes,size_t maximum,uint32_t timeoutMs) {
  if(!maximum||routes.empty())return {};Winsock winsock;maximum=std::min(maximum,size_t(16));timeoutMs=std::clamp(timeoutMs,1u,1500u);
  struct Endpoint{std::unique_ptr<Socket> socket;DiscoveryRoute route;};std::vector<Endpoint> endpoints;
  for(const auto& route:routes) {
    auto s=std::make_unique<Socket>();s->value=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s->value==INVALID_SOCKET)continue;
    BOOL yes=TRUE;if(route.broadcast&&setsockopt(s->value,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<const char*>(&yes),sizeof(yes)))continue;
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=route.source;if(bind(s->value,reinterpret_cast<sockaddr*>(&local),sizeof(local)))continue;
    u_long nonblocking=1;if(ioctlsocket(s->value,FIONBIO,&nonblocking))continue;endpoints.push_back({std::move(s),route});if(endpoints.size()==16)break;
  }
  if(endpoints.empty())return {};
  constexpr uint8_t query[]{0x50,0x44,0x4D,0x51,0,0,0,1};std::vector<Match> matches;
  constexpr uint8_t qosQuery[]{0x50,0x44,0x51,0x51,0,0,0,1};
  struct Pending {Match match;uint32_t sender;SOCKET socket;};std::vector<Pending> pending;
  auto start=std::chrono::steady_clock::now(),deadline=start+std::chrono::milliseconds(timeoutMs),nextSend=start;int sends=0;
  while(std::chrono::steady_clock::now()<deadline) {
    if(std::chrono::steady_clock::now()>=nextSend&&sends<3) {
      for(auto& e:endpoints){sockaddr_in target{};target.sin_family=AF_INET;target.sin_port=htons(3076);target.sin_addr.s_addr=e.route.destination;sendto(e.socket->value,reinterpret_cast<const char*>(query),sizeof(query),0,reinterpret_cast<sockaddr*>(&target),sizeof(target));}
      ++sends;nextSend=start+std::chrono::milliseconds(sends*250);
    }
    fd_set ready;FD_ZERO(&ready);for(auto& e:endpoints)FD_SET(e.socket->value,&ready);timeval timeout{0,50000};if(select(0,&ready,nullptr,nullptr,&timeout)<=0)continue;
    for(auto& e:endpoints)if(FD_ISSET(e.socket->value,&ready))for(int i=0;i<32;++i) {
      std::array<uint8_t,256> packet{};sockaddr_in sender{};int size=sizeof(sender);int n=recvfrom(e.socket->value,reinterpret_cast<char*>(packet.data()),int(packet.size()),0,reinterpret_cast<sockaddr*>(&sender),&size);
      if(n<=0)break;if(sender.sin_family!=AF_INET||sender.sin_port!=htons(3076))continue;
      if(!e.route.broadcast&&sender.sin_addr.s_addr!=e.route.destination)continue;
      if(std::any_of(routes.begin(),routes.end(),[&](const auto& r){return sender.sin_addr.s_addr==r.source;}))continue;
      if(n==52&&Read32(packet.data())==0x50445141&&Read32(packet.data()+4)==1&&Read32(packet.data()+8)==40&&packet[12]==0x87) {
        // Original sub_8210F8xx requires a 40-byte OutRun QoS record beginning
        // with 0x87. Do not offer unrelated ports sharing the SDK discovery port.
        auto candidate=std::find_if(pending.begin(),pending.end(),[&](const auto& p){return p.sender==sender.sin_addr.s_addr&&p.socket==e.socket->value;});
        if(candidate!=pending.end()&&std::none_of(matches.begin(),matches.end(),[&](const auto& m){return std::equal(m.info.begin(),m.info.begin()+8,candidate->match.info.begin());})) {
          matches.push_back(candidate->match);if(matches.size()==maximum)return matches;
        }
        continue;
      }
      auto* bytes=reinterpret_cast<const uint8_t*>(&sender.sin_addr.s_addr);auto match=ParseDiscovered(std::span(packet.data(),size_t(n)),std::span<const uint8_t,4>(bytes,4));if(!match)continue;
      if(std::any_of(matches.begin(),matches.end(),[&](const auto& m){return std::equal(m.info.begin(),m.info.begin()+8,match->info.begin());}))continue;
      match->address=Format(sender.sin_addr.s_addr);
      if(std::none_of(pending.begin(),pending.end(),[&](const auto& p){return p.sender==sender.sin_addr.s_addr&&p.socket==e.socket->value;})) {
        if(pending.size()>=64)continue;pending.push_back({*match,sender.sin_addr.s_addr,e.socket->value});
      }
      sendto(e.socket->value,reinterpret_cast<const char*>(qosQuery),sizeof(qosQuery),0,reinterpret_cast<sockaddr*>(&sender),sizeof(sender));
    }
  }
  return matches;
}
std::vector<Match> DiscoverLobbies(std::string_view sourceOverride,std::string_view hostOverride,size_t maximum){return DiscoverRoutes(BuildDiscoveryRoutes(sourceOverride,hostOverride),maximum);}
}
