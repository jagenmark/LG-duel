#pragma once

#include "shared/Constants.hpp"
#include "sim/GameMode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lg {

[[nodiscard]] bool hasRequiredClanArenaPlayers(
  const std::array<bool, kMaxPlayers>& connectedPlayers
);

[[nodiscard]] bool canStartClanArena(
  const std::array<bool, kMaxPlayers>& connectedPlayers,
  const std::array<bool, kMaxPlayers>& readyPlayers,
  const std::array<Team, kMaxPlayers>& teams
);

[[nodiscard]] bool areClanArenaTeammates(
  const std::array<Team, kMaxPlayers>& teams,
  std::size_t firstPlayerIndex,
  std::size_t secondPlayerIndex
);

[[nodiscard]] bool areClanArenaEnemies(
  const std::array<Team, kMaxPlayers>& teams,
  std::size_t firstPlayerIndex,
  std::size_t secondPlayerIndex
);

[[nodiscard]] std::optional<Team> clanArenaRoundWinner(
  const std::array<bool, kMaxPlayers>& connectedPlayers,
  const std::array<Team, kMaxPlayers>& teams,
  const std::array<bool, kMaxPlayers>& alivePlayers
);

void awardClanArenaRound(
  std::array<std::uint16_t, kPlayableTeamCount>& scores,
  Team winner
);

[[nodiscard]] bool hasWonClanArena(
  const std::array<std::uint16_t, kPlayableTeamCount>& scores,
  Team team,
  std::uint16_t roundLimit
);

[[nodiscard]] std::optional<Team> clanArenaScoreLeader(
  const std::array<std::uint16_t, kPlayableTeamCount>& scores
);

} // namespace lg
