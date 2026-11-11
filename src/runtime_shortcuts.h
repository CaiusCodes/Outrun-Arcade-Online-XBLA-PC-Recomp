#pragma once
#include <rex/ui/keybinds.h>
namespace outrun {
// Gate SDK shortcuts independently of game input, including saved rebindings.
inline bool DispatchRuntimeShortcut(bool enabled,rex::ui::KeyEvent& event) {
  return enabled && rex::ui::ProcessKeyEvent(event);
}
}
