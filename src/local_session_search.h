#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <string>

namespace outrun::local_sessions {
inline uint32_t Read32(const uint8_t* p) {
  return uint32_t(p[0])<<24 | uint32_t(p[1])<<16 | uint32_t(p[2])<<8 | p[3];
}
inline void Write32(uint8_t* p, uint32_t n) {
  p[0]=uint8_t(n>>24); p[1]=uint8_t(n>>16); p[2]=uint8_t(n>>8); p[3]=uint8_t(n);
}
struct Match {
  std::array<uint8_t,60> info{};
  uint32_t players = 0, slots = 0;
  std::string address;
};
// Explicit IPv4 unicast targets include ordinary LAN, Radmin (26/8), and
// other virtual LAN addresses. Automatic search uses interface broadcasts.
inline bool IsUnicastAddress(std::span<const uint8_t,4> address) {
  return address[0] != 0 && address[0] < 224;
}
inline bool CompatibleAddresses(std::span<const uint8_t,4> source,
                                std::span<const uint8_t,4> target) {
  return IsUnicastAddress(source) && IsUnicastAddress(target) &&
      (source[0]==127)==(target[0]==127) &&
      !std::equal(source.begin(),source.end(),target.begin());
}
// SDK xam_netplay.cpp v1 extended advertisement; a discovery reply is not
// proof of a joined game. Always require the explicitly requested host address.
inline std::optional<Match> Parse(std::span<const uint8_t> packet,
                                 std::span<const uint8_t,4> expectedAddress) {
  if (packet.size()!=136 || Read32(packet.data())!=0x50444D41 ||
      Read32(packet.data()+4)!=1 || !IsUnicastAddress(expectedAddress) ||
      !std::equal(expectedAddress.begin(),expectedAddress.end(),packet.begin()+16) ||
      packet[92]==0 || packet[93]>6 || packet[92]>=packet[93] || (packet[94]&1))
    return {};
  if (std::all_of(packet.begin()+8,packet.begin()+16,[](uint8_t b){return b==0;})) return {};
  Match match;
  std::copy_n(packet.begin()+8,60,match.info.begin());
  match.players=packet[92]; match.slots=packet[93];
  return match;
}
// A reply can use an alternate address advertised in the host's XNADDR.
inline std::optional<Match> ParseDiscovered(std::span<const uint8_t> packet,
                                         std::span<const uint8_t,4> sender) {
  if(packet.size()!=136 || !IsUnicastAddress(sender))return {};
  bool advertised=std::equal(sender.begin(),sender.end(),packet.begin()+16);
  if(packet[32]=='P' && packet[33]=='D' && packet[34]>0 && packet[34]<=4) {
    for(unsigned i=0;i<packet[34];++i)
      advertised |= std::equal(sender.begin(),sender.end(),packet.begin()+36+i*4);
  }
  if(!advertised)return {};
  std::array<uint8_t,136> routed;std::copy(packet.begin(),packet.end(),routed.begin());
  std::copy(sender.begin(),sender.end(),routed.begin()+16);
  std::copy(sender.begin(),sender.end(),routed.begin()+20);
  // Pin the validated reply route in abOnline as well: the runtime's Candidates
  // function prefers that list over the primary address.
  routed[32]='P';routed[33]='D';routed[34]=1;routed[35]=0;
  std::fill(routed.begin()+36,routed.begin()+52,0);
  std::copy(sender.begin(),sender.end(),routed.begin()+36);
  return Parse(routed,sender);
}
inline bool SerializeMatches(std::span<uint8_t> output,uint32_t guestAddress,
                             std::span<const Match> matches) {
  const uint64_t needed=8ull+92ull*matches.size();
  if(matches.size()>16)return false;
  if(output.size()<needed || !guestAddress || uint64_t(guestAddress)+needed>0x100000000ull)return false;
  std::fill_n(output.begin(),size_t(needed),0);Write32(output.data(),uint32_t(matches.size()));
  if(matches.empty())return true;
  Write32(output.data()+4,guestAddress+8);
  for(size_t i=0;i<matches.size();++i) {
    auto* row=output.data()+8+92*i;std::copy(matches[i].info.begin(),matches[i].info.end(),row);
    Write32(row+60,matches[i].slots-matches[i].players);Write32(row+68,matches[i].players);
  }
  return true;
}
// Xbox 360 XSESSION_SEARCHRESULT_HEADER (8) + XSESSION_SEARCHRESULT (92).
// The packet doesn't advertise title contexts/properties, so none are invented.
inline bool Serialize(std::span<uint8_t> output, uint32_t guestAddress,
                      const std::optional<Match>& match) {
  const size_t needed=match ? 100 : 8;
  if (output.size()<needed || !guestAddress || uint64_t(guestAddress)+needed>0x100000000ull)
    return false;
  std::fill_n(output.begin(),needed,0);
  if (!match) return true;
  Write32(output.data(),1);
  Write32(output.data()+4,guestAddress+8);
  std::copy(match->info.begin(),match->info.end(),output.begin()+8);
  Write32(output.data()+8+60,match->slots-match->players);
  Write32(output.data()+8+68,match->players);
  return true;
}
}
