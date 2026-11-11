// Included at the end of D3D12CommandProcessor::IssueSwap by the local overlay
// builder. Runs on the GPU command thread, after the frame's commands submit.
// Experimental: renderer caches are rebuilt while guest CPU/menu state stays live.
if (REXCVAR_GET(outrun_live_resolution) && texture_cache_) {
  uint32_t requested_x, requested_y;
  TextureCache::GetConfigDrawResolutionScale(requested_x, requested_y);
  D3D12TextureCache::ClampDrawResolutionScaleToMaxSupported(
      requested_x, requested_y, GetD3D12Provider());
  const auto previous_x = texture_cache_->draw_resolution_scale_x();
  const auto previous_y = texture_cache_->draw_resolution_scale_y();
  if ((requested_x != previous_x || requested_y != previous_y) &&
      AwaitAllQueueOperationsCompletion()) {
    // The pipeline cache owns active Shader objects. Preserve the guest
    // microcode, in guest byte order, before releasing those objects.
    auto preserve = [](Shader* shader) {
      std::vector<uint32_t> words;
      if (shader) {
        words = shader->ucode_data();
        for (auto& word : words) word = __builtin_bswap32(word);
      }
      return words;
    };
    auto vertex_words = preserve(active_vertex_shader_);
    auto pixel_words = preserve(active_pixel_shader_);
    const auto shader_storage = OutrunGetShaderStorage(this);
    active_vertex_shader_ = nullptr;
    active_pixel_shader_ = nullptr;
    ShutdownContext();
    if (!SetupContext()) {
      // The SDK cannot continue draws with a partially created GPU context.
      // Fail explicitly rather than allowing stale shader/resource pointers.
      REXGPU_ERROR("OutRun live resolution: GPU context recreation failed");
      throw std::runtime_error("Unable to recreate the OutRun renderer after resolution change");
    }
    // ShutdownContext destroys PipelineCache and closes its disk storage.
    // Reopen it before restoring shaders so new race shaders remain persistent.
    if (shader_storage) {
      pipeline_cache_->InitializeShaderStorage(shader_storage->root,
                                               shader_storage->title_id, true);
      REXGPU_INFO("OutRun live resolution: shader storage reattached for {:08X}",
                  shader_storage->title_id);
    }
    if (!vertex_words.empty()) {
      active_vertex_shader_ = LoadShader(xenos::ShaderType::kVertex, 0,
          vertex_words.data(), uint32_t(vertex_words.size()));
    }
    if (!pixel_words.empty()) {
      active_pixel_shader_ = LoadShader(xenos::ShaderType::kPixel, 0,
          pixel_words.data(), uint32_t(pixel_words.size()));
    }
    REXGPU_INFO("OutRun live resolution: {}x{} -> {}x{}, GPU resources recreated",
        previous_x, previous_y, texture_cache_->draw_resolution_scale_x(),
        texture_cache_->draw_resolution_scale_y());
  }
}
