#include "render/GltfSkinnedModel.hpp"
#include "render/Scene3D.hpp"
#include "sim/Arena.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
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

bool insidePlayerModelBounds(
  const lg::Vertex3D& vertex,
  const lg::PlayerState& player
) {
  constexpr float kSkinningVisualTolerance = 0.06F;
  return std::fabs(vertex.position.x - player.position.x) <=
      player.bounds.radius + kSkinningVisualTolerance &&
    std::fabs(vertex.position.y - player.position.y) <=
      player.bounds.radius + kSkinningVisualTolerance &&
    vertex.position.z >=
      player.position.z - player.bounds.halfHeight - kSkinningVisualTolerance &&
    vertex.position.z <=
      player.position.z + player.bounds.halfHeight + kSkinningVisualTolerance;
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
    !baseScene.vertices.empty() && baseScene.vertices.size() % 3 == 0,
    "perspective scene should emit triangle-list geometry"
  );
  failures += expect(
    baseScene.visibleRemotePlayers == 1 &&
      baseScene.remoteBodyModelsBuilt == 1 &&
      baseScene.remoteWeaponModelsBuilt == 1 &&
      baseScene.playerOutlinesBuilt == 1,
    "default render settings should build visible remote body, weapon, and outline"
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
      noWeaponScene.playerOutlinesBuilt == 1,
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
      noBodyBeamScene.playerOutlinesBuilt == 1 &&
      noBodyBeamScene.vertices.size() > noBodyNoBeamScene.vertices.size(),
    "disabled remote bodies should not suppress unrelated remote effects or scene data"
  );

  const lg::GltfSkinnedModel& duelistModel = lg::duelistMaleModel();
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
    restMin.z > -0.12F && restMax.z < 0.28F;
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
  float rightSideZ = 0.0F;
  float leftSideZ = 0.0F;
  float leanMinY = opponent.position.y + opponent.bounds.radius;
  float leanMaxY = opponent.position.y - opponent.bounds.radius;
  std::size_t rightSideCount = 0;
  std::size_t leftSideCount = 0;
  for (const lg::Vertex3D& vertex : leanScene.vertices) {
    if (isEnemyModelColor(vertex.color) && insidePlayerModelBounds(vertex, opponent)) {
      leanMinY = std::min(leanMinY, vertex.position.y);
      leanMaxY = std::max(leanMaxY, vertex.position.y);
      if (vertex.position.y < opponent.position.y - 0.01F) {
        rightSideZ += vertex.position.z;
        ++rightSideCount;
      } else if (vertex.position.y > opponent.position.y + 0.01F) {
        leftSideZ += vertex.position.z;
        ++leftSideCount;
      }
    }
  }
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
  rightSideZ = 0.0F;
  leftSideZ = 0.0F;
  float leanDisabledMinY = opponent.position.y + opponent.bounds.radius;
  float leanDisabledMaxY = opponent.position.y - opponent.bounds.radius;
  rightSideCount = 0;
  leftSideCount = 0;
  for (const lg::Vertex3D& vertex : leanDisabledScene.vertices) {
    if (isEnemyModelColor(vertex.color) && insidePlayerModelBounds(vertex, opponent)) {
      leanDisabledMinY = std::min(leanDisabledMinY, vertex.position.y);
      leanDisabledMaxY = std::max(leanDisabledMaxY, vertex.position.y);
      if (vertex.position.y < opponent.position.y - 0.01F) {
        rightSideZ += vertex.position.z;
        ++rightSideCount;
      } else if (vertex.position.y > opponent.position.y + 0.01F) {
        leftSideZ += vertex.position.z;
        ++leftSideCount;
      }
    }
  }
  failures += expect(
    nearlyEqual(leanDisabledScene.camera.right.z, 0.0F) &&
      rightSideCount > 0 && leftSideCount > 0 &&
      std::fabs(
        (rightSideZ / static_cast<float>(rightSideCount)) -
          (leftSideZ / static_cast<float>(leftSideCount))
      ) < 0.15F,
    "disabled enemy lean should keep the camera upright and avoid velocity roll"
  );
  failures += expect(
    std::fabs(leanMinY - leanDisabledMinY) > 0.004F ||
      std::fabs(leanMaxY - leanDisabledMaxY) > 0.004F,
    "enabled enemy lean should use the skinned GLB lean animation"
  );
  opponent.velocity = {};

  std::size_t opponentVertexCount = 0;
  for (const lg::Vertex3D& vertex : baseScene.vertices) {
    if (isEnemyModelColor(vertex.color) && insidePlayerModelBounds(vertex, opponent)) {
      ++opponentVertexCount;
    }
  }
  failures += expect(
    opponentVertexCount >= 1000U,
    "opponent should use the skinned GLB duelist mesh inside gameplay bounds"
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
  std::size_t legacyOpponentVertexCount = 0;
  for (const lg::Vertex3D& vertex : legacyModelScene.vertices) {
    if (isEnemyModelColor(vertex.color) && insidePlayerModelBounds(vertex, opponent)) {
      ++legacyOpponentVertexCount;
    }
  }
  failures += expect(
    legacyOpponentVertexCount > 0U && legacyOpponentVertexCount < opponentVertexCount,
    "r_player_model 0 should keep the previous legacy box model available"
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
  float groundedModelX = 0.0F;
  float airborneModelX = 0.0F;
  float groundedFrontModelX = std::numeric_limits<float>::max();
  float airborneFrontModelX = std::numeric_limits<float>::max();
  float groundedBackModelX = std::numeric_limits<float>::lowest();
  float airborneBackModelX = std::numeric_limits<float>::lowest();
  std::size_t groundedModelCount = 0;
  std::size_t airborneModelCount = 0;
  for (const lg::Vertex3D& vertex : baseScene.vertices) {
    if (isEnemyModelColor(vertex.color) && insidePlayerModelBounds(vertex, opponent)) {
      groundedFrontModelX = std::min(groundedFrontModelX, vertex.position.x);
      groundedBackModelX = std::max(groundedBackModelX, vertex.position.x);
      groundedModelX += vertex.position.x;
      ++groundedModelCount;
    }
  }
  for (const lg::Vertex3D& vertex : airborneScene.vertices) {
    if (isEnemyModelColor(vertex.color) && insidePlayerModelBounds(vertex, airborneOpponent)) {
      airborneFrontModelX = std::min(airborneFrontModelX, vertex.position.x);
      airborneBackModelX = std::max(airborneBackModelX, vertex.position.x);
      airborneModelX += vertex.position.x;
      ++airborneModelCount;
    }
  }
  failures += expect(
    airborneScene.vertices.size() == baseScene.vertices.size() &&
      groundedModelCount > 0 && airborneModelCount > 0 &&
      (
        std::fabs(airborneFrontModelX - groundedFrontModelX) > 0.02F ||
        std::fabs(airborneBackModelX - groundedBackModelX) > 0.02F
      ),
    "airborne opponent should use the skinned GLB jump animation"
  );
  (void)groundedModelX;
  (void)airborneModelX;

  std::size_t outlineVertexCount = 0;
  bool outlineExpandsPastBounds = false;
  for (const lg::Vertex3D& vertex : baseScene.vertices) {
    if (
      vertex.color.red == settings.enemyOutlineRed &&
      vertex.color.green == settings.enemyOutlineGreen &&
      vertex.color.blue == settings.enemyOutlineBlue
    ) {
      ++outlineVertexCount;
      outlineExpandsPastBounds =
        outlineExpandsPastBounds ||
        std::fabs(vertex.position.x - opponent.position.x) >
          opponent.bounds.radius + 0.001F ||
        std::fabs(vertex.position.y - opponent.position.y) >
          opponent.bounds.radius + 0.001F ||
        vertex.position.z <
          opponent.position.z - opponent.bounds.halfHeight - 0.001F ||
        vertex.position.z >
          opponent.position.z + opponent.bounds.halfHeight + 0.001F;
    }
  }
  failures += expect(
    outlineVertexCount > 0 && outlineExpandsPastBounds,
    "enabled enemy outline should emit expanded player geometry"
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
  bool disabledOutlinePresent = false;
  for (const lg::Vertex3D& vertex : outlineDisabledScene.vertices) {
    disabledOutlinePresent =
      disabledOutlinePresent ||
      (
        vertex.color.red == settings.enemyOutlineRed &&
        vertex.color.green == settings.enemyOutlineGreen &&
        vertex.color.blue == settings.enemyOutlineBlue
      );
  }
  failures += expect(
    !disabledOutlinePresent &&
      outlineDisabledScene.vertices.size() < baseScene.vertices.size(),
    "disabled enemy outline should not emit outline geometry"
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
      isolationOutlineDisabledScene.playerOutlinesBuilt == 0,
    "disabled player outlines should prevent only outline geometry construction"
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
    multiOpponentScene.vertices.size() > baseScene.vertices.size(),
    "perspective scene should emit geometry for multiple remote players"
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
      culledBehindScene.playerOutlinesBuilt == 0 &&
      culledBehindScene.vertices.size() == noRemoteScene.vertices.size(),
    "remote behind camera should not add body, weapon, or outline vertices when culling is enabled"
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
      uncullableBehindScene.playerOutlinesBuilt == 1 &&
      uncullableBehindScene.vertices.size() > noRemoteScene.vertices.size(),
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
    std::size_t forwardWeaponVertexCount = 0;
    for (const lg::Vertex3D& vertex : weaponScene.vertices) {
      if (
        vertex.position.x > opponent.position.x + opponent.bounds.radius + 0.04F &&
        std::fabs(vertex.position.y - opponent.position.y) <=
          opponent.bounds.radius + 0.7F &&
        vertex.position.z > opponent.position.z - 0.25F &&
        vertex.position.z < opponent.position.z + opponent.bounds.halfHeight + 0.25F
      ) {
        ++forwardWeaponVertexCount;
      }
    }
    failures += expect(
      forwardWeaponVertexCount > 0,
      "every playable weapon should emit forward world-model geometry"
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
  bool hasShotgunImpactColor = false;
  bool hasShotgunFlashColor = false;
  for (const lg::Vertex3D& vertex : shotgunScene.translucentVertices) {
    hasShotgunImpactColor =
      hasShotgunImpactColor ||
      (
        vertex.color.red >= 200 &&
        vertex.color.green < 120 &&
        vertex.color.blue < 100
      );
    hasShotgunFlashColor =
      hasShotgunFlashColor ||
      (
        vertex.color.red >= 220 &&
        vertex.color.green >= 160 &&
        vertex.color.blue >= 80
      );
  }
  failures += expect(
    shotgunScene.translucentVertices.size() > translucentBeamScene.translucentVertices.size() &&
      hasShotgunImpactColor &&
      hasShotgunFlashColor,
    "shotgun fire should add muzzle flash, pellet traces, and impact puffs"
  );
  lg::RenderSettings localShotgunWeaponStartSettings = settings;
  localShotgunWeaponStartSettings.renderMode = 1;
  localShotgunWeaponStartSettings.shotgunWeaponModelStart = true;
  const lg::Scene3D localShotgunWeaponScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    shotgunFires,
    rocketExplosions,
    rockets,
    localShotgunWeaponStartSettings
  );
  const lg::Vec3 localShotgunVisualDelta =
    shotgunScene.translucentVertices.front().position -
    localShotgunWeaponScene.translucentVertices.front().position;
  failures += expect(
    lg::dot(localShotgunVisualDelta, localShotgunVisualDelta) > 0.01F,
    "shotgun weapon model start toggle should move local first-person shotgun visuals"
  );

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
  bool hasMachineGunImpactColor = false;
  bool hasMachineGunFlashColor = false;
  for (const lg::Vertex3D& vertex : machineGunScene.translucentVertices) {
    hasMachineGunImpactColor =
      hasMachineGunImpactColor ||
      (
        vertex.color.red >= 220 &&
        vertex.color.green < 130 &&
        vertex.color.blue < 90
      );
    hasMachineGunFlashColor =
      hasMachineGunFlashColor ||
      (
        vertex.color.red >= 240 &&
        vertex.color.green >= 180 &&
        vertex.color.blue >= 100
      );
  }
  failures += expect(
    machineGunScene.translucentVertices.size() > translucentBeamScene.translucentVertices.size() &&
      hasMachineGunImpactColor &&
      hasMachineGunFlashColor,
    "machine gun fire should add muzzle flash, tracer, and impact spark"
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
  const lg::Vec3 flashDelta =
    remoteMachineGunScene.translucentVertices.front().position -
    rotatedMachineGunScene.translucentVertices.front().position;
  failures += expect(
    lg::dot(flashDelta, flashDelta) > 0.0001F,
    "machine gun visual seed should rotate the shot source around the weapon"
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
  const lg::Vec3 shotgunVisualDelta =
    remoteShotgunEyeScene.translucentVertices.front().position -
    remoteShotgunWeaponScene.translucentVertices.front().position;
  failures += expect(
    lg::dot(shotgunVisualDelta, shotgunVisualDelta) > 0.01F,
    "shotgun weapon model start toggle should move remote shotgun visuals"
  );

  std::array<lg::RocketProjectileSnapshot, lg::kMaxRocketProjectiles> plasmaRockets = {};
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
  bool foundShiftedPlasmaProjectile = false;
  for (const lg::Vertex3D& vertex : plasmaProjectileScene.vertices) {
    foundShiftedPlasmaProjectile =
      foundShiftedPlasmaProjectile ||
      (
        vertex.color.green >= 130 &&
        vertex.color.green > vertex.color.red * 2U &&
        vertex.color.green > vertex.color.blue &&
        vertex.position.x < plasmaRockets[0].position.x - 0.35F
      );
  }
  failures += expect(
    foundShiftedPlasmaProjectile,
    "remote plasma projectiles should render from the plasma gun model"
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
  bool foundLocalShiftedPlasmaProjectile = false;
  for (const lg::Vertex3D& vertex : localPlasmaProjectileScene.vertices) {
    foundLocalShiftedPlasmaProjectile =
      foundLocalShiftedPlasmaProjectile ||
      (
        vertex.color.green >= 130 &&
        vertex.color.green > vertex.color.red * 2U &&
        vertex.color.green > vertex.color.blue &&
        vertex.position.z < plasmaRockets[0].position.z - 0.15F
      );
  }
  failures += expect(
    foundLocalShiftedPlasmaProjectile,
    "local plasma projectiles should render from the first-person weapon muzzle"
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

  return failures == 0 ? 0 : 1;
}
