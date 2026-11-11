#include "frontend_patch.h"
#include <rex/crypto/sha256.h>
#include <stdexcept>
#include <string_view>
namespace outrun::frontend {
std::vector<uint8_t> BuildMainMenuScript(std::span<const uint8_t> original) {
  if(original.size()!=kMainMenuSize || rex::crypto::sha256(std::string_view(
      reinterpret_cast<const char*>(original.data()),original.size())) !=
      "9b48e8ba561300797474149c998210700239d29f4448c378d1f665b4471da672")
    throw std::invalid_argument("Unrecognized main menu script; original retained");
  std::vector<uint8_t> result(original.begin(),original.end());
  auto skip=[&](size_t begin,size_t end) {
    // Skip complete pushes/array construction without moving any VM offsets.
    result[begin]=0x15;
    const auto displacement=end-begin-3;
    result[begin+1]=uint8_t(displacement>>8);result[begin+2]=uint8_t(displacement);
    for(size_t p=begin+3;p<end;++p)result[p]=0x18;
  };
  // Trial/full variants each build matching tab, heading and description arrays.
  skip(0x93,0xA6);skip(0x16E,0x17F);
  for(const size_t begin:{0xDEu,0x108u,0x1A6u,0x1C6u})skip(begin,begin+5);
  for(const size_t count:{0xCEu,0xF8u,0x122u,0x196u,0x1B6u,0x1D6u})--result[count];
  // The visible index also selects a stable action ID and script from these
  // trial/full constant arrays. Compact both, retaining Help's original ID4.
  for(const size_t rows:{0x8C0u,0x900u,0x940u,0x978u}) {
    const unsigned count=result[rows+3];
    for(unsigned i=3;i+1<count;++i)
      for(unsigned j=0;j<8;++j)result[rows+16+i*8+j]=result[rows+16+(i+1)*8+j];
    --result[rows+3];--result[rows+7];
  }
  // The footer constant (Select, and B "Return to Game Library", TRC text
  // 0xC99563C8) stays whole: B asks the game's own Yes/No box (message_boxes,
  // TRC 0xD5BAC512) and Yes starts the title's return thread, 0x82193BE0.
  // exit_game.cpp relabels both "Exit Game" and makes Yes close the game.
  if(result[0x9B3]!=2 || result[0x9B7]!=2)throw std::logic_error("Main menu footer moved");
  return result;
}
std::vector<uint8_t> BuildPauseScript(std::span<const uint8_t> original) {
  if(original.size()!=kPauseSize || rex::crypto::sha256(std::string_view(
      reinterpret_cast<const char*>(original.data()),original.size())) !=
      "360eb03d4bf446724013185e2f9164051cfb42de309e575d40bb643a5032033c")
    throw std::invalid_argument("Unrecognized pause script; original retained");
  std::vector<uint8_t> result(original.begin(),original.end());
  auto get=[&](size_t p){return uint32_t(result[p])<<24|uint32_t(result[p+1])<<16|uint32_t(result[p+2])<<8|result[p+3];};
  auto put=[&](size_t p,uint32_t v){for(int i=3;i>=0;--i){result[p+i]=uint8_t(v);v>>=8;}};
  auto array=[&](size_t p,unsigned i){const size_t entry=p+16+i*8;return entry+get(entry);};
  const size_t menus=0x850+get(0x850);
  for(unsigned mode=0;mode<3;++mode) {
    const auto pane=array(menus,mode),labels=array(pane,0),actions=array(pane,1);
    const auto count=get(actions);
    unsigned index=0;while(index<count && get(actions+16+index*8)!=5)++index;
    if(index==count || get(labels)!=count)throw std::logic_error("Pause row mapping changed");
    // Remove paired labels/action IDs. Exit retains ID7; all bytecode/branch
    // offsets and the settings subpane stay intact. No original asset writes.
    for(const auto rows:{labels,actions}) {
      for(unsigned i=index;i+1<count;++i)
        for(unsigned j=0;j<8;++j)result[rows+16+i*8+j]=result[rows+16+(i+1)*8+j];
      put(rows,count-1);put(rows+4,count-1);
    }
  }
  return result;
}
std::vector<uint8_t> BuildLeaderboardScript(std::span<const uint8_t> original) {
  if (original.size()!=kLeaderboardSize || rex::crypto::sha256(std::string_view(
      reinterpret_cast<const char*>(original.data()),original.size())) !=
      "c68922429a14656d541340839baa18be7fa109ea0626865b8a4bf84f016a10b1")
    throw std::invalid_argument("Unrecognized leaderboard script; original retained");
  std::vector<uint8_t> result(original.begin(),original.end());
  auto put=[&](size_t p,uint32_t v) {
    for(int i=3;i>=0;--i){result[p+i]=uint8_t(v);v>>=8;}
  };
  // Filter state 0 (rank/top page) is already the default at 0xC17. The
  // runtime serves local records for it. State 1 centers on the current
  // player's score. Remove state 2, the unsupported Xbox LIVE friends query.
  // Constant 13 contains the *next* filter's footer label, so entry 1 must
  // now point to GLOBAL (renamed Local Scores), not FRIENDS.
  put(0x1CD0,2); put(0x1CD4,2);
  put(0x1CE8,uint32_t(0x1C90-0x1CE8));
  // Both footer variants are built at runtime: Select/Back/X/Y and Back/X/Y.
  // Skip construction of the complete X descriptor (text and icon together),
  // shrink each outer array, and follow the Y row's new index when updating
  // the filter label. Keep all original offsets and navigation instructions.
  for(const size_t offset : {0x589u,0x5D5u}) {
    result[offset]=0x15; result[offset+1]=0; result[offset+2]=0x13;
    result[offset+3]=0x18; result[offset+4]=0x18;
  }
  result[0x5BB]=3; result[0x607]=2;
  result[0xC24]=2; result[0xC31]=1;
  result[0xEA9]=2; result[0xEB6]=1;
  // The only native61 gamer-card action is guarded by input & 0x400.
  // Zero that mask so X also does nothing when a populated row is selected.
  result[0xE5A]=0;
  // No My Scores (Y) either: the boards are local only (user request). Skip
  // building the Y footer row in both variants (Select/Back and Back), skip
  // the four writes of its filter label, and zero the Y (0x800) input mask.
  auto skip=[&](size_t begin,size_t end) {
    result[begin]=0x15;
    const auto displacement=end-begin-3;
    result[begin+1]=uint8_t(displacement>>8);result[begin+2]=uint8_t(displacement);
    for(size_t p=begin+3;p<end;++p)result[p]=0x18;
  };
  skip(0x59F,0x5B5); skip(0x5EB,0x601);
  result[0x5BB]=2; result[0x607]=1;
  for(const size_t label:{0xC1Cu,0xC29u,0xEA1u,0xEAEu})skip(label,label+13);
  if(result[0xE7A]!=0x08 || result[0xE7B]!=0)throw std::logic_error("Leaderboard Y mask moved");
  result[0xE7A]=0;
  return result;
}
bool CanCreateFirstGameData(const std::filesystem::path& userData) {
  try {
    if(!std::filesystem::exists(userData))return true;
    for(const auto& profile:std::filesystem::directory_iterator(userData)) {
      if(!profile.is_directory())continue;
      const auto title=profile.path()/"58410968";
      if(std::filesystem::exists(title/"00000001"/"SaveGame") ||
         std::filesystem::exists(title/"Headers"/"00000001"/"SaveGame.header"))return false;
    }
    return true;
  }catch(const std::filesystem::filesystem_error&) { return false; }
}
std::vector<uint8_t> BuildAutoloadScript(std::span<const uint8_t> original, bool createFirstData) {
  if (original.size()!=kAutoloadSize || rex::crypto::sha256(std::string_view(
      reinterpret_cast<const char*>(original.data()),original.size())) !=
      "d1e98944783edf11ff61f17d32f1b68ab74e994fd220b898a3018931121b2d49")
    throw std::invalid_argument("Unrecognized autoload script; original retained");
  std::vector<uint8_t> result(original.begin(),original.end());
  // LoadDataSuccess: skip the informational dialog call (0x3DF892DF) and
  // its discarded return value. Continue at the original save-enable natives
  // 274, 278, 301. Error dialogs and all loading/saving code stay unchanged.
  // Branch occupies the old five-byte push; two harmless unreachable pops
  // keep a complete instruction stream without relocating any offsets.
  result[0xD5A]=0x15; result[0xD5B]=0; result[0xD5C]=0x0D;
  result[0xD5D]=0x18; result[0xD5E]=0x18;
  // Dialog_AskCreateSave is the first-run, no-data path. Substitute its
  // original choice zero (Create new game data) for the dialog's result.
  // Keep the original CreateData/save flow and all error/recovery dialogs.
  // Reuse the five-byte argument push for the result, then branch across
  // only the ten-byte local call. No offsets or other branches move.
  if(createFirstData) {
    // The same no-data prompt is also reached through LoadDataCheckError.
    // Preserve that recovery dialog whenever title save content already exists.
    for(const size_t start:{0xB9Bu,0xEFAu}) {
      for(size_t p=start+1;p<=start+4;++p)result[p]=0;
      result[start+5]=0x15;result[start+6]=0;result[start+7]=7;
      for(size_t p=start+8;p<start+15;++p)result[p]=0x18;
    }
  }
  return result;
}
}
