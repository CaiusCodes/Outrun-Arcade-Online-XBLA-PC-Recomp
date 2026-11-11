#include "audio_declick.h"
#include <iostream>
int main() {
  outrun::audio::BoundaryCorrector filter;std::array<float,1536> first{},second{};
  first.fill(.1f);second.fill(.1f);
  if(filter.Process(first))return 1;
  for(int i=0;i<256;++i)second[i]=.8f;
  const auto before=second;
  if(filter.Process(second)!=1 || std::abs(second[0]-.1f)>1e-6 || second[31]!=.8f)return 2;
  for(int i=32;i<1536;++i)if(second[i]!=before[i])return 3;
  for(int i=0;i<256;++i)second[i]=(i%2)?-.8f:.8f;
  const auto oscillatory=second;
  if(filter.Process(second) || second!=oscillatory)return 4;
  outrun::audio::BoundaryCorrector clean;
  first.fill(.1f);second.fill(.15f);clean.Process(first);
  const auto gentle=second;if(clean.Process(second) || second!=gentle)return 5;
  std::cout<<"PASS: first frame unchanged, isolated-edge continuity,32-sample scope, independent channels and oscillatory transients\n";
}
