#include "render/Scene3D.hpp"

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

  const lg::Scene3D baseScene = lg::buildPerspectiveScene(
    16.0F / 9.0F,
    arena,
    player,
    opponent,
    inactiveBeam,
    inactiveBeam,
    settings
  );
  failures += expect(
    !baseScene.vertices.empty() && baseScene.vertices.size() % 3 == 0,
    "perspective scene should emit triangle-list geometry"
  );
  failures += expect(
    nearlyEqual(baseScene.camera.position.x, player.position.x) &&
      nearlyEqual(baseScene.camera.position.y, player.position.y) &&
      baseScene.camera.position.z > player.position.z &&
      nearlyEqual(baseScene.camera.forward.x, 1.0F),
    "scene camera should use the local player's first-person view"
  );

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
    settings
  );
  failures += expect(
    translucentBeamScene.vertices.size() == baseScene.vertices.size() &&
      !translucentBeamScene.translucentVertices.empty(),
    "transparent beam geometry should use the non-depth-writing batch"
  );

  return failures == 0 ? 0 : 1;
}
