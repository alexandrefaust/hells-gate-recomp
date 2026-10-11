#include "achievement_ui.h"

#include "achievement_sound.h"
#include "overlay_style.h"

#include <rex/ui/immediate_drawer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <numbers>
#include <string>
#include <vector>

namespace overlay {

namespace {

float EaseOutCubic(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return 1.0f - std::pow(1.0f - t, 3.0f);
}

float EaseOutBack(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  constexpr float c1 = 1.4f, c3 = c1 + 1.0f;
  return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
}

// Rounded square in the game's gold with a star, for achievements without an icon.
void DrawPlaceholderIcon(ImDrawList* list, ImVec2 min, float size, float rounding, float alpha) {
  ImVec2 max(min.x + size, min.y + size);
  list->AddRectFilled(min, max, WithAlpha(color::kEmber, alpha), rounding);
  list->AddRectFilled(min, ImVec2(max.x, min.y + size * 0.6f), WithAlpha(color::kGold, alpha),
                      rounding, ImDrawFlags_RoundCornersTop);
  const ImVec2 center(min.x + size * 0.5f, min.y + size * 0.52f);
  const float outer = size * 0.3f, inner = outer * 0.42f;
  for (int i = 0; i < 10; ++i) {
    float angle = -std::numbers::pi_v<float> / 2 + float(i) * std::numbers::pi_v<float> / 5;
    float radius = (i % 2) ? inner : outer;
    list->PathLineTo(
        ImVec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius));
  }
  list->PathFillConcave(IM_COL32(255, 255, 255, int(235 * alpha)));
}

void DrawLockBadge(ImDrawList* list, ImVec2 center, float radius) {
  list->AddCircleFilled(center, radius, IM_COL32(20, 19, 24, 240));
  list->AddCircle(center, radius, IM_COL32(255, 255, 255, 40), 0, std::max(1.0f, radius * 0.08f));
  const float w = radius * 0.9f, h = radius * 0.7f;
  const ImVec2 body_min(center.x - w * 0.5f, center.y - h * 0.15f);
  const ImVec2 body_max(center.x + w * 0.5f, center.y + h * 0.7f);
  list->AddRectFilled(body_min, body_max, color::kTextMuted, radius * 0.12f);
  list->PathArcTo(ImVec2(center.x, body_min.y), w * 0.3f, std::numbers::pi_v<float>,
                  2.0f * std::numbers::pi_v<float>, 12);
  list->PathStroke(color::kTextMuted, 0, radius * 0.16f);
}

void DrawIcon(ImDrawList* list, rex::ui::ImmediateTexture* icon, ImVec2 min, float size,
              float rounding, ImU32 tint, float alpha) {
  if (icon) {
    list->AddImageRounded(reinterpret_cast<ImTextureID>(icon), min,
                          ImVec2(min.x + size, min.y + size), ImVec2(0, 0), ImVec2(1, 1),
                          WithAlpha(tint, alpha), rounding);
  } else {
    DrawPlaceholderIcon(list, min, size, rounding, alpha);
  }
}

std::string FormatGamerscore(uint32_t gamerscore) { return std::to_string(gamerscore) + "G"; }

std::string FormatUnlockDate(uint64_t filetime) {
  constexpr uint64_t kUnixEpoch = 116444736000000000ull;  // 1970-01-01 as a FILETIME
  if (filetime < kUnixEpoch) return {};
  std::time_t time = std::time_t((filetime - kUnixEpoch) / 10000000ull);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  char text[32];
  std::strftime(text, sizeof(text), "%d %b %Y", &tm);
  return text[0] == '0' ? std::string(text + 1) : std::string(text);
}

// Locked achievements without the "show unachieved" flag (0x8) are secret, when
// the title's metadata carries flags at all.
bool IsSecret(const rex::system::AchievementInfo& a, bool flags_present) {
  return flags_present && !(a.flags & 0x8);
}

}  // namespace

AchievementToast::AchievementToast(rex::ui::ImGuiDrawer* drawer,
                                   rex::ui::ImmediateDrawer* icon_drawer, rex::Runtime* runtime)
    : rex::ui::AchievementNotificationDialog(drawer), icons_(icon_drawer, runtime) {}

