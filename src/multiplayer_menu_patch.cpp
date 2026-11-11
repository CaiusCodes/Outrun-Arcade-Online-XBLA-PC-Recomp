#include "multiplayer_menu_patch.h"
#include <rex/crypto/sha256.h>
#include <array>
#include <stdexcept>
#include <string_view>

namespace outrun::multiplayer {
namespace {
uint32_t Read(std::span<const uint8_t> b, size_t p) {
  return uint32_t(b[p])<<24 | uint32_t(b[p+1])<<16 | uint32_t(b[p+2])<<8 | b[p+3];
}
void Write(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  for (int i=3;i>=0;--i) { b[p+i]=uint8_t(v); v>>=8; }
}
struct Edit { size_t offset; std::vector<uint8_t> bytes; };
// Branch metadata from the independently decoded original hash below. No local
// script calls occur in this script. Tests compare every original instruction.
constexpr std::pair<size_t,size_t> branches[] = {
  {0xCE,0x564},{0xDD,0x313},{0xEE,0x312},{0xF4,0x308},{0x121,0x1A0},
  {0x12E,0x146},{0x19D,0x2C9},{0x1A8,0x1B7},{0x1B4,0x2C9},{0x1BF,0x1C9},
  {0x1C6,0x2C9},{0x1D1,0x1FC},{0x1F9,0x2C9},{0x204,0x2C1},{0x24B,0x266},
  {0x270,0x2A3},{0x2A0,0x2BE},{0x2BE,0x2C9},{0x2C6,0x2C9},{0x2EC,0x306},
  {0x30F,0xE5},{0x317,0x55A},{0x335,0x359},{0x356,0x553},{0x36E,0x392},
  {0x38F,0x553},{0x398,0x50B},{0x3B6,0x4C8},{0x3DA,0x4C5},{0x3ED,0x41D},
  {0x41A,0x4C5},{0x427,0x443},{0x440,0x4BB},{0x44E,0x4BB},{0x4B8,0x4C5},
  {0x4C2,0x3D8},{0x4C5,0x507},{0x4D0,0x507},{0x508,0x553},{0x511,0x553},
  {0x53D,0x553},{0x555,0x558},{0x561,0xCC}
};
}
std::vector<uint8_t> BuildLobbyScript(std::span<const uint8_t> original) {
  if (original.size()!=kLobbySize || rex::crypto::sha256(std::string_view(
      reinterpret_cast<const char*>(original.data()), original.size())) !=
      "3650e81ac5f031dd9637e44c5e710f2f68704605c86f577a87bf97c65698c894")
    throw std::invalid_argument("Unrecognized lobby script; original lobby retained");
  std::vector<uint8_t> result(original.begin(),original.end());
  // Constant 14 is the footer descriptor array: Select, Back, View Gamer Card.
  // Its third row includes both the text and the X icon. Retain the first two
  // rows; unused trailing bytes stay in place, preserving every relative offset.
  Write(result,0x2AEC,2);
  Write(result,0x2AF0,2);
  // Original action condition: (input & 0x400) && player_count > 0.
  // A zero mask keeps native61 (XamShowGamerCardUI) unreachable in this lobby.
  result[0xCA7]=0;
  return result;
}
std::vector<uint8_t> BuildMenuScript(std::span<const uint8_t> original) {
  if (original.size()!=kScriptSize || rex::crypto::sha256(std::string_view(
      reinterpret_cast<const char*>(original.data()), original.size())) !=
      "002aa285dfd009c0c064b1b3fe09585f0d3af779a947ec44bbff424818601b94")
    throw std::invalid_argument("Unrecognized multiplayer script; original menu retained");
  const std::array<Edit,4> edits{{
    {0x8A,{2,0xFE,0x50,0x43,0x20, 2,0x9C,0xA6,0x63,0xAB, 0x22,8,0,0,0,0,2}},
    {0x9B,{2,0xFE,0x50,0x43,0x20}},
    {0xAC,{2,0xFE,0x50,0x43,0x21}},
    // if selected tab == 2: call native66(0x7F01), discard result, join the
    // original per-frame cleanup. Never index the original two-action array.
    {0x39B,{0x57,2,0x51,0,0x23,0x51,2,0x4A,0x14,0,14,
            0x52,0x7F,1,0x12,1,0,66,0x18,0x51,0,0x18,0x15,0,0}}
  }};
  auto map = [&](size_t offset, bool before=false) {
    size_t result=offset;
    for (const auto& edit:edits)
      if (offset>edit.offset || (!before && offset==edit.offset)) result+=edit.bytes.size();
    return result;
  };
  std::vector<uint8_t> result;
  size_t cursor=0;
  for (const auto& edit:edits) {
    result.insert(result.end(),original.begin()+cursor,original.begin()+edit.offset);
    result.insert(result.end(),edit.bytes.begin(),edit.bytes.end());
    cursor=edit.offset;
  }
  result.insert(result.end(),original.begin()+cursor,original.end());
  const auto delta=uint32_t(result.size()-original.size());
  if (delta%4) throw std::logic_error("Multiplayer script alignment changed");
  auto branch = [&](size_t source,size_t target) {
    const auto displacement=int64_t(target)-int64_t(source+3);
    if (displacement < -32768 || displacement > 32767) throw std::logic_error("Branch out of range");
    result[source+1]=uint8_t(uint16_t(displacement)>>8);
    result[source+2]=uint8_t(displacement);
  };
  for (const auto [source,target]:branches) branch(map(source),map(target,true));
  branch(map(0x39B,true)+22,map(0x553,true));
  for (size_t p:{0x8A,0x9B,0xAC}) Write(result,map(p)+3,3);
  Write(result,8,uint32_t(result.size()));
  Write(result,16,Read(original,16)+delta);
  for (size_t p:{20,24,28,44,52}) Write(result,p,uint32_t(map(Read(original,p))));
  for (uint32_t i=0;i<Read(original,48);++i) {
    const auto p=Read(original,44)+i*4;
    Write(result,map(p),uint32_t(map(Read(original,p))));
  }
  return result;
}
}
