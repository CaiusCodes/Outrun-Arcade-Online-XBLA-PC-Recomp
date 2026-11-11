#include "audio_trace_budget.h"
#include <iostream>
int main(){
 outrun::diagnostics::TraceBudget b;
 unsigned gains=0,events=0;
 // Stress repeated gain updates throughout a44s run. Late lifecycle markers
 // must survive the flood that exhausted the previous shared budget at17s.
 for(uint64_t ms=0;ms<44000;++ms){
  for(unsigned layer=0;layer<5;++layer)gains+=b.TireGain(layer,123,7,ms);
  if(ms%100==0){if(!b.Event())return 1;++events;}
 }
 if(gains!=4400 || events!=440 || !b.Event())return 2;
 if(!b.TireGain(0,123,8,44001) || b.TireGain(16,123,8,44001))return 3;
 for(uint64_t ms=45000;ms<200000;++ms)for(unsigned l=0;l<5;++l)b.TireGain(l,123,8,ms);
 if(!b.Event())return 4;
 std::cout<<"PASS:44s gain flood retains late events; independent bounded budgets and new-voice samples\n";
}
