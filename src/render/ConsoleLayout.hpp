#pragma once

#include "render/Renderer.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace lg {

struct ConsoleLayoutLine {
  std::string text;
  float x = 0.0F;
  float y = 0.0F;
  std::size_t textOffset = 0;
  std::size_t inputBegin = 0;
  std::size_t inputEnd = 0;
  std::size_t contentColumn = 0;
  bool prompt = false;
};

struct ConsoleTextLayout {
  std::vector<ConsoleLayoutLine> lines;
  std::string text;
  float consoleHeight = 0.0F;
  float characterWidth = 0.0F;
  float lineHeight = 0.0F;
  std::size_t maxScrollRows = 0;
};

[[nodiscard]] ConsoleTextLayout buildConsoleTextLayout(
  int outputWidth,
  int outputHeight,
  const ConsoleRenderState& console
);

[[nodiscard]] std::size_t consoleTextOffsetAt(
  const ConsoleTextLayout& layout,
  float x,
  float y
);

[[nodiscard]] std::string consoleSelectedText(
  const ConsoleTextLayout& layout,
  std::size_t anchor,
  std::size_t focus
);

[[nodiscard]] std::size_t consoleInputOffsetAt(const ConsoleTextLayout &layout,
                                               const std::string &input,
                                               float x, float y);

[[nodiscard]] ScreenPoint
consoleInputCursorPosition(const ConsoleTextLayout &layout,
                           const std::string &input, std::size_t cursor);

} // namespace lg
