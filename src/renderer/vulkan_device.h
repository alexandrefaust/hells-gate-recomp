#pragma once

#include <cstdint>
#include <memory>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
struct IShader;
}  // namespace Diligent

namespace renderer {

// DiligentCore Vulkan device and the game window's swap chain.
class VulkanDevice {
 public:
  VulkanDevice();
  ~VulkanDevice();

  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;

  bool Initialize(void* hwnd, uint32_t width, uint32_t height);

  void BeginFrame();

  void Clear(float r, float g, float b, float a);

  void Present(uint32_t sync_interval = 1);

  void SetDisplayAspect(double aspect, bool letterbox);

  // Blits a shader-resource view to the swapchain (letterboxed) and presents.
  bool PresentTexture(Diligent::ITextureView* srv, uint32_t sync_interval = 1);

  // GLSL (Vulkan) -> SPIR-V -> shader; the caller owns the returned reference.
  Diligent::IShader* CreateGlslShader(const char* source, bool pixel, const char* name);

  Diligent::IRenderDevice* RenderDevice() const;
  Diligent::IDeviceContext* Context() const;

  // Resizes the swap chain to the window; false while the window is minimized.
  bool UpdateWindowSize();

  void Resize(uint32_t width, uint32_t height);

  void Shutdown();

 private:
  bool CreateBlitPipeline();

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace renderer
