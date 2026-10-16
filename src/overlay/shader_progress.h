// Loading screen while the renderer builds the recorded shaders and pipelines
// before the game starts: what is happening, a progress bar, counts.

#pragma once

#include <rex/ui/imgui_dialog.h>

#include <chrono>
#include <cstdint>
#include <functional>

namespace overlay {

class ShaderProgress : public rex::ui::ImGuiDialog {
 public:
  struct Status {
    bool active = false;
    bool pipelines = false;  // shaders done, building pipelines
    uint32_t shaders_done = 0, shaders_total = 0;
    uint32_t pipelines_done = 0, pipelines_total = 0;
    std::chrono::steady_clock::time_point start;
  };
  ShaderProgress(rex::ui::ImGuiDrawer* drawer, std::function<Status()> status);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  std::function<Status()> status_;
  float shown_progress_ = 0.0f;  // eases toward the real progress
};

}  // namespace overlay
