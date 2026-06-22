#include "render/Scene3D.hpp"

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

} // namespace

int main() {
  int failures = 0;
  lg::Arena arena;
  arena.wallCount = 0;
  lg::PlayerState player;
  player.position = {1.0F, 2.0F, 0.9F};
  player.viewYawRadians = 0.0F;
  player.viewPitchRadians = 0.0F;
  lg::PlayerState opponent;
  opponent.position = {4.0F, 2.0F, 0.9F};
  lg::RenderSettings settings;
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

  std::array<lg::RemotePlayerView, lg::kDuelPlayerCount> remotePlayers = {};
  remotePlayers[1] = lg::RemotePlayerView{opponent, inactiveBeam, true};
  lg::PlayerState secondOpponent = opponent;
  secondOpponent.position.y += 3.0F;
  remotePlayers[2] = lg::RemotePlayerView{secondOpponent, inactiveBeam, true};
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
