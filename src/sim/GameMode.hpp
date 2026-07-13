#pragma once

#include <cstddef>
#include <cstdint>

namespace lg {

enum class GameMode : std::uint8_t {
  Duel = 0,
  ClanArena = 1,
  McGuffin = 2,
};

enum class Team : std::uint8_t {
  None = 0,
  Red = 1,
  Blue = 2,
};

inline constexpr std::size_t kPlayableTeamCount = 2;

[[nodiscard]] constexpr bool isValidGameMode(GameMode gameMode) {
  return gameMode == GameMode::Duel ||
    gameMode == GameMode::ClanArena ||
    gameMode == GameMode::McGuffin;
}

[[nodiscard]] constexpr bool isValidTeam(Team team) {
  return team == Team::None || team == Team::Red || team == Team::Blue;
}

[[nodiscard]] constexpr bool isPlayableTeam(Team team) {
  return team == Team::Red || team == Team::Blue;
}

} // namespace lg
