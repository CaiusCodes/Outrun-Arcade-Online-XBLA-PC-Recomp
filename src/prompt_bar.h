#pragma once
// The game-styled overlay pieces shared by the initials entry and the restart
// prompt: heavy italic lettering with a dark edge, pad buttons and key caps.
#include "ui_font.h"
#include <imgui.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <utility>

namespace outrun::prompt_bar {
// The controls bar: the game's heavy italic white lettering with a dark
// edge, on a dark glass bar like the table's own rows. Each item measures
// (draw = false) or draws from x and returns its width.
class Bar {
 public:
  Bar(ImDrawList* draw, float scale) : draw_(draw), scale_(scale) {
    font_ = outrun::ui_font::Display() ? outrun::ui_font::Display() : ImGui::GetFont();
  }
  float Text(float x, float y, const char* text, ImU32 colour, bool draw, float size = 25) const {
    const float s = size * scale_;
    const ImVec2 extent = font_->CalcTextSizeA(s, FLT_MAX, 0, text);
    if (draw) {
      const ImVec2 at(std::floor(x), std::floor(y - extent.y / 2));
      const float edge = std::max(1.0f, 1.6f * scale_);
      for (const auto& [dx, dy] : {std::pair{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}})
        draw_->AddText(font_, s, ImVec2(at.x + dx * edge, at.y + dy * edge), IM_COL32(10, 16, 40, 230), text);
      draw_->AddText(font_, s, at, colour, text);
    }
    return extent.x;
  }
  // A round pad button: 'A', 'B', 'S' (START) or 'D' (the D-pad).
  float Button(float x, float y, char kind, bool draw) const {
    const float r = 13 * scale_;
    if (!draw) return 2 * r;
    const ImVec2 c(x + r, y);
    if (kind == 'D') {
      const float arm = r * 0.95f, w = r * 0.42f, in = scale_;
      const ImU32 edge = IM_COL32(20, 24, 40, 255), face = IM_COL32(200, 205, 215, 255);
      draw_->AddRectFilled(ImVec2(c.x - arm, c.y - w), ImVec2(c.x + arm, c.y + w), edge, 2 * scale_);
      draw_->AddRectFilled(ImVec2(c.x - w, c.y - arm), ImVec2(c.x + w, c.y + arm), edge, 2 * scale_);
      draw_->AddRectFilled(ImVec2(c.x - arm + in, c.y - w + in), ImVec2(c.x + arm - in, c.y + w - in), face, 2 * scale_);
      draw_->AddRectFilled(ImVec2(c.x - w + in, c.y - arm + in), ImVec2(c.x + w - in, c.y + arm - in), face, 2 * scale_);
      return 2 * r;
    }
    const ImU32 fill = kind == 'A'   ? IM_COL32(70, 170, 40, 255)
                       : kind == 'B' ? IM_COL32(205, 35, 35, 255)
                                     : IM_COL32(225, 228, 235, 255);
    draw_->AddCircleFilled(c, r, IM_COL32(10, 16, 40, 255));
    draw_->AddCircleFilled(c, r - 1.5f * scale_, fill);
    draw_->AddCircleFilled(ImVec2(c.x, c.y - r * 0.35f), r * 0.55f, IM_COL32(255, 255, 255, 50));
    if (kind == 'S') {
      const float a = r * 0.42f;
      draw_->AddTriangleFilled(ImVec2(c.x - a * 0.7f, c.y - a), ImVec2(c.x + a, c.y), ImVec2(c.x - a * 0.7f, c.y + a),
                               IM_COL32(40, 44, 60, 255));
    } else {
      const char glyph[2] = {kind, 0};
      const float s = 19 * scale_;
      const ImVec2 extent = font_->CalcTextSizeA(s, FLT_MAX, 0, glyph);
      draw_->AddText(font_, s, ImVec2(std::floor(c.x - extent.x / 2), std::floor(c.y - extent.y / 2)),
                     IM_COL32(255, 255, 255, 255), glyph);
    }
    return 2 * r;
  }
  // A keyboard key cap with its name.
  float Key(float x, float y, const char* name, bool draw) const {
    const float s = 17 * scale_, pad = 7 * scale_, h = 24 * scale_;
    const ImVec2 extent = font_->CalcTextSizeA(s, FLT_MAX, 0, name);
    const float w = extent.x + 2 * pad;
    if (draw) {
      draw_->AddRectFilled(ImVec2(x, y - h / 2), ImVec2(x + w, y + h / 2), IM_COL32(10, 16, 40, 255), 4 * scale_);
      draw_->AddRectFilled(ImVec2(x + 1.5f * scale_, y - h / 2 + 1.5f * scale_),
                           ImVec2(x + w - 1.5f * scale_, y + h / 2 - 1.5f * scale_), IM_COL32(225, 228, 235, 255),
                           3 * scale_);
      draw_->AddText(font_, s, ImVec2(std::floor(x + pad), std::floor(y - extent.y / 2)), IM_COL32(30, 34, 50, 255),
                     name);
    }
    return w;
  }
  float Gap() const { return 8 * scale_; }
  float Spacing() const { return 30 * scale_; }

 private:
  ImDrawList* draw_;
  float scale_;
  ImFont* font_;
};

}  // namespace outrun::prompt_bar
