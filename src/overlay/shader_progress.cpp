#include "shader_progress.h"

#include "overlay_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace overlay {
namespace {

// Pipelines take most of the time; shaders are quick to translate.
constexpr float kShaderShare = 0.3f;
// Shown only when building takes longer than a blink.
constexpr float kShowAfterSeconds = 0.3f, kFadeSeconds = 0.25f;

}  // namespace

ShaderProgress::ShaderProgress(rex::ui::ImGuiDrawer* drawer, std::function<Status()> status)
    : rex::ui::ImGuiDialog(drawer), status_(std::move(status)) {}

void ShaderProgress::OnDraw(ImGuiIO& io) {
  Status st = status_();
  if (!st.active) return;
  using Clock = std::chrono::steady_clock;
  float elapsed = std::chrono::duration<float>(Clock::now() - st.start).count();
  float alpha = std::clamp((elapsed - kShowAfterSeconds) / kFadeSeconds, 0.0f, 1.0f);
  if (alpha <= 0.0f) return;

  auto fraction = [](uint32_t done, uint32_t total) {
    return total ? std::min(1.0f, float(done) / float(total)) : 1.0f;
  };
  float progress = kShaderShare * fraction(st.shaders_done, st.shaders_total) +
                   (1.0f - kShaderShare) * fraction(st.pipelines_done, st.pipelines_total);
  shown_progress_ += (progress - shown_progress_) * std::min(1.0f, io.DeltaTime * 10.0f);

  const float s = UiScale(io);
  ImDrawList* list = ImGui::GetForegroundDrawList();
  const ImVec2 display = io.DisplaySize;
  list->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, int(255 * alpha)));

  // Card.
  const float card_w = std::min(720.0f * s, display.x - 48.0f * s);
  const float pad = 40.0f * s;
  const float text_w = card_w - pad * 2;
  const char* title = "Preparing shaders";
  const char* body =
      "Building the game's shaders and pipelines for your graphics card, so effects don't "
      "stutter the first time they appear. Later launches are much quicker.";
  std::string wrapped = WrapLines(Font::kBody, s, body, text_w, 3);
  const ImVec2 title_size = MeasureText(Font::kHeading, s, title);
  const ImVec2 body_size = MeasureText(Font::kBody, s, wrapped);
  const float bar_h = 12.0f * s;
  const float status_h = MeasureText(Font::kBodyStrong, s, "0").y;
  const float card_h =
      pad + title_size.y + 12.0f * s + body_size.y + 32.0f * s + bar_h + 16.0f * s + status_h + pad;
  const ImVec2 card_min(std::round((display.x - card_w) * 0.5f),
                        std::round((display.y - card_h) * 0.5f));
  const ImVec2 card_max(card_min.x + card_w, card_min.y + card_h);
  const float rounding = 18.0f * s;
  DrawShadow(list, card_min, card_max, rounding, 18.0f * s, 0.7f * alpha);
  list->AddRectFilled(card_min, card_max, WithAlpha(color::kPanel, alpha), rounding);
  list->AddRect(card_min, card_max, WithAlpha(color::kPanelBorder, alpha), rounding, 0,
                std::max(1.0f, s));

  ImVec2 pos(card_min.x + pad, card_min.y + pad);
  DrawText(list, Font::kHeading, s, pos, WithAlpha(color::kText, alpha), title);
  pos.y += title_size.y + 12.0f * s;
  DrawText(list, Font::kBody, s, pos, WithAlpha(color::kTextMuted, alpha), wrapped);
  pos.y += body_size.y + 32.0f * s;

  // Progress bar: ember to gold, with a sheen sweeping across so it visibly moves.
  const ImVec2 bar_min = pos, bar_max(pos.x + text_w, pos.y + bar_h);
  const float bar_r = bar_h * 0.5f;
  list->AddRectFilled(bar_min, bar_max, IM_COL32(255, 255, 255, int(18 * alpha)), bar_r);
  const float fill_w = std::max(bar_h, text_w * std::clamp(shown_progress_, 0.0f, 1.0f));
  const ImVec2 fill_max(bar_min.x + fill_w, bar_max.y);
  list->PushClipRect(bar_min, fill_max, true);
  list->AddRectFilledMultiColor(bar_min, ImVec2(bar_min.x + text_w, bar_max.y),
                                WithAlpha(color::kEmber, alpha), WithAlpha(color::kGold, alpha),
                                WithAlpha(color::kGold, alpha), WithAlpha(color::kEmber, alpha));
  const float sweep = std::fmod(elapsed * 0.6f, 1.4f) - 0.2f;
  const float sheen_x = bar_min.x + text_w * sweep, sheen_w = 90.0f * s;
  const ImU32 clear = IM_COL32(255, 255, 255, 0), bright = IM_COL32(255, 255, 255, int(90 * alpha));
  list->AddRectFilledMultiColor(ImVec2(sheen_x - sheen_w, bar_min.y), ImVec2(sheen_x, bar_max.y),
                                clear, bright, bright, clear);
  list->AddRectFilledMultiColor(ImVec2(sheen_x, bar_min.y), ImVec2(sheen_x + sheen_w, bar_max.y),
                                bright, clear, clear, bright);
  list->PopClipRect();
  // Round the fill's ends over the track.
  list->AddRect(bar_min, bar_max, WithAlpha(color::kPanel, alpha), bar_r, 0, 2.0f * s);
  pos.y = bar_max.y + 16.0f * s;

  // Phase and counts on the left, percentage and time on the right.
  char status[96], right[48];
  if (st.pipelines) {
    std::snprintf(status, sizeof(status), "Building pipelines  %u / %u", st.pipelines_done,
                  st.pipelines_total);
  } else {
    std::snprintf(status, sizeof(status), "Translating shaders  %u / %u", st.shaders_done,
                  st.shaders_total);
  }
  std::snprintf(right, sizeof(right), "%d%%   %ds", int(std::floor(shown_progress_ * 100.0f)),
                int(elapsed));
  DrawText(list, Font::kBodyStrong, s, pos, WithAlpha(color::kText, alpha), status);
  const ImVec2 right_size = MeasureText(Font::kBodyStrong, s, right);
  DrawText(list, Font::kBodyStrong, s, ImVec2(card_max.x - pad - right_size.x, pos.y),
           WithAlpha(color::kGold, alpha), right);
}

}  // namespace overlay
