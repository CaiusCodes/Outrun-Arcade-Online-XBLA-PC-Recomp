#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <stdexcept>

namespace outrun {
// A private event per batch: never share the renderer's fence-wait event.
template<class Fence>
void WaitForQueryFence(Fence* fence,uint64_t target,DWORD timeout_ms=10000) {
  auto completed=fence->GetCompletedValue();
  if(completed==UINT64_MAX)throw std::runtime_error("Device removed awaiting query fence");
  if(completed>=target)return;
  struct Event {
    HANDLE handle=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    ~Event(){if(handle)CloseHandle(handle);}
  } event;
  if(!event.handle)throw std::runtime_error("Cannot create query fence event");
  if(FAILED(fence->SetEventOnCompletion(target,event.handle)))
    throw std::runtime_error("Cannot arm query fence event");
  if(WaitForSingleObject(event.handle,timeout_ms)!=WAIT_OBJECT_0)
    throw std::runtime_error("Query fence event wait failed or timed out");
  completed=fence->GetCompletedValue();
  if(completed==UINT64_MAX)throw std::runtime_error("Device removed awaiting query fence");
  if(completed<target)throw std::runtime_error("Query fence signaled before completion");
}
}
