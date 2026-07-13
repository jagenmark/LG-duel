#include "app/ClientChat.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void expect(bool condition, const char* context) {
  if (condition) {
    return;
  }
  std::cerr << context << '\n';
  std::exit(1);
}

} // namespace

int main() {
  lg::ChatMessage message;
  message.playerIndex = 1;
  message.message = "good luck";
  message.speakerName = "Zap Witch";

  expect(
    lg::chatLine(message) == "Zap Witch: good luck",
    "chat line should use replicated player names"
  );

  message.speakerName.clear();
  expect(
    lg::chatLine(message) == "PLAYER 2: good luck",
    "chat line should fall back when the replicated player name is empty"
  );

  return 0;
}
