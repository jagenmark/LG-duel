#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace lg {

enum class TextInputFilter {
  Console,
  Chat,
};

struct TextSelection {
  bool active = false;
  std::size_t anchor = 0;
  std::size_t focus = 0;
};

[[nodiscard]] bool isUtf8Boundary(std::string_view text, std::size_t index);
[[nodiscard]] bool isValidUtf8(std::string_view text);
[[nodiscard]] std::size_t clampUtf8Cursor(std::string_view text, std::size_t cursor);
[[nodiscard]] std::size_t previousUtf8Cursor(std::string_view text, std::size_t cursor);
[[nodiscard]] std::size_t nextUtf8Cursor(std::string_view text, std::size_t cursor);
[[nodiscard]] std::size_t utf8GlyphCount(std::string_view text);
[[nodiscard]] std::size_t utf8ByteOffsetForGlyph(std::string_view text, std::size_t glyphIndex);
[[nodiscard]] std::string utf8TrimToByteLimit(std::string_view text, std::size_t byteLimit);

void clearSelection(TextSelection& selection);
[[nodiscard]] bool hasSelection(const TextSelection& selection);
[[nodiscard]] std::size_t selectionBegin(const TextSelection& selection);
[[nodiscard]] std::size_t selectionEnd(const TextSelection& selection);
[[nodiscard]] std::string selectedText(std::string_view input, const TextSelection& selection);
void selectAll(std::string_view input, TextSelection& selection);

void insertText(
  std::string& input,
  std::size_t& cursor,
  std::string_view text,
  TextInputFilter filter,
  std::size_t byteLimit = 0U
);

void pasteText(
  std::string& input,
  std::size_t& cursor,
  std::string_view text,
  TextInputFilter filter,
  std::size_t byteLimit = 0U
);

void replaceSelectionOrInsert(
  std::string& input,
  std::size_t& cursor,
  TextSelection& selection,
  std::string_view text,
  TextInputFilter filter,
  std::size_t byteLimit = 0U
);

void backspaceText(std::string& input, std::size_t& cursor);
void backspaceSelectionOrText(
  std::string& input,
  std::size_t& cursor,
  TextSelection& selection
);
void moveCursorLeft(std::string_view input, std::size_t& cursor);
void moveCursorRight(std::string_view input, std::size_t& cursor);

} // namespace lg
