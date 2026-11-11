// Included inside the GPU namespace. Opt-in experiment: publish real query
// snapshots in order after the GPU fence, without stalling command recording.
namespace {
struct OutrunAsyncQueryState {
  uint64_t running_total = 0; // Worker only.
  std::atomic<uint64_t> published_total{0};
  std::atomic<uint64_t> completed{0};
  outrun::RendererWorkQueue queue; // Destroy first, before the captured state.
};
std::mutex outrun_async_queries_mutex;
std::map<const D3D12CommandProcessor*, std::shared_ptr<OutrunAsyncQueryState>> outrun_async_queries;
std::shared_ptr<OutrunAsyncQueryState> OutrunAsyncQueries(const D3D12CommandProcessor* owner) {
  std::lock_guard lock(outrun_async_queries_mutex);
  auto& state = outrun_async_queries[owner];
  if (!state) state = std::make_shared<OutrunAsyncQueryState>();
  return state;
}
void OutrunDrainAsyncQueries(const D3D12CommandProcessor* owner, bool forget) {
  std::shared_ptr<OutrunAsyncQueryState> state;
  {
    std::lock_guard lock(outrun_async_queries_mutex);
    const auto it = outrun_async_queries.find(owner);
    if (it == outrun_async_queries.end()) return;
    state = it->second;
    if (forget) outrun_async_queries.erase(it);
  }
  state->queue.Drain();
  try { state->queue.CheckError(); }
  catch (const std::exception& e) { REXGPU_ERROR("OutRun async query worker: {}", e.what()); }
}
}
