#pragma once
#include <algorithm>
#include <optional>
namespace outrun::pointer {
struct Point {float x,y;};
inline std::optional<Point> ToCanvas(float x,float y,float width,float height,bool fourByThree=false) {
  if(width<=0 || height<=0)return {};
  const auto scale=std::min(width/(fourByThree?960.f:1280.f),height/720.f);
  Point point{(x-(width-1280.f*scale)/2)/scale,(y-(height-720.f*scale)/2)/scale};
  if(point.x<(fourByThree?160:0) || point.y<0 || point.x>=(fourByThree?1120:1280) || point.y>=720)return {};
  return point;
}
}
