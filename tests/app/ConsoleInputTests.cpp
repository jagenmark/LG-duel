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

  return 0;
}
