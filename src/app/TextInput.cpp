#include "app/TextInput.hpp"

#include <algorithm>
#include <cstdint>

namespace lg {
namespace {

struct Utf8Codepoint {
  std::uint32_t value = 0;
  std::size_t length = 0;
  bool valid = false;
};

[[nodiscard]] bool isContinuation(unsigned char byte) {
  return (byte & 0xC0U) == 0x80U;
}

[[nodiscard]] Utf8Codepoint decodeOne(std::string_view text, std::size_t offset) {
  if (offset >= text.size()) {
    return {};
  }
  const auto byte0 = static_cast<unsigned char>(text[offset]);
  if (byte0 < 0x80U) {
    return {byte0, 1U, true};
  }
  if ((byte0 & 0xE0U) == 0xC0U) {
    if (offset + 1U >= text.size()) {
      return {};
    }
    const auto byte1 = static_cast<unsigned char>(text[offset + 1U]);
    if (!isContinuation(byte1)) {
      return {};
    }
    const std::uint32_t value =
      ((byte0 & 0x1FU) << 6U) | (byte1 & 0x3FU);
    return value >= 0x80U ? Utf8Codepoint{value, 2U, true} : Utf8Codepoint{};
  }
  if ((byte0 & 0xF0U) == 0xE0U) {
    if (offset + 2U >= text.size()) {
      return {};
    }
    const auto byte1 = static_cast<unsigned char>(text[offset + 1U]);
    const auto byte2 = static_cast<unsigned char>(text[offset + 2U]);
    if (!isContinuation(byte1) || !isContinuation(byte2)) {
      return {};
    }
    const std::uint32_t value =
      ((byte0 & 0x0FU) << 12U) |
      ((byte1 & 0x3FU) << 6U) |
      (byte2 & 0x3FU);
    return value >= 0x800U ? Utf8Codepoint{value, 3U, true} : Utf8Codepoint{};
  }
  if ((byte0 & 0xF8U) == 0xF0U) {
    if (offset + 3U >= text.size()) {
      return {};
    }
    const auto byte1 = static_cast<unsigned char>(text[offset + 1U]);
    const auto byte2 = static_cast<unsigned char>(text[offset + 2U]);
    const auto byte3 = static_cast<unsigned char>(text[offset + 3U]);
    if (!isContinuation(byte1) || !isContinuation(byte2) || !isContinuation(byte3)) {
      return {};
    }
    const std::uint32_t value =
      ((byte0 & 0x07U) << 18U) |
      ((byte1 & 0x3FU) << 12U) |
      ((byte2 & 0x3FU) << 6U) |
      (byte3 & 0x3FU);
    return value >= 0x10000U && value <= 0x10FFFFU
      ? Utf8Codepoint{value, 4U, true}
      : Utf8Codepoint{};
  }
  return {};
}

[[nodiscard]] bool isSwedishCodepoint(std::uint32_t value) {
  return value == 0x00E5U || value == 0x00E4U || value == 0x00F6U ||
    value == 0x00C5U || value == 0x00C4U || value == 0x00D6U;
}

[[nodiscard]] bool acceptsCodepoint(TextInputFilter filter, std::uint32_t value) {
  if (value == '\r' || value == '\n') {
    return true;
  }
  if (filter == TextInputFilter::Console) {
    return value >= 0x20U && value != 0x7FU;
  }
  return (value >= 0x20U && value <= 0x7EU) || isSwedishCodepoint(value);
}

void eraseSelection(std::string& input, std::size_t& cursor, TextSelection& selection) {
  if (!hasSelection(selection)) {
    return;
  }
  const std::size_t begin = clampUtf8Cursor(input, selectionBegin(selection));
  const std::size_t end = clampUtf8Cursor(input, selectionEnd(selection));
  if (begin < end) {
    input.erase(begin, end - begin);
    cursor = begin;
  }
  clearSelection(selection);
}

} // namespace

bool isUtf8Boundary(std::string_view text, std::size_t index) {
  if (index > text.size()) {
    return false;
  }
  return index == text.size() ||
    !isContinuation(static_cast<unsigned char>(text[index]));
}

bool isValidUtf8(std::string_view text) {
  std::size_t offset = 0;
  while (offset < text.size()) {
    const Utf8Codepoint codepoint = decodeOne(text, offset);
    if (!codepoint.valid) {
      return false;
    }
    offset += codepoint.length;
  }
  return true;
}

std::size_t clampUtf8Cursor(std::string_view text, std::size_t cursor) {
  cursor = std::min(cursor, text.size());
  while (cursor > 0U && !isUtf8Boundary(text, cursor)) {
    --cursor;
  }
  return cursor;
}

std::size_t previousUtf8Cursor(std::string_view text, std::size_t cursor) {
  cursor = clampUtf8Cursor(text, cursor);
  if (cursor == 0U) {
    return 0U;
  }
  --cursor;
  while (cursor > 0U && !isUtf8Boundary(text, cursor)) {
    --cursor;
  }
  return cursor;
}

std::size_t nextUtf8Cursor(std::string_view text, std::size_t cursor) {
  cursor = clampUtf8Cursor(text, cursor);
  if (cursor >= text.size()) {
    return text.size();
  }
  const Utf8Codepoint codepoint = decodeOne(text, cursor);
  return codepoint.valid ? cursor + codepoint.length : cursor + 1U;
}

std::size_t utf8GlyphCount(std::string_view text) {
  std::size_t count = 0;
  for (std::size_t offset = 0; offset < text.size(); offset = nextUtf8Cursor(text, offset)) {
    ++count;
  }
  return count;
}

std::size_t utf8ByteOffsetForGlyph(std::string_view text, std::size_t glyphIndex) {
  std::size_t offset = 0;
  std::size_t glyph = 0;
  while (offset < text.size() && glyph < glyphIndex) {
    offset = nextUtf8Cursor(text, offset);
    ++glyph;
  }
  return offset;
}

std::string utf8TrimToByteLimit(std::string_view text, std::size_t byteLimit) {
  if (text.size() <= byteLimit && isValidUtf8(text)) {
    return std::string(text);
  }
  std::string result;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const Utf8Codepoint codepoint = decodeOne(text, offset);
    if (!codepoint.valid || result.size() + codepoint.length > byteLimit) {
      break;
    }
    result.append(text.substr(offset, codepoint.length));
    offset += codepoint.length;
  }
  return result;
}

