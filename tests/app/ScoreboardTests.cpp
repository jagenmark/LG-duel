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
      startsAlign(header, "ACC", localRow, "SR") &&
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

  {
    lg::ServerSnapshot ffa;
    ffa.gameMode = lg::GameMode::FreeForAll;
    ffa.participatingPlayers = {true, true, true, true, false};
    ffa.playerNames = {
      "LEADER",
      "TIED A",
      "TIED B",
      "1234567890123\xC3\xA5",
      "NOT PLAYING",
    };
    ffa.scores = {10, 8, 8, -2, 100};
    setWeaponStats(
      ffa.matchCombatStats[2],
      lg::Weapon::RocketLauncher,
      90,
      5,
      3
    );
    lg::HudRenderState ffaHud;
    lg::populateScoreboard(ffaHud, ffa, 2);
    failures += expect(
      ffaHud.scoreboardOpen && ffaHud.freeForAllScoreboard &&
        ffaHud.scoreboardLines.empty() &&
        ffaHud.freeForAllScoreboardRows.size() == 4,
      "FFA should use sorted structured scoreboard rows for participants only"
    );
    failures += expect(
      ffaHud.freeForAllScoreboardRows[0].playerIndex == 0 &&
        ffaHud.freeForAllScoreboardRows[0].rank == 1 &&
        ffaHud.freeForAllScoreboardRows[1].playerIndex == 1 &&
        ffaHud.freeForAllScoreboardRows[1].rank == 2 &&
        ffaHud.freeForAllScoreboardRows[2].playerIndex == 2 &&
        ffaHud.freeForAllScoreboardRows[2].rank == 2 &&
        ffaHud.freeForAllScoreboardRows[3].playerIndex == 3 &&
        ffaHud.freeForAllScoreboardRows[3].rank == 4,
      "FFA should sort by score then slot and use competition ranks"
    );
    failures += expect(
      ffaHud.freeForAllScoreboardRows[2].localPlayer &&
        ffaHud.freeForAllScoreboardRows[2].score == 8 &&
        ffaHud.freeForAllScoreboardRows[2].accuracyWeapon ==
          lg::Weapon::RocketLauncher &&
        ffaHud.freeForAllScoreboardRows[2].accuracyPercent == 60 &&
        ffaHud.freeForAllScoreboardRows[2].totalDamage == 90 &&
        ffaHud.freeForAllScoreboardRows[3].score == -2,
      "FFA rows should carry the local mark, signed score, accuracy, and damage"
    );
    failures += expect(
      ffaHud.freeForAllScoreboardRows[3].name == "1234567890123",
      "scoreboard clipping should not split a UTF-8 name"
    );

    lg::HudRenderState standing;
    lg::populateFreeForAllStanding(standing, ffa, 0);
    failures += expect(
      standing.freeForAllStandingRows.size() == 1 &&
        standing.freeForAllStandingRows[0].playerIndex == 0 &&
        standing.freeForAllStandingRows[0].localPlayer,
      "a local FFA leader should appear once in the persistent standing"
    );
    standing = {};
    lg::populateFreeForAllStanding(standing, ffa, 3);
    failures += expect(
      standing.freeForAllStandingRows.size() == 2 &&
        standing.freeForAllStandingRows[0].playerIndex == 0 &&
        standing.freeForAllStandingRows[1].playerIndex == 3 &&
        standing.freeForAllStandingRows[1].rank == 4 &&
        standing.freeForAllStandingRows[1].localPlayer,
      "the persistent FFA standing should show the leader and local player"
    );
    standing = {};
    ffa.scores[2] = 10;
    lg::populateFreeForAllStanding(standing, ffa, 2);
    failures += expect(
      standing.freeForAllStandingRows.size() == 2 &&
        standing.freeForAllStandingRows[0].playerIndex == 0 &&
        standing.freeForAllStandingRows[1].playerIndex == 2 &&
        standing.freeForAllStandingRows[0].rank == 1 &&
        standing.freeForAllStandingRows[1].rank == 1,
      "a later-slot local player tied for first should appear beside the chosen leader"
    );
    standing = {};
    lg::populateFreeForAllStanding(standing, ffa, lg::kDuelPlayerCount);
    failures += expect(
      standing.freeForAllStandingRows.size() == 1 &&
        standing.freeForAllStandingRows[0].playerIndex == 0,
      "a spectator should see only the chosen FFA leader"
    );
    standing = {};
    ffa.participatingPlayers = {};
    lg::populateFreeForAllStanding(standing, ffa, 0);
    failures += expect(
      standing.freeForAllStandingRows.empty(),
      "an empty FFA roster should have no persistent standing rows"
    );
  }

  return failures == 0 ? 0 : 1;
}
