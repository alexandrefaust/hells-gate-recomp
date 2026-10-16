// renderer=dante pipeline storage: every guest shader and pipeline the game
// draws with, appended to a file so they can be built before they are needed
// (no compile hitches in play). Records are in guest terms: shader microcode,
// translator modifications, and render state with the GPU- and
// resolution-dependent parts (host depth format, scaled depth bias slope)
// replaced by their guest sources, so a file recorded on one GPU at one
// resolution serves every GPU and resolution.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <thread>
#include <unordered_set>
#include <vector>

namespace renderer {

// Host pipeline state of a draw (draw_renderer.cpp).
struct PipelineKey {
  uint64_t vertex_key;
  uint64_t pixel_key;
  uint32_t topology;
  uint32_t color_format[4];
  uint32_t color_count;
  uint32_t depth_format;
  uint32_t color_mask;
  uint32_t depth_enable;
  uint32_t depth_write;
  uint32_t depth_func;
  uint32_t blend[4];          // RB_BLENDCONTROL0-3 & 0x1FFF1FFF, 0 = disabled
  uint32_t stencil_control;   // RB_DEPTHCONTROL stencil bits, 0 = disabled
  uint32_t stencil_masks;     // read mask | write mask << 8
  uint32_t cull;              // bit 0 cull front, bit 1 cull back, bit 2 front face clockwise
  int32_t depth_bias;         // constant depth bias (host units)
  uint32_t depth_bias_slope;  // slope-scaled depth bias (float bits)
  bool operator==(const PipelineKey&) const = default;
};

// Drawn into a shadow map (ShadowMapScale), not a frame-scale target.
inline constexpr uint32_t kStoredPipelineShadowMap = 1;

// Bump when PipelineKey or how draw_renderer.cpp derives it changes; older
// records are then derived again from their GuestDrawState.
inline constexpr uint32_t kPipelineKeyVersion = 1;

// A pipeline as derived when it was recorded. In `key`: vertex_key and
// pixel_key are 0; depth_format is the guest depth format + 1 (0 without
// depth); depth_bias_slope is before the render target's resolution scale.
// Valid while key_version and modification_version are current.
struct StoredPipeline {
  uint64_t vertex_hash = 0;
  uint64_t vertex_modification = 0;
  uint64_t pixel_hash = 0;  // 0: no pixel shader
  uint64_t pixel_modification = 0;
  PipelineKey key{};
  uint32_t flags = 0;  // kStoredPipelineShadowMap
  uint32_t key_version = kPipelineKeyVersion;
  uint32_t modification_version = 0;  // SpirvShaderTranslator::Modification::kVersion
  uint32_t reserved = 0;
};

// Context registers of the draw state (the kShadowRanges before the constants).
inline constexpr uint32_t kStoredContextRanges = 8;
inline constexpr uint32_t kStoredContextRegisters = 124;

// The guest state a pipeline was derived from: enough to derive it again.
struct GuestDrawState {
  uint32_t registers[kStoredContextRegisters];  // kShadowRanges[0..7] order
  uint32_t draw_initiator;        // VGT_DRAW_INITIATOR without the index count
  uint32_t color_formats[4];      // guest formats of the bound render targets
  uint32_t color_count;
  uint32_t depth_format;          // guest format + 1, 0 without depth
  uint32_t flags;                 // kStoredPipelineShadowMap
};

struct StoredDraw {
  StoredPipeline pipeline;
  GuestDrawState state;
};

struct StoredShader {
  uint32_t type = 0;  // rex::graphics::xenos::ShaderType
  uint64_t hash = 0;  // XXH3 of the microcode
  std::vector<uint32_t> ucode;  // guest (big-endian) dwords
};

// Runs fn(index, thread) for index 0..count-1 on `threads` threads.
inline void RunParallel(size_t count, uint32_t threads,
                        const std::function<void(size_t index, uint32_t thread)>& fn) {
  std::atomic<size_t> next{0};
  auto worker = [&](uint32_t thread) {
    for (size_t i; (i = next.fetch_add(1)) < count;) fn(i, thread);
  };
  std::vector<std::thread> pool;
  for (uint32_t t = 1; t < threads; ++t) pool.emplace_back(worker, t);
  worker(0);
  for (auto& thread : pool) thread.join();
}

class PipelineStorage {
 public:
  // Reads `shipped` (the list installed with the game, read-only, may be
  // missing), then `path`, and appends records neither has to `path`. A
  // damaged or outdated `path` is started over (an outdated one is kept as .old).
  PipelineStorage(std::filesystem::path path, const std::filesystem::path& shipped);
  ~PipelineStorage();

  PipelineStorage(const PipelineStorage&) = delete;
  PipelineStorage& operator=(const PipelineStorage&) = delete;

  const std::filesystem::path& path() const { return path_; }
  const std::vector<StoredShader>& shaders() const { return shaders_; }
  const std::vector<StoredDraw>& pipelines() const { return pipelines_; }

  // Render thread. Records seen before are skipped.
  void AddShader(uint32_t type, uint64_t hash, const uint32_t* ucode, uint32_t dwords);
  void AddPipeline(const StoredDraw& draw);

  // Records added this session.
  uint32_t added_shaders() const { return added_shaders_; }
  uint32_t added_pipelines() const { return added_pipelines_; }

 private:
  // Returns the size of the valid prefix, 0 if the file is not usable.
  uint64_t Load(const std::filesystem::path& path);
  void Append(uint32_t tag, const void* a, size_t a_size, const void* b, size_t b_size);

  std::filesystem::path path_;
  std::FILE* file_ = nullptr;
  std::vector<StoredShader> shaders_;
  std::vector<StoredDraw> pipelines_;
  std::unordered_set<uint64_t> shader_hashes_;
  std::unordered_set<uint64_t> pipeline_hashes_;
  uint32_t added_shaders_ = 0;
  uint32_t added_pipelines_ = 0;
};

}  // namespace renderer
