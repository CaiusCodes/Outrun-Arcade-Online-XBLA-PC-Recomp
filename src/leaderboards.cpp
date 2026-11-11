#include "generated/default/outrun_init.h"
#include "empty_leaderboard.h"
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xenumerator.h>
#include <windows.h>
#include <stdexcept>
#include <mutex>
#include <set>

namespace {
using rex::X_RESULT;
class EmptyStatsEnumerator final : public rex::system::XEnumerator {
 public:
  explicit EmptyStatsEnumerator(uint32_t view)
      : XEnumerator(REX_KERNEL_STATE(),1,24), view_(view) {}
  uint32_t WriteItems(uint32_t address,uint8_t* data,uint32_t* count) override {
    if(count)*count=0;
    if(written_)return X_ERROR_NO_MORE_FILES;
    if(!outrun::leaderboards::WriteEmptyView({data,24},address,view_))
      return X_ERROR_INVALID_PARAMETER;
    written_=true;
    if(count)*count=1;
    return X_ERROR_SUCCESS;
  }
 private:
  uint32_t view_;
  bool written_=false;
};
}

REX_HOOK_RAW(__imp__XamUserCreateStatsEnumerator) {
  // OutRun requests exactly one board. Delegate every real query to the
  // installed runtime; adapt only its successful eight-byte empty result.
  const auto specs=ctx.r7.u32, spec=ctx.r8.u32;
  const auto sizeOut=ctx.r9.u32, handleOut=ctx.r10.u32;
  static auto original=reinterpret_cast<PPCFunc*>(GetProcAddress(
      GetModuleHandleW(L"rexruntime.dll"),"__imp__XamUserCreateStatsEnumerator"));
  if(!original || original==&__imp__XamUserCreateStatsEnumerator)
    throw std::runtime_error("Original stats enumerator export unavailable");
  original(ctx,base);
  if(ctx.r3.u32 || specs!=1 || !spec || !sizeOut || !handleOut || REX_LOAD_U32(sizeOut)!=8) return;
  const auto oldHandle=REX_LOAD_U32(handleOut);
  auto old=REX_KERNEL_OBJECTS()->LookupObject<rex::system::XEnumerator>(oldHandle);
  if(!old || old->item_size()!=8)return;
  const auto view=REX_LOAD_U32(spec);
  auto replacement=rex::system::object_ref<EmptyStatsEnumerator>(new EmptyStatsEnumerator(view));
  const auto status=replacement->Initialize(0,0xFB,0,0,0);
  if(status) { REXLOG_ERROR("Empty leaderboard allocation failed: {:08X}",status); return; }
  REX_STORE_U32(sizeOut,24);
  REX_STORE_U32(handleOut,replacement->handle());
  old->Delete();
  // (once per view: the leaderboard screen asks again every time it is shown)
  static std::mutex logged_mutex;
  static std::set<uint32_t> logged;
  std::lock_guard lock(logged_mutex);
  if(logged.insert(view).second)REXLOG_INFO("Local leaderboard: view {} has no scores; returning an empty board",view);
  else REXLOG_DEBUG("Local leaderboard: view {} has no scores; returning an empty board",view);
}