void AchievementToast::Push(const rex::system::AchievementEvent& event) {
  std::lock_guard lock(mutex_);
  queue_.push_back({event, std::nullopt});
}

void AchievementToast::OnDraw(ImGuiIO& io) {
  const auto now = std::chrono::steady_clock::now();
  const float total = kEnterSeconds + kShowSeconds + kExitSeconds;
  Pending toast;
  float age = 0.0f;
  bool just_shown = false;
  {
    std::lock_guard lock(mutex_);
    while (!queue_.empty()) {
      if (!queue_.front().shown) {
        queue_.front().shown = now;
        just_shown = true;
      }
      age = std::chrono::duration<float>(now - *queue_.front().shown).count();
      if (age < total) break;
      queue_.pop_front();
    }
    if (queue_.empty()) return;
    toast = queue_.front();
  }
  if (just_shown) PlayAchievementSound();

  const rex::system::AchievementInfo& a = toast.event.achievement;
  const float s = UiScale(io);

  // Slide down into place, then up and out.
  float enter = EaseOutBack(age / kEnterSeconds);
  float alpha = EaseOutCubic(age / (kEnterSeconds * 0.6f));
  float exit_t = (age - kEnterSeconds - kShowSeconds) / kExitSeconds;
  float offset = (1.0f - enter) * -36.0f * s;
  if (exit_t > 0.0f) {
    alpha = 1.0f - EaseOutCubic(exit_t);
    offset = -EaseOutCubic(exit_t) * 28.0f * s;
  }
  if (alpha <= 0.0f) return;

  const float width = std::min(640.0f * s, io.DisplaySize.x - 48.0f * s);
  const float height = 128.0f * s;
  const float rounding = 20.0f * s;
  const ImVec2 min(std::round((io.DisplaySize.x - width) * 0.5f),
                   std::round(io.DisplaySize.y * 0.055f + offset));
  const ImVec2 max(min.x + width, min.y + height);

  ImDrawList* list = ImGui::GetForegroundDrawList();
  DrawShadow(list, min, max, rounding, 18.0f * s, alpha);
  list->AddRectFilled(min, max, WithAlpha(color::kPanel, alpha), rounding);
  list->AddRect(min, max, WithAlpha(IM_COL32(246, 190, 74, 70), alpha), rounding, 0,
                std::max(1.0f, 1.25f * s));

  // Icon with a glow ring that fades after the entrance.
  const float icon_size = 92.0f * s;
  const float icon_rounding = 16.0f * s;
  const ImVec2 icon_min(min.x + 18.0f * s, min.y + (height - icon_size) * 0.5f);
  const float glow = std::clamp(1.0f - (age - kEnterSeconds) / 1.6f, 0.0f, 1.0f);
  for (int i = 3; i >= 1; --i) {
    float spread = float(i) * 3.0f * s;
    list->AddRect(ImVec2(icon_min.x - spread, icon_min.y - spread),
                  ImVec2(icon_min.x + icon_size + spread, icon_min.y + icon_size + spread),
                  WithAlpha(color::kGold, alpha * glow * 0.22f * float(4 - i)),
                  icon_rounding + spread, 0, 2.0f * s);
  }
  DrawIcon(list, icons_.GetIcon(a), icon_min, icon_size, icon_rounding, IM_COL32_WHITE, alpha);
  list->AddRect(icon_min, ImVec2(icon_min.x + icon_size, icon_min.y + icon_size),
                WithAlpha(color::kGold, alpha * (0.35f + 0.65f * glow)), icon_rounding, 0,
                std::max(1.0f, 1.5f * s));

  // Text column: header + gamerscore, name, description.
  const float text_x = icon_min.x + icon_size + 22.0f * s;
  const float text_right = max.x - 24.0f * s;
  const std::string score = FormatGamerscore(a.gamerscore);
  const ImVec2 score_size = MeasureText(Font::kBodyStrong, s, score);
  const ImVec2 caption_size = MeasureText(Font::kCaption, s, "ACHIEVEMENT UNLOCKED");
  const ImVec2 title_size = MeasureText(Font::kTitle, s, "Ag");
  const ImVec2 body_size = MeasureText(Font::kBody, s, "Ag");
  const bool has_description = !a.description.empty();
  const float block_h = caption_size.y + title_size.y + (has_description ? body_size.y : 0.0f);
  float y = min.y + (height - block_h) * 0.5f;

  DrawText(list, Font::kCaption, s, ImVec2(text_x, y), WithAlpha(color::kGold, alpha),
           "ACHIEVEMENT UNLOCKED");
  if (a.gamerscore) {
    const float pill_pad = 10.0f * s;
    const ImVec2 pill_max(text_right, y + score_size.y + 2.0f * s);
    const ImVec2 pill_min(pill_max.x - score_size.x - pill_pad * 2, y - 2.0f * s);
    list->AddRectFilled(pill_min, pill_max, WithAlpha(IM_COL32(246, 190, 74, 40), alpha),
                        (pill_max.y - pill_min.y) * 0.5f);
    DrawText(list, Font::kBodyStrong, s, ImVec2(pill_min.x + pill_pad, pill_min.y + 2.0f * s),
             WithAlpha(color::kGold, alpha), score);
  }
  y += caption_size.y;
  DrawText(list, Font::kTitle, s, ImVec2(text_x, y - 2.0f * s), WithAlpha(color::kText, alpha),
           Ellipsize(Font::kTitle, s, a.label, text_right - text_x));
  y += title_size.y;
  if (has_description) {
    DrawText(list, Font::kBody, s, ImVec2(text_x, y - 2.0f * s),
             WithAlpha(color::kTextMuted, alpha),
             Ellipsize(Font::kBody, s, a.description, text_right - text_x));
  }

  // Remaining time, along the bottom edge.
  const float remaining = std::clamp(1.0f - (age - kEnterSeconds) / kShowSeconds, 0.0f, 1.0f);
  const float bar_inset = rounding;
  const float bar_w = (width - bar_inset * 2) * remaining;
  if (bar_w > 1.0f) {
    const float bar_y = max.y - 3.0f * s;
    list->AddLine(ImVec2(min.x + bar_inset, bar_y), ImVec2(min.x + bar_inset + bar_w, bar_y),
                  WithAlpha(color::kGold, alpha * 0.55f), 2.0f * s);
  }
}

