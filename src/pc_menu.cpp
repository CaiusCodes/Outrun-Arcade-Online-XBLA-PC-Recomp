#include "generated/default/outrun_init.h"
#include "pc_menu.h"
#include "frontend_patch.h"
#include "pc_menu_patch.h"
#include "pc_settings_store.h"
#include "multiplayer_menu_patch.h"
#include "menu_pointer.h"
#include "restart_prompt.h"
#include "update_prompt.h"
#include "userdata_location.h"
#include "player_log.h"
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>
#include <rex/system.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/ui/window.h>
#include <rex/ui/window_sdl.h>
#include <rex/ui/windowed_app_context.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>

REXCVAR_DEFINE_BOOL(pc_settings_prototype, false, "OutRun",
                   "Development-only native Settings spinners; requires the local renderer overlay")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_INT32(outrun_window_resolution, 0, "OutRun/Graphics",
                    "Window resolution: 0=720p, 1=1080p, 2=1440p, 3=2160p").range(0,3);

REXCVAR_DEFINE_INT32(outrun_aspect_ratio, 0, "OutRun/Graphics",
                    "Native aspect on next launch: 0=16:9, 1=4:3").range(0,1);

namespace outrun::pc_menu {
namespace {
rex::Runtime* runtime = nullptr;
rex::Runtime* frontendRuntime = nullptr;
bool autoloadInstalled = false;
bool leaderboardInstalled = false;
bool pauseInstalled = false;
bool mainMenuInstalled = false;
constexpr Text kFrontendTexts[] = {
    {0xEC856A99,"Local Leaderboards"},
    {0x7B52EE25,"Local Scores"},
    {0x32874472,"Color"},   // NewMM (LAN lobby footer)
    {0x58696AFD,"Color"},   // TRC (car select footer)
    {0x8B979764,"My Scores"},
    {0xC09E7C33,"View scores saved on this PC."},
    {0x07855DE7,"View Single Player scores\nsaved on this PC."},
    {0xB8D5A680,"Multiplayer rankings are not\navailable for LAN matches."},
    // Main menu B (TRC): the game's return to the console's library, which
    // now closes the game (exit_game.cpp).
    {0xC99563C8,"Exit Game"},
    {0xD5BAC512,"Exit OutRun Online Arcade?"},
};
std::array<uint32_t,std::size(kFrontendTexts)> frontendTextAddresses{};
uint32_t keyboardStartText=0;
rex::Runtime* multiplayerRuntime = nullptr;
bool multiplayerInstalled = false;
bool lobbyInstalled = false;
rex::ui::Window* window = nullptr;
std::mutex mutex;
SettingsSession session;
std::filesystem::path settingsPath;
std::filesystem::path frontendUserData;
std::array<uint32_t, std::size(kTexts)> textAddresses{};
// Original localized IDs shared by menu tabs, headings and match-type labels.
// Keep these separate from the port-owned PC Settings IDs.
constexpr Text kLanTexts[] = {
    {outrun::multiplayer::kTitleId, "Multiplayer Options"},
    {outrun::multiplayer::kDescriptionId, "Player name, local address\nand ping display."},
    {0xC8CC09A1, "LAN Match"}, {0xDD0432BD, "LAN Match"},
    {0x6B436510, "LAN Match"}, {0x7AA20A23, "LAN Match"},
    {0x4FACF622, "Host LAN Match"}, {0xA92CB7CB, "Host LAN Match"},
    {0x0D047EB4, "Play OutRun via LAN or\nvirtual LAN networks."},
    {0xC13E7C11, "Play OutRun via LAN or\nvirtual LAN networks."},
};
std::array<uint32_t, std::size(kLanTexts)> lanTextAddresses{};
bool installed = false;
using ApplyFilter = bool(*)(void*);
ApplyFilter applyFilter = nullptr;
void ResizeWindow(rex::ui::Window* target, unsigned sizeIndex) {
  if (!target->IsFullscreen()) {
    if (auto* sdl = dynamic_cast<rex::ui::WindowSDL*>(target)) {
      constexpr unsigned widths[] = {1280,1920,2560,3840};
      constexpr unsigned heights[] = {720,1080,1440,2160};
      sdl->SetLogicalSize(widths[std::min(sizeIndex,3u)], heights[std::min(sizeIndex,3u)]);
    }
  }
}
void Preview(size_t index, uint32_t value) {
  // Live: texture filtering, anti-aliasing (the renderer remakes its scene
  // targets) and the picture settings (read by the output pass each frame).
  if (index == 8) {
    rex::cvar::SetFlagByName("gpu_native_texture_filtering", std::to_string(value));
    return;
  } else if (index == 9) {
    constexpr unsigned samples[] = {1, 2, 4, 8};
    rex::cvar::SetFlagByName("gpu_native_msaa", std::to_string(samples[std::min(value, 3u)]));
    return;
  } else if (index == 13) {  // HUD: the renderer makes the HUD's textures again
    rex::cvar::SetFlagByName("outrun_hud", std::to_string(std::min(value, 3u)));
    return;
  } else if (index >= 10 && index <= 12) {
    const auto* const* choices = index == 10 ? kSaturation : index == 11 ? kContrast : kBlackLevel;
    rex::cvar::SetFlagByName(index == 10 ? "color_saturation" : index == 11 ? "color_contrast" : "color_black_level",
                             choices[std::min(value, 2u)]);
    return;
  }
  if (index >= 5) {
    // Native display initialization chooses its render mode, renderer and
    // graphics API once at boot (pc-settings.toml loads before it).
    // The session alone stages this value; Cancel and live controls stay safe.
    return;
  } else if (index == 0) {
    rex::cvar::SetFlagByName("fullscreen", value ? "true" : "false");
    auto* target = window;
    const auto scale = unsigned(REXCVAR_GET(outrun_window_resolution));
    target->app_context().CallInUIThread([target, value, scale] {
      target->SetFullscreen(value != 0);
      ResizeWindow(target, scale);
    });
  } else if (index == 1) {
    // Render Scale (restart): the native renderer sizes its targets at start;
    // the saved value takes effect at the next launch.

  } else if (index == 3) {
    rex::cvar::SetFlagByName("outrun_window_resolution", std::to_string(value));
    auto* target = window;
    target->app_context().CallInUIThread([target,value] { ResizeWindow(target,value); });
  } else if (index == 4) {
    rex::cvar::SetFlagByName("outrun_present_filter", std::to_string(value));
    auto* target=window;
    auto* graphics=runtime->graphics_system();
    target->app_context().CallInUIThread([graphics] {
      if (applyFilter && graphics && !applyFilter(graphics->presenter())) REXLOG_ERROR("Image filter could not be applied");
    });
  } else if (index == 2) {
    rex::cvar::SetFlagByName("show_fps", value ? "true" : "false");
  }
}
}
// The committed PC settings, for the player log's startup header.
std::array<unsigned, 14> CurrentSettings() {
  std::lock_guard lock(mutex);
  std::array<unsigned, 14> v{};
  for (size_t i = 0; i < v.size(); ++i) v[i] = session.Read(i);
  return v;
}
bool PrototypeActive() { return runtime != nullptr; }
void LoadPrototypeSettings(const std::filesystem::path& configPath) {
  frontendUserData=outrun::userdata_location::Resolve(configPath.parent_path());
  if (!REXCVAR_GET(pc_settings_prototype)) return;
  settingsPath = configPath.parent_path()/"pc-settings.toml";
  try {
    if (const auto saved = LoadSettings(settingsPath)) {
      rex::cvar::LoadConfig(settingsPath);
      // Older files tied window size to render scale; preserve that size.
      rex::cvar::SetFlagByName("outrun_window_resolution",std::to_string((*saved)[3]));
    }
  } catch (const std::exception& error) {
    REXLOG_ERROR("PC settings were not loaded: {}", error.what());
  }
  const bool fourByThree=REXCVAR_GET(outrun_aspect_ratio)==1;
  outrun::pointer::fourByThree=fourByThree;
  rex::cvar::SetFlagByName("video_mode_width",fourByThree?"1024":"1280");
  rex::cvar::SetFlagByName("video_mode_height",fourByThree?"768":"720");
  // Ignore the removed option even when upgrading with an older overlay DLL.
  rex::cvar::SetFlagByName("presentation_vsync", "false");
}
void InitializePrototype(rex::Runtime* value, rex::ui::Window* target) {
  frontendRuntime = value;
  constexpr char prompt[]="PRESS ANY KEY";
  keyboardStartText=value->memory()->SystemHeapAlloc(sizeof(prompt));
  if(!keyboardStartText)throw std::runtime_error("Title prompt allocation failed");
  std::memcpy(value->memory()->virtual_membase()+keyboardStartText,prompt,sizeof(prompt));
  for(size_t i=0;i<std::size(kFrontendTexts);++i) {
    const auto text=kFrontendTexts[i].value;
    const auto address=value->memory()->SystemHeapAlloc(uint32_t(text.size()+1));
    if(!address)throw std::runtime_error("Frontend text allocation failed");
    std::memcpy(value->memory()->virtual_membase()+address,text.data(),text.size());
    value->memory()->virtual_membase()[address+text.size()]=0;
    frontendTextAddresses[i]=address;
  }
  {
    std::lock_guard lock(mutex);
    lanTextAddresses.fill(0);
    if (rex::cvar::GetFlagByName("outrun_local_sessions") == "true") {
      multiplayerRuntime = value;
      auto* memory = value->memory();
      auto* base = memory->virtual_membase();
      for (size_t i = 0; i < std::size(kLanTexts); ++i) {
        const auto text = kLanTexts[i].value;
        const auto address = memory->SystemHeapAlloc(uint32_t(text.size()+1));
        if (!address) {
          lanTextAddresses.fill(0);
          REXLOG_ERROR("LAN menu text allocation failed; retaining original labels");
          break;
        }
        std::memcpy(base+address, text.data(), text.size());
        base[address+text.size()] = 0;
        lanTextAddresses[i] = address;
      }
      if (lanTextAddresses[0]) REXLOG_INFO("LAN menu labels enabled: LAN Match / Host LAN Match");
    }
  }
  if (!REXCVAR_GET(pc_settings_prototype)) return;
  std::lock_guard lock(mutex);
  if (!rex::cvar::GetFlagInfo("outrun_live_resolution")) {
    REXLOG_ERROR("PC menu prototype needs the project-local renderer DLLs; leaving original menu intact");
    return;
  }
  applyFilter = reinterpret_cast<ApplyFilter>(GetProcAddress(GetModuleHandleW(L"rexruntime.dll"), "outrun_apply_present_filter"));
  if (!applyFilter) { REXLOG_ERROR("Image filters require the matching spatial renderer; retaining original Settings"); return; }
  runtime = value; window = target;
  SettingsValues initial{};
  initial[0] = rex::cvar::GetFlagByName("fullscreen") == "true";
  const auto scale = rex::cvar::GetFlagByName("resolution_scale");
  initial[1] = scale == "2" ? 1 : scale == "3" ? 2 : 0;
  initial[2] = rex::cvar::GetFlagByName("show_fps") == "true";
  initial[3] = unsigned(REXCVAR_GET(outrun_window_resolution));
  initial[4] = unsigned(std::clamp(std::stoi(rex::cvar::GetFlagByName("outrun_present_filter")),0,2));
  initial[5] = unsigned(REXCVAR_GET(outrun_aspect_ratio));
  initial[6] = rex::cvar::GetFlagByName("gpu_native") == "false" ? 1 : 0;
  initial[7] = rex::cvar::GetFlagByName("gpu") == "vulkan" ? 1 : 0;
  initial[8] = unsigned(std::clamp(std::atoi(rex::cvar::GetFlagByName("gpu_native_texture_filtering").c_str()), 0, 4));
  {
    const int samples = std::atoi(rex::cvar::GetFlagByName("gpu_native_msaa").c_str());
    initial[9] = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
  }
  auto colour = [](const char* name, double fallback) {
    const auto v = rex::cvar::GetFlagByName(name);
    return v.empty() ? fallback : std::atof(v.c_str());
  };
  initial[10] = NearestChoice(kSaturation, colour("color_saturation", 1.0));
  initial[11] = NearestChoice(kContrast, colour("color_contrast", 1.0));
  initial[12] = NearestChoice(kBlackLevel, colour("color_black_level", 0.0));
  initial[13] = unsigned(std::clamp(std::atoi(rex::cvar::GetFlagByName("outrun_hud").c_str()), 0, 3));
  session = SettingsSession(initial);
  rex::cvar::SetFlagByName("outrun_live_resolution", "true");
  Preview(3, initial[3]);
  Preview(4, initial[4]);
  auto* base = runtime->memory()->virtual_membase();
  for (size_t i = 0; i < std::size(kTexts); ++i) {
    std::string text(kTexts[i].value);
    if (kTexts[i].id == kUpdateDescription) text += "\nThis copy: version " OUTRUN_VERSION_TEXT;
    const auto address = runtime->memory()->SystemHeapAlloc(uint32_t(text.size()+1));
    if (!address) { runtime = nullptr; REXLOG_ERROR("PC menu text allocation failed"); return; }
    std::memcpy(base+address, text.data(), text.size());
    base[address+text.size()] = 0;
    textAddresses[i] = address;
  }
  REXLOG_INFO("PC menu prototype enabled with atomic saves and experimental live renderer controls");
}
void ShutdownPrototype() {
  std::lock_guard lock(mutex);
  runtime = nullptr; window = nullptr; installed = false;
  frontendRuntime = nullptr; autoloadInstalled = false;
  keyboardStartText=0;
  leaderboardInstalled = false; frontendTextAddresses.fill(0);
  pauseInstalled = false;
  mainMenuInstalled = false;
  multiplayerRuntime = nullptr; multiplayerInstalled = false;
  lobbyInstalled = false;
  lanTextAddresses.fill(0);
  // System heap allocations live for the runtime, since native text widgets and
  // script registry entries retain guest pointers until the runtime is destroyed.
}
}

