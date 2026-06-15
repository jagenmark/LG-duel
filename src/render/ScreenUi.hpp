#pragma once

#include "render/DrawList2D.hpp"
#include "render/Renderer.hpp"

namespace lg {

[[nodiscard]] DrawList2D buildScreenUi(
  int outputWidth,
  int outputHeight,
  const PlayerState& opponent,
  const RenderSettings& settings,
  const HudRenderState& hud,
  const ConsoleRenderState& console
);

[[nodiscard]] DrawList2D buildPerspectiveWeaponOverlay(
  int outputWidth,
  int outputHeight,
  const LightningGunResult& localLightningGun,
  const RenderSettings& settings
);

} // namespace lg
