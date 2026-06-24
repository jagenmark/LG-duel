#include "app/Scoreboard.hpp"

#include "net/NetProtocol.hpp"
#include "render/Renderer.hpp"

#include <iostream>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  lg::ServerSnapshot snapshot;
  snapshot.connectedPlayers = {true, true};
  snapshot.playerNames = {"LOCAL", "REMOTE", "BOT", "BOT", "BOT", "BOT"};

  lg::HudRenderState hud;
  lg::populateScoreboard(hud, snapshot, 0);

  int failures = 0;
  failures += expect(hud.scoreboardOpen, "populating the scoreboard should open it");
  failures += expect(
    hud.scoreboardLines.size() == 4,
    "disabled bots should leave only connected players on the scoreboard"
  );

  snapshot.botDodgeEnabled = true;
  hud = {};
  lg::populateScoreboard(hud, snapshot, 0);
  failures += expect(
    hud.scoreboardLines.size() == 2 + lg::kDuelPlayerCount,
    "enabled bots should remain visible on the scoreboard"
  );

  return failures == 0 ? 0 : 1;
}