REX_HOOK_RAW(sub_821DC338) {
  using namespace outrun::pc_menu;
  {
    std::lock_guard lock(mutex);
    const auto package = ctx.r3.u32;
    if (frontendRuntime && (!autoloadInstalled || !leaderboardInstalled || !pauseInstalled || !mainMenuInstalled) && std::memcmp(base+package,"smospkg2",8)==0 &&
        REX_LOAD_U32(package+8)==0x20 && REX_LOAD_U32(package+12)==27) {
      for(uint32_t i=0;i<27;++i) {
        const auto row=package+0x28+i*32;
        const auto hash=REX_LOAD_U32(row+16);
        const bool leaderboard=hash==outrun::frontend::kLeaderboardHash;
        const bool pause=hash==outrun::frontend::kPauseHash;
        const bool mainMenu=hash==outrun::frontend::kMainMenuHash;
        if(mainMenu?mainMenuInstalled:pause?pauseInstalled:leaderboard?leaderboardInstalled:(hash!=outrun::frontend::kAutoloadHash || autoloadInstalled))continue;
        const auto expectedSize=mainMenu ? outrun::frontend::kMainMenuSize : pause ? outrun::frontend::kPauseSize : leaderboard ? outrun::frontend::kLeaderboardSize : outrun::frontend::kAutoloadSize;
        const auto scriptAddress=row+28+REX_LOAD_U32(row+28);
        try {
          if(REX_LOAD_U32(scriptAddress+8)!=expectedSize)
            throw std::runtime_error("Unrecognized frontend script size");
          const auto script=mainMenu ? outrun::frontend::BuildMainMenuScript({base+scriptAddress,expectedSize}) : pause ? outrun::frontend::BuildPauseScript({base+scriptAddress,expectedSize}) : leaderboard ? outrun::frontend::BuildLeaderboardScript({base+scriptAddress,expectedSize}) :
              outrun::frontend::BuildAutoloadScript({base+scriptAddress,expectedSize},
                  outrun::frontend::CanCreateFirstGameData(frontendUserData));
          const auto replacement=frontendRuntime->memory()->SystemHeapAlloc(uint32_t(script.size()));
          if(!replacement)throw std::runtime_error("Autoload script allocation failed");
          std::memcpy(base+replacement,script.data(),script.size());
          REX_STORE_U32(row+28,replacement-(row+28));
          if(mainMenu) {
            mainMenuInstalled=true;
            REXLOG_INFO("Main menu: removed Achievements from trial/full tab, heading and description arrays");
          } else if(pause) {
            pauseInstalled=true;
            REXLOG_INFO("Pause menu: removed Achievements; remaining action mappings preserved");
          } else if(leaderboard) {
            leaderboardInstalled=true;
            REXLOG_INFO("Frontend: Local Scores default, My Scores filter, no Xbox LIVE Friends filter");
          } else {
            autoloadInstalled=true;
            REXLOG_INFO("Frontend: quiet autosave notice; first-data creation {}, existing save recovery retained",
                outrun::frontend::CanCreateFirstGameData(frontendUserData) ? "automatic" : "original recovery choices");
          }
        } catch(const std::exception& e){REXLOG_ERROR("Frontend patch skipped: {}",e.what());}
      }
    }
    if (multiplayerRuntime && (!multiplayerInstalled || !lobbyInstalled) && lanTextAddresses[0] &&
        std::memcmp(base+package, "smospkg2", 8)==0 &&
        REX_LOAD_U32(package+8)==0x20 && REX_LOAD_U32(package+12)==27) {
      for(uint32_t i=0;i<27;++i) {
        const auto row=package+0x28+i*32;
        const auto hash=REX_LOAD_U32(row+16);
        const bool lobby=hash==outrun::multiplayer::kLobbyHash;
        if(lobby ? lobbyInstalled : (hash!=outrun::multiplayer::kScriptHash || multiplayerInstalled))continue;
        const auto expectedSize=lobby ? outrun::multiplayer::kLobbySize : outrun::multiplayer::kScriptSize;
        const auto oldScript=row+28+REX_LOAD_U32(row+28);
        try {
          if(REX_LOAD_U32(oldScript+8)!=expectedSize)
            throw std::runtime_error("Unrecognized multiplayer script size");
          const auto script=lobby ? outrun::multiplayer::BuildLobbyScript({base+oldScript,expectedSize}) :
              outrun::multiplayer::BuildMenuScript({base+oldScript,expectedSize});
          const auto replacement=multiplayerRuntime->memory()->SystemHeapAlloc(uint32_t(script.size()));
          if(!replacement)throw std::runtime_error("Multiplayer menu allocation failed");
          std::memcpy(base+replacement,script.data(),script.size());
          REX_STORE_U32(row+28,replacement-(row+28));
          if(lobby) {
            lobbyInstalled=true;
            REXLOG_INFO("LAN lobby: removed Gamer Card footer and X action at guest {:08X}",replacement);
          } else {
            multiplayerInstalled=true;
            REXLOG_INFO("Multiplayer Options: installed third native tab at guest {:08X}",replacement);
          }
        } catch(const std::exception& e){REXLOG_ERROR("Multiplayer Options patch skipped: {}",e.what());}
      }
    }
    if (runtime && !installed && std::memcmp(base+package, "smospkg2", 8) == 0 &&
        REX_LOAD_U32(package+8) == 0x20 && REX_LOAD_U32(package+12) == 27) {
      const auto table = package+0x28;
      for (uint32_t i = 0; i < 27; ++i) {
        const auto row = table+i*32;
        if (REX_LOAD_U32(row+16) != kOptionsHash) continue;
        const auto oldScript = row+28+REX_LOAD_U32(row+28);
        if (REX_LOAD_U32(oldScript+8) != kOriginalSize) break;
        try {
          const auto patched = BuildOptionsScript({base+oldScript, kOriginalSize});
          const auto& script = patched.bytes;
          const auto replacement = runtime->memory()->SystemHeapAlloc(uint32_t(script.size()));
          if (!replacement) throw std::runtime_error("PC Options allocation failed");
          std::memcpy(base+replacement, script.data(), script.size());
          outrun::pointer::optionsEditorStart = patched.editorStart;
          outrun::pointer::optionsSpinnerPc = patched.spinnerPc;
          // Only the loaded package's relative pointer changes. The disk asset
          // and original script bytes are preserved. Original loader relocates
          // the clone's constant pool and registers it normally.
          REX_STORE_U32(row+28, replacement-(row+28));
          installed = true;
          REXLOG_INFO("PC menu: Help & Options with Graphics and Check for Updates cards at guest {:08X}", replacement);
        } catch (const std::exception& error) {
          REXLOG_ERROR("PC menu patch skipped: {}", error.what());
        }
        break;
      }
    }
  }
  const auto package=ctx.r3.u32;
  if(std::memcmp(base+package,"smospkg2",8)==0 && REX_LOAD_U32(package+12)==27) {
    for(uint32_t i=0;i<27;++i){const auto row=package+0x28+i*32;outrun::pointer::RegisterScript(row+28+REX_LOAD_U32(row+28),REX_LOAD_U32(row+16));}
  }
  __imp__sub_821DC338(ctx, base);
}

