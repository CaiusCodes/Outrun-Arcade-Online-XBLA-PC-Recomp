#pragma once
#include <array>
#include <algorithm>
#include <cmath>
namespace outrun::audio {
// Experimental correction of isolated block-edge steps, confined to the first
//32 samples (0.67ms) of a qualifying front stereo channel.
class BoundaryCorrector {
 public:
  unsigned Process(std::array<float,1536>& samples) {
    unsigned corrected=0;
    for(unsigned c=0;c<2;++c) {
      float* x=samples.data()+c*256;
      if(!std::all_of(x,x+256,[](float v){return std::isfinite(v);})) {valid_[c]=false;continue;}
      float nextSlope=0;for(int i=1;i<9;++i)nextSlope+=std::abs(x[i]-x[i-1]);nextSlope/=8;
      const float delta=last_[c]-x[0];
      if(valid_[c] && std::abs(delta)>.25f && std::abs(delta)>8*std::max(slope_[c],nextSlope)) {
        for(int i=0;i<32;++i)x[i]+=delta*(1-float(i)/31);
        ++corrected;
      }
      last_[c]=x[255];slope_[c]=0;for(int i=248;i<256;++i)slope_[c]+=std::abs(x[i]-x[i-1]);slope_[c]/=8;valid_[c]=true;
    }
    return corrected;
  }
 private:
  std::array<bool,2> valid_{};
  std::array<float,2> last_{},slope_{};
};
}
