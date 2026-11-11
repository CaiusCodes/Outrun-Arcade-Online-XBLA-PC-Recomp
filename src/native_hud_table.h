// Settings > HUD: the race HUD's textures (included inside native_gpu.cpp's
// namespace hud). Found with gpu_native_log_hud in background races; cells in
// the texture's own texels as stored (the HUD samples them upside down).
//   kRedrawn: replaced by resources\hud\<hash>.png in HD (at the enlarged size)
//   kRace:    drawn only in races: cleared when the HUD is Off
//   kLogo:    a light emblem on a dark disc, left enlarged in HD but re-cut crisp
#pragma once

enum : uint8_t { kRedrawn = 1, kRace = 2, kLogo = 4 };
struct Rect { uint32_t x, y, w, h; uint8_t flags; };
struct Texture {
  uint64_t hash;
  const char* name;
  std::vector<Rect> rects;
};

#include "native_hud_cells.h"

std::vector<Rect> cells_of(uint64_t hash) {
  for (const auto& [h, rects] : kCells)
    if (h == hash) return rects;
  return {};
}

const std::vector<Texture> kTextures = {
    {0x1EF05844465FA269ull, "speedometer dial 128x128", cells_of(0x1EF05844465FA269ull)},
    {0x97FFAFBB9F525669ull, "speedometer needle 16x128", cells_of(0x97FFAFBB9F525669ull)},
    {0x92F50FBE36F99AC3ull, "HUD sheet 512x512: time and speed digits, km/h, rivals, cars", cells_of(0x92F50FBE36F99AC3ull)},
    {0x4D23944303D91F91ull, "font sheet 512x512: total time, score and gear digits", cells_of(0x4D23944303D91F91ull)},
    {0xFBB0D9D26BAA003Aull, "label sheet 1024x512: Time, Total Time, Score, gear strip, stage words", cells_of(0xFBB0D9D26BAA003Aull)},
    {0x21D36C6DC75459ACull, "timer bars 512x512", cells_of(0x21D36C6DC75459ACull)},
    {0x24F1DD45F35912CDull, "race 1024x1024 (F7504000)", cells_of(0x24F1DD45F35912CDull)},
    {0x3597BE2E914C3A66ull, "race 1024x1024 (F7404000)", cells_of(0x3597BE2E914C3A66ull)},
    {0x317EE6F69584CF9Eull, "race 1024x512 (F7146000)", cells_of(0x317EE6F69584CF9Eull)},
    {0xD9BED32D7F70BE13ull, "race 1024x512 (F7604000)", cells_of(0xD9BED32D7F70BE13ull)},
    {0xED74516E980E98A6ull, "race 512x512 (F7086000)", cells_of(0xED74516E980E98A6ull)},
    {0xF5E5A93D51D1E913ull, "race 512x512 (F6DF5000)", cells_of(0xF5E5A93D51D1E913ull)},
    {0xA3BF5397FEC8789Aull, "results 512x256 (F6DD5000)", cells_of(0xA3BF5397FEC8789Aull)},
    {0x9828A949A96ED9B1ull, "results 512x512 (F7294000)", cells_of(0x9828A949A96ED9B1ull)},
    {0xB3A70A6657D50B46ull, "results 256x256 (F72B4000)", cells_of(0xB3A70A6657D50B46ull)},
    {0x27C9CF40AB32DBA9ull, "results 128x64 (F72C8000)", cells_of(0x27C9CF40AB32DBA9ull)},
    {0xE4816E8000B7E709ull, "results 128x128 (F72C4000)", cells_of(0xE4816E8000B7E709ull)},
    {0xDD6650DD378A8E2Full, "results 128x32 (F6EA3000)", cells_of(0xDD6650DD378A8E2Full)},
};
