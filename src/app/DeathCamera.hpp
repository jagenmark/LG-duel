#pragma once

#include "net/NetProtocol.hpp"

#include <cstddef>
#include <optional>

namespace lg {

enum class DeathCameraMode {
  Alive,
  DeathPosition,
  Teammate,
};

struct DeathCameraConfig {
  float spectateThresholdSeconds = 3.0F;
  float cameraHoldSeconds = 0.5F;
  float desaturation = 1.0F;
};

struct DeathCameraDecision {
  DeathCameraMode mode = DeathCameraMode::Alive;
  std::optional<std::size_t> teammateIndex;
  float respawnSecondsRemaining = 0.0F;
  float desaturation = 0.0F;
};

// These roles differ while following another player. Keeping them explicit
// prevents connection ownership from hiding the camera subject's viewmodel or
// drawing that subject's world body inside the camera.
struct PresentationViewOwnership {
  std::optional<std::size_t> connectedBody;
  std::optional<std::size_t> cameraSubject;
  std::optional<std::size_t> hiddenWorldBody;
  std::optional<std::size_t> viewModelSubject;
  bool showViewModel = false;
};

[[nodiscard]] DeathCameraDecision deathCameraDecision(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  float deadElapsedSeconds,
  const DeathCameraConfig& config,
  std::optional<std::size_t> preferredTeammate = std::nullopt
);

[[nodiscard]] bool validDeathCameraTeammate(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  std::size_t teammateIndex
);

[[nodiscard]] std::optional<std::size_t> cycleDeathCameraTeammate(
  const ServerSnapshot& snapshot,
  std::size_t localPlayerIndex,
  std::optional<std::size_t> currentTeammate,
  int direction
);

[[nodiscard]] DeathCameraDecision spectatorCameraDecision(
  const ServerSnapshot& snapshot,
  std::optional<std::size_t> preferredPlayer = std::nullopt
);

[[nodiscard]] bool validSpectatorTarget(
  const ServerSnapshot& snapshot,
  std::size_t playerIndex
);

[[nodiscard]] std::optional<std::size_t> cycleSpectatorTarget(
  const ServerSnapshot& snapshot,
  std::optional<std::size_t> currentPlayer,
  int direction
);

[[nodiscard]] std::size_t deathCameraSubjectIndex(
  const DeathCameraDecision& decision,
  std::size_t localPlayerIndex
);

[[nodiscard]] std::optional<std::size_t> presentationSubjectIndex(
  const DeathCameraDecision& decision,
  std::size_t localPlayerIndex,
  bool dedicatedSpectator
);

[[nodiscard]] Weapon presentationSubjectWeapon(
  const ServerSnapshot& snapshot,
  const DeathCameraDecision& decision,
  std::size_t localPlayerIndex,
  bool dedicatedSpectator,
  Weapon fallback
);

[[nodiscard]] PresentationViewOwnership presentationViewOwnership(
  const DeathCameraDecision& decision,
  std::size_t localPlayerIndex,
  bool dedicatedSpectator,
  bool weaponsEnabled
);

[[nodiscard]] bool suppressRemoteBodyForPresentation(
  const PresentationViewOwnership& ownership,
  std::size_t playerIndex
);

} // namespace lg
