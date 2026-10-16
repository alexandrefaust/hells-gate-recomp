// renderer=dante shader pipeline: Xenos ucode -> SPIR-V through the SDK's
// SpirvShaderTranslator -> DiligentCore shaders. Render thread only.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IShader;
}  // namespace Diligent

namespace rex::graphics {
class RegisterFile;
class SpirvShader;
}  // namespace rex::graphics

namespace renderer {

struct GuestShaderCode {
  const uint32_t* ucode = nullptr;  // host pointer to big-endian guest ucode
  uint32_t dwords = 0;
};

struct TranslatedShaders {
  Diligent::IShader* vertex = nullptr;
  Diligent::IShader* pixel = nullptr;  // null when the draw has no pixel shader
  // Analyzed guest shaders (constant maps, vertex/texture bindings).
  const rex::graphics::SpirvShader* vertex_info = nullptr;
  const rex::graphics::SpirvShader* pixel_info = nullptr;
  uint64_t vertex_key = 0;  // ucode hash ^ modification, for pipeline keys
  uint64_t pixel_key = 0;
  uint64_t vertex_hash = 0;  // ucode hashes and modifications, for PipelineStorage
  uint64_t vertex_modification = 0;
  uint64_t pixel_hash = 0;
  uint64_t pixel_modification = 0;
};

class PipelineStorage;
struct StoredShader;

// Pipeline key part of a shader: its ucode hash with the modification mixed in.
uint64_t ShaderKey(uint64_t ucode_hash, uint64_t modification);

// Shader variants to build ahead of use: ucode hash -> modifications.
using ShaderVariants = std::unordered_map<uint64_t, std::vector<uint64_t>>;

class ShaderCache {
 public:
  // Render targets are scale_x / scale_y times the guest size.
  ShaderCache(Diligent::IRenderDevice* device, float scale_x, float scale_y);
  ~ShaderCache();

  // Translates (cached) the shaders bound for a draw with the given state.
  bool Prepare(const rex::graphics::RegisterFile& regs, const GuestShaderCode& vs,
               const GuestShaderCode& ps, TranslatedShaders& out);

  // Records each new guest shader (null: none).
  void SetStorage(PipelineStorage* storage);

  // Translates and creates the `variants` of the stored shaders on `threads`
  // threads; not concurrently with Prepare. `done` counts finished variants.
  void Precompile(const std::vector<StoredShader>& shaders, const ShaderVariants& variants,
                  uint32_t threads, const std::atomic<bool>& cancel, std::atomic<uint32_t>& done);

  // A built variant; any thread while nothing is being built.
  bool Find(uint64_t hash, uint64_t modification, Diligent::IShader*& shader,
            const rex::graphics::SpirvShader*& info) const;

  // One NATIVE-SH summary line.
  void LogStats();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace renderer
