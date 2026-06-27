#include "app/ConsoleInput.hpp"
#include "app/TextInput.hpp"

#include <algorithm>

namespace lg {

std::size_t clampConsoleCursor(const std::string& input, std::size_t cursorIndex) {
  return clampUtf8Cursor(input, cursorIndex);
}

void insertConsoleText(
  std::string& input,
  std::size_t& cursorIndex,
  std::string_view text
) {
  insertText(input, cursorIndex, text, TextInputFilter::Console);
}

void appendConsolePasteText(
  std::string& input,
  std::size_t& cursorIndex,
  std::string_view text
) {
  pasteText(input, cursorIndex, text, TextInputFilter::Console);
}

void appendConsolePasteText(std::string& input, std::string_view text) {
  std::size_t cursorIndex = input.size();
  appendConsolePasteText(input, cursorIndex, text);
}

void backspaceConsoleInput(std::string& input, std::size_t& cursorIndex) {
  backspaceText(input, cursorIndex);
}

std::string consoleCompletionPrefix(const std::string& input, std::size_t cursorIndex) {
  cursorIndex = clampConsoleCursor(input, cursorIndex);
  const std::size_t wordStart = cursorIndex == 0U
    ? std::string::npos
    : input.find_last_of(" \t", cursorIndex - 1U);
  const std::size_t prefixStart =
    wordStart == std::string::npos ? 0U : wordStart + 1U;
  return input.substr(prefixStart, cursorIndex - prefixStart);
}

void replaceConsoleCompletion(
  std::string& input,
  std::size_t& cursorIndex,
  std::string_view completion
) {
  cursorIndex = clampConsoleCursor(input, cursorIndex);
  const std::size_t wordStart = cursorIndex == 0U
    ? std::string::npos
    : input.find_last_of(" \t", cursorIndex - 1U);
  const std::size_t prefixStart =
    wordStart == std::string::npos ? 0U : wordStart + 1U;
  const std::size_t wordEnd = input.find_first_of(" \t", cursorIndex);
  const std::size_t replaceEnd = wordEnd == std::string::npos ? input.size() : wordEnd;
  input.replace(prefixStart, replaceEnd - prefixStart, completion);
  cursorIndex = prefixStart + completion.size();
}

void moveConsoleCursorLeft(const std::string& input, std::size_t& cursorIndex) {
  moveCursorLeft(input, cursorIndex);
}

void moveConsoleCursorRight(const std::string& input, std::size_t& cursorIndex) {
  moveCursorRight(input, cursorIndex);
}

std::string consoleInputClipboardText(std::string_view input) {
  return std::string(input);
}

} // namespace lg
