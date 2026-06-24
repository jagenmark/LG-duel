#include "render/ConsoleLayout.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace lg {
namespace {

constexpr float kGlyphSize = 8.0F;
constexpr float kConsoleTextScale = 2.0F;
constexpr float kConsoleLineHeight = 20.0F;
constexpr float kConsoleMarginX = 10.0F;

[[nodiscard]] std::vector<std::string> wrapConsoleText(
  const std::string& text,
  std::size_t maxCharacters
) {
  if (maxCharacters == 0U || text.empty()) {
    return {""};
  }

  std::vector<std::string> lines;
  std::size_t lineStart = 0U;
  while (lineStart < text.size()) {
    const std::size_t remaining = text.size() - lineStart;
    if (remaining <= maxCharacters) {
      lines.push_back(text.substr(lineStart));
      break;
    }

    const std::size_t lineEnd = lineStart + maxCharacters;
    std::size_t breakAt = std::string::npos;
    if (lineEnd < text.size() && text[lineEnd] == ' ') {
      breakAt = lineEnd;
    }
    for (
      std::size_t index = lineEnd;
      index > lineStart && breakAt == std::string::npos;
      --index
    ) {
      if (text[index - 1U] == ' ') {
        breakAt = index - 1U;
        break;
      }
    }

    if (breakAt == std::string::npos) {
      lines.push_back(text.substr(lineStart, maxCharacters));
      lineStart += maxCharacters;
    } else {
      lines.push_back(text.substr(lineStart, breakAt - lineStart));
      lineStart = breakAt;
      while (lineStart < text.size() && text[lineStart] == ' ') {
        ++lineStart;
      }
    }
  }

  return lines;
}

void appendLayoutLine(
  ConsoleTextLayout& layout,
  std::string text,
  float x,
  float y,
  bool prompt
) {
  if (!layout.text.empty()) {
    layout.text.push_back('\n');
  }
  const std::size_t textOffset = layout.text.size();
  layout.text += text;
  layout.lines.push_back(ConsoleLayoutLine{
    std::move(text),
    x,
    y,
    textOffset,
    prompt,
  });
}

} // namespace

ConsoleTextLayout buildConsoleTextLayout(
  int outputWidth,
  int outputHeight,
  const ConsoleRenderState& console
) {
  ConsoleTextLayout layout;
  if (!console.open) {
    return layout;
  }

  layout.consoleHeight = static_cast<float>(outputHeight) * 0.55F;
  layout.characterWidth = kGlyphSize * kConsoleTextScale;
  layout.lineHeight = kConsoleLineHeight;

  const std::size_t maxCharacters = std::max(
    1,
    static_cast<int>(
      (static_cast<float>(outputWidth) - kConsoleMarginX * 2.0F) /
      layout.characterWidth
    )
  );

  std::vector<std::string> wrappedOutput;
  for (const std::string& line : console.lines) {
    std::vector<std::string> wrappedLine = wrapConsoleText(line, maxCharacters);
    wrappedOutput.insert(
      wrappedOutput.end(),
      std::make_move_iterator(wrappedLine.begin()),
      std::make_move_iterator(wrappedLine.end())
    );
  }

  const std::vector<std::string> wrappedPrompt =
    wrapConsoleText("] " + console.input, maxCharacters);
  const float promptY =
    layout.consoleHeight - 24.0F -
    static_cast<float>(wrappedPrompt.size() - 1U) * layout.lineHeight;
  const float outputHeightAvailable = std::max(0.0F, promptY - 10.0F);
  const int visibleLines =
    std::max(0, static_cast<int>(outputHeightAvailable / layout.lineHeight));
  const std::size_t firstLine =
    wrappedOutput.size() > static_cast<std::size_t>(visibleLines)
      ? wrappedOutput.size() - static_cast<std::size_t>(visibleLines)
      : 0U;

  float y = 10.0F;
  for (std::size_t index = firstLine; index < wrappedOutput.size(); ++index) {
    appendLayoutLine(layout, wrappedOutput[index], kConsoleMarginX, y, false);
    y += layout.lineHeight;
  }

  y = promptY;
  for (const std::string& line : wrappedPrompt) {
    appendLayoutLine(layout, line, kConsoleMarginX, y, true);
    y += layout.lineHeight;
  }

  return layout;
}

std::size_t consoleTextOffsetAt(
  const ConsoleTextLayout& layout,
  float x,
  float y
) {
  if (layout.lines.empty()) {
    return 0U;
  }

  const ConsoleLayoutLine* chosenLine = &layout.lines.front();
  if (y >= layout.lines.back().y + layout.lineHeight) {
    chosenLine = &layout.lines.back();
  } else {
    for (const ConsoleLayoutLine& line : layout.lines) {
      if (y < line.y + layout.lineHeight) {
        chosenLine = &line;
        break;
      }
    }
  }

  const float relativeX = std::max(0.0F, x - chosenLine->x);
  const auto column = static_cast<std::size_t>(
    std::floor(relativeX / std::max(1.0F, layout.characterWidth))
  );
  return chosenLine->textOffset + std::min(column, chosenLine->text.size());
}

std::string consoleSelectedText(
  const ConsoleTextLayout& layout,
  std::size_t anchor,
  std::size_t focus
) {
  const std::size_t begin = std::min(anchor, focus);
  const std::size_t end = std::min(std::max(anchor, focus), layout.text.size());
  if (begin >= end) {
    return {};
  }
  return layout.text.substr(begin, end - begin);
}

} // namespace lg
