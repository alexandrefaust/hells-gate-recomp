// Achievement notification (toast, top center, with a sound) and the
// achievement list (bind_achievements, F7).

#pragma once

#include <rex/system/achievement_manager.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/overlay/achievement_icon_cache.h>
#include <rex/ui/overlay/achievement_notification.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>

namespace rex {
class Runtime;
}

namespace overlay {

class AchievementToast : public rex::ui::AchievementNotificationDialog {
 public:
  AchievementToast(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* icon_drawer,
                   rex::Runtime* runtime);

  // Any thread.
  void Push(const rex::system::AchievementEvent& event) override;

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  static constexpr float kEnterSeconds = 0.45f;
  static constexpr float kShowSeconds = 6.0f;
  static constexpr float kExitSeconds = 0.4f;

  struct Pending {
    rex::system::AchievementEvent event;
    std::optional<std::chrono::steady_clock::time_point> shown;
  };

  std::mutex mutex_;
  std::deque<Pending> queue_;
  rex::ui::AchievementIconCache icons_;
};

class AchievementList : public rex::ui::ImGuiDialog {
 public:
  AchievementList(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* icon_drawer,
                  rex::Runtime* runtime, rex::system::AchievementManager* achievements);

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  rex::system::AchievementManager* achievements_;
  rex::ui::AchievementIconCache icons_;
};

}  // namespace overlay
