#include "app/Scoreboard.hpp"

#include "net/NetProtocol.hpp"
#include "render/Renderer.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }

  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool centersAlign(
  const std::string& header,
  std::string_view heading,
  const std::string& row,
  std::string_view value
) {
  const std::size_t headingStart = header.find(heading);
  const std::size_t valueStart = row.find(value, headingStart);
  if (headingStart == std::string::npos || valueStart == std::string::npos) {
    return false;
  }

  const std::size_t headingCenterTwice = headingStart * 2U + heading.size();
  const std::size_t valueCenterTwice = valueStart * 2U + value.size();
  return headingCenterTwice <= valueCenterTwice + 1U &&
    valueCenterTwice <= headingCenterTwice + 1U;
}

std::size_t centerTwice(const std::string& line, std::string_view value) {
  return line.find(value) * 2U + value.size();
}

} // namespace

int main() {
  lg::ServerSnapshot snapshot;
  snapshot.connectedPlayers = {true, true};
  snapshot.participatingPlayers = {true, true};
  snapshot.playerNames = {"LOCAL", "REMOTE", "BOT", "BOT", "BOT", "BOT"};
  snapshot.matchCombatStats[0] = {100, 100, 24};

  lg::HudRenderState hud;
  lg::populateScoreboard(hud, snapshot, 0);

  int failures = 0;
  failures += expect(hud.scoreboardOpen, "populating the scoreboard should open it");
  failures += expect(
    hud.scoreboardLines.size() == 4,
    "disabled bots should leave only connected players on the scoreboard"
  );
  failures += expect(
    hud.scoreboardLineTeams.size() == hud.scoreboardLines.size(),
    "scoreboard team metadata should stay aligned with visible rows"
  );
  const std::string& header = hud.scoreboardLines[1];
  const std::string& localRow = hud.scoreboardLines[2];
  const std::string& remoteRow = hud.scoreboardLines[3];
  const std::size_t scoreToAccuracy =
    centerTwice(header, "ACC") - centerTwice(header, "SCORE");
  const std::size_t accuracyToDamage =
    centerTwice(header, "DAMAGE") - centerTwice(header, "ACC");
  failures += expect(
    scoreToAccuracy <= accuracyToDamage + 1U &&
      accuracyToDamage <= scoreToAccuracy + 1U,
    "stat columns should have equal center spacing within half a glyph"
  );
  failures += expect(
    header.find("NAME") == 2U &&
      localRow[0] == '>' && localRow.find("LOCAL") == 2U &&
      remoteRow.find("REMOTE") == 2U,
    "player names should align under NAME with the local marker in its own gutter"
  );
  failures += expect(
    header.size() <= 43U &&
      localRow.size() == header.size() &&
      remoteRow.size() == header.size(),
    "scoreboard rows should fit inside the panel at the HUD text scale"
  );
  failures += expect(
    centersAlign(header, "SCORE", localRow, "0") &&
      centersAlign(header, "ACC", localRow, "100%") &&
      centersAlign(header, "DAMAGE", localRow, "24"),
    "local stats should be centered under their headings"
  );
  failures += expect(
    centersAlign(header, "SCORE", remoteRow, "0") &&
      centersAlign(header, "ACC", remoteRow, "0%") &&
      centersAlign(header, "DAMAGE", remoteRow, "0"),
    "remote stats should be centered under their headings"
  );

  snapshot.gameMode = lg::GameMode::ClanArena;
  snapshot.teams = {lg::Team::Red, lg::Team::Blue};
  hud = {};
  lg::populateScoreboard(hud, snapshot, 0);
  failures += expect(
    hud.scoreboardLineTeams[2] == lg::Team::Red &&
      hud.scoreboardLineTeams[3] == lg::Team::Blue,
    "Clan Arena scoreboard rows should retain their team colors"
  );
  snapshot.gameMode = lg::GameMode::Duel;

  snapshot.connectedPlayers = {true};
  snapshot.playerNames[0] = std::string(lg::kMaxPlayerNameBytes, 'X');
  hud = {};
  lg::populateScoreboard(hud, snapshot, 0);
  failures += expect(
    hud.scoreboardLines[1].size() <= 43U &&
      hud.scoreboardLines[2].size() == hud.scoreboardLines[1].size(),
    "maximum-length player names should still fit inside the panel"
  );

  snapshot.participatingPlayers = {true, true, true, true, true, true};
  hud = {};
  lg::populateScoreboard(hud, snapshot, 0);
  failures += expect(
    hud.scoreboardLines.size() == 2 + lg::kDuelPlayerCount,
    "all participating players should remain visible on the scoreboard"
  );

  snapshot.botDodgeEnabled = true;
  snapshot.participatingPlayers = {true};
  hud = {};
  lg::populateScoreboard(hud, snapshot, 0);
  failures += expect(
    hud.scoreboardLines.size() == 3,
    "bot settings should not determine scoreboard membership"
  );

  return failures == 0 ? 0 : 1;
}
