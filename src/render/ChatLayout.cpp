#include "render/ChatLayout.hpp"

#include "app/TextInput.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace lg {
namespace {

constexpr float kGlyphSize = 8.0F;
constexpr float kChatTextScale = 2.0F;
constexpr float kChatLineHeight = 18.0F;
constexpr float kChatX = 16.0F;
constexpr float kChatBottomOffset = 150.0F;
constexpr float kChatInputBottomOffset = 125.0F;
constexpr std::size_t kMaxVisibleInputRows = 4U;

[[nodiscard]] std::string spaces(std::size_t count) {
  return std::string(count, ' ');
}

[[nodiscard]] std::string prefixFor(
  std::uint8_t playerIndex,
  const std::string& speakerName
) {
  if (!speakerName.empty()) {
    return speakerName + ": ";
  }
  return "PLAYER " + std::to_string(static_cast<unsigned>(playerIndex) + 1U) + ": ";
}

struct WrappedRow {
  std::string text;
  std::size_t begin = 0;
  std::size_t end = 0;
  std::size_t contentColumn = 0;
  bool continuation = false;
};

[[nodiscard]] std::size_t byteOffsetForGlyphs(
  std::string_view text,
  std::size_t begin,
  std::size_t end,
  std::size_t glyphs
) {
  std::size_t offset = begin;
  std::size_t count = 0;
  while (offset < end && count < glyphs) {
    offset = std::min(nextUtf8Cursor(text, offset), end);
    ++count;
  }
  return offset;
}

[[nodiscard]] std::vector<WrappedRow> wrapText(
  const std::string& prefix,
  const std::string& text,
  std::size_t rowColumns
) {
  if (rowColumns == 0U) {
    return {};
  }
  const std::size_t prefixColumns = utf8GlyphCount(prefix);
  const std::size_t continuationColumns = std::min(prefixColumns, rowColumns - 1U);
  std::vector<WrappedRow> rows;
  std::size_t offset = 0;
  bool first = true;
  while (offset < text.size() || first) {
    const std::size_t indentColumns = first ? prefixColumns : continuationColumns;
    const std::size_t availableColumns = std::max<std::size_t>(
      1U,
      rowColumns > indentColumns ? rowColumns - indentColumns : 1U
    );
    std::size_t lineEnd = offset;
    std::size_t lastSpace = std::string::npos;
    std::size_t columns = 0;
    while (lineEnd < text.size() && columns < availableColumns) {
      const std::size_t next = nextUtf8Cursor(text, lineEnd);
      if (text[lineEnd] == ' ') {
        lastSpace = lineEnd;
      }
      lineEnd = next;
      ++columns;
    }
    std::size_t breakEnd = lineEnd;
    if (lineEnd < text.size() && lastSpace != std::string::npos && lastSpace > offset) {
      breakEnd = lastSpace;
    }
    const std::string rowText = text.substr(offset, breakEnd - offset);
    rows.push_back(WrappedRow{
      (first ? prefix : spaces(continuationColumns)) + rowText,
      offset,
      breakEnd,
      first ? prefixColumns : continuationColumns,
      !first,
    });
    offset = breakEnd;
    while (offset < text.size() && text[offset] == ' ') {
      offset = nextUtf8Cursor(text, offset);
    }
    first = false;
  }
  return rows;
}

[[nodiscard]] std::size_t inputCursorRowIndex(
  const std::vector<WrappedRow>& rows,
  std::size_t cursor
) {
  for (std::size_t index = 0; index < rows.size(); ++index) {
    const WrappedRow& row = rows[index];
    if (cursor >= row.begin && cursor < row.end) {
      return index;
    }
    if (cursor == row.end) {
      if (index + 1U < rows.size() && rows[index + 1U].begin == row.end) {
        continue;
      }
      return index;
    }
  }
  return rows.empty() ? 0U : rows.size() - 1U;
}

} // namespace

