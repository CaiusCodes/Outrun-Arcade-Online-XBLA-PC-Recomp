// OutRun native renderer: frames out to the SDK's Vulkan presenter (native_vk_output.h).

#include "native_vk_output.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <unordered_map>
#include <vector>

#include "native_shaders.h"
#include <rex/logging/api.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/vulkan/presenter.h>
#include <rex/ui/vulkan/provider.h>
#include <rex/ui/vulkan/util.h>

namespace outrun::native_vk {
namespace {

using rex::ui::vulkan::VulkanDevice;
using rex::ui::vulkan::VulkanPresenter;

// A triangle over the whole target (the same either way up).
constexpr char kOutputVertexShader[] = R"(
float4 main(uint id : SV_VertexID) : SV_Position {
  const float2 p = float2((id << 1) & 2, id & 2);
  return float4(p * 2.0 - 1.0, 0, 1);
}
)";

// One pixel of the frame per output pixel (the guest output is the frame's
// size), then the gamma ramp and Vivid Colours / Black Level as the Direct3D
// 12 output pass (native_gpu.cpp, kOutputShader).
constexpr char kOutputPixelShader[] = R"(
struct Settings {
  uint width;
  uint ramp;  // index of the ramp's first entry, after the pixels
  uint mode;  // 8 gamma ramp, 16 colour adjustment, 32 footer highlight
  float black_level, contrast, saturation;
  float highlight_scale, highlight_x0, highlight_y0, highlight_x1, highlight_y1;
};
[[vk::push_constant]] Settings settings;
// The footer prompt under the mouse (menu_footer.h): a capsule in the menus'
// orange, behind the game's lettering. Only the blue strip behind the label
// takes the fill: white letters, their dark edge, key caps and pad buttons
// stay; then a dark edge like the game's own highlighted rows.
float3 footer_highlight(float3 c, float2 p, float4 r, float s) {
  const float2 centre = (r.xy + r.zw) * 0.5, extent = (r.zw - r.xy) * 0.5;
  const float radius = extent.y;
  const float2 q = abs(p - centre) - (extent - radius);
  const float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - radius;
  if (d > 1.0) return c;
  const float t = saturate((p.y - r.y) / max(r.w - r.y, 1.0));
  const float3 fill = lerp(float3(1.0, 0.80, 0.16), float3(1.0, 0.45, 0.0), t);
  const float luma = dot(c, float3(0.299, 0.587, 0.114));
  const float back = saturate((c.b - max(c.r, c.g)) * 5.0 - 0.5) * saturate((luma - 0.05) * 10.0);
  float3 o = lerp(c, fill, back);
  o = lerp(o, float3(0.03, 0.09, 0.31), saturate(d + 2.0 * s + 0.5));
  return lerp(c, o, saturate(0.5 - d));
}

[[vk::binding(0, 0)]] StructuredBuffer<uint> frame;  // B8G8R8A8 pixels, then R | G << 10 | B << 20 per ramp entry
float4 main(float4 pos : SV_Position) : SV_Target {
  const uint2 xy = uint2(pos.xy);
  const uint p = frame[xy.y * settings.width + xy.x];
  float3 c = float3((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF) / 255.0;
  [branch] if (settings.mode & 8u) {
    const uint3 i = uint3(round(saturate(c) * 255.0)) + settings.ramp;
    c = float3(frame[i.r] & 0x3FF, (frame[i.g] >> 10) & 0x3FF, (frame[i.b] >> 20) & 0x3FF) / 1023.0;
  }
  [branch] if (settings.mode & 16u) {
    c = saturate((c - settings.black_level) / (1.0 - settings.black_level));
    float luma = dot(c, float3(0.299, 0.587, 0.114));
    c = luma + (c - luma) * settings.saturation;
    c = saturate((c - 0.25) * settings.contrast + 0.25);  // pivot on a dark grey: the game is dark
  }
  [branch] if (settings.mode & 32u)
    c = footer_highlight(c, pos.xy, float4(settings.highlight_x0, settings.highlight_y0, settings.highlight_x1,
                                           settings.highlight_y1), settings.highlight_scale);
  return float4(c, 1.0);
}
)";

struct PushConstants {
  uint32_t width, ramp, mode;
  float black_level, contrast, saturation;
  float highlight_scale, highlight[4];
};

constexpr uint32_t kSlots = 3;

struct Output {
  VulkanPresenter* presenter = nullptr;
  const VulkanDevice* device = nullptr;
  uint32_t w = 0, h = 0;
  VkDeviceSize pixel_bytes = 0, slot_bytes = 0;
  VkBuffer buffer[kSlots] = {};
  VkDeviceMemory memory[kSlots] = {};
  uint8_t* mapped[kSlots] = {};
  bool coherent = true;
  VkFence fence[kSlots] = {};
  bool submitted[kSlots] = {};
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd[kSlots] = {};
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet set[kSlots] = {};
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  // Framebuffers per guest output image (the presenter keeps a few in turn).
  std::unordered_map<uint64_t, std::pair<VkImageView, VkFramebuffer>> framebuffers;
  uint32_t ramp[256] = {};
} out;

rex::system::IGraphicsSystem* graphics_system() {
  auto* ks = rex::system::kernel_state();
  auto* rt = ks ? ks->emulator() : nullptr;
  return rt ? static_cast<rex::system::IGraphicsSystem*>(rt->graphics_system()) : nullptr;
}

rex::ui::vulkan::VulkanProvider* provider() {
  auto* gs = graphics_system();
  if (!gs || !gs->presenter() || !gs->provider()) return nullptr;
  return dynamic_cast<rex::ui::vulkan::VulkanProvider*>(gs->provider());
}

VkShaderModule shader_module(const VulkanDevice::Functions& dfn, VkDevice dev, const char* source, const wchar_t* profile) {
  std::vector<uint8_t> spirv;
  std::string errors;
  if (!outrun::native_shaders::CompileDxil(source, profile, spirv, errors)) {
    REXKRNL_ERROR("ORNATIVE Vulkan output shader: {}", errors.substr(0, 300));
    return VK_NULL_HANDLE;
  }
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = spirv.size();
  ci.pCode = reinterpret_cast<const uint32_t*>(spirv.data());
  VkShaderModule module = VK_NULL_HANDLE;
  return dfn.vkCreateShaderModule(dev, &ci, nullptr, &module) == VK_SUCCESS ? module : VK_NULL_HANDLE;
}

bool create_pipeline(const VulkanDevice::Functions& dfn, VkDevice dev) {
  VkDescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo sl{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  sl.bindingCount = 1;
  sl.pBindings = &binding;
  if (dfn.vkCreateDescriptorSetLayout(dev, &sl, nullptr, &out.set_layout) != VK_SUCCESS) return false;
  VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.setLayoutCount = 1;
  pl.pSetLayouts = &out.set_layout;
  pl.pushConstantRangeCount = 1;
  pl.pPushConstantRanges = &range;
  if (dfn.vkCreatePipelineLayout(dev, &pl, nullptr, &out.layout) != VK_SUCCESS) return false;

  VkAttachmentDescription attachment{};
  attachment.format = VulkanPresenter::kGuestOutputFormat;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // every pixel is written
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VulkanPresenter::kGuestOutputInternalLayout;
  VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color;
  // The presenter reads the guest output in its fragment shaders: after its
  // last paint, write; after the write, its next paint reads.
  VkSubpassDependency deps[2] = {};
  deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  deps[0].dstSubpass = 0;
  deps[0].srcStageMask = VulkanPresenter::kGuestOutputInternalStageMask;
  deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].srcSubpass = 0;
  deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  deps[1].dstStageMask = VulkanPresenter::kGuestOutputInternalStageMask;
  deps[1].dstAccessMask = VulkanPresenter::kGuestOutputInternalAccessMask;
  VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  rp.attachmentCount = 1;
  rp.pAttachments = &attachment;
  rp.subpassCount = 1;
  rp.pSubpasses = &subpass;
  rp.dependencyCount = 2;
  rp.pDependencies = deps;
  if (dfn.vkCreateRenderPass(dev, &rp, nullptr, &out.render_pass) != VK_SUCCESS) return false;

  const VkShaderModule vs = shader_module(dfn, dev, kOutputVertexShader, L"vs_6_0");
  const VkShaderModule ps = shader_module(dfn, dev, kOutputPixelShader, L"ps_6_0");
  bool ok = vs && ps;
  if (ok) {
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &blend;
    const VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dynamic;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &ds;
    gp.layout = out.layout;
    gp.renderPass = out.render_pass;
    ok = dfn.vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, nullptr, &out.pipeline) == VK_SUCCESS;
  }
  if (vs) dfn.vkDestroyShaderModule(dev, vs, nullptr);
  if (ps) dfn.vkDestroyShaderModule(dev, ps, nullptr);
  return ok;
}

