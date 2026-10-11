// renderer=dante: the SDK's ImGui overlays (FPS, achievements, settings,
// console) drawn on the native swap chain. The SDK presenter that normally
// draws them is detached, so ImGui runs on the UI thread with OverlayDrawer,
// which records each finished ImGui frame; the renderer draws the latest
// recorded frame over every presented image (one frame of latency).

#pragma once

#include <rex/ui/immediate_drawer.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Diligent {
struct IDeviceContext;
struct ITextureView;
}  // namespace Diligent

namespace renderer {

class VulkanDevice;

// Pixels of an ImmediateTexture; the GPU texture is created by the renderer
// on first use.
struct OverlayTextureData {
  uint32_t width = 0;
  uint32_t height = 0;
  bool linear = true;
  std::vector<uint8_t> rgba;
};

struct OverlayFrame {
  struct Command {
    uint32_t first_index = 0;
    uint32_t index_count = 0;
    int32_t base_vertex = 0;
    std::shared_ptr<const OverlayTextureData> texture;  // null = white
    float clip[4] = {};                                 // coordinate space
  };
  float width = 0.0f;  // coordinate space (ImGui logical pixels)
  float height = 0.0f;
  std::vector<rex::ui::ImmediateVertex> vertices;
  std::vector<uint16_t> indices;
  std::vector<Command> commands;
};

// UI thread. Records ImGuiDrawer output instead of drawing it.
class OverlayDrawer : public rex::ui::ImmediateDrawer {
 public:
  OverlayDrawer() = default;

  std::unique_ptr<rex::ui::ImmediateTexture> CreateTexture(uint32_t width, uint32_t height,
                                                           rex::ui::ImmediateTextureFilter filter,
                                                           bool is_repeated,
                                                           const uint8_t* data) override;
  void Begin(rex::ui::UIDrawContext& ui_draw_context, float coordinate_space_width,
             float coordinate_space_height) override;
  void BeginDrawBatch(const rex::ui::ImmediateDrawBatch& batch) override;
  void Draw(const rex::ui::ImmediateDraw& draw) override;
  void EndDrawBatch() override {}
  void End() override;

  // Nothing to draw (no ImGui dialogs).
  void PublishEmpty();

 private:
  std::shared_ptr<OverlayFrame> frame_;
  uint32_t batch_first_vertex_ = 0;
  uint32_t batch_first_index_ = 0;
};

// Render thread: draws the latest recorded frame into the bound back buffer.
class OverlayRenderer {
 public:
  explicit OverlayRenderer(VulkanDevice* device);
  ~OverlayRenderer();

  void Draw(Diligent::IDeviceContext* context, Diligent::ITextureView* back_buffer);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Called by the renderer after each present so the UI thread records the next
// overlay frame. nullptr to stop.
void SetOverlayFrameRequest(std::function<void()> request);
void RequestOverlayFrame();

}  // namespace renderer
