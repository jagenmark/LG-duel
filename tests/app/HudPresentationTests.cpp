#include "app/HudPresentation.hpp"

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
  snapshot.gameMode = lg::GameMode::ClanArena;
  snapshot.connectedPlayers = {true, true, true};
  snapshot.teams = {lg::Team::Red, lg::Team::Red, lg::Team::Blue};
  snapshot.teamScores = {4, 2};
  snapshot.roundWinningTeam = lg::Team::Red;
  snapshot.matchWinningTeam = lg::Team::Red;
  snapshot.playerNames[2] = "ENEMY";

  int failures = 0;
  failures += expect(
    lg::MatchRules{}.roundEndTicks == 625,
    "the default round-end phase should last five seconds"
  );
  failures += expect(
    lg::matchPhaseMessageOffsetY(lg::MatchPhase::WaitingForPlayers) == -220.0F &&
      lg::matchPhaseMessageOffsetY(lg::MatchPhase::WaitingForReady) == -220.0F &&
      lg::matchPhaseMessageOffsetY(lg::MatchPhase::Countdown) == -220.0F &&
      lg::matchPhaseMessageOffsetY(lg::MatchPhase::RoundEnd) == -220.0F &&
      lg::matchPhaseMessageOffsetY(lg::MatchPhase::MatchEnd) == -220.0F,
    "all non-live phase messages should share the warmup banner position"
  );
  failures += expect(
    lg::matchPhaseMessageOffsetY(lg::MatchPhase::Live) == 0.0F,
    "live play should not apply a phase-message offset"
  );
  failures += expect(
    lg::hudScoreLine(snapshot, 0) == "SCORE 4-2 / 10",
    "Clan Arena HUD score should use team scores"
  );
  failures += expect(
    lg::localPlayerWonResult(snapshot, 0, false),
    "Clan Arena round result should use the winning team"
  );
  failures += expect(
    lg::localPlayerWonResult(snapshot, 0, true),
    "Clan Arena match result should use the winning team"
  );
  failures += expect(
    lg::opponentPlayerIndex(snapshot, 0) == 2,
    "Clan Arena post-round stats should select an enemy, not a teammate"
  );
  failures += expect(
    lg::playerRoundStatsLine(snapshot, 2) == "ENEMY LG 0%  DMG 0",
    "post-round stats should display the enemy player name"
  );

  snapshot.gameMode = lg::GameMode::Duel;
  snapshot.scores = {3, 5};
  snapshot.roundWinner = 0;
  snapshot.matchWinner = 1;
  failures += expect(
    lg::hudScoreLine(snapshot, 0) == "SCORE 3  LEAD 5 / 10",
    "duel HUD score behavior should remain unchanged"
  );
  failures += expect(
    lg::localPlayerWonResult(snapshot, 0, false) &&
      !lg::localPlayerWonResult(snapshot, 0, true),
    "duel result behavior should remain unchanged"
  );

  lg::ServerSnapshot botSnapshot;
  botSnapshot.connectedPlayers = {true};
  botSnapshot.participatingPlayers = {true, true};
  failures += expect(
    lg::opponentPlayerIndex(botSnapshot, 0) == 1,
    "HUD opponent selection should use the authoritative player roster"
  );
  botSnapshot.participatingPlayers = {true};
  botSnapshot.botDodgeEnabled = true;
  failures += expect(
    lg::opponentPlayerIndex(botSnapshot, 0) == 0,
    "bot settings alone should not create a HUD opponent"
  );

  return failures == 0 ? 0 : 1;
}
