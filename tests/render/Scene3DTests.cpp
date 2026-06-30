#include "render/Scene3D.hpp"
#include "sim/Arena.hpp"
#include "sim/WeaponCatalog.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
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
  std::size_t rightSideCount = 0;
  std::size_t leftSideCount = 0;
  for (const lg::Vertex3D& vertex : leanScene.vertices) {
    if (
      vertex.color.red >= 120 &&
      vertex.color.green <= settings.enemyGreen &&
      vertex.color.blue <= settings.enemyBlue
    ) {
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
    rightSideCount > 0 && leftSideCount > 0 &&
      std::fabs(
        (rightSideZ / static_cast<float>(rightSideCount)) -
          (leftSideZ / static_cast<float>(leftSideCount))
      ) > 0.01F,
    "enabled enemy lean should tilt the opponent model from lateral velocity"
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
  rightSideZ = 0.0F;
  leftSideZ = 0.0F;
  rightSideCount = 0;
  leftSideCount = 0;
  for (const lg::Vertex3D& vertex : leanDisabledScene.vertices) {
    if (
      vertex.color.red >= 120 &&
      vertex.color.green <= settings.enemyGreen &&
      vertex.color.blue <= settings.enemyBlue
    ) {
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
      ) < 0.001F,
    "disabled enemy lean should keep the camera and opponent model upright"
  );
  opponent.velocity = {};

  std::size_t opponentVertexCount = 0;
  bool opponentWithinBounds = true;
  for (const lg::Vertex3D& vertex : baseScene.vertices) {
    if (
      vertex.color.red >= 120 &&
      vertex.color.green <= settings.enemyGreen &&
      vertex.color.blue <= settings.enemyBlue
    ) {
      ++opponentVertexCount;
      opponentWithinBounds =
        opponentWithinBounds &&
        std::fabs(vertex.position.x - opponent.position.x) <=
          opponent.bounds.radius + 0.001F &&
        std::fabs(vertex.position.y - opponent.position.y) <=
          opponent.bounds.radius + 0.001F &&
        vertex.position.z >=
          opponent.position.z - opponent.bounds.halfHeight - 0.001F &&
        vertex.position.z <=
          opponent.position.z + opponent.bounds.halfHeight + 0.001F;
    }
  }
  failures += expect(
    opponentVertexCount >= 7U * 36U && opponentWithinBounds,
    "opponent should use a simple multi-part model inside gameplay bounds"
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
  float groundedLowestModelZ = opponent.position.z + opponent.bounds.halfHeight;
  float airborneLowestModelZ =
    airborneOpponent.position.z + airborneOpponent.bounds.halfHeight;
  for (const lg::Vertex3D& vertex : baseScene.vertices) {
    if (
      vertex.color.red >= 120 &&
      vertex.color.green <= settings.enemyGreen &&
      vertex.color.blue <= settings.enemyBlue
    ) {
      groundedLowestModelZ = std::min(groundedLowestModelZ, vertex.position.z);
    }
  }
  for (const lg::Vertex3D& vertex : airborneScene.vertices) {
    if (
      vertex.color.red >= 120 &&
      vertex.color.green <= settings.enemyGreen &&
      vertex.color.blue <= settings.enemyBlue
    ) {
      airborneLowestModelZ = std::min(airborneLowestModelZ, vertex.position.z);
    }
  }
  failures += expect(
    airborneScene.vertices.size() == baseScene.vertices.size() &&
      airborneLowestModelZ > groundedLowestModelZ + 0.05F,
    "airborne opponent pose should visibly tuck the lower model upward"
  );

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

  return failures == 0 ? 0 : 1;
}
