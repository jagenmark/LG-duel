#pragma once

namespace lg {

class ConsoleSystem;

void registerClientCvars(ConsoleSystem& console);

[[nodiscard]] float resolvedZoomFieldOfView(
  float baseFieldOfView,
  float generalZoomFieldOfView,
  float sniperZoomFieldOfView,
  bool zoomHeld,
  bool sniperScopeActive,
  float sniperAdsAmount
);

[[nodiscard]] float zoomSensitivityMultiplier(
  float baseFieldOfView,
  float zoomFieldOfView,
  float manualMultiplier
);

} // namespace lg
