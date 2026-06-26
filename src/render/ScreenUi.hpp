#pragma once

#include "render/DrawList2D.hpp"
#include "render/Perspective.hpp"
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

[[nodiscard]] DrawList2D buildFloatingHealthBars(
  int outputWidth,
  int outputHeight,
  const PerspectiveCamera& camera,
  const Arena& arena,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const RenderSettings& settings,
  const HudRenderState& hud
);

[[nodiscard]] DrawList2D buildPerspectiveWeaponOverlay(
  int outputWidth,
  int outputHeight,
  const LightningGunResult& localLightningGun,
  Weapon selectedWeapon,
  Weapon previousWeapon,
  float weaponSwitchProgress,
  const RenderSettings& settings
);

} // namespace lg
