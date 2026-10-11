// FPS counter: frame rate, frame time and a frame time graph, top-left.

#pragma once

#include <rex/ui/imgui_dialog.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>

namespace overlay {

class FpsOverlay : public rex::ui::ImGuiDialog {
 public:
  struct Sample {
    uint64_t frames = 0;
    std::chrono::steady_clock::time_point time;  // of the last frame
  };
  // `counter`: frames shown so far and when the last one was shown.
  FpsOverlay(rex::ui::ImGuiDrawer* drawer, std::function<Sample()> counter);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void Update();

  static constexpr size_t kHistory = 120;
  std::function<Sample()> counter_;
  Sample last_{};
  std::array<float, kHistory> frame_ms_{};
  size_t history_next_ = 0;
  size_t history_count_ = 0;

  std::chrono::steady_clock::time_point window_start_;
  uint64_t window_start_frames_ = 0;
  float fps_ = 0.0f;
  float frame_time_ms_ = 0.0f;
};

}  // namespace overlay
