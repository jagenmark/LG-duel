#pragma once

#include "render/DrawList2D.hpp"
#include "render/Renderer.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace lg {

struct ChatLayoutRow {
  std::string text;
  float x = 0.0F;
  float y = 0.0F;
  std::size_t messageIndex = 0;
  bool continuation = false;
};

struct ChatInputLayout {
  float x = 0.0F;
  float y = 0.0F;
  float characterWidth = 0.0F;
  float lineHeight = 0.0F;
  std::string prompt = "CHAT: ";
};

struct ChatInputRow {
  std::string text;
  float x = 0.0F;
  float y = 0.0F;
  std::size_t inputBegin = 0;
  std::size_t inputEnd = 0;
  std::size_t contentColumn = 0;
  bool continuation = false;
};

struct ChatTextLayout {
  std::vector<ChatLayoutRow> rows;
  std::vector<ChatInputRow> inputRows;
  ChatInputLayout input;
  float characterWidth = 0.0F;
  float lineHeight = 0.0F;
};

[[nodiscard]] ChatTextLayout buildChatTextLayout(
  int outputWidth,
  int outputHeight,
  const HudRenderState& hud
);

[[nodiscard]] std::size_t chatInputOffsetAt(
  const ChatTextLayout& layout,
  const std::string& input,
  float x,
  float y
);

[[nodiscard]] ScreenPoint chatInputCursorPosition(
  const ChatTextLayout& layout,
  const std::string& input,
  std::size_t cursor
);

} // namespace lg
