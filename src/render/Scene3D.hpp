#pragma once

#include "render/DrawList2D.hpp"
#include "render/Perspective.hpp"
#include "render/Renderer.hpp"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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
  PlayerBoxCube,
  PlasmaCore,
  RocketProjectile,
  GrenadeProjectile,
  ExplosionCore,
  MachineGunTracer,
  ShotgunTracer,
  RemoteMachineGun,
  RemoteShotgun,
  RemoteGrenadeLauncher,
  RemoteRocketLauncher,
  RemoteLightningGun,
  RemoteRailgun,
  RemotePlasmaGun,
};

enum class PlayerBodyPartType : std::uint8_t {
  None = 0,
  Torso,
  Hips,
  Head,
  LeftArm,
  RightArm,
  LeftLeg,
  RightLeg,
};

enum class BillboardHandle : std::uint16_t {
  Invalid = 0,
  PlasmaGlow,
  RocketFlame,
  ExplosionFlash,
  ExplosionHalo,
};

struct BoundingSphere {
  Vec3 center = {};
  float radius = 0.0F;
};

struct GltfModelBounds {
  Vec3 min = {};
  Vec3 max = {};
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

struct TransientVfxStats {
  std::uint32_t activeEffects = 0;
  std::uint32_t activeMachineGunTracers = 0;
  std::uint32_t activeShotgunTracers = 0;
  std::uint32_t activeExplosionEffects = 0;
  std::uint32_t newExplosionEventsConsumed = 0;
  std::uint32_t tracerCandidates = 0;
  std::uint32_t tracerFrustumCulled = 0;
  std::uint32_t tracerInstancesSubmitted = 0;
  std::uint32_t tracerInstanceUploadBytes = 0;
  std::uint32_t tracerBatches = 0;
  std::uint32_t tracerDrawCalls = 0;
  std::uint32_t explosionCandidates = 0;
  std::uint32_t explosionFrustumCulled = 0;
  std::uint32_t explosionInstancesSubmitted = 0;
  std::uint32_t explosionInstanceUploadBytes = 0;
  std::uint32_t explosionOpaqueBatches = 0;
  std::uint32_t explosionAdditiveBatches = 0;
  std::uint32_t explosionDrawCalls = 0;
  std::uint32_t legacyWireframeExplosionDraws = 0;
  std::uint32_t legacyMachineGunShotgunVisualDraws = 0;
};

struct StaticMeshInstance {
  MeshHandle mesh = MeshHandle::Invalid;
  RenderPass pass = RenderPass::OpaqueWorld;
  Vec3 modelRow0 = {};
  Vec3 modelRow1 = {};
  Vec3 modelRow2 = {};
  Vec3 modelTranslation = {};
  RenderColor color = {};
  BoundingSphere worldBounds = {};
  PlayerBodyPartType playerBodyPart = PlayerBodyPartType::None;
  std::uint8_t playerIndex = 0;
  OutlineState outlineState = {};
  bool playerBoxBody = false;
  bool playerBoxOutlined = false;
};

struct StaticMeshBatch {
  MeshHandle mesh = MeshHandle::Invalid;
  RenderPass pass = RenderPass::OpaqueWorld;
  std::uint32_t firstInstance = 0;
  std::uint32_t instanceCount = 0;
};

struct ProjectileRenderStats {
  std::uint32_t projectilesActive = 0;
  std::uint32_t projectilesFrustumCulled = 0;
  std::uint32_t projectilesRendered = 0;
  std::uint32_t plasmaInstances = 0;
  std::uint32_t rocketInstances = 0;
  std::uint32_t grenadeInstances = 0;
  std::uint32_t projectileCoreInstances = 0;
  std::uint32_t projectileGlowInstances = 0;
  std::uint32_t opaqueProjectileBatches = 0;
  std::uint32_t additiveProjectileBatches = 0;
  std::uint32_t projectileInstanceUploadBytes = 0;
  std::uint32_t projectileMeshDrawCalls = 0;
  std::uint32_t projectileGlowDrawCalls = 0;
  std::uint32_t legacyProjectileDynamicVertices = 0;
};

struct RemoteWeaponRenderStats {
  std::uint32_t candidates = 0;
  std::uint32_t frustumCulled = 0;
  std::uint32_t instancesSubmitted = 0;
  std::uint32_t instanceUploadBytes = 0;
  std::uint32_t batches = 0;
  std::uint32_t drawCalls = 0;
  std::uint32_t legacyDynamicVertices = 0;
};

struct PlayerBoxRenderStats {
  std::uint32_t visiblePlayers = 0;
  std::uint32_t culledPlayers = 0;
  std::uint32_t instancesSubmitted = 0;
  std::uint32_t instanceUploadBytes = 0;
  std::uint32_t sharedCubeStaticGpuBytes = 0;
  std::uint32_t opaqueBatches = 0;
  std::uint32_t opaqueDrawCalls = 0;
  std::uint32_t outlineMaskBatches = 0;
  std::uint32_t outlineMaskDrawCalls = 0;
  std::uint32_t legacyCpuGeneratedVertices = 0;
  std::uint32_t legacyDynamicVertexUploadBytes = 0;
};

struct GltfPlayerModelRenderStats {
  std::uint32_t activeInstances = 0;
  std::uint32_t frustumCulled = 0;
  std::uint32_t staticMeshGpuBytes = 0;
  std::uint32_t staticIndexGpuBytes = 0;
  std::uint32_t poseUploadBytes = 0;
  std::uint32_t bonePaletteEntriesUploaded = 0;
  std::uint32_t rigidFallbackInstances = 0;
  std::uint32_t gpuSkinnedInstances = 0;
  std::uint32_t bodyBatches = 0;
  std::uint32_t bodyDrawCalls = 0;
  std::uint32_t outlineMaskBatches = 0;
  std::uint32_t outlineMaskDrawCalls = 0;
  std::uint32_t legacyCpuSkinnedVertexUploadBytes = 0;
};

struct ViewModelRenderStats {
  std::uint32_t drawCalls = 0;
  std::uint32_t dynamicVertices = 0;
};

struct OutlineMaskDraw {
  std::uint32_t firstVertex = 0;
  std::uint32_t vertexCount = 0;
  OutlineState state = {};
  MeshHandle mesh = MeshHandle::Invalid;
  std::uint32_t firstInstance = 0;
  std::uint32_t instanceCount = 0;
  bool gltfPlayerModel = false;
  std::uint32_t gltfFirstInstance = 0;
  std::uint32_t gltfInstanceCount = 0;
};

struct GltfPlayerModelInstance {
  Vec3 modelRow0 = {};
  Vec3 modelRow1 = {};
  Vec3 modelRow2 = {};
  Vec3 modelTranslation = {};
  RenderColor color = {};
  GltfModelBounds localBounds = {};
  std::uint32_t firstBone = 0;
  std::uint32_t boneCount = 0;
  std::uint8_t playerIndex = 0;
  OutlineState outlineState = {};
  bool skinned = false;
  bool outlined = false;
};

struct GltfPlayerModelBatch {
  std::uint32_t primitiveIndex = 0;
  std::uint32_t firstInstance = 0;
  std::uint32_t instanceCount = 0;
};

constexpr float kOutlineWorkScale = 0.5F;
constexpr float kMaxOutlineWidthFinalPixels = 6.0F;
constexpr float kMaxOutlineRadiusWorkPixels =
  kMaxOutlineWidthFinalPixels * kOutlineWorkScale;
constexpr std::uint32_t kOutlineFixedDilationRadiusPixels = 3;
constexpr std::uint32_t kOutlineFixedDilationKernelTaps =
  (kOutlineFixedDilationRadiusPixels * 2U + 1U) *
  (kOutlineFixedDilationRadiusPixels * 2U + 1U);

struct OutlineTargetDimensions {
  std::uint32_t framebufferWidth = 0;
  std::uint32_t framebufferHeight = 0;
  std::uint32_t workWidth = 0;
  std::uint32_t workHeight = 0;
  float workScale = kOutlineWorkScale;
};

struct OutlinePixelRect {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;

