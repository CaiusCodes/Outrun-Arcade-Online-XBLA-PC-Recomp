#pragma once
#include <rex/rex_app.h>
#include <rex/input/input_system.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include "keyboard_input.h"
#include "controller_presence.h"
#include "pc_menu.h"
#include "multiplayer_options.h"
#include "menu_affordances.h"
#include "build_label.h"
#include "keyboard_prompt.h"
#include "restart_prompt.h"
#include "update_prompt.h"
#include "exit_game.h"
#include "ui_font.h"
#include "runtime_shortcuts.h"
#include "userdata_location.h"
#include "player_log.h"
#include <filesystem>
#include <windows.h>
REXCVAR_DECLARE(bool,outrun_runtime_shortcuts);

class OutrunPortableApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<OutrunPortableApp>(new OutrunPortableApp(ctx, "outrun", PPCImageConfig));
  }
 protected:
  void OnKeyDown(rex::ui::KeyEvent& event) override {
    outrun::DispatchRuntimeShortcut(runtimeShortcutsEnabled_,event);
  }
  bool OnWindowCloseRequested() override {
    outrun::diagnostics::SavePCM();
    outrun::player_log::OnWindowClose();
    return true;
  }
  void OnConfigureFonts(ImFontAtlas* atlas) override { outrun::ui_font::Load(atlas); }
  void OnPreLaunchModule() override {
    // The runtime creates an SDL window; explicitly apply our embedded icon
    // to both title-bar and taskbar slots rather than the SDL default icon.
    if(window()) {
      const auto handle=static_cast<HWND>(window()->GetNativeWindowHandle());
      if(handle) {
        const auto instance=GetModuleHandleW(nullptr);
        for(const auto kind:{ICON_SMALL,ICON_BIG}) {
          const int size=GetSystemMetrics(kind==ICON_SMALL?SM_CXSMICON:SM_CXICON);
          const auto icon=LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,size,size,LR_SHARED);
          if(icon)SendMessageW(handle,WM_SETICON,kind,reinterpret_cast<LPARAM>(icon));
        }
      }
    }
    outrun::multiplayer::Initialize(imgui_drawer(), window());
    outrun::menu_affordances::Initialize(imgui_drawer());
    outrun::build_label::Initialize(imgui_drawer());
    outrun::keyboard_prompt::Initialize(imgui_drawer(), window());
    outrun::restart_prompt::Initialize(imgui_drawer(), window());
    outrun::update_prompt::Initialize(imgui_drawer(), window());
    outrun::exit_game::Initialize(window());
    outrun::pc_menu::InitializePrototype(runtime(), window());
    outrun::player_log::Startup(runtime(), window(), imgui_drawer());
  }
  void OnShutdown() override {
    outrun::multiplayer::Shutdown();
    outrun::menu_affordances::Shutdown();
    outrun::build_label::Shutdown();
    outrun::keyboard_prompt::Shutdown();
    outrun::pc_menu::ShutdownPrototype();
    outrun::player_log::Shutdown();
    outrun::update_prompt::Shutdown();
    outrun::exit_game::Shutdown();
    outrun::restart_prompt::Shutdown();  // last: may start the game again
  }
  void OnPreSetup(rex::RuntimeConfig& config) override {
    runtimeShortcutsEnabled_=REXCVAR_GET(outrun_runtime_shortcuts);
    outrun::pc_menu::LoadPrototypeSettings(config_path());
    outrun::multiplayer::Load(config_path());
    config.gpu_plugin = "xenos";
    // The native Plume renderer (native_gpu.cpp) hands its frames to the SDK's
    // presenter and runs on its API: Direct3D 12 unless gpu = vulkan is set
    // (auto would pick Vulkan on AMD and Intel GPUs; the native renderer's
    // tested path is Direct3D 12). The Xenos plugin still supplies the vblank
    // and stays the fallback if Plume cannot start.
    if (rex::cvar::GetFlagByName("gpu_native") == "true" && rex::cvar::GetFlagByName("gpu") != "vulkan")
      rex::cvar::SetFlagByName("gpu", "d3d12");
    config.input_factory = [](bool) {
      auto input = std::make_unique<rex::input::InputSystem>(nullptr);
      auto pad = std::make_unique<rex::input::OutrunControllerInput>();
      if (pad->Setup() == 0) input->AddDriver(std::move(pad));
      auto keyboard = std::make_unique<rex::input::OutrunKeyboardInput>();
      if (keyboard->Setup() == 0) input->AddDriver(std::move(keyboard));
      return input;
    };
  }
  std::string GetWindowTitle() const override { return "OutRun Online Arcade PC"; }
  void OnConfigurePaths(rex::PathConfig& paths) override {
    wchar_t executable[32768] = {};
    GetModuleFileNameW(nullptr, executable, 32768);
    const auto root = std::filesystem::path(executable).parent_path();
    paths.game_data_root = root / "assets";
    paths.user_data_root = outrun::userdata_location::Resolve(root);
    paths.cache_root = root / "cache";
    paths.update_data_root = root / "updates";
    paths.config_path = root / "outrun.toml";
    std::filesystem::create_directories(paths.user_data_root);
    std::filesystem::create_directories(paths.cache_root);
  }
 private:
  bool runtimeShortcutsEnabled_=false;
};
