#include "pc_menu_patch.h"
#include <rex/crypto/sha256.h>
#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <stdexcept>

namespace outrun::pc_menu {
namespace {
uint32_t Read(std::span<const uint8_t> b, size_t p) {
  return (uint32_t(b[p]) << 24) | (uint32_t(b[p+1]) << 16) |
         (uint32_t(b[p+2]) << 8) | b[p+3];
}
void Write(std::vector<uint8_t>& b, size_t p, uint32_t v) {
  for (int i = 3; i >= 0; --i) { b[p+i] = uint8_t(v); v >>= 8; }
}
void Word(std::vector<uint8_t>& b, uint32_t v) {
  const auto p = b.size(); b.resize(p+4); Write(b, p, v);
}
void Array(std::vector<uint8_t>& b, uint32_t count) {
  b.insert(b.end(), {0x22, 8, 0}); Word(b, count);
}
void Spinner(std::vector<uint8_t>& b, uint16_t setting, uint32_t label,
             std::initializer_list<uint32_t> choices) {
  b.insert(b.end(), {0x51, 0, 0x52, uint8_t(setting >> 8), uint8_t(setting), 2});
  Word(b, label);
  b.insert(b.end(), {0x51, 0});
  for (auto text : choices) { b.push_back(2); Word(b, text); }
  Array(b, uint32_t(choices.size())); Array(b, 5);
}

// Operand bytes per SMOS opcode, from the VM at 0x8218BF20 (the same table as
// tools/smos_bytecode.py); opcodes 4 and 0x2B carry a zero-terminated string.
int OperandBytes(uint8_t op) {
  static constexpr int8_t widths[0x5E] = {
      0, 4, 4, 4, -1, 2, 4, 1, 2, 2, 4, 1, 2, 1, 4, 1,   // 00-0F
      2, 0, 3, 2, 2, 2, 1, 0, 0, 1, 5, 0, 5, 0, 0, 0,    // 10-1F
      0, 0, 6, 0, 3, 0, 4, 2, 3, 2, 0, -1, 2, 4, 5, 4,   // 20-2F
      0, 0, 7, 4, 9, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 30-3F
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    // 40-4F
      0, 1, 2, 1, 2, 1, 2, 1, 1, 0, 0, 1, 5, 0};         // 50-5D
  return op < sizeof(widths) ? widths[op] : -2;
}

struct Instruction { size_t offset, size; uint8_t op; };

// Every instruction of the code section, as script offsets.
std::vector<Instruction> Decode(std::span<const uint8_t> script) {
  const size_t begin = Read(script, 12), end = begin+Read(script, 16);
  if (begin < 80 || end > script.size()) throw std::invalid_argument("SMOS code section out of range");
  std::vector<Instruction> code;
  for (size_t p = begin; p < end;) {
    const uint8_t op = script[p];
    const int width = OperandBytes(op);
    size_t size = 1;
    if (width == -1) {
      while (p+size < end && script[p+size]) ++size;
      ++size;
    } else if (width < 0) {
      throw std::invalid_argument("Unknown SMOS opcode");
    } else {
      size += size_t(width);
    }
    if (p+size > end) throw std::invalid_argument("Truncated SMOS instruction");
    code.push_back({p, size, op});
    p += size;
  }
  return code;
}

// Code placed before original instructions, optionally in place of the
// `removed` bytes there (whole instructions nothing jumps into). Branches and
// local calls keep their targets (one aimed at an insertion point lands on the
// inserted code, which then runs into the original instruction); the tables
// after the code move with it, as the loader's relative pointers expect.
struct Insertion { size_t at; std::vector<uint8_t> bytes; size_t removed = 0; };

class Relocation {
 public:
  Relocation(std::span<const uint8_t> script, std::vector<Insertion> insertions)
      : script_(script), insertions_(std::move(insertions)) {
    std::sort(insertions_.begin(), insertions_.end(), [](const auto& a, const auto& b) { return a.at < b.at; });
  }
  // New offset of an original position: of the instruction itself, or of the
  // code that runs first when control arrives there (land = true).
  size_t Map(size_t offset, bool land = false) const {
    int64_t moved = int64_t(offset);
    for (const auto& insertion : insertions_) {
      if (offset > insertion.at && offset < insertion.at+insertion.removed)
        throw std::logic_error("Position inside replaced code");
      if (insertion.at < offset || (!land && !insertion.removed && insertion.at == offset))
        moved += int64_t(insertion.bytes.size())-int64_t(insertion.removed);
    }
    return size_t(moved);
  }
  size_t Added() const {
    int64_t total = 0;
    for (const auto& insertion : insertions_) total += int64_t(insertion.bytes.size())-int64_t(insertion.removed);
    if (total < 0) throw std::logic_error("SMOS code shrank");
    return size_t(total);
  }
  std::vector<uint8_t> Build() const {
    const auto code = Decode(script_);
    const size_t codeEnd = Read(script_, 12)+Read(script_, 16);
    auto boundary = [&](size_t at) {
      return std::any_of(code.begin(), code.end(), [&](const auto& i) { return i.offset == at; });
    };
    for (const auto& insertion : insertions_) {
      if (!boundary(insertion.at) || !boundary(insertion.at+insertion.removed))
        throw std::logic_error("Insertion is not at an instruction");
    }
    auto replaced = [&](size_t offset) {
      return std::any_of(insertions_.begin(), insertions_.end(),
                         [&](const auto& i) { return offset >= i.at && offset < i.at+i.removed; });
    };
    const size_t added = Added();
    if (added % 4) throw std::logic_error("SMOS table alignment changed");
    std::vector<uint8_t> result;
    size_t cursor = 0;
    for (const auto& insertion : insertions_) {
      result.insert(result.end(), script_.begin()+cursor, script_.begin()+insertion.at);
      result.insert(result.end(), insertion.bytes.begin(), insertion.bytes.end());
      cursor = insertion.at+insertion.removed;
    }
    result.insert(result.end(), script_.begin()+cursor, script_.end());
    // Relative branches (13-15: 16-bit, from the instruction's end) and local
    // calls (34: 32-bit at operand byte 1, from the end).
    for (const auto& i : code) {
      if (replaced(i.offset)) continue;
      if (i.op == 0x13 || i.op == 0x14 || i.op == 0x15) {
        const auto old = int16_t(uint16_t(script_[i.offset+1] << 8 | script_[i.offset+2]));
        const auto target = int64_t(i.offset+i.size)+old;
        const auto source = Map(i.offset);
        const auto displacement = int64_t(Map(size_t(target), true))-int64_t(source+i.size);
        if (displacement < -32768 || displacement > 32767) throw std::logic_error("Branch out of range");
        result[source+1] = uint8_t(uint16_t(displacement) >> 8);
        result[source+2] = uint8_t(displacement);
      } else if (i.op == 0x34) {
        const auto old = int32_t(Read(script_, i.offset+2));
        const auto target = int64_t(i.offset+i.size)+old;
        const auto source = Map(i.offset);
        Write(result, source+2, uint32_t(int64_t(Map(size_t(target), true))-int64_t(source+i.size)));
      }
    }
    // Header: script and code sizes, then every table offset past the code,
    // and the debug table's own entries.
    Write(result, 8, Read(script_, 8)+uint32_t(added));
    Write(result, 16, Read(script_, 16)+uint32_t(added));
    for (size_t field : {20, 24, 28, 44, 52}) {
      const auto value = Read(script_, field);
      if (value >= codeEnd) Write(result, field, value+uint32_t(added));
    }
    const auto debugTable = Read(script_, 44);
    for (uint32_t i = 0; i < Read(script_, 48); ++i) {
      const auto p = debugTable+i*4;
      const auto value = Read(script_, p);
      Write(result, p+added, value >= codeEnd ? value+uint32_t(added) : value);
    }
    return result;
  }

