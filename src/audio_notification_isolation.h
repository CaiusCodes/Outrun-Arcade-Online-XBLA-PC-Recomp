#pragma once
#include <cstdint>
namespace outrun::audio {
inline thread_local uint32_t dispatchCaller=0;
// Diagnostic scope follows synchronous native one-shot allocation only.
// It never suppresses a queue event or skips the native playback lifecycle.
inline thread_local bool notificationIsolation=false;
struct NotificationIsolationScope {
  bool previous=notificationIsolation;
  explicit NotificationIsolationScope(bool active){notificationIsolation=active;}
  ~NotificationIsolationScope(){notificationIsolation=previous;}
};
}
