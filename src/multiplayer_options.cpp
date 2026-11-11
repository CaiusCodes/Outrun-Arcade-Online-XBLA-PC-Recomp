#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <Xinput.h>
#include "generated/default/outrun_init.h"
#include "multiplayer_options.h"
#include "multiplayer_menu_patch.h"
#include "multiplayer_settings.h"
#include "multiplayer_ping.h"
#include "keyboard_context.h"
#include "menu_pointer.h"
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/kernel/xam/netplay.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/window.h>
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>

namespace outrun::multiplayer {
namespace {
std::atomic<bool> enabled{}, requested{}, modal{}, namePending{};
bool settingsLoaded=false;  // a file that could not be read is never overwritten
std::atomic<uint64_t> blockUntil{};
std::mutex stateMutex;
Preferences saved;
std::filesystem::path settingsPath;
std::string addressText;
std::unique_ptr<PingMonitor> ping;
bool Enabled(){return rex::cvar::GetFlagByName("outrun_local_sessions")=="true";}

class OptionsDialog final:public rex::ui::ImGuiDialog,public rex::ui::WindowInputListener {
 public:
  OptionsDialog(rex::ui::ImGuiDrawer* drawer,rex::ui::Window* window):ImGuiDialog(drawer),window_(window){
    window_->AddInputListener(this,SIZE_MAX);
  }
  ~OptionsDialog() override {window_->RemoveInputListener(this);}
#if defined(REX_IMGUI_DIALOG_REDRAW_POLICY) || OUTRUN_PASSIVE_DIALOGS
  bool WantsContinuousRedraw() const override { return modal.load() || requested.load(); }
#endif
  // The shipped drawer writes legacy MouseDown directly, losing clicks that
  // start and finish between frames. Queue this modal's edges before that
  // listener runs, letting ImGui preserve both halves across frames.
  void OnMouseDown(rex::ui::MouseEvent& e) override {
    if(modal && e.button()==rex::ui::MouseEvent::Button::kLeft)GetIO().AddMouseButtonEvent(0,true);
    if(modal && e.button()==rex::ui::MouseEvent::Button::kRight){mouseCancel_=true;e.set_handled(true);}
  }
  void OnMouseUp(rex::ui::MouseEvent& e) override {
    if(modal && e.button()==rex::ui::MouseEvent::Button::kLeft)GetIO().AddMouseButtonEvent(0,false);
  }
 private:
  rex::ui::Window* window_;
  char name_[16]{};
  char host_[16]{};
  bool showPing_=false,waitRelease_=true;
  WORD previousButtons_=0;
  int selected_=0;
  bool focus_=false;
  bool previousKeyboardNav_=false;
  std::atomic<bool> mouseCancel_{false};
  std::string error_;
  void Dismiss() {
    outrun::pointer::Clear();
    ImGui::CloseCurrentPopup();modal=false;
    if(!previousKeyboardNav_)GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    blockUntil=GetTickCount64()+300;
  }
  bool Accept() {
    error_=ValidateName(name_);
    if(error_.empty())error_=ValidateHostAddress(host_);
    if (!error_.empty())return false;
    try {
      Preferences next{name_,showPing_,host_,true};
      {std::lock_guard lock(stateMutex);next.localName=saved.localName;}  // (set at the Ranking screen)
      SavePreferences(settingsPath,next);
      rex::cvar::SetFlagByName("net_player_name",next.name);
      rex::cvar::SetFlagByName("net_join_address",next.hostAddress);
      {
        std::lock_guard lock(stateMutex);
        if(next.name!=saved.name)namePending=true;
        saved=next;
        if(ping)ping->Enable(next.showPing);
      }
      REXLOG_INFO("Multiplayer Options saved; Show Ping {}",showPing_ ? "on":"off");
      return true;
    } catch(const std::exception& e) {
      REXLOG_ERROR("Multiplayer Options save failed: {}",e.what());
      error_="Could not save. Your previous settings are unchanged.";
      return false;
    }
  }
  WORD Buttons() {
    if(!window_->HasFocus())return 0;
    XINPUT_STATE state{};
    for(DWORD i=0;i<4;++i)if(XInputGetState(i,&state)==ERROR_SUCCESS) {
      WORD buttons=state.Gamepad.wButtons;
      if(state.Gamepad.sThumbLY>16000)buttons|=XINPUT_GAMEPAD_DPAD_UP;
      if(state.Gamepad.sThumbLY<-16000)buttons|=XINPUT_GAMEPAD_DPAD_DOWN;
      return buttons;
    }
    return 0;
  }
  void OnDraw(ImGuiIO& io) override {
    if(!enabled)return;
    if(requested.exchange(false)) {
      {std::lock_guard lock(stateMutex);
        std::memset(name_,0,sizeof(name_));
        std::memcpy(name_,saved.name.data(),std::min(saved.name.size(),size_t(15)));
        showPing_=saved.showPing;
        std::memset(host_,0,sizeof(host_));std::memcpy(host_,saved.hostAddress.data(),std::min(saved.hostAddress.size(),size_t(15)));
      }
      error_.clear();selected_=0;focus_=true;waitRelease_=true;previousButtons_=0;
      previousKeyboardNav_=(io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard)!=0;
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      modal=true;
      ImGui::OpenPopup("Multiplayer Options");
    }
    const float scale=std::clamp(io.DisplaySize.y/720.0f,0.8f,1.5f);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x/2,io.DisplaySize.y/2),ImGuiCond_Always,ImVec2(.5f,.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(640.f*scale,io.DisplaySize.x-24),0),ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(24*scale,20*scale));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(10*scale,8*scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,ImVec2(12*scale,14*scale));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(.025f,.13f,.24f,1));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive,ImVec4(.0f,.42f,.69f,1));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.025f,.28f,.49f,1));
    ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.03f,.35f,.65f,1));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.08f,.55f,.85f,1));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,ImVec4(1,.71f,.08f,1));
    ImGui::PushStyleColor(ImGuiCol_Border,ImVec4(.1f,.65f,.9f,1));
    ImGui::PushStyleColor(ImGuiCol_Separator,ImVec4(.08f,.35f,.55f,1));
    if(ImGui::BeginPopupModal("Multiplayer Options",nullptr,
          ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove)) {
      ImGui::SetWindowFontScale(1.65f*scale);
      const WORD buttons=Buttons();
      if(!buttons)waitRelease_=false;
      const WORD pressed=waitRelease_ ? 0 : WORD(buttons & ~previousButtons_);
      previousButtons_=buttons;
      if(pressed & XINPUT_GAMEPAD_DPAD_DOWN){selected_=(selected_+1)%5;focus_=true;}
      if(pressed & XINPUT_GAMEPAD_DPAD_UP){selected_=(selected_+4)%5;focus_=true;}
      // Backspace remains text editing in the name/address fields; elsewhere
      // it backs out like B. Enter/Space already activate ImGui navigation.
      bool cancel=(pressed & XINPUT_GAMEPAD_B) || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
          (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Backspace));
      bool accept=false;
      const bool activate=(pressed & XINPUT_GAMEPAD_A)!=0;
      ImGui::TextUnformatted("Online Name");
      ImGui::SameLine(235*scale);
      ImGui::SetNextItemWidth(-1);
      if(focus_ && selected_==0)ImGui::SetKeyboardFocusHere();
      ImGui::InputText("##player_name",name_,sizeof(name_));
      if(!focus_ && ImGui::IsItemFocused())selected_=0;
      ImGui::PushTextWrapPos(0.0f);ImGui::TextDisabled("1-15 characters, shown to other players. Use your keyboard to edit.");ImGui::PopTextWrapPos();
      ImGui::Separator();
      ImGui::TextUnformatted("Your Local Address");
      ImGui::SameLine(235*scale);
      ImGui::TextUnformatted(addressText.c_str());
      ImGui::TextUnformatted("Show Ping");
      ImGui::SameLine(235*scale);
      if(focus_ && selected_==1)ImGui::SetKeyboardFocusHere();
      ImGui::Checkbox("##ping",&showPing_);
      if(!focus_ && ImGui::IsItemFocused())selected_=1;
      ImGui::SameLine();ImGui::TextUnformatted(showPing_ ? "On":"Off");
      if(selected_==1 && (activate || (pressed & (XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT))))showPing_=!showPing_;
      ImGui::Separator();
      ImGui::TextUnformatted("Host Address");ImGui::SameLine(235*scale);ImGui::SetNextItemWidth(-1);
      if(focus_ && selected_==2)ImGui::SetKeyboardFocusHere();
      ImGui::InputTextWithHint("##host_address","Automatic",host_,sizeof(host_));
      if(!focus_ && ImGui::IsItemFocused())selected_=2;
      ImGui::TextWrapped("Leave blank to find LAN lobbies automatically. If discovery fails, enter the host's IPv4 address.");
      if(!error_.empty())ImGui::TextWrapped("%s",error_.c_str());
      ImGui::Separator();
      if(focus_ && selected_==3)ImGui::SetKeyboardFocusHere();
      cancel|=ImGui::Button("Cancel",ImVec2(145*scale,0)) || (selected_==3 && activate);
      if(!focus_ && ImGui::IsItemFocused())selected_=3;
      ImGui::SameLine();
      if(focus_ && selected_==4)ImGui::SetKeyboardFocusHere();
      accept=ImGui::Button("Accept",ImVec2(145*scale,0)) || (selected_==4 && activate) || (pressed & XINPUT_GAMEPAD_START);
      if(!focus_ && ImGui::IsItemFocused())selected_=4;
      focus_=false;
      cancel|=mouseCancel_.exchange(false);
      if(cancel)Dismiss();
      else if(accept && Accept())Dismiss();
      ImGui::EndPopup();
    }
    ImGui::PopStyleColor(8);ImGui::PopStyleVar(3);
    bool show=false;PingSnapshot sample;
    {std::lock_guard lock(stateMutex);show=saved.showPing;if(ping)sample=ping->Snapshot();}
    if(show) {
      ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x-12,50),ImGuiCond_Always,ImVec2(1,0));
      ImGui::SetNextWindowBgAlpha(.7f);
      if(ImGui::Begin("##OutrunPing",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|
          ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing|ImGuiWindowFlags_NoInputs|ImGuiWindowFlags_NoNav)) {
        const char* label=sample.peers>1 ? "Ping (max)":"Ping";
        if(!sample.milliseconds)ImGui::Text("%s: --",label);
        else if(*sample.milliseconds<1)ImGui::Text("%s: <1 ms",label);
        else ImGui::Text("%s: %.0f ms",label,std::ceil(*sample.milliseconds));
      }
      ImGui::End();
    }
  }
};
std::unique_ptr<OptionsDialog> dialog;
}
void Load(const std::filesystem::path& config) {
  settingsPath=config.parent_path()/"multiplayer-settings.toml";
  saved={rex::kernel::xam::netplay::PlayerName("Player"),false};
  try {saved=LoadOrCreatePreferences(settingsPath,rex::cvar::GetFlagByName("net_player_name"),rex::cvar::GetFlagByName("net_join_address"));settingsLoaded=true;}
  catch(const std::exception& e){REXLOG_ERROR("Multiplayer settings not loaded: {}",e.what());}
  rex::cvar::SetFlagByName("net_player_name",saved.name);
  if(saved.hostAddressSpecified)rex::cvar::SetFlagByName("net_join_address",saved.hostAddress);
  else saved.hostAddress=rex::cvar::GetFlagByName("net_join_address");
  if(Enabled()) {
    // Older launcher configurations pinned a single interface, which also made
    // the SDK responder bind only that address. Normal play now uses all active
    // interfaces; retain the original file and explicit loopback dev instances.
    const auto previous=rex::cvar::GetFlagByName("net_address");
    if(!previous.empty()&&!previous.starts_with("127.")) {
      rex::cvar::SetFlagByName("net_address","");
      REXLOG_INFO("LAN networking uses automatic interfaces; legacy local selection remains in the configuration file");
    }
  }
}
void Initialize(rex::ui::ImGuiDrawer* drawer,rex::ui::Window* window) {
  if(!Enabled())return;
  uint8_t xnaddr[36]{};
  rex::kernel::xam::netplay::FillLocalXnAddr(xnaddr);
  uint32_t local=0;std::memcpy(&local,xnaddr,4);
  char formatted[INET_ADDRSTRLEN]{};
  inet_ntop(AF_INET,&local,formatted,sizeof(formatted));
  addressText=formatted;
  {std::lock_guard lock(stateMutex);ping=std::make_unique<PingMonitor>(local);ping->Enable(saved.showPing);}
  dialog=std::make_unique<OptionsDialog>(drawer,window);
  enabled=true;
}
void Shutdown() {
  enabled=false;requested=false;modal=false;
  dialog.reset();
  std::lock_guard lock(stateMutex);ping.reset();
}
void Open(){if(enabled && !modal.exchange(true))requested=true;}
std::string HighScoreInitials(){std::lock_guard lock(stateMutex);return HighScoreInitials(saved);}
void SetLocalName(const std::string& name) {
  std::lock_guard lock(stateMutex);
  if(name==saved.localName || !ValidateLocalName(name).empty())return;
  auto next=saved;next.localName=name;
  if(settingsLoaded) {
    try {SavePreferences(settingsPath,next);}
    catch(const std::exception& e){REXLOG_ERROR("Local name not saved: {}",e.what());return;}
  }
  saved=next;
}
}

