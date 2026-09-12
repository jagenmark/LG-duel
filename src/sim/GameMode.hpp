#pragma once

#include <cstddef>
#include <cstdint>

namespace lg {

enum class GameMode : std::uint8_t {
  Duel = 0,
  ClanArena = 1,
  McGuffin = 2,
  FreeForAll = 3,
  GlideDuel = 4,
};

[[nodiscard]] constexpr bool isDuelGameMode(GameMode mode) {
  return mode == GameMode::Duel || mode == GameMode::GlideDuel;
}

inline constexpr std::uint16_t kGlideRoundTicks = 45U * 125U;
inline constexpr std::uint16_t kGlideOverdriveTicks = 15U * 125U;

using PlayerScore = std::int16_t;

inline constexpr PlayerScore kFreeForAllScoreLimit = 100;
inline constexpr std::uint16_t kFreeForAllTimeLimitMinutes = 10;

enum class Team : std::uint8_t {
  None = 0,
  Red = 1,
  Blue = 2,
};

inline constexpr std::size_t kPlayableTeamCount = 2;

[[nodiscard]] constexpr bool isValidGameMode(GameMode gameMode) {
  return isDuelGameMode(gameMode) ||
    gameMode == GameMode::ClanArena ||
    gameMode == GameMode::McGuffin ||
    gameMode == GameMode::FreeForAll;
}

[[nodiscard]] constexpr bool isTeamGameMode(GameMode gameMode) {
  return gameMode == GameMode::ClanArena || gameMode == GameMode::McGuffin;
}

[[nodiscard]] constexpr bool isIndividualGameMode(GameMode gameMode) {
  return isDuelGameMode(gameMode) || gameMode == GameMode::FreeForAll;
}

[[nodiscard]] constexpr bool isValidTeam(Team team) {
  return team == Team::None || team == Team::Red || team == Team::Blue;
}

[[nodiscard]] constexpr bool isPlayableTeam(Team team) {
  return team == Team::Red || team == Team::Blue;
}

} // namespace lg
