// OutRun native renderer: frames out to the SDK's Vulkan presenter (gpu = vulkan).
//
// The SDK's Vulkan device enables neither external memory nor external
// semaphores, so finished frames cannot be shared with it on the GPU the way
// the Direct3D 12 path shares textures and fences. Instead each frame comes
// back to system memory (the renderer copies it into a readback buffer), is
// copied into one of three host-visible buffers of the presenter's device, and
// a small pass on the presenter's queue turns it into the guest output: the
// gamma ramp and Vivid Colours / Black Level as the Direct3D 12 output pass.
// Kept in its own file: it includes the SDK's Vulkan headers, not Plume's.

#pragma once

#include <cstdint>
#include <string>

namespace outrun::native_vk {

// Whether the SDK presenter runs on Vulkan; its GPU's name (Plume opens the same one).
bool PresenterIsVulkan(std::string* device_name);

// The output pass's settings (as the Direct3D 12 pass's push constants).
struct OutputSettings {
  uint32_t mode = 0;  // 8 gamma ramp, 16 colour adjustment, 32 footer highlight
  float black_level = 0.0f, contrast = 1.0f, saturation = 1.0f;
  float highlight_scale = 1.0f, highlight[4] = {};  // the footer prompt under the mouse, output pixels
};

// Buffers and the pass on the presenter's device for w x h frames.
bool Setup(uint32_t w, uint32_t h);

// Where frame k's B8G8R8A8 pixels go (w * h * 4 bytes, rows packed); waits
// until the presenter's last use of that buffer has finished.
uint8_t* Staging(uint32_t k);

// The game's gamma ramp as 256 entries of R | G << 10 | B << 20 (10-bit).
void SetRamp(const uint32_t* ramp256);

// Hands frame k to the presenter (on the calling thread, like RefreshGuestOutput).
bool Present(uint32_t k, const OutputSettings& settings);

}  // namespace outrun::native_vk
