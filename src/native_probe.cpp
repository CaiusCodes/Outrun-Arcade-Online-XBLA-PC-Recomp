// Native renderer research, checkpoint 2: per-frame counts of the guest D3D
// calls a native (Plume) renderer would have to draw from, after Perfect Dark's
// pd_native_probe. Every hook only counts and then runs the original guest
// function, so guest state, timing and results are unchanged. Default off.
//
// Names and addresses: out/native-research-20261103/d3d_functions_outrun.toml
// (Perfect Dark's matcher over GoldenEye's names). D3D_CBlocker_Check
// (sub_822999B8) is counted by gpu_wait_hook.cpp's hook, not here.
//
// Optional one-shot dump of the guest image (outrun_native_dump_image) for the
// D3D render/sampler-state tables. It is game content: write it only to a
// private scratch folder, never into the project, a release or a report.
#include "generated/default/outrun_init.h"
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <string>

REXCVAR_DEFINE_BOOL(outrun_native_probe, false, "OutRun/Diagnostics",
                    "Native renderer research: log per-frame guest D3D call counts every 300 frames")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(outrun_native_dump_image, "", "OutRun/Diagnostics",
                      "Native renderer research: write the guest image (0x82000000-0x82B20000) to this file "
                      "once (game content: private scratch folder only)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace outrun::native_probe {
namespace {
enum Call : size_t {
  kSetTexture, kSetPixelShader, kSetVertexShader, kSetVertexDeclaration, kSetStreamSource, kSetIndices,
  kDrawVerticesUP, kBeginVertices, kDrawIndexA, kDrawIndexB, kClear, kResolve, kSetRenderTarget,
  kSetDepthStencil, kBeginTiling, kEndTiling, kSetPredication, kQueryIssue, kIssueOcclusionQuery,
  kKickOffIndirect, kCreateVertexShader, kCreateTexture, kCreateSurface, kCount
};
constexpr std::array<const char*, kCount> kNames = {
  "SetTexture", "SetPixelShader", "SetVertexShader", "SetVertexDeclaration", "SetStreamSource", "SetIndices",
  "DrawVerticesUP", "BeginVertices", "DrawIndx82286760", "DrawIndx82286370", "Clear", "Resolve",
  "SetRenderTarget", "SetDepthStencilSurface", "BeginTiling", "EndTiling", "SetPredication", "Query_Issue",
  "IssueOcclusionQuery", "KickOffIndirect", "CreateVertexShader", "CreateTexture", "CreateSurface"
};
std::array<std::atomic<uint32_t>, kCount> frame{};
std::array<uint64_t, kCount> total{}, peak{};
uint32_t frames = 0;
bool dumped = false;

inline void Count(Call call) {
  if (REXCVAR_GET(outrun_native_probe)) frame[call].fetch_add(1, std::memory_order_relaxed);
}

void DumpImage(uint8_t* base) {
  dumped = true;
  const auto path = REXCVAR_GET(outrun_native_dump_image);
  if (path.empty()) return;
  constexpr uint32_t kStart = 0x82000000, kEnd = 0x82B20000;
  FILE* file = _wfopen(std::filesystem::path(path).c_str(), L"wb");
  if (!file) { REXLOG_ERROR("[native_probe] cannot write image dump"); return; }
  std::array<uint8_t, 4096> zero{};
  uint32_t unreadable = 0;
  for (uint32_t page = kStart; page < kEnd; page += 4096) {
    MEMORY_BASIC_INFORMATION info{};
    const bool readable = VirtualQuery(base+page, &info, sizeof(info)) && info.State == MEM_COMMIT &&
                          !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD));
    if (!readable) ++unreadable;
    fwrite(readable ? base+page : zero.data(), 1, 4096, file);
  }
  fclose(file);
  REXLOG_INFO("[native_probe] guest image written ({} pages unreadable, zero-filled)", unreadable);
}

void EndFrame(uint8_t* base, uint32_t device) {
  if (!dumped) {
    REXLOG_INFO("[native_probe] first Swap: device {:08X}", device);
    DumpImage(base);
  }
  if (!REXCVAR_GET(outrun_native_probe)) return;
  for (size_t i = 0; i < kCount; ++i) {
    const uint64_t value = frame[i].exchange(0, std::memory_order_relaxed);
    total[i] += value;
    peak[i] = std::max(peak[i], value);
  }
  if (++frames < 300) return;
  std::string line;
  for (size_t i = 0; i < kCount; ++i) {
    if (!total[i]) continue;
    char item[96];
    std::snprintf(item, sizeof(item), " %s=%.1f/%llu", kNames[i], double(total[i]) / frames,
                  static_cast<unsigned long long>(peak[i]));
    line += item;
  }
  REXLOG_INFO("[native_probe] per frame avg/max over {} frames:{}", frames, line);
  total.fill(0); peak.fill(0); frames = 0;
}
}  // namespace
}  // namespace outrun::native_probe

#define OUTRUN_COUNT_HOOK(function, call)                                 \
  REX_HOOK_RAW(function) {                                                \
    outrun::native_probe::Count(outrun::native_probe::call);             \
    __imp__##function(ctx, base);                                         \
  }

OUTRUN_COUNT_HOOK(sub_822889A0, kSetTexture)
OUTRUN_COUNT_HOOK(sub_8228A958, kSetPixelShader)
OUTRUN_COUNT_HOOK(sub_8228AB60, kSetVertexShader)
OUTRUN_COUNT_HOOK(sub_8228AD78, kSetVertexDeclaration)
OUTRUN_COUNT_HOOK(sub_82284A00, kSetStreamSource)
OUTRUN_COUNT_HOOK(sub_82284B20, kSetIndices)
OUTRUN_COUNT_HOOK(sub_82285D70, kDrawVerticesUP)
OUTRUN_COUNT_HOOK(sub_822858D8, kBeginVertices)
OUTRUN_COUNT_HOOK(sub_82286760, kDrawIndexA)
OUTRUN_COUNT_HOOK(sub_82286370, kDrawIndexB)
OUTRUN_COUNT_HOOK(sub_82298178, kClear)
OUTRUN_COUNT_HOOK(sub_822989A0, kResolve)
OUTRUN_COUNT_HOOK(sub_822852A8, kSetRenderTarget)
OUTRUN_COUNT_HOOK(sub_822855F8, kSetDepthStencil)
OUTRUN_COUNT_HOOK(sub_8229CEC8, kBeginTiling)
OUTRUN_COUNT_HOOK(sub_8229D238, kEndTiling)
OUTRUN_COUNT_HOOK(sub_8229CD58, kSetPredication)
OUTRUN_COUNT_HOOK(sub_82294670, kQueryIssue)
OUTRUN_COUNT_HOOK(sub_822A62A8, kIssueOcclusionQuery)
OUTRUN_COUNT_HOOK(sub_8229A770, kKickOffIndirect)
OUTRUN_COUNT_HOOK(sub_8228C558, kCreateVertexShader)
OUTRUN_COUNT_HOOK(sub_82288688, kCreateTexture)
OUTRUN_COUNT_HOOK(sub_822887A8, kCreateSurface)

// D3DDevice_Swap: the frame boundary (one game caller).
REX_HOOK_RAW(sub_82293A10) {
  const auto device = ctx.r3.u32;
  __imp__sub_82293A10(ctx, base);
  outrun::native_probe::EndFrame(base, device);
}
