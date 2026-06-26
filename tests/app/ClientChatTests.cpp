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
  lg::ServerSnapshot snapshot;
  snapshot.chatPlayerIndex = 1;
  snapshot.chatMessage = "good luck";
  snapshot.playerNames[1] = "Zap Witch";

  expect(
    lg::chatLine(snapshot) == "Zap Witch: good luck",
    "chat line should use replicated player names"
  );

  snapshot.playerNames[1].clear();
  expect(
    lg::chatLine(snapshot) == "PLAYER 2: good luck",
    "chat line should fall back when the replicated player name is empty"
  );

  return 0;
}
