#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include "button_pulse.h"

namespace outrun::keyboard {
inline std::atomic<bool> menuActive{false};
// Written on the guest scheduler thread; host key events read only menuActive.
// Script instances remain allocated while dormant, so their hashes alone do
// not establish that a menu is open. Use actual native menu-input polling.
inline bool menuPolled=false;
inline void MenuInputPolled() { menuPolled=true; }
class ContextKeys {
 public:
  static int Index(uint16_t key) {
    switch(key) { case 0x1B:return 0; case 0x08:return 1;
      case 0x0D:return 2; case 0x20:return 3; default:return -1; }
  }
  void Down(uint16_t key, bool menu, int64_t now) {
    const auto index = Index(key);
    if (index < 0 || held_[index].load()) return;
    // Latch until release: opening Pause while holding Esc must not turn the
    // same press into B and immediately close it. Enter still starts the title.
    const uint16_t button = index == 0 ? (menu ? 0x2000 : 0x0010) :
        index == 1 ? 0x2000 : index == 2 ? (menu ? 0x1000 : 0x0010) : 0x1000;
    buttons_[index].store(button);
    held_[index].store(true);
    pulse_.Press(button, now);
  }
  void Up(uint16_t key) { const auto i=Index(key); if(i>=0)held_[i].store(false); }
  uint16_t Read(int64_t now) const {
    uint16_t result=pulse_.Read(now);
    for(size_t i=0;i<held_.size();++i)if(held_[i].load())result|=buttons_[i].load();
    return result;
  }
  void Clear() { for(auto& held:held_)held.store(false); pulse_.Clear(); }
 private:
  std::array<std::atomic<bool>,4> held_{};
  std::array<std::atomic<uint16_t>,4> buttons_{};
  ButtonPulse pulse_;
};
}
