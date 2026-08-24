#pragma once

#include <algorithm>
#include <cmath>

namespace lg {

struct AimTrainerViewAngles {
  float yaw = 0.0F;
  float pitch = 0.0F;
};

struct AimTrainerWheelInput {
  int rowDelta = 0;
  float remainder = 0.0F;
};

[[nodiscard]] inline bool aimTrainerUsesCompactHud(int viewportWidth) {
  return viewportWidth < 900;
}

[[nodiscard]] inline bool shouldHandleAimTrainerMenuKeyDown(
  bool pressed,
  bool repeat,
  bool repeatable
) {
  return pressed && (!repeat || repeatable);
}

[[nodiscard]] inline AimTrainerWheelInput accumulateAimTrainerWheel(
  float remainder,
  float wheelY
) {
  const float total = remainder + wheelY;
  const int wholeSteps = static_cast<int>(std::trunc(total));
  return {-wholeSteps, total - static_cast<float>(wholeSteps)};
}

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
