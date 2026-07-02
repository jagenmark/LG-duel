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
    for (const lg::GltfSkinnedModel::GpuVertex& vertex : primitive.vertices) {
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
  }
  failures += expect(
    primitivesFiniteAndSafe &&
      primitiveVertexCount > 0U &&
      primitiveIndexCount > 0U,
    "GLB duelist asset should load persistent finite primitives with normalized safe skin weights"
  );
  failures += expect(
    baseScene.gltfPlayerModelStats.staticMeshGpuBytes == primitiveVertexCount * 60U &&
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
    mixedWeaponScene.remoteWeaponStats.instancesSubmitted == 2 &&
      mixedWeaponScene.remoteWeaponStats.batches == 2 &&
      mixedWeaponScene.remoteWeaponStats.drawCalls == 2,
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
    bool foundWeaponInstance = false;
    for (const lg::StaticMeshInstance& instance : weaponScene.staticMeshInstances) {
      foundWeaponInstance =
        foundWeaponInstance ||
        (instance.mesh == mesh && instance.pass == lg::RenderPass::OpaqueWorld);
    }
    failures += expect(
      mesh != lg::MeshHandle::Invalid &&
        asset != nullptr &&
        !asset->vertices.empty() &&
        foundWeaponInstance &&
        weaponScene.remoteWeaponStats.instancesSubmitted == 1 &&
        weaponScene.remoteWeaponStats.legacyDynamicVertices == 0,
      "every playable weapon should map to a static mesh and submit one remote weapon instance"
    );
  }

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
  machineGunFires[0].end = machineGunFires[0].start + lg::Vec3{9.0F, 0.0F, 0.0F};
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
      machineGunTracerScene.simpleInstances.size() == 1U &&
      machineGunTracerScene.simpleInstances[0].mesh == lg::MeshHandle::MachineGunTracer &&
      machineGunTracerScene.simpleInstances[0].pass == lg::RenderPass::TranslucentWorld,
    "one active MG transient tracer should emit one instanced tracer mesh"
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
      shotgunTracerScene.transientVfxStats.tracerDrawCalls == 1,
    "SG representative tracers should batch into one instanced tracer draw"
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
      culledTracerScene.simpleInstances.empty(),
    "offscreen transient tracers should be frustum culled before instancing"
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
  bool hasMachineGunViewModel = false;
  for (const lg::StaticMeshInstance& instance : localMachineGunScene.staticMeshInstances) {
    hasMachineGunViewModel =
      hasMachineGunViewModel ||
      (
        instance.mesh == lg::MeshHandle::RemoteMachineGun &&
        instance.pass == lg::RenderPass::ViewModel
      );
  }
  failures += expect(
    hasMachineGunViewModel &&
      localMachineGunScene.viewModelStats.drawCalls == 1 &&
      localMachineGunScene.viewModelStats.dynamicVertices == 0,
    "first-person machine gun should use a static viewmodel mesh without dynamic vertices"
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

  rocketProjectiles[0].owner = 0;
  rocketProjectiles[0].position = player.position + lg::Vec3{0.0F, 0.0F, 0.65F};
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
