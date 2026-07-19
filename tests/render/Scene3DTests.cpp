#include "render/GltfSkinnedModel.hpp"
#include "render/Scene3D.hpp"
#include "sim/Arena.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) {
    return 0;
  }
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

bool nearlyEqual(float lhs, float rhs, float epsilon = 0.001F) {
  return std::fabs(lhs - rhs) <= epsilon;
}

bool sameColor(lg::RenderColor lhs, lg::RenderColor rhs) {
  return lhs.red == rhs.red &&
    lhs.green == rhs.green &&
    lhs.blue == rhs.blue &&
    lhs.alpha == rhs.alpha;
}

bool isEnemyModelColor(lg::RenderColor color) {
  return color.red >= 110 &&
    color.green <= 170 &&
    color.blue <= 180 &&
    color.alpha == 255;
}

bool finiteVec3(lg::Vec3 value) {
  return std::isfinite(value.x) &&
    std::isfinite(value.y) &&
    std::isfinite(value.z);
}

lg::BoundingSphere legacyMaterialMeshBounds(
  std::span<const lg::WeaponMaterialVertex3D> vertices
) {
  if (vertices.empty()) {
    return {};
  }
  lg::Vec3 center = {};
  for (const lg::WeaponMaterialVertex3D& vertex : vertices) {
    center += vertex.position;
  }
  center *= 1.0F / static_cast<float>(vertices.size());
  float radius = 0.0F;
  for (const lg::WeaponMaterialVertex3D& vertex : vertices) {
    radius = std::max(radius, lg::length(vertex.position - center));
  }
  return {center, radius};
}

bool validStaticMeshBatches(const lg::Scene3D& scene) {
  std::size_t covered = 0U;
  for (std::size_t batchIndex = 0U;
       batchIndex < scene.staticMeshBatches.size();
       ++batchIndex) {
    const lg::StaticMeshBatch& batch = scene.staticMeshBatches[batchIndex];
    if (
      batch.firstInstance != covered ||
      batch.instanceCount == 0U ||
      covered + batch.instanceCount > scene.staticMeshInstances.size()
    ) {
      return false;
    }
    if (batchIndex > 0U) {
      const lg::StaticMeshBatch& previous = scene.staticMeshBatches[batchIndex - 1U];
      if (previous.mesh == batch.mesh && previous.pass == batch.pass) {
        return false;
      }
    }
    for (std::size_t offset = 0U; offset < batch.instanceCount; ++offset) {
      const lg::StaticMeshInstance& instance =
        scene.staticMeshInstances[covered + offset];
      if (instance.mesh != batch.mesh || instance.pass != batch.pass) {
        return false;
      }
    }
    covered += batch.instanceCount;
  }
  return covered == scene.staticMeshInstances.size() &&
    std::is_sorted(
      scene.staticMeshInstances.begin(),
      scene.staticMeshInstances.end(),
      [](const lg::StaticMeshInstance& lhs, const lg::StaticMeshInstance& rhs) {
        if (lhs.pass != rhs.pass) {
          return static_cast<int>(lhs.pass) < static_cast<int>(rhs.pass);
        }
        return static_cast<std::uint16_t>(lhs.mesh) <
          static_cast<std::uint16_t>(rhs.mesh);
      }
    );
}

bool validSimpleBatches(const lg::Scene3D& scene) {
  std::size_t covered = 0U;
  for (std::size_t batchIndex = 0U;
       batchIndex < scene.simpleBatches.size();
       ++batchIndex) {
    const lg::SimpleRenderBatch& batch = scene.simpleBatches[batchIndex];
    if (
      batch.firstInstance != covered ||
      batch.instanceCount == 0U ||
      covered + batch.instanceCount > scene.simpleInstances.size()
    ) {
      return false;
    }
    if (batchIndex > 0U) {
      const lg::SimpleRenderBatch& previous = scene.simpleBatches[batchIndex - 1U];
      if (
        previous.mesh == batch.mesh &&
        previous.billboard == batch.billboard &&
        previous.pass == batch.pass
      ) {
        return false;
      }
    }
    for (std::size_t offset = 0U; offset < batch.instanceCount; ++offset) {
      const lg::SimpleRenderInstance& instance = scene.simpleInstances[covered + offset];
      if (
        instance.mesh != batch.mesh ||
        instance.billboard != batch.billboard ||
        instance.pass != batch.pass
      ) {
        return false;
      }
    }
    covered += batch.instanceCount;
  }
  return covered == scene.simpleInstances.size() &&
    std::is_sorted(
      scene.simpleInstances.begin(),
      scene.simpleInstances.end(),
      [](const lg::SimpleRenderInstance& lhs, const lg::SimpleRenderInstance& rhs) {
        if (lhs.pass != rhs.pass) {
          return static_cast<int>(lhs.pass) < static_cast<int>(rhs.pass);
        }
        if (lhs.mesh != rhs.mesh) {
          return static_cast<std::uint16_t>(lhs.mesh) <
            static_cast<std::uint16_t>(rhs.mesh);
        }
        return static_cast<std::uint16_t>(lhs.billboard) <
          static_cast<std::uint16_t>(rhs.billboard);
      }
    );
}

lg::Vec3 transformPoint(const lg::StaticMeshInstance& instance, lg::Vec3 local) {
  return {
    lg::dot(instance.modelRow0, local) + instance.modelTranslation.x,
    lg::dot(instance.modelRow1, local) + instance.modelTranslation.y,
    lg::dot(instance.modelRow2, local) + instance.modelTranslation.z,
  };
}

std::size_t playerBoxInstanceCount(const lg::Scene3D& scene) {
  return static_cast<std::size_t>(std::count_if(
    scene.staticMeshInstances.begin(),
    scene.staticMeshInstances.end(),
    [](const lg::StaticMeshInstance& instance) {
      return instance.playerBoxBody &&
        instance.mesh == lg::MeshHandle::PlayerBoxCube;
    }
  ));
}

float maxPaletteDelta(
  std::span<const std::array<float, 16>> lhs,
  std::span<const std::array<float, 16>> rhs
) {
  const std::size_t count = std::min(lhs.size(), rhs.size());
  float delta = 0.0F;
  for (std::size_t index = 0; index < count; ++index) {
    for (std::size_t value = 0; value < 16U; ++value) {
      delta = std::max(delta, std::fabs(lhs[index][value] - rhs[index][value]));
    }
  }
  return delta;
}

float maxPaletteDeltaAtIndices(
  std::span<const std::array<float, 16>> lhs,
  std::span<const std::array<float, 16>> rhs,
  std::span<const std::size_t> indices
) {
  float delta = 0.0F;
  for (std::size_t index : indices) {
    if (index >= lhs.size() || index >= rhs.size()) {
      continue;
    }
    for (std::size_t value = 0; value < 16U; ++value) {
      delta = std::max(delta, std::fabs(lhs[index][value] - rhs[index][value]));
    }
  }
  return delta;
}

float maxVertexX(const lg::Scene3D& scene) {
  float result = -std::numeric_limits<float>::infinity();
  for (const lg::Vertex3D& vertex : scene.vertices) {
    result = std::max(result, vertex.position.x);
  }
  for (const lg::Vertex3D& vertex : scene.translucentVertices) {
    result = std::max(result, vertex.position.x);
  }
  return result;
}

bool hasAnyVertex(const lg::Scene3D& scene) {
  return !scene.vertices.empty() || !scene.translucentVertices.empty();
}

struct UvBounds {
  float minU = 0.0F;
  float maxU = 0.0F;
  float minV = 0.0F;
  float maxV = 0.0F;
  bool found = false;
};

UvBounds texturedWallUvBounds(
  const lg::TextureProjection& projection,
  float quakeSize = 128.0F
) {
  lg::Arena arena;
  arena.wallCount = 1;
  arena.walls[0].min = {0.0F, 0.0F, 0.0F};
  arena.walls[0].max = {quakeSize / 40.0F, quakeSize / 40.0F, 16.0F / 40.0F};
  const std::uint32_t materialId = lg::arenaMaterialId("128x128/test/checker");
  arena.walls[0].faceMaterialIds[1] = materialId;
  arena.walls[0].faceTextureProjections[1] = projection;

  const lg::Scene3D scene = lg::buildStaticWorldScene(arena);

  UvBounds bounds;
  for (const lg::Vertex3D& vertex : scene.vertices) {
    if (vertex.materialId != materialId) {
      continue;
    }
    if (!bounds.found) {
      bounds.minU = vertex.u;
      bounds.maxU = vertex.u;
      bounds.minV = vertex.v;
      bounds.maxV = vertex.v;
      bounds.found = true;
      continue;
    }
    bounds.minU = std::min(bounds.minU, vertex.u);
    bounds.maxU = std::max(bounds.maxU, vertex.u);
    bounds.minV = std::min(bounds.minV, vertex.v);
    bounds.maxV = std::max(bounds.maxV, vertex.v);
  }
  return bounds;
}

} // namespace

