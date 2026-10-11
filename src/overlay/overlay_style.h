// Shared look of the game's overlays (FPS counter, achievement toast and list):
// fonts, colors and drawing helpers. Sizes are given for a 1080-pixel-high
// display and scale with the display height, so the overlays cover the same
// share of the screen on a monitor or a TV.

#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace rex::ui {
class Window;
}

namespace overlay {

enum class Font {
  kCaption,     // semibold 17: labels, small caps lines
  kBody,        // regular 21: descriptions
  kBodyStrong,  // semibold 21: values, pills
  kTitle,       // bold 30: achievement names, FPS
  kHeading,     // bold 42: panel headings
  kCount,
};

namespace color {
constexpr ImU32 kPanel = IM_COL32(14, 13, 18, 238);
constexpr ImU32 kPanelBorder = IM_COL32(255, 255, 255, 22);
constexpr ImU32 kText = IM_COL32(244, 242, 247, 255);
constexpr ImU32 kTextMuted = IM_COL32(178, 172, 186, 255);
constexpr ImU32 kTextFaint = IM_COL32(120, 115, 128, 255);
constexpr ImU32 kGold = IM_COL32(246, 190, 74, 255);
constexpr ImU32 kEmber = IM_COL32(226, 86, 46, 255);
constexpr ImU32 kGood = IM_COL32(92, 226, 128, 255);
constexpr ImU32 kWarn = IM_COL32(255, 196, 76, 255);
constexpr ImU32 kBad = IM_COL32(255, 92, 92, 255);
}  // namespace color

// From ReXApp::OnConfigureFonts. Bakes the fonts for the window's display.
void LoadFonts(ImFontAtlas* atlas, rex::ui::Window* window);

// Display height / 1080, in ImGui (logical) pixels.
float UiScale(const ImGuiIO& io);

ImFont* GetFont(Font font);
float FontSize(Font font, float scale);

ImVec2 MeasureText(Font font, float scale, std::string_view text);
void DrawText(ImDrawList* list, Font font, float scale, ImVec2 pos, ImU32 color,
              std::string_view text);
// `text` shortened with an ellipsis to fit `max_width`.
std::string Ellipsize(Font font, float scale, std::string_view text, float max_width);
// Up to `max_lines` lines wrapped to `max_width`, the last one ellipsized.
std::string WrapLines(Font font, float scale, std::string_view text, float max_width,
                      int max_lines);

// Soft drop shadow under a rounded rectangle.
void DrawShadow(ImDrawList* list, ImVec2 min, ImVec2 max, float rounding, float size, float alpha);

ImU32 WithAlpha(ImU32 color, float alpha);

}  // namespace overlay
