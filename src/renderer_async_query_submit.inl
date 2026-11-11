// At a completed resolve submission, before the synchronous fence wait.
if (REXCVAR_GET(outrun_async_queries)) {
  auto state = OutrunAsyncQueries(this);
  auto snapshots = occlusion_pending_;
  Microsoft::WRL::ComPtr<ID3D12Fence> fence = submission_fence_;
  auto* readback = occlusion_query_readback_mapping_;
  const uint64_t scale = uint64_t(texture_cache_->draw_resolution_scale_x()) *
                         texture_cache_->draw_resolution_scale_y();
  state->queue.Submit([this, state, snapshots = std::move(snapshots), fence,
                       readback, query_submission, scale] {
    // The worker never touches command lists or renderer caches. Shutdown and
    // query-ring wrap drain it before readback storage/guest memory can change.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (fence->GetCompletedValue() < query_submission) {
      if (std::chrono::steady_clock::now() > deadline)
        throw std::runtime_error("Timed out awaiting occlusion results");
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (fence->GetCompletedValue() == UINT64_MAX)
      throw std::runtime_error("Device removed while awaiting occlusion results");
    for (const auto& snapshot : snapshots) {
      auto* block = memory_->TranslatePhysical<xenos::xe_gpu_depth_sample_counts*>(snapshot.guest_addr);
      WriteGuestOcclusionResult(block, state->running_total);
      if (snapshot.has_query) {
        auto samples = readback[snapshot.host_index];
        if (scale > 1) samples = (samples + (scale >> 1)) / scale;
        state->running_total += samples;
      }
    }
    state->published_total.store(state->running_total, std::memory_order_release);
    if (state->completed.fetch_add(1) == 0)
      REXGPU_INFO("OutRun async queries: first real GPU snapshot batch published");
  });
  occlusion_pending_.clear();
  occlusion_active_resolve_ = false;
  return;
}
