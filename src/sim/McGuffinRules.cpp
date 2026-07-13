#include "sim/McGuffinRules.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lg {
namespace {

[[nodiscard]] std::optional<std::size_t> scoreIndex(Team team) {
  if (team == Team::Red) return 0U;
  if (team == Team::Blue) return 1U;
  return std::nullopt;
}

[[nodiscard]] Team installedTeam(McGuffinState state) {
  if (state == McGuffinState::InstalledRed) return Team::Red;
  if (state == McGuffinState::InstalledBlue) return Team::Blue;
  return Team::None;
}

[[nodiscard]] McGuffinState installedState(Team team) {
  return team == Team::Red ? McGuffinState::InstalledRed : McGuffinState::InstalledBlue;
}

[[nodiscard]] bool inPickupRange(Vec3 first, Vec3 second, float radius) {
  const Vec3 delta = first - second;
  return dot(delta, delta) <= radius * radius;
}

void clearCarrier(McGuffinObjective& objective) {
  objective.carrierIndex = kNoMcGuffinCarrier;
  objective.carrierTeam = Team::None;
}

void returnToNeutralSpawn(McGuffinObjective& objective) {
  objective.state = McGuffinState::NeutralSpawn;
  objective.associatedTeam = Team::None;
  objective.position = objective.spawnPosition;
  objective.velocity = {};
  objective.stateTicks = 0;
  objective.scoreSubPoints = 0;
  clearCarrier(objective);
}

} // namespace

bool isValidMcGuffinConfig(const McGuffinConfig& config) {
  return config.scoreLimit > 0 && config.pointsPerSecond > 0 &&
    config.carryPointsPerSecond > 0 && config.carryPointLimit > 0 &&
    std::isfinite(config.throwSpeed) && config.throwSpeed >= 0.0F &&
    std::isfinite(config.throwUpSpeed) && config.throwUpSpeed >= 0.0F &&
    std::isfinite(config.throwVelocityInheritance) &&
    config.throwVelocityInheritance >= 0.0F &&
    std::isfinite(config.throwGravity) && config.throwGravity >= 0.0F &&
    std::isfinite(config.throwBounceDamping) &&
    config.throwBounceDamping >= 0.0F && config.throwBounceDamping <= 1.5F &&
    std::isfinite(config.pickupRadius) && config.pickupRadius > 0.0F;
}

bool isValidMcGuffinObjective(const McGuffinObjective& objective) {
  if (!std::isfinite(objective.position.x) || !std::isfinite(objective.position.y) ||
      !std::isfinite(objective.position.z) || !std::isfinite(objective.velocity.x) ||
      !std::isfinite(objective.velocity.y) || !std::isfinite(objective.velocity.z) ||
      !std::isfinite(objective.spawnPosition.x) ||
      !std::isfinite(objective.spawnPosition.y) || !std::isfinite(objective.spawnPosition.z)) {
    return false;
  }
  switch (objective.state) {
  case McGuffinState::NeutralSpawn:
    return objective.associatedTeam == Team::None && objective.carrierTeam == Team::None &&
      objective.carrierIndex == kNoMcGuffinCarrier;
  case McGuffinState::Carried:
    return isPlayableTeam(objective.carrierTeam) &&
      objective.carrierIndex < kMaxPlayers && isValidTeam(objective.associatedTeam);
  case McGuffinState::Dropped:
    return objective.carrierTeam == Team::None && objective.carrierIndex == kNoMcGuffinCarrier &&
      isValidTeam(objective.associatedTeam);
  case McGuffinState::InstalledRed:
    return objective.associatedTeam == Team::Red && objective.carrierTeam == Team::None &&
      objective.carrierIndex == kNoMcGuffinCarrier;
  case McGuffinState::InstalledBlue:
    return objective.associatedTeam == Team::Blue && objective.carrierTeam == Team::None &&
      objective.carrierIndex == kNoMcGuffinCarrier;
  }
  return false;
}

bool hasRequiredMcGuffinPlayers(const std::array<bool, kMaxPlayers>& connectedPlayers) {
  return std::count(connectedPlayers.begin(), connectedPlayers.end(), true) >= 2;
}

bool canStartMcGuffin(const std::array<bool, kMaxPlayers>& connectedPlayers,
                      const std::array<bool, kMaxPlayers>& readyPlayers,
                      const std::array<Team, kMaxPlayers>& teams) {
  if (!hasRequiredMcGuffinPlayers(connectedPlayers)) return false;
  bool hasRed = false;
  bool hasBlue = false;
  for (std::size_t index = 0; index < kMaxPlayers; ++index) {
    if (!connectedPlayers[index]) continue;
    if (!readyPlayers[index] || !isPlayableTeam(teams[index])) return false;
    hasRed = hasRed || teams[index] == Team::Red;
    hasBlue = hasBlue || teams[index] == Team::Blue;
  }
  return hasRed && hasBlue;
}

bool areMcGuffinTeammates(const std::array<Team, kMaxPlayers>& teams,
                          std::size_t firstPlayerIndex, std::size_t secondPlayerIndex) {
  return firstPlayerIndex < kMaxPlayers && secondPlayerIndex < kMaxPlayers &&
    firstPlayerIndex != secondPlayerIndex && isPlayableTeam(teams[firstPlayerIndex]) &&
    teams[firstPlayerIndex] == teams[secondPlayerIndex];
}

bool areMcGuffinEnemies(const std::array<Team, kMaxPlayers>& teams,
                        std::size_t firstPlayerIndex, std::size_t secondPlayerIndex) {
  return firstPlayerIndex < kMaxPlayers && secondPlayerIndex < kMaxPlayers &&
    isPlayableTeam(teams[firstPlayerIndex]) && isPlayableTeam(teams[secondPlayerIndex]) &&
    teams[firstPlayerIndex] != teams[secondPlayerIndex];
}

