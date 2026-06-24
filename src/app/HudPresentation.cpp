#include "app/HudPresentation.hpp"

#include <string_view>

namespace lg {
namespace {

std::size_t teamScoreIndex(Team team) {
  return team == Team::Blue ? 1U : 0U;
}

} // namespace

std::size_t opponentPlayerIndex(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
) {
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (
      index != localPlayerIndex &&
      snapshot.connectedPlayers[index] &&
      (
        snapshot.gameMode == GameMode::Duel ||
        snapshot.teams[index] != snapshot.teams[localPlayerIndex]
      )
    ) {
      return index;
    }
  }
  if (snapshot.botDodgeEnabled) {
    for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
      if (
        index != localPlayerIndex &&
        !snapshot.connectedPlayers[index] &&
        snapshot.players[index].health > 0
      ) {
        return index;
      }
    }
  }
  return localPlayerIndex;
}

std::string hudScoreLine(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
) {
  if (snapshot.gameMode == GameMode::ClanArena) {
    const std::size_t localTeamIndex =
      teamScoreIndex(snapshot.teams[localPlayerIndex]);
    const std::size_t opposingTeamIndex = 1U - localTeamIndex;
    return "SCORE " + std::to_string(snapshot.teamScores[localTeamIndex]) +
      '-' + std::to_string(snapshot.teamScores[opposingTeamIndex]) + " / " +
      std::to_string(snapshot.matchRules.roundLimit);
  }

  std::size_t leaderIndex = 0;
  for (std::size_t index = 1; index < kDuelPlayerCount; ++index) {
    if (snapshot.scores[index] > snapshot.scores[leaderIndex]) {
      leaderIndex = index;
    }
  }
  return "SCORE " + std::to_string(snapshot.scores[localPlayerIndex]) +
    "  LEAD " + std::to_string(snapshot.scores[leaderIndex]) + " / " +
    std::to_string(snapshot.matchRules.roundLimit);
}

bool localPlayerWonResult(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  bool matchResult
) {
  if (snapshot.gameMode == GameMode::ClanArena) {
    const Team winningTeam =
      matchResult ? snapshot.matchWinningTeam : snapshot.roundWinningTeam;
    return isPlayableTeam(winningTeam) &&
      snapshot.teams[localPlayerIndex] == winningTeam;
  }

  const std::uint8_t winner =
    matchResult ? snapshot.matchWinner : snapshot.roundWinner;
  return winner == localPlayerIndex;
}

std::string roundStatsLine(
  std::string_view label,
  const RoundCombatStats& stats
) {
  const std::uint32_t accuracyPercent =
    stats.lightningActiveTicks == 0
    ? 0
    : (
        stats.lightningHitTicks * 100U +
        (stats.lightningActiveTicks / 2U)
      ) / stats.lightningActiveTicks;
  return std::string(label) +
    " LG " + std::to_string(accuracyPercent) +
    "%  DMG " + std::to_string(stats.damageDealt);
}

std::string playerRoundStatsLine(
  const ServerSnapshot& snapshot,
  std::size_t playerIndex
) {
  return roundStatsLine(
    snapshot.playerNames[playerIndex],
    snapshot.roundCombatStats[playerIndex]
  );
}

float matchPhaseMessageOffsetY(MatchPhase phase) {
  constexpr float topMessageOffsetY = -220.0F;
  return phase == MatchPhase::Live ? 0.0F : topMessageOffsetY;
}

} // namespace lg