REX_HOOK_RAW(sub_8226E8D0) {
  using namespace outrun::pc_menu;
  {
    std::lock_guard lock(mutex);
    if(ctx.r4.u32==0xE9F4569D && keyboardStartText && !outrun::pointer::controllerConnected.load()) {ctx.r3.u64=keyboardStartText;return;}
    for(size_t i=0;i<std::size(kFrontendTexts);++i) {
      if(frontendTextAddresses[i] && ctx.r4.u32==kFrontendTexts[i].id) {
        ctx.r3.u64=frontendTextAddresses[i]; return;
      }
    }
    for (size_t i = 0; i < std::size(kLanTexts); ++i) {
      if (lanTextAddresses[i] && ctx.r4.u32 == kLanTexts[i].id) {
        ctx.r3.u64 = lanTextAddresses[i];
        return;
      }
    }
    if (installed) {
      for (size_t i = 0; i < std::size(kTexts); ++i) {
        if (ctx.r4.u32 == kTexts[i].id) { ctx.r3.u64 = textAddresses[i]; return; }
      }
    }
  }
  __imp__sub_8226E8D0(ctx, base);
}

REX_HOOK_RAW(sub_82192DA0) {
  using namespace outrun::pc_menu;
  {
    std::lock_guard lock(mutex);
    const auto call = ctx.r4.u32;
    if (installed && REX_LOAD_U32(call+16) == 1) {
      const auto id = REX_LOAD_U32(REX_LOAD_U32(call+12));
      if (id >= kFirstSetting && id < kFirstSetting+SettingsValues{}.size()) {
        const auto value = session.Read(id-kFirstSetting);
        REX_STORE_U32(call+20, value);
        REX_STORE_U32(call+24, 2); // VM integer type.
        REXLOG_DEBUG("PC menu: read {} = {}", id, value);
        return;
      }
    }
  }
  __imp__sub_82192DA0(ctx, base);
}