int main() {
  int failures = 0;
  lg::Arena arena;
  arena.wallCount = 0;
  lg::PlayerState player;
  player.position = {1.0F, 2.0F, 0.9F};
  player.viewYawRadians = 0.0F;
  player.viewPitchRadians = 0.0F;
  player.movementMode = lg::MovementMode::Grounded;
  player.onGround = true;
  lg::PlayerState opponent;
  opponent.position = {4.0F, 2.0F, 0.9F};
  opponent.movementMode = lg::MovementMode::Grounded;
  opponent.onGround = true;
  lg::RenderSettings settings;
  settings.enemyOutlineRed = 31;
  settings.enemyOutlineGreen = 227;
  settings.enemyOutlineBlue = 19;
  settings.playerModel = 1;
  lg::LightningGunResult inactiveBeam;
  const std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> weaponFires = {};
  const std::array<lg::RocketExplosionResult, lg::kDuelPlayerCount> rocketExplosions = {};
  const std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> rockets = {};

  const lg::Scene3D baseScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    !baseScene.gltfPlayerModelInstances.empty() ||
      !baseScene.staticMeshInstances.empty() ||
      !baseScene.vertices.empty(),
    "perspective scene should emit renderable geometry or GPU model instances"
  );
  failures += expect(
    settings.playerOutlineStyle == lg::PlayerOutlineStyle::ScreenSpace &&
      !lg::usesGeometryPlayerOutlineFallback(settings.playerOutlineStyle),
    "SDL_GPU outline settings should prefer screen-space outlines over geometry fallback"
  );
  failures += expect(
    baseScene.visibleRemotePlayers == 1 &&
      baseScene.remoteBodyModelsBuilt == 1 &&
      baseScene.remoteWeaponModelsBuilt == 1 &&
      baseScene.playerOutlinesBuilt == 1 &&
      baseScene.outlinedPlayers == 1 &&
      baseScene.outlineMaskDraws.size() == 1U &&
      baseScene.normalPlayerBodyDynamicVertices == 0 &&
      baseScene.gltfPlayerModelStats.activeInstances == 1 &&
      baseScene.gltfPlayerModelStats.gpuSkinnedInstances == 1 &&
      baseScene.gltfPlayerModelStats.bodyBatches > 0 &&
      baseScene.gltfPlayerModelStats.legacyCpuSkinnedVertexUploadBytes == 0 &&
      baseScene.geometryOutlineDynamicVertices == 0 &&
      !baseScene.geometryOutlineFallbackUsed &&
      baseScene.remoteWeaponStats.instancesSubmitted == 1 &&
      baseScene.remoteWeaponStats.legacyDynamicVertices == 0,
    "GLB render settings should build visible remote body, remote weapon instance, and screen-space outline mask input"
  );

  lg::RenderSettings workerSettings = settings;
  workerSettings.playerModel = 2;
  const lg::Scene3D workerScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, opponent, inactiveBeam, inactiveBeam,
    weaponFires, rocketExplosions, rockets, workerSettings
  );
  failures += expect(
    lg::workerPlayerModel().loaded() &&
      workerScene.gltfPlayerModelStats.activeInstances == 1U &&
      workerScene.gltfPlayerModelStats.gpuSkinnedInstances == 1U &&
      workerScene.remoteWeaponStats.instancesSubmitted == 1U,
    "Worker selection should load a skinned body and retain the remote weapon"
  );
  lg::GltfSkinnedModel::PoseScratch workerPoseScratch;
  std::vector<std::array<float, 16>> workerPalette;
  lg::GltfSkinnedModel::Matrix4 workerSocket;
  const bool workerSocketSampled = lg::workerPlayerModel().appendBonePalette(
    {{"Idle_Gun_TwoHanded", 0.8333333F, 1.0F}},
    workerPalette,
    workerPoseScratch
  ) && lg::workerPlayerModel().nodeGlobalMatrix(
    "weapon_socket", workerPoseScratch, workerSocket
  );
  failures += expect(
    workerSocketSampled &&
      std::all_of(
        workerSocket.values.begin(), workerSocket.values.end(),
        [](float value) { return std::isfinite(value); }
      ),
    "Worker two-handed pose should expose a finite animated weapon socket"
  );

  lg::RenderSettings noWeaponSettings = settings;
  noWeaponSettings.drawRemoteWeapons = false;
  const lg::Scene3D noWeaponScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    noWeaponSettings
  );
  failures += expect(
    noWeaponScene.visibleRemotePlayers == 1 &&
      noWeaponScene.remoteBodyModelsBuilt == 1 &&
      noWeaponScene.remoteWeaponModelsBuilt == 0 &&
      noWeaponScene.remoteWeaponStats.instancesSubmitted == 0 &&
      noWeaponScene.playerOutlinesBuilt == 1 &&
      noWeaponScene.outlineMaskDraws.size() == 1U,
    "disabled remote weapons should prevent only remote weapon model construction"
  );

  lg::RenderSettings noBodySettings = settings;
  noBodySettings.drawRemotePlayers = false;
  lg::LightningGunResult activeOpponentBeam;
  activeOpponentBeam.active = true;
  activeOpponentBeam.start = opponent.position;
  activeOpponentBeam.end = player.position;
  const lg::Scene3D noBodyNoBeamScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    noBodySettings
  );
  const lg::Scene3D noBodyBeamScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    activeOpponentBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    noBodySettings
  );
  failures += expect(
    noBodyBeamScene.visibleRemotePlayers == 1 &&
      noBodyBeamScene.remoteBodyModelsBuilt == 0 &&
      noBodyBeamScene.remoteWeaponModelsBuilt == 1 &&
      noBodyBeamScene.remoteWeaponStats.instancesSubmitted == 1 &&
      noBodyBeamScene.playerOutlinesBuilt == 0 &&
      noBodyBeamScene.outlineMaskDraws.empty() &&
      noBodyBeamScene.vertices.size() > noBodyNoBeamScene.vertices.size(),
    "disabled remote bodies should not suppress unrelated remote effects or scene data"
  );

  const lg::GltfSkinnedModel& duelistModel = lg::duelistMaleModel();
  bool primitivesFiniteAndSafe = duelistModel.loaded() &&
    !duelistModel.primitives().empty() &&
    duelistModel.hasSkin() &&
    duelistModel.hasSkinnedPrimitives() &&
    duelistModel.jointCount() > 0U;
  std::uint32_t primitiveVertexCount = 0;
  std::uint32_t primitiveIndexCount = 0;
  bool foundTintedClothPrimitive = false;
  bool foundUntintedNonClothPrimitive = false;
  for (const lg::GltfSkinnedModel::Primitive& primitive : duelistModel.primitives()) {
    primitivesFiniteAndSafe = primitivesFiniteAndSafe &&
      !primitive.vertices.empty() &&
      !primitive.indices.empty() &&
      finiteVec3(primitive.localBounds.min) &&
      finiteVec3(primitive.localBounds.max) &&
      primitive.localBounds.min.x <= primitive.localBounds.max.x &&
      primitive.localBounds.min.y <= primitive.localBounds.max.y &&
      primitive.localBounds.min.z <= primitive.localBounds.max.z;
    primitiveVertexCount += static_cast<std::uint32_t>(primitive.vertices.size());
    primitiveIndexCount += static_cast<std::uint32_t>(primitive.indices.size());
    bool primitiveHasTintedVertex = false;
    bool primitiveHasUntintedVertex = false;
    for (const lg::GltfSkinnedModel::GpuVertex& vertex : primitive.vertices) {
      primitiveHasTintedVertex = primitiveHasTintedVertex || vertex.tintWeight > 0U;
      primitiveHasUntintedVertex = primitiveHasUntintedVertex || vertex.tintWeight == 0U;
      float weightSum = 0.0F;
      for (std::size_t influence = 0; influence < vertex.weights.size(); ++influence) {
        const float weight = vertex.weights[influence];
        primitivesFiniteAndSafe = primitivesFiniteAndSafe &&
          std::isfinite(weight) &&
          weight >= 0.0F &&
          vertex.joints[influence] < duelistModel.jointCount();
        weightSum += weight;
      }
      primitivesFiniteAndSafe = primitivesFiniteAndSafe &&
        std::isfinite(vertex.position.x) &&
        std::isfinite(vertex.position.y) &&
        std::isfinite(vertex.position.z) &&
        std::isfinite(vertex.normal.x) &&
        std::isfinite(vertex.normal.y) &&
        std::isfinite(vertex.normal.z) &&
        (weightSum == 0.0F || nearlyEqual(weightSum, 1.0F, 0.001F));
    }
    foundTintedClothPrimitive =
      foundTintedClothPrimitive || (primitive.tintable && primitiveHasTintedVertex);
    foundUntintedNonClothPrimitive =
      foundUntintedNonClothPrimitive || (!primitive.tintable && primitiveHasUntintedVertex);
  }
  failures += expect(
    primitivesFiniteAndSafe &&
      primitiveVertexCount > 0U &&
      primitiveIndexCount > 0U &&
      foundTintedClothPrimitive &&
      foundUntintedNonClothPrimitive,
    "GLB duelist asset should load finite primitives with normalized weights and material tint masks"
  );
  constexpr std::array<std::string_view, 7> presentationClips = {{
    "RUN_BACK",
    "STRAFE_LEFT",
    "STRAFE_RIGHT",
    "START_FORWARD",
    "STOP_FORWARD",
    "LAND_LIGHT",
    "LAND_HEAVY",
  }};
  failures += expect(
    std::all_of(
      presentationClips.begin(),
      presentationClips.end(),
      [&duelistModel](std::string_view expected) {
        return std::find(
          duelistModel.animationNames().begin(),
          duelistModel.animationNames().end(),
          expected
        ) != duelistModel.animationNames().end();
      }
    ),
    "runtime GLB should contain every authored presentation clip"
  );
  failures += expect(
    baseScene.gltfPlayerModelStats.staticMeshGpuBytes == primitiveVertexCount * 64U &&
      baseScene.gltfPlayerModelStats.staticIndexGpuBytes == primitiveIndexCount * 4U &&
      baseScene.gltfPlayerModelStats.poseUploadBytes ==
        duelistModel.jointCount() * 64U,
    "GLB render metrics should report resident static bytes and compact per-player pose bytes"
  );
  const std::vector<lg::SkinnedModelTriangle> restPoseTriangles =
    duelistModel.triangles({});
  lg::Vec3 restMin = {
    std::numeric_limits<float>::max(),
    std::numeric_limits<float>::max(),
    std::numeric_limits<float>::max(),
  };
  lg::Vec3 restMax = {
    std::numeric_limits<float>::lowest(),
    std::numeric_limits<float>::lowest(),
    std::numeric_limits<float>::lowest(),
  };
  for (const lg::SkinnedModelTriangle& triangle : restPoseTriangles) {
    for (lg::Vec3 vertex : triangle.vertices) {
      restMin.x = std::min(restMin.x, vertex.x);
      restMin.y = std::min(restMin.y, vertex.y);
      restMin.z = std::min(restMin.z, vertex.z);
      restMax.x = std::max(restMax.x, vertex.x);
      restMax.y = std::max(restMax.y, vertex.y);
      restMax.z = std::max(restMax.z, vertex.z);
    }
  }
  const bool restPoseCompact =
    duelistModel.loaded() &&
    !restPoseTriangles.empty() &&
    restMin.x > -0.50F && restMax.x < 0.50F &&
    restMin.y > -0.02F && restMax.y < 1.72F &&
    restMin.z > -0.22F && restMax.z < 0.30F;
  if (!restPoseCompact) {
    std::cerr << "rest bounds min=(" << restMin.x << ", " << restMin.y << ", "
              << restMin.z << ") max=(" << restMax.x << ", " << restMax.y
              << ", " << restMax.z << ")\n";
  }
  failures += expect(
    restPoseCompact,
    "GLB duelist bind pose should resolve child nodes after their parents"
  );
  {
    lg::GltfSkinnedModel::PoseScratch runScratch;
    lg::GltfSkinnedModel::PoseScratch leanScratch;
    lg::GltfSkinnedModel::PoseScratch aimScratch;
    lg::GltfSkinnedModel::PoseScratch backScratch;
    lg::GltfSkinnedModel::PoseScratch leftScratch;
    lg::GltfSkinnedModel::PoseScratch rightScratch;
    std::vector<std::array<float, 16>> runPalette;
    std::vector<std::array<float, 16>> leanPalette;
    std::vector<std::array<float, 16>> aimPalette;
    std::vector<std::array<float, 16>> backPalette;
    std::vector<std::array<float, 16>> leftPalette;
    std::vector<std::array<float, 16>> rightPalette;
    const bool runSampled = duelistModel.appendBonePalette(
      {{"RUN", 0.25F, 1.0F}},
      runPalette,
      runScratch
    );
    const bool leanSampled = duelistModel.appendBonePalette(
      {{
        {"RUN", 0.25F, 1.0F},
        {"LEAN_LEFT", 0.5833333F, 1.0F, lg::SkinnedModelPoseMask::UpperBody},
      }},
      leanPalette,
      leanScratch
    );
    const bool aimSampled = duelistModel.appendBonePalette(
      {{"RUN", 0.25F, 1.0F}},
      aimPalette,
      aimScratch,
      0.78539816F
    );
    const bool backSampled = duelistModel.appendBonePalette(
      {{"RUN_BACK", 0.25F, 1.0F}}, backPalette, backScratch
    );
    const bool leftSampled = duelistModel.appendBonePalette(
      {{"STRAFE_LEFT", 0.25F, 1.0F}}, leftPalette, leftScratch
    );
    const bool rightSampled = duelistModel.appendBonePalette(
      {{"STRAFE_RIGHT", 0.25F, 1.0F}}, rightPalette, rightScratch
    );
    constexpr std::array<std::size_t, 6> legJoints = {{
      14U, 15U, 16U, 17U, 18U, 19U,
    }};
    constexpr std::array<std::size_t, 8> upperJoints = {{
      2U, 3U, 4U, 5U, 6U, 7U, 9U, 10U,
    }};
    failures += expect(
      runSampled &&
        leanSampled &&
        runPalette.size() == duelistModel.jointCount() &&
        leanPalette.size() == duelistModel.jointCount() &&
        maxPaletteDeltaAtIndices(runPalette, leanPalette, legJoints) <= 0.0001F &&
        maxPaletteDeltaAtIndices(runPalette, leanPalette, upperJoints) > 0.001F,
      "upper-body lean layer should preserve run leg joints while changing torso/arm joints"
    );
    failures += expect(
      aimSampled &&
        aimPalette.size() == duelistModel.jointCount() &&
        maxPaletteDeltaAtIndices(runPalette, aimPalette, legJoints) <= 0.0001F &&
        maxPaletteDeltaAtIndices(runPalette, aimPalette, upperJoints) > 0.001F,
      "aim pitch should change the upper-body palette without tilting locomotion legs"
    );
    failures += expect(
      backSampled && leftSampled && rightSampled &&
        maxPaletteDelta(runPalette, backPalette) > 0.001F &&
        maxPaletteDelta(runPalette, leftPalette) > 0.001F &&
        maxPaletteDelta(runPalette, rightPalette) > 0.001F &&
        maxPaletteDelta(leftPalette, rightPalette) > 0.001F,
      "authored directional clips should produce visibly distinct bone palettes"
    );
  }
  failures += expect(
    nearlyEqual(baseScene.camera.position.x, player.position.x) &&
      nearlyEqual(baseScene.camera.position.y, player.position.y) &&
      nearlyEqual(baseScene.camera.right.z, 0.0F) &&
      baseScene.camera.position.z > player.position.z &&
      nearlyEqual(baseScene.camera.forward.x, 1.0F),
    "scene camera should use the local player's first-person view"
  );

  arena.wallCount = 1;
  arena.walls[0] = {{3.0F, 1.0F, 0.0F}, {5.0F, 3.0F, 2.0F}};
  arena.walls[0].materialId =
    lg::arenaMaterialId("512x512/Brick/Brick_14-512x512");
  const lg::Scene3D texturedWallScene = lg::buildStaticWorldScene(arena);
  bool foundTexturedWallVertex = false;
  bool foundWallUvSpan = false;
  bool foundPrototypeWallAccent = false;
  float minimumWallU = 0.0F;
  float maximumWallU = 0.0F;
  float minimumWallV = 0.0F;
  float maximumWallV = 0.0F;
  float firstWallU = 0.0F;
  float firstWallV = 0.0F;
  bool capturedFirstWallUv = false;
  for (const lg::Vertex3D& vertex : texturedWallScene.vertices) {
    if (
      vertex.materialId == 0U &&
      (
        sameColor(vertex.color, {86, 176, 96, 255}) ||
        sameColor(vertex.color, {171, 235, 145, 255}) ||
        sameColor(vertex.color, {109, 195, 105, 255})
      )
    ) {
      foundPrototypeWallAccent = true;
    }
    if (vertex.materialId != arena.walls[0].materialId) {
      continue;
    }
    foundTexturedWallVertex = true;
    if (!capturedFirstWallUv) {
      firstWallU = vertex.u;
      firstWallV = vertex.v;
      minimumWallU = vertex.u;
      maximumWallU = vertex.u;
      minimumWallV = vertex.v;
      maximumWallV = vertex.v;
      capturedFirstWallUv = true;
      continue;
    }
    minimumWallU = std::min(minimumWallU, vertex.u);
    maximumWallU = std::max(maximumWallU, vertex.u);
    minimumWallV = std::min(minimumWallV, vertex.v);
    maximumWallV = std::max(maximumWallV, vertex.v);
    foundWallUvSpan = foundWallUvSpan ||
      !nearlyEqual(vertex.u, firstWallU) ||
      !nearlyEqual(vertex.v, firstWallV);
  }
  failures += expect(
    foundTexturedWallVertex,
    "wall scene geometry should preserve the wall material id"
  );
  failures += expect(
    foundWallUvSpan,
    "wall scene geometry should emit varying texture coordinates"
  );
  failures += expect(
    maximumWallU - minimumWallU > 0.1F || maximumWallV - minimumWallV > 0.1F,
    "wall texture coordinates should use TrenchBroom scale instead of stretched LG units"
  );
  failures += expect(
    !foundPrototypeWallAccent,
    "wall scene geometry should not emit old green prototype accents over textures"
  );
  arena.wallCount = 0;

  {
    lg::Arena floorArena;
    floorArena.min = {-2.0F, -2.0F, -2.0F};
    floorArena.max = {2.0F, 2.0F, 2.0F};
    floorArena.wallCount = 1;
    floorArena.walls[0] = {{-1.0F, -1.0F, -0.2F}, {1.0F, 1.0F, 0.0F}};
    floorArena.walls[0].materialId = lg::arenaMaterialId("test/imported-floor");
    const lg::Scene3D withDefaultFloor = lg::buildStaticWorldScene(floorArena);
    floorArena.renderDefaultFloor = false;
    const lg::Scene3D importedFloor = lg::buildStaticWorldScene(floorArena);
    const bool keptImportedFloor = std::any_of(
      importedFloor.vertices.begin(), importedFloor.vertices.end(),
      [&](const lg::Vertex3D& vertex) {
        return vertex.materialId == floorArena.walls[0].materialId;
      }
    );
    failures += expect(
      importedFloor.vertices.size() < withDefaultFloor.vertices.size() &&
        keptImportedFloor,
      "imported maps should omit the default z=0 floor but keep their source floor"
    );
  }

  {
    lg::Arena clipArena;
    const lg::Scene3D baselineStaticScene = lg::buildStaticWorldScene(clipArena);
    clipArena.wallCount = 1;
    clipArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    clipArena.walls[0].max = {1.0F, 1.0F, 1.0F};
    clipArena.walls[0].materialId = lg::arenaMaterialId("common/playerclip");
    clipArena.walls[0].renderable = false;
    const lg::Scene3D clippedStaticScene = lg::buildStaticWorldScene(clipArena);
    bool foundPlayerClipMaterial = false;
    for (const lg::Vertex3D& vertex : clippedStaticScene.vertices) {
      foundPlayerClipMaterial =
        foundPlayerClipMaterial || vertex.materialId == clipArena.walls[0].materialId;
    }
    failures += expect(
      clippedStaticScene.vertices.size() == baselineStaticScene.vertices.size() &&
        !foundPlayerClipMaterial,
      "non-renderable playerclip walls should not emit static world geometry"
    );
  }

  {
    lg::Arena visualArena;
    const lg::Scene3D baselineStaticScene = lg::buildStaticWorldScene(visualArena);
    visualArena.visualWallCount = 1;
    visualArena.visualWalls[0].min = {0.0F, 0.0F, 0.0F};
    visualArena.visualWalls[0].max = {1.0F, 1.0F, 1.0F};
    visualArena.visualWalls[0].materialId = lg::arenaMaterialId("patch/arch");
    visualArena.visualWalls[0].sourcePatchIndex = 46U;
    const lg::Scene3D visualStaticScene = lg::buildStaticWorldScene(visualArena);
    bool foundVisualMaterial = false;
    for (const lg::Vertex3D& vertex : visualStaticScene.vertices) {
      foundVisualMaterial = foundVisualMaterial ||
        vertex.materialId == visualArena.visualWalls[0].materialId;
    }
    failures += expect(
      visualStaticScene.vertices.size() > baselineStaticScene.vertices.size() &&
        foundVisualMaterial,
      "visual-only walls should emit textured static world geometry"
    );

    visualArena.visualBrushCount = 1;
    lg::ArenaBrush& visualBrush = visualArena.visualBrushes[0];
    visualBrush.vertexCount = 3;
    visualBrush.vertices[0] = {2.0F, 0.0F, 0.0F};
    visualBrush.vertices[1] = {3.0F, 0.0F, 0.0F};
    visualBrush.vertices[2] = {2.0F, 1.0F, 0.0F};
    visualBrush.faceCount = 1;
    visualBrush.faces[0].vertexCount = 3;
    visualBrush.faces[0].vertices = {0U, 1U, 2U};
    visualBrush.faces[0].normal = {0.0F, 0.0F, 1.0F};
    visualBrush.faces[0].materialId = lg::arenaMaterialId("patch/curve");
    const lg::Scene3D visualBrushScene = lg::buildStaticWorldScene(visualArena);
    const bool foundVisualBrush = std::any_of(
      visualBrushScene.vertices.begin(),
      visualBrushScene.vertices.end(),
      [&visualBrush](const lg::Vertex3D& vertex) {
        return vertex.materialId == visualBrush.faces[0].materialId;
      }
    );
    failures += expect(
      foundVisualBrush,
      "visual-only convex brushes should emit textured static world geometry"
    );

    lg::Scene3D collisionDebug;
    lg::appendCollisionDebugGeometry(collisionDebug, visualArena, 1);
    failures += expect(
      collisionDebug.translucentVertices.empty(),
      "visual-only geometry should stay absent from r_show_collision"
    );
  }

  {
    lg::Arena debugArena;
    debugArena.wallCount = 3;
    for (std::size_t index = 0; index < debugArena.wallCount; ++index) {
      debugArena.walls[index].min = {static_cast<float>(index) * 2.0F, 0.0F, 0.0F};
      debugArena.walls[index].max = {static_cast<float>(index) * 2.0F + 1.0F, 1.0F, 1.0F};
    }
    debugArena.walls[0].collisionKind = lg::ArenaCollisionKind::VisibleSolid;
    debugArena.walls[1].collisionKind = lg::ArenaCollisionKind::PlayerClip;
    debugArena.walls[2].collisionKind = lg::ArenaCollisionKind::WeaponClip;
    debugArena.jumpPadCount = 1;
    debugArena.jumpPads[0].min = {0.0F, 2.0F, 0.0F};
    debugArena.jumpPads[0].max = {1.0F, 3.0F, 1.0F};
    debugArena.teleportCount = 1;
    debugArena.teleports[0].min = {2.0F, 2.0F, 0.0F};
    debugArena.teleports[0].max = {3.0F, 3.0F, 1.0F};
    debugArena.mcguffin.hasRedBase = true;
    debugArena.mcguffin.redBase.min = {4.0F, 2.0F, 0.0F};
    debugArena.mcguffin.redBase.max = {5.0F, 3.0F, 1.0F};
    debugArena.mcguffin.hasBlueBase = true;
    debugArena.mcguffin.blueBase.min = {6.0F, 2.0F, 0.0F};
    debugArena.mcguffin.blueBase.max = {7.0F, 3.0F, 1.0F};

    lg::Scene3D allCollision;
    lg::appendCollisionDebugGeometry(allCollision, debugArena, 1);
    bool foundBlue = false;
    bool foundGreen = false;
    bool foundOrange = false;
    bool foundPurple = false;
    for (const lg::Vertex3D& vertex : allCollision.translucentVertices) {
      foundBlue = foundBlue ||
        (vertex.color.red == 64 && vertex.color.green == 160 && vertex.color.blue == 255);
      foundGreen = foundGreen ||
        (vertex.color.red == 72 && vertex.color.green == 255 && vertex.color.blue == 128);
      foundOrange = foundOrange ||
        (vertex.color.red == 255 && vertex.color.green == 156 && vertex.color.blue == 48);
      foundPurple = foundPurple ||
        (vertex.color.red == 208 && vertex.color.green == 96 && vertex.color.blue == 255);
    }
    failures += expect(
      foundBlue && foundGreen && foundOrange && foundPurple,
      "all-collision mode should color visible solids, playerclip, weapclip, and triggers distinctly"
    );

    lg::Scene3D playerClipOnly;
    lg::appendCollisionDebugGeometry(playerClipOnly, debugArena, 3);
    failures += expect(
      playerClipOnly.translucentVertices.size() == 36U &&
        std::all_of(
          playerClipOnly.translucentVertices.begin(),
          playerClipOnly.translucentVertices.end(),
          [](const lg::Vertex3D& vertex) {
            return vertex.color.red == 72 && vertex.color.green == 255 &&
              vertex.color.blue == 128;
          }
        ),
      "playerclip-only mode should exclude visible solids, weapclip, and triggers"
    );

    lg::Scene3D triggersOnly;
    lg::appendCollisionDebugGeometry(triggersOnly, debugArena, 5);
    failures += expect(
      triggersOnly.translucentVertices.size() == 4U * 36U &&
        std::all_of(
          triggersOnly.translucentVertices.begin(),
          triggersOnly.translucentVertices.end(),
          [](const lg::Vertex3D& vertex) {
            return vertex.color.red == 208 && vertex.color.green == 96 &&
              vertex.color.blue == 255;
          }
        ),
      "trigger mode should include jump pads, teleports, and both enabled McGuffin bases"
    );

    lg::Scene3D disabledCollision;
    lg::appendCollisionDebugGeometry(disabledCollision, debugArena, 0);
    failures += expect(
      disabledCollision.translucentVertices.empty(),
      "disabled collision visualization should emit no debug geometry"
    );
  }

  {
    lg::TextureProjection projection;
    projection.uAxis = {1.0F, 0.0F, 0.0F};
    projection.vAxis = {0.0F, -1.0F, 0.0F};
    projection.valid = true;
    const UvBounds bounds = texturedWallUvBounds(projection);
    failures += expect(bounds.found, "projected wall UV test should find textured face");
    failures += expect(
      nearlyEqual(bounds.maxU - bounds.minU, 128.0F) &&
        nearlyEqual(bounds.maxV - bounds.minV, 128.0F),
      "128-quake-unit face should produce one 128px texture repeat at scale 1"
    );
  }

  {
    lg::TextureProjection projection;
    projection.uAxis = {1.0F, 0.0F, 0.0F};
    projection.vAxis = {0.0F, -1.0F, 0.0F};
    projection.valid = true;
    const UvBounds bounds = texturedWallUvBounds(projection, 256.0F);
    failures += expect(
      nearlyEqual(bounds.maxU - bounds.minU, 256.0F),
      "256-quake-unit face should produce two 128px texture repeats at scale 1"
    );
  }

  {
    lg::TextureProjection projection;
    projection.uAxis = {1.0F, 0.0F, 0.0F};
    projection.vAxis = {0.0F, -1.0F, 0.0F};
    projection.uOffset = 16.0F;
    projection.vOffset = 32.0F;
    projection.valid = true;
    const UvBounds bounds = texturedWallUvBounds(projection);
    failures += expect(
      nearlyEqual(bounds.minU, 16.0F) && nearlyEqual(bounds.maxV, 32.0F),
      "texture offsets should shift generated UVs"
    );
  }

  {
    lg::TextureProjection projection;
    projection.uAxis = {0.0F, -1.0F, 0.0F};
    projection.vAxis = {1.0F, 0.0F, 0.0F};
    projection.valid = true;
    const UvBounds bounds = texturedWallUvBounds(projection);
    failures += expect(
      nearlyEqual(bounds.maxU - bounds.minU, 128.0F) &&
        nearlyEqual(bounds.maxV - bounds.minV, 128.0F) &&
        bounds.minU < -127.0F,
      "90-degree rotation should rotate generated UV axes"
    );
  }

  {
    lg::TextureProjection halfScale;
    halfScale.uAxis = {2.0F, 0.0F, 0.0F};
    halfScale.vAxis = {0.0F, -2.0F, 0.0F};
    halfScale.valid = true;
    const UvBounds halfBounds = texturedWallUvBounds(halfScale);
    lg::TextureProjection doubleScale;
    doubleScale.uAxis = {0.5F, 0.0F, 0.0F};
    doubleScale.vAxis = {0.0F, -0.5F, 0.0F};
    doubleScale.valid = true;
    const UvBounds doubleBounds = texturedWallUvBounds(doubleScale);
    failures += expect(
      nearlyEqual(halfBounds.maxU - halfBounds.minU, 256.0F) &&
        nearlyEqual(doubleBounds.maxU - doubleBounds.minU, 64.0F),
      "texture scale 0.5 and 2 should affect UV spans"
    );
  }

  {
    lg::Arena cubeArena;
    cubeArena.wallCount = 1;
    cubeArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    cubeArena.walls[0].max = {1.0F, 1.0F, 1.0F};
    for (std::size_t faceIndex = 0; faceIndex < cubeArena.walls[0].faceMaterialIds.size(); ++faceIndex) {
      cubeArena.walls[0].faceMaterialIds[faceIndex] =
        lg::arenaMaterialId("face_" + std::to_string(faceIndex));
      cubeArena.walls[0].faceTextureProjections[faceIndex].uAxis = {1.0F, 0.0F, 0.0F};
      cubeArena.walls[0].faceTextureProjections[faceIndex].vAxis = {0.0F, 1.0F, 0.0F};
      cubeArena.walls[0].faceTextureProjections[faceIndex].uOffset =
        static_cast<float>(faceIndex) * 10.0F;
      cubeArena.walls[0].faceTextureProjections[faceIndex].valid = true;
    }
    const lg::Scene3D cubeScene = lg::buildStaticWorldScene(cubeArena);
    std::array<bool, 6> foundFaceMaterial = {};
    std::array<bool, 6> foundFaceOffset = {};
    for (const lg::Vertex3D& vertex : cubeScene.vertices) {
      for (std::size_t faceIndex = 0; faceIndex < foundFaceMaterial.size(); ++faceIndex) {
        if (vertex.materialId == cubeArena.walls[0].faceMaterialIds[faceIndex]) {
          foundFaceMaterial[faceIndex] = true;
          foundFaceOffset[faceIndex] = foundFaceOffset[faceIndex] ||
            vertex.u >= static_cast<float>(faceIndex) * 10.0F;
        }
      }
    }
    failures += expect(
      std::all_of(foundFaceMaterial.begin(), foundFaceMaterial.end(), [](bool value) { return value; }),
      "six-material cube should render each face material"
    );
    failures += expect(
      std::all_of(foundFaceOffset.begin(), foundFaceOffset.end(), [](bool value) { return value; }),
      "six-material cube should keep each face projection offset"
    );
  }

  {
    lg::Arena litArena;
    litArena.wallCount = 1;
    litArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    litArena.walls[0].max = {4.0F, 4.0F, 1.0F};
    litArena.walls[0].materialId = lg::arenaMaterialId("lit_static_wall");
    litArena.staticLightCount = 1;
    litArena.staticLights[0].position = {0.2F, 0.2F, 3.0F};
    litArena.staticLights[0].color = {1.0F, 0.65F, 0.35F};
    litArena.staticLights[0].intensity = 2.5F;
    litArena.staticLights[0].radius = 7.0F;
    const lg::Scene3D litScene = lg::buildStaticWorldScene(litArena);
    int minTopRed = 255;
    int maxTopRed = 0;
    int maxTopGreen = 0;
    int maxTopBlue = 0;
    for (const lg::Vertex3D& vertex : litScene.vertices) {
      if (
        vertex.materialId == litArena.walls[0].materialId &&
        nearlyEqual(vertex.position.z, litArena.walls[0].max.z)
      ) {
        minTopRed = std::min(minTopRed, static_cast<int>(vertex.color.red));
        maxTopRed = std::max(maxTopRed, static_cast<int>(vertex.color.red));
        maxTopGreen = std::max(maxTopGreen, static_cast<int>(vertex.color.green));
        maxTopBlue = std::max(maxTopBlue, static_cast<int>(vertex.color.blue));
      }
    }
    failures += expect(
      maxTopRed > minTopRed + 20,
      "static lights should create per-vertex brightness variation"
    );
    failures += expect(
      maxTopRed > maxTopGreen && maxTopGreen > maxTopBlue,
      "static lights should tint world vertices with light color"
    );
  }

  {
    lg::Arena sunArena;
    sunArena.wallCount = 1;
    sunArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    sunArena.walls[0].max = {2.0F, 2.0F, 1.0F};
    const std::uint32_t topMaterial = lg::arenaMaterialId("sunlit_top");
    const std::uint32_t bottomMaterial = lg::arenaMaterialId("sunlit_bottom");
    sunArena.walls[0].faceMaterialIds[0] = bottomMaterial;
    sunArena.walls[0].faceMaterialIds[1] = topMaterial;
    sunArena.sunLight.enabled = true;
    sunArena.sunLight.direction = {0.0F, 0.0F, -1.0F};
    sunArena.sunLight.color = {1.0F, 1.0F, 1.0F};
    sunArena.sunLight.intensity = 0.7F;
    const lg::Scene3D sunScene = lg::buildStaticWorldScene(sunArena);
    int topRed = 0;
    int bottomRed = 0;
    for (const lg::Vertex3D& vertex : sunScene.vertices) {
      if (vertex.materialId == topMaterial) {
        topRed = std::max(topRed, static_cast<int>(vertex.color.red));
      } else if (vertex.materialId == bottomMaterial) {
        bottomRed = std::max(bottomRed, static_cast<int>(vertex.color.red));
      }
    }
    failures += expect(
      topRed > bottomRed + 80,
      "downward sun should make upward-facing floor brighter"
    );
  }

  {
    lg::Arena sunArena;
    sunArena.wallCount = 1;
    sunArena.walls[0].min = {0.0F, 0.0F, 0.0F};
    sunArena.walls[0].max = {2.0F, 2.0F, 1.0F};
    const std::uint32_t facingMaterial = lg::arenaMaterialId("sun_facing_wall");
    const std::uint32_t awayMaterial = lg::arenaMaterialId("sun_away_wall");
    sunArena.walls[0].faceMaterialIds[5] = facingMaterial;
    sunArena.walls[0].faceMaterialIds[3] = awayMaterial;
    sunArena.sunLight.enabled = true;
    sunArena.sunLight.direction = {1.0F, 0.0F, 0.0F};
    sunArena.sunLight.color = {1.0F, 1.0F, 1.0F};
    sunArena.sunLight.intensity = 0.8F;
    const lg::Scene3D sunScene = lg::buildStaticWorldScene(sunArena);
    int facingRed = 0;
    int awayRed = 0;
    for (const lg::Vertex3D& vertex : sunScene.vertices) {
      if (vertex.materialId == facingMaterial) {
        facingRed = std::max(facingRed, static_cast<int>(vertex.color.red));
      } else if (vertex.materialId == awayMaterial) {
        awayRed = std::max(awayRed, static_cast<int>(vertex.color.red));
      }
    }
    failures += expect(
      facingRed > awayRed + 80,
      "wall facing sun should receive more sun contribution than wall facing away"
    );
  }

  player.velocity = lg::yawRight(player.viewYawRadians) * 8.0F;
  const lg::Scene3D movingLocalScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    nearlyEqual(movingLocalScene.camera.right.z, 0.0F),
    "local velocity should not roll the first-person camera"
  );
  player.velocity = {};

  {
    lg::Arena largeArena;
    largeArena.min = {-145.0F, -97.0F, -33.0F};
    largeArena.max = {225.0F, 225.0F, 113.0F};
    largeArena.wallCount = 0;
    lg::PlayerState largeMapPlayer = player;
    largeMapPlayer.position = {0.0F, 0.0F, 1.0F};
    const lg::Scene3D largeScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      largeArena,
      largeMapPlayer,
      opponent,
      inactiveBeam,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      settings
    );
    failures += expect(
      largeScene.vertices.size() < 131072,
      "large TrenchBroom-scale arenas should keep perspective vertex count under the GPU buffer limit"
    );
  }

  opponent.velocity = lg::yawRight(opponent.viewYawRadians) * 8.0F;
  lg::RenderSettings leanSettings = settings;
  leanSettings.enemyLeanScale = 3.0F;
  const lg::Scene3D leanScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    leanSettings
  );
  leanSettings.enemyLeanEnabled = false;
  const lg::Scene3D leanDisabledScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    leanSettings
  );
  failures += expect(
    nearlyEqual(leanDisabledScene.camera.right.z, 0.0F) &&
      leanDisabledScene.gltfPlayerModelInstances.size() == 1U,
    "disabled enemy lean should keep the camera upright and avoid velocity roll"
  );
  failures += expect(
    leanScene.gltfPlayerModelInstances.size() == 1U &&
      leanScene.gltfBonePalette.size() == leanDisabledScene.gltfBonePalette.size() &&
      maxPaletteDelta(leanScene.gltfBonePalette, leanDisabledScene.gltfBonePalette) > 0.0001F,
    "enabled enemy lean should use the skinned GLB lean animation"
  );
  opponent.velocity = {};

  {
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> strafeRemotes = {};
    lg::PlayerState strafeRunner = opponent;
    strafeRunner.velocity = lg::yawRight(strafeRunner.viewYawRadians) * 8.0F;
    strafeRemotes[0] = {
      strafeRunner,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
      0.0F,
    };
    lg::RenderSettings strafeRunSettings = settings;
    strafeRunSettings.enemyLeanEnabled = false;
    const lg::Scene3D strafeRunStartScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      strafeRemotes,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      strafeRunSettings
    );
    strafeRemotes[0].animationTimeSeconds = 0.2F;
    const lg::Scene3D strafeRunLaterScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      strafeRemotes,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      strafeRunSettings
    );
    strafeRemotes[0].player.velocity =
      lg::yawRight(strafeRunner.viewYawRadians) * 0.5F;
    strafeRemotes[0].animationTimeSeconds = 0.0F;
    const lg::Scene3D slowStrafeStartScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      strafeRemotes,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      strafeRunSettings
    );
    strafeRemotes[0].animationTimeSeconds = 0.2F;
    const lg::Scene3D slowStrafeLaterScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      strafeRemotes,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      strafeRunSettings
    );
    constexpr std::array<std::size_t, 6> legJoints = {{
      14U, 15U, 16U, 17U, 18U, 19U,
    }};
    const float fullStrafeLegDelta = maxPaletteDeltaAtIndices(
      strafeRunStartScene.gltfBonePalette,
      strafeRunLaterScene.gltfBonePalette,
      legJoints
    );
    const float slowStrafeLegDelta = maxPaletteDeltaAtIndices(
      slowStrafeStartScene.gltfBonePalette,
      slowStrafeLaterScene.gltfBonePalette,
      legJoints
    );
    const float slowAndFullSamePhaseDelta = maxPaletteDeltaAtIndices(
      slowStrafeStartScene.gltfBonePalette,
      strafeRunStartScene.gltfBonePalette,
      legJoints
    );
    failures += expect(
      strafeRunStartScene.gltfBonePalette.size() == duelistModel.jointCount() &&
        strafeRunLaterScene.gltfBonePalette.size() == duelistModel.jointCount() &&
        fullStrafeLegDelta > 0.001F,
      "pure strafe velocity should advance GLB run leg animation over render time"
    );
    failures += expect(
      slowStrafeStartScene.gltfBonePalette.size() == duelistModel.jointCount() &&
        slowStrafeLaterScene.gltfBonePalette.size() == duelistModel.jointCount() &&
        slowAndFullSamePhaseDelta <= 0.0001F &&
        slowStrafeLegDelta < fullStrafeLegDelta * 0.15F,
      "slow strafe velocity should keep full stride shape but advance it more slowly"
    );
  }

  failures += expect(
    baseScene.gltfPlayerModelInstances.size() == 1U &&
      baseScene.gltfPlayerModelInstances.front().skinned &&
      baseScene.gltfBonePalette.size() == duelistModel.jointCount() &&
      baseScene.gltfPlayerModelStats.bodyBatches ==
        static_cast<std::uint32_t>(duelistModel.primitives().size()) &&
      baseScene.gltfPlayerModelStats.bodyDrawCalls ==
        baseScene.gltfPlayerModelStats.bodyBatches,
    "opponent should use the GPU-skinned GLB duelist mesh path"
  );
  lg::RenderSettings legacyModelSettings = settings;
  legacyModelSettings.playerModel = 0;
  const lg::Scene3D legacyModelScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyModelSettings
  );
  const lg::StaticMeshAsset* playerBoxCube =
    lg::staticMeshAsset(lg::MeshHandle::PlayerBoxCube);
  const lg::StaticMeshAsset* playerBoxCubeAgain =
    lg::staticMeshAsset(lg::MeshHandle::PlayerBoxCube);
  failures += expect(
    playerBoxCube != nullptr &&
      playerBoxCube == playerBoxCubeAgain &&
      playerBoxCube->vertices.size() == 36U &&
      playerBoxCube->localBounds.radius > 0.86F &&
      playerBoxCube->localBounds.radius < 0.87F,
    "procedural player boxes should reuse one centered static cube asset"
  );
  failures += expect(
    playerBoxInstanceCount(legacyModelScene) == 7U &&
      legacyModelScene.playerBoxStats.visiblePlayers == 1 &&
      legacyModelScene.playerBoxStats.instancesSubmitted == 7 &&
      legacyModelScene.playerBoxStats.instanceUploadBytes == 7U * 52U &&
      legacyModelScene.playerBoxStats.sharedCubeStaticGpuBytes == 36U * 24U &&
      legacyModelScene.normalPlayerBodyDynamicVertices == 0 &&
      legacyModelScene.playerBoxStats.legacyCpuGeneratedVertices == 0 &&
      legacyModelScene.playerBoxStats.legacyDynamicVertexUploadBytes == 0,
    "r_player_model 0 should submit seven compact cube instances and no legacy body vertices"
  );
  constexpr std::array<lg::PlayerBodyPartType, 7> expectedPlayerBoxOrder = {{
    lg::PlayerBodyPartType::Torso,
    lg::PlayerBodyPartType::Hips,
    lg::PlayerBodyPartType::Head,
    lg::PlayerBodyPartType::LeftArm,
    lg::PlayerBodyPartType::RightArm,
    lg::PlayerBodyPartType::LeftLeg,
    lg::PlayerBodyPartType::RightLeg,
  }};
  std::size_t playerBoxOrderIndex = 0U;
  bool stablePlayerBoxOrder = true;
  for (const lg::StaticMeshInstance& instance : legacyModelScene.staticMeshInstances) {
    if (!instance.playerBoxBody) {
      continue;
    }
    stablePlayerBoxOrder = stablePlayerBoxOrder &&
      playerBoxOrderIndex < expectedPlayerBoxOrder.size() &&
      instance.playerBodyPart == expectedPlayerBoxOrder[playerBoxOrderIndex];
    ++playerBoxOrderIndex;
  }
  failures += expect(
    stablePlayerBoxOrder && playerBoxOrderIndex == expectedPlayerBoxOrder.size(),
    "static mesh finalization should preserve submission order within one batch key"
  );
  bool foundTorso = false;
  bool foundHead = false;
  bool boxTransformsFinite = true;
  bool boxTintMatchesEnemy = false;
  for (const lg::StaticMeshInstance& instance : legacyModelScene.staticMeshInstances) {
    if (!instance.playerBoxBody) {
      continue;
    }
    foundTorso = foundTorso || instance.playerBodyPart == lg::PlayerBodyPartType::Torso;
    foundHead = foundHead || instance.playerBodyPart == lg::PlayerBodyPartType::Head;
    boxTintMatchesEnemy = boxTintMatchesEnemy || isEnemyModelColor(instance.color);
    boxTransformsFinite = boxTransformsFinite &&
      finiteVec3(instance.modelRow0) &&
      finiteVec3(instance.modelRow1) &&
      finiteVec3(instance.modelRow2) &&
      finiteVec3(instance.modelTranslation) &&
      finiteVec3(transformPoint(instance, {-0.5F, -0.5F, -0.5F})) &&
      finiteVec3(transformPoint(instance, {0.5F, 0.5F, 0.5F}));
  }
  failures += expect(
    foundTorso && foundHead && boxTransformsFinite && boxTintMatchesEnemy,
    "procedural box instances should preserve body-part metadata, finite transforms, and enemy tint"
  );
  failures += expect(
    legacyModelScene.outlineMaskDraws.size() == 1U &&
      legacyModelScene.outlineMaskDraws.front().mesh == lg::MeshHandle::PlayerBoxCube &&
      legacyModelScene.outlineMaskDraws.front().instanceCount == 7U &&
      legacyModelScene.outlineMaskDraws.front().vertexCount == 0U &&
      legacyModelScene.playerBoxStats.outlineMaskBatches == 1 &&
      legacyModelScene.playerBoxStats.outlineMaskDrawCalls == 1,
    "screen-space outline mask should consume the same procedural box cube instances"
  );

  lg::PlayerState airborneOpponent = opponent;
  airborneOpponent.movementMode = lg::MovementMode::Airborne;
  airborneOpponent.onGround = false;
  airborneOpponent.velocity.z = 4.0F;
  const lg::Scene3D airborneScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    airborneOpponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    airborneScene.gltfPlayerModelInstances.size() == baseScene.gltfPlayerModelInstances.size() &&
      !airborneScene.gltfBonePalette.empty() &&
      airborneScene.gltfBonePalette.size() == baseScene.gltfBonePalette.size() &&
      maxPaletteDelta(airborneScene.gltfBonePalette, baseScene.gltfBonePalette) > 0.0001F,
    "airborne opponent should use the skinned GLB jump animation"
  );

  const lg::OutlineMaskDraw& enemyMaskDraw = baseScene.outlineMaskDraws.front();
  failures += expect(
    enemyMaskDraw.state.group == lg::OutlineGroup::Enemy &&
      enemyMaskDraw.state.visibility == lg::OutlineVisibility::VisibleOnly &&
      nearlyEqual(enemyMaskDraw.state.widthPixels, settings.enemyOutlineWidth) &&
      nearlyEqual(enemyMaskDraw.state.alpha, settings.enemyOutlineAlpha) &&
      enemyMaskDraw.gltfPlayerModel &&
      enemyMaskDraw.gltfFirstInstance == 0U &&
      enemyMaskDraw.gltfInstanceCount == baseScene.gltfPlayerModelInstances.size() &&
      enemyMaskDraw.vertexCount == 0U &&
      enemyMaskDraw.instanceCount == 0U,
    "enabled enemy outline should reuse the GPU player model instance range as mask input"
  );

  lg::RenderSettings outlineDisabledSettings = settings;
  outlineDisabledSettings.enemyOutlineEnabled = false;
  const lg::Scene3D outlineDisabledScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    outlineDisabledSettings
  );
  failures += expect(
    outlineDisabledScene.playerOutlinesBuilt == 0 &&
      outlineDisabledScene.outlineMaskDraws.empty() &&
      outlineDisabledScene.geometryOutlineDynamicVertices == 0 &&
      outlineDisabledScene.vertices.size() == baseScene.vertices.size(),
    "disabled enemy outline should exclude the player from the outline mask without changing normal geometry"
  );

  lg::RenderSettings legacyOutlineSettings = settings;
  legacyOutlineSettings.playerOutlineStyle = lg::PlayerOutlineStyle::Geometry;
  const lg::Scene3D legacyOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyOutlineSettings
  );
  failures += expect(
    legacyOutlineScene.playerOutlinesBuilt == 1 &&
      legacyOutlineScene.outlineMaskDraws.empty() &&
      legacyOutlineScene.geometryOutlineFallbackUsed &&
      legacyOutlineScene.geometryOutlineDynamicVertices > 0 &&
      legacyOutlineScene.vertices.size() > baseScene.vertices.size(),
    "legacy geometry outline style should remain explicit fallback behavior"
  );

  lg::RenderSettings modeDisabledOutlineSettings = settings;
  modeDisabledOutlineSettings.playerOutlineMode = lg::PlayerOutlineMode::Disabled;
  const lg::Scene3D modeDisabledOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, opponent, inactiveBeam, inactiveBeam,
    weaponFires, rocketExplosions, rockets, modeDisabledOutlineSettings
  );
  failures += expect(
    modeDisabledOutlineScene.playerOutlinesBuilt == 0U &&
      modeDisabledOutlineScene.outlineMaskDraws.empty() &&
      modeDisabledOutlineScene.geometryOutlineDynamicVertices == 0U,
    "outline mode zero should gate geometry and screen-space outline work"
  );

  lg::RenderSettings nativeOutlineSettings = settings;
  nativeOutlineSettings.playerOutlineMode =
    lg::PlayerOutlineMode::NativeScreenSpace;
  nativeOutlineSettings.playerOutlineStyle = lg::PlayerOutlineStyle::Geometry;
  nativeOutlineSettings.playerOutlineWidth = 1.5F;
  const lg::Scene3D nativeOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F, arena, player, opponent, inactiveBeam, inactiveBeam,
    weaponFires, rocketExplosions, rockets, nativeOutlineSettings
  );
  failures += expect(
    !nativeOutlineScene.outlineMaskDraws.empty() &&
      nativeOutlineScene.outlineMaskDraws.size() >
        baseScene.outlineMaskDraws.size() &&
      !nativeOutlineScene.geometryOutlineFallbackUsed &&
      std::any_of(
        nativeOutlineScene.outlineMaskDraws.begin(),
        nativeOutlineScene.outlineMaskDraws.end(),
        [](const lg::OutlineMaskDraw& draw) {
          return draw.mesh != lg::MeshHandle::PlayerBoxCube &&
            draw.instanceCount > 0U;
        }
      ) &&
      nearlyEqual(
        nativeOutlineScene.outlineMaskDraws.front().state.widthPixels,
        1.5F
      ),
    "native outlines should reuse body geometry and add the held weapon silhouette"
  );

  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> teammateOnlyPlayers = {};
  teammateOnlyPlayers[1] =
    lg::RemotePlayerView{
      opponent,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      true,
      {},
    };
  const lg::Scene3D teammateOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    teammateOnlyPlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    teammateOutlineScene.outlineMaskDraws.size() == 1U &&
      teammateOutlineScene.outlineMaskDraws.front().state.group ==
        lg::OutlineGroup::Teammate &&
      nearlyEqual(
        teammateOutlineScene.outlineMaskDraws.front().state.widthPixels,
        settings.teammateOutlineWidth
      ),
    "teammate and enemy outline mask groups should remain distinct"
  );
  const lg::Scene3D teammateBoxOutlineScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    teammateOnlyPlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyModelSettings
  );
  failures += expect(
    teammateBoxOutlineScene.outlineMaskDraws.size() == 1U &&
      teammateBoxOutlineScene.outlineMaskDraws.front().state.group ==
        lg::OutlineGroup::Teammate &&
      teammateBoxOutlineScene.outlineMaskDraws.front().mesh ==
        lg::MeshHandle::PlayerBoxCube &&
      teammateBoxOutlineScene.outlineMaskDraws.front().instanceCount == 7U,
    "teammate procedural box outline mask should keep teammate grouping on cube instances"
  );

  const lg::OutlineTargetDimensions oddOutlineDimensions =
    lg::outlineTargetDimensions(1921U, 1081U);
  failures += expect(
    oddOutlineDimensions.workWidth == 961U &&
      oddOutlineDimensions.workHeight == 541U &&
      nearlyEqual(lg::outlineWorkRadiusPixels(3.0F), 1.5F) &&
      nearlyEqual(lg::outlineWorkRadiusPixels(8.0F), 3.0F) &&
      lg::kOutlineFixedDilationKernelTaps == 49U,
    "screen-space outline widths should map to half-resolution radius with a fixed 7x7 kernel"
  );
  const lg::OutlineTargetDimensions nativeOddOutlineDimensions =
    lg::outlineTargetDimensions(1921U, 1081U, 1.0F);
  failures += expect(
    nativeOddOutlineDimensions.workWidth == 1921U &&
      nativeOddOutlineDimensions.workHeight == 1081U &&
      nearlyEqual(nativeOddOutlineDimensions.workScale, 1.0F) &&
      nearlyEqual(lg::outlineWorkRadiusPixels(1.5F, 1.0F), 1.5F) &&
      nearlyEqual(lg::outlineWorkRadiusPixels(3.0F, 1.0F), 3.0F),
    "native outline targets and radii should use exact output pixels"
  );

  const lg::OutlineWorkPlan noOutlinePlan = lg::buildOutlineWorkPlan(
    baseScene.camera,
    std::span<const lg::Vertex3D>(baseScene.vertices.data(), baseScene.vertices.size()),
    std::span<const lg::OutlineMaskDraw>(),
    1920U,
    1080U
  );
  failures += expect(
    !noOutlinePlan.hasWork &&
      noOutlinePlan.maskDrawCalls == 0U &&
      noOutlinePlan.dilationDrawCalls == 0U &&
      noOutlinePlan.compositeDrawCalls == 0U,
    "no outlined players should produce no work rectangle and no outline passes"
  );

  const lg::OutlineWorkPlan centeredOutlinePlan = lg::buildOutlineWorkPlan(
    baseScene.camera,
    std::span<const lg::Vertex3D>(baseScene.vertices.data(), baseScene.vertices.size()),
    std::span<const lg::StaticMeshInstance>(
      baseScene.staticMeshInstances.data(),
      baseScene.staticMeshInstances.size()
    ),
    std::span<const lg::GltfPlayerModelInstance>(
      baseScene.gltfPlayerModelInstances.data(),
      baseScene.gltfPlayerModelInstances.size()
    ),
    std::span<const lg::OutlineMaskDraw>(
      baseScene.outlineMaskDraws.data(),
      baseScene.outlineMaskDraws.size()
    ),
    1920U,
    1080U
  );
  failures += expect(
    centeredOutlinePlan.hasWork &&
      centeredOutlinePlan.dimensions.workWidth == 960U &&
      centeredOutlinePlan.dimensions.workHeight == 540U &&
      centeredOutlinePlan.workRect.valid() &&
      centeredOutlinePlan.finalRect.width < 1920 &&
      centeredOutlinePlan.finalRect.height < 1080 &&
      centeredOutlinePlan.maskDrawCalls == 1U &&
      centeredOutlinePlan.dilationDrawCalls == 1U &&
      centeredOutlinePlan.compositeDrawCalls == 1U,
    "centered outlined player should use a bounded half-resolution work rectangle"
  );
  const lg::OutlineWorkPlan nativeCenteredOutlinePlan = lg::buildOutlineWorkPlan(
    nativeOutlineScene.camera,
    std::span<const lg::Vertex3D>(
      nativeOutlineScene.vertices.data(), nativeOutlineScene.vertices.size()
    ),
    std::span<const lg::StaticMeshInstance>(
      nativeOutlineScene.staticMeshInstances.data(),
      nativeOutlineScene.staticMeshInstances.size()
    ),
    std::span<const lg::GltfPlayerModelInstance>(
      nativeOutlineScene.gltfPlayerModelInstances.data(),
      nativeOutlineScene.gltfPlayerModelInstances.size()
    ),
    std::span<const lg::OutlineMaskDraw>(
      nativeOutlineScene.outlineMaskDraws.data(),
      nativeOutlineScene.outlineMaskDraws.size()
    ),
    1921U,
    1081U,
    1.0F
  );
  failures += expect(
    nativeCenteredOutlinePlan.hasWork &&
      nativeCenteredOutlinePlan.dimensions.workWidth == 1921U &&
      nativeCenteredOutlinePlan.dimensions.workHeight == 1081U &&
      nearlyEqual(nativeCenteredOutlinePlan.maxWorkRadiusPixels, 1.5F),
    "native outline work plans should keep odd output size and final-pixel width"
  );
  const lg::OutlineWorkPlan centeredBoxOutlinePlan = lg::buildOutlineWorkPlan(
    legacyModelScene.camera,
    std::span<const lg::Vertex3D>(
      legacyModelScene.vertices.data(),
      legacyModelScene.vertices.size()
    ),
    std::span<const lg::StaticMeshInstance>(
      legacyModelScene.staticMeshInstances.data(),
      legacyModelScene.staticMeshInstances.size()
    ),
    std::span<const lg::OutlineMaskDraw>(
      legacyModelScene.outlineMaskDraws.data(),
      legacyModelScene.outlineMaskDraws.size()
    ),
    1920U,
    1080U
  );
  failures += expect(
    centeredBoxOutlinePlan.hasWork &&
      !centeredBoxOutlinePlan.conservativeFallback &&
      centeredBoxOutlinePlan.maskDrawCalls == 1U &&
      centeredBoxOutlinePlan.dilationDrawCalls == 1U &&
      centeredBoxOutlinePlan.compositeDrawCalls == 1U,
    "procedural box outline work plan should project static cube instance bounds"
  );

  lg::StaticMeshInstance longWeaponInstance =
    legacyModelScene.staticMeshInstances.front();
  longWeaponInstance.mesh = lg::MeshHandle::RemoteFreezeGunBody;
  longWeaponInstance.worldBounds.radius *= 4.0F;
  const std::array<lg::StaticMeshInstance, 1> longWeaponInstances = {{
    longWeaponInstance,
  }};
  const std::array<lg::OutlineMaskDraw, 1> longWeaponDraws = {{
    {
      0U,
      0U,
      enemyMaskDraw.state,
      lg::MeshHandle::RemoteFreezeGunBody,
      0U,
      1U,
    },
  }};
  const lg::OutlineWorkPlan longWeaponPlan = lg::buildOutlineWorkPlan(
    legacyModelScene.camera,
    std::span<const lg::Vertex3D>(),
    longWeaponInstances,
    std::span<const lg::GltfPlayerModelInstance>(),
    longWeaponDraws,
    1920U,
    1080U,
    1.0F
  );
  lg::ProjectedPoint longWeaponRightEdge;
  const bool longWeaponEdgeProjected = lg::projectPerspectivePoint(
    legacyModelScene.camera,
    longWeaponInstance.worldBounds.center +
      legacyModelScene.camera.right * longWeaponInstance.worldBounds.radius,
    longWeaponRightEdge
  );
  const float longWeaponRightPixel =
    (longWeaponRightEdge.x + 1.0F) * 0.5F * 1920.0F;
  failures += expect(
    longWeaponPlan.hasWork &&
      !longWeaponPlan.conservativeFallback &&
      longWeaponEdgeProjected &&
      static_cast<float>(
        longWeaponPlan.finalRect.x + longWeaponPlan.finalRect.width
      ) >= longWeaponRightPixel,
    "native outline work rect should contain the true bounds of a long weapon"
  );

  lg::PlayerState edgeOpponent = opponent;
  edgeOpponent.position = {4.0F, -3.0F, 0.9F};
  const lg::Scene3D edgeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    edgeOpponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  const lg::OutlineWorkPlan edgeOutlinePlan = lg::buildOutlineWorkPlan(
    edgeScene.camera,
    std::span<const lg::Vertex3D>(edgeScene.vertices.data(), edgeScene.vertices.size()),
    std::span<const lg::StaticMeshInstance>(
      edgeScene.staticMeshInstances.data(),
      edgeScene.staticMeshInstances.size()
    ),
    std::span<const lg::GltfPlayerModelInstance>(
      edgeScene.gltfPlayerModelInstances.data(),
      edgeScene.gltfPlayerModelInstances.size()
    ),
    std::span<const lg::OutlineMaskDraw>(
      edgeScene.outlineMaskDraws.data(),
      edgeScene.outlineMaskDraws.size()
    ),
    1920U,
    1080U
  );
  failures += expect(
    edgeOutlinePlan.hasWork &&
      edgeOutlinePlan.finalRect.x >= 0 &&
      edgeOutlinePlan.finalRect.x + edgeOutlinePlan.finalRect.width <= 1920 &&
      edgeOutlinePlan.workRect.x >= 0 &&
      edgeOutlinePlan.workRect.x + edgeOutlinePlan.workRect.width <= 960 &&
      edgeOutlinePlan.finalRect.width >=
        static_cast<int>(settings.enemyOutlineWidth + 4.0F),
    "screen-edge outlined player should receive a clamped rectangle with outline and filtering margin"
  );

  std::array<lg::Vertex3D, 1> invalidOutlineVertices = {{
    {baseScene.camera.position, {}, 0.0F, 0.0F, 0U},
  }};
  const std::array<lg::OutlineMaskDraw, 1> invalidOutlineDraws = {{
    {0U, 1U, enemyMaskDraw.state},
  }};
  const lg::OutlineWorkPlan invalidOutlinePlan = lg::buildOutlineWorkPlan(
    baseScene.camera,
    invalidOutlineVertices,
    invalidOutlineDraws,
    1920U,
    1080U
  );
  failures += expect(
    invalidOutlinePlan.hasWork &&
      invalidOutlinePlan.conservativeFallback &&
      invalidOutlinePlan.finalRect.x == 0 &&
      invalidOutlinePlan.finalRect.y == 0 &&
      invalidOutlinePlan.finalRect.width == 1920 &&
      invalidOutlinePlan.finalRect.height == 1080,
    "invalid or camera-intersecting outline bounds should use the conservative full-target fallback"
  );

  lg::RenderSettings isolationOutlineDisabledSettings = settings;
  isolationOutlineDisabledSettings.drawPlayerOutlines = false;
  const lg::Scene3D isolationOutlineDisabledScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    isolationOutlineDisabledSettings
  );
  failures += expect(
    isolationOutlineDisabledScene.visibleRemotePlayers == 1 &&
      isolationOutlineDisabledScene.remoteBodyModelsBuilt == 1 &&
      isolationOutlineDisabledScene.remoteWeaponModelsBuilt == 1 &&
      isolationOutlineDisabledScene.playerOutlinesBuilt == 0 &&
      isolationOutlineDisabledScene.outlineMaskDraws.empty(),
    "disabled player outlines should prevent only outline mask construction"
  );

  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> remotePlayers = {};
  remotePlayers[1] =
    lg::RemotePlayerView{
      opponent,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  lg::PlayerState secondOpponent = opponent;
  secondOpponent.position.y += 3.0F;
  remotePlayers[2] =
    lg::RemotePlayerView{
      secondOpponent,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  const lg::Scene3D multiOpponentScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    remotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    multiOpponentScene.gltfPlayerModelInstances.size() == 2U &&
      multiOpponentScene.gltfPlayerModelInstances[0].firstBone !=
        multiOpponentScene.gltfPlayerModelInstances[1].firstBone &&
      multiOpponentScene.gltfPlayerModelInstances[0].boneCount ==
        duelistModel.jointCount() &&
      multiOpponentScene.gltfPlayerModelInstances[1].boneCount ==
        duelistModel.jointCount() &&
      multiOpponentScene.gltfPlayerModelStats.bodyBatches ==
        static_cast<std::uint32_t>(duelistModel.primitives().size()) &&
      multiOpponentScene.gltfPlayerModelStats.bodyDrawCalls ==
        multiOpponentScene.gltfPlayerModelStats.bodyBatches,
    "perspective scene should batch multiple GLB remote players by primitive"
  );
  failures += expect(
    multiOpponentScene.remoteWeaponStats.instancesSubmitted == 2 &&
      multiOpponentScene.remoteWeaponStats.batches == 1 &&
      multiOpponentScene.remoteWeaponStats.drawCalls == 1,
    "multiple remotes holding the same weapon should form one remote weapon batch"
  );
  const lg::Scene3D multiBoxOpponentScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    remotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyModelSettings
  );
  failures += expect(
    multiBoxOpponentScene.playerBoxStats.visiblePlayers == 2 &&
      multiBoxOpponentScene.playerBoxStats.instancesSubmitted == 14 &&
      multiBoxOpponentScene.playerBoxStats.opaqueBatches == 1 &&
      multiBoxOpponentScene.playerBoxStats.opaqueDrawCalls == 1 &&
      multiBoxOpponentScene.playerBoxStats.outlineMaskDrawCalls == 2 &&
      multiBoxOpponentScene.normalPlayerBodyDynamicVertices == 0,
    "two visible procedural box players should combine into one compatible cube body batch"
  );

  remotePlayers[2].selectedWeapon = lg::Weapon::RocketLauncher;
  const lg::Scene3D mixedWeaponScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    remotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    mixedWeaponScene.remoteWeaponStats.instancesSubmitted == 4 &&
      mixedWeaponScene.remoteWeaponStats.batches == 4 &&
      mixedWeaponScene.remoteWeaponStats.drawCalls == 4,
    "remotes holding different weapons should form separate remote weapon batches"
  );

  lg::PlayerState behindOpponent = opponent;
  behindOpponent.position = {-4.0F, 2.0F, 0.9F};
  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> behindRemotePlayers = {};
  behindRemotePlayers[1] =
    lg::RemotePlayerView{
      behindOpponent,
      inactiveBeam,
      lg::Weapon::LightningGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  const lg::Scene3D noRemoteScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount>{},
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  const lg::Scene3D culledBehindScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    behindRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
      culledBehindScene.remoteCandidates == 1 &&
      culledBehindScene.remoteFrustumVisible == 0 &&
      culledBehindScene.remoteFrustumCulled == 1 &&
      culledBehindScene.remoteBodyModelsBuilt == 0 &&
      culledBehindScene.remoteWeaponModelsBuilt == 0 &&
      culledBehindScene.remoteWeaponStats.candidates == 1 &&
      culledBehindScene.remoteWeaponStats.frustumCulled == 1 &&
      culledBehindScene.remoteWeaponStats.instancesSubmitted == 0 &&
      culledBehindScene.playerOutlinesBuilt == 0 &&
      culledBehindScene.vertices.size() == noRemoteScene.vertices.size(),
    "remote behind camera should not add body, weapon, or outline vertices when culling is enabled"
  );
  const lg::Scene3D culledBehindBoxScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    behindRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    legacyModelSettings
  );
  failures += expect(
    culledBehindBoxScene.playerBoxStats.culledPlayers == 1 &&
      culledBehindBoxScene.playerBoxStats.visiblePlayers == 0 &&
      culledBehindBoxScene.playerBoxStats.instancesSubmitted == 0 &&
      playerBoxInstanceCount(culledBehindBoxScene) == 0U,
    "off-screen procedural box player should be culled before body-part instances are emitted"
  );
  lg::RenderSettings frustumCullDisabledSettings = settings;
  frustumCullDisabledSettings.frustumCullRemotePlayers = false;
  const lg::Scene3D uncullableBehindScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    behindRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    frustumCullDisabledSettings
  );
  failures += expect(
      uncullableBehindScene.remoteCandidates == 1 &&
      uncullableBehindScene.remoteFrustumVisible == 1 &&
      uncullableBehindScene.remoteFrustumCulled == 0 &&
      uncullableBehindScene.remoteBodyModelsBuilt == 1 &&
      uncullableBehindScene.remoteWeaponModelsBuilt == 1 &&
      uncullableBehindScene.remoteWeaponStats.instancesSubmitted == 1 &&
      uncullableBehindScene.playerOutlinesBuilt == 1 &&
      uncullableBehindScene.gltfPlayerModelInstances.size() == 1U,
    "r_frustum_cull 0 should preserve remote body, weapon, and outline construction"
  );

  for (lg::Weapon weapon : lg::kWeaponSlotOrder) {
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> weaponRemotePlayers = {};
    weaponRemotePlayers[1] =
      lg::RemotePlayerView{
        opponent,
        inactiveBeam,
        weapon,
        0.0F,
        1.0F,
        true,
        false,
        {},
      };
    const lg::Scene3D weaponScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      weaponRemotePlayers,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      settings
    );
    const lg::MeshHandle mesh = lg::remoteWeaponMeshHandle(weapon);
    const lg::StaticMeshAsset* asset = lg::staticMeshAsset(mesh);
    const lg::MaterialMeshAsset* materialAsset = lg::materialMeshAsset(mesh);
    bool foundWeaponInstance = false;
    lg::StaticMeshInstance foundWeapon = {};
    bool foundRevolverCylinder = weapon != lg::Weapon::Revolver;
    for (const lg::StaticMeshInstance& instance : weaponScene.staticMeshInstances) {
      if (instance.mesh == mesh && instance.pass == lg::RenderPass::OpaqueWorld) {
        foundWeaponInstance = true;
        foundWeapon = instance;
      }
      foundRevolverCylinder =
        foundRevolverCylinder ||
        (
          instance.mesh == lg::MeshHandle::RemoteRevolverCylinder &&
          instance.pass == lg::RenderPass::OpaqueWorld
        );
    }
    const std::uint32_t expectedInstances = weapon == lg::Weapon::RocketLauncher
      ? 3U
      : (
          weapon == lg::Weapon::Revolver ||
          weapon == lg::Weapon::MachineGun
        )
        ? 2U
        : (
            weapon == lg::Weapon::FreezeGun ||
            weapon == lg::Weapon::PlasmaGun
          )
          ? 3U
          : 1U;
    bool revolverGripAlignedAndSized = true;
    if (weapon == lg::Weapon::Revolver && foundWeaponInstance) {
      const lg::Vec3 grip = transformPoint(
        foundWeapon,
        {-0.23F, 0.0F, -0.24F}
      );
      const lg::Vec3 expectedHand = {
        opponent.position.x + opponent.bounds.radius * 0.18F,
        opponent.position.y - opponent.bounds.radius * 0.84F,
        opponent.position.z + opponent.bounds.halfHeight * 0.06F,
      };
      const float modelScale = lg::length(
        transformPoint(foundWeapon, {1.0F, 0.0F, 0.0F}) -
          foundWeapon.modelTranslation
      );
      revolverGripAlignedAndSized =
        lg::length(grip - expectedHand) < 0.001F &&
        nearlyEqual(modelScale, 0.45F);
    }
    bool plasmaGripAligned = true;
    if (weapon == lg::Weapon::PlasmaGun && foundWeaponInstance) {
      const lg::Vec3 grip = transformPoint(
        foundWeapon,
        lg::plasmaGunGripSocket()
      );
      const lg::Vec3 expectedHand = {
        opponent.position.x + opponent.bounds.radius * 0.18F,
        opponent.position.y - opponent.bounds.radius * 0.84F,
        opponent.position.z + opponent.bounds.halfHeight * 0.06F,
      };
      plasmaGripAligned = lg::length(grip - expectedHand) < 0.001F;
    }
    failures += expect(
      mesh != lg::MeshHandle::Invalid &&
        (
          (asset != nullptr && !asset->vertices.empty()) ||
          (materialAsset != nullptr && !materialAsset->vertices.empty())
        ) &&
        foundWeaponInstance &&
        foundRevolverCylinder &&
        revolverGripAlignedAndSized &&
        plasmaGripAligned &&
        weaponScene.remoteWeaponStats.instancesSubmitted == expectedInstances &&
        weaponScene.remoteWeaponStats.legacyDynamicVertices == 0,
      "every playable weapon should map to its expected static mesh instances"
    );
  }

  const lg::MaterialMeshAsset* revolverMaterial =
    lg::materialMeshAsset(lg::MeshHandle::RemoteRevolverBody);
  bool hasPolishedMetal = false;
  bool hasNonMetallicWalnut = false;
  if (revolverMaterial != nullptr) {
    for (const lg::WeaponMaterialVertex3D& vertex : revolverMaterial->vertices) {
      hasPolishedMetal = hasPolishedMetal ||
        (vertex.metallic > 0.9F && vertex.roughness < 0.3F);
      hasNonMetallicWalnut = hasNonMetallicWalnut ||
        (
          vertex.metallic < 0.1F &&
          static_cast<float>(vertex.baseColor.red) >
            static_cast<float>(vertex.baseColor.green) * 1.5F &&
          vertex.baseColor.green > vertex.baseColor.blue
        );
    }
  }
  failures += expect(
    hasPolishedMetal && hasNonMetallicWalnut,
    "revolver material mesh should preserve contrasting steel and walnut properties"
  );

  const std::array<const lg::MaterialMeshAsset*, 2> machineGunMaterials = {{
    lg::materialMeshAsset(lg::MeshHandle::RemoteMachineGunBody),
    lg::materialMeshAsset(lg::MeshHandle::RemoteMachineGunBarrels),
  }};
  bool hasMachineGunSteel = false;
  bool hasMachineGunBrass = false;
  bool hasMachineGunCyanAccent = false;
  for (const lg::MaterialMeshAsset* machineGunMaterial : machineGunMaterials) {
    if (machineGunMaterial == nullptr) continue;
    for (const lg::WeaponMaterialVertex3D& vertex : machineGunMaterial->vertices) {
      hasMachineGunSteel = hasMachineGunSteel ||
        (vertex.metallic > 0.9F && vertex.roughness < 0.3F);
      hasMachineGunBrass = hasMachineGunBrass ||
        (
          vertex.metallic > 0.5F &&
          vertex.baseColor.red > vertex.baseColor.green &&
          vertex.baseColor.green > vertex.baseColor.blue
        );
      hasMachineGunCyanAccent = hasMachineGunCyanAccent ||
        (
          vertex.metallic < 0.1F &&
          vertex.baseColor.blue > vertex.baseColor.red * 2U
        );
    }
  }
  failures += expect(
    hasMachineGunSteel && hasMachineGunBrass && hasMachineGunCyanAccent,
    "machine-gun material mesh should preserve steel, brass, and cyan accents"
  );

  const std::array<const lg::MaterialMeshAsset*, 3> rocketLauncherMaterials = {{
    lg::materialMeshAsset(lg::MeshHandle::RemoteRocketLauncherBody),
    lg::materialMeshAsset(lg::MeshHandle::RemoteRocketLauncherRecoil),
    lg::materialMeshAsset(lg::MeshHandle::RemoteRocketLauncherLatch),
  }};
  bool hasRocketLauncherMetal = false;
  bool hasRocketLauncherRed = false;
  for (const lg::MaterialMeshAsset* material : rocketLauncherMaterials) {
    if (material == nullptr) continue;
    for (const lg::WeaponMaterialVertex3D& vertex : material->vertices) {
      hasRocketLauncherMetal = hasRocketLauncherMetal || vertex.metallic > 0.7F;
      hasRocketLauncherRed = hasRocketLauncherRed ||
        (
          vertex.baseColor.red > vertex.baseColor.green * 2U &&
          vertex.baseColor.red > vertex.baseColor.blue * 2U
        );
    }
  }
  failures += expect(
    hasRocketLauncherMetal && hasRocketLauncherRed,
    "rocket-launcher material meshes should preserve metal and red identification paint"
  );

  const std::array<lg::MeshHandle, 13> cachedBoundsMeshes = {{
    lg::MeshHandle::RemoteMachineGunBody,
    lg::MeshHandle::RemoteMachineGunBarrels,
    lg::MeshHandle::RemoteRevolverBody,
    lg::MeshHandle::RemoteRevolverCylinder,
    lg::MeshHandle::RemoteRocketLauncherBody,
    lg::MeshHandle::RemoteRocketLauncherRecoil,
    lg::MeshHandle::RemoteRocketLauncherLatch,
    lg::MeshHandle::RemoteFreezeGunBody,
    lg::MeshHandle::RemoteFreezeGunFocus,
    lg::MeshHandle::RemoteFreezeGunCoolant,
    lg::MeshHandle::RemotePlasmaGunBody,
    lg::MeshHandle::RemotePlasmaGunProngs,
    lg::MeshHandle::RemotePlasmaGunCore,
  }};
  bool cachedMaterialBoundsMatch = true;
  for (lg::MeshHandle mesh : cachedBoundsMeshes) {
    const lg::MaterialMeshAsset* material = lg::materialMeshAsset(mesh);
    if (material == nullptr) {
      cachedMaterialBoundsMatch = false;
      continue;
    }
    const lg::BoundingSphere previous = legacyMaterialMeshBounds(material->vertices);
    cachedMaterialBoundsMatch = cachedMaterialBoundsMatch &&
      material->localBounds.center.x == previous.center.x &&
      material->localBounds.center.y == previous.center.y &&
      material->localBounds.center.z == previous.center.z &&
      material->localBounds.radius == previous.radius;
  }
  failures += expect(
    cachedMaterialBoundsMatch,
    "cached material weapon bounds should match the prior two-pass calculation"
  );

  lg::LightningGunResult opponentBeam;
  opponentBeam.active = true;
  opponentBeam.start = opponent.position;
  opponentBeam.end = player.position;
  const lg::Scene3D beamScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    opponentBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    beamScene.vertices.size() > baseScene.vertices.size(),
    "active opponent lightning should add world-space beam geometry"
  );

  settings.enemyBeamAlpha = 0.5F;
  const lg::Scene3D translucentBeamScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    opponentBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    translucentBeamScene.vertices.size() == baseScene.vertices.size() &&
      !translucentBeamScene.translucentVertices.empty(),
    "transparent beam geometry should use the non-depth-writing batch"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> shotgunFires = {};
  shotgunFires[0].fired = true;
  shotgunFires[0].hit = true;
  shotgunFires[0].weapon = lg::Weapon::Shotgun;
  shotgunFires[0].start = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  shotgunFires[0].end = shotgunFires[0].start + lg::Vec3{8.0F, 0.0F, 0.0F};
  shotgunFires[0].pelletCount = lg::kShotgunPelletCount;
  shotgunFires[0].pelletHitCount = 5;
  const lg::Scene3D shotgunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    shotgunFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    shotgunScene.transientVfxStats.tracerInstancesSubmitted == 0 &&
      shotgunScene.transientVfxStats.legacyMachineGunShotgunVisualDraws == 0,
    "retained shotgun fire should not emit legacy dynamic tracer geometry"
  );
  lg::RenderSettings localShotgunWeaponStartSettings = settings;
  localShotgunWeaponStartSettings.shotgunWeaponModelStart = true;

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> machineGunFires = {};
  machineGunFires[0].fired = true;
  machineGunFires[0].hit = true;
  machineGunFires[0].weapon = lg::Weapon::MachineGun;
  machineGunFires[0].start = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  machineGunFires[0].end = machineGunFires[0].start + lg::Vec3{9.0F, 0.0F, 3.0F};
  machineGunFires[0].visualSeed = 1;
  const lg::Scene3D machineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    machineGunFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    machineGunScene.transientVfxStats.tracerInstancesSubmitted == 0 &&
      machineGunScene.transientVfxStats.legacyMachineGunShotgunVisualDraws == 0,
    "retained machine gun fire should not emit legacy dynamic tracer geometry"
  );
  std::array<lg::TransientTracer, 8> tracerInstances = {};
  tracerInstances[0] = {
    machineGunFires[0].start,
    machineGunFires[0].end,
    0.0F,
    0.05F,
    0.012F,
    {255, 220, 128, 180},
    machineGunFires[0].visualSeed,
    lg::TracerStyle::MachineGun,
  };
  const lg::Scene3D machineGunTracerScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 1U),
    settings
  );
  failures += expect(
    machineGunTracerScene.transientVfxStats.tracerInstancesSubmitted == 1 &&
      machineGunTracerScene.transientVfxStats.activeMachineGunTracers == 1 &&
      machineGunTracerScene.translucentVertices.size() == 12U,
    "one active MG transient tracer should emit one transparent beam"
  );
  if (!machineGunTracerScene.translucentVertices.empty()) {
    float minTracerZ = machineGunTracerScene.translucentVertices.front().position.z;
    float maxTracerZ = minTracerZ;
    for (const lg::Vertex3D& vertex : machineGunTracerScene.translucentVertices) {
      minTracerZ = std::min(minTracerZ, vertex.position.z);
      maxTracerZ = std::max(maxTracerZ, vertex.position.z);
    }
    failures += expect(
      maxTracerZ - minTracerZ > 2.5F,
      "MG tracer beam geometry should preserve upward pitch"
    );
  }
  tracerInstances[0] = {
    machineGunFires[0].start,
    machineGunFires[0].start + lg::Vec3{0.16F, 0.0F, 0.0F},
    0.0F,
    0.045F,
    0.045F,
    {255, 188, 76, 235},
    machineGunFires[0].visualSeed,
    lg::TracerStyle::MachineGunMuzzleFlash,
  };
  const lg::Scene3D muzzleFlashScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 1U),
    settings
  );
  const bool hasAdditiveMuzzleBillboard = std::any_of(
    muzzleFlashScene.simpleInstances.begin(),
    muzzleFlashScene.simpleInstances.end(),
    [](const lg::SimpleRenderInstance& instance) {
      return instance.billboard == lg::BillboardHandle::ExplosionFlash &&
        instance.pass == lg::RenderPass::AdditiveGlow;
    }
  );
  failures += expect(
    muzzleFlashScene.transientVfxStats.activeMachineGunMuzzleFlashes == 1 &&
      muzzleFlashScene.transientVfxStats.muzzleFlashInstancesSubmitted == 1 &&
      hasAdditiveMuzzleBillboard &&
      muzzleFlashScene.translucentVertices.size() == 12U,
    "MG muzzle flash should combine a directional flame with one additive billboard"
  );

  for (std::size_t index = 0; index < 6U; ++index) {
    tracerInstances[index] = {
      shotgunFires[0].start,
      shotgunFires[0].start + lg::Vec3{4.0F, static_cast<float>(index) * 0.08F, 0.0F},
      0.0F,
      0.065F,
      0.008F,
      {230, 180, 96, 150},
      static_cast<std::uint32_t>(index),
      lg::TracerStyle::Shotgun,
    };
  }
  const lg::Scene3D shotgunTracerScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 6U),
    settings
  );
  failures += expect(
    shotgunTracerScene.transientVfxStats.tracerInstancesSubmitted == 6 &&
      shotgunTracerScene.transientVfxStats.activeShotgunTracers == 6 &&
      shotgunTracerScene.transientVfxStats.tracerBatches == 1 &&
      shotgunTracerScene.transientVfxStats.tracerDrawCalls == 1 &&
      shotgunTracerScene.translucentVertices.size() == 72U,
    "SG representative tracers should batch into one transparent draw"
  );
  const lg::Vec3 sharedPelletDirection = lg::shotgunPelletDirection(
    {1.0F, 0.0F, 0.0F},
    {0.0F, -1.0F, 0.0F},
    {0.0F, 0.0F, 1.0F},
    0.0872665F,
    5U
  );
  failures += expect(
    std::isfinite(sharedPelletDirection.x) &&
      std::isfinite(sharedPelletDirection.y) &&
      std::isfinite(sharedPelletDirection.z) &&
      lg::length(sharedPelletDirection) > 0.99F,
    "shotgun tracer directions should come from the finite shared pellet-spread helper"
  );
  tracerInstances[0].start = player.position + lg::Vec3{-100.0F, 0.0F, 0.0F};
  tracerInstances[0].end = player.position + lg::Vec3{-90.0F, 0.0F, 0.0F};
  const lg::Scene3D culledTracerScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(tracerInstances.data(), 1U),
    settings
  );
  failures += expect(
    culledTracerScene.transientVfxStats.tracerCandidates == 1 &&
      culledTracerScene.transientVfxStats.tracerFrustumCulled == 1 &&
      culledTracerScene.translucentVertices.empty(),
    "offscreen transient tracers should be frustum culled before geometry emission"
  );

  lg::RenderSettings localMachineGunSettings = settings;
  localMachineGunSettings.localSelectedWeapon = lg::Weapon::MachineGun;
  const lg::Scene3D localMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localMachineGunSettings
  );
  bool hasMachineGunBodyViewModel = false;
  bool hasMachineGunBarrelViewModel = false;
  for (const lg::StaticMeshInstance& instance : localMachineGunScene.staticMeshInstances) {
    hasMachineGunBodyViewModel = hasMachineGunBodyViewModel ||
      (instance.mesh == lg::MeshHandle::RemoteMachineGunBody &&
       instance.pass == lg::RenderPass::ViewModel);
    hasMachineGunBarrelViewModel = hasMachineGunBarrelViewModel ||
      (instance.mesh == lg::MeshHandle::RemoteMachineGunBarrels &&
       instance.pass == lg::RenderPass::ViewModel);
  }
  failures += expect(
    hasMachineGunBodyViewModel &&
      hasMachineGunBarrelViewModel &&
      localMachineGunScene.viewModelStats.drawCalls == 2 &&
      localMachineGunScene.viewModelStats.dynamicVertices == 0,
    "first-person machine gun should use separate static body and barrel viewmodel meshes"
  );

  lg::RenderSettings spinningMachineGunSettings = localMachineGunSettings;
  spinningMachineGunSettings.machineGunBarrelRotationRadians =
    3.14159265359F * 0.5F;
  const lg::Scene3D spinningMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    spinningMachineGunSettings
  );
  const auto findMachineGunPart = [](
    const lg::Scene3D& scene,
    lg::MeshHandle mesh
  ) -> const lg::StaticMeshInstance* {
    const auto found = std::find_if(
      scene.staticMeshInstances.begin(),
      scene.staticMeshInstances.end(),
      [mesh](const lg::StaticMeshInstance& instance) {
        return instance.mesh == mesh && instance.pass == lg::RenderPass::ViewModel;
      }
    );
    return found != scene.staticMeshInstances.end() ? &*found : nullptr;
  };
  const lg::StaticMeshInstance* idleBody = findMachineGunPart(
    localMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* spinningBody = findMachineGunPart(
    spinningMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* idleBarrels = findMachineGunPart(
    localMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  const lg::StaticMeshInstance* spinningBarrels = findMachineGunPart(
    spinningMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  const bool bodyUnchanged = idleBody != nullptr && spinningBody != nullptr &&
    nearlyEqual(idleBody->modelRow0.x, spinningBody->modelRow0.x) &&
    nearlyEqual(idleBody->modelRow1.y, spinningBody->modelRow1.y) &&
    nearlyEqual(idleBody->modelRow2.z, spinningBody->modelRow2.z);
  bool barrelsRotateAroundAuthoredPivot = false;
  if (idleBarrels != nullptr && spinningBarrels != nullptr) {
    const lg::Vec3 viewModelPivot = lg::machineGunBarrelPivot();
    const lg::Vec3 idlePivot = transformPoint(
      *idleBarrels,
      viewModelPivot
    );
    const lg::Vec3 spinningPivot = transformPoint(
      *spinningBarrels,
      viewModelPivot
    );
    barrelsRotateAroundAuthoredPivot =
      lg::length(idlePivot - spinningPivot) < 0.001F &&
      !nearlyEqual(idleBarrels->modelRow1.y, spinningBarrels->modelRow1.y);
  }
  failures += expect(
    bodyUnchanged && barrelsRotateAroundAuthoredPivot,
    "machine-gun barrel rotation should preserve its authored pivot and leave the body fixed"
  );
  lg::RenderSettings recoilingMachineGunSettings = localMachineGunSettings;
  recoilingMachineGunSettings.machineGunRecoilAmount = 1.0F;
  recoilingMachineGunSettings.machineGunVibrationAmount = 1.0F;
  recoilingMachineGunSettings.machineGunVibrationPhaseRadians = 0.75F;
  const lg::Scene3D recoilingMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    recoilingMachineGunSettings
  );
  const lg::StaticMeshInstance* recoilingBody = findMachineGunPart(
    recoilingMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* recoilingBarrels = findMachineGunPart(
    recoilingMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  bool authoredMuzzleMatchesAllWeaponPositions = true;
  for (int weaponPosition = 0; weaponPosition < 3; ++weaponPosition) {
    lg::RenderSettings positionedSettings = localMachineGunSettings;
    positionedSettings.weaponPosition = weaponPosition;
    const lg::Scene3D positionedScene = lg::buildPerspectiveScene(
      16.0F / 9.0F,
      arena,
      player,
      opponent,
      inactiveBeam,
      inactiveBeam,
      weaponFires,
      rocketExplosions,
      rockets,
      positionedSettings
    );
    const lg::StaticMeshInstance* positionedBody = findMachineGunPart(
      positionedScene,
      lg::MeshHandle::RemoteMachineGunBody
    );
    authoredMuzzleMatchesAllWeaponPositions =
      authoredMuzzleMatchesAllWeaponPositions &&
      positionedBody != nullptr &&
      lg::length(
        transformPoint(*positionedBody, lg::machineGunMuzzleSocket()) -
        lg::firstPersonMachineGunMuzzlePosition(player, positionedSettings)
      ) < 0.001F;
  }
  failures += expect(
    authoredMuzzleMatchesAllWeaponPositions,
    "MG tracer origin should match the authored model socket in every weapon position"
  );
  failures += expect(
    idleBody != nullptr && idleBarrels != nullptr &&
      recoilingBody != nullptr && recoilingBarrels != nullptr &&
      lg::length(recoilingBody->modelTranslation - idleBody->modelTranslation) > 0.001F &&
      lg::length(
        recoilingBarrels->modelTranslation - idleBarrels->modelTranslation
      ) > 0.001F,
    "MG firing response should move body and barrels together without changing gameplay state"
  );

  lg::RenderSettings hiddenLocalWeaponSettings = localMachineGunSettings;
  hiddenLocalWeaponSettings.showOwnWeapons = false;
  const lg::Scene3D hiddenLocalWeaponScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    hiddenLocalWeaponSettings
  );
  bool hasHiddenMachineGunViewModel = false;
  for (const lg::StaticMeshInstance& instance :
       hiddenLocalWeaponScene.staticMeshInstances) {
    hasHiddenMachineGunViewModel =
      hasHiddenMachineGunViewModel ||
      (
        (instance.mesh == lg::MeshHandle::RemoteMachineGunBody ||
         instance.mesh == lg::MeshHandle::RemoteMachineGunBarrels) &&
        instance.pass == lg::RenderPass::ViewModel
      );
  }
  failures += expect(
    !hasHiddenMachineGunViewModel &&
      hiddenLocalWeaponScene.viewModelStats.drawCalls == 0 &&
      hiddenLocalWeaponScene.viewModelStats.dynamicVertices == 0,
    "r_show_weapons 0 should suppress local first-person weapon models"
  );

  lg::RenderSettings localShotgunSettings = settings;
  localShotgunSettings.localSelectedWeapon = lg::Weapon::Shotgun;
  const lg::Scene3D localShotgunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localShotgunSettings
  );
  bool hasShotgunViewModel = false;
  for (const lg::StaticMeshInstance& instance : localShotgunScene.staticMeshInstances) {
    hasShotgunViewModel =
      hasShotgunViewModel ||
      (
        instance.mesh == lg::MeshHandle::RemoteShotgun &&
        instance.pass == lg::RenderPass::ViewModel
      );
  }
  failures += expect(
    hasShotgunViewModel &&
      localShotgunScene.viewModelStats.drawCalls == 1 &&
      localShotgunScene.viewModelStats.dynamicVertices == 0,
    "first-person shotgun should use a static viewmodel mesh without dynamic vertices"
  );

  lg::RenderSettings localRevolverSettings = settings;
  localRevolverSettings.localSelectedWeapon = lg::Weapon::Revolver;
  const lg::Scene3D localRevolverScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localRevolverSettings
  );
  const lg::RenderSettings idleRevolverSettings = localRevolverSettings;
  localRevolverSettings.revolverRecoilAmount = 1.0F;
  localRevolverSettings.revolverCylinderRotationRadians =
    3.14159265359F / 3.0F;
  const lg::Scene3D recoiledRevolverScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localRevolverSettings
  );
  const auto findViewModel = [](const lg::Scene3D& scene, lg::MeshHandle mesh) {
    for (const lg::StaticMeshInstance& instance : scene.staticMeshInstances) {
      if (instance.mesh == mesh && instance.pass == lg::RenderPass::ViewModel) {
        return instance;
      }
    }
    return lg::StaticMeshInstance{};
  };
  const lg::StaticMeshInstance revolverBody = findViewModel(
    localRevolverScene,
    lg::MeshHandle::RemoteRevolverBody
  );
  const lg::StaticMeshInstance recoiledRevolverBody = findViewModel(
    recoiledRevolverScene,
    lg::MeshHandle::RemoteRevolverBody
  );
  const lg::StaticMeshInstance revolverCylinder = findViewModel(
    localRevolverScene,
    lg::MeshHandle::RemoteRevolverCylinder
  );
  const lg::StaticMeshInstance indexedRevolverCylinder = findViewModel(
    recoiledRevolverScene,
    lg::MeshHandle::RemoteRevolverCylinder
  );
  const float revolverViewModelScale = lg::length(revolverBody.modelRow0);
  const bool revolverSocketMatchesViewModel =
    lg::length(
      transformPoint(revolverBody, lg::revolverMuzzleSocket()) -
      lg::firstPersonRevolverMuzzlePosition(
        player,
        idleRevolverSettings
      )
    ) < 0.001F;
  failures += expect(
    revolverBody.mesh == lg::MeshHandle::RemoteRevolverBody &&
      revolverCylinder.mesh == lg::MeshHandle::RemoteRevolverCylinder &&
      nearlyEqual(revolverViewModelScale, 0.40F) &&
      revolverSocketMatchesViewModel &&
      localRevolverScene.viewModelStats.drawCalls == 2 &&
      lg::length(
        recoiledRevolverBody.modelTranslation - revolverBody.modelTranslation
      ) > 0.01F &&
      lg::length(
        indexedRevolverCylinder.modelRow1 - revolverCylinder.modelRow1
      ) > 0.01F,
    "first-person revolver should submit body and cylinder with recoil and one-step indexing transforms"
  );
  std::array<lg::TransientTracer, 1> revolverFlash = {{
    {
      lg::firstPersonRevolverMuzzlePosition(player, idleRevolverSettings),
      lg::firstPersonRevolverMuzzlePosition(player, idleRevolverSettings) +
        lg::Vec3{0.22F, 0.0F, 0.0F},
      0.0F,
      0.052F,
      0.062F,
      {255, 212, 118, 245},
      19U,
      lg::TracerStyle::RevolverMuzzleFlash,
    },
  }};
  const lg::Scene3D revolverFlashScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>(revolverFlash),
    idleRevolverSettings
  );
  const std::size_t revolverFlashBillboards = static_cast<std::size_t>(
    std::count_if(
      revolverFlashScene.simpleInstances.begin(),
      revolverFlashScene.simpleInstances.end(),
      [](const lg::SimpleRenderInstance& instance) {
        return instance.pass == lg::RenderPass::AdditiveGlow &&
          (
            instance.billboard == lg::BillboardHandle::ExplosionFlash ||
            instance.billboard == lg::BillboardHandle::ExplosionHalo
          );
      }
    )
  );
  failures += expect(
    revolverFlashScene.transientVfxStats.activeRevolverMuzzleFlashes == 1U &&
      revolverFlashBillboards == 2U &&
      revolverFlashScene.translucentVertices.size() == 12U,
    "revolver muzzle flash should combine a warm directional flame, flash, and halo"
  );

  lg::RenderSettings localRocketLauncherSettings = settings;
  localRocketLauncherSettings.localSelectedWeapon = lg::Weapon::RocketLauncher;
  const lg::Scene3D idleRocketLauncherScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localRocketLauncherSettings
  );
  localRocketLauncherSettings.rocketLauncherMechanicalAmount = 1.0F;
  localRocketLauncherSettings.rocketLauncherRecoilAmount = 1.0F;
  const lg::Scene3D firingRocketLauncherScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    localRocketLauncherSettings
  );
  const lg::StaticMeshInstance idleRocketBody = findViewModel(
    idleRocketLauncherScene,
    lg::MeshHandle::RemoteRocketLauncherBody
  );
  const lg::StaticMeshInstance idleRocketRecoil = findViewModel(
    idleRocketLauncherScene,
    lg::MeshHandle::RemoteRocketLauncherRecoil
  );
  const lg::StaticMeshInstance firingRocketBody = findViewModel(
    firingRocketLauncherScene,
    lg::MeshHandle::RemoteRocketLauncherBody
  );
  const lg::StaticMeshInstance firingRocketRecoil = findViewModel(
    firingRocketLauncherScene,
    lg::MeshHandle::RemoteRocketLauncherRecoil
  );
  failures += expect(
    idleRocketBody.mesh == lg::MeshHandle::RemoteRocketLauncherBody &&
      idleRocketRecoil.mesh == lg::MeshHandle::RemoteRocketLauncherRecoil &&
      idleRocketLauncherScene.viewModelStats.drawCalls == 3U &&
      lg::length(firingRocketBody.modelTranslation - idleRocketBody.modelTranslation) > 0.001F &&
      lg::length(firingRocketRecoil.modelTranslation - idleRocketRecoil.modelTranslation) > 0.001F &&
      lg::length(
        lg::firstPersonRocketLauncherMuzzlePosition(
          player,
          localRocketLauncherSettings
        ) -
        transformPoint(
          firingRocketRecoil,
          lg::rocketLauncherMuzzleSocket() - lg::Vec3{0.5F, 0.0F, 0.08F}
        )
      ) < 0.001F,
    "first-person rocket launcher should submit three animated material parts and preserve its muzzle socket"
  );

  lg::RenderSettings localPlasmaGunSettings = settings;
  localPlasmaGunSettings.localSelectedWeapon = lg::Weapon::PlasmaGun;
  const lg::Scene3D idlePlasmaGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    {},
    {},
    localPlasmaGunSettings
  );
  lg::RenderSettings firingPlasmaGunSettings = localPlasmaGunSettings;
  firingPlasmaGunSettings.plasmaGunContainmentAmount = 1.0F;
  const lg::Scene3D firingPlasmaGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    {},
    {},
    firingPlasmaGunSettings
  );
  const lg::StaticMeshInstance idlePlasmaBody = findViewModel(
    idlePlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunBody
  );
  const lg::StaticMeshInstance idlePlasmaProngs = findViewModel(
    idlePlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunProngs
  );
  const lg::StaticMeshInstance idlePlasmaCore = findViewModel(
    idlePlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunCore
  );
  const lg::StaticMeshInstance firingPlasmaProngs = findViewModel(
    firingPlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunProngs
  );
  const lg::StaticMeshInstance firingPlasmaCore = findViewModel(
    firingPlasmaGunScene,
    lg::MeshHandle::RemotePlasmaGunCore
  );
  failures += expect(
    idlePlasmaBody.mesh == lg::MeshHandle::RemotePlasmaGunBody &&
      idlePlasmaProngs.mesh == lg::MeshHandle::RemotePlasmaGunProngs &&
      idlePlasmaCore.mesh == lg::MeshHandle::RemotePlasmaGunCore &&
      idlePlasmaGunScene.viewModelStats.drawCalls == 3U,
    "first-person plasma gun should submit its three material meshes"
  );
  failures += expect(
    lg::length(
      transformPoint(idlePlasmaBody, lg::plasmaGunMuzzleSocket()) -
      lg::firstPersonPlasmaGunMuzzlePosition(player, localPlasmaGunSettings)
    ) < 0.001F,
    "first-person plasma projectile origin should match the authored muzzle socket"
  );
  failures += expect(
    lg::length(firingPlasmaCore.modelRow0) <
      lg::length(idlePlasmaCore.modelRow0) &&
      lg::length(
        firingPlasmaProngs.modelTranslation -
        idlePlasmaProngs.modelTranslation
      ) > 0.005F,
    "plasma firing response should contract the core and shift the containment prongs"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> remoteMachineGunFires = {};
  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> machineGunRemotePlayers = {};
  machineGunRemotePlayers[1] =
    lg::RemotePlayerView{
      opponent,
      inactiveBeam,
      lg::Weapon::MachineGun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  remoteMachineGunFires[1].fired = true;
  remoteMachineGunFires[1].hit = true;
  remoteMachineGunFires[1].weapon = lg::Weapon::MachineGun;
  remoteMachineGunFires[1].start =
    opponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  remoteMachineGunFires[1].end =
    remoteMachineGunFires[1].start + lg::Vec3{-9.0F, 0.0F, 0.0F};
  remoteMachineGunFires[1].visualSeed = 1;
  const lg::Scene3D remoteMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    machineGunRemotePlayers,
    inactiveBeam,
    remoteMachineGunFires,
    rocketExplosions,
    rockets,
    settings
  );
  machineGunRemotePlayers[1].machineGunBarrelRotationRadians =
    3.14159265359F * 0.5F;
  remoteMachineGunFires[1].visualSeed = 4;
  const lg::Scene3D rotatedMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    machineGunRemotePlayers,
    inactiveBeam,
    remoteMachineGunFires,
    rocketExplosions,
    rockets,
    settings
  );
  failures += expect(
    remoteMachineGunScene.transientVfxStats.tracerInstancesSubmitted == 0 &&
      rotatedMachineGunScene.transientVfxStats.tracerInstancesSubmitted == 0,
    "remote machine gun retained fires should wait for transient VFX consumption"
  );
  const auto findRemoteMachineGunPart = [](
    const lg::Scene3D& scene,
    lg::MeshHandle mesh
  ) -> const lg::StaticMeshInstance* {
    const auto found = std::find_if(
      scene.staticMeshInstances.begin(),
      scene.staticMeshInstances.end(),
      [mesh](const lg::StaticMeshInstance& instance) {
        return instance.mesh == mesh && instance.pass == lg::RenderPass::OpaqueWorld;
      }
    );
    return found != scene.staticMeshInstances.end() ? &*found : nullptr;
  };
  const lg::StaticMeshInstance* idleRemoteBody = findRemoteMachineGunPart(
    remoteMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* spinningRemoteBody = findRemoteMachineGunPart(
    rotatedMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  const lg::StaticMeshInstance* idleRemoteBarrels = findRemoteMachineGunPart(
    remoteMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  const lg::StaticMeshInstance* spinningRemoteBarrels = findRemoteMachineGunPart(
    rotatedMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBarrels
  );
  failures += expect(
    idleRemoteBody != nullptr && spinningRemoteBody != nullptr &&
      idleRemoteBarrels != nullptr && spinningRemoteBarrels != nullptr &&
      nearlyEqual(idleRemoteBody->modelRow1.y, spinningRemoteBody->modelRow1.y) &&
      !nearlyEqual(
        idleRemoteBarrels->modelRow1.y,
        spinningRemoteBarrels->modelRow1.y
      ),
    "remote machine-gun presentation angle should rotate only its barrel instance"
  );
  auto pitchedMachineGunPlayers = machineGunRemotePlayers;
  pitchedMachineGunPlayers[1].player.viewPitchRadians = 1.4F;
  const lg::Scene3D pitchedMachineGunScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    pitchedMachineGunPlayers,
    inactiveBeam,
    remoteMachineGunFires,
    rocketExplosions,
    rockets,
    settings
  );
  const lg::StaticMeshInstance* pitchedRemoteBody = findRemoteMachineGunPart(
    pitchedMachineGunScene,
    lg::MeshHandle::RemoteMachineGunBody
  );
  failures += expect(
    idleRemoteBody != nullptr && pitchedRemoteBody != nullptr &&
      pitchedRemoteBody->modelRow2.x > idleRemoteBody->modelRow2.x + 0.1F,
    "remote held weapon should visibly pitch toward the clamped head aim direction"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> remoteShotgunFires = {};
  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> shotgunRemotePlayers = {};
  lg::PlayerState shotgunOpponent = opponent;
  shotgunOpponent.viewYawRadians = 3.14159265359F;
  shotgunRemotePlayers[1] =
    lg::RemotePlayerView{
      shotgunOpponent,
      inactiveBeam,
      lg::Weapon::Shotgun,
      0.0F,
      1.0F,
      true,
      false,
      {},
    };
  remoteShotgunFires[1].fired = true;
  remoteShotgunFires[1].hit = true;
  remoteShotgunFires[1].weapon = lg::Weapon::Shotgun;
  remoteShotgunFires[1].start =
    shotgunOpponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  remoteShotgunFires[1].end =
    remoteShotgunFires[1].start + lg::Vec3{-8.0F, 0.0F, 0.0F};
  remoteShotgunFires[1].pelletCount = lg::kShotgunPelletCount;
  remoteShotgunFires[1].pelletHitCount = 5;
  lg::RenderSettings shotgunWeaponStartSettings = settings;
  shotgunWeaponStartSettings.shotgunWeaponModelStart = true;
  const lg::Scene3D remoteShotgunEyeScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    remoteShotgunFires,
    rocketExplosions,
    rockets,
    settings
  );
  const lg::Scene3D remoteShotgunWeaponScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    remoteShotgunFires,
    rocketExplosions,
    rockets,
    shotgunWeaponStartSettings
  );
  failures += expect(
    remoteShotgunEyeScene.transientVfxStats.tracerInstancesSubmitted == 0 &&
      remoteShotgunWeaponScene.transientVfxStats.tracerInstancesSubmitted == 0,
    "remote shotgun retained fires should wait for transient VFX consumption"
  );

  lg::RenderSettings effectOnlySettings = settings;
  effectOnlySettings.showOwnWeapons = false;
  effectOnlySettings.drawRemotePlayers = false;
  effectOnlySettings.drawRemoteWeapons = false;

  lg::LightningGunResult remoteLightningBeam;
  remoteLightningBeam.active = true;
  remoteLightningBeam.start =
    shotgunOpponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  remoteLightningBeam.end =
    remoteLightningBeam.start + lg::Vec3{-8.0F, 0.0F, 0.0F};
  shotgunRemotePlayers[1].selectedWeapon = lg::Weapon::LightningGun;
  shotgunRemotePlayers[1].lightningGun = remoteLightningBeam;
  const lg::Scene3D remoteLightningMuzzleScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    effectOnlySettings
  );
  failures += expect(
    hasAnyVertex(remoteLightningMuzzleScene) &&
      maxVertexX(remoteLightningMuzzleScene) <
        remoteLightningBeam.start.x - 0.2F,
    "remote lightning beam should start from the third-person weapon muzzle"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> remoteRailFires = {};
  remoteRailFires[1].fired = true;
  remoteRailFires[1].hit = true;
  remoteRailFires[1].weapon = lg::Weapon::Railgun;
  remoteRailFires[1].start =
    shotgunOpponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  remoteRailFires[1].end =
    remoteRailFires[1].start + lg::Vec3{-8.0F, 0.0F, 0.0F};
  shotgunRemotePlayers[1].selectedWeapon = lg::Weapon::Railgun;
  shotgunRemotePlayers[1].lightningGun = inactiveBeam;
  const lg::Scene3D remoteRailMuzzleScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    remoteRailFires,
    rocketExplosions,
    rockets,
    effectOnlySettings
  );
  failures += expect(
    hasAnyVertex(remoteRailMuzzleScene) &&
      maxVertexX(remoteRailMuzzleScene) <
        remoteRailFires[1].start.x - 0.2F,
    "remote railgun beam should start from the third-person weapon muzzle"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> remoteRevolverFires = {};
  remoteRevolverFires[1] = remoteRailFires[1];
  remoteRevolverFires[1].weapon = lg::Weapon::Revolver;
  shotgunRemotePlayers[1].selectedWeapon = lg::Weapon::Revolver;
  lg::RenderSettings revolverEffectSettings = effectOnlySettings;
  revolverEffectSettings.revolverTracerAlpha[1] = 0.5F;
  const lg::Scene3D remoteRevolverMuzzleScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    remoteRevolverFires,
    rocketExplosions,
    rockets,
    revolverEffectSettings
  );
  bool hasWarmRevolverTracer = false;
  for (const lg::Vertex3D& vertex : remoteRevolverMuzzleScene.translucentVertices) {
    hasWarmRevolverTracer = hasWarmRevolverTracer ||
      (
        vertex.color.red > vertex.color.green &&
        vertex.color.green > vertex.color.blue &&
        vertex.color.alpha > 80U &&
        vertex.color.alpha < 205U
      );
  }
  failures += expect(
    hasAnyVertex(remoteRevolverMuzzleScene) &&
      hasWarmRevolverTracer &&
      maxVertexX(remoteRevolverMuzzleScene) <
        remoteRevolverFires[1].start.x - 0.2F,
    "remote revolver beam should start from the modeled barrel socket"
  );

  std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> plasmaRockets = {};
  const lg::ProjectileVisualDescriptor* plasmaDescriptor =
    lg::projectileVisualDescriptor(lg::ProjectileVisualType::Plasma);
  failures += expect(
    plasmaDescriptor != nullptr &&
      plasmaDescriptor->coreMesh != lg::MeshHandle::Invalid &&
      plasmaDescriptor->glowBillboard != lg::BillboardHandle::Invalid &&
      lg::staticMeshAsset(plasmaDescriptor->coreMesh) != nullptr &&
      lg::billboardAsset(plasmaDescriptor->glowBillboard) != nullptr,
    "plasma projectile visual descriptor should resolve to core mesh and glow billboard assets"
  );
  plasmaRockets[0].active = true;
  plasmaRockets[0].owner = 1;
  plasmaRockets[0].weapon = lg::Weapon::PlasmaGun;
  plasmaRockets[0].position =
    shotgunOpponent.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  plasmaRockets[0].velocity = {-50.0F, 0.0F, 0.0F};
  shotgunRemotePlayers[1].selectedWeapon = lg::Weapon::LightningGun;
  const lg::Scene3D plasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    settings
  );
  failures += expect(
    plasmaProjectileScene.projectileStats.projectilesActive == 1 &&
      plasmaProjectileScene.projectileStats.projectilesRendered == 1 &&
      plasmaProjectileScene.projectileStats.projectileCoreInstances == 1 &&
      plasmaProjectileScene.projectileStats.projectileGlowInstances == 1 &&
      plasmaProjectileScene.projectileStats.projectileMeshDrawCalls == 1 &&
      plasmaProjectileScene.projectileStats.projectileGlowDrawCalls == 1 &&
      plasmaProjectileScene.projectileStats.legacyProjectileDynamicVertices == 0 &&
      plasmaProjectileScene.simpleInstances.size() == 2U &&
      plasmaProjectileScene.simpleBatches.size() == 2U,
    "active plasma projectile should produce one core instance, one glow instance, and no legacy vertices"
  );
  failures += expect(
    plasmaProjectileScene.simpleInstances[0].position.x <
      plasmaRockets[0].position.x - 0.35F,
    "remote plasma projectile instances should render from the plasma gun model"
  );

  plasmaRockets[0].owner = 0;
  plasmaRockets[0].position =
    player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  const lg::Scene3D localPlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    localShotgunWeaponStartSettings
  );
  failures += expect(
    localPlasmaProjectileScene.simpleInstances.size() == 2U &&
      localPlasmaProjectileScene.simpleInstances[0].position.z <
        plasmaRockets[0].position.z - 0.15F,
    "local plasma projectile instances should render from the first-person weapon muzzle"
  );

  plasmaRockets[0].active = false;
  const lg::Scene3D inactivePlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    settings
  );
  failures += expect(
    inactivePlasmaProjectileScene.projectileStats.projectilesActive == 0 &&
      inactivePlasmaProjectileScene.simpleInstances.empty(),
    "inactive plasma projectile should produce no render instances"
  );

  plasmaRockets[0].active = true;
  plasmaRockets[0].owner = 0;
  plasmaRockets[0].position = player.position + lg::Vec3{-12.0F, 0.0F, 0.65F};
  const lg::Scene3D culledPlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    settings
  );
  failures += expect(
    culledPlasmaProjectileScene.projectileStats.projectilesActive == 1 &&
      culledPlasmaProjectileScene.projectileStats.projectilesFrustumCulled == 1 &&
      culledPlasmaProjectileScene.simpleInstances.empty(),
    "outside-frustum plasma projectile should produce no render instances when culling is enabled"
  );
  const lg::Scene3D uncullablePlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    frustumCullDisabledSettings
  );
  failures += expect(
    uncullablePlasmaProjectileScene.projectileStats.projectilesRendered == 1 &&
      uncullablePlasmaProjectileScene.simpleInstances.size() == 2U,
    "outside-frustum plasma projectile should produce instances when culling is disabled"
  );

  plasmaRockets[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  plasmaRockets[1] = plasmaRockets[0];
  plasmaRockets[1].position = player.position + lg::Vec3{4.0F, 0.25F, 0.65F};
  const lg::Scene3D multiPlasmaProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    plasmaRockets,
    settings
  );
  failures += expect(
    multiPlasmaProjectileScene.projectileStats.projectileCoreInstances == 2 &&
      multiPlasmaProjectileScene.projectileStats.projectileGlowInstances == 2 &&
      multiPlasmaProjectileScene.projectileStats.projectileMeshDrawCalls == 1 &&
      multiPlasmaProjectileScene.projectileStats.projectileGlowDrawCalls == 1 &&
      multiPlasmaProjectileScene.simpleBatches.size() == 2U,
    "multiple plasma projectiles should group into one core batch and one glow batch"
  );
  failures += expect(
    !multiPlasmaProjectileScene.staticMeshBatches.empty() &&
      validStaticMeshBatches(multiPlasmaProjectileScene) &&
      validSimpleBatches(multiPlasmaProjectileScene) &&
      multiPlasmaProjectileScene.simpleBatches[0].firstInstance == 0U &&
      multiPlasmaProjectileScene.simpleBatches[0].instanceCount == 2U &&
      multiPlasmaProjectileScene.simpleBatches[1].firstInstance == 2U &&
      multiPlasmaProjectileScene.simpleBatches[1].instanceCount == 2U,
    "final batches should cover sorted interleaved mesh and pass instances"
  );
  failures += expect(
    multiPlasmaProjectileScene.projectileStats.projectileInstanceUploadBytes ==
      plasmaProjectileScene.projectileStats.projectileInstanceUploadBytes * 2U,
    "projectile instance upload bytes should scale by instance count, not mesh vertex count"
  );

  const lg::ProjectileVisualDescriptor* rocketDescriptor =
    lg::projectileVisualDescriptor(lg::ProjectileVisualType::Rocket);
  const lg::ProjectileVisualDescriptor* grenadeDescriptor =
    lg::projectileVisualDescriptor(lg::ProjectileVisualType::Grenade);
  failures += expect(
    rocketDescriptor != nullptr &&
      rocketDescriptor->coreMesh == lg::MeshHandle::RocketProjectile &&
      rocketDescriptor->glowBillboard == lg::BillboardHandle::RocketFlame &&
      lg::staticMeshAsset(rocketDescriptor->coreMesh) != nullptr &&
      lg::billboardAsset(rocketDescriptor->glowBillboard) != nullptr,
    "rocket projectile descriptor should resolve to rocket mesh and flame billboard assets"
  );
  failures += expect(
    grenadeDescriptor != nullptr &&
      grenadeDescriptor->coreMesh == lg::MeshHandle::GrenadeProjectile &&
      grenadeDescriptor->glowBillboard == lg::BillboardHandle::Invalid &&
      lg::staticMeshAsset(grenadeDescriptor->coreMesh) != nullptr,
    "grenade projectile descriptor should resolve to grenade mesh without glow billboard"
  );

  std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> rocketProjectiles = {};
  rocketProjectiles[0].active = true;
  rocketProjectiles[0].owner = 1;
  rocketProjectiles[0].weapon = lg::Weapon::RocketLauncher;
  rocketProjectiles[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  rocketProjectiles[0].velocity = {30.0F, 0.0F, 0.0F};
  const lg::Scene3D rocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  failures += expect(
    rocketProjectileScene.projectileStats.projectilesRendered == 1 &&
      rocketProjectileScene.projectileStats.rocketInstances == 1 &&
      rocketProjectileScene.projectileStats.projectileGlowInstances == 1 &&
      rocketProjectileScene.projectileStats.opaqueProjectileBatches == 1 &&
      rocketProjectileScene.projectileStats.additiveProjectileBatches == 1 &&
      rocketProjectileScene.projectileStats.legacyProjectileDynamicVertices == 0 &&
      rocketProjectileScene.simpleInstances.size() == 2U,
    "active rocket projectile should produce one opaque rocket instance and one additive flame instance"
  );
  failures += expect(
    rocketProjectileScene.simpleInstances[0].position.x <
      rocketProjectiles[0].position.x - 0.35F,
    "remote rocket projectile instances should render from the rocket launcher barrel"
  );
  failures += expect(
    rocketProjectileScene.simpleBatches.size() == 2U &&
      rocketProjectileScene.simpleBatches[0].mesh == lg::MeshHandle::RocketProjectile &&
      rocketProjectileScene.simpleBatches[0].pass == lg::RenderPass::OpaqueWorld &&
      rocketProjectileScene.simpleBatches[1].billboard == lg::BillboardHandle::RocketFlame &&
      rocketProjectileScene.simpleBatches[1].pass == lg::RenderPass::AdditiveGlow,
    "rocket projectile should use the rocket mesh opaque pass and flame additive pass"
  );

  rocketProjectiles[0].velocity = {0.0F, 0.0F, 30.0F};
  const lg::Scene3D upwardRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  failures += expect(
    upwardRocketProjectileScene.simpleInstances.size() == 2U &&
      std::fabs(upwardRocketProjectileScene.simpleInstances[0].pitchRadians -
        (3.14159265359F * 0.5F)) < 0.001F &&
      upwardRocketProjectileScene.simpleInstances[1].position.z <
        upwardRocketProjectileScene.simpleInstances[0].position.z - 0.25F,
    "upward rocket projectile should pitch the mesh and place flame behind it in 3D"
  );

  rocketProjectiles[0].velocity = {0.0F, 0.0F, -30.0F};
  const lg::Scene3D downwardRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  failures += expect(
    downwardRocketProjectileScene.simpleInstances.size() == 2U &&
      std::fabs(downwardRocketProjectileScene.simpleInstances[0].pitchRadians +
        (3.14159265359F * 0.5F)) < 0.001F &&
      downwardRocketProjectileScene.simpleInstances[1].position.z >
        downwardRocketProjectileScene.simpleInstances[0].position.z + 0.25F,
    "downward rocket projectile should pitch the mesh and place flame behind it in 3D"
  );

  rocketProjectiles[0].owner = 0;
  rocketProjectiles[0].position = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  rocketProjectiles[0].velocity = {30.0F, 0.0F, 0.0F};
  const lg::Scene3D localRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    localShotgunWeaponStartSettings
  );
  failures += expect(
    localRocketProjectileScene.simpleInstances.size() == 2U &&
      localRocketProjectileScene.simpleInstances[0].position.z <
        rocketProjectiles[0].position.z - 0.15F,
    "local rocket projectile instances should render from the first-person weapon barrel"
  );
  lg::RenderSettings hiddenLocalProjectileSettings = localShotgunWeaponStartSettings;
  hiddenLocalProjectileSettings.showOwnWeapons = false;
  const lg::Scene3D hiddenLocalRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    hiddenLocalProjectileSettings
  );
  failures += expect(
    hiddenLocalRocketProjectileScene.simpleInstances.size() == 2U &&
      hiddenLocalRocketProjectileScene.simpleInstances[0].position.z <
        localRocketProjectileScene.simpleInstances[0].position.z - 0.15F,
    "r_show_weapons 0 should render local rocket projectiles from the bottom-center hidden weapon origin"
  );

  std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> grenadeProjectiles = {};
  grenadeProjectiles[0].active = true;
  grenadeProjectiles[0].owner = 1;
  grenadeProjectiles[0].weapon = lg::Weapon::GrenadeLauncher;
  grenadeProjectiles[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  grenadeProjectiles[0].velocity = {18.0F, 2.0F, 6.0F};
  const lg::Scene3D grenadeProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    grenadeProjectiles,
    settings
  );
  failures += expect(
    grenadeProjectileScene.projectileStats.projectilesRendered == 1 &&
      grenadeProjectileScene.projectileStats.grenadeInstances == 1 &&
      grenadeProjectileScene.projectileStats.projectileGlowInstances == 0 &&
      grenadeProjectileScene.projectileStats.opaqueProjectileBatches == 1 &&
      grenadeProjectileScene.projectileStats.additiveProjectileBatches == 0 &&
      grenadeProjectileScene.projectileStats.legacyProjectileDynamicVertices == 0 &&
      grenadeProjectileScene.simpleInstances.size() == 1U &&
      grenadeProjectileScene.simpleInstances[0].mesh == lg::MeshHandle::GrenadeProjectile &&
      grenadeProjectileScene.simpleInstances[0].pass == lg::RenderPass::OpaqueWorld,
    "active grenade projectile should produce one opaque grenade instance and no glow"
  );

  grenadeProjectiles[0].velocity = {};
  const lg::Scene3D stillGrenadeProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    grenadeProjectiles,
    settings
  );
  failures += expect(
    stillGrenadeProjectileScene.simpleInstances.size() == 1U &&
      std::isfinite(stillGrenadeProjectileScene.simpleInstances[0].rotationRadians),
    "zero-velocity grenade projectile rotation should remain finite"
  );

  rocketProjectiles[0].position = player.position + lg::Vec3{-12.0F, 0.0F, 0.65F};
  const lg::Scene3D culledRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  grenadeProjectiles[0].position = player.position + lg::Vec3{-12.0F, 0.0F, 0.65F};
  const lg::Scene3D culledGrenadeProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    grenadeProjectiles,
    settings
  );
  failures += expect(
    culledRocketProjectileScene.projectileStats.projectilesFrustumCulled == 1 &&
      culledRocketProjectileScene.simpleInstances.empty() &&
      culledGrenadeProjectileScene.projectileStats.projectilesFrustumCulled == 1 &&
      culledGrenadeProjectileScene.simpleInstances.empty(),
    "frustum culling should exclude off-screen rocket and grenade projectiles"
  );

  rocketProjectiles[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  rocketProjectiles[1] = rocketProjectiles[0];
  rocketProjectiles[1].position = player.position + lg::Vec3{4.0F, 0.2F, 0.65F};
  const lg::Scene3D multiRocketProjectileScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rocketProjectiles,
    settings
  );
  failures += expect(
    multiRocketProjectileScene.projectileStats.rocketInstances == 2 &&
      multiRocketProjectileScene.projectileStats.projectileGlowInstances == 2 &&
      multiRocketProjectileScene.projectileStats.projectileMeshDrawCalls == 1 &&
      multiRocketProjectileScene.projectileStats.projectileGlowDrawCalls == 1 &&
      multiRocketProjectileScene.simpleBatches.size() == 2U,
    "multiple rocket projectiles should batch into shared opaque and additive draws"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> rocketFireOnly = {};
  rocketFireOnly[0].fired = true;
  rocketFireOnly[0].weapon = lg::Weapon::RocketLauncher;
  rocketFireOnly[0].start = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  rocketFireOnly[0].end = rocketFireOnly[0].start + lg::Vec3{1.2F, 0.0F, 0.0F};
  const lg::Scene3D rocketFireOnlyScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount>{},
    inactiveBeam,
    rocketFireOnly,
    rocketExplosions,
    rockets,
    settings
  );
  bool foundRocketFireLine = false;
  for (const lg::Vertex3D& vertex : rocketFireOnlyScene.vertices) {
    foundRocketFireLine =
      foundRocketFireLine ||
      (
        vertex.color.red >= 240 &&
        vertex.color.green >= 120 &&
        vertex.color.green <= 175 &&
        vertex.color.blue <= 90
      );
  }
  failures += expect(
    !foundRocketFireLine,
    "rocket launcher fire events should not draw a separate projectile line"
  );

  std::array<lg::WeaponFireResult, lg::kDuelPlayerCount> plasmaFireOnly = {};
  plasmaFireOnly[0].fired = true;
  plasmaFireOnly[0].weapon = lg::Weapon::PlasmaGun;
  plasmaFireOnly[0].start = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
  plasmaFireOnly[0].end = plasmaFireOnly[0].start + lg::Vec3{1.2F, 0.0F, 0.0F};
  const lg::Scene3D plasmaFireOnlyScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    std::array<lg::RemotePlayerView, lg::kDuelPlayerCount>{},
    inactiveBeam,
    plasmaFireOnly,
    rocketExplosions,
    rockets,
    settings
  );
  bool foundPlasmaFireLine = false;
  for (const lg::Vertex3D& vertex : plasmaFireOnlyScene.vertices) {
    foundPlasmaFireLine =
      foundPlasmaFireLine ||
      (
        vertex.color.green >= 220 &&
        vertex.color.red <= 130 &&
        vertex.color.blue <= 170
      );
  }
  failures += expect(
    !foundPlasmaFireLine,
    "plasma gun fire events should not draw a separate beam line"
  );

  std::array<lg::TransientEffect, 8> explosionEffects = {};
  explosionEffects[0] = {
    lg::TransientEffectType::RocketExplosionFlash,
    player.position + lg::Vec3{3.0F, 0.0F, 0.65F},
    0.01F,
    0.05F,
    0.8F,
    1.6F,
    {255, 228, 132, 230},
    11U,
  };
  explosionEffects[1] = {
    lg::TransientEffectType::RocketExplosionCore,
    explosionEffects[0].position,
    0.04F,
    0.18F,
    0.7F,
    2.6F,
    {255, 112, 44, 200},
    12U,
  };
  explosionEffects[2] = {
    lg::TransientEffectType::RocketExplosionHalo,
    explosionEffects[0].position,
    0.02F,
    0.12F,
    1.5F,
    3.2F,
    {255, 72, 28, 82},
    13U,
  };
  const lg::Scene3D rocketExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 3U),
    settings
  );
  failures += expect(
    rocketExplosionScene.transientVfxStats.activeExplosionEffects == 3 &&
      rocketExplosionScene.transientVfxStats.explosionInstancesSubmitted == 3 &&
      rocketExplosionScene.transientVfxStats.explosionDrawCalls == 3 &&
      rocketExplosionScene.transientVfxStats.explosionOpaqueBatches == 1 &&
      rocketExplosionScene.transientVfxStats.explosionAdditiveBatches == 2 &&
      rocketExplosionScene.transientVfxStats.legacyWireframeExplosionDraws == 0,
    "rocket explosion effects should submit flash, faceted core, halo, and no legacy wireframe draws"
  );
  bool foundRocketExplosionCore = false;
  bool foundRocketExplosionFlash = false;
  bool foundRocketExplosionHalo = false;
  for (const lg::SimpleRenderInstance& instance : rocketExplosionScene.simpleInstances) {
    foundRocketExplosionCore =
      foundRocketExplosionCore || instance.mesh == lg::MeshHandle::ExplosionCore;
    foundRocketExplosionFlash =
      foundRocketExplosionFlash || instance.billboard == lg::BillboardHandle::ExplosionFlash;
    foundRocketExplosionHalo =
      foundRocketExplosionHalo || instance.billboard == lg::BillboardHandle::ExplosionHalo;
  }
  failures += expect(
    foundRocketExplosionCore && foundRocketExplosionFlash && foundRocketExplosionHalo,
    "rocket explosion burst should use reusable core, flash, and halo assets"
  );

  explosionEffects[0].type = lg::TransientEffectType::PlasmaExplosionFlash;
  explosionEffects[0].color = {122, 255, 184, 210};
  explosionEffects[0].finalScale = 0.55F;
  explosionEffects[1].type = lg::TransientEffectType::PlasmaExplosionCore;
  explosionEffects[1].color = {76, 248, 210, 185};
  explosionEffects[1].finalScale = 0.75F;
  explosionEffects[2].type = lg::TransientEffectType::PlasmaExplosionHalo;
  explosionEffects[2].color = {64, 255, 168, 88};
  explosionEffects[2].finalScale = 0.95F;
  const lg::Scene3D plasmaExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 3U),
    settings
  );
  failures += expect(
    plasmaExplosionScene.transientVfxStats.explosionInstancesSubmitted == 3 &&
      plasmaExplosionScene.simpleInstances.size() == 3U,
    "plasma explosion effects should submit a distinct compact three-instance burst"
  );

  explosionEffects[0] = {
    lg::TransientEffectType::GrenadeExplosionFlash,
    player.position + lg::Vec3{3.0F, 0.2F, 0.65F},
    0.01F,
    0.05F,
    0.7F,
    1.4F,
    {255, 224, 104, 220},
    21U,
  };
  explosionEffects[1] = {
    lg::TransientEffectType::GrenadeExplosionCore,
    explosionEffects[0].position,
    0.04F,
    0.20F,
    0.8F,
    2.7F,
    {255, 178, 66, 190},
    22U,
  };
  const lg::Scene3D grenadeExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 2U),
    settings
  );
  failures += expect(
    grenadeExplosionScene.transientVfxStats.explosionInstancesSubmitted == 2 &&
      grenadeExplosionScene.transientVfxStats.explosionDrawCalls == 2,
    "grenade detonation should use the bounded flash plus amber core burst"
  );

  explosionEffects[0] = {
    lg::TransientEffectType::RocketExplosionCore,
    player.position + lg::Vec3{3.0F, 0.0F, 0.65F},
    0.02F,
    0.20F,
    std::numeric_limits<float>::infinity(),
    10000.0F,
    {255, 112, 44, 200},
    31U,
  };
  const lg::Scene3D clampedExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 1U),
    settings
  );
  failures += expect(
    clampedExplosionScene.simpleInstances.size() == 1U &&
      std::isfinite(clampedExplosionScene.simpleInstances[0].scale.x) &&
      clampedExplosionScene.simpleInstances[0].scale.x <= 8.0F,
    "explosion effect scale should remain finite and clamped for unusual input"
  );

  explosionEffects[0].ageSeconds = explosionEffects[0].lifetimeSeconds;
  const lg::Scene3D expiredExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 1U),
    settings
  );
  failures += expect(
    expiredExplosionScene.transientVfxStats.explosionInstancesSubmitted == 0,
    "expired explosion effects should not submit render instances"
  );

  for (std::size_t index = 0; index < 6U; ++index) {
    explosionEffects[index] = {
      index % 3U == 0
        ? lg::TransientEffectType::RocketExplosionFlash
        : index % 3U == 1
          ? lg::TransientEffectType::RocketExplosionCore
          : lg::TransientEffectType::RocketExplosionHalo,
      player.position + lg::Vec3{3.0F + static_cast<float>(index) * 0.1F, 0.0F, 0.65F},
      0.01F,
      0.16F,
      0.6F,
      1.8F,
      {255, 180, 80, 180},
      static_cast<std::uint32_t>(index),
    };
  }
  const lg::Scene3D multiExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    rocketExplosions,
    rockets,
    std::span<const lg::TransientTracer>{},
    std::span<const lg::TransientEffect>(explosionEffects.data(), 6U),
    settings
  );
  failures += expect(
    multiExplosionScene.transientVfxStats.explosionInstancesSubmitted == 6 &&
      multiExplosionScene.transientVfxStats.explosionDrawCalls == 3,
    "multiple overlapping explosions should batch into bounded reusable VFX draws"
  );

  std::array<lg::RocketExplosionResult, lg::kDuelPlayerCount> retainedExplosions = {};
  retainedExplosions[0].active = true;
  retainedExplosions[0].weapon = lg::Weapon::RocketLauncher;
  retainedExplosions[0].position = player.position + lg::Vec3{3.0F, 0.0F, 0.65F};
  retainedExplosions[0].radius = 3.0F;
  retainedExplosions[0].sequence = 99U;
  const lg::Scene3D retainedExplosionScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    shotgunRemotePlayers,
    inactiveBeam,
    weaponFires,
    retainedExplosions,
    rockets,
    settings
  );
  failures += expect(
    retainedExplosionScene.transientVfxStats.legacyWireframeExplosionDraws == 0 &&
      retainedExplosionScene.simpleInstances.empty(),
    "retained authoritative explosions should not draw legacy wireframe boxes directly"
  );

  return failures == 0 ? 0 : 1;
}
