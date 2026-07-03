#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace lg {

struct RenderColor {
  std::uint8_t red = 255;
  std::uint8_t green = 255;
  std::uint8_t blue = 255;
  std::uint8_t alpha = 255;
};

struct ScreenPoint {
  float x = 0.0F;
  float y = 0.0F;
};

struct ScreenRect {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
};

struct FilledQuad2D {
  std::array<ScreenPoint, 4> points = {};
  RenderColor color = {};
};

struct Line2D {
  ScreenPoint start = {};
  ScreenPoint end = {};
  RenderColor color = {};
  float width = 1.0F;
};

enum class TextHorizontalAlignment : std::uint8_t {
  Left,
  Center,
};

struct Text2D {
  ScreenPoint position = {};
  std::string text;
  RenderColor color = {};
  float scale = 1.0F;
  TextHorizontalAlignment horizontalAlignment = TextHorizontalAlignment::Left;
};

using DrawCommand2D = std::variant<FilledQuad2D, Line2D, Text2D>;

struct DrawList2D {
  ScreenRect clip = {};
  std::vector<DrawCommand2D> commands;
  std::vector<DrawCommand2D> overlayCommands;
};

} // namespace lg
