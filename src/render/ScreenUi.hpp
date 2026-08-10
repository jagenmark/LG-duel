#pragma once

#include "render/DrawList2D.hpp"
#include "render/Perspective.hpp"
#include "render/Renderer.hpp"

#include <array>

namespace lg {

struct McGuffinNavigationProjection {
  bool valid = false;
  bool onScreen = false;
  bool behind = false;
  ScreenPoint screenPosition = {};
  ScreenPoint edgePosition = {};
  float distance = 0.0F;
};

[[nodiscard]] McGuffinNavigationProjection projectMcGuffinNavigationTarget(
  const McGuffinNavigationTarget& target,
  const PerspectiveCamera& camera,
  int outputWidth,
  int outputHeight
);

[[nodiscard]] DrawList2D buildScreenUi(
  int outputWidth,
  int outputHeight,
  const PlayerState& localPlayer,
  const RenderSettings& settings,
  const HudRenderState& hud,
  const ConsoleRenderState& console,
  const PerspectiveCamera* navigationCamera = nullptr
);

[[nodiscard]] DrawList2D buildFloatingHealthBars(
  int outputWidth,
  int outputHeight,
  const PerspectiveCamera& camera,
  const Arena& arena,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const std::array<bool, kDuelPlayerCount>& remoteRenderVisible,
  const RenderSettings& settings,
  const HudRenderState& hud
);

[[nodiscard]] DrawList2D buildFloatingDamageNumbers(
  int outputWidth,
  int outputHeight,
  const PerspectiveCamera& camera,
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
  const RenderSettings& settings,
  ScreenPoint freezeGunMuzzle = {-1.0F, -1.0F}
);

} // namespace lg
