#pragma once

#include "render/DrawList2D.hpp"
#include "render/Perspective.hpp"
#include "render/Renderer.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace lg {

struct Vertex3D {
  Vec3 position = {};
  RenderColor color = {};
  float u = 0.0F;
  float v = 0.0F;
  std::uint32_t materialId = 0;
};

enum class RenderPass {
  OpaqueWorld,
  TranslucentWorld,
  AdditiveGlow,
  ViewModel,
};

enum class MeshHandle : std::uint16_t {
  Invalid = 0,
  PlasmaCore,
};

enum class BillboardHandle : std::uint16_t {
  Invalid = 0,
  PlasmaGlow,
};

struct BoundingSphere {
  Vec3 center = {};
  float radius = 0.0F;
};

struct StaticMeshAsset {
  MeshHandle handle = MeshHandle::Invalid;
  std::span<const Vertex3D> vertices;
  BoundingSphere localBounds = {};
  RenderPass pass = RenderPass::OpaqueWorld;
};

struct BillboardAsset {
  BillboardHandle handle = BillboardHandle::Invalid;
  BoundingSphere localBounds = {};
  RenderPass pass = RenderPass::AdditiveGlow;
};

struct SimpleRenderInstance {
  MeshHandle mesh = MeshHandle::Invalid;
  BillboardHandle billboard = BillboardHandle::Invalid;
  RenderPass pass = RenderPass::OpaqueWorld;
  Vec3 position = {};
  Vec3 scale = {1.0F, 1.0F, 1.0F};
  float rotationRadians = 0.0F;
  RenderColor color = {};
  float visualPhase = 0.0F;
  BoundingSphere worldBounds = {};
};

enum class ProjectileVisualType {
  Plasma,
  Rocket,
  Grenade,
};

struct ProjectileVisualDescriptor {
  ProjectileVisualType type = ProjectileVisualType::Plasma;
  MeshHandle coreMesh = MeshHandle::Invalid;
  BillboardHandle glowBillboard = BillboardHandle::Invalid;
  RenderColor coreColor = {};
  RenderColor glowColor = {};
  float coreScale = 1.0F;
  float glowScale = 1.0F;
  bool usesAdditiveGlow = false;
};

struct SimpleRenderBatch {
  MeshHandle mesh = MeshHandle::Invalid;
  BillboardHandle billboard = BillboardHandle::Invalid;
  RenderPass pass = RenderPass::OpaqueWorld;
  std::uint32_t firstInstance = 0;
  std::uint32_t instanceCount = 0;
};

struct ProjectileRenderStats {
  std::uint32_t projectilesActive = 0;
  std::uint32_t projectilesFrustumCulled = 0;
  std::uint32_t projectilesRendered = 0;
  std::uint32_t projectileCoreInstances = 0;
  std::uint32_t projectileGlowInstances = 0;
  std::uint32_t projectileInstanceUploadBytes = 0;
  std::uint32_t projectileMeshDrawCalls = 0;
  std::uint32_t projectileGlowDrawCalls = 0;
  std::uint32_t legacyProjectileDynamicVertices = 0;
};

struct Scene3D {
  PerspectiveCamera camera = {};
  std::vector<Vertex3D> vertices;
  std::vector<Vertex3D> translucentVertices;
  std::vector<SimpleRenderInstance> simpleInstances;
  std::vector<SimpleRenderBatch> simpleBatches;
  ProjectileRenderStats projectileStats = {};
  std::array<bool, kDuelPlayerCount> remoteRenderVisible = {};
  std::uint32_t visibleRemotePlayers = 0;
  std::uint32_t remoteBodyModelsBuilt = 0;
  std::uint32_t remoteWeaponModelsBuilt = 0;
  std::uint32_t playerOutlinesBuilt = 0;
  std::uint32_t remoteCandidates = 0;
  std::uint32_t remoteFrustumVisible = 0;
  std::uint32_t remoteFrustumCulled = 0;
};

[[nodiscard]] const StaticMeshAsset* staticMeshAsset(MeshHandle handle);
[[nodiscard]] const BillboardAsset* billboardAsset(BillboardHandle handle);
[[nodiscard]] const ProjectileVisualDescriptor* projectileVisualDescriptor(
  ProjectileVisualType type
);
[[nodiscard]] ProjectileVisualType projectileVisualTypeForWeapon(Weapon weapon);

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

[[nodiscard]] Scene3D buildStaticWorldScene(const Arena& arena);

} // namespace lg