ChatTextLayout buildChatTextLayout(
  int outputWidth,
  int outputHeight,
  const HudRenderState& hud
) {
  ChatTextLayout layout;
  layout.characterWidth = kGlyphSize * kChatTextScale;
  layout.lineHeight = kChatLineHeight;
  layout.input = {
    kChatX,
    static_cast<float>(outputHeight) - kChatInputBottomOffset,
    layout.characterWidth,
    layout.lineHeight,
    "CHAT: ",
  };
  const std::size_t rowColumns = std::max(
    1,
    static_cast<int>(
      (static_cast<float>(outputWidth) - kChatX * 2.0F) / layout.characterWidth
    )
  );
  std::vector<ChatLayoutRow> rows;
  for (std::size_t messageIndex = 0; messageIndex < hud.chatLines.size(); ++messageIndex) {
    const HudRenderState::ChatLine& line = hud.chatLines[messageIndex];
    const std::string prefix = prefixFor(line.playerIndex, line.speakerName);
    std::vector<WrappedRow> wrapped = wrapText(prefix, line.message, rowColumns);
    for (std::size_t rowIndex = 0; rowIndex < wrapped.size(); ++rowIndex) {
      rows.push_back(ChatLayoutRow{
          std::move(wrapped[rowIndex].text),
          kChatX,
          0.0F,
          0U,
          messageIndex,
          rowIndex > 0U,
      });
    }
  }

  std::vector<WrappedRow> inputRows;
  if (hud.chatInputOpen) {
    inputRows = wrapText(layout.input.prompt, hud.chatInput, rowColumns);
    const std::size_t cursorRow =
      inputCursorRowIndex(inputRows, clampUtf8Cursor(hud.chatInput, hud.chatCursorIndex));
    const std::size_t visibleInputRows =
      std::min(kMaxVisibleInputRows, inputRows.size());
    std::size_t firstInputRow = 0U;
    if (cursorRow + 1U > visibleInputRows) {
      firstInputRow = cursorRow + 1U - visibleInputRows;
    }
    if (firstInputRow + visibleInputRows > inputRows.size()) {
      firstInputRow = inputRows.size() - visibleInputRows;
    }
    const float inputBottom =
      static_cast<float>(outputHeight) - kChatInputBottomOffset + layout.lineHeight;
    float inputY =
      inputBottom - static_cast<float>(visibleInputRows) * layout.lineHeight;
    layout.input.y = inputY;
    for (
      std::size_t index = firstInputRow;
      index < firstInputRow + visibleInputRows;
      ++index
    ) {
      layout.inputRows.push_back(ChatInputRow{
        std::move(inputRows[index].text),
        kChatX,
        inputY,
        inputRows[index].begin,
        inputRows[index].end,
        inputRows[index].contentColumn,
        inputRows[index].continuation,
      });
      inputY += layout.lineHeight;
    }
  }

  const float historyBottom = hud.chatInputOpen
    ? layout.input.y - 7.0F
    : static_cast<float>(outputHeight) - kChatBottomOffset;
  const int visibleRows = std::max(0, static_cast<int>(144.0F / layout.lineHeight));
  const std::size_t visibleRowCount = static_cast<std::size_t>(visibleRows);
  const std::size_t maxScroll = rows.size() > visibleRowCount
    ? rows.size() - visibleRowCount
    : 0U;
  const std::size_t scrollRows = std::min(hud.chatScrollRows, maxScroll);
  const std::size_t endRow = rows.size() - scrollRows;
  const std::size_t firstRow = endRow > visibleRowCount
    ? endRow - visibleRowCount
    : 0U;
  const std::size_t visibleCount = endRow - firstRow;
  layout.historyTop =
    historyBottom - static_cast<float>(visibleRowCount) * layout.lineHeight;
  layout.historyBottom = historyBottom;
  layout.historyRight = kChatX;
  for (const ChatLayoutRow& row : rows) {
    layout.historyRight = std::max(
      layout.historyRight,
      row.x + static_cast<float>(utf8GlyphCount(row.text)) *
        layout.characterWidth
    );
  }
  layout.totalHistoryRows = rows.size();
  layout.firstVisibleHistoryRow = firstRow;
  layout.visibleHistoryRows = visibleCount;
  layout.maxScrollRows = maxScroll;
  float y = historyBottom - static_cast<float>(visibleCount) * layout.lineHeight;
  for (std::size_t index = firstRow; index < endRow; ++index) {
    rows[index].y = y;
    if (!layout.historyText.empty()) {
      layout.historyText.push_back('\n');
    }
    rows[index].textOffset = layout.historyText.size();
    layout.historyText += rows[index].text;
    layout.rows.push_back(std::move(rows[index]));
    y += layout.lineHeight;
  }
  return layout;
}

