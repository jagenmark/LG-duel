#pragma once

#include "sim/GameMode.hpp"

#include <optional>
#include <string_view>

namespace lg {

inline constexpr std::string_view kGameModeConsoleUsage =
  "gamemode <duel|ca|mcguffin|ffa>";

[[nodiscard]] constexpr std::optional<GameMode> gameModeForConsoleToken(
  std::string_view value
) {
  if (value == "duel") return GameMode::Duel;
  if (value == "ca" || value == "clanarena" || value == "clan_arena") {
    return GameMode::ClanArena;
  }
  if (value == "mcg" || value == "mcguffin") return GameMode::McGuffin;
  if (value == "ffa" || value == "freeforall" || value == "free_for_all") {
    return GameMode::FreeForAll;
  }
  return std::nullopt;
}

} // namespace lg