bool create_buffers(const VulkanDevice::Functions& dfn, VkDevice dev) {
  out.pixel_bytes = VkDeviceSize(out.w) * out.h * 4;
  out.slot_bytes = out.pixel_bytes + 256 * 4;
  for (uint32_t i = 0; i < kSlots; ++i) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = out.slot_bytes;
    bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (dfn.vkCreateBuffer(dev, &bi, nullptr, &out.buffer[i]) != VK_SUCCESS) return false;
    VkMemoryRequirements req{};
    dfn.vkGetBufferMemoryRequirements(dev, out.buffer[i], &req);
    // Written by the CPU once a frame and read once by the GPU: uncached host memory.
    const uint32_t type =
        rex::ui::vulkan::util::ChooseHostMemoryType(out.device->memory_types(), req.memoryTypeBits, false);
    if (type == UINT32_MAX) return false;
    out.coherent = out.coherent && (out.device->memory_types().host_coherent & (1u << type));
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = type;
    void* p = nullptr;
    if (dfn.vkAllocateMemory(dev, &ai, nullptr, &out.memory[i]) != VK_SUCCESS ||
        dfn.vkBindBufferMemory(dev, out.buffer[i], out.memory[i], 0) != VK_SUCCESS ||
        dfn.vkMapMemory(dev, out.memory[i], 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS)
      return false;
    out.mapped[i] = static_cast<uint8_t*>(p);
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (dfn.vkCreateFence(dev, &fi, nullptr, &out.fence[i]) != VK_SUCCESS) return false;
  }
  VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kSlots};
  VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dp.maxSets = kSlots;
  dp.poolSizeCount = 1;
  dp.pPoolSizes = &size;
  if (dfn.vkCreateDescriptorPool(dev, &dp, nullptr, &out.descriptor_pool) != VK_SUCCESS) return false;
  VkDescriptorSetLayout layouts[kSlots] = {out.set_layout, out.set_layout, out.set_layout};
  VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  da.descriptorPool = out.descriptor_pool;
  da.descriptorSetCount = kSlots;
  da.pSetLayouts = layouts;
  if (dfn.vkAllocateDescriptorSets(dev, &da, out.set) != VK_SUCCESS) return false;
  for (uint32_t i = 0; i < kSlots; ++i) {
    VkDescriptorBufferInfo info{out.buffer[i], 0, out.slot_bytes};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = out.set[i];
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &info;
    dfn.vkUpdateDescriptorSets(dev, 1, &w, 0, nullptr);
  }
  VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cp.queueFamilyIndex = out.device->queue_family_graphics_compute();
  if (dfn.vkCreateCommandPool(dev, &cp, nullptr, &out.pool) != VK_SUCCESS) return false;
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool = out.pool;
  ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ca.commandBufferCount = kSlots;
  return dfn.vkAllocateCommandBuffers(dev, &ca, out.cmd) == VK_SUCCESS;
}

