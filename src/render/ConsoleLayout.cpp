#include "render/ConsoleLayout.hpp"

#include "app/TextInput.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace lg {
namespace {

constexpr float kGlyphSize = 8.0F;
constexpr float kConsoleTextScale = 2.0F;
constexpr float kConsoleLineHeight = 20.0F;
constexpr float kConsoleMarginX = 10.0F;

struct WrappedPromptRow {
  std::string text;
  std::size_t inputBegin = 0U;
  std::size_t inputEnd = 0U;
  std::size_t contentColumn = 0U;
};

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

[[nodiscard]] std::vector<WrappedPromptRow>
wrapConsolePrompt(const std::string &input, std::size_t maxCharacters) {
  constexpr std::string_view prompt = "] ";
  const std::size_t promptColumns = utf8GlyphCount(prompt);
  std::vector<WrappedPromptRow> rows;
  std::size_t offset = 0U;
  bool first = true;
  while (offset < input.size() || first) {
    const std::size_t contentColumn = first ? promptColumns : 0U;
    const std::size_t availableColumns = std::max<std::size_t>(
        1U, maxCharacters > contentColumn ? maxCharacters - contentColumn : 1U);
    std::size_t lineEnd = offset;
    std::size_t lastSpace = std::string::npos;
    std::size_t columns = 0U;
    while (lineEnd < input.size() && columns < availableColumns) {
      const std::size_t next = nextUtf8Cursor(input, lineEnd);
      if (input[lineEnd] == ' ') {
        lastSpace = lineEnd;
      }
      lineEnd = next;
      ++columns;
    }
    std::size_t breakEnd = lineEnd;
    if (lineEnd < input.size() && lastSpace != std::string::npos &&
        lastSpace > offset) {
      breakEnd = lastSpace;
    }
    rows.push_back(WrappedPromptRow{
        (first ? std::string(prompt) : std::string{}) +
            input.substr(offset, breakEnd - offset),
        offset,
        breakEnd,
        contentColumn,
    });
    offset = breakEnd;
    while (offset < input.size() && input[offset] == ' ') {
      offset = nextUtf8Cursor(input, offset);
    }
    first = false;
  }
  return rows;
}

void appendLayoutLine(ConsoleTextLayout &layout, std::string text, float x,
                      float y, bool prompt, std::size_t inputBegin = 0U,
                      std::size_t inputEnd = 0U,
                      std::size_t contentColumn = 0U) {
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
      inputBegin,
      inputEnd,
      contentColumn,
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

  const std::vector<WrappedPromptRow> wrappedPrompt =
      wrapConsolePrompt(console.input, maxCharacters);
  const float promptY =
    layout.consoleHeight - 24.0F -
    static_cast<float>(wrappedPrompt.size() - 1U) * layout.lineHeight;
  const float outputHeightAvailable = std::max(0.0F, promptY - 10.0F);
  const int visibleLines =
    std::max(0, static_cast<int>(outputHeightAvailable / layout.lineHeight));
  const std::size_t visibleLineCount = static_cast<std::size_t>(visibleLines);
  const std::size_t maxScrollRows = wrappedOutput.size() > visibleLineCount
    ? wrappedOutput.size() - visibleLineCount
    : 0U;
  layout.maxScrollRows = maxScrollRows;
  const std::size_t scrollRows = std::min(console.scrollRows, maxScrollRows);
  const std::size_t firstLine = maxScrollRows - scrollRows;
  const std::size_t lastLine = std::min(
    wrappedOutput.size(),
    firstLine + visibleLineCount
  );

  float y = 10.0F;
  for (std::size_t index = firstLine; index < lastLine; ++index) {
    appendLayoutLine(layout, wrappedOutput[index], kConsoleMarginX, y, false);
    y += layout.lineHeight;
  }

  y = promptY;
  for (const WrappedPromptRow &line : wrappedPrompt) {
    appendLayoutLine(layout, line.text, kConsoleMarginX, y, true,
                     line.inputBegin, line.inputEnd, line.contentColumn);
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

std::size_t consoleInputOffsetAt(const ConsoleTextLayout &layout,
                                 const std::string &input, float x, float y) {
  const ConsoleLayoutLine *firstPrompt = nullptr;
  const ConsoleLayoutLine *lastPrompt = nullptr;
  for (const ConsoleLayoutLine &line : layout.lines) {
    if (!line.prompt) {
      continue;
    }
    if (firstPrompt == nullptr) {
      firstPrompt = &line;
    }
    lastPrompt = &line;
  }
  if (firstPrompt == nullptr || lastPrompt == nullptr) {
    return 0U;
  }
  const ConsoleLayoutLine *chosenLine = firstPrompt;
  if (y >= lastPrompt->y + layout.lineHeight) {
    chosenLine = lastPrompt;
  } else {
    for (const ConsoleLayoutLine &line : layout.lines) {
      if (line.prompt && y < line.y + layout.lineHeight) {
        chosenLine = &line;
        break;
      }
    }
  }
  const float relativeX = std::max(0.0F, x - chosenLine->x);
  const auto column = static_cast<std::size_t>(
      std::floor(relativeX / std::max(1.0F, layout.characterWidth) + 0.5F));
  if (column <= chosenLine->contentColumn) {
    return chosenLine->inputBegin;
  }
  std::size_t offset = chosenLine->inputBegin;
  std::size_t glyph = 0U;
  while (offset < chosenLine->inputEnd &&
         glyph < column - chosenLine->contentColumn) {
    offset = std::min(nextUtf8Cursor(input, offset), chosenLine->inputEnd);
    ++glyph;
  }
  return offset;
}

ScreenPoint consoleInputCursorPosition(const ConsoleTextLayout &layout,
                                       const std::string &input,
                                       std::size_t cursor) {
  cursor = clampUtf8Cursor(input, cursor);
  const ConsoleLayoutLine *chosenLine = nullptr;
  for (const ConsoleLayoutLine &line : layout.lines) {
    if (!line.prompt) {
      continue;
    }
    chosenLine = &line;
    if (cursor >= line.inputBegin && cursor <= line.inputEnd) {
      break;
    }
  }
  if (chosenLine == nullptr) {
    return {kConsoleMarginX, 0.0F};
  }
  const std::size_t offset =
      std::clamp(cursor, chosenLine->inputBegin, chosenLine->inputEnd);
  const float column = static_cast<float>(
      chosenLine->contentColumn +
      utf8GlyphCount(input.substr(chosenLine->inputBegin,
                                  offset - chosenLine->inputBegin)));
  return {
      chosenLine->x + column * layout.characterWidth,
      chosenLine->y,
  };
}

} // namespace lg
