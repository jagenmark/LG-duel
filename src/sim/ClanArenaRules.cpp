#include "sim/ClanArenaRules.hpp"

#include <algorithm>

namespace lg {
namespace {

[[nodiscard]] std::optional<std::size_t> teamScoreIndex(Team team) {
  if (team == Team::Red) {
    return 0;
  }
  if (team == Team::Blue) {
    return 1;
  }
  return std::nullopt;
}

} // namespace

bool hasRequiredClanArenaPlayers(
  const std::array<bool, kMaxPlayers>& connectedPlayers
) {
  return static_cast<std::size_t>(std::count(
    connectedPlayers.begin(),
    connectedPlayers.end(),
    true
  )) >= 2U;
}

bool canStartClanArena(
  const std::array<bool, kMaxPlayers>& connectedPlayers,
  const std::array<bool, kMaxPlayers>& readyPlayers,
  const std::array<Team, kMaxPlayers>& teams
) {
  if (!hasRequiredClanArenaPlayers(connectedPlayers)) {
    return false;
  }

  bool hasRedPlayer = false;
  bool hasBluePlayer = false;
  for (std::size_t index = 0; index < kMaxPlayers; ++index) {
    if (!connectedPlayers[index]) {
      continue;
    }
    if (!readyPlayers[index] || !isPlayableTeam(teams[index])) {
      return false;
    }
    hasRedPlayer = hasRedPlayer || teams[index] == Team::Red;
    hasBluePlayer = hasBluePlayer || teams[index] == Team::Blue;
  }
  return hasRedPlayer && hasBluePlayer;
}

bool areClanArenaTeammates(
  const std::array<Team, kMaxPlayers>& teams,
  std::size_t firstPlayerIndex,
  std::size_t secondPlayerIndex
) {
  return firstPlayerIndex < kMaxPlayers &&
    secondPlayerIndex < kMaxPlayers &&
    firstPlayerIndex != secondPlayerIndex &&
    isPlayableTeam(teams[firstPlayerIndex]) &&
    teams[firstPlayerIndex] == teams[secondPlayerIndex];
}

bool areClanArenaEnemies(
  const std::array<Team, kMaxPlayers>& teams,
  std::size_t firstPlayerIndex,
  std::size_t secondPlayerIndex
) {
  return firstPlayerIndex < kMaxPlayers &&
    secondPlayerIndex < kMaxPlayers &&
    isPlayableTeam(teams[firstPlayerIndex]) &&
    isPlayableTeam(teams[secondPlayerIndex]) &&
    teams[firstPlayerIndex] != teams[secondPlayerIndex];
}

std::optional<Team> clanArenaRoundWinner(
  const std::array<bool, kMaxPlayers>& connectedPlayers,
  const std::array<Team, kMaxPlayers>& teams,
  const std::array<bool, kMaxPlayers>& alivePlayers
) {
  bool hasRedPlayer = false;
  bool hasBluePlayer = false;
  bool redAlive = false;
  bool blueAlive = false;
  for (std::size_t index = 0; index < kMaxPlayers; ++index) {
    if (!connectedPlayers[index]) {
      continue;
    }
    if (teams[index] == Team::Red) {
      hasRedPlayer = true;
      redAlive = redAlive || alivePlayers[index];
    } else if (teams[index] == Team::Blue) {
      hasBluePlayer = true;
      blueAlive = blueAlive || alivePlayers[index];
    }
  }

  if (!hasRedPlayer || !hasBluePlayer || redAlive == blueAlive) {
    return std::nullopt;
  }
  return redAlive ? Team::Red : Team::Blue;
}

void awardClanArenaRound(
  std::array<std::uint16_t, kPlayableTeamCount>& scores,
  Team winner
) {
  const auto index = teamScoreIndex(winner);
  if (index.has_value()) {
    ++scores[*index];
  }
}

bool hasWonClanArena(
  const std::array<std::uint16_t, kPlayableTeamCount>& scores,
  Team team,
  std::uint16_t roundLimit
) {
  const auto index = teamScoreIndex(team);
  return index.has_value() && roundLimit > 0 && scores[*index] >= roundLimit;
}

std::optional<Team> clanArenaScoreLeader(
  const std::array<std::uint16_t, kPlayableTeamCount>& scores
) {
  if (scores[0] == scores[1]) {
    return std::nullopt;
  }
  return scores[0] > scores[1] ? Team::Red : Team::Blue;
}

} // namespace lg