REX_HOOK_RAW(sub_82193C00) {
  using namespace outrun::multiplayer;
  const auto call=ctx.r4.u32;
  if(enabled && REX_LOAD_U32(call+16)==1 && REX_LOAD_U32(REX_LOAD_U32(call+12))==kOpenCommand) {
    Open();REX_STORE_U32(call+20,0);REX_STORE_U32(call+24,2);return;
  }
  outrun::pointer::AdjustColour(ctx.r3.u32,call,base);
  __imp__sub_82193C00(ctx,base);
}
REX_HOOK_RAW(sub_821928A8) {
  outrun::keyboard::MenuInputPolled();
  using namespace outrun::multiplayer;
  const auto call=ctx.r4.u32;
  const auto vm=ctx.r3.u32;
  __imp__sub_821928A8(ctx,base);
  if(enabled && (modal || GetTickCount64()<blockUntil.load())){REX_STORE_U32(call+20,0);outrun::pointer::Clear();}
  else REX_STORE_U32(call+20,outrun::pointer::Input(vm,base,REX_LOAD_U32(call+20)));
}
// This per-frame profile updater reads the same live manager pointer that its
// original routine uses. Refresh only user zero's verified 16-byte name cache,
// on the guest thread, before the next lobby is created. XUID/saves stay intact.
REX_HOOK_RAW(sub_82119388) {
  using namespace outrun::multiplayer;
  __imp__sub_82119388(ctx,base);
  if(enabled && namePending.exchange(false)) {
    const auto manager=REX_LOAD_U32(uint32_t(-2102984704LL-15064));
    if(manager) {
      std::lock_guard lock(stateMutex);
      std::memset(base+manager+56,0,16);
      std::memcpy(base+manager+56,saved.name.data(),std::min(saved.name.size(),size_t(15)));
      REXLOG_INFO("Multiplayer player-name cache refreshed for the next lobby");
    }
  }
}
REX_HOOK_RAW(__imp__NetDll_recvfrom) {
  using namespace outrun::multiplayer;
  const auto from=ctx.r8.u32,fromLength=ctx.r9.u32;
  static auto original=reinterpret_cast<PPCFunc*>(GetProcAddress(GetModuleHandleW(L"rexruntime.dll"),"__imp__NetDll_recvfrom"));
  if(!original || original==&__imp__NetDll_recvfrom)throw std::runtime_error("Original receive export unavailable");
  original(ctx,base);
  if(enabled && ctx.r3.s32>0 && from && fromLength && REX_LOAD_U32(fromLength)>=16 &&
      REX_LOAD_U16(from)==AF_INET && REX_LOAD_U16(from+2)==1000) {
    uint32_t peer;std::memcpy(&peer,base+from+4,4);
    std::lock_guard lock(stateMutex);if(ping)ping->Observe(peer);
  }
}