void wait_slot(uint32_t k) {
  if (!out.submitted[k]) return;
  const VulkanDevice::Functions& dfn = out.device->functions();
  dfn.vkWaitForFences(out.device->device(), 1, &out.fence[k], VK_TRUE, UINT64_MAX);
  dfn.vkResetFences(out.device->device(), 1, &out.fence[k]);
  out.submitted[k] = false;
}

VkFramebuffer framebuffer_for(uint64_t version, VkImageView view) {
  if (const auto it = out.framebuffers.find(version); it != out.framebuffers.end() && it->second.first == view)
    return it->second.second;
  const VulkanDevice::Functions& dfn = out.device->functions();
  const VkDevice dev = out.device->device();
  if (out.framebuffers.size() >= VulkanPresenter::kMaxActiveGuestOutputImageVersions + 2) {
    // Older images are gone: once nothing of ours is in flight, drop their framebuffers.
    for (uint32_t i = 0; i < kSlots; ++i) wait_slot(i);
    for (auto& [v, f] : out.framebuffers) dfn.vkDestroyFramebuffer(dev, f.second, nullptr);
    out.framebuffers.clear();
  }
  VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  fi.renderPass = out.render_pass;
  fi.attachmentCount = 1;
  fi.pAttachments = &view;
  fi.width = out.w;
  fi.height = out.h;
  fi.layers = 1;
  VkFramebuffer fb = VK_NULL_HANDLE;
  if (dfn.vkCreateFramebuffer(dev, &fi, nullptr, &fb) != VK_SUCCESS) return VK_NULL_HANDLE;
  if (const auto it = out.framebuffers.find(version); it != out.framebuffers.end())
    dfn.vkDestroyFramebuffer(dev, it->second.second, nullptr);  // (a stale one of the same version: not in flight)
  out.framebuffers[version] = {view, fb};
  return fb;
}

}  // namespace

