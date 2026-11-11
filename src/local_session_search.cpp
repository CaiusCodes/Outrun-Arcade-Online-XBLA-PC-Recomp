#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "generated/default/outrun_init.h"
#include "local_session_search.h"
#include "lan_discovery.h"
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/system/xthread.h>
#include <chrono>
#include <stdexcept>

REXCVAR_DEFINE_BOOL(outrun_local_sessions, false, "OutRun",
    "Automatic LAN lobby discovery with optional direct host")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace outrun::local_sessions {
namespace {
bool Accessible(uint32_t address, uint32_t size, bool write) {
  if (!address || !size || uint64_t(address)+size>0x100000000ull) return false;
  auto* memory=REX_KERNEL_MEMORY();
  uint64_t cursor=address, end=cursor+size;
  while (cursor<end) {
    auto* heap=memory->LookupHeap(uint32_t(cursor));
    rex::memory::HeapAllocationInfo info{};
    if (!heap || !heap->QueryRegionInfo(uint32_t(cursor),&info) ||
        !(info.state & rex::memory::kMemoryAllocationCommit) ||
        !(info.protect & (write ? rex::memory::kMemoryProtectWrite : rex::memory::kMemoryProtectRead)))
      return false;
    const uint64_t next=uint64_t(info.base_address)+info.region_size;
    if (next<=cursor) return false;
    cursor=next;
  }
  return true;
}
uint32_t Search(uint8_t* base,uint32_t request,uint32_t length) {
  constexpr uint32_t invalid=0x80070057;
  if (length!=32 || !Accessible(request,32,false)) return invalid;
  const auto* args=base+request;
  const auto capacity=Read32(args+24), output=Read32(args+28);
  if (capacity<8 || !Accessible(output,std::min(capacity,1480u),true)) return invalid;
  const auto count=Read32(args+8);
  const auto maximum=std::min({count,(capacity-8)/92,16u});
  std::vector<Match> matches;
  try {
    if(maximum)matches=DiscoverLobbies(rex::cvar::GetFlagByName("net_address"),
                                    rex::cvar::GetFlagByName("net_join_address"),maximum);
  } catch(const std::exception& e) { REXLOG_WARN("LAN discovery failed: {}",e.what()); }
  if (!SerializeMatches(std::span(base+output,std::min(capacity,1480u)),output,matches)) return invalid;
  REXLOG_INFO("LAN discovery: returned {} lobby/lobbies at {:08X}",matches.size(),output);
  return 0;
}
}
}

// Only OutRun's verified XSessionSearch call is intercepted. Other messages
// and all non-experimental runs use the original loaded runtime export.
REX_HOOK_RAW(__imp__XMsgStartIORequest) {
  using namespace outrun::local_sessions;
  if (rex::cvar::GetFlagByName("outrun_local_sessions")=="true" &&
      rex::cvar::GetFlagByName("net_private_match")=="true" &&
      ctx.r3.u32==0xFB && ctx.r4.u32==0xB0016) {
    const auto overlapped=ctx.r5.u32;
    if (overlapped && !Accessible(overlapped,28,true)) { ctx.r3.u64=0x80070057; return; }
    auto result=Search(base,ctx.r6.u32,ctx.r7.u32);
    if (overlapped) {
      REX_KERNEL_STATE()->CompleteOverlappedImmediate(overlapped,result);
      result=997;
    }
    rex::system::XThread::SetLastError(result==997 ? 0 : result);
    ctx.r3.u64=result;
    return;
  }
  static auto original=reinterpret_cast<PPCFunc*>(
      GetProcAddress(GetModuleHandleW(L"rexruntime.dll"),"__imp__XMsgStartIORequest"));
  if (!original || original==&__imp__XMsgStartIORequest)
    throw std::runtime_error("Original XMsgStartIORequest export unavailable");
  original(ctx,base);
}
