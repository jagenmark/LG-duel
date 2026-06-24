#include "app/ConsoleInput.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expectEqual(const std::string& actual, const std::string& expected, const char* context) {
  if (actual == expected) {
    return;
  }
  std::cerr << context << ": expected `" << expected << "`, got `" << actual << "`\n";
  std::exit(1);
}

} // namespace

int main() {
  std::string input = "g_";
  lg::appendConsolePasteText(input, "flight 1");
  expectEqual(input, "g_flight 1", "single-line paste appends text");

  input = "set";
  lg::appendConsolePasteText(input, "\r\n g_speed\n320");
  expectEqual(input, "set g_speed 320", "multi-line paste is flattened");

  input.clear();
  lg::appendConsolePasteText(input, "\ncmd\r\nlist");
  expectEqual(input, "cmd list", "leading and CRLF newlines do not add extra spaces");

  input = "connect 127.0.0.1:7777";
  expectEqual(
    lg::consoleInputClipboardText(input),
    "connect 127.0.0.1:7777",
    "console copy uses the active input line"
  );

  input.clear();
  expectEqual(lg::consoleInputClipboardText(input), "", "empty console input copies empty text");

  input = "set 320";
  std::size_t cursorIndex = input.size();
  lg::moveConsoleCursorLeft(input, cursorIndex);
  lg::moveConsoleCursorLeft(input, cursorIndex);
  lg::moveConsoleCursorLeft(input, cursorIndex);
  lg::insertConsoleText(input, cursorIndex, "g_speed ");
  expectEqual(input, "set g_speed 320", "text input inserts at cursor");

  lg::backspaceConsoleInput(input, cursorIndex);
  expectEqual(input, "set g_speed320", "backspace removes character before cursor");

  input = "cmd suffix";
  cursorIndex = 3U;
  lg::appendConsolePasteText(input, cursorIndex, "\nlist");
  expectEqual(input, "cmd list suffix", "paste inserts flattened text at cursor");
  if (cursorIndex != 8U) {
    std::cerr << "paste updates cursor: expected `8`, got `" << cursorIndex << "`\n";
    return 1;
  }

  input = "r_vsync 0";
  cursorIndex = 3U;
  expectEqual(
    lg::consoleCompletionPrefix(input, cursorIndex),
    "r_v",
    "completion prefix stops at cursor"
  );
  lg::replaceConsoleCompletion(input, cursorIndex, "r_vsync");
  expectEqual(input, "r_vsync 0", "completion replaces full token at cursor");
  if (cursorIndex != 7U) {
    std::cerr << "completion updates cursor: expected `7`, got `" << cursorIndex << "`\n";
    return 1;
  }

  return 0;
}
