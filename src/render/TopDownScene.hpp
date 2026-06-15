#pragma once

#include "render/DrawList2D.hpp"
#include "render/Renderer.hpp"

namespace lg {

[[nodiscard]] DrawList2D buildTopDownScene(
  int outputWidth,
  int outputHeight,
  const Arena& arena,
  const PlayerState& player,
  const PlayerState& opponent,
  const LightningGunResult& localLightningGun,
  const LightningGunResult& opponentLightningGun,
  const RenderSettings& settings,
  const HudRenderState& hud
);

} // namespace lg
