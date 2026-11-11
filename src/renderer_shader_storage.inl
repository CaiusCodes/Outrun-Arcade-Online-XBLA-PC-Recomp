// Project-local metadata, kept outside SDK classes to preserve their ABI.
// Initial cache setup can run while the command thread is paused. Guard the
// registry and return a copy, so renderer teardown never retains a map reference.
namespace {
struct OutrunShaderStorage {
  std::filesystem::path root;
  uint32_t title_id;
};
std::mutex outrun_shader_storage_mutex;
std::map<const D3D12CommandProcessor*, OutrunShaderStorage> outrun_shader_storage;
void OutrunRememberShaderStorage(const D3D12CommandProcessor* owner,
                                const std::filesystem::path& root, uint32_t title_id) {
  std::lock_guard lock(outrun_shader_storage_mutex);
  outrun_shader_storage.insert_or_assign(owner, OutrunShaderStorage{root, title_id});
}
std::optional<OutrunShaderStorage> OutrunGetShaderStorage(const D3D12CommandProcessor* owner) {
  std::lock_guard lock(outrun_shader_storage_mutex);
  const auto it = outrun_shader_storage.find(owner);
  if (it == outrun_shader_storage.end()) return std::nullopt;
  return it->second;
}
void OutrunForgetShaderStorage(const D3D12CommandProcessor* owner) {
  std::lock_guard lock(outrun_shader_storage_mutex);
  outrun_shader_storage.erase(owner);
}
}
