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
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const RenderSettings& settings,
  const HudRenderState& hud
);

[[nodiscard]] DrawList2D buildTopDownScene(
  int outputWidth,
  int outputHeight,
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const RenderSettings& settings,
  const HudRenderState& hud
);

} // namespace lg
