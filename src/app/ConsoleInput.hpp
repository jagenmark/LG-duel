#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace lg {

[[nodiscard]] std::size_t clampConsoleCursor(
  const std::string& input,
  std::size_t cursorIndex
);
void insertConsoleText(
  std::string& input,
  std::size_t& cursorIndex,
  std::string_view text
);
void appendConsolePasteText(
  std::string& input,
  std::size_t& cursorIndex,
  std::string_view text
);
void appendConsolePasteText(std::string& input, std::string_view text);
void backspaceConsoleInput(std::string& input, std::size_t& cursorIndex);
[[nodiscard]] std::string consoleCompletionPrefix(
  const std::string& input,
  std::size_t cursorIndex
);
void replaceConsoleCompletion(
  std::string& input,
  std::size_t& cursorIndex,
  std::string_view completion
);
void moveConsoleCursorLeft(const std::string& input, std::size_t& cursorIndex);
void moveConsoleCursorRight(const std::string& input, std::size_t& cursorIndex);

} // namespace lg
