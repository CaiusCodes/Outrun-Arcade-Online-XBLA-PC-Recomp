#include "audio_engine_attack.h"
#include <iostream>
int main(){
  outrun::audio::EngineAttack a;
  double l=-2000,r=-3000;
  if(a.Apply(1,4,100,l,r) || l!=-2000 || r!=-3000)return 1;
  a.Start(0,3,1,4,100);
  double last=0;
  for(unsigned ms=0;ms<80;++ms){
    l=-2000;r=-3000;
    if(!a.Apply(1,4,100+ms,l,r) || l>-2000 || r>-3000)return 2;
    const double gain=std::pow(10.0,(l+2000)/2000);
    if(gain<last)return 3;last=gain;
  }
  if(!a.NeedsUpdate(4))return 10;
  l=-2000;r=-3000;if(a.Apply(1,4,180,l,r) || l!=-2000 || r!=-3000 || a.NeedsUpdate(4))return 4;
  if(a.Apply(2,4,120,l,r) || a.Apply(1,4,99,l,r))return 5;
  a.Start(1,3,1,4,120);if(a.Apply(1,4,121,l,r))return 6;
  a.Start(0,3,1,4,120);a.Stop(4);if(a.Apply(1,4,121,l,r))return 7;
  for(unsigned layer: {0u,1u,16u}){a.Start(0,layer,1,4,120);if(a.Apply(1,4,121,l,r))return 8;}
  a.Start(0,3,1,-1,120);a.Start(0,3,1,315,120);a.Stop(-1);a.Stop(315);
  if(a.Apply(1,-1,121,l,r) || a.Apply(1,315,121,l,r))return 9;
  std::cout<<"PASS: bounded monotonic startup, exact full volume at80ms, stop/reuse/object/group/base/bounds preservation\n";
}
