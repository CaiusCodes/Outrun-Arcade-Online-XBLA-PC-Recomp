#pragma once
#include <functional>
#include <map>
#include <mutex>

namespace outrun::query_order {
// Shared within the GPU plugin, without changing the SDK's class/vtable ABI.
// Only opt-in D3D12 processors register. Other backends have no callback.
inline std::mutex mutex;
inline std::map<const void*,std::function<void()>> callbacks;
inline void Register(const void* owner,std::function<void()> callback) {
  std::lock_guard lock(mutex);
  callbacks[owner]=std::move(callback);
}
inline void Forget(const void* owner) {
  std::lock_guard lock(mutex);
  callbacks.erase(owner);
}
inline void BeforeGuestWrite(const void* owner) {
  std::function<void()> callback;
  {
    std::lock_guard lock(mutex);
    auto it=callbacks.find(owner);
    if(it!=callbacks.end())callback=it->second;
  }
  // No registry lock while waiting for GPU work. Failure must prevent the
  // later command from falsely acknowledging completion.
  if(callback)callback();
}
}
