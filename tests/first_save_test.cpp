#include "frontend_patch.h"
#include <chrono>
#include <fstream>
#include <iostream>
int main() {
  const auto root=std::filesystem::current_path()/
      ("first-save-fixture-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  const auto title=root/"user-id"/"58410968";
  using outrun::frontend::CanCreateFirstGameData;
  if(!CanCreateFirstGameData(root))return 1;
  std::filesystem::create_directories(root/"58410968"/"profile");
  if(!CanCreateFirstGameData(root))return 2;
  std::filesystem::create_directories(title/"00000001"/"SaveGame");
  // Even an empty or incomplete save directory is existing content. Do not
  // automatically replace it; retain the game's recovery dialog.
  if(CanCreateFirstGameData(root))return 3;
  std::filesystem::rename(title/"00000001"/"SaveGame",title/"00000001"/"fixture-backup");
  std::filesystem::create_directories(title/"Headers"/"00000001");
  std::ofstream(title/"Headers"/"00000001"/"SaveGame.header")<<"invalid header fixture";
  if(CanCreateFirstGameData(root))return 4;
  const auto unreadable=root/"not-a-directory";
  std::ofstream(unreadable)<<"fixture";
  if(CanCreateFirstGameData(unreadable))return 5;
  std::cout<<"PASS: new profile allowed; existing/incomplete content, metadata and inspection failure preserved\n";
}
