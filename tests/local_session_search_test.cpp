#include "local_session_search.h"
#include <iostream>
#include <stdexcept>
using namespace outrun::local_sessions;
void Check(bool good,const char* message) { if (!good) throw std::runtime_error(message); }
int main() {
  std::array<uint8_t,136> packet{};
  Write32(packet.data(),0x50444D41); Write32(packet.data()+4,1);
  packet[8]=0xAE; packet[16]=127; packet[19]=1; packet[92]=1; packet[93]=6;
  const std::array<uint8_t,4> address{127,0,0,1};
  const auto match=Parse(packet,address);
  Check(bool(match),"Valid advertisement rejected");
  for (const auto& target : {std::array<uint8_t,4>{26,12,34,56},
                             std::array<uint8_t,4>{192,168,8,2},
                             std::array<uint8_t,4>{100,64,0,2}}) {
    auto lan=packet; std::copy(target.begin(),target.end(),lan.begin()+16);
    Check(bool(Parse(lan,target)),"LAN/VPN advertisement rejected");
    auto source=target; source[3]--;
    Check(CompatibleAddresses(source,target),"LAN/VPN peer rejected");
    Check(!CompatibleAddresses(target,target),"Self target accepted");
    Check(!CompatibleAddresses(address,target),"Mixed loopback/LAN accepted");
    Check(!Parse(lan,address),"Different advertised host accepted");
  }
  for (const auto& invalid : {std::array<uint8_t,4>{0,0,0,0},
                              std::array<uint8_t,4>{224,0,0,1},
                              std::array<uint8_t,4>{255,255,255,255}})
    Check(!IsUnicastAddress(invalid),"Non-unicast target accepted");
  for (size_t n=0;n<packet.size();++n) Check(!Parse(std::span(packet.data(),n),address),"Truncated packet accepted");
  for (size_t offset : {size_t(0),size_t(4),size_t(16),size_t(19)}) {
    auto bad=packet; bad[offset]^=1; Check(!Parse(bad,address),"Bad identity accepted");
  }
  auto bad=packet; bad[8]=0; Check(!Parse(bad,address),"Zero session accepted");
  bad=packet; bad[92]=6; Check(!Parse(bad,address),"Full lobby accepted");
  bad=packet; bad[94]=1; Check(!Parse(bad,address),"Running race accepted");
  std::array<uint8_t,104> output; output.fill(0xCC);
  Check(Serialize(output,0x40597000,match),"Serialize failed");
  Check(Read32(output.data())==1 && Read32(output.data()+4)==0x40597008,"Bad guest header");
  Check(std::equal(match->info.begin(),match->info.end(),output.begin()+8),"Session bytes changed");
  Check(Read32(output.data()+68)==5 && Read32(output.data()+76)==1,"Bad slots");
  Check(Read32(output.data()+88)==0 && Read32(output.data()+96)==0,"Invented contexts");
  Check(output[100]==0xCC,"Overran result");
  const auto before=output;
  Check(!Serialize(std::span(output.data(),99),0x40597000,match) && output==before,"Short buffer modified");
  Check(!Serialize(output,0xFFFFFFF0,match) && output==before,"Address overflow modified buffer");
  Check(Serialize(output,0x40597000,{}) && Read32(output.data())==0 && Read32(output.data()+4)==0,"Empty search not cleared");
  std::cout<<"Local discovery parsing and guest result bounds passed\n";
  auto aliases=packet;aliases[32]='P';aliases[33]='D';aliases[34]=2;
  std::copy(address.begin(),address.end(),aliases.begin()+36);
  const std::array<uint8_t,4> alternate{26,1,2,3};std::copy(alternate.begin(),alternate.end(),aliases.begin()+40);
  auto routed=ParseDiscovered(aliases,alternate);Check(bool(routed),"Advertised alternate reply rejected");
  Check(std::equal(alternate.begin(),alternate.end(),routed->info.begin()+8)&&routed->info[26]==1&&std::equal(alternate.begin(),alternate.end(),routed->info.begin()+28),"Reply route not pinned in both primary/list addresses");
  auto unknown=alternate;unknown[3]++;Check(!ParseDiscovered(aliases,unknown),"Unadvertised sender accepted");
  aliases[34]=5;Check(!ParseDiscovered(aliases,alternate),"Oversize address list accepted");
}