bool PresenterIsVulkan(std::string* device_name) {
  rex::ui::vulkan::VulkanProvider* p = provider();
  if (!p || !p->vulkan_device()) return false;
  if (device_name) *device_name = p->vulkan_device()->properties().deviceName;
  return true;
}

bool Setup(uint32_t w, uint32_t h) {
  rex::ui::vulkan::VulkanProvider* p = provider();
  if (!p || !p->vulkan_device()) return false;
  out.device = p->vulkan_device();
  out.presenter = static_cast<VulkanPresenter*>(graphics_system()->presenter());
  out.w = w;
  out.h = h;
  const VulkanDevice::Functions& dfn = out.device->functions();
  const VkDevice dev = out.device->device();
  if (!create_pipeline(dfn, dev) || !create_buffers(dfn, dev)) {
    REXKRNL_ERROR("ORNATIVE could not set up the Vulkan output pass on the presenter's device");
    return false;
  }
  for (uint32_t i = 0; i < 256; ++i) out.ramp[i] = (i * 1023 / 255) * 0x100401u;  // identity
  REXKRNL_INFO("ORNATIVE Vulkan output: {}x{} frames through system memory to the presenter ({})", w, h,
               out.coherent ? "coherent" : "flushed");
  return true;
}

uint8_t* Staging(uint32_t k) {
  wait_slot(k);
  return out.mapped[k];
}

void SetRamp(const uint32_t* ramp256) { std::memcpy(out.ramp, ramp256, sizeof(out.ramp)); }

bool Present(uint32_t k, const OutputSettings& settings) {
  std::memcpy(out.mapped[k] + out.pixel_bytes, out.ramp, sizeof(out.ramp));
  const VulkanDevice::Functions& dfn = out.device->functions();
  const VkDevice dev = out.device->device();
  if (!out.coherent) {
    VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = out.memory[k];
    range.size = VK_WHOLE_SIZE;
    dfn.vkFlushMappedMemoryRanges(dev, 1, &range);
  }
  return out.presenter->RefreshGuestOutput(out.w, out.h, out.w, out.h,
                                           [k, &settings, &dfn, dev](rex::ui::Presenter::GuestOutputRefreshContext& context) {
    auto& c = static_cast<VulkanPresenter::VulkanGuestOutputRefreshContext&>(context);
    context.SetIs8bpc(true);
    const VkFramebuffer fb = framebuffer_for(c.image_version(), c.image_view());
    if (!fb) return false;
    const VkCommandBuffer cmd = out.cmd[k];  // (finished: Staging waited; begin resets it)
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    dfn.vkBeginCommandBuffer(cmd, &bi);
    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = out.render_pass;
    rb.framebuffer = fb;
    rb.renderArea.extent = {out.w, out.h};
    dfn.vkCmdBeginRenderPass(cmd, &rb, VK_SUBPASS_CONTENTS_INLINE);
    dfn.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, out.pipeline);
    VkViewport viewport{0.0f, 0.0f, float(out.w), float(out.h), 0.0f, 1.0f};
    dfn.vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, {out.w, out.h}};
    dfn.vkCmdSetScissor(cmd, 0, 1, &scissor);
    dfn.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, out.layout, 0, 1, &out.set[k], 0, nullptr);
    const PushConstants pc{out.w, uint32_t(out.pixel_bytes / 4), settings.mode, settings.black_level,
                           settings.contrast, settings.saturation, settings.highlight_scale,
                           {settings.highlight[0], settings.highlight[1], settings.highlight[2], settings.highlight[3]}};
    dfn.vkCmdPushConstants(cmd, out.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
    dfn.vkCmdDraw(cmd, 3, 1, 0, 0);
    dfn.vkCmdEndRenderPass(cmd);
    dfn.vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    {
      // The refresher must use the device's graphics and compute queue 0.
      const VulkanDevice::Queue::Acquisition queue =
          out.device->AcquireQueue(out.device->queue_family_graphics_compute(), 0);
      if (dfn.vkQueueSubmit(queue.queue(), 1, &si, out.fence[k]) != VK_SUCCESS) return false;
    }
    out.submitted[k] = true;
    return true;
  });
}

}  // namespace outrun::native_vk
