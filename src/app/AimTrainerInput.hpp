#pragma once

#include <algorithm>

namespace lg {

struct AimTrainerViewAngles {
  float yaw = 0.0F;
  float pitch = 0.0F;
};

enum class AimTrainerEscapeAction {
  CancelText,
  OpenScenarios,
  CloseScenarios,
};

[[nodiscard]] inline AimTrainerEscapeAction aimTrainerEscapeAction(
  bool scenarioMenuOpen,
  bool editingText
) {
  if (!scenarioMenuOpen) return AimTrainerEscapeAction::OpenScenarios;
  if (editingText) return AimTrainerEscapeAction::CancelText;
  return AimTrainerEscapeAction::CloseScenarios;
}

[[nodiscard]] inline AimTrainerViewAngles applyAimTrainerMouseMotion(
  AimTrainerViewAngles angles,
  float deltaX,
  float deltaY
) {
  angles.yaw -= deltaX * 0.0025F;
  angles.pitch = std::clamp(angles.pitch - deltaY * 0.0025F, -1.5F, 1.5F);
  return angles;
}

} // namespace lg
