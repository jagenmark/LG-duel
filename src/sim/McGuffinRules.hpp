#pragma once

#include "shared/Constants.hpp"
#include "shared/Math.hpp"
#include "sim/GameMode.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace lg {

// The explicit phase is replicated rather than inferred from carrier/team fields so
// clients can present every objective transition without guessing server authority.
enum class McGuffinState : std::uint8_t {
  NeutralSpawn = 0,
  Carried = 1,
  Dropped = 2,
  InstalledRed = 3,
  InstalledBlue = 4,
};

inline constexpr std::uint8_t kNoMcGuffinCarrier = 255;

struct McGuffinConfig {
  std::uint16_t scoreLimit = 100;
  std::uint16_t pointsPerSecond = 1;
  std::uint16_t carryPointsPerSecond = 1;
  std::uint16_t carryPointLimit = 10;
  std::uint32_t initialSpawnTicks = 3750;
  std::uint32_t installationDelayTicks = 0;
  std::uint32_t stealTicks = 125;
  // Ground drops eventually reset as a safety net for unreachable positions.
  // Installed objectives use distinct states and never consult this timer.
  std::uint32_t returnTicks = 3750;
  float throwSpeed = 12.0F;
  float throwUpSpeed = 4.0F;
  float throwVelocityInheritance = 1.0F;
  float throwGravity = 20.0F;
  float throwBounceDamping = 0.4F;
  std::uint32_t throwPickupLockoutTicks = 25;
  std::uint32_t finalHoldTicks = 375;
  float pickupRadius = 0.9F;
};

struct McGuffinObjective {
  McGuffinState state = McGuffinState::NeutralSpawn;
  // The team that last installed the objective. It makes a dropped stolen
  // objective returnable by its original team while it is not installed.
  Team associatedTeam = Team::None;
  Team carrierTeam = Team::None;
  std::uint8_t carrierIndex = kNoMcGuffinCarrier;
  Vec3 position = {};
  Vec3 velocity = {};
  Vec3 spawnPosition = {};
  std::uint32_t stateTicks = 0;
  // One score point is kFixedTickRate units. Adding pointsPerSecond every
  // fixed tick is exact and retains fractions between score awards.
  std::uint32_t scoreSubPoints = 0;
};

[[nodiscard]] bool isValidMcGuffinConfig(const McGuffinConfig& config);
[[nodiscard]] bool isValidMcGuffinObjective(const McGuffinObjective& objective);

[[nodiscard]] bool hasRequiredMcGuffinPlayers(
  const std::array<bool, kMaxPlayers>& connectedPlayers
);
[[nodiscard]] bool canStartMcGuffin(
  const std::array<bool, kMaxPlayers>& connectedPlayers,
  const std::array<bool, kMaxPlayers>& readyPlayers,
  const std::array<Team, kMaxPlayers>& teams
);
[[nodiscard]] bool areMcGuffinTeammates(
  const std::array<Team, kMaxPlayers>& teams,
  std::size_t firstPlayerIndex,
  std::size_t secondPlayerIndex
);
[[nodiscard]] bool areMcGuffinEnemies(
  const std::array<Team, kMaxPlayers>& teams,
  std::size_t firstPlayerIndex,
  std::size_t secondPlayerIndex
);

void resetMcGuffin(McGuffinObjective& objective, Vec3 spawnPosition);
[[nodiscard]] bool tryPickupMcGuffin(
  McGuffinObjective& objective,
  const McGuffinConfig& config,
  std::size_t playerIndex,
  Team playerTeam,
  Vec3 playerPosition
);
[[nodiscard]] bool tryStealMcGuffin(
  McGuffinObjective& objective,
  const McGuffinConfig& config,
  std::size_t playerIndex,
  Team playerTeam,
  Vec3 playerPosition
);
[[nodiscard]] bool dropMcGuffin(
  McGuffinObjective& objective,
  std::size_t playerIndex,
  Vec3 dropPosition
);
[[nodiscard]] bool throwMcGuffin(
  McGuffinObjective& objective,
  std::size_t playerIndex,
  Vec3 throwPosition,
  Vec3 throwVelocity
);
// Call exactly once per authoritative fixed simulation tick. carrierAtOwnBase
// is server-derived: clients never decide whether an installation is valid.
[[nodiscard]] std::optional<Team> tickMcGuffin(
  McGuffinObjective& objective,
  const McGuffinConfig& config,
  bool carrierAtOwnBase,
  std::array<std::uint16_t, kPlayableTeamCount>& teamScores
);

[[nodiscard]] bool hasWonMcGuffin(
  const std::array<std::uint16_t, kPlayableTeamCount>& teamScores,
  Team team,
  std::uint16_t scoreLimit
);

} // namespace lg
