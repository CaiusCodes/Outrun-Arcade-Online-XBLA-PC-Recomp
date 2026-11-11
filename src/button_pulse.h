#pragma once
#include <array>
#include <atomic>
#include <cstdint>

class ButtonPulse {
 public:
  // OutRun queries controller connection and button state separately each frame.
  // Keep a quick tap available for both queries and at least one 30 Hz frame.
  void Press(uint16_t buttons, int64_t now_ms) {
    for (unsigned bit = 0; bit < 16; ++bit) {
      if (buttons & (1u << bit)) until_[bit].store(now_ms + 50);
    }
  }
  uint16_t Read(int64_t now_ms) const {
    uint16_t buttons = 0;
    for (unsigned bit = 0; bit < 16; ++bit) {
      if (until_[bit].load() > now_ms) buttons |= uint16_t(1u << bit);
    }
    return buttons;
  }
  void Clear() { for (auto& until : until_) until.store(0); }
 private:
  std::array<std::atomic<int64_t>, 16> until_{};
};
