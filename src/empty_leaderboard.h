#pragma once
#include <cstdint>
#include <span>
#include <algorithm>
namespace outrun::leaderboards {
// XUSER_STATS_READ_RESULTS + one XUSER_STATS_VIEW with zero rows. OutRun
// reads pViews[0] before looking at the row count, even on a new profile.
inline bool WriteEmptyView(std::span<uint8_t> output, uint32_t guest, uint32_t view) {
  if(output.size()<24 || !guest || uint64_t(guest)+24>0x100000000ull) return false;
  std::fill_n(output.begin(),24,0);
  auto put=[&](size_t p,uint32_t v) {
    for(int i=3;i>=0;--i){output[p+i]=uint8_t(v);v>>=8;}
  };
  put(0,1); put(4,guest+8); put(8,view);
  return true;
}
}