void clearSelection(TextSelection& selection) {
  selection = {};
}

bool hasSelection(const TextSelection& selection) {
  return selection.active && selection.anchor != selection.focus;
}

std::size_t selectionBegin(const TextSelection& selection) {
  return std::min(selection.anchor, selection.focus);
}

std::size_t selectionEnd(const TextSelection& selection) {
  return std::max(selection.anchor, selection.focus);
}

std::string selectedText(std::string_view input, const TextSelection& selection) {
  if (!hasSelection(selection)) {
    return {};
  }
  const std::size_t begin = clampUtf8Cursor(input, selectionBegin(selection));
  const std::size_t end = clampUtf8Cursor(input, selectionEnd(selection));
  return begin < end ? std::string(input.substr(begin, end - begin)) : std::string{};
}

void selectAll(std::string_view input, TextSelection& selection) {
  selection.active = !input.empty();
  selection.anchor = 0U;
  selection.focus = input.size();
}

void insertText(
  std::string& input,
  std::size_t& cursor,
  std::string_view text,
  TextInputFilter filter,
  std::size_t byteLimit
) {
  cursor = clampUtf8Cursor(input, cursor);
  std::string accepted;
  bool pendingLineBreak = false;
  std::size_t offset = 0;
  while (offset < text.size()) {
    const Utf8Codepoint codepoint = decodeOne(text, offset);
    if (!codepoint.valid) {
      break;
    }
    if (codepoint.value == '\r' || codepoint.value == '\n') {
      pendingLineBreak = !input.empty() || !accepted.empty();
      offset += codepoint.length;
      continue;
    }
    if (acceptsCodepoint(filter, codepoint.value)) {
      if (pendingLineBreak) {
        const bool needsSpace =
          codepoint.value != ' ' &&
          codepoint.value != '\t' &&
          (
            (!accepted.empty() && accepted.back() != ' ') ||
            (accepted.empty() && cursor > 0U && input[cursor - 1U] != ' ')
          );
        if (needsSpace && (byteLimit == 0U || input.size() + accepted.size() + 1U <= byteLimit)) {
          accepted.push_back(' ');
        }
        pendingLineBreak = false;
      }
      if (byteLimit != 0U && input.size() + accepted.size() + codepoint.length > byteLimit) {
        break;
      }
      accepted.append(text.substr(offset, codepoint.length));
    }
    offset += codepoint.length;
  }
  input.insert(cursor, accepted);
  cursor += accepted.size();
}

void pasteText(
  std::string& input,
  std::size_t& cursor,
  std::string_view text,
  TextInputFilter filter,
  std::size_t byteLimit
) {
  insertText(input, cursor, text, filter, byteLimit);
}

void replaceSelectionOrInsert(
  std::string& input,
  std::size_t& cursor,
  TextSelection& selection,
  std::string_view text,
  TextInputFilter filter,
  std::size_t byteLimit
) {
  eraseSelection(input, cursor, selection);
  insertText(input, cursor, text, filter, byteLimit);
}

void backspaceText(std::string& input, std::size_t& cursor) {
  cursor = clampUtf8Cursor(input, cursor);
  const std::size_t previous = previousUtf8Cursor(input, cursor);
  if (previous < cursor) {
    input.erase(previous, cursor - previous);
    cursor = previous;
  }
}

void backspaceSelectionOrText(
  std::string& input,
  std::size_t& cursor,
  TextSelection& selection
) {
  if (hasSelection(selection)) {
    eraseSelection(input, cursor, selection);
    return;
  }
  backspaceText(input, cursor);
}

void moveCursorLeft(std::string_view input, std::size_t& cursor) {
  cursor = previousUtf8Cursor(input, cursor);
}

void moveCursorRight(std::string_view input, std::size_t& cursor) {
  cursor = nextUtf8Cursor(input, cursor);
}

} // namespace lg