void resetMcGuffin(McGuffinObjective& objective, Vec3 spawnPosition) {
  objective = {};
  objective.spawnPosition = spawnPosition;
  objective.position = spawnPosition;
  objective.carrierIndex = kNoMcGuffinCarrier;
}

bool tryPickupMcGuffin(McGuffinObjective& objective, const McGuffinConfig& config,
                       std::size_t playerIndex, Team playerTeam, Vec3 playerPosition) {
  if (!isValidMcGuffinConfig(config) || !isValidMcGuffinObjective(objective) ||
      playerIndex >= kMaxPlayers || !isPlayableTeam(playerTeam) ||
      (objective.state != McGuffinState::NeutralSpawn && objective.state != McGuffinState::Dropped) ||
      !inPickupRange(playerPosition, objective.position, config.pickupRadius)) return false;
  objective.state = McGuffinState::Carried;
  objective.carrierIndex = static_cast<std::uint8_t>(playerIndex);
  objective.carrierTeam = playerTeam;
  objective.velocity = {};
  objective.stateTicks = 0;
  return true;
}

bool tryStealMcGuffin(McGuffinObjective& objective, const McGuffinConfig& config,
                      std::size_t playerIndex, Team playerTeam, Vec3 playerPosition) {
  const Team owner = installedTeam(objective.state);
  if (!isValidMcGuffinConfig(config) || !isValidMcGuffinObjective(objective) ||
      playerIndex >= kMaxPlayers || !isPlayableTeam(playerTeam) || playerTeam == owner ||
      !isPlayableTeam(owner) || !inPickupRange(playerPosition, objective.position, config.pickupRadius)) return false;
  objective.state = McGuffinState::Carried;
  objective.carrierIndex = static_cast<std::uint8_t>(playerIndex);
  objective.carrierTeam = playerTeam;
  objective.velocity = {};
  objective.stateTicks = 0;
  objective.scoreSubPoints = 0;
  return true;
}

bool dropMcGuffin(McGuffinObjective& objective, std::size_t playerIndex, Vec3 dropPosition) {
  if (!isValidMcGuffinObjective(objective) || objective.state != McGuffinState::Carried ||
      playerIndex != objective.carrierIndex || !std::isfinite(dropPosition.x) ||
      !std::isfinite(dropPosition.y) || !std::isfinite(dropPosition.z)) return false;
  objective.state = McGuffinState::Dropped;
  objective.position = dropPosition;
  objective.velocity = {};
  objective.stateTicks = 0;
  clearCarrier(objective);
  return true;
}

bool throwMcGuffin(McGuffinObjective& objective, std::size_t playerIndex,
                    Vec3 throwPosition, Vec3 throwVelocity) {
  if (!std::isfinite(throwVelocity.x) || !std::isfinite(throwVelocity.y) ||
      !std::isfinite(throwVelocity.z) ||
      !dropMcGuffin(objective, playerIndex, throwPosition)) {
    return false;
  }
  objective.velocity = throwVelocity;
  return true;
}

std::optional<Team> tickMcGuffin(McGuffinObjective& objective, const McGuffinConfig& config,
                                 bool carrierAtOwnBase,
                                 std::array<std::uint16_t, kPlayableTeamCount>& teamScores) {
  if (!isValidMcGuffinConfig(config) || !isValidMcGuffinObjective(objective)) return std::nullopt;
  if (objective.state == McGuffinState::Carried) {
    objective.stateTicks = carrierAtOwnBase ? objective.stateTicks + 1U : 0U;
    if (carrierAtOwnBase && objective.stateTicks >= config.installationDelayTicks) {
      objective.state = installedState(objective.carrierTeam);
      objective.associatedTeam = objective.carrierTeam;
      objective.position = objective.spawnPosition;
      objective.velocity = {};
      objective.stateTicks = 0;
      objective.scoreSubPoints = 0;
      clearCarrier(objective);
    }
    return std::nullopt;
  }
  if (objective.state == McGuffinState::Dropped) {
    ++objective.stateTicks;
    if (config.returnTicks > 0 && objective.stateTicks >= config.returnTicks) {
      returnToNeutralSpawn(objective);
    }
    return std::nullopt;
  }
  const Team scoringTeam = installedTeam(objective.state);
  if (!isPlayableTeam(scoringTeam)) return std::nullopt;
  ++objective.stateTicks;
  objective.scoreSubPoints += config.pointsPerSecond;
  constexpr std::uint32_t kSubPointsPerPoint = static_cast<std::uint32_t>(kFixedTickRate);
  const std::uint32_t points = objective.scoreSubPoints / kSubPointsPerPoint;
  objective.scoreSubPoints %= kSubPointsPerPoint;
  if (points > 0) {
    const std::size_t index = *scoreIndex(scoringTeam);
    const std::uint32_t updated = static_cast<std::uint32_t>(teamScores[index]) + points;
    teamScores[index] = static_cast<std::uint16_t>(std::min<std::uint32_t>(updated, std::numeric_limits<std::uint16_t>::max()));
    if (hasWonMcGuffin(teamScores, scoringTeam, config.scoreLimit)) return scoringTeam;
  }
  return std::nullopt;
}

bool hasWonMcGuffin(const std::array<std::uint16_t, kPlayableTeamCount>& teamScores,
                    Team team, std::uint16_t scoreLimit) {
  const auto index = scoreIndex(team);
  return index.has_value() && scoreLimit > 0 && teamScores[*index] >= scoreLimit;
}

} // namespace lg
