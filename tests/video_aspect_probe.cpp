#include <rex/cvar.h>
#include <rex/system/xvideo.h>
#include <iostream>
namespace rex::kernel::xboxkrnl {void VdQueryVideoMode(rex::system::X_VIDEO_MODE*);}
int main() {
  rex::cvar::SetFlagByName("video_mode_refresh_rate","60");
  for(const auto width:{640,1024,1280,1920}) {
    const auto height=width==640?480:width==1024?768:width==1280?720:1080;
    rex::cvar::SetFlagByName("video_mode_width",std::to_string(width));
    rex::cvar::SetFlagByName("video_mode_height",std::to_string(height));
    rex::system::X_VIDEO_MODE mode{};
    rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
    std::cout<<uint32_t(mode.display_width)<<'x'<<uint32_t(mode.display_height)
      <<" widescreen="<<uint32_t(mode.is_widescreen)<<" refresh="<<float(mode.refresh_rate)<<'\n';
    if(uint32_t(mode.display_width)!=width || uint32_t(mode.display_height)!=height ||
       bool(uint32_t(mode.is_widescreen))!=(width>=1280) || float(mode.refresh_rate)!=60)return 1;
  }
}
