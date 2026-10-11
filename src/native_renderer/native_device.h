#pragma once

#include <cstdint>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITextureView;
struct IShader;
}  // namespace Diligent

namespace dante {

// DiligentCore Vulkan device and the game window's swap chain.
class NativeDevice {
 public:
  NativeDevice();
  ~NativeDevice();

  NativeDevice(const NativeDevice&) = delete;
  NativeDevice& operator=(const NativeDevice&) = delete;

  bool initialize(void* hwnd, uint32_t width, uint32_t height);

  void beginFrame();

  void clear(float r, float g, float b, float a);

  void present(uint32_t sync_interval = 1);

  void setDisplayAspect(double aspect, bool letterbox);

  // Blits a shader-resource view to the swapchain (letterboxed) and presents.
  bool presentTexture(Diligent::ITextureView* srv, uint32_t sync_interval = 1);

  // GLSL (Vulkan) -> SPIR-V -> shader; the caller owns the returned reference.
  Diligent::IShader* createGlslShader(const char* source, bool pixel, const char* name);

  Diligent::IRenderDevice* renderDevice() const;
  Diligent::IDeviceContext* immediateContext() const;

  // Resizes the swap chain to the window; false while the window is minimized.
  bool updateWindowSize();

  void resize(uint32_t width, uint32_t height);

  void shutdown();

 private:
  bool initializeBlitPipeline();

  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace dante