REX_HOOK_RAW(sub_82193418) {
  using namespace outrun::pc_menu;
  {
    std::lock_guard lock(mutex);
    const auto call = ctx.r4.u32;
    if (installed && REX_LOAD_U32(call+16) == 3) {
      const auto args = REX_LOAD_U32(call+12);
      const auto id = REX_LOAD_U32(args);
      if (id == kUpdateSetting) {  // the Check for Updates card
        outrun::update_prompt::Show();
        return;
      }
      if (id == kSetupSetting) {  // the Setup card
        outrun::update_prompt::ShowSetup();
        return;
      }
      if (id >= kFirstSetting && id < kFirstSetting+SettingsValues{}.size()) {
        const auto index = id-kFirstSetting;
        constexpr uint32_t limits[] = {1, 2, 1, 3, 2, 1, 1, 1, 4, 3, 2, 2, 2, 3};
        const auto value = std::min(REX_LOAD_U32(args+4), limits[index]);
        const auto commit = REX_LOAD_U32(args+8) != 0;
        Preview(index, value);
        if (commit) session.Stage(index, value);
        REXLOG_DEBUG("PC menu: {} {} = {}", commit ? "stage" : "preview", id, value);
        return;
      }
    }
  }
  __imp__sub_82193418(ctx, base);
}

REX_HOOK_RAW(sub_8223B648) {
  using namespace outrun::pc_menu;
  {
    std::lock_guard lock(mutex);
    if (installed) {
      std::string error;
      // Render Scale, Aspect Ratio and Graphics API apply at the next launch.
      const bool restart = session.Changed(1) || session.Changed(5) || session.Changed(7);
      SettingsValues before{};
      for (size_t i = 0; i < before.size(); ++i) before[i] = session.Read(i);
      if (!session.Accept(settingsPath, error)) {
        for (size_t i = 0; i < SettingsValues{}.size(); ++i) Preview(i, session.Read(i));
        REXLOG_ERROR("PC settings save failed; restored previous values: {}", error);
        window->app_context().CallInUIThread([] {
          rex::ShowSimpleMessageBox(rex::SimpleMessageBoxType::Error,
              "PC settings could not be saved. Previous PC settings have been restored.");
        });
      } else {
        SettingsValues after{};
        for (size_t i = 0; i < after.size(); ++i) after[i] = session.Read(i);
        outrun::player_log::OnSettingsAccepted(before, after);
        if (restart) outrun::restart_prompt::Show();
      }
    }
  }
  __imp__sub_8223B648(ctx, base);
}
