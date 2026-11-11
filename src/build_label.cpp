#include "build_label.h"
#include "menu_pointer.h"
#include "ui_font.h"
#include "outrun_build_label.h"
#include <rex/ui/imgui_dialog.h>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

#if OUTRUN_HAS_NATIVE
namespace outrun { bool NativeRendererActive(); double NativeFps(); }  // native_gpu.cpp
#endif
#include <rex/cvar.h>

// "0.9.0" shows as "0.9"; a patch release keeps its third number.
#ifndef OUTRUN_VERSION_TEXT
#define OUTRUN_VERSION_TEXT "0.0.0"
#endif
// The developer build label (version, build number, commit, renderer) at the
// title screen's bottom right: off in releases ("Version 0.9" bottom left stays;
// the player log's header has the full label).
REXCVAR_DEFINE_BOOL(outrun_show_build_label, false, "OutRun/Diagnostics",
                    "Show the developer build label on the title screen");
namespace outrun::build_label {
namespace {
class Label final : public rex::ui::ImGuiDialog {
 public:
  explicit Label(rex::ui::ImGuiDrawer* drawer):ImGuiDialog(drawer){}
  bool WantsContinuousRedraw() const override {return false;}
 private:
  void OnDraw(ImGuiIO& io) override {
    if(!pointer::titleActive.load())return;
    // The game picture's corner (letterboxed like menu_affordances), in whole
    // multiples of the overlay's pixel font so it stays crisp.
    const float gameW=pointer::fourByThree?960.f:1280.f;
    const float scale=std::min(io.DisplaySize.x/gameW,io.DisplaySize.y/720.f);
    if(scale<=0)return;
    const ImVec2 origin((io.DisplaySize.x-gameW*scale)*.5f,(io.DisplaySize.y-720*scale)*.5f);
    auto* draw=ImGui::GetForegroundDrawList();
    ImFont* font=ImGui::GetFont();
    if(REXCVAR_GET(outrun_show_build_label)) {
    if(text.empty()) {
      text=kOutrunBuildLabel;
#if OUTRUN_HAS_NATIVE
      text+=NativeRendererActive()?" - Plume":" - Classic";
#else
      text+=" - Classic";
#endif
    }
    const float size=ImGui::GetFontSize()*std::max(1.f,std::round(scale));
    const ImVec2 extent=font->CalcTextSizeA(size,FLT_MAX,0,text.c_str());
    const ImVec2 at(std::floor(origin.x+gameW*scale-extent.x-8*scale),std::floor(origin.y+720*scale-extent.y-6*scale));
    draw->AddText(font,size,ImVec2(at.x+1,at.y+1),IM_COL32(0,0,0,140),text.c_str());
    draw->AddText(font,size,at,IM_COL32(255,255,255,150),text.c_str());
    }
    // The release version, bottom left in the game's lettering (heavy italic),
    // level with the build label.
    static const std::string version=[]{
      std::string v=OUTRUN_VERSION_TEXT;
      if(v.size()>2 && v.compare(v.size()-2,2,".0")==0)v.resize(v.size()-2);
      return "Version "+v;
    }();
    // Faint and small (user's choice): soft white over a light shadow.
    ImFont* face=ui_font::Display()?ui_font::Display():font;
    const float vs=19*scale;
    const ImVec2 vext=face->CalcTextSizeA(vs,FLT_MAX,0,version.c_str());
    const ImVec2 vat(std::floor(origin.x+40*scale),std::floor(origin.y+(720-26)*scale-vext.y/2));
    draw->AddText(face,vs,ImVec2(vat.x+1.5f*scale,vat.y+1.5f*scale),IM_COL32(0,10,40,70),version.c_str());
    draw->AddText(face,vs,vat,IM_COL32(255,255,255,120),version.c_str());
  }
  std::string text;
};
std::unique_ptr<Label> label;
#if OUTRUN_HAS_NATIVE
// Show FPS with the native renderer: the SDK's counter counts the Xenos
// renderer's swaps, which never happen then. Same corner and look.
class Fps final : public rex::ui::ImGuiDialog {
 public:
  explicit Fps(rex::ui::ImGuiDrawer* drawer):ImGuiDialog(drawer){}
  bool WantsContinuousRedraw() const override {return false;}
 private:
  void OnDraw(ImGuiIO& io) override {
    if(rex::cvar::GetFlagByName("show_fps")!="true" || !NativeRendererActive())return;
    const double fps=NativeFps();
    if(fps<=0)return;
    char text[32];
    std::snprintf(text,sizeof(text),"%.1f FPS",fps);
    ImFont* font=ImGui::GetFont();
    const float size=ImGui::GetFontSize()*std::max(1.f,std::round(io.DisplaySize.y/720.f*1.5f));
    const ImVec2 extent=font->CalcTextSizeA(size,FLT_MAX,0,text);
    const float pad=4*std::max(1.f,io.DisplaySize.y/720.f);
    const ImVec2 at(std::floor(io.DisplaySize.x-10-extent.x-2*pad),10);
    auto* draw=ImGui::GetForegroundDrawList();
    draw->AddRectFilled(at,ImVec2(at.x+extent.x+2*pad,at.y+extent.y+2*pad),IM_COL32(0,0,0,128),3);
    draw->AddText(font,size,ImVec2(at.x+pad,at.y+pad),IM_COL32(255,255,255,255),text);
  }
};
std::unique_ptr<Fps> fps;
#endif
}
void Initialize(rex::ui::ImGuiDrawer* drawer){
  if(!drawer)return;
  label=std::make_unique<Label>(drawer);
#if OUTRUN_HAS_NATIVE
  fps=std::make_unique<Fps>(drawer);
#endif
}
void Shutdown(){
  label.reset();
#if OUTRUN_HAS_NATIVE
  fps.reset();
#endif
}
}
