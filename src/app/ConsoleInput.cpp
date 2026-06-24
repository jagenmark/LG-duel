#include "app/ConsoleInput.hpp"

#include <algorithm>

namespace lg {

std::size_t clampConsoleCursor(const std::string& input, std::size_t cursorIndex) {
  return std::min(cursorIndex, input.size());
}

void insertConsoleText(
  std::string& input,
  std::size_t& cursorIndex,
  std::string_view text
) {
  cursorIndex = clampConsoleCursor(input, cursorIndex);
  input.insert(cursorIndex, text);
  cursorIndex += text.size();
}

void appendConsolePasteText(
  std::string& input,
  std::size_t& cursorIndex,
  std::string_view text
) {
  bool pendingLineBreak = false;
  for (const char character : text) {
    if (character == '\r' || character == '\n') {
      pendingLineBreak = !input.empty();
      continue;
    }
    if (pendingLineBreak) {
      const bool needsSpace =
        !input.empty() &&
        clampConsoleCursor(input, cursorIndex) > 0U &&
        input[clampConsoleCursor(input, cursorIndex) - 1U] != ' ' &&
        character != ' ' &&
        character != '\t';
      if (needsSpace) {
        insertConsoleText(input, cursorIndex, " ");
      }
      pendingLineBreak = false;
    }
    insertConsoleText(input, cursorIndex, std::string_view(&character, 1U));
  }
}

void appendConsolePasteText(std::string& input, std::string_view text) {
  std::size_t cursorIndex = input.size();
  appendConsolePasteText(input, cursorIndex, text);
}

void backspaceConsoleInput(std::string& input, std::size_t& cursorIndex) {
  cursorIndex = clampConsoleCursor(input, cursorIndex);
  if (cursorIndex == 0U) {
    return;
  }
  input.erase(cursorIndex - 1U, 1U);
  --cursorIndex;
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
  cursorIndex = clampConsoleCursor(input, cursorIndex);
  if (cursorIndex > 0U) {
    --cursorIndex;
  }
}

void moveConsoleCursorRight(const std::string& input, std::size_t& cursorIndex) {
  cursorIndex = clampConsoleCursor(input, cursorIndex);
  if (cursorIndex < input.size()) {
    ++cursorIndex;
  }
}

} // namespace lg