AchievementList::AchievementList(rex::ui::ImGuiDrawer* drawer,
                                 rex::ui::ImmediateDrawer* icon_drawer, rex::Runtime* runtime,
                                 rex::system::AchievementManager* achievements)
    : rex::ui::ImGuiDialog(drawer), achievements_(achievements), icons_(icon_drawer, runtime) {}

void AchievementList::OnDraw(ImGuiIO& io) {
  const float s = UiScale(io);
  struct Row {
    rex::system::AchievementInfo info;
    bool unlocked;
    uint64_t time;
  };
  std::vector<Row> rows;
  int unlocked = 0, earned = 0, total_score = 0;
  bool flags_present = false;
  for (auto& info : achievements_->ListAchievements()) {
    bool is_unlocked = achievements_->IsUnlocked(info.id);
    uint64_t time = is_unlocked ? achievements_->GetUnlockTime(info.id) : 0;
    total_score += int(info.gamerscore);
    if (is_unlocked) {
      ++unlocked;
      earned += int(info.gamerscore);
    }
    flags_present |= (info.flags & 0x8) != 0;
    rows.push_back({std::move(info), is_unlocked, time});
  }
  // Most recent unlocks first, then the locked ones in catalog order.
  std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    if (a.unlocked != b.unlocked) return a.unlocked;
    return a.unlocked && a.time > b.time;
  });

  // Dim the game behind the panel.
  ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize,
                                                IM_COL32(0, 0, 0, 150));

  const ImVec2 size(std::min(1120.0f * s, io.DisplaySize.x - 64.0f * s),
                    std::min(900.0f * s, io.DisplaySize.y - 96.0f * s));
  ImGui::SetNextWindowPos(
      ImVec2((io.DisplaySize.x - size.x) * 0.5f, (io.DisplaySize.y - size.y) * 0.5f),
      ImGuiCond_Always);
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 24.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(40.0f * s, 34.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, std::max(1.0f, s));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 10.0f * s));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8.0f * s);
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 4.0f * s);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(color::kPanel));
  ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(color::kPanelBorder));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(1, 1, 1, 0.16f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(1, 1, 1, 0.26f));
  ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(1, 1, 1, 0.34f));

  if (ImGui::Begin("##achievement_list", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {
    ImDrawList* list = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float content_w = ImGui::GetContentRegionAvail().x;

    // Header: title, counts, progress.
    const ImVec2 heading = MeasureText(Font::kHeading, s, "Achievements");
    DrawText(list, Font::kHeading, s, origin, color::kText, "Achievements");
    char count_text[32], score_text[48];
    std::snprintf(count_text, sizeof(count_text), "%d / %d", unlocked, int(rows.size()));
    std::snprintf(score_text, sizeof(score_text), "%d / %dG", earned, total_score);
    const ImVec2 score_size = MeasureText(Font::kTitle, s, score_text);
    const ImVec2 count_size = MeasureText(Font::kTitle, s, count_text);
    const float stats_y = origin.y + heading.y - score_size.y - 2.0f * s;
    float x = origin.x + content_w - score_size.x;
    DrawText(list, Font::kTitle, s, ImVec2(x, stats_y), color::kGold, score_text);
    const ImVec2 label = MeasureText(Font::kCaption, s, "GAMERSCORE");
    DrawText(list, Font::kCaption, s, ImVec2(x, stats_y - label.y + 2.0f * s), color::kTextFaint,
             "GAMERSCORE");
    x -= count_size.x + 40.0f * s;
    DrawText(list, Font::kTitle, s, ImVec2(x, stats_y), color::kText, count_text);
    DrawText(list, Font::kCaption, s, ImVec2(x, stats_y - label.y + 2.0f * s), color::kTextFaint,
             "UNLOCKED");

    const float bar_y = origin.y + heading.y + 16.0f * s;
    const float bar_h = 8.0f * s;
    const float fraction = rows.empty() ? 0.0f : float(unlocked) / float(rows.size());
    list->AddRectFilled(ImVec2(origin.x, bar_y), ImVec2(origin.x + content_w, bar_y + bar_h),
                        IM_COL32(255, 255, 255, 18), bar_h * 0.5f);
    if (fraction > 0.0f) {
      list->AddRectFilled(ImVec2(origin.x, bar_y),
                          ImVec2(origin.x + std::max(content_w * fraction, bar_h), bar_y + bar_h),
                          color::kGold, bar_h * 0.5f);
    }
    ImGui::Dummy(ImVec2(content_w, bar_y + bar_h + 22.0f * s - origin.y));

    // Footer hint.
    const ImVec2 hint_size = MeasureText(Font::kCaption, s, "F7  Close");
    const float footer_h = hint_size.y + 16.0f * s;

    ImGui::PushFont(GetFont(Font::kBody), FontSize(Font::kBody, s));
    ImGui::BeginChild("##rows", ImVec2(0.0f, -footer_h), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoNav);
    ImDrawList* rows_list = ImGui::GetWindowDrawList();
    const float row_w = ImGui::GetContentRegionAvail().x - 14.0f * s;
    const float row_h = 112.0f * s;
    const float icon_size = 80.0f * s;
    const float right_w = 170.0f * s;
    for (const Row& row : rows) {
      const ImVec2 min = ImGui::GetCursorScreenPos();
      ImGui::Dummy(ImVec2(row_w, row_h));
      if (!ImGui::IsItemVisible()) continue;
      const ImVec2 max(min.x + row_w, min.y + row_h);
      const auto& a = row.info;
      const bool secret = !row.unlocked && IsSecret(a, flags_present);

      rows_list->AddRectFilled(
          min, max, row.unlocked ? IM_COL32(255, 255, 255, 14) : IM_COL32(255, 255, 255, 6),
          16.0f * s);
      if (row.unlocked) {
        rows_list->AddRectFilled(ImVec2(min.x, min.y + 22.0f * s),
                                 ImVec2(min.x + 4.0f * s, max.y - 22.0f * s), color::kGold,
                                 2.0f * s);
      }

      const ImVec2 icon_min(min.x + 20.0f * s, min.y + (row_h - icon_size) * 0.5f);
      DrawIcon(rows_list, secret ? nullptr : icons_.GetIcon(a), icon_min, icon_size, 14.0f * s,
               row.unlocked ? IM_COL32_WHITE : IM_COL32(120, 120, 120, 255),
               row.unlocked ? 1.0f : 0.55f);
      if (!row.unlocked) {
        DrawLockBadge(rows_list,
                      ImVec2(icon_min.x + icon_size - 6.0f * s, icon_min.y + icon_size - 6.0f * s),
                      15.0f * s);
      }

      const float text_x = icon_min.x + icon_size + 24.0f * s;
      const float text_w = max.x - right_w - text_x - 16.0f * s;
      const float title_scale = s * 0.87f;
      const std::string title = secret ? "Secret achievement" : a.label;
      std::string description = row.unlocked ? a.description : a.unachieved_description;
      if (description.empty()) description = a.description;
      if (secret) description = "Keep playing to reveal this achievement.";
      const std::string wrapped = WrapLines(Font::kBody, s, description, text_w, 2);
      const ImVec2 title_size = MeasureText(Font::kTitle, title_scale, title);
      const ImVec2 desc_size = MeasureText(Font::kBody, s, wrapped);
      const float block_h = title_size.y + 2.0f * s + (wrapped.empty() ? 0.0f : desc_size.y);
      float y = min.y + (row_h - block_h) * 0.5f;
      DrawText(rows_list, Font::kTitle, title_scale, ImVec2(text_x, y),
               row.unlocked ? color::kText : color::kTextMuted,
               Ellipsize(Font::kTitle, title_scale, title, text_w));
      y += title_size.y + 2.0f * s;
      if (!wrapped.empty()) {
        DrawText(rows_list, Font::kBody, s, ImVec2(text_x, y),
                 row.unlocked ? color::kTextMuted : color::kTextFaint, wrapped);
      }

      // Gamerscore and unlock date, right aligned.
      const std::string score = FormatGamerscore(a.gamerscore);
      const ImVec2 score_text = MeasureText(Font::kBodyStrong, s, score);
      const std::string date = row.unlocked ? FormatUnlockDate(row.time) : std::string();
      const ImVec2 date_size = MeasureText(Font::kCaption, s, date);
      const float right_h = score_text.y + (date.empty() ? 0.0f : date_size.y + 4.0f * s);
      float ry = min.y + (row_h - right_h) * 0.5f;
      const float pill_pad = 12.0f * s;
      const ImVec2 pill_min(max.x - 24.0f * s - score_text.x - pill_pad * 2, ry - 3.0f * s);
      const ImVec2 pill_max(max.x - 24.0f * s, ry + score_text.y + 3.0f * s);
      rows_list->AddRectFilled(
          pill_min, pill_max,
          row.unlocked ? IM_COL32(246, 190, 74, 40) : IM_COL32(255, 255, 255, 12),
          (pill_max.y - pill_min.y) * 0.5f);
      DrawText(rows_list, Font::kBodyStrong, s, ImVec2(pill_min.x + pill_pad, ry),
               row.unlocked ? color::kGold : color::kTextFaint, score);
      if (!date.empty()) {
        ry += score_text.y + 8.0f * s;
        DrawText(rows_list, Font::kCaption, s, ImVec2(pill_max.x - date_size.x, ry),
                 color::kTextFaint, date);
      }
    }
    ImGui::EndChild();
    ImGui::PopFont();

    const ImVec2 window_max(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                            ImGui::GetWindowPos().y + ImGui::GetWindowSize().y);
    const float hint_y = window_max.y - 34.0f * s - hint_size.y + 4.0f * s;
    const char* hint =
        rows.empty() ? "No achievements for this game." : "Scroll with the mouse wheel";
    DrawText(list, Font::kCaption, s, ImVec2(origin.x, hint_y), color::kTextFaint, hint);
    DrawText(list, Font::kCaption, s, ImVec2(origin.x + content_w - hint_size.x, hint_y),
             color::kTextFaint, "F7  Close");
  }
  ImGui::End();
  ImGui::PopStyleColor(6);
  ImGui::PopStyleVar(6);
}

}  // namespace overlay
