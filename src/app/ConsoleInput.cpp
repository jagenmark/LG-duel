#include "app/ConsoleInput.hpp"

namespace lg {

void appendConsolePasteText(std::string& input, std::string_view text) {
  bool pendingLineBreak = false;
  for (const char character : text) {
    if (character == '\r' || character == '\n') {
      pendingLineBreak = !input.empty();
      continue;
    }
    if (pendingLineBreak) {
      if (!input.empty() && input.back() != ' ' && character != ' ' && character != '\t') {
        input.push_back(' ');
      }
      pendingLineBreak = false;
    }
    input.push_back(character);
  }
}

} // namespace lg