  [[nodiscard]] bool valid() const {
    return width > 0 && height > 0;
  }
};

struct OutlineWorkPlan {
  OutlineTargetDimensions dimensions = {};
  OutlinePixelRect finalRect = {};
  OutlinePixelRect workRect = {};
  bool hasWork = false;
  bool conservativeFallback = false;
  float maxFinalWidthPixels = 0.0F;
  float maxWorkRadiusPixels = 0.0F;
  std::uint32_t outlinedPlayers = 0;
  std::uint32_t maskDrawCalls = 0;
  std::uint32_t dilationDrawCalls = 0;
  std::uint32_t compositeDrawCalls = 0;
  std::uint32_t uploadBytes = 0;
};

[[nodiscard]] inline float outlineFinalWidthPixels(float requestedWidthPixels) {
  if (!std::isfinite(requestedWidthPixels)) {
    return 0.0F;
  }
  return std::clamp(requestedWidthPixels, 0.0F, kMaxOutlineWidthFinalPixels);
}

[[nodiscard]] inline float outlineWorkRadiusPixels(float requestedWidthPixels) {
  return outlineFinalWidthPixels(requestedWidthPixels) * kOutlineWorkScale;
}

[[nodiscard]] inline OutlineTargetDimensions outlineTargetDimensions(
  std::uint32_t framebufferWidth,
  std::uint32_t framebufferHeight
) {
  return {
    framebufferWidth,
    framebufferHeight,
    (framebufferWidth + 1U) / 2U,
    (framebufferHeight + 1U) / 2U,
    kOutlineWorkScale,
  };
}

[[nodiscard]] inline OutlinePixelRect fullOutlineRect(
  std::uint32_t width,
  std::uint32_t height
) {
  return {
    0,
    0,
    static_cast<std::int32_t>(width),
    static_cast<std::int32_t>(height),
  };
}

[[nodiscard]] inline OutlinePixelRect outlineWorkRectFromFinalRect(
  OutlinePixelRect rect,
  const OutlineTargetDimensions& dimensions
) {
  const float scale = dimensions.workScale;
  const auto x0 = static_cast<std::int32_t>(
    std::floor(static_cast<float>(rect.x) * scale)
  );
  const auto y0 = static_cast<std::int32_t>(
    std::floor(static_cast<float>(rect.y) * scale)
  );
  const auto x1 = static_cast<std::int32_t>(
    std::ceil(static_cast<float>(rect.x + rect.width) * scale)
  );
  const auto y1 = static_cast<std::int32_t>(
    std::ceil(static_cast<float>(rect.y + rect.height) * scale)
  );
  const std::int32_t maxX =
    static_cast<std::int32_t>(dimensions.workWidth);
  const std::int32_t maxY =
    static_cast<std::int32_t>(dimensions.workHeight);
  const std::int32_t clampedX0 = std::clamp(x0, 0, maxX);
  const std::int32_t clampedY0 = std::clamp(y0, 0, maxY);
  const std::int32_t clampedX1 = std::clamp(x1, 0, maxX);
  const std::int32_t clampedY1 = std::clamp(y1, 0, maxY);
  return {
    clampedX0,
    clampedY0,
    clampedX1 - clampedX0,
    clampedY1 - clampedY0,
  };
}

[[nodiscard]] inline OutlineWorkPlan buildOutlineWorkPlan(
  const PerspectiveCamera& camera,
  std::span<const Vertex3D> vertices,
  std::span<const StaticMeshInstance> staticMeshInstances,
  std::span<const GltfPlayerModelInstance> gltfPlayerModelInstances,
  std::span<const OutlineMaskDraw> draws,
  std::uint32_t framebufferWidth,
  std::uint32_t framebufferHeight
) {
  OutlineWorkPlan plan;
  plan.dimensions = outlineTargetDimensions(framebufferWidth, framebufferHeight);
  plan.outlinedPlayers = static_cast<std::uint32_t>(draws.size());
  if (
    framebufferWidth == 0U ||
    framebufferHeight == 0U ||
    plan.dimensions.workWidth == 0U ||
    plan.dimensions.workHeight == 0U ||
    draws.empty()
  ) {
    return plan;
  }

  float minX = std::numeric_limits<float>::max();
  float minY = std::numeric_limits<float>::max();
  float maxX = std::numeric_limits<float>::lowest();
  float maxY = std::numeric_limits<float>::lowest();
  bool anyProjected = false;
  bool fallback = false;
  std::uint32_t drawCalls = 0;
  for (const OutlineMaskDraw& draw : draws) {
    if (
      draw.vertexCount == 0U &&
      draw.instanceCount == 0U &&
      (!draw.gltfPlayerModel || draw.gltfInstanceCount == 0U)
    ) {
      continue;
    }
    ++drawCalls;
    plan.maxFinalWidthPixels =
      std::max(plan.maxFinalWidthPixels, outlineFinalWidthPixels(draw.state.widthPixels));
    const auto addProjectedPoint = [&](Vec3 position) {
      ProjectedPoint projected;
      if (!projectPerspectivePoint(camera, position, projected)) {
        fallback = true;
        return;
      }
      const float screenX =
        (projected.x + 1.0F) * 0.5F * static_cast<float>(framebufferWidth);
      const float screenY =
        (1.0F - projected.y) * 0.5F * static_cast<float>(framebufferHeight);
      if (!std::isfinite(screenX) || !std::isfinite(screenY)) {
        fallback = true;
        return;
      }
      minX = std::min(minX, screenX);
      minY = std::min(minY, screenY);
      maxX = std::max(maxX, screenX);
      maxY = std::max(maxY, screenY);
      anyProjected = true;
    };

    if (draw.gltfPlayerModel && draw.gltfInstanceCount > 0U) {
      const std::uint64_t end =
        static_cast<std::uint64_t>(draw.gltfFirstInstance) +
        draw.gltfInstanceCount;
      if (
        draw.gltfFirstInstance >= gltfPlayerModelInstances.size() ||
        end > gltfPlayerModelInstances.size()
      ) {
        fallback = true;
        break;
      }
      for (
        std::uint32_t index = draw.gltfFirstInstance;
        index < draw.gltfFirstInstance + draw.gltfInstanceCount;
        ++index
      ) {
        const GltfPlayerModelInstance& instance = gltfPlayerModelInstances[index];
        const GltfModelBounds& bounds = instance.localBounds;
        const std::array<Vec3, 8> corners = {{
          {bounds.min.x, bounds.min.y, bounds.min.z},
          {bounds.max.x, bounds.min.y, bounds.min.z},
          {bounds.max.x, bounds.max.y, bounds.min.z},
          {bounds.min.x, bounds.max.y, bounds.min.z},
          {bounds.min.x, bounds.min.y, bounds.max.z},
          {bounds.max.x, bounds.min.y, bounds.max.z},
          {bounds.max.x, bounds.max.y, bounds.max.z},
          {bounds.min.x, bounds.max.y, bounds.max.z},
        }};
        for (Vec3 local : corners) {
          addProjectedPoint({
            dot(instance.modelRow0, local) + instance.modelTranslation.x,
            dot(instance.modelRow1, local) + instance.modelTranslation.y,
            dot(instance.modelRow2, local) + instance.modelTranslation.z,
          });
          if (fallback) {
            break;
          }
        }
        if (fallback) {
          break;
        }
      }
    } else if (draw.instanceCount > 0U) {
      const std::uint64_t end =
        static_cast<std::uint64_t>(draw.firstInstance) + draw.instanceCount;
      if (
        draw.firstInstance >= staticMeshInstances.size() ||
        end > staticMeshInstances.size()
      ) {
        fallback = true;
        break;
      }
      constexpr std::array<Vec3, 8> kUnitCubeCorners = {{
        {-0.5F, -0.5F, -0.5F},
        { 0.5F, -0.5F, -0.5F},
        { 0.5F,  0.5F, -0.5F},
        {-0.5F,  0.5F, -0.5F},
        {-0.5F, -0.5F,  0.5F},
        { 0.5F, -0.5F,  0.5F},
        { 0.5F,  0.5F,  0.5F},
        {-0.5F,  0.5F,  0.5F},
      }};
      for (
        std::uint32_t index = draw.firstInstance;
        index < draw.firstInstance + draw.instanceCount;
        ++index
      ) {
        const StaticMeshInstance& instance = staticMeshInstances[index];
        for (Vec3 local : kUnitCubeCorners) {
          addProjectedPoint({
            dot(instance.modelRow0, local) + instance.modelTranslation.x,
            dot(instance.modelRow1, local) + instance.modelTranslation.y,
            dot(instance.modelRow2, local) + instance.modelTranslation.z,
          });
          if (fallback) {
            break;
          }
        }
        if (fallback) {
          break;
        }
      }
    } else {
      const std::uint64_t end =
        static_cast<std::uint64_t>(draw.firstVertex) + draw.vertexCount;
      if (draw.firstVertex >= vertices.size() || end > vertices.size()) {
        fallback = true;
        break;
      }
      for (
        std::uint32_t index = draw.firstVertex;
        index < draw.firstVertex + draw.vertexCount;
        ++index
      ) {
        addProjectedPoint(vertices[index].position);
        if (fallback) {
          break;
        }
      }
    }
    if (fallback) {
      break;
    }
  }
  plan.maskDrawCalls = drawCalls;
  plan.maxWorkRadiusPixels = outlineWorkRadiusPixels(plan.maxFinalWidthPixels);

  if (drawCalls == 0U) {
    return plan;
  }

  if (fallback || !anyProjected) {
    plan.conservativeFallback = true;
    plan.finalRect = fullOutlineRect(framebufferWidth, framebufferHeight);
    plan.workRect = fullOutlineRect(
      plan.dimensions.workWidth,
      plan.dimensions.workHeight
    );
    plan.hasWork = plan.finalRect.valid() && plan.workRect.valid();
  } else {
    constexpr float kFilteringMarginPixels = 2.0F;
    constexpr float kAnimationSafetyMarginPixels = 2.0F;
    const float margin =
      plan.maxFinalWidthPixels + kFilteringMarginPixels + kAnimationSafetyMarginPixels;
    const std::int32_t x0 = std::clamp(
      static_cast<std::int32_t>(std::floor(minX - margin)),
      0,
      static_cast<std::int32_t>(framebufferWidth)
    );
    const std::int32_t y0 = std::clamp(
      static_cast<std::int32_t>(std::floor(minY - margin)),
      0,
      static_cast<std::int32_t>(framebufferHeight)
    );
    const std::int32_t x1 = std::clamp(
      static_cast<std::int32_t>(std::ceil(maxX + margin)),
      0,
      static_cast<std::int32_t>(framebufferWidth)
    );
    const std::int32_t y1 = std::clamp(
      static_cast<std::int32_t>(std::ceil(maxY + margin)),
      0,
      static_cast<std::int32_t>(framebufferHeight)
    );
    plan.finalRect = {x0, y0, x1 - x0, y1 - y0};
    plan.workRect = outlineWorkRectFromFinalRect(plan.finalRect, plan.dimensions);
    plan.hasWork = plan.finalRect.valid() && plan.workRect.valid();
  }

  if (plan.hasWork) {
    plan.dilationDrawCalls = 1U;
    plan.compositeDrawCalls = 1U;
  }
  return plan;
}

[[nodiscard]] inline OutlineWorkPlan buildOutlineWorkPlan(
  const PerspectiveCamera& camera,
  std::span<const Vertex3D> vertices,
  std::span<const OutlineMaskDraw> draws,
  std::uint32_t framebufferWidth,
  std::uint32_t framebufferHeight
) {
  return buildOutlineWorkPlan(
    camera,
    vertices,
    std::span<const StaticMeshInstance>(),
    std::span<const GltfPlayerModelInstance>(),
    draws,
    framebufferWidth,
    framebufferHeight
  );
}

[[nodiscard]] inline OutlineWorkPlan buildOutlineWorkPlan(
  const PerspectiveCamera& camera,
  std::span<const Vertex3D> vertices,
  std::span<const StaticMeshInstance> staticMeshInstances,
  std::span<const OutlineMaskDraw> draws,
  std::uint32_t framebufferWidth,
  std::uint32_t framebufferHeight
) {
  return buildOutlineWorkPlan(
    camera,
    vertices,
    staticMeshInstances,
    std::span<const GltfPlayerModelInstance>(),
    draws,
    framebufferWidth,
    framebufferHeight
  );
}

struct Scene3D {
  PerspectiveCamera camera = {};
  std::vector<Vertex3D> vertices;
  std::vector<Vertex3D> translucentVertices;
  std::vector<OutlineMaskDraw> outlineMaskDraws;
  std::vector<StaticMeshInstance> staticMeshInstances;
  std::vector<StaticMeshBatch> staticMeshBatches;
  std::vector<GltfPlayerModelInstance> gltfPlayerModelInstances;
  std::vector<GltfPlayerModelBatch> gltfPlayerModelBatches;
  std::vector<std::array<float, 16>> gltfBonePalette;
  std::vector<SimpleRenderInstance> simpleInstances;
  std::vector<SimpleRenderBatch> simpleBatches;
  ProjectileRenderStats projectileStats = {};
  RemoteWeaponRenderStats remoteWeaponStats = {};
  PlayerBoxRenderStats playerBoxStats = {};
  GltfPlayerModelRenderStats gltfPlayerModelStats = {};
  ViewModelRenderStats viewModelStats = {};
  TransientVfxStats transientVfxStats = {};
  std::array<bool, kDuelPlayerCount> remoteRenderVisible = {};
  std::uint32_t visibleRemotePlayers = 0;
  std::uint32_t remoteBodyModelsBuilt = 0;
  std::uint32_t remoteWeaponModelsBuilt = 0;
  std::uint32_t playerOutlinesBuilt = 0;
  std::uint32_t normalPlayerBodyDynamicVertices = 0;
  std::uint32_t geometryOutlineDynamicVertices = 0;
  std::uint32_t outlinedPlayers = 0;
  bool geometryOutlineFallbackUsed = false;
  std::uint32_t remoteCandidates = 0;
  std::uint32_t remoteFrustumVisible = 0;
  std::uint32_t remoteFrustumCulled = 0;
};

[[nodiscard]] const StaticMeshAsset* staticMeshAsset(MeshHandle handle);
[[nodiscard]] MeshHandle remoteWeaponMeshHandle(Weapon weapon);
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
  std::span<const TransientTracer> transientTracers,
  std::span<const TransientEffect> transientEffects,
  const RenderSettings& settings
);

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
  std::span<const TransientTracer> transientTracers,
  const RenderSettings& settings
);

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
  std::span<const TransientTracer> transientTracers,
  std::span<const TransientEffect> transientEffects,
  const RenderSettings& settings,
  float cameraVerticalOffset = 0.0F
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
  std::span<const TransientTracer> transientTracers,
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
