#include "app/Scoreboard.hpp"

#include "net/NetProtocol.hpp"
#include "render/Renderer.hpp"

#include <cstdint>
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

bool startsAlign(
  const std::string& header,
  std::string_view heading,
  const std::string& row,
  std::string_view value
) {
  const std::size_t headingStart = header.find(heading);
  const std::size_t valueStart = row.find(value, headingStart);
  return headingStart != std::string::npos && valueStart == headingStart;
}

void setWeaponStats(
  lg::RoundCombatStats& stats,
  lg::Weapon weapon,
  std::uint32_t damage,
  std::uint16_t attempts,
  std::uint16_t hits
) {
  stats.weapons[lg::weaponIndex(weapon)] = {damage, attempts, hits};
}

} // namespace

int main() {
  lg::ServerSnapshot snapshot;
  snapshot.connectedPlayers = {true, true};
  snapshot.participatingPlayers = {true, true};
  snapshot.playerNames = {"LOCAL", "REMOTE", "BOT", "BOT", "BOT", "BOT"};
  setWeaponStats(snapshot.matchCombatStats[0], lg::Weapon::LightningGun, 24, 100, 100);
  setWeaponStats(snapshot.matchCombatStats[0], lg::Weapon::Railgun, 60, 4, 3);

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
  failures += expect(
    startsAlign(header, "SCORE", localRow, "0") &&
      startsAlign(header, "ACC", localRow, "RG") &&
      startsAlign(header, "DAMAGE", localRow, "84"),
    "local stat columns should align by their left edge"
  );
  failures += expect(
    startsAlign(header, "SCORE", remoteRow, "0") &&
      startsAlign(header, "ACC", remoteRow, "LG") &&
      startsAlign(header, "DAMAGE", remoteRow, "0"),
    "remote stat columns should align by their left edge"
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
    hud.scoreboardLineAccuracyWeapons.size() == hud.scoreboardLines.size() &&
      hud.scoreboardLineAccuracyWeaponColumns.size() == hud.scoreboardLines.size() &&
      hud.scoreboardLineAccuracyWeapons[2] == lg::Weapon::Railgun &&
      hud.scoreboardLineAccuracyWeaponColumns[2] == header.find("ACC"),
    "scoreboard should expose the highest-damage weapon abbreviation for colored rendering"
  );

  snapshot.gameMode = lg::GameMode::ClanArena;
  snapshot.teams = {lg::Team::Red, lg::Team::Blue};
  snapshot.scores = {7, 5};
  hud = {};
  lg::populateScoreboard(hud, snapshot, 0);
  failures += expect(
    hud.scoreboardLineTeams[2] == lg::Team::Red &&
      hud.scoreboardLineTeams[3] == lg::Team::Blue,
    "Clan Arena scoreboard rows should retain their team colors"
  );
  failures += expect(
    hud.scoreboardLines[1].find("KILLS") != std::string::npos &&
      hud.scoreboardLines[1].find("SCORE") == std::string::npos &&
      startsAlign(hud.scoreboardLines[1], "KILLS", hud.scoreboardLines[2], "7") &&
      startsAlign(hud.scoreboardLines[1], "KILLS", hud.scoreboardLines[3], "5"),
    "Clan Arena scoreboard should label and display individual kills"
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
  failures += expect(
      hud.scoreboardLines[2].find(std::string(14U, 'X')) == 2U &&
      hud.scoreboardLines[2].find(std::string(15U, 'X')) == std::string::npos &&
      startsAlign(hud.scoreboardLines[1], "SCORE", hud.scoreboardLines[2], "7"),
    "maximum-length player names should be clipped before the score column"
  );

  snapshot.participatingPlayers.fill(true);
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

  snapshot.gameMode = lg::GameMode::McGuffin;
  snapshot.teams[0] = lg::Team::Red;
  snapshot.mcguffinScores = {73, 40};
  hud = {};
  lg::populateScoreboard(hud, snapshot, 0);
  failures += expect(
    hud.scoreboardLines[1].find("TEAM") != std::string::npos &&
      hud.scoreboardLines[2].find("73") != std::string::npos &&
      hud.scoreboardLineTeams[2] == lg::Team::Red,
    "McGuffin scoreboard should show team score and team styling"
  );

  return failures == 0 ? 0 : 1;
}
