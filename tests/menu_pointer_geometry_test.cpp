#include "menu_pointer_geometry.h"
#include <cmath>
#include <iostream>
int main(){
 using outrun::pointer::ToCanvas;
 for(const auto scale:{1.f,1.5f,2.f,3.f}) {
  auto p=ToCanvas(550*scale,535*scale,1280*scale,720*scale);
  if(!p || std::abs(p->x-550)>0.01 || std::abs(p->y-535)>0.01)return 1;
 }
 auto wide=ToCanvas(1540,1070,3440,1440);
 if(!wide || std::abs(wide->x-550)>0.01 || std::abs(wide->y-535)>0.01)return 2;
 if(ToCanvas(100,700,3440,1440) || ToCanvas(200,80,1280,1000) || ToCanvas(0,0,0,720))return 3;
 for(const auto scale:{1.f,1.5f,2.f,3.f}) {
   auto p=ToCanvas(416*scale,(535*768.f/720)*scale,1024*scale,768*scale,true);
   if(!p || std::abs(p->x-550)>.01 || std::abs(p->y-535)>.01)return 4;
 }
 auto p=ToCanvas(825,802.5f,1920,1080,true);
 if(!p || std::abs(p->x-550)>.01 || std::abs(p->y-535)>.01)return 5;
 if(ToCanvas(100,500,1920,1080,true) || ToCanvas(1800,500,1920,1080,true))return 6;
 std::cout<<"PASS: 720p/1080p/1440p/4K coordinates and letterbox rejection\n";
}
