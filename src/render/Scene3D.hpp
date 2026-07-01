#pragma once

#include "render/DrawList2D.hpp"
#include "render/Perspective.hpp"
#include "render/Renderer.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace lg {

struct Vertex3D {
  Vec3 position = {};
  RenderColor color = {};
  float u = 0.0F;
  float v = 0.0F;
  std::uint32_t materialId = 0;
};

struct Scene3D {
  PerspectiveCamera camera = {};
  std::vector<Vertex3D> vertices;
  std::vector<Vertex3D> translucentVertices;
  std::array<bool, kDuelPlayerCount> remoteRenderVisible = {};
  std::uint32_t remoteCandidates = 0;
  std::uint32_t remoteFrustumVisible = 0;
  std::uint32_t remoteFrustumCulled = 0;
  std::uint32_t remoteBodiesBuilt = 0;
  std::uint32_t remoteWeaponsBuilt = 0;
  std::uint32_t remoteOutlinesBuilt = 0;
};

[[nodiscard]] Scene3D buildPerspectiveScene(
  float aspectRatio,
  const Arena& arena,
  const PlayerState& player,
  const PlayerState& opponent,
  const LightningGunResult& localLightningGun,
  const LightningGunResult& opponentLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const RenderSettings& settings
);

[[nodiscard]] Scene3D buildPerspectiveScene(
  float aspectRatio,
  const Arena& arena,
  const PlayerState& player,
  const std::array<RemotePlayerView, kDuelPlayerCount>& remotePlayers,
  const LightningGunResult& localLightningGun,
  const std::array<WeaponFireResult, kDuelPlayerCount>& weaponFires,
  const std::array<RocketExplosionResult, kDuelPlayerCount>& rocketExplosions,
  const std::array<RocketProjectileSnapshot, kMaxRocketProjectiles>& rockets,
  const RenderSettings& settings
);

} // namespace lg