std::size_t chatInputOffsetAt(
  const ChatTextLayout& layout,
  const std::string& input,
  float x,
  float y
) {
  if (layout.inputRows.empty()) {
    return 0U;
  }
  const ChatInputRow* chosenRow = &layout.inputRows.front();
  if (y >= layout.inputRows.back().y + layout.input.lineHeight) {
    chosenRow = &layout.inputRows.back();
  } else {
    for (const ChatInputRow& row : layout.inputRows) {
      if (y < row.y + layout.input.lineHeight) {
        chosenRow = &row;
        break;
      }
    }
  }
  const float relativeX = std::max(0.0F, x - chosenRow->x);
  const auto column = static_cast<std::size_t>(
    std::floor(relativeX / std::max(1.0F, layout.input.characterWidth) + 0.5F)
  );
  if (column <= chosenRow->contentColumn) {
    return chosenRow->inputBegin;
  }
  return byteOffsetForGlyphs(
    input,
    chosenRow->inputBegin,
    chosenRow->inputEnd,
    column - chosenRow->contentColumn
  );
}

ScreenPoint chatInputCursorPosition(
  const ChatTextLayout& layout,
  const std::string& input,
  std::size_t cursor
) {
  cursor = clampUtf8Cursor(input, cursor);
  if (layout.inputRows.empty()) {
    return {layout.input.x, layout.input.y};
  }
  const ChatInputRow* chosenRow = &layout.inputRows.back();
  for (const ChatInputRow& row : layout.inputRows) {
    if (cursor >= row.inputBegin && cursor <= row.inputEnd) {
      chosenRow = &row;
      break;
    }
  }
  const std::size_t offset = std::clamp(
    cursor,
    chosenRow->inputBegin,
    chosenRow->inputEnd
  );
  const float column =
    static_cast<float>(
      chosenRow->contentColumn +
      utf8GlyphCount(input.substr(chosenRow->inputBegin, offset - chosenRow->inputBegin))
    );
  return {
    chosenRow->x + column * layout.input.characterWidth,
    chosenRow->y,
  };
}

std::size_t chatHistoryTextOffsetAt(const ChatTextLayout &layout, float x,
                                    float y) {
  if (layout.rows.empty()) {
    return 0U;
  }
  const ChatLayoutRow *chosenRow = &layout.rows.front();
  if (y >= layout.rows.back().y + layout.lineHeight) {
    chosenRow = &layout.rows.back();
  } else {
    for (const ChatLayoutRow &row : layout.rows) {
      if (y < row.y + layout.lineHeight) {
        chosenRow = &row;
        break;
      }
    }
  }
  const float relativeX = std::max(0.0F, x - chosenRow->x);
  const auto column = static_cast<std::size_t>(
      std::floor(relativeX / std::max(1.0F, layout.characterWidth) + 0.5F));
  return chosenRow->textOffset + byteOffsetForGlyphs(chosenRow->text, 0U,
                                                     chosenRow->text.size(),
                                                     column);
}

std::string chatHistorySelectedText(const ChatTextLayout &layout,
                                    std::size_t anchor, std::size_t focus) {
  const std::size_t begin = std::min(anchor, focus);
  const std::size_t end =
      std::min(std::max(anchor, focus), layout.historyText.size());
  if (begin >= end) {
    return {};
  }
  return layout.historyText.substr(begin, end - begin);
}

} // namespace lg
