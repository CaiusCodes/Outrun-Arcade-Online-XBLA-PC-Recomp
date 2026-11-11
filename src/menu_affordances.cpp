#include "menu_affordances.h"
#include "menu_pointer.h"
#include "menu_footer.h"
#include <rex/ui/imgui_dialog.h>
#include <imgui.h>
#include <algorithm>
#include <memory>
#include <windows.h>

namespace outrun { bool NativeRendererActive(); }  // native_gpu.cpp
namespace outrun::menu_affordances {
namespace {
class Footer final : public rex::ui::ImGuiDialog {
 public:
  explicit Footer(rex::ui::ImGuiDrawer* drawer):ImGuiDialog(drawer){}
  bool WantsContinuousRedraw() const override {return false;}
 private:
  void OnDraw(ImGuiIO& io) override {
    // The footer prompt under the mouse (Back, Select, Color, Cancel,
    // Accept...) gets the hand cursor, as they are clickable, and the menus'
    // highlight when kFooterHighlight is on. Not while an overlay holds input.
    if(pointer::inputHeld)return;
    const auto items=pointer::Footer();
    if(items.empty())return;
    const float gameW=pointer::fourByThree?960.f:1280.f;
    const float scale=std::min(io.DisplaySize.x/gameW,io.DisplaySize.y/720.f);
    if(scale<=0)return;
    // 4:3 shows the middle 960 columns of the 1280-wide picture.
    const float shift=pointer::fourByThree?160.f:0.f;
    const ImVec2 origin((io.DisplaySize.x-gameW*scale)*.5f,(io.DisplaySize.y-720*scale)*.5f);
    auto point=[&](float x,float y){return ImVec2(origin.x+(x-shift)*scale,origin.y+y*scale);};
    float mx=(io.MousePos.x-origin.x)/scale+shift,my=(io.MousePos.y-origin.y)/scale;
    if(const auto test=pointer::testCursor.load();test.x>=0){mx=test.x;my=test.y;}
    for(const auto& item:items) {
      if(!item.Contains(mx,my))continue;
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      if(!pointer::kFooterHighlight)break;
      const float l=item.left-6,t=item.top-2,r=item.right+6,b=item.bottom+2;
      if(NativeRendererActive()) {
        // Plume fills it in its output pass, behind the game's lettering.
        pointer::highlight=pointer::Highlight{l,t,r,b,GetTickCount64()+120};
      } else {
        auto* draw=ImGui::GetForegroundDrawList();
        const ImVec2 a=point(l,t),c=point(r,b);
        const float round=(c.y-a.y)*.5f;
        draw->AddRectFilled(a,c,IM_COL32(255,196,0,72),round);
        draw->AddRect(a,c,IM_COL32(255,214,40,240),round,0,std::max(1.5f,2.5f*scale));
      }
      break;
    }
  }
};
std::unique_ptr<Footer> footer;
}
void Initialize(rex::ui::ImGuiDrawer* drawer){if(drawer)footer=std::make_unique<Footer>(drawer);}
void Shutdown(){footer.reset();}
}
