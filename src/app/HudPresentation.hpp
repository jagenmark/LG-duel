#pragma once

#include "net/NetProtocol.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace lg {

[[nodiscard]] std::size_t opponentPlayerIndex(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
);

[[nodiscard]] std::string hudScoreLine(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex
);

[[nodiscard]] bool localPlayerWonResult(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  bool matchResult
);

[[nodiscard]] std::string roundStatsLine(
  std::string_view label,
  const RoundCombatStats& stats
);

[[nodiscard]] std::string playerRoundStatsLine(
  const ServerSnapshot& snapshot,
  std::size_t playerIndex
);

[[nodiscard]] float matchPhaseMessageOffsetY(MatchPhase phase);

} // namespace lg
