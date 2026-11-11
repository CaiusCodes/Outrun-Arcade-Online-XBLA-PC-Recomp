#include "pc_settings_store.h"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace outrun::pc_menu;
void Check(bool okay, const char* message) { if (!okay) throw std::runtime_error(message); }
int main() {
  const auto directory = std::filesystem::current_path()/
      ("pc-settings-test-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
  try {
    Check(std::filesystem::create_directory(directory), "Test directory exists");
    const auto path = directory/"pc-settings.toml";
    Check(!LoadSettings(path), "Missing file must use defaults");
    SaveSettings(path, {0,0,0});
    SettingsSession session(*LoadSettings(path));
    // Native Cancel reads the previously committed value, then calls setter(commit=1).
    // Staging alone must never change that getter or disk state.
    session.Stage(0,1); session.Stage(2,1);
    Check(session.Read(0)==0 && session.Read(2)==0, "Preview/staging changed committed values");
    Check(session.Changed(0) && !session.Changed(1), "Changed() must report only staged differences (restart prompt)");
    for (size_t i=0; i<SettingsValues{}.size(); ++i) session.Stage(i,session.Read(i));
    std::string error;
    Check(session.Accept(path,error) && *LoadSettings(path)==SettingsValues{0,0,0}, "Cancel persisted changes");
    session.Stage(0,1); session.Stage(1,2); session.Stage(2,1);
    Check(session.Accept(path,error), "Accept failed");
    Check(*LoadSettings(path)==SettingsValues{1,2,1}, "Restart reload mismatch");
    // Previous four-row prototype files remain readable, but VSync is discarded.
    for (const char* legacy : {"true", "false"}) {
      { std::ofstream out(path); out << "fullscreen = true\nresolution_scale = 3\nshow_fps = true\npresentation_vsync = " << legacy << '\n'; }
      auto expected=DefaultSettings();expected[0]=1;expected[1]=2;expected[2]=1;expected[3]=3;
      Check(*LoadSettings(path)==expected, "Legacy VSync changed retained settings");
    }
    {
      SettingsSession filters(SettingsValues{0,0,0,3,0});
      SaveSettings(path,{0,0,0,3,0});
      filters.Stage(4,2);
      Check(filters.Read(4)==0, "Filter preview changed committed value");
      filters.Stage(4,filters.Read(4));
      Check(filters.Accept(path,error) && (*LoadSettings(path))[4]==0, "Filter Cancel persisted preview");
      filters.Stage(4,2);
      Check(filters.Accept(path,error) && (*LoadSettings(path))[4]==2, "Filter Accept did not persist");
    }
    for(unsigned effect=0;effect<3;++effect) {
      SaveSettings(path, {0,0,0,3,effect});
      Check(*LoadSettings(path)==SettingsValues{0,0,0,3,effect}, "Filter persistence changed unrelated settings");
    }
    {
      SettingsSession aspect(SettingsValues{0,0,0,3,2,0});SaveSettings(path,{0,0,0,3,2,0});
      aspect.Stage(5,1);Check(aspect.Read(5)==0,"Aspect preview changed committed mode");
      aspect.Stage(5,0);Check(aspect.Accept(path,error) && (*LoadSettings(path))[5]==0,"Aspect Cancel failed");
      aspect.Stage(5,1);Check(aspect.Accept(path,error) && *LoadSettings(path)==SettingsValues{0,0,0,3,2,1},"Aspect Accept/reload changed other settings");
    }
    {
      // Picture and filtering rows: saved as the renderer's and SDK's cvars, read back.
      SettingsValues v{};v[8]=4;v[9]=3;v[10]=2;v[11]=0;v[12]=1;SaveSettings(path,v);
      Check(*LoadSettings(path)==v,"Filtering/AA/colour reload");
      { std::ifstream in(path); std::string saved((std::istreambuf_iterator<char>(in)), {});
        Check(saved.find("gpu_native_msaa = 8")!=std::string::npos && saved.find("color_saturation = 1.3")!=std::string::npos &&
              saved.find("gpu_native_texture_filtering = 4")!=std::string::npos,"Filtering/AA/colour not saved as cvars"); }
      { std::ofstream out(path); out << "fullscreen = false\nresolution_scale = 1\nshow_fps = false\ncolor_contrast = 1.12\ngpu_native_msaa = 3\n"; }
      bool bad=false;try{LoadSettings(path);}catch(...){bad=true;}
      Check(bad,"Invalid sample count accepted");
      { std::ofstream out(path); out << "fullscreen = false\nresolution_scale = 1\nshow_fps = false\ncolor_contrast = 1.12\n"; }
      Check((*LoadSettings(path))[11]==2 && (*LoadSettings(path))[9]==2,"Hand-edited colour or default AA wrong");
    }
    {
      // Renderer and graphics API: next launch, saved as the cvars they set.
      SettingsSession next(SettingsValues{0,0,0,3,2,0,0,0});SaveSettings(path,{0,0,0,3,2,0,0,0});
      next.Stage(7,1);
      Check(next.Read(7)==0,"API staging changed committed value");
      Check(next.Accept(path,error) && *LoadSettings(path)==SettingsValues{0,0,0,3,2,0,0,1},"API Accept/reload");
      { std::ifstream in(path); std::string saved((std::istreambuf_iterator<char>(in)), {});
        // The renderer stays an outrun.toml setting: never written here.
        Check(saved.find("gpu_native =")==std::string::npos && saved.find("gpu = \"vulkan\"")!=std::string::npos,"API not saved, or renderer saved"); }
      { std::ofstream out(path); out << "fullscreen = false\nresolution_scale = 1\nshow_fps = false\ngpu_native = false\n"; }
      Check((*LoadSettings(path))[6]==1,"Older file's renderer not read");
      { std::ofstream out(path); out << "fullscreen = false\nresolution_scale = 1\nshow_fps = false\ngpu = \"metal\"\n"; }
      bool rejected=false;try{LoadSettings(path);}catch(...){rejected=true;}
      Check(rejected,"Unknown graphics API accepted");
    }
    SaveSettings(path, {0,0,0,3});
    Check(*LoadSettings(path)==SettingsValues{0,0,0,3}, "Output size changed render scale");
    SaveSettings(path, {1,2,1});
    { std::ifstream in(path); std::string saved((std::istreambuf_iterator<char>(in)), {});
      Check(saved.find("vsync")==std::string::npos, "Removed option was written to disk"); }
    // A reader denying replacement reproduces a real save failure on Windows.
    HANDLE locked = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "Cannot lock saved settings");
    session.Stage(0,0);
    const auto accepted = session.Accept(path,error);
    CloseHandle(locked);
    Check(!accepted && !error.empty(), "Locked write reported success");
    Check(session.Read(0)==1 && *LoadSettings(path)==SettingsValues{1,2,1}, "Failed save destroyed committed settings");
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
      Check(entry.path()==path, "Staging file leaked");
    }
    for (const char* malformed : {"fullscreen = true\n", "fullscreen = true\nresolution_scale = 1\nshow_fps = false\npresentation_vsync = invalid\n",
          "fullscreen = true\nresolution_scale = 1\nshow_fps = false\npresentation_vsync = true\npresentation_vsync = false\n", "fullscreen = true\nresolution_scale = 9\nshow_fps = false\n",
          "fullscreen = true\nresolution_scale = 1\nshow_fps = false\nfullscreen = false\n"}) {
      { std::ofstream out(path); out << malformed; }
      bool rejected = false;
      try { LoadSettings(path); } catch (...) { rejected = true; }
      Check(rejected, "Malformed settings accepted");
    }
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
    std::cout << "PASS: staged/committed separation, Cancel, atomic Accept/reload, locked-file preservation, legacy VSync migration, renderer/API, malformed input\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << " (evidence: " << directory << ")\n"; return 1; }
}
