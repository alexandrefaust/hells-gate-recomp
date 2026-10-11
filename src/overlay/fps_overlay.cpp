#include "fps_overlay.h"

#include "overlay_style.h"

#include <rex/cvar.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace overlay {

FpsOverlay::FpsOverlay(rex::ui::ImGuiDrawer* drawer, std::function<Sample()> counter)
    : rex::ui::ImGuiDialog(drawer), counter_(std::move(counter)) {
  last_ = counter_();
  window_start_ = std::chrono::steady_clock::now();
  window_start_frames_ = last_.frames;
}

void FpsOverlay::Update() {
  Sample now = counter_();
  if (now.frames > last_.frames) {
    uint64_t frames = now.frames - last_.frames;
    float ms =
        std::chrono::duration<float, std::milli>(now.time - last_.time).count() / float(frames);
    for (uint64_t i = 0; i < std::min<uint64_t>(frames, 8); ++i) {
      frame_ms_[history_next_] = ms;
      history_next_ = (history_next_ + 1) % kHistory;
      history_count_ = std::min(history_count_ + 1, kHistory);
    }
    last_ = now;
  } else if (now.frames < last_.frames) {
    last_ = now;
    window_start_frames_ = now.frames;
  }

  auto clock = std::chrono::steady_clock::now();
  float window_ms = std::chrono::duration<float, std::milli>(clock - window_start_).count();
  if (window_ms >= 333.0f) {
    uint64_t frames = last_.frames - window_start_frames_;
    fps_ = float(frames) * 1000.0f / window_ms;
    frame_time_ms_ = frames ? window_ms / float(frames) : 0.0f;
    window_start_ = clock;
    window_start_frames_ = last_.frames;
  }
}

void FpsOverlay::OnDraw(ImGuiIO& io) {
  Update();

  const float s = UiScale(io);
  const double refresh = rex::cvar::Query<double>("video_mode_refresh_rate");
  const float target_fps = refresh >= 24.0 ? float(refresh) : 60.0f;
  const float target_ms = 1000.0f / target_fps;
  ImU32 fps_color = fps_ >= target_fps * 0.95f  ? color::kGood
                    : fps_ >= target_fps * 0.6f ? color::kWarn
                                                : color::kBad;

  char fps_text[16], ms_text[24];
  std::snprintf(fps_text, sizeof(fps_text), "%d", int(std::lround(fps_)));
  std::snprintf(ms_text, sizeof(ms_text), "%.1f ms", frame_time_ms_);

  const float pad_x = 16.0f * s, pad_y = 12.0f * s;
  const ImVec2 number_size = MeasureText(Font::kTitle, s, fps_text);
  const ImVec2 digits_size = MeasureText(Font::kTitle, s, "000");
  const ImVec2 label_size = MeasureText(Font::kCaption, s, "FPS");
  const ImVec2 ms_size = MeasureText(Font::kBodyStrong, s, "000.0 ms");
  const float text_w = std::max(digits_size.x + 6.0f * s + label_size.x, ms_size.x);
  const float text_h = number_size.y + ms_size.y - 4.0f * s;
  const float graph_w = 136.0f * s;
  const float gap = 18.0f * s;

  const ImVec2 origin(std::round(24.0f * s), std::round(24.0f * s));
  const ImVec2 card_max(origin.x + pad_x * 2 + text_w + gap + graph_w,
                        origin.y + pad_y * 2 + text_h);
  const float rounding = 14.0f * s;

  ImDrawList* list = ImGui::GetForegroundDrawList();
  DrawShadow(list, origin, card_max, rounding, 10.0f * s, 0.6f);
  list->AddRectFilled(origin, card_max, IM_COL32(10, 10, 14, 200), rounding);
  list->AddRect(origin, card_max, color::kPanelBorder, rounding, 0, std::max(1.0f, s));

  // Frame rate, "FPS", frame time.
  ImVec2 pos(origin.x + pad_x, origin.y + pad_y - 2.0f * s);
  DrawText(list, Font::kTitle, s, pos, fps_color, fps_text);
  DrawText(
      list, Font::kCaption, s,
      ImVec2(pos.x + number_size.x + 6.0f * s, pos.y + number_size.y - label_size.y - 4.0f * s),
      color::kTextMuted, "FPS");
  DrawText(list, Font::kBodyStrong, s, ImVec2(pos.x, pos.y + number_size.y - 4.0f * s),
           color::kTextMuted, ms_text);

  // Frame time graph: the target frame time is the dashed line; spikes are red.
  const ImVec2 graph_min(origin.x + pad_x + text_w + gap, origin.y + pad_y + 2.0f * s);
  const ImVec2 graph_max(graph_min.x + graph_w, card_max.y - pad_y - 2.0f * s);
  const float graph_h = graph_max.y - graph_min.y;
  float max_ms = target_ms * 2.0f;
  for (size_t i = 0; i < history_count_; ++i) max_ms = std::max(max_ms, frame_ms_[i] * 1.1f);
  max_ms = std::min(max_ms, 100.0f);
  auto y_of = [&](float ms) { return graph_max.y - std::clamp(ms / max_ms, 0.0f, 1.0f) * graph_h; };
  list->AddRectFilled(graph_min, graph_max, IM_COL32(255, 255, 255, 10), 6.0f * s);
  const float target_y = std::round(y_of(target_ms));
  for (float x = graph_min.x + 4.0f * s; x < graph_max.x - 4.0f * s; x += 8.0f * s) {
    list->AddLine(ImVec2(x, target_y),
                  ImVec2(std::min(x + 4.0f * s, graph_max.x - 4.0f * s), target_y),
                  IM_COL32(255, 255, 255, 60), std::max(1.0f, s));
  }
  if (history_count_ >= 2) {
    const float step = (graph_w - 8.0f * s) / float(kHistory - 1);
    const size_t first = (history_next_ + kHistory - history_count_) % kHistory;
    const float x0 = graph_max.x - 4.0f * s - step * float(history_count_ - 1);
    ImVec2 previous;
    for (size_t i = 0; i < history_count_; ++i) {
      float ms = frame_ms_[(first + i) % kHistory];
      ImVec2 point(x0 + step * float(i), y_of(ms));
      ImU32 line = ms > target_ms * 1.5f ? color::kBad : WithAlpha(fps_color, 0.9f);
      list->AddLine(ImVec2(point.x, point.y), ImVec2(point.x, graph_max.y - 1.0f),
                    WithAlpha(line, 0.16f), std::max(1.0f, step));
      if (i > 0) list->AddLine(previous, point, line, 1.6f * s);
      previous = point;
    }
  }
}

}  // namespace overlay
