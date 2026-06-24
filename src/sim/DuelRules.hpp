#pragma once

#include "shared/Constants.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lg {

inline constexpr std::size_t kRequiredDuelPlayers = 2;

[[nodiscard]] bool hasRequiredDuelPlayers(
  const std::array<bool, kMaxPlayers>& connectedPlayers
);

[[nodiscard]] bool canStartDuel(
  const std::array<bool, kMaxPlayers>& connectedPlayers,
  const std::array<bool, kMaxPlayers>& readyPlayers
);

[[nodiscard]] bool areDuelOpponents(
  std::size_t firstPlayerIndex,
  std::size_t secondPlayerIndex
);

[[nodiscard]] std::optional<std::size_t> duelRoundWinner(
  const std::array<bool, kMaxPlayers>& connectedPlayers,
  std::size_t eliminatedPlayerIndex
);

void awardDuelRound(
  std::array<std::uint16_t, kMaxPlayers>& scores,
  std::size_t winnerIndex
);

[[nodiscard]] bool hasWonDuel(
  const std::array<std::uint16_t, kMaxPlayers>& scores,
  std::size_t playerIndex,
  std::uint16_t roundLimit
);

[[nodiscard]] std::optional<std::size_t> duelScoreLeader(
  const std::array<std::uint16_t, kMaxPlayers>& scores,
  const std::array<bool, kMaxPlayers>& connectedPlayers
);

} // namespace lg
