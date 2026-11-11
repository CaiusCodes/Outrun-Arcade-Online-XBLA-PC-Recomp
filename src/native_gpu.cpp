// OutRun Online Arcade native renderer ("OutRun Plume"): the game's own Direct3D
// calls drawn with Plume (Direct3D 12) and XenosRecomp-translated shaders instead
// of the emulated Xenos GPU. Written after GoldenEye XBLA's and Perfect Dark's
// native renderers (the same approach and much of the same code, copied into
// this project; nothing is read from those projects). Addresses and device
// offsets are OutRun's (XDK 2.0.7978): native/README.md, d3d_functions_outrun.toml.
//// Like Perfect Dark, OutRun keeps the SDK's Xenos graphics system: its
// vblank is the game's clock (fixed_guest_vblank, the network clock sync) and
// its presenter shows the frames. Once the native D3D12 device is up, Swap
// puts the guest D3D in its own "GPU hung" mode (device+10941 bit 2, the same
// bit in the June 2009 XDK: D3D_CBlocker_Check returns at once and the ring is
// no longer kicked), so the Xenos command processor goes idle and every GPU
// wait in the game returns at once; the native Plume backend draws from the
// D3D calls instead and hands each frame to the presenter. If the native
// device cannot start, hung mode is never set and Xenos carries on as usual.
// Every hook runs the original guest function (__imp__sub_*) too.
//
// Shaders come from a local cache of XenosRecomp output (<exe>\native_shaders\
// <hash>.vs|ps.final.dxil, hash = FNV-1a of the shader container), translated
// on this PC from the player's own files; nothing is shipped.
//
// Device offsets and function addresses are OutRun's: up to ~10944 the device
// matches Perfect Dark's June 2009 layout, above ~11000 it sits 168 bytes lower
// (native/offset_map_pd_to_outrun.txt, native/d3d_functions_outrun.toml).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#pragma comment(lib, "gdi32.lib")  // (keyglyphs: key caps drawn with GDI)
#include "menu_pointer.h"
#include "menu_footer.h"

#include <algorithm>
#include <bit>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <cmath>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "generated/default/outrun_init.h"  // PPCContext, generated function declarations
#include "native_shaders.h"
#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging/api.h>
#include <rex/runtime.h>
#include <rex/system/interfaces/graphics.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/d3d12/d3d12_presenter.h>
#include <rex/ui/d3d12/d3d12_provider.h>
#include <rex/ui/image_decode.h>
#include <rex/filesystem.h>
#include <rex/system/xmemory.h>

#include "plume_render_interface.h"
#include "plume_d3d12.h"
#include "plume_vulkan.h"
#include "native_vk_output.h"

#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <filesystem>
#include <fstream>
#include <iterator>

#define XXH_INLINE_ALL
#include <xxhash.h>

// On by default since 0.9.0a (Settings > Graphics > Renderer: Plume / Classic).
REXCVAR_DEFINE_BOOL(gpu_native, true, "GPU",
                    "Renderer: Plume, drawing the game's D3D calls with Direct3D 12 natively (true), or "
                    "Classic, the emulated Xenos GPU (false). Read once at start; Plume uses gpu = d3d12");
REXCVAR_DECLARE(bool, gpu_native_translate);  // pd_native_shaders.cpp
REXCVAR_DEFINE_STRING(gpu_native_skip_ps, "", "GPU",
                      "Native renderer test: skip draws whose pixel shader has this cache hash (16 hex digits)");
REXCVAR_DEFINE_STRING(gpu_native_hide_vs, "", "GPU",
                      "Native renderer test: hide draws whose vertex shader has this cache hash (16 hex digits)");
REXCVAR_DEFINE_STRING(gpu_native_hide, "", "GPU",
                      "Native renderer test: hide draws ""a-b"" of each frame, or triangles ""d:a-b"" of draw d");
REXCVAR_DEFINE_BOOL(gpu_native_debug_layer, false, "GPU",
                    "Native renderer test: enable the D3D12 debug layer and log its messages");
REXCVAR_DEFINE_INT32(gpu_native_trace_frame, 0, "GPU",
                     "Native renderer test: log every draw, clear, resolve and target change of this frame (0 = off)");
REXCVAR_DEFINE_BOOL(gpu_native_f9_trace, false, "GPU",
                    "Tester tool: F9 traces the next two frames into the log (off in releases)");
REXCVAR_DEFINE_INT32(gpu_native_trace_count, 1, "GPU",
                     "Native renderer test: how many frames gpu_native_trace_frame traces, from that frame on");
REXCVAR_DEFINE_BOOL(gpu_native_dump_textures, false, "GPU",
                    "Native renderer test: save every decoded texture as a BMP in native_textures (game content: keep local)");
REXCVAR_DEFINE_BOOL(gpu_native_trace_new_resolves, false, "GPU",
                    "Native renderer test: trace the rest of the frame in which a new resolve destination appears");
REXCVAR_DEFINE_BOOL(gpu_native_prefer_decl, false, "GPU",
                    "Native renderer: lay out vertices from the bound declaration even when the shader's live "
                    "(D3D-patched) fetches can be read");
REXCVAR_DEFINE_BOOL(gpu_native_hide_left_edge, true, "GPU",
                    "Native renderer: show the frame's third pixel column in its first two. The menus' backdrops "
                    "draw a red line down x = 0 (one pixel; two on car and transmission select; Xenos draws it "
                    "too, a TV's overscan hid it)");
REXCVAR_DEFINE_BOOL(gpu_native_half_pixel, false, "GPU",
                    "Native renderer test: shift geometry half a pixel (D3D9 pixel centres); OutRun lines up "
                    "with Xenos without it");
REXCVAR_DEFINE_BOOL(gpu_native_all_mips, false, "GPU",
                    "Native renderer test: build a full mip chain for every texture");
// 4 by default: the game draws its scene 4x multisampled, and the Xenos path
// keeps that (native at 1x showed stair-stepped edges Xenos doesn't have).
REXCVAR_DEFINE_INT32(gpu_native_msaa, 4, "GPU",
                     "Native renderer: scene MSAA samples, 4 (as the game asks, and as the Xenos renderer draws), "
                     "8 (smoother edges), 2 or 1 (off, the lightest); applied at once");
// Settings > Vivid Colors / Contrast / Black Level: the output pass applies
// them each frame (GoldenEye's SDK had these cvars; OutRun's runtime has not).
REXCVAR_DEFINE_DOUBLE(color_saturation, 1.0, "GPU", "Native renderer output: colour saturation (1 = as the game)");
REXCVAR_DEFINE_DOUBLE(color_contrast, 1.0, "GPU", "Native renderer output: contrast (1 = as the game)");
REXCVAR_DEFINE_DOUBLE(color_black_level, 0.0, "GPU", "Native renderer output: black level (0 = as the game)");
REXCVAR_DEFINE_INT32(gpu_native_texture_filtering, 2, "GPU",
                     "Native renderer (Graphics > Texture Filtering): anisotropic filtering of linear, mipmapped "
                     "textures: 0 off, 1 2x, 2 4x, 3 8x, 4 16x; applied at once");
REXCVAR_DEFINE_BOOL(gpu_native_log_creation, false, "GPU",
                    "Native renderer test: log every pipeline and texture created");
REXCVAR_DEFINE_BOOL(gpu_native_bc_textures, true, "GPU",
                    "Native renderer: keep DXT textures compressed (BC1-3) with the game's own mips");
REXCVAR_DEFINE_BOOL(gpu_native_depth_copy, true, "GPU",
                    "Native renderer: copy the scene depth for depth resolves (off: the last clear value)");
REXCVAR_DEFINE_BOOL(gpu_native_skip_resolves, false, "GPU",
                    "Native renderer test: ignore resolves (wrong picture; for timing only)");
REXCVAR_DEFINE_INT32(gpu_native_profile_frame, 0, "GPU",
                     "Native renderer test: GPU-time every draw, clear and resolve of this frame and log the costliest");
REXCVAR_DEFINE_DOUBLE(gpu_native_long_frame_ms, 25.0, "GPU",
                      "Native renderer test: with gpu_native_frame_stats, log frames longer than this");
REXCVAR_DEFINE_BOOL(gpu_native_warm_shaders, true, "GPU",
                    "Native renderer: translate a level's shaders while it loads, not at their first draw");
REXCVAR_DEFINE_BOOL(gpu_native_stage_textures, true, "GPU",
                    "Native renderer: start the texture cache afresh on each new stage (level or front end)");
REXCVAR_DEFINE_BOOL(gpu_native_async_textures, false, "GPU",
                    "Native renderer: prepare new textures past ~1 ms a frame on a worker thread (in level)");
// Off by default here: Perfect Dark keeps the Xenos vblank, which already
// reports every vblank to the presenter (GoldenEye had none to anchor to).
REXCVAR_DEFINE_BOOL(gpu_native_pace, false, "GPU",
                    "Native renderer: present each frame at a fixed moment after the frame's usual arrival");
REXCVAR_DEFINE_INT32(gpu_native_pace_margin_us, 2000, "GPU",
                     "Native renderer: how long after a frame's usual arrival it is presented (paced presents)");
REXCVAR_DEFINE_INT32(gpu_native_frame_stats, 0, "GPU",
                     "Log guest frame times (Swap to Swap) every this many frames, native or Xenos (0 = off)");
REXCVAR_DEFINE_INT32(gpu_native_debug_clear, 0, "GPU",
                     "Native renderer test: clear to this 0xRRGGBB instead of the game's colour (0 = off)");
REXCVAR_DEFINE_INT32(gpu_native_test_timer, 0, "GPU",
                     "Native renderer test: from this frame (in a race), find the countdown in the game's data "
                     "and keep it from running out, so a background run reaches every stage (0 = off)");
REXCVAR_DEFINE_DOUBLE(gpu_native_test_speed, 1.0, "GPU",
                      "Native renderer test: run the game clock this much faster (2 to 4) so background runs reach "
                      "later stages sooner; 1 = normal");
REXCVAR_DEFINE_INT32(gpu_native_dump_resolves, 0, "GPU",
                     "Native renderer test (Vulkan): at the end of this frame, save every colour resolve's image "
                     "as raw BGRA in native_dumps beside the exe (game content: keep local, delete after)");
REXCVAR_DEFINE_INT32(gpu_native_query_test, 0, "GPU",
                     "Native renderer test: 1 = every occlusion query reads fully visible, 2 = draws inside a "
                     "query write colour (to see them)");
// Settings > HUD (pc-settings.toml): the in-race HUD's textures as the game has
// them, smoothly enlarged and sharpened, redrawn (resources\hud, over the
// enlarged picture parts) or with the race HUD cleared. Applied at once.
REXCVAR_DEFINE_INT32(outrun_hud, 0, "OutRun/Graphics",
                     "In-race HUD: 0 Original, 1 Sharp (enlarged and sharpened), 2 HD (redrawn), 3 Off").range(0, 3);
REXCVAR_DEFINE_BOOL(gpu_native_log_hud, false, "GPU",
                    "Native renderer test: log each texture cell the HUD sprite shader draws (content hash, texture "
                    "coordinates, screen box), once each");

namespace outrun {
// Texture Filtering: sampler_slot applies this anisotropy (GoldenEye's
// anisotropic_override setting; 3 = 4x) to linear, mipmapped sampling.
std::atomic<int> g_native_aniso{3};
int NativeAnisotropy() { return g_native_aniso.load(std::memory_order_relaxed); }
// The Render Scale this run (pd_render_scale -> resolution_scale, set in OnPreSetup).
int ActiveRenderScale() {
  const std::string v = rex::cvar::GetFlagByName("resolution_scale");
  const int s = v.empty() ? 1 : std::atoi(v.c_str());
  return s >= 1 && s <= 3 ? s : 1;
}
}  // namespace outrun

namespace plume {
std::unique_ptr<RenderInterface> CreateD3D12Interface();
std::unique_ptr<RenderInterface> CreateVulkanInterface();
}

// Plume's Vulkan swap chain code names it; the SDK's volk, which Plume shares
// (PLUME_EXTERNAL_VOLK), was built without Win32 surfaces, and the renderer
// never makes a swap chain of its own.
extern "C" PFN_vkCreateWin32SurfaceKHR vkCreateWin32SurfaceKHR = nullptr;

// For the test pad's route (test_script.h): checkpoints passed, seen as the held
// countdown going up (test_timer), and game seconds since the last one (or since it was found).
std::atomic<int> g_test_checkpoints{-1};
std::atomic<double> g_test_stage_seconds{0.0};
std::atomic<double> g_fps{0.0};  // guest frames per second (frame_stats), for Show FPS
// The periodic statistics lines (frame, pools, video memory, queue lag, GPU
// time): once a minute at 60 fps, so the player log stays short.
constexpr uint64_t kStatsFrames = 3600;
namespace outrun::player_log { void OnSwap(); std::string Where(); }

using namespace plume;

namespace {

// --- guest memory -----------------------------------------------------------

inline uint8_t* gmem() { return rex::system::kernel_state()->memory()->virtual_membase(); }
// A guest address as the recompiled code reads it: on Windows the SDK maps the 0xE0000000+
// physical heap 4 KB on (rex::memory PhysicalHostOffset). OutRun keeps D3D objects there.
inline uint8_t* gptr(uint32_t a) { return gmem() + a + (a >= 0xE0000000u ? 0x1000u : 0u); }
inline uint32_t g32(uint32_t a) {
  uint32_t v;
  std::memcpy(&v, gptr(a), 4);
  return __builtin_bswap32(v);
}
inline uint16_t g16(uint32_t a) {
  uint16_t v;
  std::memcpy(&v, gptr(a), 2);
  return __builtin_bswap16(v);
}
// Physical memory at 0xE0000000+ is seen 4 KB further on than the raw guest
// address reads (see ge_native_probe.cpp); shader microcode pointers need it.
inline uint32_t gpu_view(uint32_t a) { return a; }  // gptr applies the 0xE0000000+ host offset now

uint64_t fnv1a(const uint8_t* p, size_t n) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 0x100000001B3ull; }
  return h;
}

void create_scene_targets(const RenderMultisampling& msaa, uint32_t rt = 0, uint32_t w = 0, uint32_t h = 0);
RenderSampleCounts pick_msaa();

// Device fields (offsets into the guest D3D device; see native/d3d_functions.toml).
constexpr uint32_t kDevVsConsts = 1920;     // 256 float4, big-endian
constexpr uint32_t kDevPsConsts = 6016;     // 256 float4
constexpr uint32_t kDevVsBools = 2528 * 4;  // dword index 2528
constexpr uint32_t kDevPsBools = 2532 * 4;
constexpr uint32_t kDevVertexDecl = 11824;
constexpr uint32_t kDevPixelShader = 12700;
constexpr uint32_t kDevVertexShader = 12704;
constexpr uint32_t kDevViewport = 12656;    // X, Y, W, H, MinZ, MaxZ (floats)
// PA_SC_WINDOW_SCISSOR_TL / _BR as the guest D3D writes them (x | y << 16): the
// viewport, cut to the scissor rect while ScissorTestEnable (dev+11864) is on.
constexpr uint32_t kDevWindowScissorTL = 10436;
constexpr uint32_t kDevWindowScissorBR = 10440;

// --- native device ----------------------------------------------------------

// The texture cache starts afresh on a new stage (gpu_native_stage_textures).
// OutRun's course/menu change signal is not wired yet: one stage for now.
inline uint32_t stage_number() { return 0; }

constexpr uint32_t kTextureSlots = 4096;  // a race into stage 2 held ~1000 textures before eviction
// Sampler descriptors (set 3); 0 = the default sampler. 64 ran out by Casino Town (a run's 4th stage):
// later fetch states fell back to the default (clamped) sampler, so the sky dome showed its edge texels as
// stripes in some frames. Plume's shader-visible sampler heap holds 1024.
constexpr uint32_t kSamplerSlots = 768;
constexpr uint32_t kFrames = 3;  // frames in flight: a GPU clock stall doesn't block the game thread
// System memory. OutRun's busiest frame (a race loading) used 4.3 MB of its third and a race frame
// under 1 MB, so 32 MB a frame leaves room for the largest texture with mips; more than that in one
// frame is submitted part-way (flush_mid_frame). (Perfect Dark's 288 MB was most of OutRun's extra memory.)
constexpr uint64_t kUploadSize = 96ull << 20;
constexpr uint64_t kSlotUpload = kUploadSize / kFrames;
constexpr uint64_t kSlotConsts = 4ull << 20;  // shader constants per frame (OutRun's race peaks at 1.3 MB)
// The game draws for a 1280x720 EDRAM frame (guest pixels); the scene targets
// are that times the Render Scale (outrun::ActiveRenderScale, set at device start).
// OutRun boots 1280x720 (16:9) or 1024x768 (4:3, Aspect Ratio): pc_menu.cpp sets the
// video mode before the runtime starts, and ensure_device reads it once.
uint32_t kGuestW = 1280, kGuestH = 720;
// The native renderer's API this run: the SDK presenter's (gpu = d3d12, or
// vulkan: frames then reach the presenter through system memory, native_vk_output.h).
bool g_vulkan = false;

// Whether Plume made the pipeline (it returns an object either way).
bool pipeline_ok(const RenderPipeline* p) {
  if (!p) return false;
  return g_vulkan ? static_cast<const VulkanGraphicsPipeline*>(p)->vk != VK_NULL_HANDLE
                  : static_cast<const D3D12GraphicsPipeline*>(p)->d3d != nullptr;
}
uint32_t g_scene_scale = 1, g_scene_w = kGuestW, g_scene_h = kGuestH;
// gpu_native_hide_left_edge: guest pixel columns at x = 0 the menus draw red.
constexpr uint32_t kHiddenEdgeColumns = 2;
constexpr RenderFormat kSceneFormat = RenderFormat::B8G8R8A8_UNORM;
// Depth and stencil as the 360 has them (OutRun's depth surfaces are D24S8: RB_DEPTH_INFO format 0).
// Plume makes it R24G8_TYPELESS, so the depth resolve can read it (R24_UNORM_X8 view); half the size
// of a D32_FLOAT_S8 target. Vulkan on AMD has no D24S8 depth target: D32_FLOAT_S8 there (ensure_device).
RenderFormat kDepthFormat = RenderFormat::D24_UNORM_S8_UINT;
// A texture compute passes write: an unordered-access view in Direct3D 12,
// storage usage in Vulkan (Plume keeps the two flags apart).
constexpr RenderTextureFlags kStorageFlags = RenderTextureFlag::UNORDERED_ACCESS | RenderTextureFlag::STORAGE;

// A vertex input. For declarations: stream = D3D stream, type = D3DDECLTYPE.
// For a shader's own vertex fetches: stream = vertex fetch constant slot, type
// = Xenos format | signed << 8 | integer << 9 (the same bits D3DDECLTYPE uses).
constexpr uint16_t kIdentitySwizzle = 0x688;  // vfetch dst swizzle x,y,z,w (3 bits each)

// Vulkan matches vertex inputs by location: XenosRecomp's [[vk::location]] for
// a D3D declaration usage (USAGE_LOCATIONS in shader_recompiler.cpp).
uint32_t vk_location(uint32_t usage, uint32_t index) {
  switch (usage * 16 + index) {
    case 0 * 16 + 0: return 0;    // Position
    case 0 * 16 + 1: return 15;
    case 3 * 16 + 0: return 1;    // Normal
    case 6 * 16 + 0: return 2;    // Tangent
    case 7 * 16 + 0: return 3;    // Binormal
    case 5 * 16 + 0: case 5 * 16 + 1: case 5 * 16 + 2: case 5 * 16 + 3: return 4 + index;  // TexCoord 0-3
    case 10 * 16 + 0: return 8;   // Color
    case 2 * 16 + 0: return 9;    // BlendIndices
    case 1 * 16 + 0: return 10;   // BlendWeight
    case 10 * 16 + 1: return 11;  // Color 1
    case 5 * 16 + 4: case 5 * 16 + 5: case 5 * 16 + 6: case 5 * 16 + 7: return 12 + (index - 4);  // TexCoord 4-7
    // Any other usage (patches/xenosrecomp.patch gives them 16-31 the same way).
    default: return 16 + ((usage * 4 + (index & 3)) % 16);
  }
}

struct Element {
  uint16_t stream, offset;
  uint32_t type;
  uint8_t usage, index;
  int8_t exp = 0;  // vfetch exponent adjust: values scale by 2^exp
  // Destination swizzle D3D patched into the live vfetch (e.g. BGRA for a
  // D3DCOLOR) where the shader was compiled from an identity template:
  // applied while decoding, since the translated shader doesn't know it.
  uint16_t swizzle = kIdentitySwizzle;
};

struct HostShader {
  std::unique_ptr<RenderShader> shader;
  std::vector<Element> inputs;                  // vertex shaders: from the microcode
  std::vector<uint8_t> container;               // vertex shaders: for re-parsing live microcode
  uint32_t cb_bytes = 4096;                     // constant-buffer bytes the shader reads (<hash>.cbsize)
  std::unordered_map<uint16_t, uint32_t> strides;  // fetch slot -> stride in bytes
  uint64_t hash = 0;
  bool missing = false;
  bool live_inputs = false;  // inputs read from the live microcode (D3D's patched fetches) at the last draw
};

struct TextureJob;
struct ResolvedTexture {
  std::unique_ptr<RenderTexture> texture;
  std::unique_ptr<RenderTextureView> view;  // cube maps: the cube view in the descriptor
  uint32_t w = 0, h = 0;                     // colour resolves: the copy's size
  // A colour resolve into a cube map (OutRun's car reflection: six faces drawn
  // each frame, each resolved into its own face of one texture).
  bool cube = false;
  // Colour resolves sampled through a guest texture whose fetch swizzle forces
  // a component to 0 or 1 (X8R8G8B8: alpha 1): a view with that mapping each.
  std::unordered_map<uint16_t, std::pair<std::unique_ptr<RenderTextureView>, uint32_t>> swizzled;
  uint32_t slot = 0;
  std::shared_ptr<TextureJob> job;  // being prepared on the texture worker
  // Textures outside the CAFF heap can be rewritten by the game (classic Dam's
  // 4x4 sky colour ramp): their bytes are re-hashed once a frame when used.
  bool dynamic = false;
  int8_t keyboard = -1;  // the menu atlas: 1 with keyboard keys in its button icons, 0 without; -1 other
  // A HUD texture (hud::kTextures): the Settings > HUD mode it was made for
  // (made again when that changes), and its mip levels when enlarged (its
  // sampler then uses them, though the game samples the base level only).
  int8_t hud_mode = -1;
  uint8_t hud_mips = 0;
  bool hud_pending = false;  // the mode's texture is still being made (hud::build): the game's for now
  uint32_t addr = 0, span = 0;
  uint64_t content = 0, checked_frame = 0;
  // Full-size depth resolve not copied yet (copy_pending_depth), and the last
  // frame a draw sampled this depth texture (0: never).
  bool depth_pending = false;
  uint64_t depth_used_frame = 0;
  // The depth target (of its set: sets outlive a frame) the resolve reads,
  // whichever set is bound when the copy is made, and whether it is multisampled.
  RenderTexture* depth_src = nullptr;
  bool depth_src_msaa = false;
  // A depth resolve's D24S8 words as an A8R8G8B8 texture samples them (the race
  // reads its depth that way to rebuild it in EDRAM): made from `texture` when
  // sampled through such a view (depth_words_slot).
  bool is_depth = false;
  std::unique_ptr<RenderTexture> words;
  std::unique_ptr<RenderDescriptorSet> words_set;
  RenderTexture* words_src = nullptr;
  uint32_t words_slot = 0, words_span = 0, words_w = 0, words_h = 0;
  uint64_t words_content = 0;
};

// A piece of a pooled GPU buffer (vertex or index cache entries). Each committed
// buffer costs 64 KB at least and ~0.5 ms to create: Dam's level load made 500
// of them in one frame.
struct GpuAlloc {
  RenderBuffer* buffer = nullptr;
  uint64_t offset = 0;
  uint32_t size_class = 0;  // capacity = class_bytes(size_class)
  explicit operator bool() const { return buffer != nullptr; }
};

// Size classes: four per doubling from 256 bytes (at most ~19% rounding,
// against up to 2x with powers of two).
constexpr uint32_t kSizeClasses = 128;
inline uint64_t class_bytes(uint32_t c) {
  const uint64_t base = 256ull << (c / 4);
  return base + (base / 4) * (c % 4);
}

struct BufferPool {
  uint64_t chunk = 32ull << 20;
  RenderBufferFlags flags = RenderBufferFlag::NONE;
  std::vector<std::unique_ptr<RenderBuffer>> chunks;
  uint64_t bump = UINT64_MAX;  // into chunks.back(); MAX = no chunk yet
  std::vector<GpuAlloc> free_lists[kSizeClasses];
};

struct Native {
  std::unique_ptr<RenderInterface> api;
  std::unique_ptr<RenderDevice> device;
  std::unique_ptr<RenderCommandQueue> queue;
  // Two frames in flight: frame N records into slot N % 2 while the GPU may
  // still run frame N-1. A slot's list, fence and half of the upload ring are
  // reused only after waiting for its fence.
  std::unique_ptr<RenderCommandList> cmds[kFrames];
  std::unique_ptr<RenderCommandFence> fences[kFrames];
  RenderCommandList* cmd = nullptr;
  RenderCommandFence* fence = nullptr;
  uint32_t slot = 0;
  bool pending[kFrames] = {};  // a submission signalling that slot's fence not waited for yet (Plume's
                         // wait blocks on an event: never wait without one, or twice)
  struct Retired { uint64_t frame; BufferPool* pool; GpuAlloc alloc; };
  std::vector<Retired> retired;  // returned to their pool 2 frames later
  struct RetiredTexture { uint64_t frame; std::unique_ptr<RenderTexture> texture; uint32_t slot; };
  std::vector<RetiredTexture> retired_textures;  // re-uploaded dynamic textures: freed kFrames frames later
  struct RetiredSet { uint64_t frame; std::unique_ptr<RenderDescriptorSet> set; };
  std::vector<RetiredSet> retired_sets;  // depth resolve sets remade for another source: likewise
  std::vector<uint32_t> free_slots;
  BufferPool vertex_pool, index_pool;
  std::unique_ptr<RenderCommandSemaphore> acquire;
  std::vector<std::unique_ptr<RenderCommandSemaphore>> release;
  std::vector<std::unique_ptr<RenderFramebuffer>> swap_framebuffers;

  // Presenter output: finished frames go to the SDK
  // presenter, which has a D3D12 device of its own (window, ImGui overlays,
  // FSR), through textures and fences shared between the two devices. Ours:
  // copy frame k into tex[k], signal `ready`. The presenter's queue: wait for
  // `ready`, convert tex[k] into its guest output, signal `done`; tex[k] is
  // written again only after that.
  struct Out {
    rex::ui::Presenter* presenter = nullptr;
    ID3D12Device* pdev = nullptr;  // the presenter's device and direct queue
    ID3D12CommandQueue* pqueue = nullptr;
    ID3D12Resource* tex[3] = {};   // shared frames, opened by our device ...
    ID3D12Resource* ptex[3] = {};  // ... and by the presenter's
    ID3D12Fence* ready = nullptr;  // created by ours
    ID3D12Fence* ready_p = nullptr;
    ID3D12Fence* done_p = nullptr;  // created by the presenter's
    ID3D12Fence* done = nullptr;
    uint64_t ready_value = 0, done_value = 0;
    uint64_t tex_ready[3] = {}, tex_done[3] = {}, list_done[3] = {};
    ID3D12CommandAllocator* alloc[3] = {};
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12RootSignature* root = nullptr;
    ID3D12PipelineState* pso = nullptr;
    ID3D12DescriptorHeap* heap = nullptr;
    UINT heap_step = 0;
    uint32_t next = 0, next_list = 0;
    // The game's gamma ramp as a 256 x 1 texture (one upload buffer per list).
    ID3D12Resource* ramp = nullptr;
    ID3D12Resource* ramp_upload[3] = {};
    uint32_t ramp_version = 0;
    bool ramp_ready = false;
    HANDLE event = nullptr;
    // Vulkan: frame k copied into readback[k] by the frame's list, then into
    // the presenter's buffer (native_vk_output.h) once the frame has finished.
    std::unique_ptr<RenderBuffer> readback[3];
    uint8_t* readback_ptr[3] = {};
    uint32_t vk_ramp_version = 0;
  } out;
  // D3DDevice_SetGammaRamp: the display gamma ramp the Xenos path applies to
  // every frame on the way out (its 256-entry table, for 8_8_8_8 front buffers).
  uint16_t gamma_ramp[256 * 3] = {};  // R, G, B; 16-bit values
  uint32_t gamma_ramp_version = 0;

  std::unique_ptr<RenderPipelineLayout> layout;
  std::unique_ptr<RenderDescriptorSet> textures;
  std::unique_ptr<RenderDescriptorSet> samplers;
  std::unique_ptr<RenderSampler> default_sampler;
  std::unique_ptr<RenderTexture> white;
  // 1x1 white cube map: what TextureCube descriptor indices point at when the
  // sampler has no cube texture (slot 0 is the 2D white).
  std::unique_ptr<RenderTexture> white_cube;
  std::unique_ptr<RenderTextureView> white_cube_view;
  uint32_t white_cube_slot = 0;
  std::unique_ptr<RenderBuffer> upload;
  // Shader constants: written to a per-slot upload range, copied in one go into
  // a GPU-local buffer by a short list submitted just before the frame's list
  // (the draws' root CBVs point at the GPU-local copy, not at system memory).
  std::unique_ptr<RenderBuffer> const_upload;
  uint8_t* const_ptr = nullptr;
  std::unique_ptr<RenderBuffer> const_gpu[kFrames];
  std::unique_ptr<RenderCommandList> const_lists[kFrames];
  uint64_t const_used = 0;
  std::unordered_map<uint64_t, uint64_t> const_dedup;  // block hash -> offset, this submission
  std::unique_ptr<RenderQueryPool> timestamps;  // GPU time per frame: [0] frame start, [1] end
  // One-frame profile (gpu_native_profile_frame): a timestamp after every
  // operation; the time since the previous one is that operation's.
  std::unique_ptr<RenderQueryPool> prof_pool;
  std::vector<std::string> prof_labels;
  bool prof_active = false;
  double gpu_ms_sum = 0, gpu_ms_max = 0, copy_ms_sum = 0;
  uint32_t gpu_frames = 0;
  uint8_t* upload_ptr = nullptr;
  uint64_t upload_used = 0;  // within the current slot's half
  uint64_t upload_peak = 0, const_peak = 0;  // highest use of a slot's share (600-frame log)
  uint32_t flushes_600 = 0;

  RenderMultisampling msaa;        // the most a target set gets (gpu_native_msaa)
  RenderMultisampling scene_msaa;  // the bound set's: msaa for the 360's 4x surfaces, 1 for its 1x ones
  // Render targets. The game's scene target (bound with a depth surface) is
  // the MSAA scene above; a target bound without depth (the post-process pass
  // into 400076A0) gets a single-sample colour target of its own, per guest
  // surface. bind_target() makes the bound one match what the game has set.
  // A single-sampled set uses its surface's colour target as its colour: the
  // 360 has one copy of a surface in EDRAM however it is bound (the race's
  // bloom is added to the final picture with the depth surface unbound).
  struct ColorTarget {
    std::unique_ptr<RenderTexture> color;
    std::unique_ptr<RenderFramebuffer> fb;
  };
  std::unordered_map<uint32_t, ColorTarget> color_targets;
  uint32_t guest_rt = 0, guest_depth = 0, front_buffer = 0, scene_surface = 0;
  RenderFramebuffer* bound_fb = nullptr;
  RenderTexture* bound_color = nullptr;
  bool bound_scene = true;
  uint32_t last_resolve_dest = 0, last_resolve_face = 0;
  bool draws_since_resolve = true;
  // Scene depth since its last clear: the clear value, and whether a draw has
  // written depth since (then a depth resolve would need the real contents).
  float depth_clear = 1.0f;
  bool depth_written = true;
  // A resolve's clears (flags 0x100 / 0x200) waiting for the scene to be used
  // again (apply_pending_clears): Perfect Dark resolves the scene with them,
  // then binds a second target on the same EDRAM and resolves that to the
  // front buffer with nothing drawn, so that copy has to see the picture.
  bool pending_clear_color = false, pending_clear_depth = false;
  RenderColor pending_color;
  float pending_depth = 1.0f;
  // The set those clears came from (the resolve's source). Applied to another
  // set (one at the same EDRAM address), they are still owed to the source,
  // which gets them when it is next bound: in OutRun's stage transitions the
  // resolve's clear is the scene depth's only clear in a frame (without it the
  // road and the car failed the depth test against the frame before).
  uint64_t pending_src_key = 0;
  struct OwedClear { bool color = false, depth = false; RenderColor color_value; float depth_value = 1.0f; };
  std::unordered_map<uint64_t, OwedClear> clear_on_bind;
  // Bumped by every depth-writing draw and depth clear: a full depth resolve
  // whose destination already holds this generation is skipped (the game
  // resolves the same depth more than once a frame).
  uint64_t depth_gen = 1;
  uint32_t depth_copies = 0;  // full depth copies, for the 600-frame log line
  std::vector<uint32_t> depth_pending;  // destinations with ResolvedTexture::depth_pending
  // Depth resolve pass (native_depth_resolve): sample 0 of the scene depth
  // into an R32F texture per destination; [1] reads a multisampled depth.
  std::unique_ptr<RenderPipelineLayout> depth_layout;
  std::unique_ptr<RenderShader> depth_shader[2];
  std::unique_ptr<RenderPipeline> depth_pipe[2];
  std::unique_ptr<RenderTextureView> depth_view;  // shader view of the scene depth (depth only)
  bool depth_pass_failed = false;
  std::unordered_map<uint32_t, std::unique_ptr<RenderDescriptorSet>> depth_sets;
  std::unique_ptr<RenderShader> words_shader;  // depth -> D24S8 words (depth_words_slot)
  std::unique_ptr<RenderPipeline> words_pipe;
  // EDRAM colour -> depth (apply_pending_restore). The race rebuilds a depth
  // surface by drawing its resolved depth, as words, into a colour surface at
  // the same EDRAM address. edram_color: EDRAM base -> the colour surface last
  // drawn there alone this frame; restore_rt: the one the bound depth is to be
  // rebuilt from before it is next used.
  std::unordered_map<uint32_t, uint32_t> edram_color;
  uint32_t restore_rt = 0;
  std::unique_ptr<RenderPipelineLayout> restore_layout;
  std::unique_ptr<RenderShader> restore_vs, restore_ps;
  std::unique_ptr<RenderPipeline> restore_pipe[5];  // by sample count
  std::unordered_map<uint32_t, std::unique_ptr<RenderDescriptorSet>> restore_sets;  // by colour surface
  bool restore_failed = false;
  std::unique_ptr<RenderTexture> scene;  // the bound set's own colour (none when it shares a colour target)
  RenderTexture* scene_color = nullptr;  // the bound set's colour: scene, or its surface's colour target
  uint32_t scene_tw = 0, scene_th = 0;   // the bound set's size (larger than the scene for a big depth-only pass)
  uint32_t bound_w = 0, bound_h = 0;     // the bound target's size
  std::unique_ptr<RenderTexture> scene_depth;
  std::unique_ptr<RenderFramebuffer> scene_fb;
  // OutRun draws several passes with a depth buffer (reflections, the car
  // preview, glare at 640x360 / 320x180 / 256x256), not one scene as Perfect
  // Dark: each guest colour+depth pair gets its own scene-sized set, swapped
  // into scene / scene_depth / scene_fb / depth_view while it is bound, so a
  // small pass no longer draws over the top-left of the main picture. A set is
  // multisampled only when the guest surface is (RB_SURFACE_INFO at +24, bits
  // 16-17): the race's scene tiles are 4x, its reflection, bloom, glare and
  // final passes 1x (a 4x set with depth is 44 MB at 1280x720).
  struct SceneSet {
    std::unique_ptr<RenderTexture> color, depth;  // color: none when it shares a colour target
    RenderTexture* color_ptr = nullptr;
    uint32_t w = 0, h = 0;
    std::unique_ptr<RenderFramebuffer> fb;
    std::unique_ptr<RenderTextureView> depth_view;
    RenderMultisampling msaa;
  };
  std::unordered_map<uint64_t, SceneSet> scene_sets;  // parked sets, by rt << 32 | depth
  uint64_t scene_key = 0;
  bool scene_key_set = false;

  std::unordered_map<uint32_t, HostShader> shaders;            // guest shader object
  std::unordered_map<uint32_t, uint32_t> created_blobs;        // object -> container (boot shaders)
  // Shaders a level registered in place (warm_registered_shaders): object ->
  // vertex?, the hash its bytes had last frame and the frame it was registered.
  struct WarmEntry { bool vertex; uint64_t last_hash; uint64_t frame; };
  std::unordered_map<uint32_t, WarmEntry> warm;
  uint32_t warm_requested = 0;
  std::unordered_map<uint64_t, std::unique_ptr<RenderPipeline>> pipelines;
  std::mutex warm_mutex;  // pipelines the boot worker made, for begin_frame to take in
  std::vector<std::pair<uint64_t, std::unique_ptr<RenderPipeline>>> warm_made;
  std::unordered_map<uint32_t, ResolvedTexture> resolved;      // guest texture -> host copy
  // Resolve destinations by their memory (fetch constant base address): another
  // texture object made over the same memory samples the resolved picture too.
  // In hung mode nothing writes resolves to guest memory, so decoding such a
  // texture from memory gave black (the Perfect Menu's blurred background).
  std::unordered_map<uint32_t, uint32_t> resolved_base;      // base address -> guest texture in `resolved`
  std::unordered_map<uint64_t, ResolvedTexture> textures_by_fetch;  // fetch constant hash -> upload
  uint32_t textures_stage = 0;  // the stage textures_by_fetch was filled in (begin_frame)
  std::vector<std::unique_ptr<RenderSampler>> sampler_objects;
  std::unordered_map<uint64_t, uint32_t> sampler_slots;        // filter/clamp/mip key -> sampler slot
  // Decoded vertex data kept on the GPU between frames (see cached_vertices).
  struct VertexCacheEntry {
    GpuAlloc buffer;
    uint64_t capacity = 0, content = 0, last_frame = 0;
    uint32_t misses = 0;  // content changes seen: past a few, decoded into the ring every time
  };
  std::unordered_map<uint64_t, VertexCacheEntry> vertex_cache;
  // Converted 32-bit index lists of indexed draws, the same way (index fetch from
  // the upload heap, i.e. system memory, made Dam's big draws slow on the GPU).
  struct IndexCacheEntry {
    GpuAlloc buffer;
    uint64_t capacity = 0, content = 0, last_frame = 0;
    uint32_t misses = 0, count = 0, vcount = 0;
    int64_t lo = 0;
    RenderPrimitiveTopology topo = RenderPrimitiveTopology::TRIANGLE_LIST;
  };
  std::unordered_map<uint64_t, IndexCacheEntry> index_cache;
  uint32_t vcache_hits = 0, vcache_misses = 0, last_vcache_hits = 0, last_vcache_misses = 0;
  uint32_t next_slot = 1;

  bool failed = false;
  bool recording = false;
  bool layout_bound = false;  // root signature + descriptor tables set on the current list
  uint64_t frames = 0;
  uint32_t draws = 0, skipped = 0;
  uint32_t skip_reason[8] = {};  // shader, decl, prim, range, stream, pipeline, upload
  // Creation work on the game thread this frame (and the last presented one).
  double pipe_ms = 0, tex_ms = 0, last_pipe_ms = 0, last_tex_ms = 0;
  double sect[5] = {}, last_sect[5] = {};
  double present_ms[4] = {};  // last present: acquire, submit, present, fence wait
  double tex_decode_ms = 0, tex_mip_ms = 0, last_tex_decode_ms = 0, last_tex_mip_ms = 0;
  double vdecode_ms = 0, last_vdecode_ms = 0;
  double flush_ms = 0, alloc_ms = 0, hash_ms = 0, last_flush_ms = 0, last_alloc_ms = 0, last_hash_ms = 0;
  uint32_t flushes = 0, last_flushes = 0;  // draw path: parse, indices, decode, state+submit, total
  uint32_t pipe_n = 0, tex_n = 0, last_pipe_n = 0, last_tex_n = 0;
  uint32_t tex_requests = 0;  // new textures asked for this frame (many = a level load)
  // Present pacing and its statistics.
  double pace_period = 1000.0 / 60.0, pace_next = 0, pace_last_arrival = 0, pace_waited_ms = 0;
  double pace_last_present = 0, pace_wait_sum = 0;
  std::vector<double> present_intervals;
  uint32_t last_counter = 0, double_steps = 0;
};

Native g;

// Occlusion queries (see query_begin): per guest query object, the last result.
constexpr uint32_t kMaxQueries = 512;
struct Occlusion {
  ID3D12QueryHeap* heap = nullptr;
  ID3D12Resource* readback = nullptr;
  const uint64_t* mapped = nullptr;
  bool failed = false;
  std::unordered_map<uint32_t, uint32_t> slot_of;  // guest query object -> slot
  // Per slot: the last finished result in guest samples (UINT32_MAX: none yet).
  // OutRun's sun glare scales with the count (0x820EA36C: up to 1280x720x4),
  // so host samples are scaled to the 360's 4x scene at 1280x720 (factor).
  std::vector<uint32_t> samples;
  std::vector<float> factor;
  std::vector<uint32_t> owner;                     // per slot: its guest query object
  uint32_t next_reuse = 0;                         // slots handed on round-robin once all are taken
  std::vector<uint32_t> ended[kFrames];            // per frame slot: query slots resolved in it
  int32_t open = -1;                               // slot begun in the list being recorded
  uint32_t begins = 0, reads = 0;
  // Vulkan: a query must be reset before each use and begin and end inside one
  // render pass instance. Each begin takes the next index of its frame slot's
  // range (reset when the frame starts); the query itself begins at the next
  // draw, inside the pass, and ends at the game's end or when the pass ends.
  VkQueryPool vk_pool = VK_NULL_HANDLE;
  uint32_t vk_next[kFrames] = {};
  bool vk_reset[kFrames] = {};
  uint32_t vk_index = 0;
  bool vk_begun = false;
  std::vector<std::pair<uint32_t, int32_t>> vk_ended[kFrames];  // (query slot, pool index; -1: no draws, 0 samples)
} g_occ;

// Test flight recorder (gpu_native_trace_new_resolves): every frame is traced
// into a ring of the last 1500 lines, written to the log when a new resolve
// destination appears (so the frames that led up to it can be read).
std::deque<std::string> g_trace_ring;
inline bool recording_trace() { return REXCVAR_GET(gpu_native_trace_new_resolves); }
inline bool tracing() {
  if (recording_trace()) return true;
  const int32_t f = REXCVAR_GET(gpu_native_trace_frame);
  return f > 0 && g.frames + 1 >= uint64_t(f) && g.frames + 1 < uint64_t(f) + uint64_t(std::max(1, REXCVAR_GET(gpu_native_trace_count)));
}
// Player marks (F8, player_log.cpp): the next frame traced into a file
// beside the log instead of into it. Requested from the game thread, opened
// and closed by present_frame on the GPU thread.
std::mutex g_mark_trace_mutex;
std::string g_mark_trace_request;            // path asked for (empty: none)
std::FILE* g_mark_trace_file = nullptr;
std::string g_mark_trace_path;
uint64_t g_mark_trace_frame = 0, g_mark_trace_bytes = 0;
uint32_t g_mark_trace_lines = 0, g_mark_trace_draws = 0;
constexpr uint64_t kMarkTraceCap = 8u << 20;  // bytes; the rest of a frame is cut
void trace_line(std::string s) {
  if (g_mark_trace_file) {
    if (g_mark_trace_bytes < kMarkTraceCap) {
      s += char(10);
      std::fwrite(s.data(), 1, s.size(), g_mark_trace_file);
      g_mark_trace_bytes += s.size();
      ++g_mark_trace_lines;
      if (s.rfind("draw#", 0) == 0) ++g_mark_trace_draws;
    }
    return;
  }
  if (!recording_trace()) { REXKRNL_INFO("ORNATIVE trace {}", s); return; }
  g_trace_ring.push_back(std::move(s));
  if (g_trace_ring.size() > 12000) g_trace_ring.pop_front();
}
void dump_trace_ring(const char* why) {
  REXKRNL_INFO("ORNATIVE flight recorder ({}): {} lines", why, g_trace_ring.size());
  for (const std::string& s : g_trace_ring) REXKRNL_INFO("ORNATIVE rec {}", s);
  g_trace_ring.clear();
}
#define NTRACE(...) do { if (tracing()) trace_line(fmt::format(__VA_ARGS__)); } while (0)

struct Stream {
  uint32_t vb = 0, offset = 0, stride = 0;
};
Stream g_streams[16];
uint32_t g_textures[32];                                   // guest texture per sampler
std::atomic<uint32_t> g_clear_color{0xFF000000u};
uint32_t g_spec_bits = 2;
uint32_t g_vs_cb_bytes = 4096, g_ps_cb_bytes = 3584;  // what the current draw's shaders read  // shared c17.y: bit 0 R11G11B10 normals, bit 1 alpha test

std::wstring exe_dir() {
  wchar_t path[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring s(path, path + n);
  const size_t slash = s.find_last_of(L"\\/");
  return slash == std::wstring::npos ? L"." : s.substr(0, slash);
}

void flush_mid_frame();
void warm_pipelines();
uint32_t alloc_texture_slot();
void close_open_query();
void harvest_queries(uint32_t s);
void begin_vulkan_query();
void reset_vulkan_queries();

// Makes room for `size` more bytes of upload data, submitting the frame so far
// and waiting for it if the ring is too full. Only call it where no command
// state for the current draw has been recorded yet (the start of a draw, or a
// texture upload done before the draw's own allocations).
void ensure_room(uint64_t size) {
  if (g.upload_used + size + 4096 > kSlotUpload || g.const_used + 16384 > kSlotConsts) flush_mid_frame();
}

uint64_t const_alloc(uint64_t size) {
  const uint64_t off = (g.const_used + 255) & ~255ull;
  if (off + size > kSlotConsts) return UINT64_MAX;
  g.const_used = off + size;
  g.const_peak = std::max(g.const_peak, g.const_used);
  return off;
}

// Submits the current slot's list, preceded by the copy of this submission's
// constants into the GPU-local constant buffer.
void submit_frame_lists(RenderCommandSemaphore** waits, uint32_t wait_count, RenderCommandSemaphore** signals,
                        uint32_t signal_count) {
  const RenderCommandList* lists[2];
  uint32_t n = 0;
  if (g.const_used) {
    RenderCommandList* c = g.const_lists[g.slot].get();
    RenderBuffer* dst = g.const_gpu[g.slot].get();
    c->begin();
    if (g.timestamps) c->writeTimestamp(g.timestamps.get(), 2 * kFrames + g.slot);
    c->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(dst, RenderBufferAccess::WRITE));
    c->copyBufferRegion(RenderBufferReference(dst, 0), RenderBufferReference(g.const_upload.get(), g.slot * kSlotConsts),
                        g.const_used);
    c->barriers(RenderBarrierStage::GRAPHICS, RenderBufferBarrier(dst, RenderBufferAccess::READ));
    c->end();
    lists[n++] = c;
  }
  lists[n++] = g.cmd;
  g.queue->executeCommandLists(lists, n, waits, wait_count, signals, signal_count, g.fence);
}

double now_ms() {
  static LARGE_INTEGER freq{};
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  LARGE_INTEGER t;
  QueryPerformanceCounter(&t);
  return double(t.QuadPart) * 1000.0 / double(freq.QuadPart);
}

GpuAlloc pool_alloc_impl(BufferPool& pool, uint64_t size);
GpuAlloc pool_alloc(BufferPool& pool, uint64_t size) {
  const double t0 = now_ms();
  const GpuAlloc a = pool_alloc_impl(pool, size);
  g.alloc_ms += now_ms() - t0;
  return a;
}

GpuAlloc pool_alloc_impl(BufferPool& pool, uint64_t size) {
  uint32_t cls = 0;
  while (cls + 1 < kSizeClasses && class_bytes(cls) < size) ++cls;
  if (!pool.free_lists[cls].empty()) {
    const GpuAlloc a = pool.free_lists[cls].back();
    pool.free_lists[cls].pop_back();
    return a;
  }
  const uint64_t bytes = class_bytes(cls);
  if (bytes > pool.chunk) {  // larger than a chunk: a buffer of its own
    pool.chunks.insert(pool.chunks.begin(), g.device->createBuffer(RenderBufferDesc::DefaultBuffer(bytes, pool.flags)));
    return GpuAlloc{pool.chunks.front().get(), 0, cls};
  }
  if (pool.bump == UINT64_MAX || pool.bump + bytes > pool.chunk) {
    pool.chunks.push_back(g.device->createBuffer(RenderBufferDesc::DefaultBuffer(pool.chunk, pool.flags)));
    pool.bump = 0;
  }
  const GpuAlloc a{pool.chunks.back().get(), pool.bump, cls};
  pool.bump += bytes;
  return a;
}

// Back to its pool's free list once no frame in flight can use it.
void retire(BufferPool& pool, GpuAlloc& a) {
  if (!a) return;
  g.retired.push_back({g.frames, &pool, a});
  a = GpuAlloc{};
}

uint64_t upload_alloc(uint64_t size, uint64_t align = 256) {
  uint64_t off = (g.upload_used + align - 1) & ~(align - 1);
  if (off + size > kSlotUpload) return UINT64_MAX;
  g.upload_used = off + size;
  g.upload_peak = std::max(g.upload_peak, g.upload_used);
  return g.slot * kSlotUpload + off;
}


// D3D12 debug layer (gpu_native_debug_layer): messages are copied to the game log.
void drain_debug_messages(const char* where) {
  if (!REXCVAR_GET(gpu_native_debug_layer) || !g.device || g_vulkan) return;
  auto* dev = static_cast<D3D12Device*>(g.device.get())->d3d;
  ID3D12InfoQueue* q = nullptr;
  if (FAILED(dev->QueryInterface(IID_PPV_ARGS(&q)))) return;
  const UINT64 n = q->GetNumStoredMessages();
  for (UINT64 i = 0; i < n && i < 20; ++i) {
    SIZE_T len = 0;
    q->GetMessage(i, nullptr, &len);
    std::vector<uint8_t> buf(len);
    auto* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
    if (SUCCEEDED(q->GetMessage(i, m, &len)) && m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
      REXKRNL_WARN("ORNATIVE d3d12 ({}): {}", where, std::string(m->pDescription, m->DescriptionByteLength));
  }
  q->ClearStoredMessages();
  q->Release();
  rex::FlushLogging();
}

rex::ui::d3d12::D3D12Provider* sdk_provider() {
  auto* ks = rex::system::kernel_state();
  auto* rt = ks ? ks->emulator() : nullptr;
  auto* gs = rt ? rt->graphics_system() : nullptr;
  if (!gs || !gs->presenter() || !gs->provider()) return nullptr;
  // Perfect Dark keeps the SDK's Xenos graphics system; OnPreSetup forces gpu = d3d12.
  return dynamic_cast<rex::ui::d3d12::D3D12Provider*>(gs->provider());
}

// The presenter's adapter by name, so Plume opens its device on the same GPU
// (shared textures only work within one adapter).
std::string sdk_adapter_name() {
  auto* provider = sdk_provider();
  if (!provider) return {};
  IDXGIFactory4* factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return {};
  std::string name;
  IDXGIAdapter1* adapter = nullptr;
  if (SUCCEEDED(factory->EnumAdapterByLuid(provider->GetDevice()->GetAdapterLuid(), IID_PPV_ARGS(&adapter)))) {
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
      char utf8[256] = {};
      WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, utf8, sizeof(utf8) - 1, nullptr, nullptr);
      name = utf8;
    }
    adapter->Release();
  }
  factory->Release();
  return name;
}

// The renderer's API follows the SDK presenter's: decided once a presenter
// exists, before the first shader is translated (SPIR-V for Vulkan).
void choose_api() {
  static bool chosen = false;
  if (chosen) return;
  if (outrun::native_vk::PresenterIsVulkan(nullptr)) g_vulkan = true;
  else if (sdk_provider()) g_vulkan = false;
  else return;  // no presenter yet
  chosen = true;
  outrun::native_shaders::SetSpirv(g_vulkan);
}

// The output pass on the presenter's device: converts a shared B8G8R8A8 frame
// into its R10G10B10A2 guest output, applying the gamma ramp and then Vivid
// Colours / Black Level (as the Xenos path's apply_gamma_color does). GoldenEye's
// version also drew its menu backdrop here; the bindings are kept as they were
// (t1 unused).
constexpr char kOutputShader[] = R"(
cbuffer push_consts : register(b0) {
  uint2 size;
  float uv_scale_x, uv_scale_y, uv_offset_x, uv_offset_y;
  float key_r, key_g, key_b, key_tolerance;
  uint layout_4x3;
  uint mode;  // 8 gamma ramp, 16 colour adjustment, 32 footer highlight
  float black_level, contrast, saturation, highlight_scale, temperature, gamma_;
  float highlight_x0, highlight_y0, highlight_x1, highlight_y1;  // output pixels
};
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

Texture2D<float4> src : register(t0);
Texture2D<float4> image : register(t1);
Texture2D<float4> ramp : register(t2);  // the game's gamma ramp: 256 x 1, R/G/B tables
SamplerState image_sampler : register(s0);
RWTexture2D<float4> dst : register(u0);

[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
  [branch] if (id.x >= size.x || id.y >= size.y) return;
  float3 c = src[id.xy].rgb;
  [branch] if (mode & 8u) {
    uint3 i = uint3(round(saturate(c) * 255.0));
    c = float3(ramp.Load(int3(i.r, 0, 0)).r, ramp.Load(int3(i.g, 0, 0)).g, ramp.Load(int3(i.b, 0, 0)).b);
  }
  [branch] if (mode & 16u) {
    c = saturate((c - black_level) / (1.0 - black_level));
    float luma = dot(c, float3(0.299, 0.587, 0.114));
    c = luma + (c - luma) * saturation;
    c = saturate((c - 0.25) * contrast + 0.25);  // pivot on a dark grey: the game is dark
  }
  [branch] if (mode & 32u)
    c = footer_highlight(c, float2(id.xy) + 0.5, float4(highlight_x0, highlight_y0, highlight_x1, highlight_y1),
                         highlight_scale);
  dst[id.xy] = float4(c, 1.0);
}
)";

bool share_handle(ID3D12Device* from, ID3D12DeviceChild* object, ID3D12Device* to, REFIID iid, void** opened) {
  HANDLE h = nullptr;
  if (FAILED(from->CreateSharedHandle(object, nullptr, GENERIC_ALL, nullptr, &h))) return false;
  const bool ok = SUCCEEDED(to->OpenSharedHandle(h, iid, opened));
  CloseHandle(h);
  return ok;
}

struct OutputConstants {  // kOutputShader's push_consts
  uint32_t size[2];
  float uv_scale[2], uv_offset[2];
  float key[3], key_tolerance;
  uint32_t layout_4x3, mode;
  float black_level, contrast, saturation, highlight_scale, temperature, gamma;
  float highlight[4];
};

// Vulkan: three readback buffers here, the output pass on the presenter's
// device (native_vk_output.h).
bool setup_vulkan_output() {
  Native::Out& o = g.out;
  const uint64_t bytes = uint64_t(g_scene_w) * g_scene_h * 4;
  for (int i = 0; i < 3; ++i) {
    o.readback[i] = g.device->createBuffer(RenderBufferDesc::ReadbackBuffer(bytes));
    o.readback_ptr[i] = o.readback[i] ? static_cast<uint8_t*>(o.readback[i]->map()) : nullptr;
    if (!o.readback_ptr[i]) return false;
  }
  if (!outrun::native_vk::Setup(g_scene_w, g_scene_h)) return false;
  o.presenter = static_cast<rex::system::IGraphicsSystem*>(rex::system::kernel_state()->emulator()->graphics_system())
                    ->presenter();
  return true;
}

bool setup_presenter_output() {
  if (g_vulkan) return setup_vulkan_output();
  rex::ui::d3d12::D3D12Provider* provider = sdk_provider();
  if (!provider) return false;
  Native::Out& o = g.out;
  ID3D12Device* dev = static_cast<D3D12Device*>(g.device.get())->d3d;
  o.pdev = provider->GetDevice();
  o.pqueue = provider->GetDirectQueue();
  const LUID a = dev->GetAdapterLuid(), b = o.pdev->GetAdapterLuid();
  if (a.LowPart != b.LowPart || a.HighPart != b.HighPart) {
    REXKRNL_WARN("ORNATIVE the presenter is on another GPU: using a swap chain of our own");
    return false;
  }
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  rd.Width = g_scene_w;
  rd.Height = g_scene_h;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  rd.SampleDesc.Count = 1;
  for (int i = 0; i < 3; ++i) {
    if (FAILED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr,
                                            IID_PPV_ARGS(&o.tex[i]))) ||
        !share_handle(dev, o.tex[i], o.pdev, IID_PPV_ARGS(&o.ptex[i])))
      return false;
  }
  if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&o.ready))) ||
      !share_handle(dev, o.ready, o.pdev, IID_PPV_ARGS(&o.ready_p)) ||
      FAILED(o.pdev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, IID_PPV_ARGS(&o.done_p))) ||
      !share_handle(o.pdev, o.done_p, dev, IID_PPV_ARGS(&o.done)))
    return false;

  ID3DBlob* code = nullptr;
  ID3DBlob* errors = nullptr;
  if (FAILED(D3DCompile(kOutputShader, sizeof(kOutputShader) - 1, "pd_native_output", nullptr, nullptr, "main",
                        "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
    REXKRNL_ERROR("ORNATIVE output shader: {}",
                  errors ? std::string(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "");
    if (errors) errors->Release();
    return false;
  }
  D3D12_DESCRIPTOR_RANGE ranges[2] = {};
  ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  ranges[0].NumDescriptors = 3;
  ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
  ranges[1].NumDescriptors = 1;
  ranges[1].OffsetInDescriptorsFromTableStart = 3;
  D3D12_ROOT_PARAMETER params[2] = {};
  params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  params[0].Constants.Num32BitValues = sizeof(OutputConstants) / 4;
  params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  params[1].DescriptorTable.NumDescriptorRanges = 2;
  params[1].DescriptorTable.pDescriptorRanges = ranges;
  D3D12_STATIC_SAMPLER_DESC sampler{};
  sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  sampler.MaxLOD = D3D12_FLOAT32_MAX;
  D3D12_ROOT_SIGNATURE_DESC rs{};
  rs.NumParameters = 2;
  rs.pParameters = params;
  rs.NumStaticSamplers = 1;
  rs.pStaticSamplers = &sampler;
  ID3DBlob* rs_blob = nullptr;
  bool ok = SUCCEEDED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, nullptr)) &&
            SUCCEEDED(o.pdev->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(),
                                                  IID_PPV_ARGS(&o.root)));
  if (rs_blob) rs_blob->Release();
  if (ok) {
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = o.root;
    pd.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    ok = SUCCEEDED(o.pdev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&o.pso)));
  }
  code->Release();
  D3D12_DESCRIPTOR_HEAP_DESC hd{};
  hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  hd.NumDescriptors = 12;  // frame, backdrop, gamma ramp and output per command list
  hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  ok = ok && SUCCEEDED(o.pdev->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&o.heap)));
  o.heap_step = o.pdev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  for (int i = 0; ok && i < 3; ++i)
    ok = SUCCEEDED(o.pdev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&o.alloc[i])));
  ok = ok && SUCCEEDED(o.pdev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, o.alloc[0], nullptr,
                                                 IID_PPV_ARGS(&o.list))) &&
       SUCCEEDED(o.list->Close());
  o.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (!ok || !o.event) return false;
  o.presenter = static_cast<rex::system::IGraphicsSystem*>(rex::system::kernel_state()->emulator()->graphics_system())
                    ->presenter();
  return true;
}

D3D12_RESOURCE_BARRIER transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
  D3D12_RESOURCE_BARRIER b{};
  b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  b.Transition.pResource = r;
  b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  b.Transition.StateBefore = from;
  b.Transition.StateAfter = to;
  return b;
}

float cvar_float(const char* name, float def) {
  const std::string v = rex::cvar::GetFlagByName(name);
  return v.empty() ? def : float(std::atof(v.c_str()));
}

// The output pass's settings for this frame: the size, and Vivid Colours /
// Black Level read live from the SDK's cvars with its clamps (GetColorAdjust);
// the ramp's bit is set by the caller.
void output_constants(OutputConstants& c) {
  c = {};
  c.size[0] = g_scene_w;
  c.size[1] = g_scene_h;
  c.saturation = std::clamp(float(REXCVAR_GET(color_saturation)), 0.0f, 3.0f);
  c.contrast = std::clamp(float(REXCVAR_GET(color_contrast)), 0.5f, 2.0f);
  c.black_level = std::clamp(float(REXCVAR_GET(color_black_level)), 0.0f, 0.25f);
  if (c.saturation != 1.0f || c.contrast != 1.0f || c.black_level != 0.0f) c.mode |= 16;
  // The footer prompt under the mouse (menu_affordances.cpp): canvas to output pixels.
  const auto h = outrun::pointer::highlight.load();
  if (GetTickCount64() < h.until) {
    const float sx = g_scene_w / 1280.0f, sy = g_scene_h / 720.0f;
    c.highlight[0] = h.left * sx;
    c.highlight[1] = h.top * sy;
    c.highlight[2] = h.right * sx;
    c.highlight[3] = h.bottom * sy;
    c.highlight_scale = sy;
    c.mode |= 32;
  }
}

// The 360's D3D adjusts every gamma ramp for the display it is told about
// (VdGetCurrentDisplayGamma; the SDK reports a BT.709 HDTV) before writing it
// to the GPU: sRGB-encoded in, BT.709-encoded out. Under Xenos this is what
// turns GoldenEye's identity ramp into the darker table it shows with (entry
// 64 -> 194 of 1023); the native renderer applies the same.
uint16_t for_display(uint16_t value) {
  const double v = value / 65535.0;
  const double linear = v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  const double out = linear < 0.018 ? 4.5 * linear : 1.099 * std::pow(linear, 0.45) - 0.099;
  return uint16_t(std::clamp(out, 0.0, 1.0) * 65535.0 + 0.5);
}

// Uploads the game's gamma ramp when it changed (recorded into `list`; list
// slot `a` owns the upload buffer). Returns whether the ramp texture exists.
bool update_gamma_ramp(ID3D12GraphicsCommandList* list, uint32_t a) {
  Native::Out& o = g.out;
  // Never set by the game (Perfect Dark never sets one): D3D's own identity
  // ramp, which Xenos still shows through the display adjustment above (a
  // shot under Xenos maps sRGB 60 -> 44, 136 -> 124: for_display exactly).
  if (!g.gamma_ramp_version) {
    for (uint32_t i = 0; i < 256; ++i) g.gamma_ramp[i] = g.gamma_ramp[256 + i] = g.gamma_ramp[512 + i] = uint16_t(i * 257);
    g.gamma_ramp_version = 1;
  }
  if (!o.ramp) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = 256;
    td.Height = 1;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
    td.SampleDesc.Count = 1;
    if (FAILED(o.pdev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&o.ramp))))
      return false;
    o.ramp_version = 0;
  }
  if (o.ramp_version != g.gamma_ramp_version) {
    if (!o.ramp_upload[a]) {
      D3D12_HEAP_PROPERTIES heap{};
      heap.Type = D3D12_HEAP_TYPE_UPLOAD;
      D3D12_RESOURCE_DESC bd{};
      bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
      bd.Width = 256 * 8;
      bd.Height = 1;
      bd.DepthOrArraySize = 1;
      bd.MipLevels = 1;
      bd.SampleDesc.Count = 1;
      bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
      if (FAILED(o.pdev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                 nullptr, IID_PPV_ARGS(&o.ramp_upload[a]))))
        return o.ramp_ready;
    }
    uint16_t* mapped = nullptr;
    if (FAILED(o.ramp_upload[a]->Map(0, nullptr, reinterpret_cast<void**>(&mapped)))) return o.ramp_ready;
    // (OutRun never sets a ramp either: its setter 0x82293200 and table writer
    // 0x8229C520 were not called in a menu-to-car-select run, 4 Nov 2026.)
    for (int i = 0; i < 256; ++i) {
      mapped[i * 4 + 0] = for_display(g.gamma_ramp[i]);
      mapped[i * 4 + 1] = for_display(g.gamma_ramp[256 + i]);
      mapped[i * 4 + 2] = for_display(g.gamma_ramp[512 + i]);
      mapped[i * 4 + 3] = 0xFFFF;
    }
    o.ramp_upload[a]->Unmap(0, nullptr);
    if (o.ramp_ready) {
      D3D12_RESOURCE_BARRIER b = transition(o.ramp, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
      list->ResourceBarrier(1, &b);
    }
    D3D12_TEXTURE_COPY_LOCATION to{}, from{};
    to.pResource = o.ramp;
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource = o.ramp_upload[a];
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_UNORM, 256, 1, 1, 256 * 8};
    list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    D3D12_RESOURCE_BARRIER b = transition(o.ramp, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    list->ResourceBarrier(1, &b);
    o.ramp_version = g.gamma_ramp_version;
    o.ramp_ready = true;
  }
  return o.ramp_ready;
}

// Hands shared frame k to the presenter: its queue waits for our copy, then
// converts the frame into the guest output it paints (with FSR, the overlays).
void present_to_presenter(uint32_t k) {
  Native::Out& o = g.out;
  o.presenter->RefreshGuestOutput(g_scene_w, g_scene_h, g_scene_w, g_scene_h,
                                  [k, &o](rex::ui::Presenter::GuestOutputRefreshContext& context) {
    auto& c = static_cast<rex::ui::d3d12::D3D12Presenter::D3D12GuestOutputRefreshContext&>(context);
    context.SetIs8bpc(true);
    const uint32_t a = o.next_list;
    o.next_list = (a + 1) % 3;
    if (o.list_done[a] && o.done_p->GetCompletedValue() < o.list_done[a]) {
      o.done_p->SetEventOnCompletion(o.list_done[a], o.event);
      WaitForSingleObject(o.event, INFINITE);
    }
    o.alloc[a]->Reset();
    o.list->Reset(o.alloc[a], o.pso);
    ID3D12Resource* dst = c.resource_uav_capable();
    OutputConstants constants;
    output_constants(constants);
    ID3D12Resource* picture = nullptr;  // (GoldenEye's backdrop picture: none here)
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = o.heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += SIZE_T(a) * 4 * o.heap_step;
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    o.pdev->CreateShaderResourceView(o.ptex[k], &sv, cpu);
    cpu.ptr += o.heap_step;
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    o.pdev->CreateShaderResourceView(picture, &sv, cpu);
    cpu.ptr += o.heap_step;
    sv.Format = DXGI_FORMAT_R16G16B16A16_UNORM;
    o.pdev->CreateShaderResourceView(update_gamma_ramp(o.list, a) ? o.ramp : nullptr, &sv, cpu);
    if (o.ramp_ready && REXCVAR_GET(gpu_native_query_test) != 30) constants.mode |= 8;  // (30: test without it)
    cpu.ptr += o.heap_step;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
    uv.Format = rex::ui::d3d12::D3D12Presenter::kGuestOutputFormat;
    uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    o.pdev->CreateUnorderedAccessView(dst, nullptr, &uv, cpu);
    constexpr auto kOutState = rex::ui::d3d12::D3D12Presenter::kGuestOutputInternalState;
    D3D12_RESOURCE_BARRIER in[2] = {
        transition(o.ptex[k], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
        transition(dst, kOutState, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};
    o.list->ResourceBarrier(2, in);
    o.list->SetComputeRootSignature(o.root);
    o.list->SetDescriptorHeaps(1, &o.heap);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu = o.heap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += UINT64(a) * 4 * o.heap_step;
    o.list->SetComputeRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
    o.list->SetComputeRootDescriptorTable(1, gpu);
    o.list->Dispatch((g_scene_w + 7) / 8, (g_scene_h + 7) / 8, 1);
    D3D12_RESOURCE_BARRIER back[2] = {
        transition(o.ptex[k], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
        transition(dst, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kOutState)};
    o.list->ResourceBarrier(2, back);
    o.list->Close();
    {  // how far behind each queue is when a frame is handed over (in frames)
      static uint64_t frames = 0, ready_lag = 0, done_lag = 0, ready_most = 0, done_most = 0;
      const uint64_t r = o.ready_value - o.ready->GetCompletedValue(), d = o.done_value - o.done_p->GetCompletedValue();
      ready_lag += r; done_lag += d;
      ready_most = std::max(ready_most, r); done_most = std::max(done_most, d);
      if (++frames == kStatsFrames) {
        REXKRNL_INFO("ORNATIVE queue lag at hand-over: native {:.2f} frames (most {}), presenter {:.2f} (most {})",
                     double(ready_lag) / frames, ready_most, double(done_lag) / frames, done_most);
        frames = ready_lag = done_lag = ready_most = done_most = 0;
      }
    }
    o.pqueue->Wait(o.ready_p, o.tex_ready[k]);
    ID3D12CommandList* lists[] = {o.list};
    o.pqueue->ExecuteCommandLists(1, lists);
    o.pqueue->Signal(o.done_p, ++o.done_value);
    o.list_done[a] = o.tex_done[k] = o.done_value;
    return true;
  });
}

// Test (gpu_native_dump_resolves, Vulkan): every colour resolve's image at
// the end of one frame, copied into readback buffers by the frame's list and
// written out once it has finished.
struct ResolveDump {
  uint32_t dest, w, h;
  std::unique_ptr<RenderBuffer> buffer;
};
std::vector<ResolveDump> g_resolve_dumps;

void record_resolve_dumps() {
  const VkCommandBuffer cb = static_cast<VulkanCommandList*>(g.cmd)->vk;
  for (auto& [dest, r] : g.resolved) {
    if (r.texture)
      REXKRNL_INFO("ORNATIVE test: resolve {:08X} {}x{} format {} depth {} cube {}", dest,
                   static_cast<VulkanTexture*>(r.texture.get())->desc.width,
                   static_cast<VulkanTexture*>(r.texture.get())->desc.height,
                   int(static_cast<VulkanTexture*>(r.texture.get())->desc.format), r.is_depth, r.cube);
    if (r.cube) continue;
    // Colour resolves; depth resolves (R32F) and their D24S8 words (marked + 1 in the name).
    for (RenderTexture* tex : {r.texture.get(), r.words.get()}) {
      if (!tex) continue;
      auto* t = static_cast<VulkanTexture*>(tex);
      if (t->desc.format != RenderFormat::B8G8R8A8_UNORM && t->desc.format != RenderFormat::R8G8B8A8_UNORM &&
          t->desc.format != RenderFormat::R32_FLOAT)
        continue;
      ResolveDump d{dest + (tex == r.words.get() ? 1u : 0u), t->desc.width, t->desc.height,
                    g.device->createBuffer(RenderBufferDesc::ReadbackBuffer(uint64_t(t->desc.width) * t->desc.height * 4))};
      g.cmd->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(tex, RenderTextureLayout::COPY_SOURCE));
      VkBufferImageCopy region{};
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.layerCount = 1;
      region.imageExtent = {d.w, d.h, 1};
      vkCmdCopyImageToBuffer(cb, t->vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                             static_cast<VulkanBuffer*>(d.buffer.get())->vk, 1, &region);
      g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(tex, RenderTextureLayout::SHADER_READ));
      g_resolve_dumps.push_back(std::move(d));
    }
  }
  VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0, nullptr);
}

std::wstring exe_dir();

void write_resolve_dumps() {
  const std::wstring dir = exe_dir() + L"\\native_dumps";
  CreateDirectoryW(dir.c_str(), nullptr);
  for (ResolveDump& d : g_resolve_dumps) {
    const auto* p = static_cast<const uint8_t*>(d.buffer->map());
    wchar_t name[96];
    swprintf(name, 96, L"\\%llu-%08X-%ux%u.bgra", static_cast<unsigned long long>(g.frames), d.dest, d.w, d.h);
    if (FILE* f = _wfopen((dir + name).c_str(), L"wb")) {
      std::fwrite(p, 1, size_t(d.w) * d.h * 4, f);
      std::fclose(f);
    }
  }
  REXKRNL_INFO("ORNATIVE test: {} colour resolves saved in native_dumps", g_resolve_dumps.size());
  g_resolve_dumps.clear();
}

// Vulkan: frame k (finished: its fence has signalled) into the presenter's
// buffer, with the gamma ramp and colour settings, then to the presenter.
void present_vulkan(uint32_t k) {
  Native::Out& o = g.out;
  uint8_t* dst = outrun::native_vk::Staging(k);
  if (!dst || !o.readback_ptr[k]) return;
  // (Plume's readback memory is host-cached and coherent on Windows drivers: no invalidate.)
  std::memcpy(dst, o.readback_ptr[k], size_t(g_scene_w) * g_scene_h * 4);
  if (!g.gamma_ramp_version) {  // never set by the game: D3D's identity ramp (update_gamma_ramp)
    for (uint32_t i = 0; i < 256; ++i) g.gamma_ramp[i] = g.gamma_ramp[256 + i] = g.gamma_ramp[512 + i] = uint16_t(i * 257);
    g.gamma_ramp_version = 1;
  }
  if (o.vk_ramp_version != g.gamma_ramp_version) {
    uint32_t ramp[256];
    auto ten = [](uint16_t v) { return uint32_t((uint32_t(for_display(v)) * 1023u + 32767u) / 65535u); };
    for (uint32_t i = 0; i < 256; ++i)
      ramp[i] = ten(g.gamma_ramp[i]) | ten(g.gamma_ramp[256 + i]) << 10 | ten(g.gamma_ramp[512 + i]) << 20;
    outrun::native_vk::SetRamp(ramp);
    o.vk_ramp_version = g.gamma_ramp_version;
  }
  OutputConstants c;
  output_constants(c);
  outrun::native_vk::OutputSettings settings;
  settings.mode = c.mode | (REXCVAR_GET(gpu_native_query_test) != 30 ? 8u : 0u);  // (30: test without the ramp)
  settings.black_level = c.black_level;
  settings.contrast = c.contrast;
  settings.saturation = c.saturation;
  settings.highlight_scale = c.highlight_scale;
  for (int i = 0; i < 4; ++i) settings.highlight[i] = c.highlight[i];
  outrun::native_vk::Present(k, settings);
}

void prof_mark(std::string label);

// Copies the frame Swap was given into shared frame k (recorded at the end of
// the frame's command list).
bool copy_to_presenter_texture(uint32_t k) {
  const auto front = g.resolved.find(g.front_buffer);
  if (front == g.resolved.end() || !front->second.texture) {
    static uint32_t missing = 0;
    if ((missing++ % 60) == 0)
      REXKRNL_INFO("ORNATIVE no resolved image for front buffer {:08X} ({} frames so far)", g.front_buffer, missing);
    return false;
  }
  RenderTexture* src = front->second.texture.get();
  if (g_vulkan) {
    // Into readback[k] (Plume copies buffers into images only), for the CPU
    // once the frame's fence has signalled.
    auto* t = static_cast<VulkanTexture*>(src);
    if (t->desc.width != g_scene_w || t->desc.height != g_scene_h) return false;
    g.cmd->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(src, RenderTextureLayout::COPY_SOURCE));
    // With gpu_native_hide_left_edge, columns [s, 2s) also go to [0, s)
    // (s = the hidden columns at this render scale): two regions that write
    // different bytes.
    const uint32_t s = REXCVAR_GET(gpu_native_hide_left_edge) ? kHiddenEdgeColumns * g_scene_scale : 0;
    VkBufferImageCopy regions[2]{};
    for (VkBufferImageCopy& r : regions) {
      r.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      r.imageSubresource.layerCount = 1;
      r.bufferRowLength = g_scene_w;
      r.bufferImageHeight = g_scene_h;
    }
    regions[0].bufferOffset = uint64_t(s) * 4;
    regions[0].imageOffset = {int32_t(s), 0, 0};
    regions[0].imageExtent = {g_scene_w - s, g_scene_h, 1};
    regions[1].imageOffset = {int32_t(s), 0, 0};
    regions[1].imageExtent = {s, g_scene_h, 1};
    const VkCommandBuffer cb = static_cast<VulkanCommandList*>(g.cmd)->vk;
    vkCmdCopyImageToBuffer(cb, t->vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           static_cast<VulkanBuffer*>(g.out.readback[k].get())->vk, s ? 2 : 1, regions);
    VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, nullptr, 0,
                         nullptr);
    g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(src, RenderTextureLayout::SHADER_READ));
    prof_mark("present copy");
    return true;
  }
  g.cmd->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(src, RenderTextureLayout::COPY_SOURCE));
  ID3D12GraphicsCommandList* l = static_cast<D3D12CommandList*>(g.cmd)->d3d;
  D3D12_RESOURCE_BARRIER b = transition(g.out.tex[k], D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
  l->ResourceBarrier(1, &b);
  if (const UINT s = REXCVAR_GET(gpu_native_hide_left_edge) ? kHiddenEdgeColumns * g_scene_scale : 0) {
    // Columns [s, w) in place and [s, 2s) again over [0, s): copies to
    // different texels, so no barrier between them (gpu_native_hide_left_edge).
    D3D12_TEXTURE_COPY_LOCATION to{}, from{};
    to.pResource = g.out.tex[k];
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource = static_cast<D3D12Texture*>(src)->d3d;
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    const D3D12_BOX rest{s, 0, 0, g_scene_w, g_scene_h, 1}, edge{s, 0, 0, 2 * s, g_scene_h, 1};
    l->CopyTextureRegion(&to, s, 0, 0, &from, &rest);
    l->CopyTextureRegion(&to, 0, 0, 0, &from, &edge);
  } else {
    l->CopyResource(g.out.tex[k], static_cast<D3D12Texture*>(src)->d3d);
  }
  b = transition(g.out.tex[k], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
  l->ResourceBarrier(1, &b);
  g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(src, RenderTextureLayout::SHADER_READ));
  prof_mark("present copy");
  return true;
}

bool ensure_device() {
  if (g.out.presenter) return true;
  if (g.failed) return false;
  // Every failure below leaves the Xenos renderer running: hung mode (Swap)
  // is only set once this device is up.
  auto fail = [](const char* what) {
    g.failed = true;
    REXKRNL_ERROR("ORNATIVE {}: staying on the Xenos renderer", what);
    return false;
  };
  choose_api();
  std::string vk_name;
  if (outrun::native_vk::PresenterIsVulkan(&vk_name)) {
    if (!g_vulkan) {  // (decided before the presenter existed: translate for Vulkan from now on)
      g_vulkan = true;
      outrun::native_shaders::SetSpirv(true);
    }
    g.api = CreateVulkanInterface();
    if (g.api) g.device = g.api->createDevice(vk_name);
    if (!g.device) return fail("could not create a Vulkan device");
    if (!g.device->getCapabilities().bufferDeviceAddress) {
      g.device.reset();
      return fail("the Vulkan device has no buffer device addresses (the translated shaders read constants through them)");
    }
    // AMD's Vulkan drivers have no D24S8 depth target.
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(static_cast<VulkanDevice*>(g.device.get())->physicalDevice,
                                        VK_FORMAT_D24_UNORM_S8_UINT, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
      kDepthFormat = RenderFormat::D32_FLOAT_S8_UINT;
      REXKRNL_INFO("ORNATIVE no D24S8 depth target on this Vulkan device: D32_FLOAT_S8 instead");
    }
  } else {
    if (!sdk_provider()) return fail("the SDK presenter is neither Direct3D 12 nor Vulkan");
    if (REXCVAR_GET(gpu_native_debug_layer)) {
      ID3D12Debug* dbg = nullptr;
      if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) { dbg->EnableDebugLayer(); dbg->Release(); }
    }
    g.api = CreateD3D12Interface();
    if (g.api) g.device = g.api->createDevice(sdk_adapter_name());
    if (!g.device) return fail("could not create a D3D12 device");
  }
  g.queue = g.device->createCommandQueue(RenderCommandListType::DIRECT);
  g_scene_scale = uint32_t(std::clamp(outrun::ActiveRenderScale(), 1, 3));
  {
    const int w = std::atoi(rex::cvar::GetFlagByName("video_mode_width").c_str());
    const int h = std::atoi(rex::cvar::GetFlagByName("video_mode_height").c_str());
    if (w >= 640 && w <= 1920 && h >= 480 && h <= 1080) { kGuestW = uint32_t(w); kGuestH = uint32_t(h); }
  }
  g_scene_w = kGuestW * g_scene_scale;
  g_scene_h = kGuestH * g_scene_scale;
  REXKRNL_INFO("ORNATIVE scene {}x{} (render scale {}x)", g_scene_w, g_scene_h, g_scene_scale);
  if (!setup_presenter_output()) {
    g.out = {};
    g.queue.reset();
    g.device.reset();
    return fail("could not share frames with the SDK presenter");
  }
  for (uint32_t i = 0; i < kFrames; ++i) {
    g.cmds[i] = g.queue->createCommandList();
    g.fences[i] = g.device->createCommandFence();
  }
  g.cmd = g.cmds[0].get();
  g.fence = g.fences[0].get();

  // Binding model expected by XenosRecomp's shader_common.h: bindless
  // Texture2D/3D/Cube arrays in spaces 0-2, samplers in space 3, and the
  // vertex / pixel / shared constant buffers as root CBVs b0-b2 in space 4.
  RenderPipelineLayoutBuilder lb;
  lb.begin(false, true);
  RenderDescriptorSetBuilder tb;
  tb.begin();
  tb.addTexture(0, kTextureSlots);
  tb.end(true, kTextureSlots);
  g.textures = tb.create(g.device.get());
  lb.addDescriptorSet(tb);
  lb.addDescriptorSet(tb);
  lb.addDescriptorSet(tb);
  RenderDescriptorSetBuilder sb;
  sb.begin();
  sb.addSampler(0, kSamplerSlots);
  sb.end(true, kSamplerSlots);
  g.samplers = sb.create(g.device.get());
  lb.addDescriptorSet(sb);
  if (g_vulkan) {
    // XenosRecomp's Vulkan shaders read their constants through three buffer
    // addresses in push constants (vertex, pixel, shared).
    lb.addPushConstant(0, 0, 24, RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
  } else {
    lb.addRootDescriptor(0, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
    lb.addRootDescriptor(1, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
    lb.addRootDescriptor(2, 4, RenderRootDescriptorType::CONSTANT_BUFFER);
  }
  lb.end();
  g.layout = lb.create(g.device.get());

  RenderSamplerDesc sd;
  sd.minFilter = RenderFilter::LINEAR;
  sd.magFilter = RenderFilter::LINEAR;
  sd.mipmapMode = RenderMipmapMode::LINEAR;
  sd.addressU = sd.addressV = sd.addressW = RenderTextureAddressMode::CLAMP;
  g.default_sampler = g.device->createSampler(sd);
  for (uint32_t i = 0; i < 16; ++i) g.samplers->setSampler(i, g.default_sampler.get());

  // GPU time per frame (Direct3D 12 only: Vulkan timestamps would need resetting before each use).
  if (!g_vulkan) g.timestamps = g.device->createQueryPool(3 * kFrames);  // per slot: start, end; then constants-copy starts
  g.vertex_pool.flags = RenderBufferFlag::VERTEX;
  g.index_pool.flags = RenderBufferFlag::INDEX;
  g.index_pool.chunk = 4ull << 20;  // Dam's index lists come to ~1 MB
  if (!g_vulkan) g.prof_pool = g.device->createQueryPool(4096);
  g.upload = g.device->createBuffer(RenderBufferDesc::UploadBuffer(kUploadSize));
  g.const_upload = g.device->createBuffer(RenderBufferDesc::UploadBuffer(kSlotConsts * kFrames));
  g.const_ptr = static_cast<uint8_t*>(g.const_upload->map());
  for (uint32_t i = 0; i < kFrames; ++i) {
    g.const_gpu[i] = g.device->createBuffer(RenderBufferDesc::DefaultBuffer(
        kSlotConsts, RenderBufferFlag::CONSTANT | (g_vulkan ? RenderBufferFlag::DEVICE_ADDRESSABLE : 0)));
    g.const_lists[i] = g.queue->createCommandList();
  }
  g.upload_ptr = static_cast<uint8_t*>(g.upload->map());

  // The game's scene target is 4x multisampled (RB_SURFACE_INFO, mirrored at
  // device+10940, has msaa_samples = 2): the scene and its depth match, and
  // resolves / present average the samples.
  g.msaa.sampleCount = pick_msaa();
  create_scene_targets(g.msaa);  // (remade at the first bind if that surface is single-sampled)

  // Slot 0 of the texture heap: 1x1 white, sampled by anything not yet supported.
  g.white = g.device->createTexture(RenderTextureDesc::Texture2D(1, 1, 1, RenderFormat::R8G8B8A8_UNORM));
  {
    const uint64_t off = upload_alloc(256, 512);
    std::memset(g.upload_ptr + off, 0xFF, 4);
    g.cmd->begin();
    g.cmd->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(g.white.get(), RenderTextureLayout::COPY_DEST));
    g.cmd->copyTextureRegion(
        RenderTextureCopyLocation::Subresource(g.white.get()),
        RenderTextureCopyLocation::PlacedFootprint(g.upload.get(), RenderFormat::R8G8B8A8_UNORM, 1, 1, 1, 64, off));
    g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(g.white.get(), RenderTextureLayout::SHADER_READ));
    g.cmd->end();
    const RenderCommandList* l = g.cmd;
    g.queue->executeCommandLists(&l, 1, nullptr, 0, nullptr, 0, g.fence);
    g.queue->waitForCommandFence(g.fence);
    g.upload_used = 0;
  }
  for (uint32_t i = 0; i < kTextureSlots; ++i) g.textures->setTexture(i, g.white.get(), RenderTextureLayout::SHADER_READ);
  {
    g.white_cube = g.device->createTexture(RenderTextureDesc::Texture(RenderTextureDimension::TEXTURE_2D, 1, 1, 1, 1, 6,
                                                                      RenderFormat::R8G8B8A8_UNORM, RenderTextureFlag::CUBE));
    RenderTextureViewDesc vd = RenderTextureViewDesc::TextureCube(RenderFormat::R8G8B8A8_UNORM);
    vd.arraySize = 6;
    vd.mipLevels = 1;
    g.white_cube_view = g.white_cube->createTextureView(vd);
    const uint64_t off = upload_alloc(256, 512);
    std::memset(g.upload_ptr + off, 0xFF, 4);
    g.cmd->begin();
    g.cmd->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(g.white_cube.get(), RenderTextureLayout::COPY_DEST));
    for (uint32_t face = 0; face < 6; ++face)
      g.cmd->copyTextureRegion(
          RenderTextureCopyLocation::Subresource(g.white_cube.get(), 0, face),
          RenderTextureCopyLocation::PlacedFootprint(g.upload.get(), RenderFormat::R8G8B8A8_UNORM, 1, 1, 1, 64, off));
    g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(g.white_cube.get(), RenderTextureLayout::SHADER_READ));
    g.cmd->end();
    const RenderCommandList* l = g.cmd;
    g.queue->executeCommandLists(&l, 1, nullptr, 0, nullptr, 0, g.fence);
    g.queue->waitForCommandFence(g.fence);
    g.upload_used = 0;
    g.white_cube_slot = alloc_texture_slot();
    g.textures->setTexture(g.white_cube_slot, g.white_cube.get(), RenderTextureLayout::SHADER_READ, g.white_cube_view.get());
  }

  REXKRNL_INFO("ORNATIVE {} device up, frames go to the SDK presenter", g_vulkan ? "Vulkan" : "D3D12");
  std::thread(warm_pipelines).detach();
  return true;
}


// The scene's sample count from gpu_native_msaa (1, 2 or 4), as far as the GPU
// supports it for both the colour and depth formats.
RenderSampleCounts pick_msaa() {
  const int32_t want = REXCVAR_GET(gpu_native_msaa);
  const RenderSampleCounts supported = g.device->getSampleCountsSupported(kSceneFormat) &
                                       g.device->getSampleCountsSupported(kDepthFormat);
  for (RenderSampleCounts c : {RenderSampleCount::COUNT_8, RenderSampleCount::COUNT_4, RenderSampleCount::COUNT_2})
    if (want >= int32_t(c) && (supported & c)) return c;
  return RenderSampleCount::COUNT_1;
}

// The colour target of a guest colour surface (made when first needed).
Native::ColorTarget& color_target(uint32_t rt) {
  Native::ColorTarget& t = g.color_targets[rt];
  if (!t.color) {
    t.color = g.device->createTexture(RenderTextureDesc::ColorTarget(g_scene_w, g_scene_h, kSceneFormat));
    const RenderTexture* c = t.color.get();
    t.fb = g.device->createFramebuffer(RenderFramebufferDesc(&c, 1));
  }
  return t;
}

// The size a set for this guest pair gets: the scene's, or a depth-only
// surface's own when it is larger (the race's shadow map is 1024x1024: in a
// 1280x720 target its lower rows were lost and its resolve, read with the
// 1024x1024 texture's coordinates, put shadows in the wrong places).
// RB_SURFACE_INFO-adjacent size word at +36: width-1 in bits 18-31, height-1
// in bits 3-17.
void set_size_for(uint32_t rt, uint32_t depth, uint32_t& w, uint32_t& h) {
  w = g_scene_w;
  h = g_scene_h;
  if (rt || !depth) return;
  const uint32_t d = g32(depth + 36);
  const uint32_t sw = ((d >> 18) & 0x3FFF) + 1, sh = ((d >> 3) & 0x7FFF) + 1;
  if (sw > 4096 || sh > 4096) return;
  w = std::max(w, sw * g_scene_scale);
  h = std::max(h, sh * g_scene_scale);
}

// A colour and depth target set of this size and sample count, made the bound
// one (scene / scene_color / scene_depth / scene_fb). A single-sampled
// scene-sized set for colour surface rt draws into rt's colour target.
void create_scene_targets(const RenderMultisampling& msaa, uint32_t rt, uint32_t w, uint32_t h) {
  if (!w || !h) { w = g_scene_w; h = g_scene_h; }
  if (rt && msaa.sampleCount == RenderSampleCount::COUNT_1 && w == g_scene_w && h == g_scene_h) {
    g.scene.reset();
    g.scene_color = color_target(rt).color.get();
  } else {
    g.scene = g.device->createTexture(RenderTextureDesc::ColorTarget(w, h, kSceneFormat, msaa));
    g.scene_color = g.scene.get();
  }
  g.scene_tw = w;
  g.scene_th = h;
  // Depth with stencil (OutRun marks the car in the stencil and draws its shadow
  // volume there), made typeless by Plume so the depth resolve can read it.
  g.scene_depth = g.device->createTexture(RenderTextureDesc::DepthTarget(w, h, kDepthFormat, msaa));
  const RenderTexture* sc = g.scene_color;
  g.scene_fb = g.device->createFramebuffer(RenderFramebufferDesc(&sc, 1, g.scene_depth.get()));
  g.depth_view.reset();  // (made for this depth when a depth resolve needs it)
  g.scene_msaa = msaa;
}

// The sample count a guest colour+depth pair gets: the user's for a surface the
// 360 multisamples (its RB_SURFACE_INFO at +24, bits 16-17: 0 1x, 1 2x, 2 4x),
// one sample for the rest.
RenderMultisampling msaa_for(uint32_t rt, uint32_t depth) {
  const uint32_t surface = rt ? rt : depth;
  const uint32_t guest = surface ? (g32(surface + 24) >> 16) & 3 : 2;
  RenderMultisampling m = g.msaa;
  if (guest == 0) m.sampleCount = RenderSampleCount::COUNT_1;
  else if (guest == 1 && m.sampleCount > RenderSampleCount::COUNT_2) m.sampleCount = RenderSampleCount::COUNT_2;
  return m;
}

// --- shaders ----------------------------------------------------------------

// Rebuilds the contiguous container (virtual part + physical part) exactly as
// the probe saved it, so the hash matches the translated cache.
bool container_at(uint32_t container, uint32_t physical, std::vector<uint8_t>& out) {
  const uint32_t flags = g32(container), vsize = g32(container + 4), psize = g32(container + 8);
  if ((flags & 0xFFFFFF00u) != 0x102A1100u || vsize < 36 || vsize > 65536 || psize > 65536) return false;
  out.resize(vsize + psize);
  std::memcpy(out.data(), gptr(container), vsize);
  std::memcpy(out.data() + vsize, gptr(physical ? physical : container + vsize), psize);
  return true;
}

// The blob a shader was created from (it may since have been freed: then the
// copy kept in the object, with its microcode at the object's GPU address).
bool container_bytes(uint32_t object, bool vertex, std::vector<uint8_t>& out) {
  const auto created = g.created_blobs.find(object);
  if (created != g.created_blobs.end() && container_at(created->second, 0, out)) return true;
  return container_at(object + (vertex ? 872 : 40), gpu_view(g32(object + (vertex ? 32 : 24))), out);
}


// Vertex shaders registered with XGRegisterVertexShader carry their own vertex
// fetch: each vertex element (usage, index, microcode address) points at a
// vfetch instruction giving the fetch constant slot, offset, stride and format
// (layout as XenosRecomp's VertexFetchInstruction). Mini fetches reuse the
// previous full fetch's slot and stride.
// live_micro: guest address of the microcode the GPU runs (D3D patches the
// vfetch format/stride/offset there when a declaration is bound; boot shaders'
// original blobs keep zeroed templates). 0 = use the container's own copy.
void parse_vertex_inputs(const std::vector<uint8_t>& blob, HostShader& hs, uint32_t live_micro = 0) {
  auto be = [&](size_t o) { return o + 4 <= blob.size() ? __builtin_bswap32(*reinterpret_cast<const uint32_t*>(&blob[o])) : 0u; };
  const uint32_t vsize = be(4), shader = be(24);
  const uint32_t phys_off = be(shader), field18 = be(shader + 24), count = be(shader + 28);
  const size_t micro = size_t(vsize) + phys_off;
  auto mc = [&](size_t o) { return live_micro ? g32(live_micro + uint32_t(o - micro)) : be(o); };
  hs.inputs.clear();
  hs.strides.clear();
  // The fetches as the GPU runs them, in microcode order: a mini fetch takes
  // the slot and stride of the full fetch before it.
  struct Fetch { uint32_t address, d0, d1, d2; uint16_t slot; uint32_t stride; };
  std::vector<uint32_t> addresses;
  for (uint32_t i = 0; i < count && i < 32; ++i) addresses.push_back(be(shader + 36 + (field18 + i) * 4) & 0xFFF);
  std::sort(addresses.begin(), addresses.end());
  addresses.erase(std::unique(addresses.begin(), addresses.end()), addresses.end());
  std::vector<Fetch> fetches;
  uint16_t last_slot = 0;
  uint32_t last_stride = 0;
  for (const uint32_t address : addresses) {
    const size_t at = micro + size_t(address) * 12;
    const uint32_t d0 = mc(at), d1 = mc(at + 4), d2 = mc(at + 8);
    if ((d0 & 0x1F) != 0) continue;  // not a vertex fetch
    if (!((d1 >> 30) & 1)) {         // full fetch
      last_slot = uint16_t(((d0 >> 20) & 0x1F) * 3 + ((d0 >> 25) & 3));
      last_stride = (d2 & 0xFF) * 4;
    }
    fetches.push_back({address, d0, d1, d2, last_slot, last_stride});
  }
  // Each element is fed by the fetch that writes the register and components
  // its compiled fetch writes (d0 bits 12-17, d1 bits 0-11): D3D's patching
  // for the bound streams can move fetches to other addresses (the passenger's
  // hair: its second keyframe's position and the texcoords swapped places,
  // so the keyframe was read from the texcoords).
  for (uint32_t i = 0; i < count && i < 32; ++i) {
    const uint32_t e = be(shader + 36 + (field18 + i) * 4);
    const uint32_t address = e & 0xFFF, usage = (e >> 12) & 0xF, index = (e >> 16) & 0xF;
    const size_t at = micro + size_t(address) * 12;
    const uint32_t t0 = be(at), t1 = be(at + 4);
    const uint32_t reg = (t0 >> 12) & 0x3F, mask = t1 & 0xFFF;
    const Fetch* f = nullptr;
    for (const Fetch& c : fetches)
      if (c.address == address) f = &c;
    auto writes = [&](const Fetch& c, bool same_mask) {
      return ((c.d0 >> 12) & 0x3F) == reg && (!same_mask || (c.d1 & 0xFFF) == mask);
    };
    if ((t0 & 0x1F) == 0 && (!f || !writes(*f, true))) {
      const Fetch* by_mask = nullptr;
      const Fetch* by_reg = nullptr;
      int regs = 0;
      for (const Fetch& c : fetches) {
        if (writes(c, true) && !by_mask) by_mask = &c;
        if (writes(c, false)) { by_reg = &c; ++regs; }
      }
      if (by_mask) f = by_mask;
      else if (regs == 1) f = by_reg;
    }
    if (!f) continue;
    const uint32_t d1 = f->d1, d2 = f->d2;
    const int32_t offset = (int32_t(d2 << 1) >> 9) * 4;
    // d1: bit 12 signed (format_comp_all), bit 13 integer (num_format_all);
    // bit 14 is only the signed rounding mode.
    const uint32_t type = ((d1 >> 16) & 0x3F) | (((d1 >> 12) & 1) << 8) | (((d1 >> 13) & 1) << 9);
    const int8_t exp = int8_t(int32_t((d1 >> 24) & 0x3F) << 26 >> 26);
    const uint16_t templ = uint16_t(mask), live = uint16_t(d1 & 0xFFF);
    hs.inputs.push_back({f->slot, uint16_t(offset), type, uint8_t(usage), uint8_t(index), exp,
                         (templ == kIdentitySwizzle || templ == 0) && live ? live : kIdentitySwizzle});
    hs.strides[f->slot] = f->stride;
  }
}

// <hash>.vs|ps.final.dxil (boot shaders translated locally may lack the kind)
// and its .cbsize from the shader cache.
RenderShader* load_cached_shader(HostShader& hs, bool vertex) {
  const std::wstring& dir = outrun::native_shaders::CacheDir();
  wchar_t name[64];
  for (const wchar_t* kind : {vertex ? L"vs." : L"ps.", L""}) {
    swprintf(name, 64, L"\\%016llX.%s%s", static_cast<unsigned long long>(hs.hash), kind,
             outrun::native_shaders::FinalExtension());
    FILE* f = _wfopen((dir + name).c_str(), L"rb");
    if (!f) continue;
    std::fseek(f, 0, SEEK_END);
    std::vector<uint8_t> dxil(static_cast<size_t>(std::ftell(f)));
    std::fseek(f, 0, SEEK_SET);
    std::fread(dxil.data(), 1, dxil.size(), f);
    std::fclose(f);
    hs.shader = g.device->createShader(dxil.data(), dxil.size(), "main",
                                       g_vulkan ? RenderShaderFormat::SPIRV : RenderShaderFormat::DXIL);
    if (!hs.shader) continue;
    hs.cb_bytes = vertex ? 4096 : 3584;
    std::wstring cb = dir + name;
    cb.replace(cb.rfind(L"final."), std::wstring(outrun::native_shaders::FinalExtension()).size(), L"cbsize");
    if (FILE* c = _wfopen(cb.c_str(), L"rb")) {
      unsigned v = 0;
      if (std::fscanf(c, "%u", &v) == 1) hs.cb_bytes = std::min<uint32_t>(v, hs.cb_bytes);
      std::fclose(c);
    }
    return hs.shader.get();
  }
  return nullptr;
}

// Level shaders are registered in place while the level loads, and their
// microcode streams in a little later. Once an object's bytes read the same
// two frames running, its translation starts (outrun::native_shaders::Request does
// nothing for one already cached), so a first run translates a level's shaders
// during its loading instead of at their first draw. A shader drawn before
// that is translated then, as before; the hash is the same either way.
void warm_registered_shaders() {
  choose_api();
  for (auto it = g.warm.begin(); it != g.warm.end();) {
    auto& [object, w] = *it;
    std::vector<uint8_t> blob;
    if (g.frames - w.frame > 600 || !container_bytes(object, w.vertex, blob)) {
      if (g.frames - w.frame > 600) it = g.warm.erase(it); else ++it;
      continue;
    }
    // Microcode not streamed in yet: all zeros past the container's header.
    const uint32_t vsize = (uint32_t(blob[4]) << 24) | (uint32_t(blob[5]) << 16) | (uint32_t(blob[6]) << 8) | blob[7];
    bool filled = false;
    for (size_t i = vsize; i < blob.size() && !filled; ++i) filled = blob[i] != 0;
    const uint64_t hash = fnv1a(blob.data(), blob.size());
    if (!filled || hash != w.last_hash) {
      w.last_hash = hash;
      ++it;
      continue;
    }
    outrun::native_shaders::Request(hash, std::move(blob));
    ++g.warm_requested;
    it = g.warm.erase(it);
  }
}

// A shader the game just created: start translating it if it isn't cached,
// so it is usually ready by the time a draw needs it.
void request_translation(uint32_t object, bool vertex) {
  choose_api();
  std::vector<uint8_t> blob;
  if (!container_bytes(object, vertex, blob)) return;
  const uint64_t hash = fnv1a(blob.data(), blob.size());  // before the move: argument order is unspecified
  outrun::native_shaders::Request(hash, std::move(blob));
}

RenderShader* host_shader(uint32_t object, bool vertex) {
  if (!object) return nullptr;
  HostShader& hs = g.shaders[object];
  if (hs.shader) return hs.shader.get();
  if (hs.missing) return nullptr;
  std::vector<uint8_t> blob;
  // Not cached as missing: the post-process PS is bound on frame 1 before its
  // container is filled in, and becomes valid a few frames later.
  if (!container_bytes(object, vertex, blob)) return nullptr;
  hs.hash = fnv1a(blob.data(), blob.size());
  if (vertex) { parse_vertex_inputs(blob, hs); hs.container = blob; }
  // From the cache (native_shaders); a shader not there yet is translated now
  // (usually already started when the game created it, see request_translation)
  // and this draw waits for it, so nothing is drawn with pieces missing.
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (RenderShader* shader = load_cached_shader(hs, vertex)) return shader;
    if (attempt == 0) {
      outrun::native_shaders::Request(hs.hash, blob);
      if (!outrun::native_shaders::Wait(hs.hash)) break;
    }
  }
  hs.missing = true;
  if (!REXCVAR_GET(gpu_native_translate)) {
    // Leave the container for the local translation step (native/translate_shaders.ps1).
    const std::wstring missing = outrun::native_shaders::CacheDir() + L"\\missing";
    CreateDirectoryW(missing.c_str(), nullptr);
    wchar_t name[64];
    swprintf(name, 64, L"\\%016llX.%s.bin", static_cast<unsigned long long>(hs.hash), vertex ? L"vs" : L"ps");
    if (FILE* f = _wfopen((missing + name).c_str(), L"wb")) {
      std::fwrite(blob.data(), 1, blob.size(), f);
      std::fclose(f);
    }
  }
  REXKRNL_WARN("ORNATIVE no shader {:016X} ({}) for object {:08X} ({} bytes)", hs.hash, vertex ? "vs" : "ps", object,
               blob.size());
  return nullptr;
}

// --- vertex input -----------------------------------------------------------

const char* kSemantics[] = {"POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE", "TEXCOORD",
                            "TANGENT", "BINORMAL", "TESSFACTOR", "POSITIONT", "COLOR", "FOG", "DEPTH",
                            "SAMPLE"};


// D3DDECLTYPE on Xbox 360: low 6 bits = Xenos vertex format, bit 8 = signed,
// bit 9 = integer (not normalised); the rest is the component swizzle, which
// the vertex fetch in the microcode applies itself.
RenderFormat element_format(uint32_t type, uint32_t& component_bytes) {
  const uint32_t fmt = type & 0x3F;
  const bool sign = (type >> 8) & 1, integer = (type >> 9) & 1;
  component_bytes = 4;
  switch (fmt) {
    case 6: component_bytes = 1;
      return integer ? (sign ? RenderFormat::R8G8B8A8_SINT : RenderFormat::R8G8B8A8_UINT)
                     : (sign ? RenderFormat::R8G8B8A8_SNORM : RenderFormat::R8G8B8A8_UNORM);
    case 25: component_bytes = 2;
      return integer ? (sign ? RenderFormat::R16G16_SINT : RenderFormat::R16G16_UINT)
                     : (sign ? RenderFormat::R16G16_SNORM : RenderFormat::R16G16_UNORM);
    case 26: component_bytes = 2;
      return integer ? (sign ? RenderFormat::R16G16B16A16_SINT : RenderFormat::R16G16B16A16_UINT)
                     : (sign ? RenderFormat::R16G16B16A16_SNORM : RenderFormat::R16G16B16A16_UNORM);
    case 31: component_bytes = 2; return RenderFormat::R16G16_FLOAT;
    case 32: component_bytes = 2; return RenderFormat::R16G16B16A16_FLOAT;
    case 36: return RenderFormat::R32_FLOAT;
    case 37: return RenderFormat::R32G32_FLOAT;
    case 38: return RenderFormat::R32G32B32A32_FLOAT;
    case 57: return RenderFormat::R32G32B32_FLOAT;
    case 7: return RenderFormat::UNKNOWN;  // 2:10:10:10 - Plume has no such format yet
    default: return RenderFormat::UNKNOWN;
  }
}

std::vector<Element> read_decl(uint32_t decl) {
  std::vector<Element> els;
  if (!decl) return els;
  const uint32_t n = std::min<uint32_t>(g32(decl + 24), 32);
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t e = decl + 52 + i * 12;
    Element el{g16(e), g16(e + 2), g32(e + 4), *gptr(e + 9), *gptr(e + 10)};
    if (el.stream == 0xFF) break;
    // D3DDECLTYPE carries the component swizzle in bits 10-21 (3 bits each:
    // FLOAT3 = x,y,z,1; FLOAT2 = x,y,0,1; D3DCOLOR = z,y,x,w), which D3D writes
    // into the vfetch it patches; decoded here instead.
    if (const uint16_t sw = uint16_t((el.type >> 10) & 0xFFF)) el.swizzle = sw;
    els.push_back(el);
  }
  return els;
}

// Vertex decoding, done on the CPU the way the Xenos vertex fetch does it: read
// the dwords with the fetch constant's endian mode (0 none, 1 8in16, 2 8in32,
// 3 16in32), take components from the low bits up, then normalise (or not,
// for integer formats), apply the sign and scale by 2^exp. Every element is
// written as 16 bytes: float4, or for XenosRecomp's uint4 inputs (BLENDINDICES,
// NORMAL, TANGENT, BINORMAL) the raw bits (float formats as asuint, packed
// 11:11:10 normals as the dword in .x with spec bit 0).
struct Source {
  uint32_t addr = 0, stride = 0, endian = 2;
};

// 16-bit pairs. Vertex buffers follow the fetch constant's swap as is, but the
// game's own memory passed to DrawVerticesUP / Begin+EndVertices has the first
// value as X (the menu font's U and V came out crossed): those sources carry
// endian bit 2, which swaps the halves back.
inline uint32_t fetch_pair(uint32_t addr, uint32_t endian);

inline uint32_t fetch_dword(uint32_t addr, uint32_t endian) {
  uint32_t w;
  std::memcpy(&w, gptr(addr), 4);
  switch (endian & 3) {
    case 1: return ((w & 0x00FF00FFu) << 8) | ((w >> 8) & 0x00FF00FFu);
    case 2: return __builtin_bswap32(w);
    case 3: return (w << 16) | (w >> 16);
    default: return w;
  }
}

inline bool uint_usage(uint8_t usage) { return usage == 2 || usage == 3 || usage == 6 || usage == 7; }

float half_to_float(uint16_t h) {
  const uint32_t sign = uint32_t(h & 0x8000) << 16, e = (h >> 10) & 0x1F, m = h & 0x3FF;
  uint32_t bits;
  if (e == 0) {
    if (m == 0) bits = sign;
    else { float f = std::ldexp(float(m), -24); std::memcpy(&bits, &f, 4); bits |= sign; }
  } else if (e == 31) bits = sign | 0x7F800000u | (m << 13);
  else bits = sign | ((e + 112) << 23) | (m << 13);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

inline uint32_t fetch_pair(uint32_t addr, uint32_t endian) {
  const uint32_t w = fetch_dword(addr, endian);
  return (endian & 4) ? (w << 16) | (w >> 16) : w;
}

// Host vertex format per element: as small as the source allows (a vertex
// used to be 16 bytes per element: ~100 MB of cached vertices in Dam).
enum class HostKind : uint8_t { kRecord, kF32, kU32, kU8x4, kU16x2, kU16x4, kRaw };
struct HostFmt {
  RenderFormat format;
  uint32_t size;
  HostKind kind;
};

HostFmt host_format(const Element& el) {
  const uint32_t fmt = el.type & 0x3F;
  const bool sign = (el.type >> 8) & 1, integer = (el.type >> 9) & 1, plain = !integer && el.exp == 0;
  if (uint_usage(el.usage)) {
    if ((fmt == 16 || fmt == 17) && el.usage != 2) return {RenderFormat::R32_UINT, 4, HostKind::kU32};  // shader reads .x
    if (el.usage == 2 && fmt == 6) return {RenderFormat::R8G8B8A8_UINT, 4, HostKind::kU8x4};
    return {RenderFormat::R32G32B32A32_UINT, 16, HostKind::kRecord};
  }
  switch (fmt) {
    case 57: return {RenderFormat::R32G32B32_FLOAT, 12, HostKind::kF32};
    case 37: return {RenderFormat::R32G32_FLOAT, 8, HostKind::kF32};
    case 36: return {RenderFormat::R32_FLOAT, 4, HostKind::kF32};
    case 6:
      if (plain) return {sign ? RenderFormat::R8G8B8A8_SNORM : RenderFormat::R8G8B8A8_UNORM, 4, HostKind::kU8x4};
      break;
    case 25:
      if (plain) return {sign ? RenderFormat::R16G16_SNORM : RenderFormat::R16G16_UNORM, 4, HostKind::kU16x2};
      return {RenderFormat::R32G32_FLOAT, 8, HostKind::kF32};
    case 26:
      if (plain) return {sign ? RenderFormat::R16G16B16A16_SNORM : RenderFormat::R16G16B16A16_UNORM, 8, HostKind::kU16x4};
      break;
    case 31: return {RenderFormat::R16G16_FLOAT, 4, HostKind::kRaw};
    case 32: return {RenderFormat::R16G16B16A16_FLOAT, 8, HostKind::kRaw};
    default: break;
  }
  return {RenderFormat::R32G32B32A32_FLOAT, 16, HostKind::kRecord};
}

void decode_element(const Element& el, uint32_t addr, uint32_t endian, uint8_t* out, const HostFmt& hf) {
  const uint32_t fmt = el.type & 0x3F;
  const bool sign = (el.type >> 8) & 1, integer = (el.type >> 9) & 1;
  const float scale = std::ldexp(1.0f, el.exp);
  float f[4] = {0, 0, 0, 1};
  uint32_t u[4] = {0, 0, 0, 1};
  bool is_float = false;
  auto fixed = [&](uint32_t raw, int bits, int c) {
    int64_t v = raw;
    if (sign && ((raw >> (bits - 1)) & 1)) v -= int64_t(1) << bits;
    u[c] = uint32_t(v);
    float x = float(v);
    if (!integer) x = sign ? std::max(x / float((1 << (bits - 1)) - 1), -1.0f) : x / float((int64_t(1) << bits) - 1);
    f[c] = x * scale;
  };
  switch (fmt) {
    case 6: { const uint32_t w = fetch_dword(addr, endian); for (int c = 0; c < 4; ++c) fixed((w >> (8 * c)) & 0xFF, 8, c); break; }
    case 7: { const uint32_t w = fetch_dword(addr, endian);
      fixed(w & 0x3FF, 10, 0); fixed((w >> 10) & 0x3FF, 10, 1); fixed((w >> 20) & 0x3FF, 10, 2); fixed(w >> 30, 2, 3); break; }
    case 16: case 17: { const uint32_t w = fetch_dword(addr, endian);
      if (uint_usage(el.usage)) { u[0] = w; u[1] = u[2] = u[3] = 0; break; }
      fixed(w & 0x7FF, 11, 0); fixed((w >> 11) & 0x7FF, 11, 1); fixed(w >> 22, 10, 2); break; }
    case 25: { const uint32_t w = fetch_pair(addr, endian); fixed(w & 0xFFFF, 16, 0); fixed(w >> 16, 16, 1); break; }
    case 26: { for (int d = 0; d < 2; ++d) { const uint32_t w = fetch_pair(addr + 4 * d, endian);
        fixed(w & 0xFFFF, 16, 2 * d); fixed(w >> 16, 16, 2 * d + 1); } break; }
    case 31: { const uint32_t w = fetch_pair(addr, endian); f[0] = half_to_float(w & 0xFFFF); f[1] = half_to_float(w >> 16);
      u[0] = w; is_float = true; break; }
    case 32: { for (int d = 0; d < 2; ++d) { const uint32_t w = fetch_pair(addr + 4 * d, endian);
        f[2 * d] = half_to_float(w & 0xFFFF); f[2 * d + 1] = half_to_float(w >> 16); u[d] = w; } is_float = true; break; }
    case 36: case 37: case 38: case 57: {
      const int n = fmt == 36 ? 1 : fmt == 37 ? 2 : fmt == 57 ? 3 : 4;
      for (int c = 0; c < n; ++c) { const uint32_t w = fetch_dword(addr + 4 * c, endian); std::memcpy(&f[c], &w, 4); }
      is_float = true; break; }
    default: {
      static uint64_t logged = 0;
      if (fmt < 64 && !(logged & (1ull << fmt))) {
        logged |= 1ull << fmt;
        REXKRNL_WARN("ORNATIVE vertex format {} (type {:08X}, usage {}) not decoded yet", fmt, el.type, el.usage);
      }
      break;
    }
  }
  if (el.swizzle != kIdentitySwizzle) {  // the live vfetch's dst swizzle: 0-3 = x..w, 4 = 0, 5 = 1
    const float sf[4] = {f[0], f[1], f[2], f[3]};
    const uint32_t su[4] = {u[0], u[1], u[2], u[3]};
    for (int c = 0; c < 4; ++c) {
      const uint32_t sel = (el.swizzle >> (3 * c)) & 7;
      if (sel < 4) { f[c] = sf[sel]; u[c] = su[sel]; }
      else if (sel == 4) { f[c] = 0.0f; u[c] = 0; }
      else if (sel == 5) { f[c] = 1.0f; u[c] = 1; }
    }
  }
  switch (hf.kind) {
    case HostKind::kF32: std::memcpy(out, f, hf.size); return;
    case HostKind::kU32: std::memcpy(out, &u[0], 4); return;
    case HostKind::kRaw: std::memcpy(out, u, hf.size); return;
    case HostKind::kU8x4: for (int c = 0; c < 4; ++c) out[c] = uint8_t(u[c]); return;
    case HostKind::kU16x2:
    case HostKind::kU16x4:
      for (uint32_t c = 0; c < hf.size / 2; ++c) { const uint16_t v = uint16_t(u[c]); std::memcpy(out + 2 * c, &v, 2); }
      return;
    case HostKind::kRecord: break;
  }
  if (uint_usage(el.usage)) {
    // Normals, tangents and binormals reach the shader through
    // tfetchR11G11B10, which unpacks 11_11_10 bits (spec bit 0) and otherwise
    // reinterprets the input as floats: every other format goes over as float
    // bits (the Rare logo's 2_10_10_10 normals were arriving as raw integers).
    // Blend indices are used as numbers and stay integers.
    // Perfect Dark's blend indices are floats (FLOAT2): the shader converts
    // the uint4 input by value, so they go over as the numbers they hold.
    const bool packed = fmt == 16 || fmt == 17;
    if (el.usage == 2 && is_float)
      for (int c = 0; c < 4; ++c) u[c] = uint32_t(std::max(0.0f, std::round(f[c])));
    else if (is_float || (el.usage != 2 && !packed))
      for (int c = 0; c < 4; ++c) std::memcpy(&u[c], &f[c], 4);
    std::memcpy(out, u, 16);
  } else {
    std::memcpy(out, f, 16);
  }
}

// Physical address -> a guest address the CPU can read. Physical memory shows
// up through several aliases and not every page is mapped in every one: the
// 0xA0000000 view, the 0xE0000000 view (whose CPU addresses are 4 KB behind
// the physical address), and 0xC0000000. Pick the first whose first and last
// bytes of [phys, phys+size) can be read.
bool readable_seh(uint32_t a, uint32_t b) {
  __try {
    volatile uint8_t x = *gptr(a);
    volatile uint8_t y = *gptr(b);
    (void)x; (void)y;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// parse_vertex_inputs over live microcode, catching a bad page.
bool parse_live_inputs_try(const std::vector<uint8_t>& blob, HostShader& hs, uint32_t live_micro) {
  __try {
    parse_vertex_inputs(blob, hs, live_micro);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}
bool parse_live_inputs_seh(const std::vector<uint8_t>& blob, HostShader& hs, uint32_t live_micro) {
  if (parse_live_inputs_try(blob, hs, live_micro)) return true;
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXKRNL_WARN("ORNATIVE live vertex microcode at {:08X} unreadable; using the shader's own fetches", live_micro);
  }
  return false;
}

uint32_t cpu_view_of_physical(uint32_t phys, uint32_t size) {
  if (!phys) return 0;
  const uint32_t last = size ? size - 1 : 0;
  for (uint32_t base : {0xA0000000u + phys, 0xE0000000u + phys, 0xC0000000u + phys})
    if (readable_seh(base, base + last)) return base;
  return 0;
}

// --- guest textures -----------------------------------------------------------
//
// A texture's 24-byte fetch constant sits at +28 in its D3D object (the same
// bytes SetTexture copies into the device). Decoded fields (Xenos layout):
//   d0: bits 10-18 clamp x/y/z, 22-30 pitch / 32 texels, 31 tiled
//   d1: bits 0-5 format, 6-7 endian, 12-31 base address / 4 KB (physical)
//   d2: (2D) bits 0-12 width-1, 13-25 height-1
//   d3: bits 1-12 swizzle (3 bits per component: 0-3 = x,y,z,w, 4 = 0, 5 = 1),
//       19-20 mag filter, 21-22 min filter
//   d5: bits 9-10 dimension (1 = 2D)
// Only the base level of 2D textures is uploaded for now (no mips), converted to
// RGBA8 on the CPU with the swizzle applied, since the translated shaders
// leave the fetch-constant swizzle to the hardware.

// XGAddress2DTiledOffset: texel (block) offset of (x, y) in a tiled surface.
uint32_t tiled_offset(uint32_t x, uint32_t y, uint32_t pitch, uint32_t bpb) {
  const uint32_t aligned = (pitch + 31) & ~31u;
  const uint32_t log_bpp = (bpb >> 2) + ((bpb >> 1) >> (bpb >> 2));
  const uint32_t macro = ((x >> 5) + (y >> 5) * (aligned >> 5)) << (log_bpp + 7);
  const uint32_t micro = ((x & 7) + ((y & 6) << 2)) << log_bpp;
  const uint32_t off = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << (3 + log_bpp)) + ((y & 1) << 4);
  return (((off & ~511u) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) +
          (((((y & 8) >> 2) + (x >> 3)) & 3) << 6)) >> log_bpp;
}

struct TexFormat {
  uint32_t bpb, block;  // bytes per block, block size in texels (1 or 4)
};
bool tex_format(uint32_t fmt, TexFormat& f) {
  switch (fmt) {
    case 2: f = {1, 1}; return true;                       // k_8
    case 3: case 4: case 10: case 15: f = {2, 1}; return true;  // 1555, 565, 8_8, 4444
    case 6: f = {4, 1}; return true;                       // 8888
    case 18: f = {8, 4}; return true;                      // DXT1
    case 19: case 20: f = {16, 4}; return true;            // DXT2/3, DXT4/5
    default: return false;
  }
}

inline uint32_t swap_by_endian(uint32_t w, uint32_t endian) {
  switch (endian & 3) {
    case 1: return ((w & 0x00FF00FFu) << 8) | ((w >> 8) & 0x00FF00FFu);
    case 2: return __builtin_bswap32(w);
    case 3: return (w << 16) | (w >> 16);
    default: return w;
  }
}

void rgb565(uint16_t c, uint8_t* o) {
  o[0] = uint8_t(((c >> 11) & 31) * 255 / 31);
  o[1] = uint8_t(((c >> 5) & 63) * 255 / 63);
  o[2] = uint8_t((c & 31) * 255 / 31);
}

// Decodes one block (already endian-swapped to little-endian) into RGBA texels
// laid out as x,y,z,w components (Xenos component order: x = lowest bits).
void decode_block(uint32_t fmt, const uint8_t* b, uint8_t out[16][4]) {
  switch (fmt) {
    case 2: out[0][0] = out[0][1] = out[0][2] = out[0][3] = b[0]; break;
    case 10: out[0][0] = b[0]; out[0][1] = b[1]; out[0][2] = 0; out[0][3] = 255; break;
    case 4: { uint16_t c = uint16_t(b[0] | b[1] << 8); out[0][2] = uint8_t((c & 31) * 255 / 31);
      out[0][1] = uint8_t(((c >> 5) & 63) * 255 / 63); out[0][0] = uint8_t(((c >> 11) & 31) * 255 / 31); out[0][3] = 255; break; }
    case 3: { uint16_t c = uint16_t(b[0] | b[1] << 8); out[0][0] = uint8_t((c & 31) * 255 / 31);
      out[0][1] = uint8_t(((c >> 5) & 31) * 255 / 31); out[0][2] = uint8_t(((c >> 10) & 31) * 255 / 31); out[0][3] = (c >> 15) ? 255 : 0; break; }
    case 15: { uint16_t c = uint16_t(b[0] | b[1] << 8);
      for (int k = 0; k < 4; ++k) out[0][k] = uint8_t(((c >> (4 * k)) & 15) * 17); break; }
    case 6: for (int k = 0; k < 4; ++k) out[0][k] = b[k]; break;
    case 18: case 19: case 20: {
      const uint8_t* cb = fmt == 18 ? b : b + 8;
      uint8_t pal[4][4];
      const uint16_t c0 = uint16_t(cb[0] | cb[1] << 8), c1 = uint16_t(cb[2] | cb[3] << 8);
      rgb565(c0, pal[0]); rgb565(c1, pal[1]);
      pal[0][3] = pal[1][3] = pal[2][3] = pal[3][3] = 255;
      if (fmt != 18 || c0 > c1) {
        for (int k = 0; k < 3; ++k) { pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k]) / 3); pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k]) / 3); }
      } else {
        for (int k = 0; k < 3; ++k) { pal[2][k] = uint8_t((pal[0][k] + pal[1][k]) / 2); pal[3][k] = 0; }
        pal[3][3] = 0;
      }
      const uint32_t bits = uint32_t(cb[4]) | uint32_t(cb[5]) << 8 | uint32_t(cb[6]) << 16 | uint32_t(cb[7]) << 24;
      for (int t = 0; t < 16; ++t) std::memcpy(out[t], pal[(bits >> (2 * t)) & 3], 4);
      if (fmt == 19) {
        for (int t = 0; t < 16; ++t) out[t][3] = uint8_t(((b[t / 2] >> (4 * (t & 1))) & 15) * 17);
      } else if (fmt == 20) {
        uint8_t a[8] = {b[0], b[1]};
        if (a[0] > a[1]) for (int k = 2; k < 8; ++k) a[k] = uint8_t(((8 - k) * a[0] + (k - 1) * a[1]) / 7);
        else { for (int k = 2; k < 6; ++k) a[k] = uint8_t(((6 - k) * a[0] + (k - 1) * a[1]) / 5); a[6] = 0; a[7] = 255; }
        uint64_t ab = 0;
        for (int k = 0; k < 6; ++k) ab |= uint64_t(b[2 + k]) << (8 * k);
        for (int t = 0; t < 16; ++t) out[t][3] = a[(ab >> (3 * t)) & 7];
      }
      break;
    }
  }
}

// Decodes a whole 2D level into RGBA8 (with the swizzle); false on a fault.
bool decode_texture_seh(uint32_t addr, uint32_t fmt, uint32_t endian, bool tiled, uint32_t width, uint32_t height,
                        uint32_t pitch, uint32_t swizzle, uint8_t* out, uint32_t row_bytes, uint32_t ox = 0,
                        uint32_t oy = 0) {
  TexFormat tf;
  if (!tex_format(fmt, tf)) return false;
  const uint32_t bw = (width + tf.block - 1) / tf.block, bh = (height + tf.block - 1) / tf.block;
  const uint32_t pitch_blocks = std::max<uint32_t>(pitch / tf.block, bw);
  __try {
    for (uint32_t by = 0; by < bh; ++by) {
      for (uint32_t bx = 0; bx < bw; ++bx) {
        const uint32_t idx = tiled ? tiled_offset(bx + ox, by + oy, pitch_blocks, tf.bpb)
                                   : (by + oy) * pitch_blocks + bx + ox;
        uint8_t raw[16];
        std::memcpy(raw, gptr(addr) + idx * tf.bpb, tf.bpb);
        for (uint32_t k = 0; k + 3 < tf.bpb + 3 && k < tf.bpb; k += 4) {
          uint32_t w = 0;
          std::memcpy(&w, raw + k, std::min<uint32_t>(4, tf.bpb - k));
          w = tf.bpb >= 4 ? swap_by_endian(w, endian) : (endian == 1 || endian == 2 ? ((w & 0xFF) << 8 | (w >> 8 & 0xFF)) : w);
          std::memcpy(raw + k, &w, std::min<uint32_t>(4, tf.bpb - k));
        }
        uint8_t texels[16][4];
        decode_block(fmt, raw, texels);
        const uint32_t n = tf.block;
        for (uint32_t ty = 0; ty < n; ++ty)
          for (uint32_t tx = 0; tx < n; ++tx) {
            const uint32_t x = bx * n + tx, y = by * n + ty;
            if (x >= width || y >= height) continue;
            const uint8_t* src = texels[ty * n + tx];
            uint8_t* dst = out + size_t(y) * row_bytes + size_t(x) * 4;
            for (int c = 0; c < 4; ++c) {
              const uint32_t sel = (swizzle >> (3 * c)) & 7;
              dst[c] = sel < 4 ? src[sel] : sel == 4 ? 0 : 255;
            }
          }
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// Debug: decoded RGBA texture -> <exe>\native_textures\<object>_f<fmt>_<w>x<h>.bmp.
void dump_texture(uint32_t tex, uint32_t fmt, uint32_t width, uint32_t height, const uint8_t* rgba, uint32_t row_bytes) {
  const std::wstring dir = exe_dir() + L"\\native_textures";
  CreateDirectoryW(dir.c_str(), nullptr);
  wchar_t name[96];
  swprintf(name, 96, L"\\%08X_f%u_%ux%u.bmp", tex, fmt, width, height);
  FILE* f = _wfopen((dir + name).c_str(), L"wb");
  if (!f) return;
  const uint32_t image = width * height * 4;
  uint8_t h[54] = {'B', 'M'};
  auto put = [&](int at, uint32_t v) { std::memcpy(h + at, &v, 4); };
  put(2, 54 + image); put(10, 54); put(14, 40); put(18, width); put(22, uint32_t(-int32_t(height)));
  h[26] = 1; h[28] = 32; put(34, image);
  std::fwrite(h, 1, 54, f);
  std::vector<uint8_t> row(width * 4);
  for (uint32_t y = 0; y < height; ++y) {
    const uint8_t* src = rgba + size_t(y) * row_bytes;
    for (uint32_t x = 0; x < width; ++x) {
      row[x * 4 + 0] = src[x * 4 + 2]; row[x * 4 + 1] = src[x * 4 + 1];
      row[x * 4 + 2] = src[x * 4 + 0]; row[x * 4 + 3] = src[x * 4 + 3];
    }
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
}

// Packed mips (fetch dword 5 bit 11): a base level whose shorter side (rounded
// up to a power of two) is 16 texels or less lies inside the 32x32 mip-tail
// tile, 16 texels across (taller than wide or square) or down (wider), as in
// the SDK's TextureInfo::GetPackedTileOffset. Offsets are in blocks.
void packed_base_offset(const uint32_t* f, uint32_t width, uint32_t height, uint32_t block, uint32_t& ox, uint32_t& oy) {
  ox = oy = 0;
  if (!((f[5] >> 11) & 1)) return;
  const auto log2_ceil = [](uint32_t v) { uint32_t l = 0; while ((1u << l) < v) ++l; return l; };
  const uint32_t lw = log2_ceil(width), lh = log2_ceil(height);
  if (std::min(lw, lh) > 4) return;
  if (lw > lh) oy = 16 / block; else ox = 16 / block;
}

// Untiles one level of DXT blocks into rows of `row_pitch` bytes, swapping
// each 16-bit word (DXT data is 8in16): the result is a little-endian BCn level.
bool untile_blocks_seh(uint32_t addr, uint32_t endian, bool tiled, uint32_t bw, uint32_t bh, uint32_t pitch_blocks,
                       uint32_t bpb, uint8_t* out, uint32_t row_pitch, uint32_t ox = 0, uint32_t oy = 0) {
  __try {
    for (uint32_t by = 0; by < bh; ++by) {
      uint8_t* row = out + size_t(by) * row_pitch;
      for (uint32_t bx = 0; bx < bw; ++bx) {
        const uint32_t idx = tiled ? tiled_offset(bx + ox, by + oy, pitch_blocks, bpb) : (by + oy) * pitch_blocks + bx + ox;
        const uint8_t* src = gptr(addr) + size_t(idx) * bpb;
        uint8_t* dst = row + size_t(bx) * bpb;
        if (endian == 1 || endian == 2)
          for (uint32_t k = 0; k < bpb; k += 2) { dst[k] = src[k + 1]; dst[k + 1] = src[k]; }
        else
          std::memcpy(dst, src, bpb);
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// A guest texture made ready in ordinary memory, on the game thread or on the
// texture worker: its levels laid out with the row pitch the upload footprint
// needs (256-byte aligned), so committing it is a copy.
struct PreparedTexture {
  bool ok = false;
  RenderFormat format = RenderFormat::R8G8B8A8_UNORM;
  uint32_t width = 0, height = 0;
  uint32_t block_bytes = 4, block = 1;  // footprint row width (texels) = row_pitch / block_bytes * block
  struct Level {
    uint32_t w, h, rows, row_pitch;
    size_t offset;
  };
  std::vector<Level> levels;  // faces x mips, face by face
  std::vector<uint8_t> data;
  uint32_t faces = 1;  // 6: a cube map
  int8_t keyboard = -1;  // as ResolvedTexture::keyboard
  int8_t hud_mode = -1;  // as ResolvedTexture::hud_mode
  bool hud_pending = false;  // as ResolvedTexture::hud_pending
};

// Keyboard keys in the menus' button icons while no controller is connected
// (user request): the front end's 2048x2048 DXT3 atlas holds the 36x36 A, X
// and Y icons in a row at (1288, 1008) and the B icon at (2012, 732). Each is
// drawn over in place with a key cap naming the key that does that job in
// the menus (Enter, Esc, Q, E), so every screen that shows them changes alike.
namespace keyglyphs {
struct Icon { uint32_t x, y; char button; const char* key; };
constexpr Icon kIcons[] = {{1288, 1008, 'A', "Enter"}, {2012, 732, 'B', "Esc"}, {1324, 1008, 'X', "Q"},
                           {1360, 1008, 'Y', "E"}};
constexpr uint32_t kSize = 36;

uint16_t to565(int r, int g, int b) { return uint16_t(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255)); }
void from565(uint16_t c, int& r, int& g, int& b) {
  r = ((c >> 11) & 31) * 255 / 31; g = ((c >> 5) & 63) * 255 / 63; b = (c & 31) * 255 / 31;
}

// The mean colour of a BC2 block's endpoints: enough to tell the icons apart.
void block_colour(const uint8_t* block, int& r, int& g, int& b) {
  int r0, g0, b0, r1, g1, b1;
  from565(uint16_t(block[8] | block[9] << 8), r0, g0, b0);
  from565(uint16_t(block[10] | block[11] << 8), r1, g1, b1);
  r = (r0 + r1) / 2; g = (g0 + g1) / 2; b = (b0 + b1) / 2;
}

// A key cap (RGBA, 36x36): a light rounded key with a dark edge and the key's
// name in a heavy face, sized to fit.
std::vector<uint8_t> render_key(const char* name) {
  std::vector<uint8_t> rgba(kSize * kSize * 4, 0);
  const bool word = std::strlen(name) > 1;
  // Letter keys are square, as tall as the word keys (Esc, Enter).
  const float x0 = word ? 1.5f : 7.5f, y0 = 7.5f, x1 = word ? kSize - 1.5f : kSize - 7.5f, y1 = kSize - 7.5f,
              radius = word ? 5.0f : 4.0f;
  // Text coverage from GDI (grey anti-aliasing, white on black).
  std::vector<uint8_t> cover(kSize * kSize, 0);
  if (HDC dc = CreateCompatibleDC(nullptr)) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = int(kSize);
    bi.bmiHeader.biHeight = -int(kSize);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    if (HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0)) {
      HGDIOBJ old = SelectObject(dc, bmp);
      std::memset(bits, 0, kSize * kSize * 4);
      for (int height = word ? 15 : 17; height >= 8; --height) {
        HFONT font = CreateFontA(-height, 0, 0, 0, FW_HEAVY, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Segoe UI");
        HGDIOBJ oldFont = SelectObject(dc, font);
        SIZE extent{};
        GetTextExtentPoint32A(dc, name, int(std::strlen(name)), &extent);
        if (extent.cx <= int(x1 - x0) - 4 || height == 8) {
          SetBkMode(dc, TRANSPARENT);
          SetTextColor(dc, RGB(255, 255, 255));
          RECT rc{int(x0), int(y0), int(x1), int(y1)};
          DrawTextA(dc, name, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
          GdiFlush();
          const auto* px = static_cast<const uint8_t*>(bits);
          for (uint32_t i = 0; i < kSize * kSize; ++i) cover[i] = px[i * 4 + 1];
          SelectObject(dc, oldFont);
          DeleteObject(font);
          break;
        }
        SelectObject(dc, oldFont);
        DeleteObject(font);
      }
      SelectObject(dc, old);
      DeleteObject(bmp);
    }
    DeleteDC(dc);
  }
  for (uint32_t y = 0; y < kSize; ++y)
    for (uint32_t x = 0; x < kSize; ++x) {
      // Signed distance to the rounded rectangle, anti-aliased over a pixel.
      const float px = x + 0.5f, py = y + 0.5f;
      const float qx = std::max(std::abs(px - (x0 + x1) / 2) - ((x1 - x0) / 2 - radius), 0.0f);
      const float qy = std::max(std::abs(py - (y0 + y1) / 2) - ((y1 - y0) / 2 - radius), 0.0f);
      const float d = std::sqrt(qx * qx + qy * qy) - radius;
      const float shape = std::clamp(0.5f - d, 0.0f, 1.0f), face = std::clamp(-1.5f - d, 0.0f, 1.0f);
      const float t = cover[y * kSize + x] / 255.0f;
      float r = 20 + (226 - 20) * face, g = 24 + (229 - 24) * face, b = 40 + (236 - 40) * face;
      r += (28 - r) * t; g += (32 - g) * t; b += (48 - b) * t;
      uint8_t* o = &rgba[(y * kSize + x) * 4];
      o[0] = uint8_t(r); o[1] = uint8_t(g); o[2] = uint8_t(b); o[3] = uint8_t(shape * 255.0f + 0.5f);
    }
  return rgba;
}

// One 4x4 block of RGBA to BC2: explicit 4-bit alpha, then the colours as two
// endpoints (the darkest and lightest visible texels) and their blends.
void encode_bc2(const uint8_t* rgba, uint32_t stride, uint8_t* out) {
  int lo = 1 << 30, hi = -1, li = 0, hi_i = 0;
  for (int i = 0; i < 16; ++i) {
    const uint8_t* t = rgba + (i / 4) * stride + (i % 4) * 4;
    const int a4 = (t[3] * 15 + 127) / 255;
    out[i / 2] = uint8_t((i & 1) ? (out[i / 2] | a4 << 4) : a4);
    if (t[3] < 8) continue;
    const int l = t[0] * 3 + t[1] * 6 + t[2];
    if (l < lo) { lo = l; li = i; }
    if (l > hi) { hi = l; hi_i = i; }
  }
  const uint8_t* a = rgba + (hi_i / 4) * stride + (hi_i % 4) * 4;
  const uint8_t* b = rgba + (li / 4) * stride + (li % 4) * 4;
  const uint16_t c0 = to565(a[0], a[1], a[2]), c1 = to565(b[0], b[1], b[2]);
  int pr[4], pg[4], pb[4];
  from565(c0, pr[0], pg[0], pb[0]);
  from565(c1, pr[1], pg[1], pb[1]);
  pr[2] = (2 * pr[0] + pr[1]) / 3; pg[2] = (2 * pg[0] + pg[1]) / 3; pb[2] = (2 * pb[0] + pb[1]) / 3;
  pr[3] = (pr[0] + 2 * pr[1]) / 3; pg[3] = (pg[0] + 2 * pg[1]) / 3; pb[3] = (pb[0] + 2 * pb[1]) / 3;
  uint32_t indices = 0;
  for (int i = 0; i < 16; ++i) {
    const uint8_t* t = rgba + (i / 4) * stride + (i % 4) * 4;
    int best = 0, dist = 1 << 30;
    for (int k = 0; k < 4; ++k) {
      const int d = (t[0] - pr[k]) * (t[0] - pr[k]) + (t[1] - pg[k]) * (t[1] - pg[k]) + (t[2] - pb[k]) * (t[2] - pb[k]);
      if (d < dist) { dist = d; best = k; }
    }
    indices |= uint32_t(best) << (i * 2);
  }
  out[8] = uint8_t(c0); out[9] = uint8_t(c0 >> 8); out[10] = uint8_t(c1); out[11] = uint8_t(c1 >> 8);
  for (int k = 0; k < 4; ++k) out[12 + k] = uint8_t(indices >> (k * 8));
}

// The menu atlas? Its icons' colours give it away (green A, red B, blue X,
// yellow Y). Then, without a controller, the keys go in. Returns
// PreparedTexture::keyboard.
int8_t apply(PreparedTexture& p, uint32_t fmt) {
  if (fmt != 19 || p.width != 2048 || p.height != 2048 || p.levels.empty()) return -1;
  const auto& lv = p.levels[0];
  auto block = [&](uint32_t x, uint32_t y) { return p.data.data() + lv.offset + size_t(y / 4) * lv.row_pitch + size_t(x / 4) * 16; };
  for (const Icon& icon : kIcons) {
    int r, g, b;
    block_colour(block(icon.x + 16, icon.y + 16), r, g, b);
    const bool ok = icon.button == 'A' ? g > r + 40 && g > b + 40
                  : icon.button == 'B' ? r > g + 60 && r > b + 60
                  : icon.button == 'X' ? b > r + 40 && b > g
                                       : r > b + 60 && g > b + 40;
    if (!ok) return -1;
  }
  if (outrun::pointer::controllerConnected.load()) return 0;
  static const std::vector<std::vector<uint8_t>> keys = [] {
    std::vector<std::vector<uint8_t>> k;
    for (const Icon& icon : kIcons) k.push_back(render_key(icon.key));
    return k;
  }();
  for (size_t i = 0; i < std::size(kIcons); ++i)
    for (uint32_t by = 0; by < kSize; by += 4)
      for (uint32_t bx = 0; bx < kSize; bx += 4)
        encode_bc2(keys[i].data() + (by * kSize + bx) * 4, kSize * 4, block(kIcons[i].x + bx, kIcons[i].y + by));
  return 1;
}
}  // namespace keyglyphs

// DXT1/3/5 textures go to the GPU compressed (BC1/2/3), with the game's own mip
// chain: level 0 at the base address, levels 1.. one after another from the mip
// address, each padded to 32x32-block tiles and 4 KB. From the first level
// with a side of 16 texels or less they're packed together on the 360 and
// aren't taken (the host chain stops before them; dumps showed those levels
// garbled when only the longer side was checked). Power-of-two sizes with the
// plain swizzle only; returns false to leave the texture to the RGBA path.
bool prepare_bc(const uint32_t* f, uint32_t addr, uint32_t tex, PreparedTexture& p) {
  constexpr uint32_t swizzle_identity = 0x688;
  const uint32_t fmt = f[1] & 0x3F, endian = (f[1] >> 6) & 3;
  const bool tiled = (f[0] >> 31) & 1;
  const uint32_t width = (f[2] & 0x1FFF) + 1, height = ((f[2] >> 13) & 0x1FFF) + 1;
  const uint32_t pitch = ((f[0] >> 22) & 0x1FF) * 32, swizzle = (f[3] >> 1) & 0xFFF;
  if ((fmt != 18 && fmt != 19 && fmt != 20) || swizzle != swizzle_identity || !REXCVAR_GET(gpu_native_bc_textures))
    return false;
  const bool pow2 = (width & (width - 1)) == 0 && (height & (height - 1)) == 0;
  if (!pow2 || width < 4 || height < 4) return false;
  const uint32_t bpb = fmt == 18 ? 8 : 16;
  p.format = fmt == 18 ? RenderFormat::BC1_UNORM : fmt == 19 ? RenderFormat::BC2_UNORM : RenderFormat::BC3_UNORM;
  p.width = width; p.height = height; p.block_bytes = bpb; p.block = 4;
  const uint32_t mip_phys = f[5] & 0xFFFFF000u, guest_last = (f[4] >> 6) & 0xF;
  uint32_t levels = 1;
  if (tiled && mip_phys)
    while (levels <= guest_last && std::min(width >> levels, height >> levels) > 16) ++levels;
  const uint32_t mip_addr = levels > 1 ? (mip_phys >= 0xE0000000u ? gpu_view(mip_phys) : cpu_view_of_physical(mip_phys & 0x1FFFFFFFu, 4096))
                                       : 0;
  if (levels > 1 && !mip_addr) levels = 1;
  size_t total = 0;
  for (uint32_t l = 0; l < levels; ++l) {
    const uint32_t w = width >> l, h = height >> l, bw = std::max(1u, w / 4), bh = std::max(1u, h / 4);
    const uint32_t row_pitch = (bw * bpb + 255) & ~255u;
    p.levels.push_back({w, h, bh, row_pitch, total});
    total += size_t(row_pitch) * bh;
  }
  p.data.resize(total);
  uint32_t level_addr = addr;
  for (uint32_t l = 0; l < levels; ++l) {
    const PreparedTexture::Level& lv = p.levels[l];
    const uint32_t bw = std::max(1u, lv.w / 4), bh = lv.rows;
    const uint32_t pitch_blocks = l == 0 ? std::max(pitch / 4, bw) : bw;
    if (l == 1) level_addr = mip_addr;
    uint32_t ox = 0, oy = 0;
    if (l == 0) packed_base_offset(f, width, height, 4, ox, oy);
    if (!untile_blocks_seh(level_addr, endian, tiled, bw, bh, pitch_blocks, bpb, p.data.data() + lv.offset, lv.row_pitch,
                           ox, oy)) {
      if (l == 0) return true;  // handled, failed
      p.levels.resize(l);       // the chain so far is still usable
      break;
    }
    if (REXCVAR_GET(gpu_native_dump_textures)) {
      std::vector<uint8_t> rgba(size_t(lv.w) * lv.h * 4);
      if (decode_texture_seh(level_addr, fmt, endian, tiled, lv.w, lv.h, pitch_blocks * 4, swizzle_identity, rgba.data(),
                             lv.w * 4, ox, oy))
        dump_texture(tex + l * 0x10000000u, fmt, lv.w, lv.h, rgba.data(), lv.w * 4);
    }
    if (l >= 1) {  // next level: this one's 32x32-block tiles, 4 KB aligned
      const uint64_t size = uint64_t((bw + 31) & ~31u) * ((bh + 31) & ~31u) * bpb;
      level_addr += uint32_t((size + 4095) & ~4095ull);
    }
  }
  p.ok = true;
  p.keyboard = keyglyphs::apply(p, fmt);
  if (REXCVAR_GET(gpu_native_log_creation))
    REXKRNL_INFO("ORNATIVE texture {:08X}: BC{} {}x{} {} levels (mips at {:08X})", tex, fmt - 17, width, height,
                 p.levels.size(), mip_phys);
  return true;
}

// Everything else is decoded to RGBA8; mip levels the guest texture has (fetch
// dword 4 bits 6-9 = last level) are rebuilt from level 0 with a box filter.
void fill_rgba_levels(PreparedTexture& p, std::vector<uint8_t> prev, uint32_t width, uint32_t height, uint32_t levels);

void prepare_rgba(const uint32_t* f, uint32_t addr, uint32_t tex, PreparedTexture& p) {
  const uint32_t fmt = f[1] & 0x3F, endian = (f[1] >> 6) & 3;
  const bool tiled = (f[0] >> 31) & 1;
  const uint32_t width = (f[2] & 0x1FFF) + 1, height = ((f[2] >> 13) & 0x1FFF) + 1;
  const uint32_t pitch = ((f[0] >> 22) & 0x1FF) * 32, swizzle = (f[3] >> 1) & 0xFFF;
  uint32_t levels = 1;
  const uint32_t guest_last = REXCVAR_GET(gpu_native_all_mips) ? 15 : (f[4] >> 6) & 0xF;
  while (levels <= guest_last && std::max(width, height) >> levels) ++levels;
  std::vector<uint8_t> prev(size_t(width) * height * 4);
  TexFormat tf{};
  tex_format(fmt, tf);
  uint32_t ox = 0, oy = 0;
  packed_base_offset(f, width, height, std::max(1u, tf.block), ox, oy);
  if (!decode_texture_seh(addr, fmt, endian, tiled, width, height, std::max(pitch, width), swizzle, prev.data(), width * 4,
                          ox, oy))
    return;
  if (REXCVAR_GET(gpu_native_log_creation))
    REXKRNL_INFO("ORNATIVE texture {:08X}: d0 {:08X} d3 {:08X} fmt {} {}x{} pitch {} tiled {} endian {} swizzle {:03X} d4 {:08X} d5 {:08X} "
                 "levels {}", tex, f[0], f[3], fmt, width, height, pitch, tiled, endian, swizzle, f[4], f[5], levels);
  if (REXCVAR_GET(gpu_native_dump_textures)) dump_texture(tex, fmt, width, height, prev.data(), width * 4);
  fill_rgba_levels(p, std::move(prev), width, height, levels);
}

// `prev`: the base level, RGBA8 rows of width * 4 bytes. Fills `p` with it and
// `levels - 1` box-filtered mips.
void fill_rgba_levels(PreparedTexture& p, std::vector<uint8_t> prev, uint32_t width, uint32_t height, uint32_t levels) {
  std::vector<uint8_t> cur;
  p.format = RenderFormat::R8G8B8A8_UNORM;
  p.width = width; p.height = height; p.block_bytes = 4; p.block = 1;
  size_t total = 0;
  for (uint32_t l = 0, w = width, h = height; l < levels; ++l, w = std::max(1u, w / 2), h = std::max(1u, h / 2)) {
    const uint32_t row_pitch = (w * 4 + 255) & ~255u;
    p.levels.push_back({w, h, h, row_pitch, total});
    total += size_t(row_pitch) * h;
  }
  p.data.resize(total);
  uint32_t w = width, h = height;
  for (uint32_t l = 0; l < levels; ++l) {
    const PreparedTexture::Level& lv = p.levels[l];
    if (l) {  // box-filter the previous level
      cur.resize(size_t(lv.w) * lv.h * 4);
      const size_t sp = size_t(w) * 4;
      for (uint32_t y = 0; y < lv.h; ++y) {
        const uint8_t* r0 = prev.data() + std::min(y * 2, h - 1) * sp;
        const uint8_t* r1 = prev.data() + std::min(y * 2 + 1, h - 1) * sp;
        uint8_t* d = cur.data() + size_t(y) * lv.w * 4;
        for (uint32_t x = 0; x < lv.w; ++x) {
          const uint32_t a = std::min(x * 2, w - 1) * 4, b = std::min(x * 2 + 1, w - 1) * 4;
          for (int c = 0; c < 4; ++c) d[x * 4 + c] = uint8_t((r0[a + c] + r0[b + c] + r1[a + c] + r1[b + c] + 2) >> 2);
        }
      }
      prev.swap(cur);
      w = lv.w; h = lv.h;
    }
    for (uint32_t y = 0; y < lv.h; ++y)
      std::memcpy(p.data.data() + lv.offset + size_t(y) * lv.row_pitch, prev.data() + size_t(y) * lv.w * 4, size_t(lv.w) * 4);
  }
  p.ok = true;
}

void prepare_2d(const uint32_t* f, uint32_t addr, uint32_t tex, PreparedTexture& p) {
  if (!prepare_bc(f, addr, tex, p)) prepare_rgba(f, addr, tex, p);
}

// Cube maps (fetch dimension 3; Perfect Dark's shiny surfaces: logos, metal,
// flags): six faces one after another, each laid out like a 2D texture's base
// level, at a stride of the face's 32x32-block tiles rounded up to 4 KB (as
// xenia's texture layout has array slices). The base level only, for now.
uint32_t cube_face_stride(const uint32_t* f) {
  TexFormat tf{};
  if (!tex_format(f[1] & 0x3F, tf)) return 0;
  const bool tiled = (f[0] >> 31) & 1;
  const uint32_t width = (f[2] & 0x1FFF) + 1, height = ((f[2] >> 13) & 0x1FFF) + 1;
  const uint32_t pitch = ((f[0] >> 22) & 0x1FF) * 32;
  const uint32_t bw = (std::max(pitch, width) + tf.block - 1) / tf.block, bh = (height + tf.block - 1) / tf.block;
  const uint64_t bytes = tiled ? uint64_t((bw + 31) & ~31u) * ((bh + 31) & ~31u) * tf.bpb : uint64_t(bw) * bh * tf.bpb;
  return uint32_t((bytes + 4095) & ~4095ull);
}

void prepare_cube(const uint32_t* f, uint32_t addr, uint32_t tex, PreparedTexture& p) {
  uint32_t ff[6];
  std::memcpy(ff, f, sizeof(ff));
  ff[4] &= ~(0xFu << 6);                 // last mip level 0: the base level only
  ff[5] = (ff[5] & ~(3u << 9)) | (1u << 9);  // each face as a 2D texture
  const uint32_t stride = cube_face_stride(f);
  if (!stride) return;
  for (uint32_t face = 0; face < 6; ++face) {
    PreparedTexture q;
    prepare_2d(ff, addr + face * stride, tex, q);
    if (!q.ok || q.levels.empty() || (face && (q.format != p.format || q.width != p.width || q.height != p.height)))
      return;
    if (!face) {
      p.format = q.format;
      p.width = q.width;
      p.height = q.height;
      p.block_bytes = q.block_bytes;
      p.block = q.block;
    }
    const size_t base = p.data.size();
    PreparedTexture::Level lv = q.levels[0];
    lv.offset += base;
    p.levels.push_back(lv);
    p.data.insert(p.data.end(), q.data.begin(), q.data.begin() + q.levels[0].offset + size_t(lv.row_pitch) * lv.rows);
  }
  p.faces = 6;
  p.ok = true;
}

// Thread-safe: reads guest memory only (under SEH) and fills `p`.
void prepare_texture(const uint32_t* f, uint32_t addr, uint32_t tex, PreparedTexture& p) {
  if (((f[5] >> 9) & 3) == 3) prepare_cube(f, addr, tex, p);
  else prepare_2d(f, addr, tex, p);
}

// Settings > HUD. The race HUD is drawn from a handful of small textures
// (a 128x128 dial, a 16x128 needle, 512x512 and 1024x512 sheets) stretched
// about 1.5 screen pixels per texel at 720p, 4.5 at 4K. They are known by the
// content hash of their guest bytes (native_hud_table.h). For each mode the
// texture is made anew from the game's own pixels:
//   Sharp: enlarged (Catmull-Rom, premultiplied alpha) and sharpened (unsharp mask);
//   HD:    as Sharp, with the redrawn parts (resources\hud\<hash>.png, at the
//          enlarged size) over the cells the table marks as redrawn;
//   Off:   the cells only the race draws cleared (results, menus keep theirs).
// Enlarged textures carry full mips and their sampler uses them (hud_mips).
namespace hud {
#include "native_hud_table.h"

int mode() { return std::clamp(REXCVAR_GET(outrun_hud), 0, 3); }

const Texture* find(uint64_t content) {
  for (const Texture& t : kTextures)
    if (t.hash == content) return &t;
  return nullptr;
}

// The base level as RGBA8 rows (prepare_rgba's decode, BC formats included).
bool decode_base(const uint32_t* f, uint32_t addr, std::vector<uint8_t>& rgba, uint32_t& w, uint32_t& h) {
  const uint32_t fmt = f[1] & 0x3F, endian = (f[1] >> 6) & 3;
  const bool tiled = (f[0] >> 31) & 1;
  w = (f[2] & 0x1FFF) + 1;
  h = ((f[2] >> 13) & 0x1FFF) + 1;
  const uint32_t pitch = ((f[0] >> 22) & 0x1FF) * 32, swizzle = (f[3] >> 1) & 0xFFF;
  TexFormat tf{};
  if (!tex_format(fmt, tf)) return false;
  uint32_t ox = 0, oy = 0;
  packed_base_offset(f, w, h, std::max(1u, tf.block), ox, oy);
  rgba.assign(size_t(w) * h * 4, 0);
  return decode_texture_seh(addr, fmt, endian, tiled, w, h, std::max(pitch, w), swizzle, rgba.data(), w * 4, ox, oy);
}

// Catmull-Rom enlargement by k (separable, clamped edges like the HUD's
// samplers), on premultiplied colour so transparent texels add no dark fringe;
// then, when `amount` > 0, an unsharp mask (box blur of radius k / 2).
// Returns straight-alpha RGBA8.
std::vector<uint8_t> enlarge(const std::vector<uint8_t>& src, uint32_t w, uint32_t h, uint32_t k, float amount) {
  const uint32_t W = w * k, H = h * k;
  std::vector<float> pre(size_t(w) * h * 4);
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    const float a = src[i * 4 + 3] / 255.0f;
    for (int c = 0; c < 3; ++c) pre[i * 4 + c] = src[i * 4 + c] / 255.0f * a;
    pre[i * 4 + 3] = a;
  }
  struct Taps { int i[4]; float w[4]; };
  auto taps = [k](uint32_t n, uint32_t N) {
    std::vector<Taps> t(N);
    for (uint32_t x = 0; x < N; ++x) {
      const float s = (x + 0.5f) / float(k) - 0.5f;
      const int i0 = int(std::floor(s));
      const float f = s - float(i0);
      const float f2 = f * f, f3 = f2 * f;
      const float wt[4] = {-0.5f * f3 + f2 - 0.5f * f, 1.5f * f3 - 2.5f * f2 + 1.0f, -1.5f * f3 + 2.0f * f2 + 0.5f * f,
                           0.5f * f3 - 0.5f * f2};
      for (int j = 0; j < 4; ++j) {
        t[x].i[j] = std::clamp(i0 - 1 + j, 0, int(n) - 1);
        t[x].w[j] = wt[j];
      }
    }
    return t;
  };
  const std::vector<Taps> tx = taps(w, W), ty = taps(h, H);
  std::vector<float> row(size_t(W) * h * 4);  // horizontal pass
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < W; ++x)
      for (int c = 0; c < 4; ++c) {
        float v = 0;
        for (int j = 0; j < 4; ++j) v += tx[x].w[j] * pre[(size_t(y) * w + tx[x].i[j]) * 4 + c];
        row[(size_t(y) * W + x) * 4 + c] = v;
      }
  std::vector<float> big(size_t(W) * H * 4);  // vertical pass
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x)
      for (int c = 0; c < 4; ++c) {
        float v = 0;
        for (int j = 0; j < 4; ++j) v += ty[y].w[j] * row[(size_t(ty[y].i[j]) * W + x) * 4 + c];
        big[(size_t(y) * W + x) * 4 + c] = v;
      }
  row.clear();
  row.shrink_to_fit();
  if (amount > 0 && k >= 2) {
    const int r = int(k / 2);
    std::vector<float> tmp(big.size()), blur(big.size());
    for (uint32_t y = 0; y < H; ++y)  // box blur, rows then columns
      for (int c = 0; c < 4; ++c) {
        float sum = 0;
        for (int x = -r; x <= r; ++x) sum += big[(size_t(y) * W + std::clamp(x, 0, int(W) - 1)) * 4 + c];
        for (uint32_t x = 0; x < W; ++x) {
          tmp[(size_t(y) * W + x) * 4 + c] = sum / float(2 * r + 1);
          sum += big[(size_t(y) * W + std::min(int(x) + r + 1, int(W) - 1)) * 4 + c] -
                 big[(size_t(y) * W + std::max(int(x) - r, 0)) * 4 + c];
        }
      }
    for (uint32_t x = 0; x < W; ++x)
      for (int c = 0; c < 4; ++c) {
        float sum = 0;
        for (int y = -r; y <= r; ++y) sum += tmp[(size_t(std::clamp(y, 0, int(H) - 1)) * W + x) * 4 + c];
        for (uint32_t y = 0; y < H; ++y) {
          blur[(size_t(y) * W + x) * 4 + c] = sum / float(2 * r + 1);
          sum += tmp[(size_t(std::min(int(y) + r + 1, int(H) - 1)) * W + x) * 4 + c] -
                 tmp[(size_t(std::max(int(y) - r, 0)) * W + x) * 4 + c];
        }
      }
    for (size_t i = 0; i < big.size(); ++i) big[i] += amount * (big[i] - blur[i]);
  }
  // Hard alpha edges (outlined lettering, icons: every texel around nearly
  // clear or nearly opaque) stay hard: the enlarged ramp, k pixels wide, is
  // narrowed to about 1.5 pixels. Soft glows and shadows keep theirs.
  std::vector<uint8_t> hard(size_t(w) * h);
  for (uint32_t y = 0; y < h; ++y)
    for (uint32_t x = 0; x < w; ++x) {
      bool ok = true;
      for (int dy = -1; dy <= 1 && ok; ++dy)
        for (int dx = -1; dx <= 1 && ok; ++dx) {
          const uint8_t a = src[(size_t(std::clamp(int(y) + dy, 0, int(h) - 1)) * w + std::clamp(int(x) + dx, 0, int(w) - 1)) * 4 + 3];
          ok = a < 26 || a > 229;
        }
      hard[size_t(y) * w + x] = ok;
    }
  const float ramp = std::min(0.5f, 0.75f / float(k));
  std::vector<uint8_t> out(size_t(W) * H * 4);
  for (size_t i = 0; i < size_t(W) * H; ++i) {
    float a = std::clamp(big[i * 4 + 3], 0.0f, 1.0f);
    if (k >= 2 && hard[size_t((i / W) / k) * w + (i % W) / k] && a > 0.0f && a < 1.0f) {
      const float t = std::clamp((a - 0.5f + ramp) / (2.0f * ramp), 0.0f, 1.0f);
      const float crisp = t * t * (3.0f - 2.0f * t);
      for (int c = 0; c < 3; ++c) big[i * 4 + c] *= crisp / a;  // same colour, new coverage
      big[i * 4 + 3] = a = crisp;
    }
    out[i * 4 + 3] = uint8_t(a * 255.0f + 0.5f);
    for (int c = 0; c < 3; ++c)
      out[i * 4 + c] = a > 0.0f ? uint8_t(std::clamp(big[i * 4 + c] / a, 0.0f, 1.0f) * 255.0f + 0.5f) : 0;
  }
  return out;
}

// The enlargement: as large as fits one upload (a level of at most ~30 MB with
// its mips: 4x for 512x512 sheets, 3x for 1024x512, 2x for 1024x1024), at most 8x.
uint32_t scale_for(uint32_t w, uint32_t h) {
  uint32_t k = 8;
  while (k > 1 && double(w) * k * h * k * 4 * 1.34 > 30.0 * 1024 * 1024) --k;
  return k;
}

// Clear texels take the colour of their nearest visible ones (a few texels
// out). The HUD blends straight alpha, so bilinear filtering and mips mix a
// clear texel's colour into the edge: black ones (as enlarging and the art
// leave them) put a dark seam around holes and shapes, e.g. the gear frame.
void bleed(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, int passes = 6) {
  std::vector<uint8_t> done(size_t(w) * h);
  for (size_t i = 0; i < done.size(); ++i) done[i] = rgba[i * 4 + 3] != 0;
  for (int pass = 0; pass < passes; ++pass) {
    std::vector<uint32_t> fill;
    for (uint32_t y = 0; y < h; ++y)
      for (uint32_t x = 0; x < w; ++x) {
        const size_t i = size_t(y) * w + x;
        if (done[i]) continue;
        uint32_t sum[3] = {}, n = 0;
        const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& d : nb) {
          const int xx = int(x) + d[0], yy = int(y) + d[1];
          if (xx < 0 || yy < 0 || xx >= int(w) || yy >= int(h)) continue;
          const size_t j = size_t(yy) * w + xx;
          if (!done[j]) continue;
          for (int c = 0; c < 3; ++c) sum[c] += rgba[j * 4 + c];
          ++n;
        }
        if (!n) continue;
        for (int c = 0; c < 3; ++c) rgba[i * 4 + c] = uint8_t(sum[c] / n);
        fill.push_back(uint32_t(i));
      }
    if (fill.empty()) break;
    for (uint32_t i : fill) done[i] = 1;
  }
}

// Made textures by content hash and mode, kept for the session (a stage change
// starts the texture cache afresh). Made on a thread of their own: a sheet
// takes a few hundred milliseconds, so the game's own texture is drawn until
// it is ready (ResolvedTexture::hud_pending), a frame or few.
struct Made {
  std::vector<uint8_t> rgba;
  uint32_t w = 0, h = 0;
};
std::mutex g_cache_mutex;
std::unordered_map<uint64_t, std::shared_ptr<const Made>> g_cache;
std::unordered_set<uint64_t> g_building;

uint64_t cache_key(uint64_t hash, int mode) { return hash ^ (uint64_t(mode) << 60); }

std::shared_ptr<const Made> cached(uint64_t hash, int mode) {
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  const auto it = g_cache.find(cache_key(hash, mode));
  return it != g_cache.end() ? it->second : nullptr;
}

void build(const Texture& e, std::array<uint32_t, 6> f, uint32_t addr, int mode) {
  const double t0 = now_ms();
  auto made = std::make_shared<Made>();
  uint32_t w = 0, h = 0;
  std::vector<uint8_t> base;
  if (decode_base(f.data(), addr, base, w, h)) {
    if (mode == 3) {  // Off: the race's cells cleared
      for (const Rect& r : e.rects) {
        if (!(r.flags & kRace)) continue;
        for (uint32_t y = r.y; y < std::min(r.y + r.h, h); ++y)
          for (uint32_t x = r.x; x < std::min(r.x + r.w, w); ++x) std::memset(&base[(size_t(y) * w + x) * 4], 0, 4);
      }
      made->rgba = std::move(base);
      made->w = w;
      made->h = h;
    } else {
      const uint32_t k = scale_for(w, h);
      made->rgba = enlarge(base, w, h, k, 0.6f);
      made->w = w * k;
      made->h = h * k;
      if (mode == 2) {  // HD: the redrawn cells over the enlarged picture
        int aw = 0, ah = 0;
        size_t png_bytes = 0;
        std::vector<uint8_t> art;
        const std::filesystem::path name =
            std::filesystem::path(exe_dir()) / "hud" / fmt::format("{:016X}.png", e.hash);
        if (std::ifstream file(name, std::ios::binary); file) {
          const std::vector<uint8_t> png((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
          png_bytes = png.size();
          art = rex::ui::DecodeImageRGBA(png.data(), png.size(), aw, ah);
        }
        if (!art.empty() && uint32_t(aw) == made->w && uint32_t(ah) == made->h) {
          for (const Rect& r : e.rects) {
            if (!(r.flags & kRedrawn)) continue;
            const uint32_t x0 = std::min(r.x * k, made->w), y0 = std::min(r.y * k, made->h);
            const uint32_t x1 = std::min((r.x + r.w) * k, made->w), y1 = std::min((r.y + r.h) * k, made->h);
            for (uint32_t y = y0; y < y1; ++y)
              std::memcpy(&made->rgba[(size_t(y) * made->w + x0) * 4], &art[(size_t(y) * made->w + x0) * 4],
                          size_t(x1 - x0) * 4);
          }
        } else if (std::any_of(e.rects.begin(), e.rects.end(), [](const Rect& r) { return r.flags & kRedrawn; })) {
          REXKRNL_WARN("ORNATIVE HUD: no redrawn art {}x{} for {:016X} ({}) in the hud folder beside outrun.exe "
                       "(file {} bytes, decoded {}x{}); enlarged only", made->w, made->h, e.hash, e.name, png_bytes, aw, ah);
        }
        // A light logo on a dark disc (the dial's emblem, not redrawn): its
        // enlarged brightness is re-cut into crisp edges, a light shape over the
        // redrawn dial's disc colour.
        for (const Rect& r : e.rects) {
          if (!(r.flags & kLogo)) continue;
          const uint32_t x0 = std::min(r.x * k, made->w), y0 = std::min(r.y * k, made->h);
          const uint32_t x1 = std::min((r.x + r.w) * k, made->w), y1 = std::min((r.y + r.h) * k, made->h);
          // Brightness (over black), blurred by 0.3 texel so the 18 x 21 texel
          // shape's stair steps round off, then cut with a soft 70-170 ramp:
          // a hard cut left it blobby with stray dots (user, 9 Nov).
          const uint32_t rw = x1 - x0, rh = y1 - y0;
          std::vector<float> lum(size_t(rw) * rh), tmp(lum.size());
          for (uint32_t y = 0; y < rh; ++y)
            for (uint32_t x = 0; x < rw; ++x) {
              const uint8_t* px = &made->rgba[(size_t(y0 + y) * made->w + x0 + x) * 4];
              lum[size_t(y) * rw + x] = (0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]) * (px[3] / 255.0f);
            }
          const float sigma = 0.3f * float(k);
          const int rad = int(std::ceil(sigma * 3.0f));
          std::vector<float> kern(size_t(2 * rad + 1));
          float ksum = 0.0f;
          for (int i = -rad; i <= rad; ++i) ksum += kern[size_t(i + rad)] = std::exp(-0.5f * float(i * i) / (sigma * sigma));
          for (float& v : kern) v /= ksum;
          for (int pass = 0; pass < 2; ++pass) {
            std::vector<float>& src = pass ? tmp : lum;
            std::vector<float>& dst = pass ? lum : tmp;
            for (uint32_t y = 0; y < rh; ++y)
              for (uint32_t x = 0; x < rw; ++x) {
                float s = 0.0f;
                for (int i = -rad; i <= rad; ++i) {
                  const int xx = pass ? int(x) : std::clamp(int(x) + i, 0, int(rw) - 1);
                  const int yy = pass ? std::clamp(int(y) + i, 0, int(rh) - 1) : int(y);
                  s += kern[size_t(i + rad)] * src[size_t(yy) * rw + xx];
                }
                dst[size_t(y) * rw + x] = s;
              }
          }
          for (uint32_t y = y0; y < y1; ++y)
            for (uint32_t x = x0; x < x1; ++x) {
              uint8_t* px = &made->rgba[(size_t(y) * made->w + x) * 4];
              float t = std::clamp((lum[size_t(y - y0) * rw + (x - x0)] - 70.0f) / 100.0f, 0.0f, 1.0f);
              t = t * t * (3.0f - 2.0f * t);
              const float out_a = t + (1.0f - t) * (150.0f / 255.0f);
              const float c = out_a > 0 ? t * 245.0f / out_a : 0.0f;
              px[0] = px[1] = px[2] = uint8_t(std::clamp(c, 0.0f, 255.0f));
              px[3] = uint8_t(out_a * 255.0f + 0.5f);
            }
        }
      }
    }
  }
  if (mode != 3 && !made->rgba.empty()) bleed(made->rgba, made->w, made->h);
  std::lock_guard<std::mutex> lock(g_cache_mutex);
  g_building.erase(cache_key(e.hash, mode));
  if (made->rgba.empty()) return;  // (not decodable: the game's own stays)
  REXKRNL_INFO("ORNATIVE HUD texture {:016X} ({}) for mode {}: {}x{} in {:.0f} ms", e.hash, e.name, mode, made->w,
               made->h, now_ms() - t0);
  g_cache[cache_key(e.hash, mode)] = std::move(made);
}

// Prepares a HUD texture for the current mode; false for other textures.
// `pending`: the mode's texture is still being made (the game's is prepared).
bool prepare(const uint32_t* f, uint32_t addr, uint32_t tex, uint64_t content, PreparedTexture& p, bool& pending) {
  pending = false;
  if (((f[5] >> 9) & 3) == 3) return false;
  const Texture* e = find(content);
  if (!e) return false;
  const int m = mode();
  if (m != 0) {
    if (const auto made = cached(content, m)) {
      uint32_t levels = 1;
      if (m != 3)
        while (std::max(made->w, made->h) >> levels) ++levels;
      fill_rgba_levels(p, made->rgba, made->w, made->h, levels);
      p.hud_mode = int8_t(m);
      return true;
    }
    pending = true;
    std::lock_guard<std::mutex> lock(g_cache_mutex);
    if (g_building.insert(cache_key(content, m)).second) {
      std::array<uint32_t, 6> ff;
      std::copy(f, f + 6, ff.begin());
      std::thread([e, ff, addr, m] { build(*e, ff, addr, m); }).detach();
    }
  }
  prepare_texture(f, addr, tex, p);
  p.hud_mode = int8_t(m);
  return true;
}
}  // namespace hud

// prepare_texture with Settings > HUD applied (`content`: the guest bytes' hash).
void prepare_texture_hud(const uint32_t* f, uint32_t addr, uint32_t tex, uint64_t content, PreparedTexture& p) {
  bool pending = false;
  if (!hud::prepare(f, addr, tex, content, p, pending)) prepare_texture(f, addr, tex, p);
  p.hud_pending = pending;
}

// Game thread, at the start of a draw (may flush the upload ring): creates the
// host texture and records the copies of its levels.
// Descriptor slots: a slot freed by a re-uploaded or evicted texture is reused
// only once no frame in flight can still read it. Out of slots, a texture gets
// slot 0 (white): the time-over title came out as a white box late in a
// session before textures unused for a while were evicted (begin_frame).
uint32_t alloc_texture_slot() {
  if (!g.free_slots.empty()) {
    const uint32_t slot = g.free_slots.back();
    g.free_slots.pop_back();
    return slot;
  }
  if (g.next_slot < kTextureSlots) return g.next_slot++;
  static bool logged = false;
  if (!logged) {
    logged = true;
    REXKRNL_WARN("ORNATIVE texture slots used up ({}): new textures draw white", kTextureSlots);
  }
  return 0;
}

uint32_t commit_texture(ResolvedTexture& t, const PreparedTexture& p) {
  if (!p.ok || p.levels.empty()) { t.slot = UINT32_MAX; return 0; }
  t.keyboard = p.keyboard;
  t.hud_mode = p.hud_mode;
  t.hud_pending = p.hud_pending;
  t.hud_mips = p.hud_mode >= 0 && p.faces == 1 && p.levels.size() > 1 ? uint8_t(p.levels.size()) : 0;
  uint64_t total = 0;
  for (const auto& lv : p.levels) total += uint64_t(lv.row_pitch) * lv.rows + 512;
  ensure_room(total);
  const uint32_t mips = uint32_t(p.levels.size()) / p.faces;
  t.texture = g.device->createTexture(
      p.faces == 6 ? RenderTextureDesc::Texture(RenderTextureDimension::TEXTURE_2D, p.width, p.height, 1, mips, 6, p.format,
                                                RenderTextureFlag::CUBE)
                   : RenderTextureDesc::Texture2D(p.width, p.height, mips, p.format));
  t.view.reset();
  if (p.faces == 6) {
    RenderTextureViewDesc vd = RenderTextureViewDesc::TextureCube(p.format);
    vd.arraySize = 6;
    vd.mipLevels = mips;
    t.view = t.texture->createTextureView(vd);
  }
  t.slot = alloc_texture_slot();
  g.cmd->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(t.texture.get(), RenderTextureLayout::COPY_DEST));
  for (uint32_t l = 0; l < p.levels.size(); ++l) {
    const auto& lv = p.levels[l];
    const uint64_t bytes = uint64_t(lv.row_pitch) * lv.rows;
    const uint64_t off = upload_alloc(bytes, 512);
    if (off == UINT64_MAX) break;
    std::memcpy(g.upload_ptr + off, p.data.data() + lv.offset, bytes);
    g.cmd->copyTextureRegion(RenderTextureCopyLocation::Subresource(t.texture.get(), l % mips, l / mips),
                             RenderTextureCopyLocation::PlacedFootprint(g.upload.get(), p.format, lv.w, lv.h, 1,
                                                                        lv.row_pitch / p.block_bytes * p.block, off));
  }
  g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(t.texture.get(), RenderTextureLayout::SHADER_READ));
  g.textures->setTexture(t.slot, t.texture.get(), RenderTextureLayout::SHADER_READ, t.view.get());
  return t.slot;
}

// Texture worker: prepares textures the game thread had no time for this
// frame (gpu_native_async_textures); the game thread commits them later.
struct TextureJob {
  uint32_t f[6] = {}, addr = 0, tex = 0;
  uint64_t content = 0;
  PreparedTexture result;
  std::atomic<bool> done{false};
};

struct TextureWorker {
  std::mutex mutex;
  std::condition_variable cv;
  std::deque<std::shared_ptr<TextureJob>> queue;
  bool started = false;

  void push(std::shared_ptr<TextureJob> job) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      queue.push_back(std::move(job));
      if (!started) {
        started = true;
        std::thread([this] { run(); }).detach();
      }
    }
    cv.notify_one();
  }

  void run() {
    for (;;) {
      std::shared_ptr<TextureJob> job;
      {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return !queue.empty(); });
        job = std::move(queue.front());
        queue.pop_front();
      }
      prepare_texture_hud(job->f, job->addr, job->tex, job->content, job->result);
      job->done.store(true, std::memory_order_release);
    }
  }
};
TextureWorker g_texture_worker;

// Host texture slot for a guest texture object (0 = white when unsupported or
// still being prepared). may_commit: the caller is at the start of a draw,
// where new textures may be created (their uploads can flush the ring).
bool hash_bytes_seh(uint32_t addr, uint32_t bytes, uint64_t* out);

void copy_pending_depth(uint32_t dest, ResolvedTexture& r);
void depth_will_change();
uint32_t depth_words_slot(uint32_t dest, ResolvedTexture& r, bool may_commit);
void apply_pending_restore();

// The slot of a colour resolve as `tex` samples it: its own, or a view whose
// components follow the constant selectors (0 / 1) of tex's fetch swizzle.
// The colour channels keep the host order (the resolve holds the picture).
uint32_t resolved_slot(uint32_t tex, ResolvedTexture& r) {
  if (!r.texture || r.span != 0 || r.cube || r.is_depth || g.depth_sets.count(tex)) return r.slot;
  const uint16_t sw = uint16_t((g32(tex + 40) >> 1) & 0xFFF);
  bool constant = false;
  for (int c = 0; c < 4; ++c) constant |= ((sw >> (3 * c)) & 7) >= 4;
  if (!constant) return r.slot;
  auto& v = r.swizzled[sw];
  if (!v.first) {
    RenderTextureViewDesc vd = RenderTextureViewDesc::Texture2D(kSceneFormat);
    RenderSwizzle* comps[4] = {&vd.componentMapping.r, &vd.componentMapping.g, &vd.componentMapping.b, &vd.componentMapping.a};
    for (int c = 0; c < 4; ++c) {
      const uint32_t sel = (sw >> (3 * c)) & 7;
      *comps[c] = sel == 4 ? RenderSwizzle::ZERO : sel == 5 ? RenderSwizzle::ONE : RenderSwizzle::IDENTITY;
    }
    v.first = r.texture->createTextureView(vd);
    v.second = alloc_texture_slot();
    g.textures->setTexture(v.second, r.texture.get(), RenderTextureLayout::SHADER_READ, v.first.get());
    REXKRNL_INFO("ORNATIVE resolve {:08X} sampled with swizzle {:03X}: view in slot {}", tex, sw, v.second);
  }
  return v.second;
}

// The resolve a guest texture samples: its own, or one made over the same memory.
const ResolvedTexture* resolved_for(uint32_t tex) {
  if (const auto r = g.resolved.find(tex); r != g.resolved.end()) return &r->second;
  if (const auto rb = g.resolved_base.find(g32(tex + 32) & 0xFFFFF000u); rb != g.resolved_base.end())
    if (const auto r = g.resolved.find(rb->second); r != g.resolved.end() && r->second.texture) return &r->second;
  return nullptr;
}

uint32_t texture_slot(uint32_t tex, bool may_commit = false) {
  if (!tex) return 0;
  const auto r = g.resolved.find(tex);
  if (r != g.resolved.end()) {
    if (r->second.depth_pending && may_commit) {
      r->second.depth_used_frame = g.frames + 1;
      copy_pending_depth(tex, r->second);
    }
    return resolved_slot(tex, r->second);
  }
  uint32_t f[6];
  for (int i = 0; i < 6; ++i) f[i] = g32(tex + 28 + i * 4);
  if (const auto rb = g.resolved_base.find(f[1] & 0xFFFFF000u); rb != g.resolved_base.end()) {
    const auto rr = g.resolved.find(rb->second);
    if (rr != g.resolved.end() && rr->second.texture) {
      // A depth resolve read through a k_8_8_8_8 texture: its D24S8 words.
      if (rr->second.is_depth && (f[1] & 0x3F) == 6) return depth_words_slot(rb->second, rr->second, may_commit);
      return resolved_slot(tex, rr->second);
    }
  }
  const uint64_t key = fnv1a(reinterpret_cast<const uint8_t*>(f), sizeof(f));
  ResolvedTexture& t = g.textures_by_fetch[key];
  if (t.job) {  // on the worker
    if (!may_commit || !t.job->done.load(std::memory_order_acquire)) return 0;
    const std::shared_ptr<TextureJob> job = std::move(t.job);
    return commit_texture(t, job->result);
  }
  // The menu atlas, when a controller comes or goes: its icons again.
  if (t.texture && t.keyboard >= 0 && may_commit && (t.keyboard == 1) == outrun::pointer::controllerConnected.load()) {
    g.retired_textures.push_back({g.frames, std::move(t.texture), t.slot});
    PreparedTexture p;
    prepare_texture_hud(f, t.addr, tex, t.content, p);
    return commit_texture(t, p);
  }
  // A HUD texture, when Settings > HUD changes: made again for the new mode.
  if (t.texture && t.hud_mode >= 0 && may_commit &&
      (t.hud_mode != hud::mode() || (t.hud_pending && hud::cached(t.content, t.hud_mode)))) {
    g.retired_textures.push_back({g.frames, std::move(t.texture), t.slot});
    PreparedTexture p;
    prepare_texture_hud(f, t.addr, tex, t.content, p);
    return commit_texture(t, p);
  }
  if (t.texture && t.dynamic && may_commit && t.checked_frame != g.frames + 1) {
    t.checked_frame = g.frames + 1;
    uint64_t h = 0;
    if (hash_bytes_seh(t.addr, t.span, &h) && h != t.content) {
      // Rewritten by the game: upload again (the old copy and its slot stay
      // until the frames in flight are done with them).
      g.retired_textures.push_back({g.frames, std::move(t.texture), t.slot});
      t.content = h;
      PreparedTexture p;
      prepare_texture_hud(f, t.addr, tex, t.content, p);
      return commit_texture(t, p);
    }
  }
  if (t.texture || t.slot == UINT32_MAX || !may_commit) return t.texture ? t.slot : 0;
  struct Timer {
    double t0 = now_ms();
    ~Timer() { g.tex_ms += now_ms() - t0; ++g.tex_n; }
  } timer;
  const uint32_t fmt = f[1] & 0x3F, dim = (f[5] >> 9) & 3;
  const bool tiled = (f[0] >> 31) & 1;
  const uint32_t width = (f[2] & 0x1FFF) + 1, height = ((f[2] >> 13) & 0x1FFF) + 1;
  const uint32_t pitch = ((f[0] >> 22) & 0x1FF) * 32;
  const uint32_t phys = f[1] & 0xFFFFF000u;
  TexFormat tf;
  if ((dim != 1 && dim != 3) || !tex_format(fmt, tf) || width > 4096 || height > 4096) {
    t.slot = UINT32_MAX;
    static uint64_t logged = 0;
    if (fmt < 64 && !(logged & (1ull << fmt))) {
      logged |= 1ull << fmt;
      REXKRNL_WARN("ORNATIVE texture format {} dim {} {}x{} not uploaded yet", fmt, dim, width, height);
    }
    return 0;
  }
  const uint32_t bh = (height + tf.block - 1) / tf.block, pitch_blocks = std::max<uint32_t>(pitch, width) / tf.block;
  const uint32_t span = dim == 3 ? cube_face_stride(f) * 6
                        : tiled ? ((pitch_blocks + 31) & ~31u) * ((bh + 31) & ~31u) * tf.bpb : pitch_blocks * bh * tf.bpb;
  // Base addresses stored in the 0xE0000000 range (CAFF-loaded textures) are
  // read 4 KB on, like shader microcode (tested: the title logo only decodes
  // there); true physical addresses go through the mapped alias.
  const uint32_t addr = phys >= 0xE0000000u ? gpu_view(phys) : cpu_view_of_physical(phys & 0x1FFFFFFFu, span);
  if (!addr) { t.slot = UINT32_MAX; return 0; }
  // OutRun rewrites textures in the 0xE0000000 heap too (the car's paint colour
  // goes into its body texture): every texture is re-hashed once a frame when used.
  t.dynamic = true;
  t.addr = addr;
  t.span = span;
  if (t.dynamic) {
    hash_bytes_seh(addr, span, &t.content);
    t.checked_frame = g.frames + 1;
  } else if (!REXCVAR_GET(gpu_native_stage_textures)) {
    hash_bytes_seh(addr, span, &t.content);  // (test: kept to count stale ones in begin_frame)
  }
  // In level, a burst of new textures (new objects coming into view) used to
  // cost a 25 ms frame: past ~1 ms of texture work in a frame they go to the
  // worker, and the draws using them wait a frame or two. Frames that ask for
  // many at once are level loads, which are long anyway: those stay here, so
  // a level doesn't open with missing textures.
  // The front end (stage 90) loads in the frame, as the Xenos path does: on
  // the worker, the main menu's first frame or two drew without its pages and
  // photo, showing the bare green folder for a moment.
  ++g.tex_requests;
  if (REXCVAR_GET(gpu_native_async_textures) && stage_number() != 90u &&
      g.tex_ms + (now_ms() - timer.t0) > 1.0 && g.tex_requests <= 24) {
    auto job = std::make_shared<TextureJob>();
    std::memcpy(job->f, f, sizeof(f));
    job->addr = addr;
    job->tex = tex;
    job->content = t.content;
    t.job = job;
    g_texture_worker.push(std::move(job));
    return 0;
  }
  const double t_dec = now_ms();
  PreparedTexture p;
  prepare_texture_hud(f, addr, tex, t.content, p);
  g.tex_decode_ms += now_ms() - t_dec;
  return commit_texture(t, p);
}

// True while the texture's data is still being prepared on the worker: draws
// using it wait (the object appears a frame or two later instead of white).
bool texture_pending(uint32_t tex) {
  if (!tex || g.resolved.count(tex)) return false;
  uint32_t f[6];
  for (int i = 0; i < 6; ++i) f[i] = g32(tex + 28 + i * 4);
  const auto it = g.textures_by_fetch.find(fnv1a(reinterpret_cast<const uint8_t*>(f), sizeof(f)));
  return it != g.textures_by_fetch.end() && it->second.job != nullptr;
}

// Sampler slot from the texture's fetch constant (filters, clamp modes, mip
// filter, anisotropy, LOD bias and mip range), as the SDK's Xenos texture
// cache sets its samplers up. Clamp and filter come from the device's copy of
// the fetch constant (device+1152 + sampler*24: SetTexture copies the
// texture's in, and SetSamplerState patches it there), not the texture header.
//   d3: 19-20 mag, 21-22 min, 23-24 mip filter (0 point, 1 linear, 2 base map),
//       25-27 aniso (0 off, n = 2^(n-1):1)
//   d4: 2-5 / 6-9 mip min / max level, 12-21 LOD bias (signed, 1/32 units)
// The Texture Filtering setting (anisotropic_override, -1 = the game's own; the
// Xenos plugin's default 3 = 4x when that cvar isn't loaded) applies as there:
// to linear, mipmapped sampling only.
uint32_t sampler_slot(uint32_t dev, uint32_t sampler, uint32_t tex) {
  if (!tex) return 0;
  const uint32_t fetch = dev + 1152 + sampler * 24;
  const uint32_t d0 = g32(fetch), d3 = g32(fetch + 12), d4 = g32(fetch + 16);
  const uint32_t clamp_x = (d0 >> 10) & 7, clamp_y = (d0 >> 13) & 7, mag = (d3 >> 19) & 3, min = (d3 >> 21) & 3;
  int32_t bias = int32_t(d4 << 10) >> 22;  // bits 12-21, sign-extended
  // An enlarged HUD texture (Settings > HUD): the game samples its base level
  // only (base map); minified at low resolutions that would shimmer, so its
  // mips are used.
  uint32_t mip = (d3 >> 23) & 3, mip_min = (d4 >> 2) & 0xF, mip_max = std::max((d4 >> 6) & 0xF, mip_min);
  if (mip == 2 && mag == 1 && min == 1) {
    uint32_t f[6];
    for (int i = 0; i < 6; ++i) f[i] = g32(tex + 28 + i * 4);
    const auto it = g.textures_by_fetch.find(fnv1a(reinterpret_cast<const uint8_t*>(f), sizeof(f)));
    if (it != g.textures_by_fetch.end() && it->second.hud_mips > 1) {
      mip = 1;
      mip_min = 0;
      mip_max = std::min<uint32_t>(it->second.hud_mips - 1, 15);
      bias = 0;
    }
  }
  // Texture Filtering: off, 2x, 4x, 8x, 16x (the sampler key holds it, so a
  // change makes new samplers as states come up).
  static constexpr int32_t kAniso[] = {0, 2, 3, 4, 5};
  const int32_t override_aniso = kAniso[std::clamp(REXCVAR_GET(gpu_native_texture_filtering), 0, 4)];
  uint32_t aniso = (d3 >> 25) & 7;
  if (override_aniso > -1 && override_aniso < 6 && mip != 2 && mag == 1 && min == 1 && mip_max > 0) aniso = uint32_t(override_aniso);
  aniso = std::min<uint32_t>(aniso, 5);
  // d5 bits 0-1: the border colour (0 transparent black, 1 opaque white, 2-3
  // the YCbCr blacks). OutRun's shadow map is sampled clamp-to-border, white:
  // terrain beyond the map's reach is lit. The default (opaque black) left the
  // cliffs and buildings past it in shadow, a third darker than on Xenos.
  const uint32_t border = g32(fetch + 20) & 3;
  const uint64_t key = uint64_t(clamp_x) | clamp_y << 3 | mag << 6 | min << 8 | mip << 10 | aniso << 12 | mip_min << 15 |
                       mip_max << 19 | uint64_t(uint32_t(bias) & 0x3FF) << 23 | uint64_t(border) << 33;
  const auto it = g.sampler_slots.find(key);
  if (it != g.sampler_slots.end()) return it->second;
  if (g.sampler_objects.size() >= kSamplerSlots - 1) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXKRNL_WARN("ORNATIVE sampler slots used up ({}): new sampler states use the default", kSamplerSlots);
    }
    return 0;
  }
  // Xenos clamp modes: 0 repeat, 1 mirrored repeat, 2 clamp to edge, 3 mirror
  // clamp to edge, 4 clamp to halfway, 5 mirror clamp to halfway, 6 clamp to
  // border, 7 mirror clamp to border.
  auto addr = [](uint32_t c) {
    switch (c) {
      case 0: return RenderTextureAddressMode::WRAP;
      case 1: return RenderTextureAddressMode::MIRROR;
      case 2: case 3: case 4: case 5: return RenderTextureAddressMode::CLAMP;
      default: return RenderTextureAddressMode::BORDER;
    }
  };
  RenderSamplerDesc sd;
  sd.magFilter = mag == 0 ? RenderFilter::NEAREST : RenderFilter::LINEAR;
  sd.minFilter = min == 0 ? RenderFilter::NEAREST : RenderFilter::LINEAR;
  sd.mipmapMode = mip == 0 ? RenderMipmapMode::NEAREST : RenderMipmapMode::LINEAR;
  if (aniso) {
    sd.anisotropyEnabled = true;
    sd.maxAnisotropy = 1u << (aniso - 1);
  }
  sd.mipLODBias = float(bias) / 32.0f;
  sd.minLOD = float(mip_min);
  sd.maxLOD = mip == 2 ? float(mip_min) : float(mip_max);  // base map: no mips
  sd.addressU = addr(clamp_x);
  sd.addressV = addr(clamp_y);
  sd.addressW = RenderTextureAddressMode::CLAMP;
  sd.borderColor = border == 1 ? RenderBorderColor::OPAQUE_WHITE : RenderBorderColor::TRANSPARENT_BLACK;
  g.sampler_objects.push_back(g.device->createSampler(sd));
  const uint32_t slot = uint32_t(g.sampler_objects.size());  // slot 0 = default
  g.samplers->setSampler(slot, g.sampler_objects.back().get());
  g.sampler_slots[key] = slot;
  return slot;
}

bool hash_bytes_seh(uint32_t addr, uint32_t bytes, uint64_t* out) {
  __try {
    *out = XXH3_64bits(gptr(addr), bytes);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// xxHash of every source's bytes for vertices [first, first + vcount).
bool hash_sources_seh(const Source* srcs, size_t n, uint32_t first, uint32_t vcount, uint64_t* out) {
  __try {
    uint64_t h = 0;
    for (size_t i = 0; i < n; ++i)
      h = XXH3_64bits_withSeed(gptr(srcs[i].addr) + size_t(first) * srcs[i].stride, size_t(vcount) * srcs[i].stride, h);
    *out = h;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// --- render states from the device shadows --------------------------------
// Field locations come from the D3D render-state getters (d3d_functions.toml):
// RB_DEPTHCONTROL (+10548), PA_SU_SC_MODE_CNTL (+10568), RB_BLENDCONTROL
// (+11840), blend enables (+11844), RB_COLORCONTROL (+10556), ZEnable (+11868),
// ColorWriteEnable (+11852), AlphaRef float (+10500). OutRun's getters
// (0x82282BD0..0x82283030): RB_BLEND_RED/GREEN/BLUE/ALPHA floats at +10464..
// +10476; RB_STENCILREFMASK at +10496 (ref, mask, write mask in its low three
// bytes); RB_DEPTHCONTROL bit 0 stencil on (with a depth surface), bit 7 two-
// sided, bits 8-19 func/fail/pass/zfail, bits 20-31 the same for back faces.
// PA_SU_POLY_OFFSET_FRONT_SCALE / _OFFSET / BACK_SCALE / _OFFSET at +10832..
// +10844 (the depth bias setters, 0x822830F8 and 0x822831C0), switched on by
// PA_SU_SC_MODE_CNTL bits 11 (front) and 12 (back).
struct DrawState {
  bool z_enable, z_write, blend;
  uint8_t z_func, cull, src, dst, op, src_a, dst_a, op_a, write_mask;
  bool alpha_test;
  uint8_t alpha_func;
  float alpha_ref;
  bool alpha_to_mask;  // RB_COLORCONTROL bit 4: alpha also sets the MSAA coverage (soft text edges)
  bool stencil, stencil_bf;
  uint32_t stencil_ops;  // RB_DEPTHCONTROL bits 8-31 as they are
  uint8_t stencil_ref, stencil_mask, stencil_write;
  float blend_factor[4];
  float poly_scale, poly_offset;  // depth bias: slope (1/16 pixel units) and constant (depth units)
};

DrawState read_state(uint32_t dev) {
  DrawState st{};
  const uint32_t depth = g32(dev + 10548), mode = g32(dev + 10568), blend = g32(dev + 11856);
  const uint32_t enables = g32(dev + 11860), color = g32(dev + 10556);
  st.z_enable = g32(dev + 11884) != 0;
  st.z_write = (depth >> 2) & 1;
  st.z_func = (depth >> 4) & 7;
  st.cull = mode & 7;
  st.blend = (enables >> 31) & 1;
  st.src = blend & 0x1F; st.op = (blend >> 5) & 7; st.dst = (blend >> 8) & 0x1F;
  st.src_a = (blend >> 16) & 0x1F; st.op_a = (blend >> 21) & 7; st.dst_a = (blend >> 24) & 0x1F;
  if (!((enables >> 30) & 1)) { st.src_a = st.src; st.dst_a = st.dst; st.op_a = st.op; }
  st.write_mask = uint8_t(g32(dev + 11868) & 0xF);
  st.alpha_test = (color >> 3) & 1;
  st.alpha_func = color & 7;
  st.alpha_to_mask = (color >> 4) & 1;
  uint32_t ref = g32(dev + 10500);
  std::memcpy(&st.alpha_ref, &ref, 4);
  st.stencil = depth & 1;
  st.stencil_bf = (depth >> 7) & 1;
  st.stencil_ops = depth >> 8;
  const uint32_t srm = g32(dev + 10496);
  st.stencil_ref = uint8_t(srm);
  st.stencil_mask = uint8_t(srm >> 8);
  st.stencil_write = uint8_t(srm >> 16);
  for (int i = 0; i < 4; ++i) {
    const uint32_t u = g32(dev + 10464 + 4 * i);
    std::memcpy(&st.blend_factor[i], &u, 4);
  }
  // Depth bias as the Xenos path takes it (draw_util::GetPreferredFacePolygonOffset):
  // the front face's unless front faces are culled, else the back face's.
  // OutRun sets it for road markings and other decals and for its shadow map;
  // without it the markings fought the road (flickering lines, user, 4 Nov).
  auto f32 = [&](uint32_t off) { const uint32_t u = g32(dev + off); float f; std::memcpy(&f, &u, 4); return f; };
  if (((mode >> 11) & 1) && !(mode & 1)) {
    st.poly_scale = f32(10832);
    st.poly_offset = f32(10836);
  }
  if (((mode >> 12) & 1) && !(mode & 2) && st.poly_scale == 0.0f && st.poly_offset == 0.0f) {
    st.poly_scale = f32(10840);
    st.poly_offset = f32(10844);
  }
  return st;
}
DrawState g_draw_state{};  // state of the draw being prepared (pipeline key + desc)

RenderBlend to_blend(uint8_t b) {
  switch (b) {
    case 0: return RenderBlend::ZERO;
    case 1: return RenderBlend::ONE;
    case 4: return RenderBlend::SRC_COLOR;
    case 5: return RenderBlend::INV_SRC_COLOR;
    case 6: return RenderBlend::SRC_ALPHA;
    case 7: return RenderBlend::INV_SRC_ALPHA;
    case 8: return RenderBlend::DEST_COLOR;
    case 9: return RenderBlend::INV_DEST_COLOR;
    case 10: return RenderBlend::DEST_ALPHA;
    case 11: return RenderBlend::INV_DEST_ALPHA;
    case 12: case 14: return RenderBlend::BLEND_FACTOR;  // constant colour / constant alpha (see blend_factor)
    case 13: case 15: return RenderBlend::INV_BLEND_FACTOR;
    case 16: return RenderBlend::SRC_ALPHA_SAT;
    default: return RenderBlend::ONE;
  }
}

// The alpha equation's factor. Direct3D 12 refuses colour factors there (the
// pipeline is not made), and the 360, like D3D9, reads that colour's alpha:
// without a separate alpha blend the glare pass's DEST_COLOR blend is copied
// into the alpha equation.
RenderBlend to_blend_alpha(uint8_t b) {
  switch (b) {
    case 4: return RenderBlend::SRC_ALPHA;
    case 5: return RenderBlend::INV_SRC_ALPHA;
    case 8: return RenderBlend::DEST_ALPHA;
    case 9: return RenderBlend::INV_DEST_ALPHA;
    default: return to_blend(b);
  }
}

// The blend factor a draw's blend equation reads (D3DRS_BLENDFACTOR, RB_BLEND_*):
// Direct3D 12 has one factor, so a blend that only asks for the constant
// alpha (14/15) gets it in every channel.
bool blend_factor(const DrawState& st, float out[4]) {
  if (!st.blend) return false;
  const uint8_t f[4] = {st.src, st.dst, st.src_a, st.dst_a};
  bool colour = false, alpha = false;
  for (uint8_t b : f) { colour |= b == 12 || b == 13; alpha |= b == 14 || b == 15; }
  if (!colour && !alpha) return false;
  for (int i = 0; i < 4; ++i) out[i] = colour ? st.blend_factor[i] : st.blend_factor[3];
  return true;
}

// Xenos stencil ops (0 keep, 1 zero, 2 replace, 3 incr clamp, 4 decr clamp,
// 5 invert, 6 incr wrap, 7 decr wrap) in Plume's order, which has UNKNOWN first.
RenderStencilOp to_stencil_op(uint32_t o) { return RenderStencilOp((o & 7) + 1); }
RenderBlendOperation to_blend_op(uint8_t o) {
  switch (o) {
    case 1: return RenderBlendOperation::SUBTRACT;
    case 2: return RenderBlendOperation::MIN;
    case 3: return RenderBlendOperation::MAX;
    case 4: return RenderBlendOperation::REV_SUBTRACT;
    default: return RenderBlendOperation::ADD;
  }
}
RenderComparisonFunction to_cmp(uint8_t c) {
  switch (c) {
    case 0: return RenderComparisonFunction::NEVER;
    case 1: return RenderComparisonFunction::LESS;
    case 2: return RenderComparisonFunction::EQUAL;
    case 3: return RenderComparisonFunction::LESS_EQUAL;
    case 4: return RenderComparisonFunction::GREATER;
    case 5: return RenderComparisonFunction::NOT_EQUAL;
    case 6: return RenderComparisonFunction::GREATER_EQUAL;
    default: return RenderComparisonFunction::ALWAYS;
  }
}

// Decodes vcount vertices into out; returns false if guest memory faulted.
// Kept free of C++ objects so it can use SEH.
bool decode_vertices_seh(const Element* els, size_t n_els, const Source* srcs, const uint16_t* keys,
                         size_t n_keys, uint32_t first, uint32_t vcount, uint8_t* out, uint32_t host_stride,
                         const HostFmt* fmts, const uint32_t* offsets) {
  __try {
    for (uint32_t v = 0; v < vcount; ++v) {
      uint8_t* dst = out + uint64_t(v) * host_stride;
      for (size_t e = 0; e < n_els; ++e) {
        size_t k = 0;
        while (k < n_keys && keys[k] != els[e].stream) ++k;
        const Source& src = srcs[k];
        decode_element(els[e], src.addr + (first + v) * src.stride + els[e].offset, src.endian, dst + offsets[e], fmts[e]);
      }
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
  return true;
}

// --- frame ------------------------------------------------------------------

void copy_scene_to(RenderTexture* dst);

void bind_target() {
  const bool scene = g.guest_depth != 0 || g.guest_rt == 0;
  if (scene) {
    const uint64_t key = uint64_t(g.guest_rt) << 32 | g.guest_depth;
    const RenderMultisampling want = msaa_for(g.guest_rt, g.guest_depth);
    uint32_t want_w = 0, want_h = 0;
    set_size_for(g.guest_rt, g.guest_depth, want_w, want_h);
    // A set made at another sample count or size (the first set, made before any
    // surface was known, or a new surface at a parked set's addresses): its
    // targets are released once the frames in flight are done with them.
    auto retire_set = [](std::unique_ptr<RenderTexture>& color, std::unique_ptr<RenderTexture>& depth) {
      for (auto& [dest, r] : g.resolved)  // depth resolves not copied from it yet are dropped
        if (r.depth_src == depth.get()) {
          r.depth_src = nullptr;
          r.depth_pending = false;
          std::erase(g.depth_pending, dest);
        }
      if (color) g.retired_textures.push_back({g.frames, std::move(color), 0});
      g.retired_textures.push_back({g.frames, std::move(depth), 0});
    };
    if (!g.scene_key_set) {
      g.scene_key = key;
      g.scene_key_set = true;
      if (want.sampleCount != g.scene_msaa.sampleCount || want_w != g.scene_tw || want_h != g.scene_th) {
        retire_set(g.scene, g.scene_depth);
        create_scene_targets(want, g.guest_rt, want_w, want_h);
        g.bound_fb = nullptr;
      }
    } else if (key != g.scene_key) {
      // Park the bound set, then bring in (or make) this pair's.
      Native::SceneSet& out = g.scene_sets[g.scene_key];
      out.color = std::move(g.scene);
      out.color_ptr = g.scene_color;
      out.depth = std::move(g.scene_depth);
      out.fb = std::move(g.scene_fb);
      out.depth_view = std::move(g.depth_view);
      out.msaa = g.scene_msaa;
      out.w = g.scene_tw;
      out.h = g.scene_th;
      Native::SceneSet& in = g.scene_sets[key];
      if (in.depth && (in.msaa.sampleCount != want.sampleCount || in.w != want_w || in.h != want_h))
        retire_set(in.color, in.depth);
      if (in.depth) {
        g.scene = std::move(in.color);
        g.scene_color = in.color_ptr;
        g.scene_depth = std::move(in.depth);
        g.scene_fb = std::move(in.fb);
        g.depth_view = std::move(in.depth_view);
        g.scene_msaa = in.msaa;
        g.scene_tw = in.w;
        g.scene_th = in.h;
      } else {
        create_scene_targets(want, g.guest_rt, want_w, want_h);
      }
      g.scene_sets.erase(key);
      g.scene_key = key;
      g.bound_fb = nullptr;
    }
    if (g.guest_rt) g.scene_surface = g.guest_rt;
    if (g.bound_fb == g.scene_fb.get()) return;
    // A colour surface drawn alone at this depth surface's EDRAM address (same
    // pitch and samples) since: the depth is rebuilt from it before it is used.
    g.restore_rt = 0;
    if (g.guest_depth)
      if (const auto it = g.edram_color.find(g32(g.guest_depth + 28) & 0xFFF);
          it != g.edram_color.end() && it->second != g.guest_rt &&
          (g32(it->second + 24) & 0x3FFFF) == (g32(g.guest_depth + 24) & 0x3FFFF))
        g.restore_rt = it->second;
    g.cmd->barriers(RenderBarrierStage::GRAPHICS,
                    {RenderTextureBarrier(g.scene_color, RenderTextureLayout::COLOR_WRITE),
                     RenderTextureBarrier(g.scene_depth.get(), RenderTextureLayout::DEPTH_WRITE)});
    g.cmd->setFramebuffer(g.scene_fb.get());
    g.bound_fb = g.scene_fb.get();
    g.bound_color = g.scene_color;
    g.bound_scene = true;
    g.bound_w = g.scene_tw;
    g.bound_h = g.scene_th;
    if (const auto owed = g.clear_on_bind.find(g.scene_key); owed != g.clear_on_bind.end()) {
      const Native::OwedClear c = owed->second;
      g.clear_on_bind.erase(owed);
      if (c.color) g.cmd->clearColor(0, c.color_value);
      if (c.depth) {
        depth_will_change();
        g.cmd->clearDepthStencil(true, true, c.depth_value, 0);
        g.depth_clear = c.depth_value;
        g.depth_written = false;
        ++g.depth_gen;
      }
      NTRACE("  owed resolve clears applied to rt {:08X} depth {:08X}", g.guest_rt, g.guest_depth);
    }
    return;
  }
  Native::ColorTarget& t = color_target(g.guest_rt);
  if (g.bound_fb == t.fb.get()) return;
  // EDRAM aliasing: a surface at the same EDRAM base as the scene starts out
  // holding the scene's pixels on the 360 (the title's post pass blends onto
  // them: without this the never-cleared target smeared old frames). The base
  // is the low 12 bits of the surface's colour info word (+24). A set that
  // draws into this colour target already holds them.
  const uint32_t base_rt = g32(g.guest_rt + 24) & 0xFFF;
  if (g.bound_scene && g.scene_surface && g.scene_color != t.color.get() && g.scene_tw == g_scene_w &&
      g.scene_th == g_scene_h && base_rt == (g32(g.scene_surface + 24) & 0xFFF)) {
    copy_scene_to(t.color.get());
    g.cmd->barriers(RenderBarrierStage::GRAPHICS,
                    {RenderTextureBarrier(g.scene_color, RenderTextureLayout::COLOR_WRITE),
                     RenderTextureBarrier(g.scene_depth.get(), RenderTextureLayout::DEPTH_WRITE)});
  }
  g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(t.color.get(), RenderTextureLayout::COLOR_WRITE));
  g.cmd->setFramebuffer(t.fb.get());
  g.bound_fb = t.fb.get();
  g.bound_color = t.color.get();
  g.bound_scene = false;
  g.bound_w = g_scene_w;
  g.bound_h = g_scene_h;
}

void begin_frame() {
  if (g.recording) return;
  if (!g.warm.empty() && REXCVAR_GET(gpu_native_warm_shaders)) warm_registered_shaders();
  {
    std::lock_guard<std::mutex> lock(g.warm_mutex);
    for (auto& [key, p] : g.warm_made)
      if (const auto it = g.pipelines.find(key); it == g.pipelines.end()) g.pipelines.emplace(key, std::move(p));
    g.warm_made.clear();
  }
  // The Anti-Aliasing setting changed: wait for the frames in flight, then new
  // scene targets with the new sample count (pipelines are keyed by it, and
  // the depth resolve pass is rebuilt for it).
  if (const RenderSampleCounts want = pick_msaa(); g.scene_color && want != g.msaa.sampleCount) {
    for (uint32_t i = 0; i < kFrames; ++i) {
      if (!g.pending[i]) continue;
      g.queue->waitForCommandFence(g.fences[i].get());
      g.pending[i] = false;
    }
    g.msaa.sampleCount = want;
    g.bound_fb = nullptr;
    g.bound_color = nullptr;
    g.scene_fb.reset();
    g.scene_sets.clear();  // parked passes are remade at the new sample count when next bound
    g.depth_view.reset();
    if (g.scene_key_set) {
      const uint32_t rt = uint32_t(g.scene_key >> 32), depth = uint32_t(g.scene_key);
      uint32_t w = 0, h = 0;
      set_size_for(rt, depth, w, h);
      create_scene_targets(msaa_for(rt, depth), rt, w, h);
    } else
      create_scene_targets(g.msaa);
    for (auto& [dest, r] : g.resolved) {  // full-size depth resolves: made again from the new targets
      if (!g.depth_sets.count(dest) || !r.texture || r.span != 0) continue;
      g.retired_textures.push_back({g.frames, std::move(r.texture), r.slot});
      r.depth_pending = false;
      r.depth_src = nullptr;
    }
    g.depth_pending.clear();
    g.depth_sets.clear();
    REXKRNL_INFO("ORNATIVE anti-aliasing: {}x MSAA", uint32_t(want));
  }
  // A new stage (a level, or the front end) loads its textures where the last
  // one's were: the same fetch constants can then name a different picture
  // (Cradle after a run of levels showed red from an earlier one), and the
  // old ones would stay in video memory. Start the texture cache afresh.
  if (const uint32_t stage = stage_number(); stage != g.textures_stage && !REXCVAR_GET(gpu_native_stage_textures)) {
    // Test (cache kept across stages): how many cached pictures the new stage
    // has overwritten in memory, i.e. would be drawn with the old picture.
    g.textures_stage = stage;
    size_t cached = 0, stale = 0;
    for (const auto& [key, t] : g.textures_by_fetch) {
      if (!t.texture || t.dynamic) continue;
      ++cached;
      uint64_t h = 0;
      if (hash_bytes_seh(t.addr, t.span, &h) && h != t.content) ++stale;
    }
    REXKRNL_INFO("ORNATIVE stage {}: {} cached level textures, {} now hold a different picture", stage, cached, stale);
  } else if (const uint32_t stage = stage_number(); stage != g.textures_stage) {
    g.textures_stage = stage;
    size_t n = 0;
    for (auto& [key, t] : g.textures_by_fetch) {
      if (!t.texture) continue;
      g.retired_textures.push_back({g.frames, std::move(t.texture), t.slot});
      ++n;
    }
    g.textures_by_fetch.clear();  // (a texture still on the worker is dropped with its job)
    if (n) REXKRNL_INFO("ORNATIVE stage {}: {} textures from the last stage released", stage, n);
  }
  g.cmd->begin();
  reset_vulkan_queries();
  g.layout_bound = false;
  g.bound_fb = nullptr;
  bind_target();
  if (g.timestamps) g.cmd->writeTimestamp(g.timestamps.get(), g.slot * 2);
  g.prof_active = g.prof_pool && REXCVAR_GET(gpu_native_profile_frame) > 0 &&
                  g.frames + 1 == uint64_t(REXCVAR_GET(gpu_native_profile_frame));
  g.prof_labels.clear();
  if (g.prof_active) { g.cmd->writeTimestamp(g.prof_pool.get(), 0); g.prof_labels.push_back("start"); }
  g.recording = true;
  g.draws = g.skipped = 0;
  std::memset(g.skip_reason, 0, sizeof(g.skip_reason));
}

void prof_mark(std::string label) {
  if (!g.prof_active || g.prof_labels.size() >= 4096) return;
  g.cmd->writeTimestamp(g.prof_pool.get(), uint32_t(g.prof_labels.size()));
  g.prof_labels.push_back(std::move(label));
}

void set_common_state(uint32_t dev) {
  // The root signature and the bindless tables stay bound for the whole
  // command list (re-setting them every draw reset all root bindings).
  if (!g.layout_bound) {
    g.cmd->setGraphicsPipelineLayout(g.layout.get());
    g.cmd->setGraphicsDescriptorSet(g.textures.get(), 0);
    g.cmd->setGraphicsDescriptorSet(g.textures.get(), 1);
    g.cmd->setGraphicsDescriptorSet(g.textures.get(), 2);
    g.cmd->setGraphicsDescriptorSet(g.samplers.get(), 3);
    g.layout_bound = true;
  }
  const uint32_t vp = dev + kDevViewport;
  float f[6];
  for (int i = 0; i < 6; ++i) {
    const uint32_t u = g32(vp + i * 4);
    std::memcpy(&f[i], &u, 4);
  }
  if (f[2] <= 0 || f[3] <= 0 || (g32(dev + 10572) & 0x3F) == 0) {
    f[0] = f[1] = 0; f[2] = float(kGuestW); f[3] = float(kGuestH); f[4] = 0; f[5] = 1;
  }
  for (int i = 0; i < 4; ++i) f[i] *= float(g_scene_scale);  // guest pixels -> scene pixels
  // Perfect Dark sets an inverted depth range (MinZ 1, MaxZ 0: reversed depth,
  // cleared to 0 and tested GREATER_EQUAL); GoldenEye's "MaxZ 1 if not above
  // MinZ" flattened all its depth to 1. Kept as given, within [0, 1].
  const float zmin = std::clamp(f[4], 0.0f, 1.0f), zmax = std::clamp(f[5], 0.0f, 1.0f);
  RenderViewport viewport(f[0], f[1], f[2], f[3], zmin, zmax == zmin ? 1.0f : zmax);
  g.cmd->setViewports(&viewport, 1);
  // The hardware scissor Xenos applies to every draw (the cut-scene letterbox
  // clips the sun's glow with it); the whole target if it is empty.
  {
    const uint32_t tw = g.bound_w ? g.bound_w : g_scene_w, th = g.bound_h ? g.bound_h : g_scene_h;
    RenderRect scissor(0, 0, tw, th);
    const uint32_t tl = g32(dev + kDevWindowScissorTL), br = g32(dev + kDevWindowScissorBR);
    const int32_t l = tl & 0x7FFF, t = (tl >> 16) & 0x7FFF, r = br & 0x7FFF, b = (br >> 16) & 0x7FFF;
    if (r > l && b > t) {
      const int32_t k = int32_t(g_scene_scale), w = int32_t(tw), h = int32_t(th);
      scissor = RenderRect(std::min(l * k, w), std::min(t * k, h), std::min(r * k, w), std::min(b * k, h));
    }
    g.cmd->setScissors(&scissor, 1);
  }

  // Constants: guest big-endian floats -> little-endian.
  // Each shader's constants are only the bytes it reads (<hash>.cbsize), and a
  // block identical to one already uploaded for this submission is reused:
  // copying full 4 KB + 3.5 KB blocks per draw took ~3.7 ms of GPU time a frame.
  uint8_t* cbase = g.const_ptr + g.slot * kSlotConsts;
  auto swap_copy = [](uint8_t* dst, uint32_t src, uint32_t bytes) {
    const uint8_t* s = gptr(src);
    for (uint32_t i = 0; i < bytes; i += 4) {
      uint32_t w;
      std::memcpy(&w, s + i, 4);
      w = __builtin_bswap32(w);
      std::memcpy(dst + i, &w, 4);
    }
  };
  auto upload_block = [&](uint32_t src, uint32_t bytes) -> uint64_t {
    alignas(16) uint8_t tmp[4096];
    bytes = std::max<uint32_t>(16, std::min<uint32_t>(bytes, 4096));
    swap_copy(tmp, src, bytes);
    if (REXCVAR_GET(gpu_native_query_test) == 20 && src == dev + kDevVsConsts && bytes > 98 * 16)  // test: fog off
      std::memset(tmp + 98 * 16, 0, 4);
    if (REXCVAR_GET(gpu_native_query_test) == 21 && src == dev + kDevVsConsts && bytes > 100 * 16) {  // test: red fog
      const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
      std::memcpy(tmp + 99 * 16, red, 16);
    }
    const uint64_t key = XXH3_64bits_withSeed(tmp, bytes, bytes);
    const auto it = g.const_dedup.find(key);
    if (it != g.const_dedup.end()) return it->second;
    const uint64_t off = const_alloc(bytes);
    if (off != UINT64_MAX) { std::memcpy(cbase + off, tmp, bytes); g.const_dedup[key] = off; }
    return off;
  };
  const uint64_t vs = upload_block(dev + kDevVsConsts, g_vs_cb_bytes);
  const uint64_t ps = upload_block(dev + kDevPsConsts, g_ps_cb_bytes);
  const uint64_t shared = const_alloc(512);
  if (vs == UINT64_MAX || ps == UINT64_MAX || shared == UINT64_MAX) return;
  alignas(16) uint32_t sh[128] = {};
  for (uint32_t s = 0; s < 16; ++s) {
    sh[s] = texture_slot(g_textures[s]);       // Texture2D descriptor index
    // TextureCube descriptor index (XenosRecomp's shared constants: 2D at s, 3D
    // at 16 + s, cube at 32 + s, sampler at 48 + s): an uploaded cube map, or a
    // resolve into one (a 2D resolve never, whatever the texture object says).
    uint32_t cube_slot = g.white_cube_slot;
    if (g_textures[s] && sh[s]) {
      if (const ResolvedTexture* r = resolved_for(g_textures[s])) {
        if (r->cube) cube_slot = sh[s];
      } else if (((g32(g_textures[s] + 48) >> 9) & 3) == 3) {
        cube_slot = sh[s];
      }
    }
    sh[32 + s] = cube_slot;
    sh[48 + s] = sampler_slot(dev, s, g_textures[s]);  // sampler descriptor index
  }
  // Alpha test: the translated pixel shaders clip(alpha - threshold); only
  // GREATER / GREATER_EQUAL tests map onto that, others draw untested.
  const DrawState st = read_state(dev);
  float threshold = 0.0f;
  if (st.alpha_test && (st.alpha_func == 4 || st.alpha_func == 6)) {
    threshold = st.alpha_ref > 1.0f ? st.alpha_ref / 255.0f : st.alpha_ref;
    if (st.alpha_func == 4) threshold += 1.0f / 512.0f;
  }
  std::memcpy(&sh[68], &threshold, 4);  // g_AlphaThreshold (c17.x)
  sh[64] = (g32(dev + kDevVsBools) & 0xFFFF) | ((g32(dev + kDevPsBools) & 0xFFFF) << 16);  // g_Booleans
  sh[65] = 0;  // g_SwappedTexcoords (16-bit components are fixed on upload)
  sh[69] = g_spec_bits;  // g_NativeSpecConstants (c17.y)
  NTRACE("  bools {:08X} alpha ref {} threshold {} sampler0 slot {} fetch d0 {:08X} d3 {:08X}", sh[64], st.alpha_ref,
         threshold, sh[48], g32(dev + 1152), g32(dev + 1152 + 12));
  if (tracing()) {
    const float* c = reinterpret_cast<const float*>(cbase + vs);  // (GoldenEye read the upload ring here)
    std::string guest;
    // OutRun's lighting registers too: c16-c21 (R_DRANGE, EYE, MATERIAL_DIFFUSE/AMBIENT/EMISSIVE, LIGHT_DIFFUSE).
    for (uint32_t i : {0u, 1u, 2u, 3u, 7u, 16u, 17u, 18u, 19u, 20u, 21u, 26u, 27u, 98u, 99u, 100u}) {
      float f4[4];
      for (int k = 0; k < 4; ++k) { const uint32_t u = g32(dev + kDevVsConsts + i * 16 + k * 4); std::memcpy(&f4[k], &u, 4); }
      guest += fmt::format(" c{}[{} {} {} {}]", i, f4[0], f4[1], f4[2], f4[3]);
    }
    NTRACE("  guest vs consts{} ({} bytes uploaded)", guest, g_vs_cb_bytes);
    std::string pguest;
    for (uint32_t i : {3u, 4u, 5u, 6u, 7u, 22u, 23u, 24u, 38u}) {
      float f4[4];
      for (int k = 0; k < 4; ++k) { const uint32_t u = g32(dev + kDevPsConsts + i * 16 + k * 4); std::memcpy(&f4[k], &u, 4); }
      pguest += fmt::format(" c{}[{} {} {} {}]", i, f4[0], f4[1], f4[2], f4[3]);
    }
    NTRACE("  guest ps consts{}", pguest);
    NTRACE("  vs c0-3 [{} {} {} {}] [{} {} {} {}] [{} {} {} {}] [{} {} {} {}]", c[0], c[1], c[2], c[3], c[4], c[5], c[6],
           c[7], c[8], c[9], c[10], c[11], c[12], c[13], c[14], c[15]);
    const float* pc = reinterpret_cast<const float*>(cbase + ps);
    NTRACE("  ps c0-1 [{} {} {} {}] [{} {} {} {}] ({} bytes)", pc[0], pc[1], pc[2], pc[3], pc[4], pc[5], pc[6], pc[7],
           g_ps_cb_bytes);
  }
  // c18: position scale / offset. PA_CL_VTE_CNTL (+10572, written by the
  // VIEWPORTENABLE setter: 0x43F on, 0x400 off). With the viewport transform off
  // the shader outputs window coordinates, mapped here to NDC for the target.
  const bool vte = (g32(dev + 10572) & 0x3F) != 0;
  // D3D9-style pixel centres would need everything moved right and down by
  // half a pixel (XenosRecomp's g_HalfPixelOffset), but OutRun's frames line
  // up with Xenos without it: with it the title and menus sat 1-1.5 pixels
  // right and down (measured against Xenos captures, 4 Nov) and the menus'
  // one-pixel red edge at x = 0 smeared into a 2-3 pixel band.
  const bool half = REXCVAR_GET(gpu_native_half_pixel);
  float pos[4] = {1.0f, 1.0f, half ? 1.0f / kGuestW : 0.0f, half ? -1.0f / kGuestH : 0.0f};
  if (!vte) { pos[0] = 2.0f / kGuestW; pos[1] = -2.0f / kGuestH; pos[2] = -1.0f + pos[2]; pos[3] = 1.0f + pos[3]; }
  std::memcpy(&sh[72], pos, sizeof(pos));
  std::memcpy(cbase + shared, sh, sizeof(sh));
  if (g_vulkan) {
    const uint64_t at = g.const_gpu[g.slot]->getDeviceAddress();
    const uint64_t addrs[3] = {at + vs, at + ps, at + shared};
    g.cmd->setGraphicsPushConstants(0, addrs, 0, sizeof(addrs));
  } else {
    g.cmd->setGraphicsRootDescriptor(RenderBufferReference(g.const_gpu[g.slot].get(), vs), 0);
    g.cmd->setGraphicsRootDescriptor(RenderBufferReference(g.const_gpu[g.slot].get(), ps), 1);
    g.cmd->setGraphicsRootDescriptor(RenderBufferReference(g.const_gpu[g.slot].get(), shared), 2);
  }
}

// --- pipelines ----------------------------------------------------------------
//
// A pipeline's first creation on a PC costs the driver's compile: 13-29 ms for
// the one to three new pipelines a level shows when it starts or a new effect
// first appears, the only hitches left in play. The driver keeps what it
// compiled in its own disk cache (a pipeline it has seen costs ~0.1 ms), so
// every pipeline's description (shader hashes and state: no game data) goes on
// a list, and at boot a worker thread creates each listed pipeline whose
// shaders are cached; the game thread's own creation then finds it in the
// driver's cache. The list is the shader cache's pipelines.bin (what this PC
// has drawn) plus native_pipelines.bin beside the exe (every level, recorded
// on the developer's PC), so a first play starts warm too.

struct PipeRecord {  // one pipeline as listed (version 1, little-endian, no padding)
  uint64_t vs = 0, ps = 0;  // shader hashes
  uint8_t topo = 0, scene = 0, samples = 1, n_els = 0;
  uint8_t z_enable = 0, z_write = 0, z_func = 0, cull = 0;  // cull: 0 none, 1 front, 2 back
  uint8_t blend = 0, src = 0, dst = 0, op = 0, src_a = 0, dst_a = 0, op_a = 0, write_mask = 0;
  uint8_t alpha_to_mask = 0;
  // Stencil (all zero: off; lists written before it had these bytes zero): on,
  // then clockwise and counter-clockwise faces' func | fail << 3 and
  // pass | zfail << 3 (Xenos values), the read and write masks.
  uint8_t stencil = 0, s_cw = 0, s_cw2 = 0, s_ccw = 0, s_ccw2 = 0, s_mask = 0, s_write = 0;
  struct El {
    uint32_t type = 0;
    uint8_t usage = 0, index = 0;
    int8_t exp = 0;
    uint8_t reserved = 0;
  } els[16];
  // Version 2: depth bias, as Direct3D 12 takes it (D24 steps; slope per pixel).
  int32_t depth_bias = 0;
  float slope_bias = 0.0f;
};
static_assert(sizeof(PipeRecord) == 176, "PipeRecord is stored as it is");
constexpr uint32_t kPipeListMagic = 0x4C504450;  // "PDPL"
constexpr uint32_t kPipeListVersion = 2;
constexpr uint32_t kPipeRecordV1Size = 168;  // version 1 lists (no depth bias) still load

// What reaches the pipeline, nothing else: state the pipeline ignores (blend
// factors with blending off, the depth test with depth off) is zeroed, so it
// doesn't make a second pipeline. Shaders by hash, not by host object: a
// shader the game releases and makes again keeps its pipelines.
PipeRecord make_record(uint64_t vs, uint64_t ps, const std::vector<Element>& els, RenderPrimitiveTopology topo) {
  const DrawState& st = g_draw_state;
  PipeRecord r;
  r.vs = vs;
  r.ps = ps;
  r.topo = uint8_t(topo);
  r.scene = g.bound_scene;
  r.samples = g.bound_scene ? uint8_t(g.scene_msaa.sampleCount) : 1;
  r.n_els = uint8_t(std::min<size_t>(els.size(), std::size(r.els)));
  for (uint32_t e = 0; e < r.n_els; ++e) {
    r.els[e].type = els[e].type;
    r.els[e].usage = els[e].usage;
    r.els[e].index = els[e].index;
    r.els[e].exp = els[e].exp;
  }
  if (r.scene && st.z_enable) { r.z_enable = 1; r.z_write = st.z_write; r.z_func = st.z_func; }
  // PA_SU_SC_MODE_CNTL: bit 0 cull front, bit 1 cull back, bit 2 front faces
  // are clockwise (else counter-clockwise). r.cull: 1 culls clockwise, 2
  // counter-clockwise triangles; bit 2: the front faces are counter-clockwise
  // (the pipeline's front face follows the game, so a pixel shader reading
  // the face sees the 360's: the guard rails' two-sided colour came out as
  // the dark back colour).
  const bool cw_front = (st.cull >> 2) & 1;
  const bool cull_cw = ((st.cull & 2) && !cw_front) || ((st.cull & 1) && cw_front);
  r.cull = uint8_t(((st.cull & 3) == 0 ? 0 : cull_cw ? 1 : 2) | (cw_front ? 0 : 4));
  if (r.scene && st.stencil) {
    // RB_DEPTHCONTROL's first ops are for front faces, the second (two-sided
    // stencil) for back faces; one-sided, the front ops serve both.
    const uint32_t front = st.stencil_ops & 0xFFF, back = st.stencil_bf ? (st.stencil_ops >> 12) & 0xFFF : front;
    const uint32_t cw = cw_front ? front : back, ccw = cw_front ? back : front;
    r.stencil = 1;
    r.s_cw = uint8_t(cw & 0x3F); r.s_cw2 = uint8_t(cw >> 6);
    r.s_ccw = uint8_t(ccw & 0x3F); r.s_ccw2 = uint8_t(ccw >> 6);
    r.s_mask = st.stencil_mask;
    r.s_write = st.stencil_write;
  }
  if (st.blend) {
    r.blend = 1;
    r.src = st.src; r.dst = st.dst; r.op = st.op;
    r.src_a = st.src_a; r.dst_a = st.dst_a; r.op_a = st.op_a;
  }
  r.write_mask = st.write_mask;
  r.alpha_to_mask = r.scene && st.alpha_to_mask && r.samples > 1;
  if (r.scene && r.z_enable && (st.poly_offset != 0.0f || st.poly_scale != 0.0f)) {
    // The Xenos path's conversion (draw_util::GetD3D10IntegerPolygonOffset):
    // the constant in D24 steps, rounded away from zero; the slope from 1/16
    // pixel units, times the Render Scale (pixels are smaller).
    const int32_t steps = int32_t(std::ceil(std::abs(st.poly_offset) * float((1u << 24) - 1)));
    r.depth_bias = st.poly_offset < 0 ? -steps : steps;
    r.slope_bias = st.poly_scale * (1.0f / 16.0f) * float(g_scene_scale);
  }
  return r;
}

// The creation description for a record (inputs: storage the description points into).
RenderGraphicsPipelineDesc describe_pipeline(const PipeRecord& r, RenderShader* vs, RenderShader* ps,
                                             std::vector<RenderInputElement>& inputs, RenderInputSlot& slot) {
  inputs.clear();
  uint32_t offset = 0;
  for (uint32_t e = 0; e < r.n_els; ++e) {
    Element el{};
    el.type = r.els[e].type;
    el.usage = r.els[e].usage;
    el.index = r.els[e].index;
    el.exp = r.els[e].exp;
    const HostFmt hf = host_format(el);
    if (el.usage < std::size(kSemantics))
      inputs.emplace_back(kSemantics[el.usage], el.index, vk_location(el.usage, el.index), hf.format, 0, offset);
    offset += hf.size;
  }
  slot = RenderInputSlot(0, offset);
  RenderGraphicsPipelineDesc d;
  d.pipelineLayout = g.layout.get();
  d.vertexShader = vs;
  d.pixelShader = ps;
  d.primitiveTopology = RenderPrimitiveTopology(r.topo);
  d.renderTargetFormat[0] = kSceneFormat;
  d.multisampling = RenderMultisampling(RenderSampleCounts(r.samples));
  d.alphaToCoverageEnabled = r.alpha_to_mask;
  RenderBlendDesc blend = RenderBlendDesc::Copy();
  blend.blendEnabled = r.blend;
  blend.srcBlend = to_blend(r.src); blend.dstBlend = to_blend(r.dst); blend.blendOp = to_blend_op(r.op);
  blend.srcBlendAlpha = to_blend_alpha(r.src_a); blend.dstBlendAlpha = to_blend_alpha(r.dst_a);
  blend.blendOpAlpha = to_blend_op(r.op_a);
  blend.renderTargetWriteMask = ps ? r.write_mask : 0;  // depth-only draws write no colour
  d.renderTargetBlend[0] = blend;
  d.depthEnabled = r.z_enable;
  d.depthWriteEnabled = r.z_enable && r.z_write;
  d.depthFunction = to_cmp(r.z_func);
  const bool ccw_front = (r.cull & 4) != 0;
  if (r.stencil) {  // the reference is set with each draw (OMSetStencilRef)
    auto face = [](uint8_t a, uint8_t b) {
      RenderStencilFaceDesc f;
      f.compareFunction = to_cmp(a & 7);
      f.failOp = to_stencil_op(a >> 3);
      f.passOp = to_stencil_op(b);
      f.depthFailOp = to_stencil_op(b >> 3);
      return f;
    };
    d.stencilEnabled = true;
    d.stencilReadMask = r.s_mask;
    d.stencilWriteMask = r.s_write;
    const RenderStencilFaceDesc cw = face(r.s_cw, r.s_cw2), ccw = face(r.s_ccw, r.s_ccw2);
    d.stencilFrontFace = ccw_front ? ccw : cw;
    d.stencilBackFace = ccw_front ? cw : ccw;
  }
  d.frontFace = ccw_front ? RenderFrontFace::COUNTER_CLOCKWISE : RenderFrontFace::CLOCKWISE;
  const uint8_t culled = r.cull & 3;  // 1 clockwise, 2 counter-clockwise triangles
  d.cullMode = culled == 0 ? RenderCullMode::NONE
               : (culled == 1) != ccw_front ? RenderCullMode::FRONT : RenderCullMode::BACK;
  d.depthBias = r.depth_bias;
  d.slopeScaledDepthBias = r.slope_bias;
  d.renderTargetCount = 1;
  d.depthTargetFormat = r.scene ? kDepthFormat : RenderFormat::UNKNOWN;
  d.inputSlots = &slot;
  d.inputSlotsCount = 1;
  d.inputElements = inputs.data();
  d.inputElementsCount = uint32_t(inputs.size());
  return d;
}

struct PipeList {
  std::mutex mutex;
  std::unordered_set<uint64_t> listed;  // records in this PC's pipelines.bin
  bool loaded = false;
};
PipeList g_pipe_list;

// (pipelines2.bin since version 2; this PC's version 1 list is still read)
std::wstring user_pipe_list_path() { return outrun::native_shaders::CacheDir() + L"\\pipelines2.bin"; }
std::wstring old_user_pipe_list_path() { return outrun::native_shaders::CacheDir() + L"\\pipelines.bin"; }

std::vector<PipeRecord> read_pipe_list(const std::wstring& path) {
  std::vector<PipeRecord> out;
  FILE* f = _wfopen(path.c_str(), L"rb");
  if (!f) return out;
  uint32_t head[3] = {};
  if (std::fread(head, 4, 3, f) == 3 && head[0] == kPipeListMagic &&
      ((head[1] == kPipeListVersion && head[2] == sizeof(PipeRecord)) || (head[1] == 1 && head[2] == kPipeRecordV1Size))) {
    const size_t size = head[2];
    // Records; a header again in between is skipped (the first test builds
    // wrote one before every record).
    for (;;) {
      uint32_t peek[3] = {};
      const long at = std::ftell(f);
      if (std::fread(peek, 4, 3, f) != 3) break;
      if (std::memcmp(peek, head, sizeof(head)) != 0) std::fseek(f, at, SEEK_SET);
      PipeRecord r;
      if (std::fread(&r, size, 1, f) != 1) break;
      out.push_back(r);
    }
  }
  std::fclose(f);
  return out;
}

// Puts a pipeline the game thread just made on this PC's list (once).
void list_pipeline(const PipeRecord& r, uint64_t key) {
  std::lock_guard<std::mutex> lock(g_pipe_list.mutex);
  if (!g_pipe_list.loaded) {
    for (const PipeRecord& old : read_pipe_list(user_pipe_list_path()))
      g_pipe_list.listed.insert(XXH3_64bits(&old, sizeof(old)));
    g_pipe_list.loaded = true;
  }
  if (!g_pipe_list.listed.insert(key).second) return;
  const std::wstring path = user_pipe_list_path();
  FILE* f = _wfopen(path.c_str(), L"ab");
  if (!f) return;
  std::fseek(f, 0, SEEK_END);  // (append mode reports position 0 until the first write)
  if (std::ftell(f) == 0) {
    const uint32_t head[3] = {kPipeListMagic, kPipeListVersion, uint32_t(sizeof(PipeRecord))};
    std::fwrite(head, 4, 3, f);
  }
  std::fwrite(&r, sizeof(r), 1, f);
  std::fclose(f);
}

// The boot worker: creates every listed pipeline whose shaders are cached (and
// hands it to the renderer), then again for those whose
// shaders were not translated yet: every 3 s at first (OutRun creates nearly
// all its shaders in the first seconds, so on a first run they are translated
// by then), every 10 s after the first half minute, for up to 10 minutes.
void warm_pipelines() {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  std::vector<PipeRecord> todo = read_pipe_list(exe_dir() + L"\\native_pipelines.bin");
  const size_t shipped = todo.size();
  for (const PipeRecord& r : read_pipe_list(old_user_pipe_list_path())) todo.push_back(r);
  for (const PipeRecord& r : read_pipe_list(user_pipe_list_path())) todo.push_back(r);
  {
    // Multisampled scene pipelines at the current Anti-Aliasing setting's
    // sample count (it may have changed since they were recorded); passes the
    // 360 draws single-sampled stay at one sample.
    const uint8_t samples = uint8_t(g.msaa.sampleCount);
    std::unordered_set<uint64_t> seen;
    std::vector<PipeRecord> unique;
    for (PipeRecord r : todo) {
      if (r.scene && r.samples > 1) r.samples = samples;
      if (r.samples <= 1) r.alpha_to_mask = 0;
      if (seen.insert(XXH3_64bits(&r, sizeof(r))).second) unique.push_back(r);
    }
    todo = std::move(unique);
  }
  if (todo.empty()) return;
  std::unordered_map<uint64_t, std::unique_ptr<RenderShader>> shaders;
  auto shader = [&](uint64_t hash, bool vertex) -> RenderShader* {
    auto& s = shaders[hash];
    if (!s) {
      HostShader hs;
      hs.hash = hash;
      if (load_cached_shader(hs, vertex)) s = std::move(hs.shader);
    }
    return s.get();
  };
  size_t made = 0;
  double ms = 0;
  for (int pass = 0; pass < 60 && !todo.empty(); ++pass) {
    if (pass) std::this_thread::sleep_for(std::chrono::seconds(pass <= 10 ? 3 : 10));
    std::vector<PipeRecord> later;
    const size_t before = made;
    for (const PipeRecord& r : todo) {
      RenderShader* vs = shader(r.vs, true);
      RenderShader* ps = r.ps ? shader(r.ps, false) : nullptr;
      if (!vs || (r.ps && !ps)) {
        later.push_back(r);
        if (!vs) shaders.erase(r.vs);  // look again next pass
        if (r.ps && !ps) shaders.erase(r.ps);
        continue;
      }
      std::vector<RenderInputElement> inputs;
      RenderInputSlot slot;
      const RenderGraphicsPipelineDesc d = describe_pipeline(r, vs, ps, inputs, slot);
      const double t0 = now_ms();
      std::unique_ptr<RenderPipeline> p = g.device->createGraphicsPipeline(d);
      ms += now_ms() - t0;
      ++made;
      // Handed to the renderer (begin_frame), keyed as pipeline_for keys it:
      // the first race then makes none (a driver without a shader cache would
      // compile them all again).
      if (pipeline_ok(p.get())) {
        std::lock_guard<std::mutex> lock(g.warm_mutex);
        g.warm_made.emplace_back(XXH3_64bits(&r, sizeof(r)), std::move(p));
      }
    }
    todo = std::move(later);
    if (made != before || pass == 0)
      REXKRNL_INFO("ORNATIVE pipeline warm-up: {} of the listed made in {:.0f} ms ({} from the shipped list), {} waiting "
                   "for their shaders", made, ms, shipped, todo.size());
  }
}

RenderPipeline* pipeline_for(uint64_t vs_hash, RenderShader* vs, uint64_t ps_hash, RenderShader* ps,
                             const std::vector<Element>& els, RenderPrimitiveTopology topo) {
  const PipeRecord r = make_record(vs_hash, ps_hash, els, topo);
  const uint64_t key = XXH3_64bits(&r, sizeof(r));
  const bool known = g.pipelines.count(key) != 0;
  auto& p = g.pipelines[key];
  if (p || known) return p.get();  // known && !p: creation failed before
  std::vector<RenderInputElement> inputs;
  RenderInputSlot slot;
  const RenderGraphicsPipelineDesc d = describe_pipeline(r, vs, ps, inputs, slot);
  if (REXCVAR_GET(gpu_native_log_creation)) {
    std::string desc;
    for (const RenderInputElement& in : inputs)
      desc += fmt::format(" {}{}:slot{}+{}:fmt{}", in.semanticName, in.semanticIndex, in.slotIndex,
                          in.alignedByteOffset, int(in.format));
    desc += fmt::format(" [slot {} stride {}]", slot.index, slot.stride);
    REXKRNL_INFO("ORNATIVE pipeline {:016X}:{}", key, desc);
  }
  const double t0 = now_ms();
  p = g.device->createGraphicsPipeline(d);
  g.pipe_ms += now_ms() - t0;
  ++g.pipe_n;
  drain_debug_messages("pipeline");
  if (p && !pipeline_ok(p.get())) {
    std::string desc;
    for (const RenderInputElement& in : inputs)
      desc += fmt::format(" {}{}:slot{}+{}:fmt{}", in.semanticName, in.semanticIndex, in.slotIndex,
                          in.alignedByteOffset, int(in.format));
    REXKRNL_WARN("ORNATIVE pipeline {:016X} failed to create; its draws are skipped (vs {:016X} ps {:016X} topo {} "
                 "samples {} scene {} blend {} {}/{}/{} alpha {}/{}/{} write {:X} depth {}/{}/{} stencil {}; inputs{} "
                 "[stride {}]; at {})",
                 key, vs_hash, ps_hash, r.topo, r.samples, r.scene, r.blend, r.src, r.dst, r.op, r.src_a, r.dst_a, r.op_a,
                 r.write_mask, r.z_enable, r.z_write, r.z_func, r.stencil, desc, slot.stride, outrun::player_log::Where());
    p.reset();
  }
  if (p) {
    // Listed with the alpha-to-coverage the game asked for, whatever the scene's
    // sample count now, so a list recorded at 1x also warms the anti-aliased
    // pipelines (warm_pipelines drops it again at 1x).
    PipeRecord listed = r;
    listed.alpha_to_mask = r.scene && g_draw_state.alpha_to_mask;
    list_pipeline(listed, XXH3_64bits(&listed, sizeof(listed)));
  }
  return p.get();
}

// One draw. indices == nullptr: non-indexed, `count` vertices from `first`.
// up_data != 0: vertices come from guest memory directly (DrawVerticesUP /
// Begin/EndVertices) with stride up_stride, as stream 0.
// idx_addr != 0: indexed draw, `count` indices from guest memory at idx_addr
// (32-bit when idx_wide, else 16-bit), read only when the cached list is stale.
// Applies a resolve's deferred clears to the scene target (bound here).
void apply_pending_clears() {
  if (!g.pending_clear_color && !g.pending_clear_depth) return;
  g.cmd->barriers(RenderBarrierStage::GRAPHICS,
                  {RenderTextureBarrier(g.scene_color, RenderTextureLayout::COLOR_WRITE),
                   RenderTextureBarrier(g.scene_depth.get(), RenderTextureLayout::DEPTH_WRITE)});
  g.cmd->setFramebuffer(g.scene_fb.get());
  if (g.pending_clear_color) g.cmd->clearColor(0, g.pending_color);
  if (g.pending_clear_depth) {
    depth_will_change();
    g.cmd->clearDepthStencil(true, true, g.pending_depth, 0);
    g.depth_clear = g.pending_depth;
    g.depth_written = false;
    ++g.depth_gen;
  }
  if (g.scene_key != g.pending_src_key)  // still owed to the resolve's source set
    g.clear_on_bind[g.pending_src_key] = {g.pending_clear_color, g.pending_clear_depth, g.pending_color, g.pending_depth};
  g.pending_clear_color = g.pending_clear_depth = false;
  g.bound_fb = nullptr;  // bind whatever the game has set again
  bind_target();
  NTRACE("  deferred resolve clears applied");
}

void native_draw(uint32_t dev, uint32_t prim, uint32_t count, const uint32_t* indices,
                 int32_t base_vertex, uint32_t first, uint32_t up_data, uint32_t up_stride,
                 bool restart_16 = true, uint32_t idx_addr = 0, bool idx_wide = false) {
  if (!ensure_device() || count == 0) return;
  begin_frame();
  const double t_start = now_ms();
  struct Total {
    double t0;
    ~Total() { g.sect[4] += now_ms() - t0; }
  } total{t_start};
  const uint32_t vs_obj = g32(dev + kDevVertexShader);
  RenderShader* vs = host_shader(vs_obj, true);
  // No pixel shader: a depth-only draw (OutRun's shadow-map and depth passes),
  // drawn with a pipeline that has no pixel stage, as Xenos does.
  const uint32_t ps_obj = g32(dev + kDevPixelShader);
  RenderShader* ps = ps_obj ? host_shader(ps_obj, false) : nullptr;
  if (!vs || (ps_obj && !ps)) {
    NTRACE("skip prim {} n {}: vs {:08X} {} ps {:08X} {}", prim, count, vs_obj, vs != nullptr,
           g32(dev + kDevPixelShader), ps != nullptr);
    {  // OutRun diagnosis: each shader object that can't be used, once (first 24).
      static std::unordered_set<uint32_t> seen;
      const uint32_t bad = !vs ? vs_obj : g32(dev + kDevPixelShader);
      if (bad && seen.size() < 24 && seen.insert(bad).second && readable_seh(bad, bad + 64)) {
        std::string words;
        for (uint32_t i = 0; i < 16; ++i) words += fmt::format(" {:08X}", g32(bad + i * 4));
        const bool v = !vs;
        const uint32_t cont = bad + (v ? 872 : 40);
        REXKRNL_WARN("ORNATIVE unusable {} object {:08X}:{} | blob {} | container@+{} {:08X} {:08X} {:08X}",
                     v ? "vs" : "ps", bad, words, g.created_blobs.count(bad) ? "known" : "none", v ? 872 : 40,
                     readable_seh(cont, cont + 12) ? g32(cont) : 0, readable_seh(cont, cont + 12) ? g32(cont + 4) : 0,
                     readable_seh(cont, cont + 12) ? g32(cont + 8) : 0);
      }
    }
    if (tracing() && ps_obj && !ps && readable_seh(ps_obj, ps_obj + 95)) {
      std::string w;
      for (uint32_t i = 0; i < 24; ++i) w += fmt::format(" {:08X}", g32(ps_obj + i * 4));
      NTRACE("ps object{}", w);
    }
    ++g.skipped; ++g.skip_reason[0]; return;
  }
  {  // gpu_native_hide_vs: leave out draws with that vertex shader (debugging; read every draw)
    const std::string hide_vs = REXCVAR_GET(gpu_native_hide_vs);
    if (!hide_vs.empty() && fmt::format("{:016X}", g.shaders[vs_obj].hash) == hide_vs) return;
  }
  {  // gpu_native_skip_ps: leave out draws with that pixel shader (debugging)
    static const std::string skip = REXCVAR_GET(gpu_native_skip_ps);
    if (!skip.empty() && ps && fmt::format("{:016X}", g.shaders[ps_obj].hash) == skip) return;
  }
  HostShader& vsh = g.shaders[vs_obj];
  if (!vsh.container.empty()) {
    const uint32_t shader_off = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(&vsh.container[24]));
    const uint32_t phys_off = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(&vsh.container[shader_off]));
    const uint32_t live = gpu_view(g32(vs_obj + 32));
    // OutRun's in-race vertex shaders can name microcode that isn't mapped at
    // that address (crash 3 Nov, first race frame): a fault while reading it
    // falls back to the container's own fetches.
    vsh.live_inputs = live && parse_live_inputs_seh(vsh.container, vsh, live + phys_off);
    if (live && !vsh.live_inputs) parse_vertex_inputs(vsh.container, vsh);
  }
  // A shader whose vertex fetches are still templates (format 0: D3D fills them
  // in for the bound declaration, which in hung mode it doesn't do; Perfect
  // Dark's quad and sprite shader) takes its layout from the declaration.
  const bool templ = std::any_of(vsh.inputs.begin(), vsh.inputs.end(),
                                 [](const Element& el) { return (el.type & 0x3F) == 0; });
  // The live microcode's fetches are what the 360 runs (D3D patched them for
  // the bound declaration): the declaration is read instead only for
  // templates, or when the live microcode could not be read.
  const bool use_decl = g32(dev + kDevVertexDecl) &&
                        (templ || !vsh.live_inputs || REXCVAR_GET(gpu_native_prefer_decl));
  const bool from_shader = !vsh.inputs.empty() && !use_decl;
  const double t_parsed = now_ms();
  g.sect[0] += t_parsed - t_start;
  const uint32_t decl = from_shader ? 0 : g32(dev + kDevVertexDecl);
  std::vector<Element> els = from_shader ? vsh.inputs : read_decl(decl);
  if (els.empty()) {
    static int logged = 0;
    if (logged++ < 4)
      REXKRNL_WARN("ORNATIVE empty decl {:08X}: +0 {:08X} +24 {:08X} +28 {:08X} +52 {:08X} {:08X} {:08X}", decl,
                   decl ? g32(decl) : 0, decl ? g32(decl + 24) : 0, decl ? g32(decl + 28) : 0,
                   decl ? g32(decl + 52) : 0, decl ? g32(decl + 56) : 0, decl ? g32(decl + 60) : 0);
    ++g.skipped; ++g.skip_reason[1]; return;
  }
  // A declaration without some input the shader declares (the traffic's
  // shadow volumes: position only, the shader also takes normal, colour and
  // texcoord): Direct3D 12 makes no pipeline then. On the 360 those fetches
  // stay unpatched and read data nothing uses (no pixel shader); here they
  // read the first element's.
  if (!from_shader)
    for (const Element& in : vsh.inputs)
      if (std::none_of(els.begin(), els.end(),
                       [&](const Element& el) { return el.usage == in.usage && el.index == in.index; })) {
        Element extra = els[0];
        extra.usage = in.usage;
        extra.index = in.index;
        els.push_back(extra);
      }

  // Index list (rebased to the vertex range actually used); fans -> lists.
  // Indexed draws keep the converted list on the GPU between frames, checked
  // against an xxHash of the guest indices.
  Native::IndexCacheEntry* ientry = nullptr;
  uint64_t icontent = 0;
  if (idx_addr) {
    const uint32_t bytes = count * (idx_wide ? 4 : 2);
    if (!hash_bytes_seh(idx_addr, bytes, &icontent)) { ++g.skipped; ++g.skip_reason[3]; return; }
    const uint64_t key = XXH3_64bits_withSeed(&idx_addr, 4, (uint64_t(count) << 32) ^ (uint64_t(prim) << 8) ^ idx_wide) ^
                         uint64_t(uint32_t(base_vertex)) * 0x9E3779B97F4A7C15ull;
    ientry = &g.index_cache[key];
    ientry->last_frame = g.frames;
  }
  // A traced frame decodes every draw afresh (the geometry log below needs the indices and vertices).
  const std::string hide = REXCVAR_GET(gpu_native_hide);  // test: see gpu_native_hide
  if (prim == 8) ientry = nullptr;  // rect lists: made-up corners, not cached
  const bool ihit = !tracing() && hide.empty() && ientry && ientry->buffer && ientry->content == icontent;
  std::vector<uint32_t> idx;
  // Rect lists (primitive 8): three corners per rectangle, the GPU makes the
  // fourth (v1 + v2 - v0). Each rect becomes two triangles whose fourth corner
  // is an extra vertex after the decoded ones (index kRectCorner + n until the
  // indices are rebased). Perfect Dark draws its Perfect Menu blur this way.
  constexpr uint32_t kRectCorner = 0xF0000000u;
  std::vector<uint32_t> rect_src;  // per rect: its three corners' indices
  uint32_t rects = 0;
  std::vector<uint32_t> hidden_tris;  // gpu_native_hide's triangles, traced below
  int64_t lo = 0;
  uint32_t vcount = 0, icount = 0;
  RenderPrimitiveTopology topo = RenderPrimitiveTopology::TRIANGLE_LIST;
  if (ihit) {
    lo = ientry->lo; vcount = ientry->vcount; icount = ientry->count; topo = ientry->topo;
  } else {
  if (idx_addr) {
    idx.resize(count);
    for (uint32_t i = 0; i < count; ++i) idx[i] = idx_wide ? g32(idx_addr + i * 4) : g16(idx_addr + i * 2);
  } else if (indices) idx.assign(indices, indices + count);
  else for (uint32_t i = 0; i < count; ++i) idx.push_back(first + i);
  // Everything becomes a triangle (or point/line) list. Index 0xFFFF / 0xFFFFFFFF
  // is the primitive-restart cut (D3DRS_PRIMITIVERESETINDEX): strips and fans
  // restart there, list triangles touching it are dropped.
  const uint32_t restart = ((indices || idx_addr) && restart_16) ? 0xFFFFu : 0xFFFFFFFFu;
  std::vector<uint32_t> list;
  switch (prim) {
    case 4:
      for (size_t i = 0; i + 2 < idx.size(); i += 3)
        if (idx[i] != restart && idx[i + 1] != restart && idx[i + 2] != restart)
          list.insert(list.end(), {idx[i], idx[i + 1], idx[i + 2]});
      break;
    case 5: case 6: {  // fan / strip, split at restart
      size_t start = 0;
      for (size_t i = 0; i <= idx.size(); ++i) {
        if (i < idx.size() && idx[i] != restart) continue;
        for (size_t k = start; k + 2 < i; ++k) {
          if (prim == 5) list.insert(list.end(), {idx[start], idx[k + 1], idx[k + 2]});
          else if (((k - start) & 1) == 0) list.insert(list.end(), {idx[k], idx[k + 1], idx[k + 2]});
          else list.insert(list.end(), {idx[k + 1], idx[k], idx[k + 2]});
        }
        start = i + 1;
      }
      break;
    }
    case 13:  // quad list
      for (size_t i = 0; i + 3 < idx.size(); i += 4)
        list.insert(list.end(), {idx[i], idx[i + 1], idx[i + 2], idx[i], idx[i + 2], idx[i + 3]});
      break;
    case 8:  // rect list
      for (size_t i = 0; i + 2 < idx.size(); i += 3) {
        const uint32_t d = kRectCorner + rects++;
        rect_src.insert(rect_src.end(), {idx[i], idx[i + 1], idx[i + 2]});
        list.insert(list.end(), {idx[i], idx[i + 1], idx[i + 2], idx[i + 2], idx[i + 1], d});
      }
      break;
    case 1: topo = RenderPrimitiveTopology::POINT_LIST; list = idx; break;
    case 2: topo = RenderPrimitiveTopology::LINE_LIST; list = idx; break;
    case 3: topo = RenderPrimitiveTopology::LINE_STRIP; list = idx; break;
    default: ++g.skipped; ++g.skip_reason[2]; return;
  }
  idx.swap(list);
  if (idx.empty()) return;
  if (!hide.empty()) {  // gpu_native_hide: "a-b" draws, or "d:a-b" triangles of draw d
    unsigned d = 0, a = 0, b = 0;
    if (std::sscanf(hide.c_str(), "%u:%u-%u", &d, &a, &b) == 3) {
      if (g.draws == d && prim != 1 && prim != 2 && prim != 3) {
        std::vector<uint32_t> kept;
        hidden_tris.clear();
        for (size_t t = 0; t + 2 < idx.size(); t += 3)
          if (t / 3 < a || t / 3 > b) kept.insert(kept.end(), {idx[t], idx[t + 1], idx[t + 2]});
          else hidden_tris.insert(hidden_tris.end(), {idx[t], idx[t + 1], idx[t + 2]});
        if (kept.empty()) { ++g.draws; return; }
        idx.swap(kept);
      }
    } else if (std::sscanf(hide.c_str(), "%u-%u", &a, &b) == 2 && g.draws >= a && g.draws <= b) {
      ++g.draws;
      return;
    }
  }
  lo = INT64_MAX;
  int64_t hi = INT64_MIN;
  for (uint32_t i : idx) {
    if (rects && i >= kRectCorner) continue;
    lo = std::min<int64_t>(lo, int64_t(i) + base_vertex);
    hi = std::max<int64_t>(hi, int64_t(i) + base_vertex);
  }
  if (lo < 0 || hi - lo > 1'000'000) { ++g.skipped; ++g.skip_reason[3]; return; }
  vcount = uint32_t(hi - lo + 1);
  icount = uint32_t(idx.size());
  }
  const double t_indexed = now_ms();
  g.sect[1] += t_indexed - t_parsed;

  // Sources per fetch slot / stream. The shader path reads the device's vertex
  // fetch constants (device+1152 + slot*8: physical address | type, size |
  // endian); fetch constants hold physical addresses, which the CPU sees at
  // 0xA0000000+. The declaration path uses SetStreamSource; UP draws read
  // up_data. Everything is decoded into one interleaved host stream.
  std::vector<uint16_t> keys;
  std::vector<Source> sources;
  for (const Element& el : els) {
    if (std::find(keys.begin(), keys.end(), el.stream) != keys.end()) continue;
    keys.push_back(el.stream);
    Source src;
    if (up_data) { src.addr = up_data; src.stride = up_stride; src.endian = 2 | 4; }
    else if (from_shader) {
      const uint32_t d0 = g32(dev + 1152 + el.stream * 8), d1 = g32(dev + 1152 + el.stream * 8 + 4);
      const uint32_t phys = d0 & 0x1FFFFFFCu;
      src.endian = d1 & 3;
      const auto st = vsh.strides.find(el.stream);
      src.stride = st != vsh.strides.end() ? st->second : 0;
      // A vfetch with no stride (Perfect Dark's quad shader, whose microcode D3D
      // leaves as the template here): D3D stream s is fetch slot 95 - s, so the
      // stride comes from SetStreamSource.
      if (!src.stride && el.stream >= 80 && el.stream <= 95 && g_streams[95 - el.stream].vb)
        src.stride = g_streams[95 - el.stream].stride;
      src.addr = cpu_view_of_physical(phys, (uint32_t(lo) + vcount) * src.stride);
    } else if (el.stream < 16 && g_streams[el.stream].vb) {
      const uint32_t vb = g_streams[el.stream].vb;
      // (0xE0000000+ buffers: the data is 4 KB on, as for index buffers.)
      src.addr = gpu_view(g32(vb + 24) & ~3u) + g_streams[el.stream].offset;
      src.stride = g_streams[el.stream].stride;
      src.endian = g32(vb + 28) & 3;
    }
    if (!src.addr || !src.stride) {
      NTRACE("skip prim {} n {}: no source for stream {} ({}; vs {:016X} decl {:08X}; fetch {:08X} {:08X}; vb {:08X} "
             "stride {})", prim, count, el.stream, up_data ? "UP" : from_shader ? "fetch" : "decl", vsh.hash, decl,
             g32(dev + 1152 + el.stream * 8), g32(dev + 1152 + el.stream * 8 + 4),
             el.stream < 16 ? g_streams[el.stream].vb : 0, src.stride);
      static int logged = 0;
      if (logged++ < 12)
        REXKRNL_WARN("ORNATIVE no source: prim {} n {} stream {} ({}) vs {:016X} decl {:08X} fetch {:08X} {:08X} vb {:08X} "
                     "stride {} offset {}", prim, count, el.stream, up_data ? "UP" : from_shader ? "fetch" : "decl",
                     vsh.hash, decl, g32(dev + 1152 + el.stream * 8), g32(dev + 1152 + el.stream * 8 + 4),
                     el.stream < 16 ? g_streams[el.stream].vb : 0, src.stride, el.offset);
      ++g.skipped; ++g.skip_reason[4]; return;
    }
    sources.push_back(src);
  }
  std::vector<HostFmt> hfmts;
  std::vector<uint32_t> hoffsets;
  uint32_t host_stride = 0;
  for (const Element& el : els) {
    hfmts.push_back(host_format(el));
    hoffsets.push_back(host_stride);
    host_stride += hfmts.back().size;
  }
  // Textures first (their uploads may flush the ring), then room for this
  // draw's vertices, indices and constants in one go.
  for (uint32_t s = 0; s < 16; ++s) texture_slot(g_textures[s], true);
  // gpu_native_log_hud: each texture cell the HUD sprite shader draws, once
  // (content hash, texture coordinates, screen box): the table's source.
  if (REXCVAR_GET(gpu_native_log_hud) && ps && g.shaders[ps_obj].hash == 0x4FD4F06E1F054D05ull && g_textures[0] &&
      !sources.empty() && sources[0].stride == 28 && vcount == 4) {
    uint32_t f[6];
    for (int i = 0; i < 6; ++i) f[i] = g32(g_textures[0] + 28 + i * 4);
    const auto it = g.textures_by_fetch.find(fnv1a(reinterpret_cast<const uint8_t*>(f), sizeof(f)));
    if (it != g.textures_by_fetch.end()) {
      // Each texture once, as decoded, in native_textures as <hash's high 32 bits>_f<fmt>_<w>x<h>.bmp.
      static std::unordered_set<uint64_t> dumped;
      if (dumped.insert(it->second.content).second) {
        std::vector<uint8_t> rgba;
        uint32_t dw = 0, dh = 0;
        if (hud::decode_base(f, it->second.addr, rgba, dw, dh))
          dump_texture(uint32_t(it->second.content >> 32), f[1] & 0x3F, dw, dh, rgba.data(), dw * 4);
      }
      float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, u0 = 1e9f, u1 = -1e9f, v0 = 1e9f, v1 = -1e9f;
      for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t a = sources[0].addr + uint32_t(lo + i) * 28;
        float x, y, u, v;
        const uint32_t bx = g32(a), by = g32(a + 4), bu = g32(a + 20), bv = g32(a + 24);
        std::memcpy(&x, &bx, 4); std::memcpy(&y, &by, 4); std::memcpy(&u, &bu, 4); std::memcpy(&v, &bv, 4);
        x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
        u0 = std::min(u0, u); u1 = std::max(u1, u); v0 = std::min(v0, v); v1 = std::max(v1, v);
      }
      const uint32_t w = (f[2] & 0x1FFF) + 1, h = ((f[2] >> 13) & 0x1FFF) + 1;
      static std::unordered_set<uint64_t> seen;
      const int cellkey[4] = {int(u0 * w), int(u1 * w), int(v0 * h), int(v1 * h)};
      const uint64_t cell = it->second.content ^ XXH3_64bits(cellkey, sizeof(cellkey));
      if (seen.insert(cell).second)
        REXKRNL_INFO("ORNATIVE HUD cell {:016X} mem {:08X} fmt {} {}x{} texels {:.1f}-{:.1f} x {:.1f}-{:.1f} at {:.1f}-{:.1f} x "
                     "{:.1f}-{:.1f} frame {}",
                     it->second.content, f[1] & 0xFFFFF000u, f[1] & 0x3F, w, h, u0 * w, u1 * w, v0 * h, v1 * h, x0, x1, y0,
                     y1, g.frames + 1);
    }
  }
  for (uint32_t s = 0; s < 16; ++s)
    if (texture_pending(g_textures[s])) { ++g.skipped; ++g.skip_reason[6]; return; }
  ensure_room(uint64_t(host_stride) * vcount + uint64_t(icount) * 4 + 16384);
  // Static geometry is decoded once and kept in a GPU buffer, found again by
  // the draw's layout and source range and checked against an xxHash of the
  // guest bytes (decoding every draw every frame cost ~47 ms a frame in Dam).
  // Data that keeps changing, and UP draws, go through the upload ring.
  const uint64_t vbytes = uint64_t(host_stride) * (vcount + rects);
  uint64_t layout = XXH3_64bits(sources.data(), sources.size() * sizeof(Source));
  for (const Element& el : els)  // field by field: Element has a padding byte
    layout = layout * 0x100000001B3ull ^ (uint64_t(el.stream) | uint64_t(el.offset) << 16 | uint64_t(el.type) << 32) ^
             (uint64_t(el.usage) << 8 | el.index | uint64_t(uint8_t(el.exp)) << 16 | uint64_t(el.swizzle) << 24) *
                 0xC2B2AE3D27D4EB4Full;
  layout ^= (uint64_t(lo) << 32 | vcount) * 0x9E3779B97F4A7C15ull;
  Native::VertexCacheEntry* entry = up_data || rects ? nullptr : &g.vertex_cache[layout];
  uint64_t content = 0;
  const double t_h = now_ms();
  const bool hashed = !entry || hash_sources_seh(sources.data(), sources.size(), uint32_t(lo), vcount, &content);
  g.hash_ms += now_ms() - t_h;
  if (!hashed) {
    ++g.skipped; ++g.skip_reason[4]; return;
  }
  RenderBufferReference vref;
  const bool hit = !tracing() && entry && entry->buffer && entry->content == content;
  if (entry) entry->last_frame = g.frames;
  if (hit) ++g.vcache_hits; else ++g.vcache_misses;
  const uint64_t voff = hit ? 0 : upload_alloc(vbytes, 16);
  if (voff == UINT64_MAX) return;
  const double t_vd = now_ms();
  const bool decoded = hit || decode_vertices_seh(els.data(), els.size(), sources.data(), keys.data(), keys.size(),
                                                  uint32_t(lo), vcount, g.upload_ptr + voff, host_stride,
                                                  hfmts.data(), hoffsets.data());
  g.vdecode_ms += now_ms() - t_vd;
  if (!decoded) {
    static int logged = 0;
    if (logged++ < 8)
      REXKRNL_WARN("ORNATIVE vertex read faulted: {} sources, first {:08X} stride {} endian {}, vertices {}..{} ({})",
                   sources.size(), sources[0].addr, sources[0].stride, sources[0].endian, lo, lo + vcount - 1,
                   up_data ? "UP" : from_shader ? "fetch" : "decl");
    ++g.skipped; ++g.skip_reason[4];
    return;
  }
  size_t rect_pos = els.size();  // the position element (for the right-angle corner)
  for (size_t e = 0; e < els.size(); ++e)
    if (els[e].usage == 0 && hfmts[e].kind != HostKind::kRaw && hfmts[e].size >= 8 && rect_pos == els.size()) rect_pos = e;
  for (uint32_t r = 0; r < rects; ++r) {
    uint8_t* base = g.upload_ptr + voff;
    uint32_t v[3] = {rect_src[r * 3], rect_src[r * 3 + 1], rect_src[r * 3 + 2]};
    // The corner opposite the longest edge (the diagonal) is the right angle,
    // as xenia finds it; the fourth corner mirrors it across the diagonal.
    if (rect_pos < els.size()) {
      auto xy = [&](uint32_t i, float* f) {
        std::memcpy(f, base + uint64_t(int64_t(i) + base_vertex - lo) * host_stride + hoffsets[rect_pos], 8);
      };
      float p0[2], p1[2], p2[2];
      xy(v[0], p0); xy(v[1], p1); xy(v[2], p2);
      auto d2 = [](const float* x, const float* y) { return (x[0] - y[0]) * (x[0] - y[0]) + (x[1] - y[1]) * (x[1] - y[1]); };
      const float e01 = d2(p0, p1), e02 = d2(p0, p2), e12 = d2(p1, p2);
      if (e02 > e12 && e02 > e01) std::swap(v[0], v[1]);       // corner at v1
      else if (e01 > e12 && e01 > e02) std::swap(v[0], v[2]);  // corner at v2
      idx[r * 6 + 3] = v[2];
      idx[r * 6 + 4] = v[1];
    }
    const uint8_t* a = base + uint64_t(int64_t(v[0]) + base_vertex - lo) * host_stride;
    const uint8_t* b = base + uint64_t(int64_t(v[1]) + base_vertex - lo) * host_stride;
    const uint8_t* c = base + uint64_t(int64_t(v[2]) + base_vertex - lo) * host_stride;
    uint8_t* d = base + uint64_t(vcount + r) * host_stride;
    for (size_t e = 0; e < els.size(); ++e) {
      const uint32_t o = hoffsets[e], n = hfmts[e].size;
      const HostKind k = hfmts[e].kind;
      if ((k == HostKind::kF32 || k == HostKind::kRecord) && !uint_usage(els[e].usage)) {
        for (uint32_t i = 0; i + 4 <= n; i += 4) {
          float fa, fb, fc;
          std::memcpy(&fa, a + o + i, 4); std::memcpy(&fb, b + o + i, 4); std::memcpy(&fc, c + o + i, 4);
          const float fd = fb + fc - fa;
          std::memcpy(d + o + i, &fd, 4);
        }
      } else if (k == HostKind::kU8x4) {
        for (uint32_t i = 0; i < 4; ++i) d[o + i] = uint8_t(std::clamp(int(b[o + i]) + int(c[o + i]) - int(a[o + i]), 0, 255));
      } else {
        std::memcpy(d + o, c + o, n);  // (integers and half floats: the third corner's)
      }
    }
  }
  if (hit) {
    vref = RenderBufferReference(entry->buffer.buffer, entry->buffer.offset);
  } else if (entry && (!entry->buffer || entry->misses < 4)) {
    if (entry->buffer) ++entry->misses;
    if (!entry->buffer || entry->capacity < vbytes) {
      retire(g.vertex_pool, entry->buffer);
      entry->buffer = pool_alloc(g.vertex_pool, vbytes);
      entry->capacity = class_bytes(entry->buffer.size_class);
    }
    g.cmd->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(entry->buffer.buffer, RenderBufferAccess::WRITE));
    g.cmd->copyBufferRegion(RenderBufferReference(entry->buffer.buffer, entry->buffer.offset),
                            RenderBufferReference(g.upload.get(), voff), vbytes);
    g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderBufferBarrier(entry->buffer.buffer, RenderBufferAccess::READ));
    entry->content = content;
    vref = RenderBufferReference(entry->buffer.buffer, entry->buffer.offset);
  } else {
    vref = RenderBufferReference(g.upload.get(), voff);
  }
  const double t_decoded = now_ms();
  g.sect[2] += t_decoded - t_indexed;
  std::vector<RenderInputSlot> slots{RenderInputSlot(0, host_stride)};
  std::vector<RenderVertexBufferView> views{RenderVertexBufferView(vref, uint32_t(vbytes))};
  RenderBufferReference iref;
  if (ihit) {
    iref = RenderBufferReference(ientry->buffer.buffer, ientry->buffer.offset);
  } else {
    auto rebase = [&](uint32_t i) {
      return rects && i >= kRectCorner ? int64_t(vcount + (i - kRectCorner)) : int64_t(i) + base_vertex - lo;
    };
    const uint64_t ibytes = uint64_t(icount) * (vcount + rects < 0xFFFF ? 2 : 4);
    const uint64_t ioff = upload_alloc(ibytes, 16);
    if (ioff == UINT64_MAX) return;
    if (vcount + rects < 0xFFFF) {  // rebased indices fit 16 bits: half the size and bandwidth
      uint16_t* id = reinterpret_cast<uint16_t*>(g.upload_ptr + ioff);
      for (size_t i = 0; i < idx.size(); ++i) id[i] = uint16_t(rebase(idx[i]));
    } else {
      uint32_t* id = reinterpret_cast<uint32_t*>(g.upload_ptr + ioff);
      for (size_t i = 0; i < idx.size(); ++i) id[i] = uint32_t(rebase(idx[i]));
    }
    if (ientry && (!ientry->buffer || ientry->misses < 4)) {
      if (ientry->buffer) ++ientry->misses;
      if (!ientry->buffer || ientry->capacity < ibytes) {
        retire(g.index_pool, ientry->buffer);
        ientry->buffer = pool_alloc(g.index_pool, ibytes);
        ientry->capacity = class_bytes(ientry->buffer.size_class);
      }
      g.cmd->barriers(RenderBarrierStage::COPY, RenderBufferBarrier(ientry->buffer.buffer, RenderBufferAccess::WRITE));
      g.cmd->copyBufferRegion(RenderBufferReference(ientry->buffer.buffer, ientry->buffer.offset),
                              RenderBufferReference(g.upload.get(), ioff), ibytes);
      g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderBufferBarrier(ientry->buffer.buffer, RenderBufferAccess::READ));
      ientry->content = icontent; ientry->lo = lo; ientry->vcount = vcount; ientry->count = icount; ientry->topo = topo;
      iref = RenderBufferReference(ientry->buffer.buffer, ientry->buffer.offset);
    } else {
      iref = RenderBufferReference(g.upload.get(), ioff);
    }
  }

  g_spec_bits = 2;
  for (const Element& el : els) {
    uint32_t cb = 4;
    (void)cb;
    if ((el.usage == 3 || el.usage == 6 || el.usage == 7) && ((el.type & 0x3F) == 16 || (el.type & 0x3F) == 17))
      g_spec_bits |= 1;
  }
  g_draw_state = read_state(dev);
  if (REXCVAR_GET(gpu_native_query_test) >= 2 && g_occ.open >= 0) {  // test: show query draws
    g_draw_state.write_mask = 0xF;
    g_draw_state.blend = false;
    const int32_t t = REXCVAR_GET(gpu_native_query_test);
    if (t == 3 || t == 4) g_draw_state.z_enable = false;
    if (t == 3 || t == 5) g_draw_state.cull = 0;
  }
  bind_target();
  if (g.bound_scene) {
    apply_pending_clears();
    apply_pending_restore();
  } else if (g.guest_rt) {
    g.edram_color[g32(g.guest_rt + 28) & 0xFFF] = g.guest_rt;  // its EDRAM now holds this colour
  }
  g.draws_since_resolve = true;
  if (g.bound_scene && g_draw_state.z_enable && g_draw_state.z_write) {
    depth_will_change();
    g.depth_written = true;
    ++g.depth_gen;
  }
  RenderPipeline* pipe = pipeline_for(g.shaders[vs_obj].hash, vs, ps ? g.shaders[ps_obj].hash : 0, ps, els, topo);
  if (!pipe) { ++g.skipped; ++g.skip_reason[5]; return; }
  g_vs_cb_bytes = g.shaders[vs_obj].cb_bytes;
  g_ps_cb_bytes = ps ? g.shaders[ps_obj].cb_bytes : 16;
  set_common_state(dev);
  if (tracing()) {
    const DrawState& st = g_draw_state;
    std::string tex;
    for (uint32_t t = 0; t < 8; ++t)
      if (g_textures[t]) tex += fmt::format(" s{}={:08X}->{} sign {:02X} fmt {} dim {} obj {:08X} {:08X} {:08X}{}", t, g_textures[t], texture_slot(g_textures[t]), (g32(dev + 1152 + t * 24) >> 2) & 0xFF, g32(dev + 1152 + t * 24 + 4) & 0x3F,
                                            (g32(dev + 1152 + t * 24 + 20) >> 9) & 3, g32(g_textures[t] + 28), g32(g_textures[t] + 32), g32(g_textures[t] + 48),
                                            // the device's own fetch constant, when it names other memory than the object SetTexture was given
                                            (g32(dev + 1152 + t * 24 + 4) & 0xFFFFF000u) != (g32(g_textures[t] + 32) & 0xFFFFF000u)
                                                ? fmt::format(" DEVICE FETCH {:08X}", g32(dev + 1152 + t * 24 + 4)) : std::string());
    // Fetch constants the game set without SetTexture (a sampler this draw's shader may read).
    for (uint32_t t = 0; t < 16; ++t)
      if (!g_textures[t] && (g32(dev + 1152 + t * 24) & 3) == 2 && (g32(dev + 1152 + t * 24 + 4) & 0xFFFFF000u))
        tex += fmt::format(" s{}=(no SetTexture) fetch {:08X} {:08X}", t, g32(dev + 1152 + t * 24), g32(dev + 1152 + t * 24 + 4));
    NTRACE("draw#{} prim {} n {} verts {} vs {:016X} ps {:016X} z {}{} f{} cull {} blend {} {}/{}/{} mask {:X} at {} {}{}",
           g.draws, prim, count, vcount, g.shaders[vs_obj].hash, (ps ? g.shaders[ps_obj].hash : 0),
           st.z_enable, st.z_write ? "w" : "", st.z_func, st.cull, st.blend, st.src, st.dst, st.op, st.write_mask,
           st.alpha_test, st.alpha_func, tex);
    float vp[6];
    for (int i = 0; i < 6; ++i) { const uint32_t u = g32(dev + kDevViewport + i * 4); std::memcpy(&vp[i], &u, 4); }
    const float* p0 = reinterpret_cast<const float*>(g.upload_ptr + voff);
    NTRACE("  vte {:X} viewport {} {} {} {} {} {} pos0 {} {} {} {} (el0 usage {} fmt {})", g32(dev + 10572), vp[0], vp[1],
           vp[2], vp[3], vp[4], vp[5], p0[0], p0[1], p0[2], p0[3], els[0].usage, els[0].type & 0x3F);
    float vreg[6];  // PA_CL_VPORT_XSCALE, XOFFSET, YSCALE, YOFFSET, ZSCALE, ZOFFSET (D3D_SetViewport_Worker)
    for (int i = 0; i < 6; ++i) { const uint32_t u = g32(dev + 10504 + i * 4); std::memcpy(&vreg[i], &u, 4); }
    NTRACE("  depthcontrol {:08X} stencil {} ref {:02X} mask {:02X} write {:02X} blend factor {} {} {} {} mode {:08X} "
           "scissor {:08X} {:08X} vport {} {} {} {} {} {}",
           g32(dev + 10548), st.stencil, st.stencil_ref, st.stencil_mask, st.stencil_write, st.blend_factor[0],
           st.blend_factor[1], st.blend_factor[2], st.blend_factor[3], g32(dev + 10568), g32(dev + kDevWindowScissorTL),
           g32(dev + kDevWindowScissorBR), vreg[0], vreg[1], vreg[2], vreg[3], vreg[4], vreg[5]);
    std::string e;
    for (size_t i = 0; i < els.size() * std::min<size_t>(vcount, 4); ++i) {
      const float* v = p0 + i * 4;
      if (i % els.size() == 0 && i) e += " |";
      const Element& el = els[i % els.size()];
      e += fmt::format(" [u{}.{} t{:X} exp{} off{} = {:.3f} {:.3f} {:.3f} {:.3f}]", el.usage, el.index,
                       el.type, int(el.exp), el.offset, v[0], v[1], v[2], v[3]);
    }
    NTRACE("  elements{} {}", e, from_shader ? "fetch" : "decl");
    std::string raw;
    for (uint32_t i = 0; !sources.empty() && i < 8; ++i)
      raw += fmt::format(" {:08X}", g32(sources[0].addr + uint32_t(lo) * sources[0].stride + i * 4));
    NTRACE("  source0 {:08X} stride {} endian {} {} first vertex:{}", sources[0].addr, sources[0].stride,
           sources[0].endian, up_data ? "UP" : "", raw);
    for (size_t k = 1; k < sources.size(); ++k)
      NTRACE("  source{} (slot {}) {:08X} stride {} endian {} lo {} n {}", k, keys[k], sources[k].addr, sources[k].stride,
             sources[k].endian, lo, vcount);
    {
      std::string offs;
      for (const Element& el : els) offs += fmt::format(" u{}.{}@s{}+{}:{:X}", el.usage, el.index, el.stream, el.offset, el.type);
      NTRACE("  layout{}", offs);
      std::string d;
      for (const Element& el : read_decl(g32(dev + kDevVertexDecl)))
        d += fmt::format(" u{}.{}@s{}+{}:{:X}", el.usage, el.index, el.stream, el.offset, el.type);
      NTRACE("  decl{}", d);
    }
    // Stretched triangles (one edge far longer than the draw's usual edge):
    // a vertex out of place in the data, e.g. a spike across the sky.
    size_t pe = els.size();
    for (size_t e = 0; e < els.size(); ++e)
      if (els[e].usage == 0 && hfmts[e].size >= 12 && pe == els.size()) pe = e;
    // A draw inside an occlusion query (or any draw of OutRun's world shader
    // FC6B81D3): where its first vertices land, as position x c64-c67 (its
    // world-view-projection rows), and its triangles' screen winding.
    if ((g_occ.open >= 0 || g.shaders[vs_obj].hash == 0xFC6B81D38A23FD54ull) && pe < els.size()) {
      float m[16];
      for (int i = 0; i < 16; ++i) { const uint32_t u = g32(dev + kDevVsConsts + 64 * 16 + i * 4); std::memcpy(&m[i], &u, 4); }
      std::string clip;
      for (uint32_t v = 0; v < std::min<uint32_t>(vcount, 6); ++v) {
        float p[3];
        std::memcpy(p, g.upload_ptr + voff + uint64_t(v) * host_stride + hoffsets[pe], 12);
        float c[4];
        for (int k = 0; k < 4; ++k) c[k] = p[0] * m[k] + p[1] * m[4 + k] + p[2] * m[8 + k] + m[12 + k];
        clip += fmt::format(" ({:.2f} {:.2f} {:.2f} | clip {:.3f} {:.3f} {:.3f} {:.3f} | ndc {:.3f} {:.3f} {:.5f})", p[0],
                            p[1], p[2], c[0], c[1], c[2], c[3], c[0] / c[3], c[1] / c[3], c[2] / c[3]);
      }
      NTRACE("  query draw vertices{}", clip);
      // Screen-space winding of each converted triangle (y down): cw / ccw / degenerate.
      uint32_t cw = 0, ccw = 0, flat = 0;
      std::string first;
      for (size_t t = 0; t + 2 < idx.size() && !rects; t += 3) {
        float s[3][2];
        for (int k = 0; k < 3; ++k) {
          float p[3];
          std::memcpy(p, g.upload_ptr + voff + uint64_t(int64_t(idx[t + k]) + base_vertex - lo) * host_stride + hoffsets[pe], 12);
          float c[4];
          for (int j = 0; j < 4; ++j) c[j] = p[0] * m[j] + p[1] * m[4 + j] + p[2] * m[8 + j] + m[12 + j];
          s[k][0] = c[0] / c[3];
          s[k][1] = -c[1] / c[3];
        }
        const float cross = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[1][1] - s[0][1]) * (s[2][0] - s[0][0]);
        if (cross > 1e-9f) ++cw; else if (cross < -1e-9f) ++ccw; else ++flat;
        if (t < 18) first += fmt::format(" {}/{}/{}", idx[t], idx[t + 1], idx[t + 2]);
      }
      std::string raw;
      for (uint32_t i = 0; i < std::min<uint32_t>(count, 24) && idx_addr; ++i)
        raw += fmt::format(" {}", idx_wide ? g32(idx_addr + i * 4) : g16(idx_addr + i * 2));
      const uint32_t ibo = g32(dev + 12444);
      NTRACE("  query draw triangles: {} cw, {} ccw, {} flat; first{}; guest indices{} (ib {:08X}: {:08X} {:08X} {:08X} "
             "{:08X}, data at {:08X}, base vertex {})", cw, ccw, flat, first, raw, ibo, ibo ? g32(ibo) : 0,
             ibo ? g32(ibo + 24) : 0, ibo ? g32(ibo + 28) : 0, ibo ? g32(ibo + 32) : 0, idx_addr, base_vertex);
    }
    // Triangles hidden by gpu_native_hide, with their vertices' guest addresses.
    if (pe < els.size() && !sources.empty())
      for (size_t t = 0; t + 2 < hidden_tris.size(); t += 3) {
        uint32_t src_addr = 0, src_stride = 0;
        for (size_t k = 0; k < keys.size(); ++k)
          if (keys[k] == els[pe].stream) { src_addr = sources[k].addr; src_stride = sources[k].stride; }
        std::string vs;
        for (int k = 0; k < 3; ++k) {
          const uint32_t gv = uint32_t(int64_t(hidden_tris[t + k]) + base_vertex);
          const uint32_t ga = src_addr ? src_addr + gv * src_stride + els[pe].offset : 0;
          vs += fmt::format(" v{} at {:08X}: {:08X} {:08X} {:08X}", gv, ga, ga ? g32(ga) : 0, ga ? g32(ga + 4) : 0,
                            ga ? g32(ga + 8) : 0);
        }
        NTRACE("  HIDDEN tri of draw#{}:{}", g.draws, vs);
      }
    if (pe < els.size() && topo == RenderPrimitiveTopology::TRIANGLE_LIST && idx.size() >= 3 && !rects) {
      auto pos = [&](uint32_t i) {
        const float* f = reinterpret_cast<const float*>(g.upload_ptr + voff +
                                                        uint64_t(int64_t(i) + base_vertex - lo) * host_stride +
                                                        hoffsets[pe]);
        return std::array<float, 3>{f[0], f[1], f[2]};
      };
      auto dist = [](const std::array<float, 3>& a, const std::array<float, 3>& b) {
        return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
      };
      std::vector<float> edges;
      for (size_t t = 0; t + 2 < idx.size(); t += 3)
        for (int k = 0; k < 3; ++k) edges.push_back(dist(pos(idx[t + k]), pos(idx[t + (k + 1) % 3])));
      std::vector<float> sorted = edges;
      std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
      const float median = std::max(sorted[sorted.size() / 2], 1e-3f);
      int reported = 0;
      uint32_t src_addr = 0, src_stride = 0;
      for (size_t k = 0; k < keys.size(); ++k)
        if (keys[k] == els[pe].stream) { src_addr = sources[k].addr; src_stride = sources[k].stride; }
      for (size_t t = 0; t + 2 < idx.size() && reported < 8; t += 3) {
        const float m = std::max({edges[t], edges[t + 1], edges[t + 2]});
        if (m < median * 8.0f) continue;
        ++reported;
        std::string vs;
        for (int k = 0; k < 3; ++k) {
          const auto q = pos(idx[t + k]);
          const uint32_t gv = uint32_t(int64_t(idx[t + k]) + base_vertex);
          const uint32_t ga = src_addr ? src_addr + gv * src_stride + els[pe].offset : 0;
          vs += fmt::format(" v{} ({:.1f} {:.1f} {:.1f}) at {:08X}: {:08X} {:08X} {:08X}", gv, q[0], q[1], q[2], ga,
                            ga ? g32(ga) : 0, ga ? g32(ga + 4) : 0, ga ? g32(ga + 8) : 0);
        }
        NTRACE("  STRETCHED tri {} of draw#{}: longest edge {:.1f} vs median {:.1f}, fmt {:X}:{}", t / 3, g.draws, m,
               median, els[pe].type, vs);
      }
    }
  }
  g.cmd->setPipeline(pipe);
  {  // dynamic state Plume keeps in the pipeline or not at all: stencil reference, blend factor
    float factor[4] = {};
    const bool has_factor = blend_factor(g_draw_state, factor);
    if (g_vulkan) {  // (dynamic in every Vulkan pipeline: patches/plume.patch; set before each draw)
      const VkCommandBuffer cb = static_cast<VulkanCommandList*>(g.cmd)->vk;
      vkCmdSetStencilReference(cb, VK_STENCIL_FACE_FRONT_AND_BACK,
                               g.bound_scene && g_draw_state.stencil ? g_draw_state.stencil_ref : 0);
      vkCmdSetBlendConstants(cb, factor);
    } else {
      ID3D12GraphicsCommandList* l = static_cast<D3D12CommandList*>(g.cmd)->d3d;
      if (g.bound_scene && g_draw_state.stencil) l->OMSetStencilRef(g_draw_state.stencil_ref);
      if (has_factor) l->OMSetBlendFactor(factor);
    }
  }
  g.cmd->setVertexBuffers(0, views.data(), uint32_t(views.size()), slots.data());
  const bool narrow = vcount + rects < 0xFFFF;
  RenderIndexBufferView ib(iref, icount * (narrow ? 2 : 4), narrow ? RenderFormat::R16_UINT : RenderFormat::R32_UINT);
  g.cmd->setIndexBuffer(&ib);
  begin_vulkan_query();
  g.cmd->drawIndexedInstanced(icount, 1, 0, 0, 0);
  if (g.prof_active)
    prof_mark(fmt::format("draw#{} prim {} idx {} verts {} vs {:016X} ps {:016X} blend {}", g.draws, prim, icount, vcount,
                          g.shaders[vs_obj].hash, (ps ? g.shaders[ps_obj].hash : 0), g_draw_state.blend));
  ++g.draws;
}

// Scene -> a single-sampled texture of the same size and format: a copy, or
// a resolve (sample average) when the scene is multisampled.
void copy_scene_to(RenderTexture* dst) {
  if (g.scene_msaa.sampleCount == RenderSampleCount::COUNT_1) {
    g.cmd->barriers(RenderBarrierStage::COPY, {RenderTextureBarrier(g.scene_color, RenderTextureLayout::COPY_SOURCE),
                                               RenderTextureBarrier(dst, RenderTextureLayout::COPY_DEST)});
    g.cmd->copyTexture(dst, g.scene_color);
  } else {
    g.cmd->barriers(RenderBarrierStage::COPY, {RenderTextureBarrier(g.scene_color, RenderTextureLayout::RESOLVE_SOURCE),
                                               RenderTextureBarrier(dst, RenderTextureLayout::RESOLVE_DEST)});
    g.cmd->resolveTexture(dst, g.scene_color);
  }
}

// Paced presents (gpu_native_pace): like the SDK presenter's pacing, each frame
// is presented at a fixed moment instead of whenever the game thread gets to
// Swap. Without a GPU plugin there is no guest vblank tick to anchor to, so the
// moments run on the game's own frame period (smoothed from Swap arrivals) and
// sit gpu_native_pace_margin_us after the typical arrival; a slow phase lock
// keeps them there. Frames arriving after their moment present at once (and
// the schedule re-anchors after a real hitch).
void precise_wait_until(double target_ms) {
  static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
  const double ahead = 0.7;
  const double now = now_ms();
  if (timer && target_ms - now > ahead) {
    LARGE_INTEGER due;
    due.QuadPart = -int64_t((target_ms - now - ahead) * 10000.0);
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(timer, INFINITE);
  }
  while (now_ms() < target_ms) std::this_thread::yield();
}

void pace_present() {
  const double now = now_ms();
  if (g.pace_last_arrival > 0) {
    const double iv = now - g.pace_last_arrival;
    if (iv > 12.0 && iv < 22.0) g.pace_period += (iv - g.pace_period) * 0.01;
  }
  g.pace_last_arrival = now;
  if (!REXCVAR_GET(gpu_native_pace)) { g.pace_waited_ms = 0; return; }
  const double margin = REXCVAR_GET(gpu_native_pace_margin_us) / 1000.0;
  double target = g.pace_next;
  if (target <= 0 || now > target + g.pace_period * 0.5 || target - now > g.pace_period * 1.5) {
    target = now + margin;  // (re)anchor
  } else {
    target += (now + margin - target) * 0.02;  // keep the moment `margin` after the usual arrival
  }
  g.pace_waited_ms = std::max(0.0, target - now);
  if (target > now) precise_wait_until(target);
  g.pace_next = std::max(target, now) + g.pace_period;
}

// Present-to-present intervals and the game's sim steps per frame, for the
// frame stats line (double steps as in the 0.9.6c "ge frames" line).
void note_present() {
  const double now = now_ms();
  if (g.pace_last_present > 0) g.present_intervals.push_back(now - g.pace_last_present);
  g.pace_last_present = now;
  g.pace_wait_sum += g.pace_waited_ms;
  // (GoldenEye also counted its double sim steps here, from its own frame
  // counter; Perfect Dark's are in the pd_frame_stats line.)
}

// Test (gpu_native_test_timer): the race countdown, found in the game's data
// (0x82000000-0x84000000) as a value falling steadily over two two-second
// windows with 10 to 99 seconds left; it is a 32-bit count of frames (at
// 82851A70 in a race, 60 a second). Then kept above half a minute.
struct TimerCand { uint32_t addr; uint8_t kind; float v, d; };  // kind 0 u16, 1 u32, 2 f32

float timer_read(uint32_t a, uint8_t kind) {
  return kind == 0 ? float(g16(a)) : kind == 1 ? float(g32(a)) : std::bit_cast<float>(g32(a));
}

size_t timer_scan_seh(uint32_t base, uint32_t bytes, std::vector<TimerCand>& out) {
  const size_t before = out.size();
  __try {
    for (uint32_t a = base; a < base + bytes; a += 2) {
      const float h = float(g16(a));
      if (h >= 1 && h <= 65535) out.push_back({a, 0, h, 0});
      if (a & 2) continue;
      const uint32_t w = g32(a);
      if (w >= 1 && w <= 1000000) out.push_back({a, 1, float(w), 0});
      const float f = std::bit_cast<float>(w);
      if (f >= 1.0f && f <= 100000.0f) out.push_back({a, 2, f, 0});
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
  return out.size() - before;
}

uint64_t g_xenos_test_frame = 0;  // Swaps seen (either renderer; tests)

// Called at every Swap, with either renderer (the frame count is Swaps).
void test_timer() {
  if (static bool sped = false; !sped) {
    sped = true;
    const double speed = std::clamp(REXCVAR_GET(gpu_native_test_speed), 1.0, 4.0);
    if (speed != 1.0) {
      rex::chrono::Clock::set_guest_time_scalar(speed);
      REXKRNL_INFO("ORNATIVE test: game clock {}x", speed);
    }
  }
  static std::vector<TimerCand> cands;
  static int phase = 0;
  static uint64_t next = 0;
  static double t_prev = 0;  // game seconds at the last look (the windows are timed in game time)
  const double t_now = double(rex::chrono::Clock::QueryGuestTickCount()) / double(rex::chrono::Clock::guest_tick_frequency());
  const int32_t start = REXCVAR_GET(gpu_native_test_timer);
  if (start <= 0 || g_xenos_test_frame < uint64_t(start) || g_xenos_test_frame < next) return;
  if (phase == 0) {
    for (uint32_t c = 0x82000000u; c < 0x84000000u; c += 0x10000)
      if (readable_seh(c, c + 0xFFFF)) timer_scan_seh(c, 0x10000, cands);
    REXKRNL_INFO("ORNATIVE test timer: {} values", cands.size());
  } else if (phase <= 2) {
    const float span = float(std::max(0.25, t_now - t_prev)) / 2;  // (in two-second units)
    std::erase_if(cands, [span](TimerCand& c) {
      if (!readable_seh(c.addr, c.addr + 3)) return true;
      const float v = timer_read(c.addr, c.kind), d = (v - c.v) / span;
      bool keep = false;
      // (two-second windows: a whole-second counter steps by 1 or 2 in one)
      if (phase == 1) {
        for (const float rate : {1.0f, 60.0f, 100.0f, 1000.0f})
          if (std::abs(d + 2 * rate) <= rate * 0.6f && v / rate >= 10 && v / rate <= 99) keep = true;
        c.d = d / 2;
      } else {
        keep = std::abs(d / 2 - c.d) <= std::abs(c.d) * 0.3f;
      }
      c.v = v;
      return !keep;
    });
    REXKRNL_INFO("ORNATIVE test timer: {} values after {} s", cands.size(), phase);
    if (phase == 2)  // the countdown: an integer count of 1/60 s (82851A70 or 8278F474 in a race; the address varies)
      std::erase_if(cands, [](const TimerCand& c) { return c.kind == 2 || c.d > -55 || c.d < -65; });
    if (phase >= 1)
      for (size_t i = 0; i < cands.size() && i < 40; ++i)
        REXKRNL_INFO("ORNATIVE test timer: {:08X} {} value {} per second {}", cands[i].addr,
                     cands[i].kind == 0 ? "u16" : cands[i].kind == 1 ? "u32" : "f32", cands[i].v, cands[i].d);
  } else if (cands.empty()) {  // not in a race yet: look again in a few seconds
    phase = 0;
    next = g_xenos_test_frame + 300;
    return;
  } else if (cands.size() <= 8) {  // hold (a few at most: a wrong guess must not write widely)
    static double stage_t0 = 0;
    static float last = -1;
    const float v0 = timer_read(cands[0].addr, cands[0].kind);
    if (g_test_checkpoints.load() < 0) {
      // The first stage starts with 100 s on the clock (HUD: total time + time left).
      g_test_checkpoints = 0;
      stage_t0 = t_now - std::max(0.0, 100.0 - double(v0 / -cands[0].d));
      REXKRNL_INFO("ORNATIVE test timer: holding {:08X} from {} s left", cands[0].addr, v0 / -cands[0].d);
    } else if (last >= 0 && v0 > last && t_now - stage_t0 > 20) {  // Extend Time (the hold's own writes set `last`)
      REXKRNL_INFO("ORNATIVE test timer: checkpoint {} after {:.1f} game seconds", g_test_checkpoints.load() + 1,
                   t_now - stage_t0);
      ++g_test_checkpoints;
      stage_t0 = t_now;
    }
    g_test_stage_seconds = t_now - stage_t0;
    for (const TimerCand& c : cands) {
      const float rate = -c.d, v = timer_read(c.addr, c.kind);
      if (v >= 30 * rate) continue;
      const float set = 60 * rate;
      if (c.kind == 0) { const uint16_t be = __builtin_bswap16(uint16_t(set)); std::memcpy(gptr(c.addr), &be, 2); }
      else {
        const uint32_t be = __builtin_bswap32(c.kind == 1 ? uint32_t(set) : std::bit_cast<uint32_t>(set));
        std::memcpy(gptr(c.addr), &be, 4);
      }
    }
    last = timer_read(cands[0].addr, cands[0].kind);
    return;
  } else {
    return;
  }
  ++phase;
  t_prev = t_now;
  next = g_xenos_test_frame + 120;
}

void present_frame() {
  if (!ensure_device()) return;
  NTRACE("---- end of frame {} ({} draws)", g.frames + 1, g.draws);
  if (REXCVAR_GET(gpu_native_trace_frame) == -1)  // the test pipe's TRACE: the next frame
    rex::cvar::SetFlagByName("gpu_native_trace_frame", std::to_string(g.frames + 2));
  if (g_mark_trace_file && g.frames + 1 > g_mark_trace_frame) {  // the marked frame is written
    std::fclose(g_mark_trace_file);
    g_mark_trace_file = nullptr;
    rex::cvar::SetFlagByName("gpu_native_trace_frame", "0");
    REXKRNL_INFO("Player mark frame trace: {} (frame {}, {} draws, {} lines, {} KB{})", g_mark_trace_path,
                 g_mark_trace_frame, g_mark_trace_draws, g_mark_trace_lines, g_mark_trace_bytes / 1024,
                 g_mark_trace_bytes >= kMarkTraceCap ? ", cut at the size cap" : "");
  }
  {
    std::string path;
    {
      std::lock_guard lock(g_mark_trace_mutex);
      path.swap(g_mark_trace_request);
    }
    if (!path.empty() && !g_mark_trace_file) {
      if ((g_mark_trace_file = _wfopen(std::filesystem::path(path).c_str(), L"wb")) != nullptr) {
        g_mark_trace_path = path;
        g_mark_trace_frame = g.frames + 2;
        g_mark_trace_bytes = 0;
        g_mark_trace_lines = g_mark_trace_draws = 0;
        const std::string head = fmt::format(
            "# OutRun player mark frame trace: frame {} (Plume, {}), render scale {}x, msaa {}. Per draw: draw#, prim, "
            "vs/ps shader hashes, z enable/write(w)/func, cull, blend src/dst/op, write mask, alpha test, textures "
            "(sN=guest texture/fetch); then viewport, depthcontrol (RB_DEPTHCONTROL), stencil, blend factor, guest "
            "constants (vs c17 = eye/camera), vertex layout. Resolves name their render target and depth surface.{}",
            g_mark_trace_frame, g_vulkan ? "Vulkan" : "Direct3D 12", outrun::ActiveRenderScale(),
            rex::cvar::GetFlagByName("gpu_native_msaa"), char(10));
        std::fwrite(head.data(), 1, head.size(), g_mark_trace_file);
        rex::cvar::SetFlagByName("gpu_native_trace_count", "1");
        rex::cvar::SetFlagByName("gpu_native_trace_frame", std::to_string(g_mark_trace_frame));
      } else {
        REXKRNL_WARN("Player mark frame trace: cannot write {}", path);
      }
    }
  }
  if (REXCVAR_GET(gpu_native_f9_trace)) {  // F9 with the game window in front: trace the next two frames into the log (testers)
    static bool was_down = false;
    const bool down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (down && !was_down) {
      DWORD pid = 0;
      GetWindowThreadProcessId(GetForegroundWindow(), &pid);
      if (pid == GetCurrentProcessId()) {
        rex::cvar::SetFlagByName("gpu_native_trace_count", "2");
        rex::cvar::SetFlagByName("gpu_native_trace_frame", std::to_string(g.frames + 2));
        REXKRNL_INFO("ORNATIVE F9: tracing frames {} and {}", g.frames + 2, g.frames + 3);
      }
    }
    was_down = down;
  }
  if (!g_vulkan && (g.frames % 60) == 0) {  // the GPU device lost (driver reset, TDR): once, with the place
    static bool reported = false;
    ID3D12Device8* d3d = g.device ? static_cast<D3D12Device*>(g.device.get())->d3d : nullptr;
    const HRESULT hr = d3d && !reported ? d3d->GetDeviceRemovedReason() : S_OK;
    if (hr != S_OK) {
      reported = true;
      REXKRNL_ERROR("ORNATIVE GPU device removed (reason {:08X}) at {}", uint32_t(hr), outrun::player_log::Where());
      rex::FlushLogging();
    }
  }
  begin_frame();
  const int32_t debug = REXCVAR_GET(gpu_native_debug_clear);
  if (g.draws == 0 && debug) {
    const uint32_t c = uint32_t(debug);
    g.cmd->clearColor(0, RenderColor(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f, 1.0f));
  }
  bool have_image = false;
  const double p0 = now_ms();
  double p1 = p0, p2 = p0, p3 = p0;
  {
    const uint32_t k = g.out.next;
    if (g_vulkan && g.frames + 1 == uint64_t(REXCVAR_GET(gpu_native_dump_resolves))) record_resolve_dumps();
    have_image = copy_to_presenter_texture(k);
    close_open_query();
    g.edram_color.clear();  // (EDRAM aliasing within a frame only)
    g.restore_rt = 0;
    if (g.timestamps) g.cmd->writeTimestamp(g.timestamps.get(), g.slot * 2 + 1);
    g.cmd->end();
    g.recording = false;
    p1 = p2 = now_ms();
    if (g_vulkan) {
      submit_frame_lists(nullptr, 0, nullptr, 0);
    } else {
      ID3D12CommandQueue* q = static_cast<D3D12CommandQueue*>(g.queue.get())->d3d;
      if (have_image && g.out.tex_done[k]) q->Wait(g.out.done, g.out.tex_done[k]);  // the presenter has read it
      submit_frame_lists(nullptr, 0, nullptr, 0);
      if (have_image) {
        q->Signal(g.out.ready, ++g.out.ready_value);
        g.out.tex_ready[k] = g.out.ready_value;
      }
    }
    p3 = now_ms();
  }
  g.pending[g.slot] = true;
  if (g.prof_active) {  // wait for this frame now and log where its GPU time went
    g.queue->waitForCommandFence(g.fence);
    g.pending[g.slot] = false;
    g.prof_pool->queryResults();
    const uint64_t* ts = g.prof_pool->getResults();
    std::vector<std::pair<double, size_t>> ops;
    std::unordered_map<std::string, std::pair<double, int>> kinds;
    for (size_t i = 1; i < g.prof_labels.size(); ++i) {
      const double ms = ts[i] > ts[i - 1] ? double(ts[i] - ts[i - 1]) / 1e6 : 0.0;
      ops.emplace_back(ms, i);
      const std::string& l = g.prof_labels[i];
      std::string kind = l.rfind("draw#", 0) == 0 ? l.substr(l.find(" vs ")) : l.substr(0, l.find(' '));
      kinds[kind].first += ms;
      ++kinds[kind].second;
    }
    const double total = g.prof_labels.size() > 1 ? double(ts[g.prof_labels.size() - 1] - ts[0]) / 1e6 : 0.0;
    REXKRNL_INFO("ORNATIVE profile frame {}: {} operations, {:.3f} ms GPU", g.frames + 1, ops.size(), total);
    std::sort(ops.begin(), ops.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < ops.size() && i < 25; ++i)
      REXKRNL_INFO("ORNATIVE profile op {:.3f} ms {}", ops[i].first, g.prof_labels[ops[i].second]);
    std::vector<std::pair<double, std::string>> ks;
    for (auto& [k, v] : kinds) ks.emplace_back(v.first, fmt::format("{} x{}", k, v.second));
    std::sort(ks.begin(), ks.end(), [](auto& a, auto& b) { return a.first > b.first; });
    for (size_t i = 0; i < ks.size() && i < 20; ++i)
      REXKRNL_INFO("ORNATIVE profile kind {:.3f} ms {}", ks[i].first, ks[i].second);
    g.prof_active = false;
  }
  if (have_image) {
    pace_present();
    // (The Xenos vsync worker keeps reporting vblanks to the presenter's pacing,
    // unlike GoldenEye's port, which had to report them from here.)
    if (g_vulkan) {
      // The frame comes back through system memory: wait for it here (the
      // GPU's part of a frame is ~1-2 ms; the slot is then free at once).
      if (g.pending[g.slot]) {
        g.queue->waitForCommandFence(g.fence);
        g.pending[g.slot] = false;
      }
      if (!g_resolve_dumps.empty()) write_resolve_dumps();
      present_vulkan(g.out.next);
    } else {
      present_to_presenter(g.out.next);
    }
    g.out.next = (g.out.next + 1) % 3;
    note_present();
  }
  // Next frame records into the next slot once the GPU is done with the frame
  // that used it last (kFrames frames ago): the game thread doesn't wait for
  // this frame's GPU work, nor for one GPU clock stall.
  g.slot = (g.slot + 1) % kFrames;
  g.cmd = g.cmds[g.slot].get();
  g.fence = g.fences[g.slot].get();
  const bool finished = g.pending[g.slot];
  const double p4 = now_ms();
  if (finished) { g.queue->waitForCommandFence(g.fence); g.pending[g.slot] = false; }
  if (finished || !g.pending[g.slot]) harvest_queries(g.slot);
  g.present_ms[0] = p1 - p0; g.present_ms[1] = p3 - p2; g.present_ms[2] = p4 - p3; g.present_ms[3] = now_ms() - p4;
  g.const_used = 0;
  g.const_dedup.clear();
  drain_debug_messages("frame");
  // GPU time of that finished frame's work (submitted at once, so no CPU gaps
  // inside), logged like the Xenos backend's "ge gpu time" line every 600 frames.
  if (g.timestamps && finished) {
    g.timestamps->queryResults();
    const uint64_t* ts = g.timestamps->getResults() + g.slot * 2;  // nanoseconds
    const double ms = ts[1] > ts[0] ? double(ts[1] - ts[0]) / 1e6 : 0.0;
    const uint64_t copy_start = g.timestamps->getResults()[2 * kFrames + g.slot];  // constants copy, just before the frame
    g.copy_ms_sum += ts[0] > copy_start && ts[0] - copy_start < 100000000ull ? double(ts[0] - copy_start) / 1e6 : 0.0;
    g.gpu_ms_sum += ms;
    g.gpu_ms_max = std::max(g.gpu_ms_max, ms);
    if (++g.gpu_frames >= kStatsFrames) {
      REXKRNL_INFO("ORNATIVE gpu time per frame: {:.2f} ms (max {:.2f}) + constants copy {:.2f} ms",
                   g.gpu_ms_sum / g.gpu_frames, g.gpu_ms_max, g.copy_ms_sum / g.gpu_frames);
      g.gpu_ms_sum = g.gpu_ms_max = g.copy_ms_sum = 0;
      g.gpu_frames = 0;
    }
  }
  g.upload_used = 0;
  for (int i = 0; i < 5; ++i) { g.last_sect[i] = g.sect[i]; g.sect[i] = 0; }
  g.last_vdecode_ms = g.vdecode_ms; g.vdecode_ms = 0;
  g.last_flush_ms = g.flush_ms; g.last_alloc_ms = g.alloc_ms; g.last_hash_ms = g.hash_ms; g.flush_ms = g.alloc_ms = g.hash_ms = 0; g.last_flushes = g.flushes; g.flushes = 0;
  g.last_tex_decode_ms = g.tex_decode_ms; g.last_tex_mip_ms = g.tex_mip_ms; g.tex_decode_ms = g.tex_mip_ms = 0;
  g.last_vcache_hits = g.vcache_hits; g.last_vcache_misses = g.vcache_misses; g.vcache_hits = g.vcache_misses = 0;
  // Buffers dropped two or more frames ago are no longer used by the GPU.
  std::erase_if(g.retired_textures, [](const auto& r) {
    if (r.frame + kFrames > g.frames) return false;
    if (r.slot) g.free_slots.push_back(r.slot);
    return true;
  });
  std::erase_if(g.retired_sets, [](const auto& r) { return r.frame + kFrames <= g.frames; });
  std::erase_if(g.retired, [](const auto& r) {
    if (r.frame + kFrames > g.frames) return false;
    r.pool->free_lists[r.alloc.size_class].push_back(r.alloc);
    return true;
  });
  if ((g.frames % 600) == 0)
    for (auto it = g.vertex_cache.begin(); it != g.vertex_cache.end();) {
      if (g.frames - it->second.last_frame <= 600) { ++it; continue; }
      retire(g.vertex_pool, it->second.buffer);
      it = g.vertex_cache.erase(it);
    }
  if ((g.frames % 600) == 0)
    for (auto it = g.index_cache.begin(); it != g.index_cache.end();) {
      if (g.frames - it->second.last_frame <= 600) { ++it; continue; }
      retire(g.index_pool, it->second.buffer);
      it = g.index_cache.erase(it);
    }
  // Textures not drawn for 30 s give back their slot and memory (they decode
  // again if drawn later); ones that could not be prepared are tried again.
  // Kept for good, a long session (car select, a race into its later stages)
  // used up the descriptor slots.
  if ((g.frames % 600) == 300 && REXCVAR_GET(gpu_native_query_test) != 43)  // (test 43: kept for good, as before)
    for (auto it = g.textures_by_fetch.begin(); it != g.textures_by_fetch.end();) {
      ResolvedTexture& t = it->second;
      if (t.job) { ++it; continue; }
      if (t.texture && t.checked_frame + 1800 < g.frames) {
        g.retired_textures.push_back({g.frames, std::move(t.texture), t.slot});
        it = g.textures_by_fetch.erase(it);
      } else if (!t.texture && t.slot == UINT32_MAX) {
        it = g.textures_by_fetch.erase(it);
      } else {
        ++it;
      }
    }
  g.last_pipe_ms = g.pipe_ms; g.last_tex_ms = g.tex_ms; g.last_pipe_n = g.pipe_n; g.last_tex_n = g.tex_n;
  g.pipe_ms = g.tex_ms = 0; g.pipe_n = g.tex_n = 0; g.tex_requests = 0;
  if ((g.frames % kStatsFrames) == kStatsFrames - 1) {
    auto used = [](const BufferPool& p) {
      uint64_t free_bytes = 0;
      for (uint32_t c = 0; c < kSizeClasses; ++c) free_bytes += p.free_lists[c].size() * class_bytes(c);
      return fmt::format("{} chunks, {:.1f} MB free-listed, {:.1f} MB bump left", p.chunks.size(), free_bytes / 1048576.0,
                         p.bump == UINT64_MAX ? 0.0 : (p.chunk - std::min(p.bump, p.chunk)) / 1048576.0);
    };
    REXKRNL_INFO("ORNATIVE pools: vertex {} ({} entries); index {} ({} entries); upload ring peak {:.1f} of {:.0f} MB, "
                 "constants peak {:.2f} of {:.0f} MB, {} mid-frame flushes (last minute); texture slots {} of {} ({} free), "
                 "{} cached textures; samplers {} of {}",
                 used(g.vertex_pool), g.vertex_cache.size(), used(g.index_pool), g.index_cache.size(),
                 g.upload_peak / 1048576.0, kSlotUpload / 1048576.0, g.const_peak / 1048576.0, kSlotConsts / 1048576.0,
                 g.flushes_600, g.next_slot, kTextureSlots, g.free_slots.size(), g.textures_by_fetch.size(),
                 g.sampler_objects.size() + 1, kSamplerSlots);
    g.upload_peak = g.const_peak = 0;
    g.flushes_600 = 0;
    // Video memory this process uses on the adapter (local: VRAM, or the iGPU's
    // carve-out; non-local: system memory the GPU reads), and the render-target sets.
    IDXGIAdapter3* adapter3 = nullptr;
    if (IDXGIAdapter1* adapter = g_vulkan ? nullptr : static_cast<D3D12Device*>(g.device.get())->adapter;
        adapter && SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3)))) {
      DXGI_QUERY_VIDEO_MEMORY_INFO local{}, nonlocal{};
      adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local);
      adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &nonlocal);
      adapter3->Release();
      auto texels = [](const RenderTexture* t) {
        const RenderTextureDesc& d = static_cast<const D3D12Texture*>(t)->desc;
        return uint64_t(d.width) * d.height * uint32_t(d.multisampling.sampleCount);
      };
      uint64_t sets = 0;  // depth-stencil and own colour (multisampled sets), 4 bytes per sample
      for (const auto& [key, s] : g.scene_sets)
        if (s.depth) sets += ((s.color ? texels(s.color.get()) : 0) + texels(s.depth.get())) * 4;
      if (g.scene_depth) sets += ((g.scene ? texels(g.scene.get()) : 0) + texels(g.scene_depth.get())) * 4;
      REXKRNL_INFO("ORNATIVE video memory: {:.0f} MB local, {:.0f} MB non-local; {} target sets ~{:.0f} MB, {} colour targets "
                   "~{:.0f} MB", local.CurrentUsage / 1048576.0, nonlocal.CurrentUsage / 1048576.0, g.scene_sets.size() + 1,
                   sets / 1048576.0, g.color_targets.size(),
                   g.color_targets.size() * double(g_scene_w) * g_scene_h * 4 / 1048576.0);
    }
  }
  if (++g.frames == 1 || (g.frames % kStatsFrames) == 0) {
    REXKRNL_INFO("ORNATIVE frame {}: {} draws, {} skipped (shader {} decl {} prim {} range {} stream {} pipeline {}), "
                 "{} depth copies in all; occlusion queries {} begun, {} read, {} objects",
                 g.frames, g.draws, g.skipped, g.skip_reason[0], g.skip_reason[1], g.skip_reason[2],
                 g.skip_reason[3], g.skip_reason[4], g.skip_reason[5], g.depth_copies, g_occ.begins, g_occ.reads,
                 g_occ.slot_of.size());
    g_occ.begins = g_occ.reads = 0;
  }
}

// Submits what has been recorded this frame, waits, and carries on with an
// empty upload ring (level loads upload far more texture data than fits).
void flush_mid_frame() {
  if (!g.recording) { g.upload_used = 0; return; }
  const double t0 = now_ms();
  struct T { double t0; ~T() { g.flush_ms += now_ms() - t0; } } timer{t0};
  close_open_query();
  g.cmd->end();
  submit_frame_lists(nullptr, 0, nullptr, 0);
  g.queue->waitForCommandFence(g.fence);
  g.upload_used = 0;
  g.const_used = 0;
  g.const_dedup.clear();
  g.cmd->begin();
  g.layout_bound = false;
  g.bound_fb = nullptr;
  bind_target();
  ++g.flushes;
  ++g.flushes_600;
}

// Depth resolve (D3DRESOLVE_DEPTHSTENCIL, flag 0x4): the scene's depth into an
// R32F texture the game samples (the ZSprite pass reads it back as depth).
// Sample 0 of each pixel, as the game asks for one fragment. While nothing has
// written depth since its last clear, a 1x1 texture of the clear value is
// enough (and cheaper); otherwise a compute pass copies the whole target.
constexpr char kDepthCopyShader[] = R"(
#ifdef MSAA
Texture2DMS<float> src : register(t0);
#else
Texture2D<float> src : register(t0);
#endif
RWTexture2D<float> dst : register(u1);
[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
  uint w, h;
  dst.GetDimensions(w, h);
  if (id.x >= w || id.y >= h) return;
#ifdef MSAA
  dst[id.xy] = src.Load(int2(id.xy), 0);
#else
  dst[id.xy] = src.Load(int3(id.xy, 0));
#endif
}
)";

// The depth copy for a single-sampled or a multisampled depth target (passes
// the 360 draws at 1x have 1x targets here).
bool ensure_depth_pass(bool msaa) {
  if (g.depth_pipe[msaa]) return true;
  if (g.depth_pass_failed) return false;
  g.depth_pass_failed = true;  // until it is up
  std::string source = kDepthCopyShader;
  if (msaa) source = "#define MSAA 1\n" + source;
  std::vector<uint8_t> dxil;
  std::string errors;
  if (!outrun::native_shaders::CompileDxil(source, L"cs_6_0", dxil, errors)) {
    REXKRNL_WARN("ORNATIVE depth copy shader: {}", errors.substr(0, 300));
    return false;
  }
  if (!g.depth_layout) {
    RenderDescriptorSetBuilder sb;
    sb.begin();
    sb.addTexture(0);
    sb.addReadWriteTexture(1);
    sb.end();
    RenderPipelineLayoutBuilder lb;
    lb.begin();
    lb.addDescriptorSet(sb);
    lb.end();
    g.depth_layout = lb.create(g.device.get());
  }
  g.depth_shader[msaa] = g.device->createShader(dxil.data(), dxil.size(), "main",
                                               g_vulkan ? RenderShaderFormat::SPIRV : RenderShaderFormat::DXIL);
  g.depth_pipe[msaa] =
      g.device->createComputePipeline(RenderComputePipelineDesc(g.depth_layout.get(), g.depth_shader[msaa].get(), 8, 8, 1));
  if (!g.depth_pipe[msaa]) return false;
  g.depth_pass_failed = false;
  return true;
}

void native_depth_resolve(uint32_t dest) {
  if (g.bound_scene) {
    apply_pending_clears();
    apply_pending_restore();
  }
  ResolvedTexture& r = g.resolved[dest];
  r.is_depth = true;
  // Other guest textures over its memory (an A8R8G8B8 view of the words) find it.
  if (const uint32_t base = g32(dest + 32) & 0xFFFFF000u) g.resolved_base[base] = dest;
  if (!g.depth_written || !REXCVAR_GET(gpu_native_depth_copy)) {  // every texel is the clear value
    const float value = g.depth_clear;
    r.depth_pending = false;
    if (r.texture && r.span == 1 && r.content == uint64_t(std::bit_cast<uint32_t>(value))) return;
    if (r.texture) g.retired_textures.push_back({g.frames, std::move(r.texture), r.slot});
    r.texture = g.device->createTexture(RenderTextureDesc::Texture2D(1, 1, 1, RenderFormat::R32_FLOAT));
    r.slot = alloc_texture_slot();
    r.span = 1;
    r.content = std::bit_cast<uint32_t>(value);
    ensure_room(512);
    const uint64_t off = upload_alloc(256, 512);
    if (off == UINT64_MAX) return;
    std::memcpy(g.upload_ptr + off, &value, 4);
    g.cmd->barriers(RenderBarrierStage::COPY, RenderTextureBarrier(r.texture.get(), RenderTextureLayout::COPY_DEST));
    g.cmd->copyTextureRegion(RenderTextureCopyLocation::Subresource(r.texture.get(), 0),
                             RenderTextureCopyLocation::PlacedFootprint(g.upload.get(), RenderFormat::R32_FLOAT, 1, 1, 1, 64, off));
    g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(r.texture.get(), RenderTextureLayout::SHADER_READ));
    g.textures->setTexture(r.slot, r.texture.get(), RenderTextureLayout::SHADER_READ);
    g.bound_fb = nullptr;
    bind_target();
    return;
  }
  const bool msaa = g.scene_msaa.sampleCount != RenderSampleCount::COUNT_1;
  if (!ensure_depth_pass(msaa)) return;
  RenderTexture* src = g.scene_depth.get();
  // The destination texture's own size (fetch dword 2 at +36), within the
  // depth target: the shadow map is 1024x1024, the scene's depth 1280x720.
  const uint32_t d2 = g32(dest + 36);
  const uint32_t tw = std::min(g.scene_tw, ((d2 & 0x1FFF) + 1) * g_scene_scale);
  const uint32_t th = std::min(g.scene_th, (((d2 >> 13) & 0x1FFF) + 1) * g_scene_scale);
  if (r.texture && r.span == 0 && r.w == tw && r.h == th && r.content == g.depth_gen && r.depth_src == src)
    return;  // already holds this depth
  if (!r.texture || r.span != 0 || r.w != tw || r.h != th) {  // full size
    if (r.texture) g.retired_textures.push_back({g.frames, std::move(r.texture), r.slot});
    r.texture = g.device->createTexture(RenderTextureDesc::Texture2D(tw, th, 1, RenderFormat::R32_FLOAT, kStorageFlags));
    r.slot = alloc_texture_slot();
    r.span = 0;
    r.w = tw;
    r.h = th;
    r.depth_src = nullptr;
  }
  if (r.depth_src != src) {
    // A set reading this depth target (a resolve from another pass's depth
    // gets a new one: the old may still be read by a frame in flight).
    RenderDescriptorSetBuilder sb;
    sb.begin();
    sb.addTexture(0);
    sb.addReadWriteTexture(1);
    sb.end();
    std::unique_ptr<RenderDescriptorSet>& set = g.depth_sets[dest];
    if (set) g.retired_sets.push_back({g.frames, std::move(set)});
    set = sb.create(g.device.get());
    if (!g.depth_view)  // a pass set swapped in (bind_target) has its own view, made here
      g.depth_view = src->createTextureView(RenderTextureViewDesc::Texture2D(kDepthFormat));
    set->setTexture(0, src, RenderTextureLayout::SHADER_READ, g.depth_view.get());
    set->setTexture(1, r.texture.get(), RenderTextureLayout::GENERAL);
    r.depth_src = src;
    r.depth_src_msaa = msaa;
    r.content = 0;
  }
  if (!r.depth_pending) {
    r.depth_pending = true;
    g.depth_pending.push_back(dest);
  }
}

// The copy of a recorded depth resolve: the scene depth as it is now.
void copy_pending_depth(uint32_t dest, ResolvedTexture& r) {
  r.depth_pending = false;
  std::erase(g.depth_pending, dest);
  if (r.content == g.depth_gen || !r.depth_src) return;
  // The depth target the resolve read (its set's, bound or parked now).
  g.cmd->barriers(RenderBarrierStage::COMPUTE, {RenderTextureBarrier(r.depth_src, RenderTextureLayout::SHADER_READ),
                                                RenderTextureBarrier(r.texture.get(), RenderTextureLayout::GENERAL)});
  g.cmd->setComputePipelineLayout(g.depth_layout.get());
  g.cmd->setPipeline(g.depth_pipe[r.depth_src_msaa].get());
  g.cmd->setComputeDescriptorSet(g.depth_sets[dest].get(), 0);
  g.cmd->dispatch((r.w + 7) / 8, (r.h + 7) / 8, 1);
  r.content = g.depth_gen;
  ++g.depth_copies;
  g.cmd->barriers(RenderBarrierStage::GRAPHICS, {RenderTextureBarrier(r.depth_src, RenderTextureLayout::DEPTH_WRITE),
                                                 RenderTextureBarrier(r.texture.get(), RenderTextureLayout::SHADER_READ)});
  g.textures->setTexture(r.slot, r.texture.get(), RenderTextureLayout::SHADER_READ);
  g.bound_fb = nullptr;
  bind_target();
}

// Before a draw or clear changes the scene depth: recorded depth resolves are
// copied now if their texture has been sampled lately (so it gets the depth as
// it was at the resolve); the rest stay recorded and cost nothing unless a
// draw samples them later (then they get the depth of that moment).
void depth_will_change() {
  for (size_t i = 0; i < g.depth_pending.size();) {
    const uint32_t dest = g.depth_pending[i];
    ResolvedTexture& r = g.resolved[dest];
    if (r.depth_used_frame && r.depth_used_frame + 120 > g.frames + 1)
      copy_pending_depth(dest, r);  // erases it from the list
    else
      ++i;
  }
}

// A depth resolve sampled through a k_8_8_8_8 texture (A8R8G8B8 swizzle) reads
// the D24S8 words the 360 resolved: R depth bits 8-15, G bits 0-7, B the
// stencil (0 here), A bits 16-23. The race copies them into a colour surface
// that shares EDRAM with a depth surface, which then holds that depth again.
constexpr char kDepthWordsShader[] = R"(
Texture2D<float> src : register(t0);
[[vk::image_format("rgba8")]] RWTexture2D<float4> dst : register(u1);  // (SPIR-V: the R8G8B8A8 target's format)
[numthreads(8, 8, 1)] void main(uint3 id : SV_DispatchThreadID) {
  uint w, h, sw, sh;
  dst.GetDimensions(w, h);
  if (id.x >= w || id.y >= h) return;
  src.GetDimensions(sw, sh);
  // round(), not + 0.5: 16777215.5 rounds to 2^24 in a float, so depth 1 lost its top byte.
  const uint d = min(uint(round(saturate(src.Load(int3(min(id.xy, uint2(sw - 1, sh - 1)), 0))) * 16777215.0)), 0xFFFFFFu);
  dst[id.xy] = float4((d >> 8) & 0xFF, d & 0xFF, 0, d >> 16) / 255.0;
}
)";

bool ensure_words_pass() {
  if (g.words_pipe) return true;
  if (!g.depth_layout && !ensure_depth_pass(false)) return false;
  static bool failed = false;
  if (failed) return false;
  failed = true;  // until it is up
  std::vector<uint8_t> dxil;
  std::string errors;
  if (!outrun::native_shaders::CompileDxil(kDepthWordsShader, L"cs_6_0", dxil, errors)) {
    REXKRNL_WARN("ORNATIVE depth words shader: {}", errors.substr(0, 300));
    return false;
  }
  g.words_shader = g.device->createShader(dxil.data(), dxil.size(), "main",
                                         g_vulkan ? RenderShaderFormat::SPIRV : RenderShaderFormat::DXIL);
  g.words_pipe =
      g.device->createComputePipeline(RenderComputePipelineDesc(g.depth_layout.get(), g.words_shader.get(), 8, 8, 1));
  if (!g.words_pipe) return false;
  failed = false;
  return true;
}

// The words of depth resolve `dest`, made again when its depth has changed.
uint32_t depth_words_slot(uint32_t dest, ResolvedTexture& r, bool may_commit) {
  if (!may_commit) return r.words ? r.words_slot : r.slot;
  r.depth_used_frame = g.frames + 1;
  if (r.depth_pending) copy_pending_depth(dest, r);
  if (!ensure_words_pass()) return r.slot;
  const uint32_t ww = r.span == 0 ? r.w : g_scene_w, wh = r.span == 0 ? r.h : g_scene_h;  // the depth's size
  if (!r.words || r.words_w != ww || r.words_h != wh) {
    if (r.words) g.retired_textures.push_back({g.frames, std::move(r.words), 0});
    r.words = g.device->createTexture(RenderTextureDesc::Texture2D(ww, wh, 1, RenderFormat::R8G8B8A8_UNORM, kStorageFlags));
    if (!r.words_slot) {
      r.words_slot = alloc_texture_slot();
      REXKRNL_INFO("ORNATIVE depth resolve {:08X} read as D24S8 words: slot {}", dest, r.words_slot);
    }
    g.textures->setTexture(r.words_slot, r.words.get(), RenderTextureLayout::SHADER_READ);
    r.words_w = ww;
    r.words_h = wh;
    r.words_src = nullptr;  // a set for the new texture
  }
  if (r.words_src == r.texture.get() && r.words_span == r.span && r.words_content == r.content) return r.words_slot;
  if (r.words_src != r.texture.get()) {  // a new depth texture: a set for it (the old one may be in flight)
    RenderDescriptorSetBuilder sb;
    sb.begin();
    sb.addTexture(0);
    sb.addReadWriteTexture(1);
    sb.end();
    if (r.words_set) g.retired_sets.push_back({g.frames, std::move(r.words_set)});
    r.words_set = sb.create(g.device.get());
    r.words_set->setTexture(0, r.texture.get(), RenderTextureLayout::SHADER_READ);
    r.words_set->setTexture(1, r.words.get(), RenderTextureLayout::GENERAL);
  }
  g.cmd->barriers(RenderBarrierStage::COMPUTE, {RenderTextureBarrier(r.texture.get(), RenderTextureLayout::SHADER_READ),
                                                RenderTextureBarrier(r.words.get(), RenderTextureLayout::GENERAL)});
  g.cmd->setComputePipelineLayout(g.depth_layout.get());
  g.cmd->setPipeline(g.words_pipe.get());
  g.cmd->setComputeDescriptorSet(r.words_set.get(), 0);
  g.cmd->dispatch((r.words_w + 7) / 8, (r.words_h + 7) / 8, 1);
  g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(r.words.get(), RenderTextureLayout::SHADER_READ));
  r.words_src = r.texture.get();
  r.words_span = r.span;
  r.words_content = r.content;
  return r.words_slot;
}

// EDRAM colour -> depth: a depth surface bound at the EDRAM address of a colour
// surface drawn there alone since holds that colour's words as D24S8 (depth in
// the upper 24 bits of R | G << 8 | B << 16 | A << 24), with the two 40-sample
// halves of each 80-sample tile row swapped, as Xenos lays depth out in EDRAM.
constexpr char kRestoreVertexShader[] = R"(
float4 main(uint id : SV_VertexID) : SV_Position {
  const float2 p = float2((id << 1) & 2, id & 2);
  return float4(p * float2(2, -2) + float2(-1, 1), 0, 1);
}
)";
constexpr char kRestorePixelShader[] = R"(
Texture2D<float4> src : register(t0);
float main(float4 pos : SV_Position) : SV_Depth {
  const int x = int(pos.x), y = int(pos.y);
  const int xs = (x % 80) < 40 ? x + 40 : x - 40;
  const uint4 b = uint4(round(saturate(src.Load(int3(xs, y, 0))) * 255.0));
  return float(b.g | (b.b << 8) | (b.a << 16)) / 16777215.0;
}
)";

RenderPipeline* restore_pipeline(RenderSampleCounts samples) {
  const uint32_t i = std::min<uint32_t>(uint32_t(samples), 4);
  if (g.restore_pipe[i]) return g.restore_pipe[i].get();
  if (g.restore_failed) return nullptr;
  if (!g.restore_layout) {
    g.restore_failed = true;  // until it is up
    std::vector<uint8_t> vs, ps;
    std::string errors;
    if (!outrun::native_shaders::CompileDxil(kRestoreVertexShader, L"vs_6_0", vs, errors) ||
        !outrun::native_shaders::CompileDxil(kRestorePixelShader, L"ps_6_0", ps, errors)) {
      REXKRNL_WARN("ORNATIVE depth restore shaders: {}", errors.substr(0, 300));
      return nullptr;
    }
    const RenderShaderFormat format = g_vulkan ? RenderShaderFormat::SPIRV : RenderShaderFormat::DXIL;
    g.restore_vs = g.device->createShader(vs.data(), vs.size(), "main", format);
    g.restore_ps = g.device->createShader(ps.data(), ps.size(), "main", format);
    RenderDescriptorSetBuilder sb;
    sb.begin();
    sb.addTexture(0);
    sb.end();
    RenderPipelineLayoutBuilder lb;
    lb.begin();
    lb.addDescriptorSet(sb);
    lb.end();
    g.restore_layout = lb.create(g.device.get());
    g.restore_failed = false;
  }
  RenderGraphicsPipelineDesc d;
  d.pipelineLayout = g.restore_layout.get();
  d.vertexShader = g.restore_vs.get();
  d.pixelShader = g.restore_ps.get();
  d.primitiveTopology = RenderPrimitiveTopology::TRIANGLE_LIST;
  d.renderTargetFormat[0] = kSceneFormat;
  d.renderTargetCount = 1;
  RenderBlendDesc blend = RenderBlendDesc::Copy();
  blend.renderTargetWriteMask = 0;
  d.renderTargetBlend[0] = blend;
  d.multisampling = RenderMultisampling(samples);
  d.depthTargetFormat = kDepthFormat;
  d.depthEnabled = true;
  d.depthWriteEnabled = true;
  d.depthFunction = RenderComparisonFunction::ALWAYS;
  d.cullMode = RenderCullMode::NONE;
  g.restore_pipe[i] = g.device->createGraphicsPipeline(d);
  if (g.restore_pipe[i] && !pipeline_ok(g.restore_pipe[i].get())) {
    REXKRNL_WARN("ORNATIVE depth restore pipeline failed to create");
    g.restore_pipe[i].reset();
    g.restore_failed = true;
  }
  return g.restore_pipe[i].get();
}

// Runs a restore bind_target found for the bound set (after the deferred
// resolve clears, which came first on the 360).
void apply_pending_restore() {
  if (!g.restore_rt || !g.bound_scene || !g.guest_depth) return;
  const uint32_t rt = g.restore_rt;
  g.restore_rt = 0;
  g.edram_color.erase(g32(g.guest_depth + 28) & 0xFFF);
  if (REXCVAR_GET(gpu_native_query_test) == 41) return;  // test: no rebuild
  RenderPipeline* pipe = restore_pipeline(g.scene_msaa.sampleCount);
  if (!pipe) return;
  RenderTexture* src = color_target(rt).color.get();
  std::unique_ptr<RenderDescriptorSet>& set = g.restore_sets[rt];
  if (!set) {
    RenderDescriptorSetBuilder sb;
    sb.begin();
    sb.addTexture(0);
    sb.end();
    set = sb.create(g.device.get());
    set->setTexture(0, src, RenderTextureLayout::SHADER_READ);
  }
  depth_will_change();  // resolves of the old depth that are still to be copied
  g.cmd->barriers(RenderBarrierStage::GRAPHICS,
                  {RenderTextureBarrier(src, RenderTextureLayout::SHADER_READ),
                   RenderTextureBarrier(g.scene_color, RenderTextureLayout::COLOR_WRITE),
                   RenderTextureBarrier(g.scene_depth.get(), RenderTextureLayout::DEPTH_WRITE)});
  g.cmd->setFramebuffer(g.scene_fb.get());
  g.bound_fb = g.scene_fb.get();
  g.cmd->setGraphicsPipelineLayout(g.restore_layout.get());
  g.cmd->setPipeline(pipe);
  g.cmd->setGraphicsDescriptorSet(set.get(), 0);
  const RenderViewport viewport(0, 0, float(g.scene_tw), float(g.scene_th), 0, 1);
  g.cmd->setViewports(&viewport, 1);
  const RenderRect scissor(0, 0, g.scene_tw, g.scene_th);
  g.cmd->setScissors(&scissor, 1);
  g.cmd->drawInstanced(3, 1, 0, 0);
  g.layout_bound = false;  // the next draw binds the game's layout again
  g.depth_written = true;
  ++g.depth_gen;
  NTRACE("  depth {:08X} rebuilt from colour {:08X} (EDRAM {:03X})", g.guest_depth, rt, g32(g.guest_depth + 28) & 0xFFF);
}

// Resolve: the bound target's colour into a texture keyed by the guest
// destination (depth: native_depth_resolve); the game resolves each EDRAM tile
// separately, so repeats with no draws in between are skipped. Flags (XDK):
// 0x4 depth/stencil, 0x8 all fragments, 0x10.. one fragment, 0x100 / 0x200
// clear colour / depth afterwards (the game clears explicitly as well).
// dest_point: D3DPOINT (x, y) in the destination, or 0; face: DestSliceOrFace,
// the cube face a resolve into a cube map writes (D3DCUBEMAP_FACES order, as
// Direct3D 12's array slices).
void native_resolve_copy(uint32_t dest, uint32_t flags, uint32_t src_rect, uint32_t dest_point, uint32_t face) {
  if (!dest || !ensure_device() || REXCVAR_GET(gpu_native_skip_resolves)) return;
  NTRACE("  resolve copy {:08X}: last {:08X} draws since {} bound scene {} rt {:08X} rect {} point {} face {}", dest,
         g.last_resolve_dest, g.draws_since_resolve, g.bound_scene, g.guest_rt,
         src_rect ? fmt::format("{},{}-{},{}", int32_t(g32(src_rect)), int32_t(g32(src_rect + 4)),
                                int32_t(g32(src_rect + 8)), int32_t(g32(src_rect + 12)))
                  : std::string("-"),
         dest_point ? fmt::format("{},{}", int32_t(g32(dest_point)), int32_t(g32(dest_point + 4))) : std::string("-"),
         face);
  if (flags & 0x4) {
    begin_frame();
    native_depth_resolve(dest);
    return;
  }
  begin_frame();
  bind_target();
  const bool cube = ((g32(dest + 48) >> 9) & 3) == 3;
  face = cube ? std::min<uint32_t>(face, 5) : 0;
  if (dest == g.last_resolve_dest && face == g.last_resolve_face && !g.draws_since_resolve) return;
  if (g.bound_scene) apply_pending_clears();
  g.last_resolve_dest = dest;
  g.last_resolve_face = face;
  g.draws_since_resolve = false;
  // The destination's size (its fetch constant, at the Render Scale), filled
  // from the source rectangle's corner: the Perfect Menu's blur passes draw into
  // the top-left 256x256 and 128x128 of a target and resolve just that. A
  // full-size 2D destination is the whole target (predicated tiles: the first
  // resolve). A cube map is that size per face.
  const uint32_t d2 = g32(dest + 36);
  const uint32_t dw = std::min(g_scene_w, ((d2 & 0x1FFF) + 1) * g_scene_scale);
  const uint32_t dh = std::min(g_scene_h, (((d2 >> 13) & 0x1FFF) + 1) * g_scene_scale);
  const bool full = !cube && dw == g_scene_w && dh == g_scene_h;
  uint32_t sx = 0, sy = 0, w = dw, h = dh, dx = 0, dy = 0;
  if (!full) {
    if (dest_point) {
      dx = std::min(uint32_t(std::max(0, int32_t(g32(dest_point)))) * g_scene_scale, dw - 1);
      dy = std::min(uint32_t(std::max(0, int32_t(g32(dest_point + 4)))) * g_scene_scale, dh - 1);
    }
    if (src_rect) {
      const int32_t x1 = int32_t(g32(src_rect)), y1 = int32_t(g32(src_rect + 4));
      const int32_t x2 = int32_t(g32(src_rect + 8)), y2 = int32_t(g32(src_rect + 12));
      sx = uint32_t(std::max(0, x1)) * g_scene_scale;
      sy = uint32_t(std::max(0, y1)) * g_scene_scale;
      if (x2 > x1 && y2 > y1) { w = uint32_t(x2 - x1) * g_scene_scale; h = uint32_t(y2 - y1) * g_scene_scale; }
    }
    w = std::min(w, dw - dx);
    h = std::min(h, dh - dy);
    sx = std::min(sx, g_scene_w - w);
    sy = std::min(sy, g_scene_h - h);
  }
  ResolvedTexture& r = g.resolved[dest];
  if (r.texture && r.span == 0 && (r.w != dw || r.h != dh || r.cube != cube) && !g.depth_sets.count(dest)) {
    g.retired_textures.push_back({g.frames, std::move(r.texture), 0});
    r.swizzled.clear();
    r.view.reset();
  }
  if (!r.texture) {
    if (cube) {
      r.texture = g.device->createTexture(RenderTextureDesc::Texture(RenderTextureDimension::TEXTURE_2D, dw, dh, 1, 1, 6,
                                                                     kSceneFormat, RenderTextureFlag::CUBE));
      RenderTextureViewDesc vd = RenderTextureViewDesc::TextureCube(kSceneFormat);
      vd.arraySize = 6;
      vd.mipLevels = 1;
      r.view = r.texture->createTextureView(vd);
    } else {
      r.texture = g.device->createTexture(RenderTextureDesc::Texture2D(dw, dh, 1, kSceneFormat));
    }
    r.cube = cube;
    r.w = dw;
    r.h = dh;
    if (!r.slot) r.slot = alloc_texture_slot();
    g.textures->setTexture(r.slot, r.texture.get(), RenderTextureLayout::SHADER_READ, r.view.get());
    const uint32_t d1 = g32(dest + 32), d2 = g32(dest + 36);
    if (const uint32_t base = d1 & 0xFFFFF000u) g.resolved_base[base] = dest;
    REXKRNL_INFO("ORNATIVE resolve destination {:08X}: memory {:08X}, {}x{}{}", dest, d1 & 0xFFFFF000u, (d2 & 0x1FFF) + 1,
                 ((d2 >> 13) & 0x1FFF) + 1, cube ? " cube" : "");
    if (recording_trace()) dump_trace_ring("new resolve destination");
  }
  RenderTexture* src = g.bound_scene ? g.scene_color : g.bound_color;
  {
    static std::unordered_map<uint32_t, uint32_t> logged;  // the first resolves to each destination
    if (logged[dest]++ < (cube ? 6u : 4u))
      REXKRNL_INFO("ORNATIVE resolve {:08X} {}x{} from {},{} of {} to {},{} face {} (rt {:08X} depth {:08X}), frame {} draw {}",
                   dest, w, h, sx, sy, g.bound_scene ? "scene" : "target", dx, dy, face, g.guest_rt, g.guest_depth,
                   g.frames + 1, g.draws);
  }
  if (full) {
    if (g.bound_scene) {
      copy_scene_to(r.texture.get());
    } else {
      g.cmd->barriers(RenderBarrierStage::COPY, {RenderTextureBarrier(src, RenderTextureLayout::COPY_SOURCE),
                                                 RenderTextureBarrier(r.texture.get(), RenderTextureLayout::COPY_DEST)});
      g.cmd->copyTexture(r.texture.get(), src);
    }
  } else if (g.bound_scene && g.scene_msaa.sampleCount != RenderSampleCount::COUNT_1) {
    g.cmd->barriers(RenderBarrierStage::COPY, {RenderTextureBarrier(src, RenderTextureLayout::RESOLVE_SOURCE),
                                               RenderTextureBarrier(r.texture.get(), RenderTextureLayout::RESOLVE_DEST)});
    // Plume resolves into subresource 0 only: a cube face is array slice `face`.
    if (g_vulkan) {
      VkImageResolve region{};
      region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
      region.srcOffset = {int32_t(sx), int32_t(sy), 0};
      region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, face, 1};
      region.dstOffset = {int32_t(dx), int32_t(dy), 0};
      region.extent = {w, h, 1};
      vkCmdResolveImage(static_cast<VulkanCommandList*>(g.cmd)->vk, static_cast<VulkanTexture*>(src)->vk,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, static_cast<VulkanTexture*>(r.texture.get())->vk,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    } else {
      D3D12_RECT rect{LONG(sx), LONG(sy), LONG(sx + w), LONG(sy + h)};
      static_cast<D3D12CommandList*>(g.cmd)->d3dV1->ResolveSubresourceRegion(
          static_cast<D3D12Texture*>(r.texture.get())->d3d, face, dx, dy, static_cast<D3D12Texture*>(src)->d3d, 0, &rect,
          DXGI_FORMAT_B8G8R8A8_UNORM, D3D12_RESOLVE_MODE_AVERAGE);
    }
  } else {
    g.cmd->barriers(RenderBarrierStage::COPY, {RenderTextureBarrier(src, RenderTextureLayout::COPY_SOURCE),
                                               RenderTextureBarrier(r.texture.get(), RenderTextureLayout::COPY_DEST)});
    RenderBox box;
    box.left = int32_t(sx); box.top = int32_t(sy); box.front = 0;
    box.right = int32_t(sx + w); box.bottom = int32_t(sy + h); box.back = 1;
    g.cmd->copyTextureRegion(RenderTextureCopyLocation::Subresource(r.texture.get(), 0, face),
                             RenderTextureCopyLocation::Subresource(src), dx, dy, 0, &box);
  }
  g.cmd->barriers(RenderBarrierStage::GRAPHICS, RenderTextureBarrier(r.texture.get(), RenderTextureLayout::SHADER_READ));
  g.bound_fb = nullptr;  // the source left COLOR_WRITE: bind it again
  bind_target();
  prof_mark(fmt::format("resolve {:08X}", dest));
  // Test 16: does anything read a 640x360 resolve's memory (the bloom)? Fill it white.
  if (REXCVAR_GET(gpu_native_query_test) == 16 && dw == 640 * g_scene_scale && dh == 360 * g_scene_scale)
    if (const uint32_t mem = g32(dest + 32) & 0xFFFFF000u; mem && readable_seh(mem, mem + 640 * 384 * 4 - 1))
      std::memset(gptr(mem), 0xFF, 640 * 384 * 4);
}

// Resolve, then the clears it asks for (D3DRESOLVE_CLEARRENDERTARGET 0x100,
// D3DRESOLVE_CLEARDEPTHSTENCIL 0x200) with its pClearColor (a D3DVECTOR4 of
// big-endian floats) and ClearZ: Perfect Dark clears its targets this way,
// not with Clear (GoldenEye also called Clear, so its port ignored these).
void native_resolve(uint32_t dest, uint32_t flags, uint32_t src_rect, uint32_t dest_point, uint32_t face,
                    uint32_t clear_color, double clear_z) {
  native_resolve_copy(dest, flags, src_rect, dest_point, face);
  if (!(flags & 0x300) || !ensure_device()) return;
  // No clear colour given: D3D clears with its constant vector at 0x82001830
  // (the race's glare buffer: without it, the depth words copied there stayed
  // in its alpha and the glare composite blended the whole buffer).
  if (!clear_color) clear_color = 0x82001830;
  if (!g.bound_scene) {  // a target without depth (OutRun's glare passes): cleared now, it is bound
    if ((flags & 0x100) && clear_color && g.recording) {
      float c[4];
      for (int i = 0; i < 4; ++i) { const uint32_t u = g32(clear_color + i * 4); std::memcpy(&c[i], &u, 4); }
      g.cmd->clearColor(0, RenderColor(c[0], c[1], c[2], c[3]));
      NTRACE("  resolve clear of target {:08X}: {} {} {} {}", g.guest_rt, c[0], c[1], c[2], c[3]);
    }
    return;
  }
  if ((flags & 0x100) && clear_color) {
    float c[4];
    for (int i = 0; i < 4; ++i) { const uint32_t u = g32(clear_color + i * 4); std::memcpy(&c[i], &u, 4); }
    g.pending_color = RenderColor(c[0], c[1], c[2], c[3]);
    g.pending_clear_color = true;
  }
  if (flags & 0x200) {
    g.pending_depth = float(clear_z);
    g.pending_clear_depth = true;
  }
  g.pending_src_key = g.scene_key;
  // (Not counted as new drawing: the game resolves each predicated tile to the
  // same texture with these clears, and a repeat resolve must not copy the
  // cleared target over the picture.)
  NTRACE("  resolve clears {:X} deferred, color {:08X} = {:08X} {:08X} {:08X} {:08X} z {}", flags & 0x300, clear_color,
         clear_color ? g32(clear_color) : 0, clear_color ? g32(clear_color + 4) : 0, clear_color ? g32(clear_color + 8) : 0,
         clear_color ? g32(clear_color + 12) : 0, clear_z);
}

// --- occlusion queries (the flares) -----------------------------------------
//
// Perfect Dark tests each flare with a point draw inside a query and reads the
// result at once (D3DQuery_GetData 0x82295310; the flare code 0x82267468 only
// asks whether any sample passed). Under Xenos that read waited for the GPU to
// finish the frame so far (HANDOVER 4be/4bg: the stutter on Intel hybrid PCs).
// Here each query is a real D3D12 occlusion query around the native draws
// (counted, not binary: OutRun's sun glare scales with the sample count), and
// GetData answers at once with the last result the GPU has finished for that
// query object (kFrames frames old): no wait.

// Vulkan: a pass ending with a query begun in it ends the query first (its
// count so far stands).
void end_query_before_pass_end(VulkanCommandList* l) {
  if (g_occ.open < 0 || !g_occ.vk_begun || l != g.cmd) return;
  vkCmdEndQuery(l->vk, g_occ.vk_pool, g_occ.vk_index);
  g_occ.vk_ended[g.slot].push_back({uint32_t(g_occ.open), int32_t(g_occ.vk_index)});
  g_occ.open = -1;
  g_occ.vk_begun = false;
}

bool ensure_occlusion() {
  if (g_occ.heap || g_occ.vk_pool) return true;
  if (g_occ.failed || !g.device) return false;
  g_occ.failed = true;  // until it is up
  if (g_vulkan) {
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_OCCLUSION;
    qi.queryCount = kMaxQueries * kFrames;
    if (vkCreateQueryPool(static_cast<VulkanDevice*>(g.device.get())->vk, &qi, nullptr, &g_occ.vk_pool) != VK_SUCCESS) {
      REXKRNL_WARN("ORNATIVE occlusion queries could not be set up: flares stay visible");
      return false;
    }
    for (uint32_t i = 0; i < kFrames; ++i)
      static_cast<VulkanCommandList*>(g.cmds[i].get())->beforeEndRenderPass = end_query_before_pass_end;
    g_occ.failed = false;  // (used from the next frame: its range is reset when the frame starts)
    return true;
  }
  ID3D12Device* dev = static_cast<D3D12Device*>(g.device.get())->d3d;
  D3D12_QUERY_HEAP_DESC qd{};
  qd.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
  qd.Count = kMaxQueries * kFrames;
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC bd{};
  bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bd.Width = uint64_t(kMaxQueries) * kFrames * 8;
  bd.Height = 1;
  bd.DepthOrArraySize = 1;
  bd.MipLevels = 1;
  bd.SampleDesc.Count = 1;
  bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  void* mapped = nullptr;
  if (FAILED(dev->CreateQueryHeap(&qd, IID_PPV_ARGS(&g_occ.heap))) ||
      FAILED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                          IID_PPV_ARGS(&g_occ.readback))) ||
      FAILED(g_occ.readback->Map(0, nullptr, &mapped))) {
    REXKRNL_WARN("ORNATIVE occlusion queries could not be set up: flares stay visible");
    return false;
  }
  g_occ.mapped = static_cast<const uint64_t*>(mapped);
  g_occ.failed = false;
  return true;
}

uint32_t query_slot(uint32_t object) {
  const auto it = g_occ.slot_of.find(object);
  if (it != g_occ.slot_of.end()) return it->second;
  uint32_t slot;
  if (g_occ.owner.size() < kMaxQueries) {
    slot = uint32_t(g_occ.owner.size());
    g_occ.owner.push_back(object);
    g_occ.samples.push_back(UINT32_MAX);  // no result yet: GetData says "not ready"
    g_occ.factor.push_back(1.0f);
  } else {
    // All taken (the game makes new query objects as it goes: 360 in 80 s of
    // the attract scenes): hand the oldest slot on. A result still in flight
    // for it lands on the new owner once, which only delays its flare a frame.
    slot = g_occ.next_reuse;
    g_occ.next_reuse = (g_occ.next_reuse + 1) % kMaxQueries;
    if (g_occ.open == int32_t(slot)) return UINT32_MAX;
    g_occ.slot_of.erase(g_occ.owner[slot]);
    g_occ.owner[slot] = object;
    g_occ.samples[slot] = UINT32_MAX;
  }
  g_occ.slot_of[object] = slot;
  return slot;
}

// Ends a query left open when the list is closed (frame end, mid-frame flush):
// a D3D12 query must begin and end in one command list.
void close_open_query() {
  if (g_occ.open < 0) return;
  if (g_vulkan) {  // (a begun query ended with its pass; one not begun had no draws)
    if (g_occ.vk_begun) end_query_before_pass_end(static_cast<VulkanCommandList*>(g.cmd));
    g_occ.open = -1;
    return;
  }
  const UINT index = g.slot * kMaxQueries + uint32_t(g_occ.open);
  static_cast<D3D12CommandList*>(g.cmd)->d3d->EndQuery(g_occ.heap, D3D12_QUERY_TYPE_OCCLUSION, index);
  g_occ.open = -1;  // (not resolved: its old result stays)
  static uint32_t logged = 0;
  if (logged++ < 4) REXKRNL_INFO("ORNATIVE occlusion query cut short by a list submission (frame {})", g.frames + 1);
}

void query_begin(uint32_t object) {
  if (!ensure_device() || !ensure_occlusion()) return;
  begin_frame();
  close_open_query();
  const uint32_t q = query_slot(object);
  if (q == UINT32_MAX) return;
  if (g_vulkan) {
    if (!g_occ.vk_reset[g.slot] || g_occ.vk_next[g.slot] >= kMaxQueries) return;
    g_occ.vk_index = g.slot * kMaxQueries + g_occ.vk_next[g.slot]++;
    g_occ.vk_begun = false;  // (begins at the next draw, inside its render pass)
  } else {
    static_cast<D3D12CommandList*>(g.cmd)->d3d->BeginQuery(g_occ.heap, D3D12_QUERY_TYPE_OCCLUSION,
                                                           g.slot * kMaxQueries + q);
  }
  g_occ.open = int32_t(q);
  // Host samples -> the 360's: the guest surface's own sample count
  // (RB_SURFACE_INFO bits 16-17 at +24: the race scene is 4x, the target the
  // sun is tested in 1x) at 1280x720 guest pixels.
  const float host = (g.bound_scene ? float(uint32_t(g.scene_msaa.sampleCount)) : 1.0f) * float(g_scene_scale * g_scene_scale);
  const uint32_t surface = g.guest_rt ? g.guest_rt : g.guest_depth;
  const float guest = surface ? float(1u << ((g32(surface + 24) >> 16) & 3)) : 1.0f;
  g_occ.factor[q] = guest / host;
  ++g_occ.begins;
  NTRACE("query begin {:08X} slot {} (scene {}, factor {})", object, q, g.bound_scene, g_occ.factor[q]);
}

void query_end(uint32_t object) {
  const auto it = g_occ.slot_of.find(object);
  if (g_occ.open < 0 || it == g_occ.slot_of.end() || int32_t(it->second) != g_occ.open || !g.recording) return;
  if (g_vulkan) {
    if (g_occ.vk_begun) {
      end_query_before_pass_end(static_cast<VulkanCommandList*>(g.cmd));
    } else {
      g_occ.vk_ended[g.slot].push_back({it->second, -1});  // nothing drawn: 0 samples
      g_occ.open = -1;
    }
    NTRACE("query end {:08X} slot {}", object, it->second);
    return;
  }
  const UINT index = g.slot * kMaxQueries + it->second;
  ID3D12GraphicsCommandList* l = static_cast<D3D12CommandList*>(g.cmd)->d3d;
  l->EndQuery(g_occ.heap, D3D12_QUERY_TYPE_OCCLUSION, index);
  l->ResolveQueryData(g_occ.heap, D3D12_QUERY_TYPE_OCCLUSION, index, 1, g_occ.readback, uint64_t(index) * 8);
  g_occ.ended[g.slot].push_back(it->second);
  g_occ.open = -1;
  NTRACE("query end {:08X} slot {}", object, it->second);
}

// Vulkan: the query begun last starts here, just before its first draw (the
// draw's render pass is then active).
void begin_vulkan_query() {
  if (!g_vulkan || g_occ.open < 0 || g_occ.vk_begun) return;
  auto* l = static_cast<VulkanCommandList*>(g.cmd);
  l->checkActiveRenderPass();
  vkCmdBeginQuery(l->vk, g_occ.vk_pool, g_occ.vk_index, 0);
  g_occ.vk_begun = true;
}

// Vulkan: at the start of a frame's list, the frame slot's query range.
void reset_vulkan_queries() {
  if (!g_vulkan || !g_occ.vk_pool) return;
  vkCmdResetQueryPool(static_cast<VulkanCommandList*>(g.cmd)->vk, g_occ.vk_pool, g.slot * kMaxQueries, kMaxQueries);
  g_occ.vk_next[g.slot] = 0;
  g_occ.vk_reset[g.slot] = true;
}

// After waiting for frame slot s's fence: its queries' results are final.
void harvest_queries(uint32_t s) {
  if (g_vulkan) {
    for (const auto& [q, index] : g_occ.vk_ended[s]) {
      uint64_t count = 0;
      if (index >= 0)
        vkGetQueryPoolResults(static_cast<VulkanDevice*>(g.device.get())->vk, g_occ.vk_pool, uint32_t(index), 1,
                              sizeof(count), &count, sizeof(count), VK_QUERY_RESULT_64_BIT);
      g_occ.samples[q] = uint32_t(std::min<double>(double(count) * g_occ.factor[q] + 0.5, double(UINT32_MAX - 1)));
    }
    g_occ.vk_ended[s].clear();
    return;
  }
  if (!g_occ.mapped) return;
  for (uint32_t q : g_occ.ended[s]) {
    g_occ.samples[q] = uint32_t(std::min<double>(double(g_occ.mapped[s * kMaxQueries + q]) * g_occ.factor[q] + 0.5,
                                                 double(UINT32_MAX - 1)));
    static uint32_t logged = 0;
    if (logged < 40 && (logged++ % 4) == 0)
      REXKRNL_DEBUG("ORNATIVE occlusion result: query slot {} = {} host samples (frame {})", q,
                   g_occ.mapped[s * kMaxQueries + q], g.frames);
  }
  g_occ.ended[s].clear();
}

}  // namespace

// gpu_native as it was at the first D3D call (fixed for the run), and off for
// good once the native device failed to start. (GoldenEye's frame-limiter and
// gamma-ramp hooks have no counterpart: Perfect Dark's game code never calls
// SetGammaRamp, and its pacing comes from the guest vblank.)
bool NativeOn() {
  static const bool on = REXCVAR_GET(gpu_native);
  return on && !g.failed;
}
#define NATIVE_ON() NativeOn()
namespace outrun {
bool NativeRendererActive() { return NativeOn(); }  // (the title screen's build label)
double NativeFps() { return g_fps.load(); }          // (build_label's Show FPS counter)
// Player mark (player_log.cpp): trace the next frame into `path`. False with the classic renderer.
bool RequestMarkTrace(const std::string& path) {
  if (!NativeOn()) return false;
  std::lock_guard lock(g_mark_trace_mutex);
  g_mark_trace_request = path;
  return true;
}
}

// D3DDevice_CreateVertexShader / CreatePixelShader(pFunction) -> shader object.
extern "C" REX_FUNC(sub_8228C558) {
  const uint32_t blob = ctx.r3.u32;
  __imp__sub_8228C558(ctx, base);
  if (NATIVE_ON() && ctx.r3.u32) {
    g.created_blobs[ctx.r3.u32] = blob;
    g.shaders.erase(ctx.r3.u32);
    request_translation(ctx.r3.u32, true);
  }
}
extern "C" REX_FUNC(sub_8228C370) {
  const uint32_t blob = ctx.r3.u32;
  __imp__sub_8228C370(ctx, base);
  if (!NATIVE_ON() && ctx.r3.u32 && REXCVAR_GET(gpu_native_query_test) == 12) g.created_blobs[ctx.r3.u32] = blob;
  if (NATIVE_ON() && ctx.r3.u32) {
    g.created_blobs[ctx.r3.u32] = blob;
    g.shaders.erase(ctx.r3.u32);
    request_translation(ctx.r3.u32, false);
  }
}
// In-place shader registration: forget any host shader cached for that object,
// and watch it for translating ahead (warm_registered_shaders).
extern "C" REX_FUNC(sub_8228C460) {
  const uint32_t object = ctx.r3.u32;
  if (NATIVE_ON()) { g.shaders.erase(object); g.created_blobs.erase(object); }
  __imp__sub_8228C460(ctx, base);
  if (NATIVE_ON() && object) g.warm[object] = {true, 0, g.frames};
}
extern "C" REX_FUNC(sub_8228A920) {
  const uint32_t object = ctx.r3.u32;
  if (NATIVE_ON()) { g.shaders.erase(object); g.created_blobs.erase(object); }
  __imp__sub_8228A920(ctx, base);
  if (NATIVE_ON() && object) g.warm[object] = {false, 0, g.frames};
}

// D3DDevice_SetStreamSource(pDevice, Stream, pVB, Offset, Stride).
extern "C" REX_FUNC(sub_82284A00) {
  if (NATIVE_ON() && ctx.r4.u32 < 16) g_streams[ctx.r4.u32] = {ctx.r5.u32, ctx.r6.u32, ctx.r7.u32};
  __imp__sub_82284A00(ctx, base);
}

// D3DDevice_SetTexture(pDevice, Sampler, pTexture).
extern "C" REX_FUNC(sub_822889A0) {
  if (NATIVE_ON() && ctx.r4.u32 < 32) g_textures[ctx.r4.u32] = ctx.r5.u32;
  __imp__sub_822889A0(ctx, base);
}

// D3DDevice_DrawIndexedVertices(pDevice, PrimType, BaseVertexIndex, StartIndex, IndexCount).
bool g_xenos_test_in_query = false;  // gpu_native_query_test 10 (Xenos mode): skip the draws inside queries
// gpu_native_query_test 12 / 13 (Xenos mode): skip draws whose pixel shader is gpu_native_skip_ps (12), or
// draw them with colour writes off (13: D3D still flushes their state), to compare the two renderers
// without one effect.
// (15: as 13, but only after the bloom blur, PS A6B50A16FB18163A, has drawn in the frame.)
bool g_xenos_test_after_blur = false;
uint64_t xenos_ps_hash(uint32_t dev) {
  static std::unordered_map<uint32_t, uint64_t> hashes;
  const uint32_t ps = g32(dev + kDevPixelShader);
  if (!ps) return 0;
  auto it = hashes.find(ps);
  if (it == hashes.end()) {
    std::vector<uint8_t> blob;
    it = hashes.emplace(ps, container_bytes(ps, false, blob) ? fnv1a(blob.data(), blob.size()) : 0).first;
  }
  return it->second;
}
bool xenos_test_match(uint32_t dev) {
  const int32_t t = REXCVAR_GET(gpu_native_query_test);
  if (NATIVE_ON() || (t != 12 && t != 13 && t != 15)) return false;
  static const std::string skip = REXCVAR_GET(gpu_native_skip_ps);
  const uint64_t hash = xenos_ps_hash(dev);
  if (hash == 0xA6B50A16FB18163Aull) g_xenos_test_after_blur = true;
  if (!hash || skip.empty() || (t == 15 && !g_xenos_test_after_blur)) return false;
  return fmt::format("{:016X}", hash) == skip;
}
bool xenos_test_skip(uint32_t dev) { return REXCVAR_GET(gpu_native_query_test) == 12 && xenos_test_match(dev); }
// Colour writes off around one guest draw (test 13): D3DDevice_SetRenderState_ColorWriteEnable 0x822832B8.
struct XenosTestMask {
  PPCContext& ctx;
  uint8_t* base;
  uint32_t dev = 0, saved = 0;
  bool on = false;
  XenosTestMask(PPCContext& c, uint8_t* b) : ctx(c), base(b) {
    const int32_t t = REXCVAR_GET(gpu_native_query_test);
    if ((t != 13 && t != 15) || !xenos_test_match(c.r3.u32)) return;
    on = true;
    dev = c.r3.u32;
    saved = g32(dev + 11868);
    set(0);
  }
  ~XenosTestMask() { if (on) set(saved); }
  void set(uint32_t mask) {
    PPCContext keep = ctx;
    ctx.r3.u64 = dev;
    ctx.r4.u64 = mask;
    sub_822832B8(ctx, base);
    ctx = keep;
  }
};
extern "C" REX_FUNC(sub_82286760) {
  if (xenos_test_skip(ctx.r3.u32)) return;
  XenosTestMask test_mask(ctx, base);
  if (!NATIVE_ON() && g_xenos_test_in_query && REXCVAR_GET(gpu_native_query_test) == 10) return;
  if (!NATIVE_ON() && g_xenos_test_in_query && REXCVAR_GET(gpu_native_query_test) == 11) {  // test: cull CCW instead
    PPCContext saved = ctx;
    ctx.r4.u64 = 6;  // D3DCULL_CCW
    sub_822825F0(ctx, base);
    ctx = saved;
  }
  if (NATIVE_ON()) {
    const uint32_t dev = ctx.r3.u32, prim = ctx.r4.u32, count = ctx.r7.u32, start = ctx.r6.u32;
    const int32_t base_vertex = ctx.r5.s32;
    const uint32_t ibo = g32(dev + 12444);
    if (ibo && count) {
      const bool wide = (g32(ibo) & 0x80000000u) != 0;
      // The index data's own address, aligned to an index only: OutRun packs
      // 16-bit lists back to back (the sun's occlusion disc started at ...F86,
      // and reading from ...F84 shifted the strip by one index, flipping every
      // triangle's winding so the whole disc was culled).
      const uint32_t addr = gpu_view(g32(ibo + 24) & (wide ? ~3u : ~1u));  // 0xE0000000+ buffers: data is 4 KB on
      native_draw(dev, prim, count, nullptr, base_vertex, 0, 0, 0, !wide, addr + start * (wide ? 4 : 2), wide);
    }
  }
  __imp__sub_82286760(ctx, base);
}

// D3DDevice_DrawVertices(pDevice, PrimType, StartVertex, VertexCount): the
// auto-indexed draw, which OutRun's title and menus use (Perfect Dark's game
// code never called it).
extern "C" REX_FUNC(sub_82286370) {
  if (xenos_test_skip(ctx.r3.u32)) return;
  XenosTestMask test_mask(ctx, base);
  if (NATIVE_ON()) native_draw(ctx.r3.u32, ctx.r4.u32, ctx.r6.u32, nullptr, 0, ctx.r5.u32, 0, 0);
  __imp__sub_82286370(ctx, base);
}

// D3DDevice_DrawVerticesUP(pDevice, PrimType, VertexCount, pData, Stride). It
// draws through BeginVertices and ends inline (OutRun's game code calls
// neither BeginVertices nor EndVertices itself).
extern "C" REX_FUNC(sub_82285D70) {
  if (xenos_test_skip(ctx.r3.u32)) return;
  XenosTestMask test_mask(ctx, base);
  if (NATIVE_ON()) native_draw(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, nullptr, 0, 0, ctx.r6.u32, ctx.r7.u32);
  __imp__sub_82285D70(ctx, base);
}

// D3DDevice_DrawIndexedVerticesUP(pDevice, PrimType, MinVertexIndex, NumVertices,
// IndexCount, pIndexData, IndexDataFormat, pVertexStreamZeroData,
// VertexStreamZeroStride): read from its code (0x822862D0, which copies the
// vertices and indices into the space 0x82285DB8 reserves; the stride is the
// ninth argument, on the stack at r1+84). IndexDataFormat bit 2 = 32-bit
// indices. Perfect Dark's quads (6 indices each) all come this way.
extern "C" REX_FUNC(sub_822862D0) {
  if (xenos_test_skip(ctx.r3.u32)) return;
  XenosTestMask test_mask(ctx, base);
  if (NATIVE_ON() && ctx.r7.u32 && ctx.r8.u32 && ctx.r10.u32) {
    const uint32_t stride = g32(ctx.r1.u32 + 84);
    native_draw(ctx.r3.u32, ctx.r4.u32, ctx.r7.u32, nullptr, 0, 0, ctx.r10.u32, stride, true, ctx.r8.u32,
                (ctx.r9.u32 & 4) != 0);
  }
  __imp__sub_822862D0(ctx, base);
}

// D3DDevice_Clear(pDevice, Count, pRects, Flags, Color, Z, Stencil).
extern "C" REX_FUNC(sub_82298178) {
  if (NATIVE_ON() && ensure_device()) {
    begin_frame();
    bind_target();
    if (g.bound_scene) apply_pending_clears();
    g.draws_since_resolve = true;
    const uint32_t flags = ctx.r6.u32, c = ctx.r7.u32;
    if (flags & 1) {
      g_clear_color.store(c);
      g.cmd->clearColor(0, RenderColor(((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f,
                                       (c & 0xFF) / 255.0f, ((c >> 24) & 0xFF) / 255.0f));
    }
    if ((flags & 0x30) && g.bound_scene) {
      g.restore_rt = 0;  // a depth rebuild waiting for this surface is overwritten
      if (g.guest_depth) g.edram_color.erase(g32(g.guest_depth + 28) & 0xFFF);
      depth_will_change();
      g.cmd->clearDepthStencil(true, true, float(ctx.f1.f64), ctx.r9.u32 & 0xFF);
      g.depth_clear = float(ctx.f1.f64);
      g.depth_written = false;
      ++g.depth_gen;
    }
    prof_mark(fmt::format("clear {:X}", flags));
    NTRACE("clear flags {:X} color {:08X} z {}", flags, c, ctx.f1.f64);
  }
  __imp__sub_82298178(ctx, base);
}

// D3DDevice_Resolve(pDevice, Flags, pSourceRect, pDestTexture, pDestPoint, DestLevel,
// DestSliceOrFace, pClearColor, ClearZ (f1), ClearStencil, pParameters): copy the scene.
extern "C" REX_FUNC(sub_822989A0) {
  // Test 17 / 18 (Xenos mode): leave out the resolves into a 640x360 texture (the bloom): all of them (17),
  // or all but the fifth of the frame (18: the one after the last blur pass).
  if (!NATIVE_ON() && ctx.r6.u32 && (g32(ctx.r6.u32 + 36) & 0x03FFFFFF) == ((359u << 13) | 639u)) {
    static uint64_t frame = 0;
    static int n = 0;
    if (frame != g_xenos_test_frame) { frame = g_xenos_test_frame; n = 0; }
    ++n;
    if (REXCVAR_GET(gpu_native_query_test) == 17 || (REXCVAR_GET(gpu_native_query_test) == 18 && n != 5)) return;
  }
  if (NATIVE_ON()) {
    NTRACE("resolve flags {:X} dest {:08X} level {} face {}", ctx.r4.u32, ctx.r6.u32, ctx.r8.u32, ctx.r9.u32);
    if (ctx.r8.u32 == 0) {
      native_resolve(ctx.r6.u32, ctx.r4.u32, ctx.r5.u32, ctx.r7.u32, ctx.r9.u32, ctx.r10.u32, ctx.f1.f64);
    } else {  // into a mip level: not kept yet (level 0 is never overwritten with it)
      static bool logged = false;
      if (!logged) {
        logged = true;
        REXKRNL_WARN("ORNATIVE resolve into level {} of {:08X} ignored", ctx.r8.u32, ctx.r6.u32);
      }
    }
  }
  __imp__sub_822989A0(ctx, base);
}

// D3DDevice_SetRenderTarget / SetDepthStencilSurface: traced only (one scene target for now).
extern "C" REX_FUNC(sub_822852A8) {
  if (NATIVE_ON() && tracing()) {
    std::string w;
    for (uint32_t i = 0; ctx.r5.u32 && i < 16; ++i) w += fmt::format(" {:08X}", g32(ctx.r5.u32 + i * 4));
    NTRACE("set rt{} {:08X}:{}", ctx.r4.u32, ctx.r5.u32, w);
  }
  const uint32_t dev = ctx.r3.u32;
  if (NATIVE_ON() && ctx.r4.u32 == 0) g.guest_rt = ctx.r5.u32;
  __imp__sub_822852A8(ctx, base);
  if (NATIVE_ON()) NTRACE("  after set rt: dev+10936 {:08X} {:08X} {:08X}", g32(dev + 10936), g32(dev + 10940), g32(dev + 10944));
}
extern "C" REX_FUNC(sub_822855F8) {
  if (NATIVE_ON()) {
    if (tracing()) {
      std::string w;
      for (uint32_t i = 0; ctx.r4.u32 && i < 12; ++i) w += fmt::format(" {:08X}", g32(ctx.r4.u32 + i * 4));
      NTRACE("set depth {:08X}:{}", ctx.r4.u32, w);
    }
    g.guest_depth = ctx.r4.u32;
  }
  __imp__sub_822855F8(ctx, base);
}

// D3DDevice_Swap(pDevice, pFrontBuffer, pParameters).
// Frame-time log for comparing the renderers: guest Swap-to-Swap intervals and
// the time spent inside Swap (guest D3D + native present) on the game thread.
void frame_stats(double interval_ms, double swap_ms) {
  // Show FPS (the SDK's counter counts Xenos swaps, which hung mode skips):
  // guest frames per second over half-second windows.
  {
    static double window_ms = 0;
    static int window_frames = 0;
    if (interval_ms > 0 && interval_ms < 1000) {
      window_ms += interval_ms;
      if (++window_frames >= 30 || window_ms >= 500) {
        g_fps = 1000.0 * window_frames / window_ms;
        window_ms = 0;
        window_frames = 0;
      }
    }
  }
  static std::vector<double> intervals;
  static double swap_total = 0, swap_max = 0;
  const int32_t every = REXCVAR_GET(gpu_native_frame_stats);
  if (every <= 0) return;
  if (interval_ms > 0) intervals.push_back(interval_ms);
  if (interval_ms > double(REXCVAR_GET(gpu_native_long_frame_ms)))
    REXKRNL_INFO("ORNATIVE long frame {:.1f} ms (swap {:.1f}; present: acquire {:.1f} submit {:.1f} present {:.1f} "
                 "fence {:.1f}): {} pipelines {:.1f} ms, {} textures {:.1f} ms; draws: "
                 "parse {:.1f} indices {:.1f} decode {:.1f} total {:.1f} ms; vertex cache {} hits {} misses; texture decode {:.1f} "
                 "mips {:.1f} ms; vertex decode {:.1f} ms; {} mid-frame flushes {:.1f} ms; pool alloc {:.1f} ms; vertex hash {:.1f} ms",
                 interval_ms,
                 swap_ms, g.present_ms[0], g.present_ms[1], g.present_ms[2], g.present_ms[3], g.last_pipe_n, g.last_pipe_ms, g.last_tex_n, g.last_tex_ms, g.last_sect[0], g.last_sect[1],
                 g.last_sect[2], g.last_sect[4], g.last_vcache_hits, g.last_vcache_misses, g.last_tex_decode_ms,
                 g.last_tex_mip_ms, g.last_vdecode_ms, g.last_flushes, g.last_flush_ms, g.last_alloc_ms, g.last_hash_ms);
  swap_total += swap_ms;
  swap_max = std::max(swap_max, swap_ms);
  if (intervals.size() < size_t(every)) return;
  std::vector<double> v = intervals;
  std::sort(v.begin(), v.end());
  double sum = 0;
  for (double x : v) sum += x;
  const auto pct = [&](double q) { return v[std::min(v.size() - 1, size_t(q * double(v.size())))]; };
  REXKRNL_INFO("ORNATIVE frame_stats {} {} frames: avg {:.3f} ms p50 {:.3f} p99 {:.3f} max {:.3f} | swap avg {:.3f} "
               "max {:.3f} ms | thread {}", NATIVE_ON() ? "native" : "xenos", v.size(), sum / double(v.size()),
               pct(0.5), pct(0.99), v.back(), swap_total / double(v.size()), swap_max, GetCurrentThreadId());
  // Spread of the intervals: <15.7, 15.7-16.2, 16.2-16.7, 16.7-17.2, 17.2-17.7, 17.7-19, 19-25, >25 ms.
  int hist[8] = {};
  for (double x : v) {
    const double edges[] = {15.7, 16.2, 16.7, 17.2, 17.7, 19.0, 25.0};
    int b = 0;
    while (b < 7 && x >= edges[b]) ++b;
    ++hist[b];
  }
  REXKRNL_INFO("ORNATIVE frame_hist {} {} {} {} {} {} {} {}", hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6],
               hist[7]);
  if (!g.present_intervals.empty()) {
    int phist[8] = {};
    double pmax = 0;
    for (double x : g.present_intervals) {
      const double edges[] = {15.7, 16.2, 16.7, 17.2, 17.7, 19.0, 25.0};
      int b = 0;
      while (b < 7 && x >= edges[b]) ++b;
      ++phist[b];
      pmax = std::max(pmax, x);
    }
    double psum = 0, psq = 0;
    for (double x : g.present_intervals) { psum += x; psq += x * x; }
    const double pmean = psum / g.present_intervals.size();
    REXKRNL_INFO("ORNATIVE present_hist {} {} {} {} {} {} {} {} | interval mean {:.3f} sd {:.3f} max {:.2f} ms | pacing "
                 "wait avg {:.2f} ms, period {:.3f} ms | {} double steps", phist[0], phist[1], phist[2], phist[3], phist[4],
                 phist[5], phist[6], phist[7], pmean, std::sqrt(std::max(0.0, psq / g.present_intervals.size() - pmean * pmean)),
                 pmax, g.pace_wait_sum / g.present_intervals.size(), g.pace_period, g.double_steps);
    g.present_intervals.clear();
    g.pace_wait_sum = 0;
    g.double_steps = 0;
  }
  intervals.clear();
  swap_total = swap_max = 0;
}

// D3DQuery_Issue(pQuery, dwIssueFlags): D3DISSUE_BEGIN 2, D3DISSUE_END 1.
extern "C" REX_FUNC(sub_82294670) {
  if (NATIVE_ON() && g.out.presenter) {
    if (ctx.r4.u32 & 2) query_begin(ctx.r3.u32);
    if (ctx.r4.u32 & 1) query_end(ctx.r3.u32);
  }
  if (!NATIVE_ON()) g_xenos_test_in_query = (ctx.r4.u32 & 2) != 0;
  __imp__sub_82294670(ctx, base);
}

// D3DQuery_GetData(pQuery, pData, dwSize, dwGetDataFlags): answered at once,
// never waiting for the GPU: S_OK with the last finished result (samples that
// passed, as the 360 counts them; 0 for a query never issued), or S_FALSE
// while an issued query's first result is still on its way (OutRun's sun then
// keeps its last value; it reads the query it did not issue this frame and
// only moves on to the other after an S_OK). The guest's own GetData is not
// run in native mode.
extern "C" REX_FUNC(sub_82295310) {
  if (NATIVE_ON() && g.out.presenter && (g_occ.heap || g_occ.vk_pool)) {
    const auto it = g_occ.slot_of.find(ctx.r3.u32);
    uint32_t samples = it == g_occ.slot_of.end() ? 0u : g_occ.samples[it->second];
    if (REXCVAR_GET(gpu_native_query_test) == 1) samples = 1280 * 720 * 4;  // test: everything visible
    if (samples != UINT32_MAX && ctx.r4.u32 && ctx.r5.u32 >= 4) {
      const uint32_t be = __builtin_bswap32(samples);
      std::memcpy(gptr(ctx.r4.u32), &be, 4);
    }
    ++g_occ.reads;
    NTRACE("query read {:08X}: {}", ctx.r3.u32, samples == UINT32_MAX ? std::string("not ready") : std::to_string(samples));
    ctx.r3.u64 = samples == UINT32_MAX ? 1 : 0;  // S_FALSE / S_OK
    return;
  }
  const uint32_t data = ctx.r4.u32;
  __imp__sub_82295310(ctx, base);
  if (!NATIVE_ON() && REXCVAR_GET(gpu_native_query_test) >= 9 && data) {  // test: the Xenos path's results
    static uint32_t n = 0;
    if ((n++ % 15) == 0 && n < 600)
      REXKRNL_INFO("ORNATIVE xenos query read: hr {} samples {}", ctx.r3.u32, g32(data));
  }
}

// The guest's GPU interrupt callback (D3D_GraphicsInterruptCallback, the
// address every log prints in SetInterruptCallback): the SDK's vsync worker
// calls it with source 0 on every guest vblank. Counted here, so Swap can wait
// for the vblank as the guest's presentation interval did through the GPU.
std::atomic<uint64_t> g_vblanks{0};
std::mutex g_vblank_mutex;
std::condition_variable g_vblank_cv;
extern "C" REX_FUNC(sub_8229A348) {
  if (ctx.r3.u32 == 0) {
    { std::lock_guard<std::mutex> lock(g_vblank_mutex); g_vblanks.fetch_add(1, std::memory_order_release); }
    g_vblank_cv.notify_all();
  }
  __imp__sub_8229A348(ctx, base);
}

// In hung mode the guest's Swap no longer waits for the GPU, which is what held
// the game to the display rate (the first run swapped ~800 times a second): wait
// for a guest vblank after the last Swap, as presentation interval 1 did. (The
// game's simulation runs on vblanks either way.) A missing vblank never holds
// the game for more than 50 ms.
void wait_for_vblank() {
  static uint64_t last = 0;
  std::unique_lock<std::mutex> lock(g_vblank_mutex);
  g_vblank_cv.wait_for(lock, std::chrono::milliseconds(50),
                       [] { return g_vblanks.load(std::memory_order_acquire) > last; });
  last = g_vblanks.load(std::memory_order_acquire);
}

extern "C" REX_FUNC(sub_82293A10) {
  static LARGE_INTEGER freq{}, last{};
  if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
  LARGE_INTEGER t0, t1;
  QueryPerformanceCounter(&t0);
  // Once the native device is up, the game's own "GPU hung" mode goes on
  // (device+10941 bit 2, as D3D_GpuHangBreak 0x822A7C50 sets it: fences are
  // faked on the CPU, D3D_CBlocker_Check 0x822999B8 returns at once and the
  // ring is no longer kicked), so the Xenos command processor goes idle and the
  // native renderer takes the frame at the D3D calls. Before that (or if the
  // device never starts) Xenos draws as usual.
  if (NATIVE_ON() && ctx.r3.u32 && ensure_device() && !(*gptr(ctx.r3.u32 + 10941) & 0x02)) {
    *gptr(ctx.r3.u32 + 10941) |= 0x02;
    REXKRNL_INFO("ORNATIVE native renderer on: guest D3D in its GPU-hung mode, Xenos idle from frame {}", g.frames + 1);
  }
  if (NATIVE_ON() && g.out.presenter) wait_for_vblank();
  if (NATIVE_ON()) { g.front_buffer = ctx.r4.u32; NTRACE("swap front {:08X}", ctx.r4.u32); }
  g_xenos_test_after_blur = false;
  ++g_xenos_test_frame;
  test_timer();
  outrun::player_log::OnSwap();
  outrun::native_shaders::FlushTranslationSummary();
  __imp__sub_82293A10(ctx, base);
  if (NATIVE_ON()) present_frame();
  QueryPerformanceCounter(&t1);
  const auto ms = [&](int64_t ticks) { return double(ticks) * 1000.0 / double(freq.QuadPart); };
  frame_stats(last.QuadPart ? ms(t0.QuadPart - last.QuadPart) : 0.0, ms(t1.QuadPart - t0.QuadPart));
  last = t0;
}