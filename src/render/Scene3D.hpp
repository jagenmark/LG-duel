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
#include <string_view>
#include <vector>

namespace lg {

struct Vertex3D {
  Vec3 position = {};
  RenderColor color = {};
  float u = 0.0F;
  float v = 0.0F;
  std::uint32_t materialId = 0;
  Vec3 normal = {};
  std::uint32_t materialSlot = 0;
  std::uint8_t ambientVisibility = 255;
  std::uint8_t ambientDebug = 0;
};

// Material meshes retain authored normals and compact PBR parameters instead
// of baking a fixed light direction into their vertex colors.
struct WeaponMaterialVertex3D {
  Vec3 position = {};
  Vec3 normal = {};
  RenderColor baseColor = {};
  float metallic = 0.0F;
  float roughness = 1.0F;
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
  RemoteMachineGunBody,
  RemoteMachineGunBarrels,
  RemoteShotgun,
  RemoteGrenadeLauncher,
  RemoteRocketLauncherBody,
  RemoteRocketLauncherRecoil,
  RemoteRocketLauncherLatch,
  RemoteLightningGun,
  RemoteFreezeGunBody,
  RemoteFreezeGunFocus,
  RemoteFreezeGunCoolant,
  RemoteRailgun,
  RemotePlasmaGunBody,
  RemotePlasmaGunProngs,
  RemotePlasmaGunCore,
  RemoteRevolverBody,
  RemoteRevolverCylinder,
  ViewModelRightTriggerGrip,
  ViewModelLeftClosedSupport,
  ViewModelLeftOpenSupport,
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
  LightSource,
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

struct MaterialMeshAsset {
  MeshHandle handle = MeshHandle::Invalid;
  std::span<const WeaponMaterialVertex3D> vertices;
  BoundingSphere localBounds = {};
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
  float pitchRadians = 0.0F;
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

[[nodiscard]] constexpr bool hasBloomSources(
  std::span<const SimpleRenderBatch> batches
) {
  return std::any_of(
    batches.begin(),
    batches.end(),
    [](const SimpleRenderBatch& batch) {
      return batch.instanceCount > 0U &&
        batch.pass == RenderPass::AdditiveGlow;
    }
  );
}

[[nodiscard]] constexpr bool effectiveBloom(
  bool requested,
  std::span<const SimpleRenderBatch> batches
) {
  return requested && hasBloomSources(batches);
}

struct TransientVfxStats {
  std::uint32_t activeEffects = 0;
  std::uint32_t activeMachineGunTracers = 0;
  std::uint32_t activeMachineGunMuzzleFlashes = 0;
  std::uint32_t activeRevolverMuzzleFlashes = 0;
  std::uint32_t activeRocketLauncherMuzzleFlashes = 0;
  std::uint32_t activeSniperSmokeTracers = 0;
  std::uint32_t activeShotgunTracers = 0;
  std::uint32_t activeExplosionEffects = 0;
  std::uint32_t newExplosionEventsConsumed = 0;
  std::uint32_t tracerCandidates = 0;
  std::uint32_t tracerFrustumCulled = 0;
  std::uint32_t tracerInstancesSubmitted = 0;
  std::uint32_t sniperSmokeTracerFrustumCulled = 0;
  std::uint32_t sniperSmokeTracerDynamicVertices = 0;
  std::uint32_t muzzleFlashInstancesSubmitted = 0;
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
  std::uint32_t activeTemporaryLights = 0;
  std::uint32_t activeCasings = 0;
  std::uint32_t activeImpactParticles = 0;
  std::uint32_t activeBulletDecals = 0;
  std::uint32_t transparentEffectsSubmitted = 0;
};

struct TemporaryLight {
  Vec3 position = {};
  Vec3 color = {1.0F, 0.55F, 0.2F};
  float intensity = 0.0F;
  float radius = 0.0F;
};

inline constexpr std::size_t kMaxLivePointLights = 32U;
inline constexpr std::size_t kMaxPointShadowLights = 2U;

struct LivePointLight {
  Vec3 position = {};
  Vec3 color = {1.0F, 1.0F, 1.0F};
  float intensity = 0.0F;
  float selectionIntensity = 0.0F;
  float radius = 0.0F;
  float sourceRadius = 0.0F;
  float selectionFade = 1.0F;
  std::int16_t priority = 0;
  std::uint16_t sourceIndex = 0;
  bool authored = false;
  bool affectsStaticWorld = true;
  bool castsShadows = false;
  bool flickering = false;
  bool temporary = false;
};

struct PointLightSelectionStats {
  std::uint32_t authored = 0;
  std::uint32_t candidates = 0;
  std::uint32_t frustumCulled = 0;
  std::uint32_t selected = 0;
  std::uint32_t dropped = 0;
  std::uint32_t flickering = 0;
  std::uint32_t shadowed = 0;
  std::uint32_t closeRetained = 0;
};

[[nodiscard]] constexpr std::size_t livePointLightCapacity(int quality) {
  return quality <= 0 ? 8U : quality == 1 ? 16U : kMaxLivePointLights;
}

[[nodiscard]] constexpr bool staticLightBakesIntoWorld(
  const ArenaStaticLight& light
) {
  return !light.castsShadows && !light.flickerEnabled;
}

[[nodiscard]] float pointLightFlickerFactor(
  std::uint32_t seed,
  float frequencyHz,
  float minFactor,
  float maxFactor,
  double timeSeconds
);

[[nodiscard]] std::vector<LivePointLight> selectLivePointLights(
  std::span<const LivePointLight> candidates,
  const PerspectiveCamera& camera,
  std::size_t capacity,
  PointLightSelectionStats* stats = nullptr
);

[[nodiscard]] std::vector<LivePointLight> selectPointShadowLights(
  std::span<const LivePointLight> liveLights,
  const PerspectiveCamera& camera,
  std::size_t capacity
);

enum class PointShadowFace : std::uint8_t {
  PositiveX = 0,
  NegativeX,
  PositiveY,
  NegativeY,
  PositiveZ,
  NegativeZ,
};

struct PointShadowFaceProjection {
  PointShadowFace face = PointShadowFace::PositiveX;
  Vec3 right = {0.0F, -1.0F, 0.0F};
  Vec3 up = {0.0F, 0.0F, 1.0F};
  Vec3 forward = {1.0F, 0.0F, 0.0F};
};

[[nodiscard]] PointShadowFace pointShadowFace(Vec3 direction);
[[nodiscard]] PointShadowFaceProjection pointShadowFaceProjection(
  PointShadowFace face
);
[[nodiscard]] constexpr std::uint32_t pointShadowLayer(
  std::size_t shadowSlot,
  PointShadowFace face
) {
  return static_cast<std::uint32_t>(
    shadowSlot * 6U + static_cast<std::size_t>(face)
  );
}

struct SunShadowProjection {
  Vec3 origin = {};
  Vec3 right = {1.0F, 0.0F, 0.0F};
  Vec3 up = {0.0F, 1.0F, 0.0F};
  Vec3 forward = {0.0F, 0.0F, -1.0F};
  float halfExtent = 32.0F;
  float nearPlane = 0.0F;
  float farPlane = 96.0F;
  std::uint32_t mapSize = 0;
  float depthBias = 0.0012F;
  float normalBias = 0.018F;
};

struct SceneLightData {
  Vec3 sunDirection = {0.35F, 0.45F, -0.82F};
  Vec3 sunColor = {1.0F, 1.0F, 1.0F};
  float sunIntensity = 0.0F;
  Vec3 fillColor = {0.30F, 0.36F, 0.46F};
  float fillIntensity = 0.0F;
  float exposure = 1.0F;
  int gradeQuality = 0;
  int materialQuality = 2;
  int playerRimQuality = 2;
  SunShadowProjection shadow = {};
};

enum class WorldMaterialKind : std::uint8_t {
  Generic = 0,
  Metal,
  OxidizedMetal,
  Chain,
  Tech,
  Masonry,
  Wood,
  Energy,
};

struct WorldMaterialTraits {
  WorldMaterialKind kind = WorldMaterialKind::Generic;
  float roughness = 0.78F;
  float metallic = 0.0F;
  float specular = 0.18F;
  float emissive = 0.0F;
};

struct WorldMaterialLightingPlan {
  float diffuseScale = 1.0F;
  float specularScale = 0.0F;
  float emissiveScale = 0.0F;
};

[[nodiscard]] WorldMaterialTraits classifyWorldMaterial(
  std::string_view materialPath
);

[[nodiscard]] constexpr WorldMaterialLightingPlan worldMaterialLightingPlan(
  WorldMaterialTraits traits,
  int quality
) {
  return {
    1.0F,
    quality <= 0
      ? 0.0F
      : traits.specular * (quality == 1 ? 0.55F : 1.0F),
    traits.emissive,
  };
}

[[nodiscard]] inline std::uint32_t antiAliasingSampleCount(int quality) {
  return quality <= 0 ? 1U : quality == 1 ? 2U : 4U;
}

[[nodiscard]] inline std::uint32_t sunShadowMapSize(int quality) {
  return quality <= 0 ? 0U : quality == 1 ? 1024U : 2048U;
}

[[nodiscard]] SunShadowProjection buildSunShadowProjection(
  const PerspectiveCamera& camera,
  Vec3 sunDirection,
  int quality
);

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
  bool playerSilhouetteOutlined = false;
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
  std::uint32_t shadowCasterInstances = 0;
  std::uint32_t shadowCasterDrawCalls = 0;
  std::uint32_t outlineMaskBatches = 0;
  std::uint32_t outlineMaskDrawCalls = 0;
  std::uint32_t legacyCpuSkinnedVertexUploadBytes = 0;
};

struct GltfShadowCasterPlan {
  std::uint32_t instances = 0;
  std::uint32_t drawCalls = 0;
};

[[nodiscard]] constexpr GltfShadowCasterPlan gltfShadowCasterPlan(
  std::uint32_t instanceCount,
  std::uint32_t primitiveDrawCalls,
  std::uint32_t shadowMapSize
) {
  return shadowMapSize == 0U
    ? GltfShadowCasterPlan{}
    : GltfShadowCasterPlan{instanceCount, primitiveDrawCalls};
}

struct ViewModelRenderStats {
  std::uint32_t drawCalls = 0;
  std::uint32_t dynamicVertices = 0;
  std::uint32_t sharedHandVertices = 0;
  std::uint32_t sharedHandStaticGpuBytes = 0;
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

struct GltfPlayerModelBasisColumns {
  Vec3 right = {};
  Vec3 up = {};
  Vec3 forward = {};
};

[[nodiscard]] inline GltfPlayerModelBasisColumns gltfPlayerModelBasisColumns(
  const GltfPlayerModelInstance& instance
) {
  return {
    {
      instance.modelRow0.x,
      instance.modelRow1.x,
      instance.modelRow2.x,
    },
    {
      instance.modelRow0.y,
      instance.modelRow1.y,
      instance.modelRow2.y,
    },
    {
      instance.modelRow0.z,
      instance.modelRow1.z,
      instance.modelRow2.z,
    },
  };
}

[[nodiscard]] inline bool gltfPlayerModelBasisIsOrthogonal(
  const GltfPlayerModelInstance& instance,
  float maximumNormalizedDot = 0.0001F
) {
  const GltfPlayerModelBasisColumns columns = gltfPlayerModelBasisColumns(instance);
  const float rightLength = length(columns.right);
  const float upLength = length(columns.up);
  const float forwardLength = length(columns.forward);
  if (
    rightLength <= 0.00001F ||
    upLength <= 0.00001F ||
    forwardLength <= 0.00001F
  ) {
    return false;
  }
  const Vec3 right = columns.right / rightLength;
  const Vec3 up = columns.up / upLength;
  const Vec3 forward = columns.forward / forwardLength;
  return std::fabs(dot(right, up)) <= maximumNormalizedDot &&
    std::fabs(dot(right, forward)) <= maximumNormalizedDot &&
    std::fabs(dot(up, forward)) <= maximumNormalizedDot;
}

// GLTF player model rows are built from an orthogonal right/up/forward basis
// with independent horizontal and vertical scales. A normal therefore needs
// the reciprocal scale on each basis column, not the position transform.
[[nodiscard]] inline Vec3 transformGltfPlayerModelNormal(
  const GltfPlayerModelInstance& instance,
  Vec3 localNormal
) {
  constexpr float kMinimumScaleSquared = 0.00000001F;
  const GltfPlayerModelBasisColumns columns = gltfPlayerModelBasisColumns(instance);
  const Vec3 transformed =
    columns.right * (localNormal.x / std::max(
      dot(columns.right, columns.right),
      kMinimumScaleSquared
    )) +
    columns.up * (localNormal.y / std::max(
      dot(columns.up, columns.up),
      kMinimumScaleSquared
    )) +
    columns.forward * (localNormal.z / std::max(
      dot(columns.forward, columns.forward),
      kMinimumScaleSquared
    ));
  return normalize(transformed);
}

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

[[nodiscard]] inline float outlineWorkRadiusPixels(
  float requestedWidthPixels,
  float workScale
) {
  return outlineFinalWidthPixels(requestedWidthPixels) *
    std::clamp(workScale, 0.0F, 1.0F);
}

[[nodiscard]] inline OutlineTargetDimensions outlineTargetDimensions(
  std::uint32_t framebufferWidth,
  std::uint32_t framebufferHeight,
  float workScale = kOutlineWorkScale
) {
  const float scale = std::clamp(workScale, 0.0F, 1.0F);
  const bool halfResolution = scale == kOutlineWorkScale;
  const bool nativeResolution = scale == 1.0F;
  return {
    framebufferWidth,
    framebufferHeight,
    nativeResolution
      ? framebufferWidth
      : halfResolution
      ? framebufferWidth / 2U + framebufferWidth % 2U
      : static_cast<std::uint32_t>(
          std::ceil(static_cast<float>(framebufferWidth) * scale)
        ),
    nativeResolution
      ? framebufferHeight
      : halfResolution
      ? framebufferHeight / 2U + framebufferHeight % 2U
      : static_cast<std::uint32_t>(
          std::ceil(static_cast<float>(framebufferHeight) * scale)
        ),
    scale,
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
  std::uint32_t framebufferHeight,
  float workScale = kOutlineWorkScale
) {
  OutlineWorkPlan plan;
  plan.dimensions = outlineTargetDimensions(
    framebufferWidth,
    framebufferHeight,
    workScale
  );
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
      for (
        std::uint32_t index = draw.firstInstance;
        index < draw.firstInstance + draw.instanceCount;
        ++index
      ) {
        const StaticMeshInstance& instance = staticMeshInstances[index];
        const BoundingSphere& bounds = instance.worldBounds;
        if (
          !std::isfinite(bounds.radius) ||
          bounds.radius <= 0.0F
        ) {
          fallback = true;
          break;
        }
        // A camera-aligned cube around the real mesh sphere is conservative
        // for long held weapons and avoids clipping them with the work scissor.
        for (int forwardSign : {-1, 1}) {
          for (int upSign : {-1, 1}) {
            for (int rightSign : {-1, 1}) {
              addProjectedPoint(
                bounds.center +
                camera.forward * (bounds.radius * static_cast<float>(forwardSign)) +
                camera.up * (bounds.radius * static_cast<float>(upSign)) +
                camera.right * (bounds.radius * static_cast<float>(rightSign))
              );
              if (fallback) {
                break;
              }
            }
            if (fallback) {
              break;
            }
          }
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
  plan.maxWorkRadiusPixels = outlineWorkRadiusPixels(
    plan.maxFinalWidthPixels,
    plan.dimensions.workScale
  );

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
  SceneLightData lights = {};
  std::vector<Vertex3D> vertices;
  std::vector<Vertex3D> contactShadowVertices;
  std::vector<Vertex3D> translucentVertices;
  std::vector<OutlineMaskDraw> outlineMaskDraws;
  std::vector<StaticMeshInstance> staticMeshInstances;
  std::vector<StaticMeshBatch> staticMeshBatches;
  std::vector<GltfPlayerModelInstance> gltfPlayerModelInstances;
  std::vector<GltfPlayerModelBatch> gltfPlayerModelBatches;
  std::vector<std::array<float, 16>> gltfBonePalette;
  std::vector<SimpleRenderInstance> simpleInstances;
  std::vector<SimpleRenderBatch> simpleBatches;
  std::vector<TemporaryLight> temporaryLights;
  std::vector<LivePointLight> livePointLights;
  PointLightSelectionStats pointLightStats = {};
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

// Appends presentation-only translucent collision geometry. The mode matches
// r_show_collision and never changes which authoritative traces use a solid.
void appendCollisionDebugGeometry(
  Scene3D& scene,
  const Arena& arena,
  int mode
);

[[nodiscard]] const StaticMeshAsset* staticMeshAsset(MeshHandle handle);
[[nodiscard]] const MaterialMeshAsset* materialMeshAsset(MeshHandle handle);
[[nodiscard]] MeshHandle remoteWeaponMeshHandle(Weapon weapon);
[[nodiscard]] Vec3 machineGunBarrelPivot();
[[nodiscard]] Vec3 machineGunMuzzleSocket();
[[nodiscard]] Vec3 machineGunCasingEjectSocket();
[[nodiscard]] Vec3 firstPersonMachineGunMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
);
[[nodiscard]] Vec3 firstPersonMachineGunCasingEjectPosition(
  const PlayerState& player,
  const RenderSettings& settings
);
[[nodiscard]] Vec3 revolverMuzzleSocket();
[[nodiscard]] Vec3 firstPersonRevolverMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
);
[[nodiscard]] Vec3 rocketLauncherMuzzleSocket();
[[nodiscard]] Vec3 rocketLauncherGripSocket();
[[nodiscard]] Vec3 firstPersonRocketLauncherMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
);
[[nodiscard]] Vec3 remoteRocketLauncherMuzzlePosition(
  const RemotePlayerView& remote,
  const RenderSettings& settings
);
[[nodiscard]] Vec3 firstPersonFreezeGunMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
);
[[nodiscard]] Vec3 plasmaGunMuzzleSocket();
[[nodiscard]] Vec3 plasmaGunGripSocket();
[[nodiscard]] Vec3 sniperRifleGripSocket();
[[nodiscard]] Vec3 sniperRifleMuzzleSocket();
[[nodiscard]] Vec3 firstPersonSniperRifleMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
);
[[nodiscard]] Vec3 remoteSniperRifleMuzzlePosition(
  const RemotePlayerView& remote,
  const RenderSettings& settings
);
[[nodiscard]] Vec3 firstPersonPlasmaGunMuzzlePosition(
  const PlayerState& player,
  const RenderSettings& settings
);
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
  const std::array<bool, Arena::kHealthPickupCount>& healthPickupAvailable,
  std::span<const TransientTracer> transientTracers,
  std::span<const TransientEffect> transientEffects,
  std::span<const IcePool> icePools,
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
  std::span<const TransientEffect> transientEffects,
  std::span<const IcePool> icePools,
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
  const std::array<bool, Arena::kHealthPickupCount>& healthPickupAvailable,
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
  std::span<const TransientEffect> transientEffects,
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

struct StaticAmbientBakeStats;

[[nodiscard]] Scene3D buildStaticWorldScene(
  const Arena& arena,
  int ambientQuality = 0,
  int ambientDebug = 0,
  StaticAmbientBakeStats* ambientStats = nullptr
);

} // namespace lg