 private:
  std::span<const uint8_t> script_;
  std::vector<Insertion> insertions_;
};

// Original Options script offsets (decoded from the hash-checked original;
// tools/Test-PcMenuPatch.py checks them).
// Card menu initializer: the four [label, icon] pairs (17 bytes each, How to
// play, Controls, Settings, Audio Options) then their op_22; the four
// headings and four descriptions (5-byte pushes) then theirs.
constexpr size_t kCardPairs = 0x63, kCardsArray = 0xA7;
constexpr size_t kHeadings = 0xAE, kHeadingsArray = 0xC2;
constexpr size_t kDescriptions = 0xC9, kDescriptionsArray = 0xDD;
// Shown order, as original card positions (kGraphicsCard, kUpdateCard = the
// new cards): Settings, Graphics, Audio Options, Controls, How to play (user's
// order), then Check for Updates and Setup.
constexpr int kGraphicsCard = -1, kUpdateCard = -2, kSetupCard = -3;
constexpr int kOrder[] = {2, kGraphicsCard, 3, 1, 0, kUpdateCard, kSetupCard};
constexpr int kCards = int(std::size(kOrder));
// On A: global 0 (the caller's map from card position to section, four long)
// indexed by the chosen card (global 1 field 0) picks How to play / Controls /
// Settings / Audio, compared at 0x598; its value is dropped at kAfterDispatch.
// The shown card is mapped back to its original position first (a table
// pushed before the load of global 1, indexed after it), and Graphics is
// caught before the lookup.
constexpr size_t kCardDispatch = 0x590, kLoadMenu = 0x592, kSectionLookup = 0x597;
constexpr size_t kAfterDispatch = 0x5F8;
constexpr size_t kSettingsRows = 0x23F;  // op_22 of the five Settings rows (global 6)
// The in-race Settings (global 8: Brightness, Speedometer, Display Ghost Cars):
// the pause menu's Help & Options > Settings sends message 0x5B196DA2, whose
// handler (0x3B9) edits global 8 with the same row editor. Its op_22 of three rows.
constexpr size_t kPauseSettingsRows = 0x2F7;
constexpr size_t kRowEditor = 0x607;          // shared row editor; the pane is global 9
constexpr size_t kRowEditorInput = 0xAA0;     // its native 47 (menu input) call
constexpr uint32_t kSettingsIcon = 0x9CA663AB;
constexpr uint32_t kCancelText = 0x21836792, kAcceptText = 0x6D5DA8A8;
constexpr uint32_t kGraphicsText = 0xFE504316, kGraphicsDescription = 0xFE504317;
constexpr uint32_t kUpdateIcon = 0xFA1C6ADA;  // How to play's

std::vector<uint8_t> Push(std::initializer_list<uint32_t> values) {
  std::vector<uint8_t> b;
  for (auto value : values) { b.push_back(2); Word(b, value); }
  return b;
}
}  // namespace

OptionsScript BuildOptionsScript(std::span<const uint8_t> original) {
  if (original.size() != kOriginalSize ||
      rex::crypto::sha256(std::string_view(reinterpret_cast<const char*>(original.data()),
                                         original.size())) !=
          "b54c699a36a396ef665be95c53dc89135b5a8f5f849624f0af859b6c2f1d6921") {
    throw std::invalid_argument("Unrecognized OutRun Options script; native menu left unchanged");
  }
  // The cards, headings and descriptions in the new order, with Graphics.
  std::vector<uint8_t> cards, headings, descriptions;
  int graphicsPosition = 0, updatePosition = 0, setupPosition = 0;
  for (int i = 0; i < kCards; ++i) {
    const int from = kOrder[i];
    if (from < 0) {
      const bool graphics = from == kGraphicsCard, setup = from == kSetupCard;
      (graphics ? graphicsPosition : setup ? setupPosition : updatePosition) = i;
      const uint32_t text = graphics ? kGraphicsText : setup ? kSetupText : kUpdateText;
      auto pair = Push({text, graphics || setup ? kSettingsIcon : kUpdateIcon});
      Array(pair, 2);
      cards.insert(cards.end(), pair.begin(), pair.end());
      auto heading = Push({text}),
           description = Push({graphics ? kGraphicsDescription : setup ? kSetupDescription : kUpdateDescription});
      headings.insert(headings.end(), heading.begin(), heading.end());
      descriptions.insert(descriptions.end(), description.begin(), description.end());
      continue;
    }
    cards.insert(cards.end(), original.begin()+kCardPairs+from*17, original.begin()+kCardPairs+from*17+17);
    headings.insert(headings.end(), original.begin()+kHeadings+from*5, original.begin()+kHeadings+from*5+5);
    descriptions.insert(descriptions.end(), original.begin()+kDescriptions+from*5,
                        original.begin()+kDescriptions+from*5+5);
  }
  // Shown position -> original position, for the section lookup (Graphics
  // never reaches it; its entry is the original Settings, harmlessly).
  std::vector<uint8_t> positions;
  for (int from : kOrder) positions.insert(positions.end(), {0x51, uint8_t(from < 0 ? 2 : from)});
  Array(positions, kCards);
  // On A with Graphics chosen (global 1 is the card menu, its field 0 the
  // chosen card), as the Controls/Settings/Audio cards do: the pane
  // [Cancel, Accept, rows] into global 9, then the shared row editor; then on
  // past the original dispatch and the drop of its value.
  std::vector<uint8_t> pane = Push({kCancelText, kAcceptText});
  // Live settings first, then the ones taken at the next launch.
  Spinner(pane, 1024, 0xFE504300, {0xFE504301, 0xFE504302});
  Spinner(pane, 1027, 0xFE50430A, {0xFE50430B, 0xFE50430C, 0xFE50430D, 0xFE50430E});
  Spinner(pane, 1032, 0xFE504330, {0xFE504308, 0xFE504305, 0xFE504331, 0xFE504332, 0xFE504333});
  Spinner(pane, 1033, 0xFE504334, {0xFE504308, 0xFE504305, 0xFE504331, 0xFE504332});
  Spinner(pane, 1028, 0xFE50430F, {0xFE504310, 0xFE504311, 0xFE504312});
  Spinner(pane, 1026, 0xFE504307, {0xFE504308, 0xFE504309});
  // The native renderer sizes its targets for the render scale at start.
  Spinner(pane, 1025, 0xFE504303, {0xFE504304, 0xFE504305, 0xFE504306});
  Spinner(pane, 1029, 0xFE504313, {0xFE504314, 0xFE504315});
  Spinner(pane, 1031, 0xFE50431B, {0xFE50431C, 0xFE50431D});
  Array(pane, 9);
  Array(pane, 3);
  pane.insert(pane.end(), {0x27, 0, 9, 0x17});  // global 9 = pane; call frame
  const size_t call = pane.size();
  pane.insert(pane.end(), {0x34, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x7D, 0x18});  // editor (line 381); drop its result
  // Word alignment of the tables after the code: balanced push/pops.
  const size_t blockFixed = 2+2+1+2+1+3;  // global 1, push 0, index, push position, ==, branch if false
  // Settings: the picture rows and HUD after the game's five (its op_22 becomes
  // nine, the popup's most).
  std::vector<uint8_t> picture;
  Spinner(picture, 1034, 0xFE504335, {0xFE504308, 0xFE504336, 0xFE504337});
  Spinner(picture, 1035, 0xFE504338, {0xFE504336, 0xFE504339, 0xFE504337});
  Spinner(picture, 1036, 0xFE50433A, {0xFE504339, 0xFE50433B, 0xFE50433C});
  // HUD: Original, Sharp, HD, Off (live: the renderer makes the HUD's textures again).
  Spinner(picture, 1037, 0xFE50433D, {0xFE50433E, 0xFE50433F, 0xFE504340, 0xFE504308});
  // The same HUD row after the in-race Settings' three (pause menu), live too.
  std::vector<uint8_t> pauseRows;
  Spinner(pauseRows, 1037, 0xFE50433D, {0xFE50433E, 0xFE50433F, 0xFE504340, 0xFE504308});
  // On A with Check for Updates chosen: native 64 (the setter, as the row
  // editor calls it: id, value, commit) with the port's own id, which
  // pc_menu.cpp answers by opening the update box (update_prompt.cpp); then on
  // past the dispatch, as above, so the cards stay where they are.
  // Setup works the same way with its own id (pc_menu.cpp asks to close the
  // game and open the launcher's setup screen); its block follows this one.
  auto setterCard = [](int position, uint32_t setting) {
    std::vector<uint8_t> card{0x57, 0x01, 0x51, 0x00, 0x23, 0x51, uint8_t(position), 0x4A, 0x14, 0, 0,
                              0x52, uint8_t(setting >> 8), uint8_t(setting), 0x51, 0, 0x51, 0,
                              0x32, 3, 0x00, 0x40, 0, 0, 0x02, 0xB4,  // native 64 (line 692, as Accept's)
                              0x18, 0x15, 0, 0};  // then to kAfterDispatch, filled below
    card[9] = uint8_t((card.size()-11) >> 8);
    card[10] = uint8_t(card.size()-11);
    return card;
  };
  std::vector<uint8_t> update = setterCard(updatePosition, kUpdateSetting);
  const size_t updateExit = update.size()-3;
  const auto setupCard = setterCard(setupPosition, kSetupSetting);
  update.insert(update.end(), setupCard.begin(), setupCard.end());
  const size_t others = (cards.size()-68)+(headings.size()-20)+(descriptions.size()-20)+positions.size()+1+
                        picture.size()+pauseRows.size()+update.size();
  while ((others+blockFixed+pane.size()+3) % 4) pane.insert(pane.end(), {0x51, 0, 0x18});
  const size_t exit = pane.size();
  pane.insert(pane.end(), {0x15, 0, 0});  // to kAfterDispatch, filled below
  std::vector<uint8_t> block{0x57, 0x01, 0x51, 0x00, 0x23, 0x51, uint8_t(graphicsPosition), 0x4A, 0x14,
                             uint8_t(pane.size() >> 8), uint8_t(pane.size())};
  block.insert(block.end(), pane.begin(), pane.end());
  const size_t updateAt = block.size();
  block.insert(block.end(), update.begin(), update.end());

  Relocation relocation(original, {{kCardPairs, cards, kCardsArray-kCardPairs},
                                   {kHeadings, headings, kHeadingsArray-kHeadings},
                                   {kDescriptions, descriptions, kDescriptionsArray-kDescriptions},
                                   {kCardDispatch, block},
                                   {kLoadMenu, positions},
                                   {kSectionLookup, {0x23}},
                                   {kSettingsRows, picture},
                                   {kPauseSettingsRows, pauseRows}});
  OptionsScript result{relocation.Build()};
  auto& b = result.bytes;
  // Six cards, headings and descriptions.
  for (size_t array : {kCardsArray, kHeadingsArray, kDescriptionsArray}) {
    const auto at = relocation.Map(array);
    if (b[at] != 0x22 || Read(b, at+3) != 4) throw std::logic_error("Options card array moved");
    Write(b, at+3, kCards);
  }
  {
    const auto at = relocation.Map(kSettingsRows);
    if (b[at] != 0x22 || Read(b, at+3) != 5) throw std::logic_error("Settings rows moved");
    Write(b, at+3, 9);
  }
  {
    const auto at = relocation.Map(kPauseSettingsRows);
    if (b[at] != 0x22 || Read(b, at+3) != 3) throw std::logic_error("In-race Settings rows moved");
    Write(b, at+3, 4);
  }
  // The block's call into the shared row editor, and its way out.
  const auto blockAt = relocation.Map(kCardDispatch, true);
  const auto callAt = blockAt+blockFixed+call, exitAt = blockAt+blockFixed+exit;
  if (b[callAt] != 0x34 || b[exitAt] != 0x15) throw std::logic_error("Graphics block misplaced");
  Write(b, callAt+2, uint32_t(int64_t(relocation.Map(kRowEditor, true))-int64_t(callAt+10)));
  const auto out = int64_t(relocation.Map(kAfterDispatch, true))-int64_t(exitAt+3);
  b[exitAt+1] = uint8_t(uint16_t(out) >> 8);
  b[exitAt+2] = uint8_t(out);
  // The update and Setup blocks' ways out.
  for (const size_t start : {updateAt, updateAt+updateExit+3}) {
    const auto exitAt = blockAt+start+updateExit;
    if (b[exitAt] != 0x15 || b[blockAt+start+18] != 0x32) throw std::logic_error("Update/Setup block misplaced");
    const auto cardOut = int64_t(relocation.Map(kAfterDispatch, true))-int64_t(exitAt+3);
    b[exitAt+1] = uint8_t(uint16_t(cardOut) >> 8);
    b[exitAt+2] = uint8_t(cardOut);
  }
  result.editorStart = uint32_t(relocation.Map(kRowEditor, true));
  result.spinnerPc = uint32_t(relocation.Map(kRowEditorInput)+8);
  return result;
}
}
