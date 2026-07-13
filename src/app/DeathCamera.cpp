#include "app/DeathCamera.hpp"

#include <algorithm>

namespace lg {

DeathCameraDecision deathCameraDecision(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  float deadElapsedSeconds,
  const DeathCameraConfig& config,
  std::optional<std::size_t> preferredTeammate
) {
  DeathCameraDecision result;
  if (localPlayerIndex >= kDuelPlayerCount ||
      snapshot.players[localPlayerIndex].health > 0) {
    return result;
  }

  result.mode = DeathCameraMode::DeathPosition;
  result.desaturation = std::clamp(config.desaturation, 0.0F, 1.0F);
  result.respawnSecondsRemaining =
    static_cast<float>(snapshot.respawnTicksRemaining[localPlayerIndex]) /
    static_cast<float>(kFixedTickRate);

  const bool hasLiveRespawn =
    snapshot.respawnTicksRemaining[localPlayerIndex] > 0;
  const float configuredRespawnSeconds =
    static_cast<float>(snapshot.matchRules.deathRespawnTicks) /
    static_cast<float>(kFixedTickRate);
  const bool longDeath = hasLiveRespawn
    ? configuredRespawnSeconds >= config.spectateThresholdSeconds
    : snapshot.matchPhase == MatchPhase::Live;
  if (!longDeath || deadElapsedSeconds < config.cameraHoldSeconds) {
    return result;
  }

  if (preferredTeammate.has_value() && validDeathCameraTeammate(
        snapshot, localPlayerIndex, *preferredTeammate
      )) {
    result.mode = DeathCameraMode::Teammate;
    result.teammateIndex = *preferredTeammate;
    return result;
  }
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!validDeathCameraTeammate(snapshot, localPlayerIndex, index)) continue;
    result.mode = DeathCameraMode::Teammate;
    result.teammateIndex = index;
    return result;
  }
  return result;
}

bool validDeathCameraTeammate(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  std::size_t teammateIndex
) {
  return localPlayerIndex < kDuelPlayerCount &&
    teammateIndex < kDuelPlayerCount &&
    teammateIndex != localPlayerIndex &&
    snapshot.teams[localPlayerIndex] != Team::None &&
    snapshot.teams[teammateIndex] == snapshot.teams[localPlayerIndex] &&
    snapshot.participatingPlayers[teammateIndex] &&
    snapshot.players[teammateIndex].health > 0;
}

std::optional<std::size_t> cycleDeathCameraTeammate(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  std::optional<std::size_t> currentTeammate,
  int direction
) {
  const int step = direction < 0 ? -1 : 1;
  std::size_t cursor = currentTeammate.value_or(
    step > 0 ? localPlayerIndex : (localPlayerIndex + 1U) % kDuelPlayerCount
  );
  for (std::size_t attempt = 0; attempt < kDuelPlayerCount; ++attempt) {
    cursor = step > 0
      ? (cursor + 1U) % kDuelPlayerCount
      : (cursor + kDuelPlayerCount - 1U) % kDuelPlayerCount;
    if (validDeathCameraTeammate(snapshot, localPlayerIndex, cursor)) {
      return cursor;
    }
  }
  return std::nullopt;
}

DeathCameraDecision spectatorCameraDecision(
  const ServerSnapshot& snapshot,
  std::optional<std::size_t> preferredPlayer
) {
  DeathCameraDecision result;
  if (preferredPlayer.has_value() &&
      validSpectatorTarget(snapshot, *preferredPlayer)) {
    result.mode = DeathCameraMode::Teammate;
    result.teammateIndex = preferredPlayer;
    return result;
  }
  for (std::size_t index = 0; index < kDuelPlayerCount; ++index) {
    if (!validSpectatorTarget(snapshot, index)) continue;
    result.mode = DeathCameraMode::Teammate;
    result.teammateIndex = index;
    return result;
  }
  result.mode = DeathCameraMode::DeathPosition;
  return result;
}

bool validSpectatorTarget(
  const ServerSnapshot& snapshot,
  std::size_t playerIndex
) {
  return playerIndex < kDuelPlayerCount &&
    snapshot.participatingPlayers[playerIndex] &&
    snapshot.players[playerIndex].health > 0;
}

std::optional<std::size_t> cycleSpectatorTarget(
  const ServerSnapshot& snapshot,
  std::optional<std::size_t> currentPlayer,
  int direction
) {
  const int step = direction < 0 ? -1 : 1;
  std::size_t cursor = currentPlayer.value_or(
    step > 0 ? kDuelPlayerCount - 1U : 0U
  );
  for (std::size_t attempt = 0; attempt < kDuelPlayerCount; ++attempt) {
    cursor = step > 0
      ? (cursor + 1U) % kDuelPlayerCount
      : (cursor + kDuelPlayerCount - 1U) % kDuelPlayerCount;
    if (validSpectatorTarget(snapshot, cursor)) return cursor;
  }
  return std::nullopt;
}

std::size_t deathCameraSubjectIndex(
  const DeathCameraDecision& decision,
  std::size_t localPlayerIndex
) {
  return decision.mode == DeathCameraMode::Teammate &&
      decision.teammateIndex.has_value()
    ? *decision.teammateIndex
    : localPlayerIndex;
}

} // namespace lg
