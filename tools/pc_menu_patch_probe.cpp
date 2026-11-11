#include "pc_menu_patch.h"
#include "multiplayer_menu_patch.h"
#include "frontend_patch.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) { std::cerr << "pc_menu_patch_probe <original-options-script> <new-output> [preserve-saves]\n"; return 2; }
  try {
    if (std::filesystem::exists(argv[2])) throw std::runtime_error("Output already exists");
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) throw std::runtime_error("Input could not be opened");
    const std::vector<uint8_t> original{std::istreambuf_iterator<char>(in), {}};
    const auto patched = original.size()==outrun::frontend::kMainMenuSize ?
        outrun::frontend::BuildMainMenuScript(original) : original.size()==outrun::frontend::kPauseSize ?
        outrun::frontend::BuildPauseScript(original) : original.size()==outrun::frontend::kLeaderboardSize ?
        outrun::frontend::BuildLeaderboardScript(original) : original.size()==outrun::frontend::kAutoloadSize ?
        outrun::frontend::BuildAutoloadScript(original,argc==3) : original.size()==outrun::multiplayer::kLobbySize ?
        outrun::multiplayer::BuildLobbyScript(original) : original.size()==outrun::multiplayer::kScriptSize ?
        outrun::multiplayer::BuildMenuScript(original) : outrun::pc_menu::BuildOptionsScript(original).bytes;
    std::ofstream out(argv[2], std::ios::binary);
    out.write(reinterpret_cast<const char*>(patched.data()), patched.size());
    out.close();
    if (!out) throw std::runtime_error("Output write failed");
    std::cout << "Patched private copy: " << original.size() << " -> " << patched.size() << " bytes\n";
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
