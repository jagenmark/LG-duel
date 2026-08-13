#include "sim/DuelRules.hpp"

#include <algorithm>
#include <limits>

namespace lg {

bool hasRequiredDuelPlayers(
  const std::array<bool, kMaxPlayers>& connectedPlayers
) {
  return static_cast<std::size_t>(std::count(
    connectedPlayers.begin(),
    connectedPlayers.end(),
    true
  )) == kRequiredDuelPlayers;
}

bool canStartDuel(
  const std::array<bool, kMaxPlayers>& connectedPlayers,
  const std::array<bool, kMaxPlayers>& readyPlayers
) {
  if (!hasRequiredDuelPlayers(connectedPlayers)) {
    return false;
  }

  for (std::size_t index = 0; index < kMaxPlayers; ++index) {
    if (connectedPlayers[index] && !readyPlayers[index]) {
      return false;
    }
  }
  return true;
}

bool areDuelOpponents(
  std::size_t firstPlayerIndex,
  std::size_t secondPlayerIndex
) {
  return firstPlayerIndex < kMaxPlayers &&
    secondPlayerIndex < kMaxPlayers &&
    firstPlayerIndex != secondPlayerIndex;
}

std::optional<std::size_t> duelRoundWinner(
  const std::array<bool, kMaxPlayers>& connectedPlayers,
  std::size_t eliminatedPlayerIndex
) {
  if (
    !hasRequiredDuelPlayers(connectedPlayers) ||
    eliminatedPlayerIndex >= kMaxPlayers ||
    !connectedPlayers[eliminatedPlayerIndex]
  ) {
    return std::nullopt;
  }

  for (std::size_t index = 0; index < kMaxPlayers; ++index) {
    if (
      connectedPlayers[index] &&
      areDuelOpponents(index, eliminatedPlayerIndex)
    ) {
      return index;
    }
  }
  return std::nullopt;
}

void awardDuelRound(
  std::array<PlayerScore, kMaxPlayers>& scores,
  std::size_t winnerIndex
) {
  if (
    winnerIndex < kMaxPlayers &&
    scores[winnerIndex] < std::numeric_limits<PlayerScore>::max()
  ) {
    ++scores[winnerIndex];
  }
}

bool hasWonDuel(
  const std::array<PlayerScore, kMaxPlayers>& scores,
  std::size_t playerIndex,
  std::uint16_t roundLimit
) {
  return playerIndex < kMaxPlayers &&
    roundLimit > 0 &&
    scores[playerIndex] >= roundLimit;
}

std::optional<std::size_t> duelScoreLeader(
  const std::array<PlayerScore, kMaxPlayers>& scores,
  const std::array<bool, kMaxPlayers>& connectedPlayers
) {
  if (!hasRequiredDuelPlayers(connectedPlayers)) {
    return std::nullopt;
  }

  std::optional<std::size_t> leader;
  bool tied = false;
  for (std::size_t index = 0; index < kMaxPlayers; ++index) {
    if (!connectedPlayers[index]) {
      continue;
    }
    if (!leader.has_value() || scores[index] > scores[*leader]) {
      leader = index;
      tied = false;
    } else if (scores[index] == scores[*leader]) {
      tied = true;
    }
  }
  return tied ? std::nullopt : leader;
}

} // namespace lg
