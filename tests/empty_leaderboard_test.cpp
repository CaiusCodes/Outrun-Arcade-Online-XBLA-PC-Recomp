#include "empty_leaderboard.h"
#include <array>
#include <iostream>
#include <stdexcept>
void check(bool value){if(!value)throw std::runtime_error("Empty board regression");}
int main(){
  std::array<uint8_t,32> buffer;buffer.fill(0xCD);
  auto read=[&](size_t p){return uint32_t(buffer[p])<<24|uint32_t(buffer[p+1])<<16|
      uint32_t(buffer[p+2])<<8|buffer[p+3];};
  check(outrun::leaderboards::WriteEmptyView(buffer,0x10000000,33));
  check(read(0)==1 && read(4)==0x10000008);
  // Follow OutRun's original consumer: pViews[0].dwNumRows and total rows.
  const auto view=read(4)-0x10000000;
  check(read(view)==33 && read(view+4)==0 && read(view+8)==0 && read(view+12)==0);
  check(read(24)==0xCDCDCDCD && read(28)==0xCDCDCDCD);
  const auto saved=buffer;
  check(!outrun::leaderboards::WriteEmptyView(std::span(buffer).first(23),0x10000000,33));
  check(!outrun::leaderboards::WriteEmptyView(buffer,0xFFFFFFF0,33));
  check(!outrun::leaderboards::WriteEmptyView(buffer,0,33));
  check(saved==buffer);
  std::cout<<"Empty board pointer chain, zero rows, buffer boundaries and rejected writes passed\n";
}
