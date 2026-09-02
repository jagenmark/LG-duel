#pragma once

namespace lg {

class ConsoleSystem;

void registerClientCvars(ConsoleSystem& console);

[[nodiscard]] float generalZoomDurationMilliseconds(
  const ConsoleSystem& console
);

[[nodiscard]] float resolvedZoomFieldOfView(
  float baseFieldOfView,
  float generalZoomFieldOfView,
  float sniperZoomFieldOfView,
  bool zoomHeld,
  bool sniperScopeActive,
  float sniperAdsAmount
);

[[nodiscard]] float advanceGeneralZoomAmount(
  float currentAmount,
  bool zoomHeld,
  bool smoothEnabled,
  float durationMilliseconds,
  float elapsedSeconds
);

[[nodiscard]] float generalZoomFieldOfView(
  float baseFieldOfView,
  float zoomFieldOfView,
  float zoomAmount
);

[[nodiscard]] float zoomSensitivityMultiplier(
  float baseFieldOfView,
  float zoomFieldOfView,
  float manualMultiplier
);

[[nodiscard]] float transitioningZoomSensitivityMultiplier(
  float baseFieldOfView,
  float liveFieldOfView,
  float manualMultiplier,
  float zoomAmount
);

} // namespace lg
