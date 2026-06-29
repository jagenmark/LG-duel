#include "render/Scene3D.hpp"
#include "sim/Arena.hpp"
#include "sim/WeaponCatalog.hpp"

#include <array>
#include <cmath>
#include <iostream>
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
  const lg::Scene3D texturedWallScene = lg::buildPerspectiveScene(
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
  bool foundTexturedWallVertex = false;
  bool foundWallUvSpan = false;
  bool foundPrototypeWallAccent = false;
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
      capturedFirstWallUv = true;
      continue;
    }
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
    !foundPrototypeWallAccent,
    "wall scene geometry should not emit old green prototype accents over textures"
  );
  arena.wallCount = 0;

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

  return failures == 0 ? 0 : 1;
}
