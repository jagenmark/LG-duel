#include "app/ConsoleInput.hpp"
#include "app/TextInput.hpp"
#include "net/NetProtocol.hpp"

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

  input = "åäöÅÄÖ";
  cursorIndex = input.size();
  lg::moveConsoleCursorLeft(input, cursorIndex);
  lg::backspaceConsoleInput(input, cursorIndex);
  expectEqual(input, "åäöÅÖ", "UTF-8 backspace removes a full Swedish codepoint");
  lg::moveConsoleCursorLeft(input, cursorIndex);
  lg::moveConsoleCursorLeft(input, cursorIndex);
  lg::insertConsoleText(input, cursorIndex, "x");
  expectEqual(input, "åäxöÅÖ", "UTF-8 insert uses a codepoint-safe cursor");

  input = "hej ö";
  cursorIndex = input.size();
  lg::TextSelection selection;
  lg::selectAll(input, selection);
  lg::replaceSelectionOrInsert(
    input,
    cursorIndex,
    selection,
    "å",
    lg::TextInputFilter::Chat,
    lg::kMaxChatMessageBytes
  );
  expectEqual(input, "å", "select all replacement keeps UTF-8 intact");

  input = "hej ö";
  cursorIndex = input.size();
  selection = {};
  selection.active = true;
  selection.anchor = 4U;
  selection.focus = input.size();
  lg::replaceSelectionOrInsert(
    input,
    cursorIndex,
    selection,
    "åä",
    lg::TextInputFilter::Chat,
    lg::kMaxChatMessageBytes
  );
  expectEqual(input, "hej åä", "selection replacement accepts Swedish chat text");

  input.assign(lg::kMaxChatMessageBytes - 1U, 'a');
  cursorIndex = input.size();
  lg::pasteText(
    input,
    cursorIndex,
    "åö",
    lg::TextInputFilter::Chat,
    lg::kMaxChatMessageBytes
  );
  expectEqual(
    input,
    std::string(lg::kMaxChatMessageBytes - 1U, 'a'),
    "chat paste rejects partial UTF-8 at byte limit"
  );

  input.assign(lg::kMaxChatMessageBytes - 2U, 'a');
  cursorIndex = input.size();
  lg::pasteText(
    input,
    cursorIndex,
    "åö",
    lg::TextInputFilter::Chat,
    lg::kMaxChatMessageBytes
  );
  expectEqual(
    input,
    std::string(lg::kMaxChatMessageBytes - 2U, 'a') + "å",
    "chat paste truncates on codepoint boundary"
  );
  if (!lg::isValidUtf8(input)) {
    std::cerr << "chat paste result should remain valid UTF-8\n";
    return 1;
  }

  input.assign(80U, 'b');
  cursorIndex = input.size();
  lg::insertText(
    input,
    cursorIndex,
    " longer than the previous chat cap",
    lg::TextInputFilter::Chat,
    lg::kMaxChatMessageBytes
  );
  if (input.size() <= 64U) {
    std::cerr << "chat input should allow messages longer than the old 64-byte limit\n";
    return 1;
  }

  return 0;
}
