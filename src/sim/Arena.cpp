#include "sim/Arena.hpp"

#include <algorithm>
#include <cmath>

namespace lg {
namespace {

void resolveWallCollision(
  const ArenaWall& wall,
  const PlayerState& player,
  Vec3 previousPosition,
  CollisionResult& result
) {
  constexpr float kStepHeight = 0.45F;
  constexpr float kCollisionEpsilon = 0.0001F;
  const float minX = wall.min.x - player.bounds.radius;
  const float maxX = wall.max.x + player.bounds.radius;
  const float minY = wall.min.y - player.bounds.radius;
  const float maxY = wall.max.y + player.bounds.radius;
  if (
    result.position.x <= minX ||
    result.position.x >= maxX ||
    result.position.y <= minY ||
    result.position.y >= maxY
  ) {
    return;
  }

  const float previousPlayerMinZ =
    previousPosition.z - player.bounds.halfHeight;
  const float previousPlayerMaxZ =
    previousPosition.z + player.bounds.halfHeight;
  const float playerMinZ = result.position.z - player.bounds.halfHeight;
  const float playerMaxZ = result.position.z + player.bounds.halfHeight;

  if (
    previousPlayerMinZ >= wall.max.z - kCollisionEpsilon &&
    playerMinZ < wall.max.z &&
    result.velocity.z <= 0.0F
  ) {
    result.position.z = wall.max.z + player.bounds.halfHeight;
    result.velocity.z = 0.0F;
    result.onGround = true;
    return;
  }

  if (
    previousPlayerMaxZ <= wall.min.z + kCollisionEpsilon &&
    playerMaxZ > wall.min.z &&
    result.velocity.z > 0.0F
  ) {
    result.position.z = wall.min.z - player.bounds.halfHeight;
    result.velocity.z = 0.0F;
    return;
  }

  const float stepHeight = wall.max.z - previousPlayerMinZ;
  if (
    player.onGround &&
    stepHeight > 0.0F &&
    stepHeight <= kStepHeight
  ) {
    result.position.z = wall.max.z + player.bounds.halfHeight;
    result.velocity.z = std::max(0.0F, result.velocity.z);
    result.onGround = true;
    return;
  }

  if (
    playerMaxZ <= wall.min.z ||
    playerMinZ >= wall.max.z
  ) {
    return;
  }

  if (previousPosition.x <= minX) {
    result.position.x = minX;
    result.velocity.x = 0.0F;
    return;
  }
  if (previousPosition.x >= maxX) {
    result.position.x = maxX;
    result.velocity.x = 0.0F;
    return;
  }
  if (previousPosition.y <= minY) {
    result.position.y = minY;
    result.velocity.y = 0.0F;
    return;
  }
  if (previousPosition.y >= maxY) {
    result.position.y = maxY;
    result.velocity.y = 0.0F;
    return;
  }

  const float leftPenetration = result.position.x - minX;
  const float rightPenetration = maxX - result.position.x;
  const float bottomPenetration = result.position.y - minY;
  const float topPenetration = maxY - result.position.y;
  const float minimumPenetration = std::min({
    leftPenetration,
    rightPenetration,
    bottomPenetration,
    topPenetration,
  });
  if (minimumPenetration == leftPenetration) {
    result.position.x = minX;
    result.velocity.x = std::min(0.0F, result.velocity.x);
  } else if (minimumPenetration == rightPenetration) {
    result.position.x = maxX;
    result.velocity.x = std::max(0.0F, result.velocity.x);
  } else if (minimumPenetration == bottomPenetration) {
    result.position.y = minY;
    result.velocity.y = std::min(0.0F, result.velocity.y);
  } else {
    result.position.y = maxY;
    result.velocity.y = std::max(0.0F, result.velocity.y);
  }
}

} // namespace

Arena thunderstruckArena() {
  Arena arena;
  arena.min = {-15.0F, -11.0F, 0.0F};
  arena.max = {15.0F, 11.0F, 10.0F};

  const auto addBox = [&arena](Vec3 min, Vec3 max) {
    arena.walls[arena.wallCount++] = {min, max};
  };

  // Thunderstruck's lower central court is ringed by raised fighting lanes.
  addBox({-15.0F, 6.5F, 0.0F}, {15.0F, 11.0F, 2.0F});
  addBox({-15.0F, -7.0F, 0.0F}, {-10.0F, 6.5F, 2.0F});
  addBox({10.0F, -7.0F, 0.0F}, {15.0F, 6.5F, 2.0F});
  addBox({-15.0F, -11.0F, 0.0F}, {-3.0F, -7.0F, 2.0F});
  addBox({3.0F, -11.0F, 0.0F}, {15.0F, -7.0F, 2.0F});

  // A raised cross-lane overlooks the court while leaving an underpass.
  addBox({-10.0F, 3.0F, 2.0F}, {10.0F, 4.5F, 2.4F});

  // Opposing five-step stairways connect the lower court to the side lanes.
  for (int step = 0; step < 5; ++step) {
    const float outerX = -6.0F - (static_cast<float>(step) * 0.8F);
    addBox(
      {outerX - 0.8F, -5.0F, 0.0F},
      {outerX, -2.0F, 0.4F * static_cast<float>(step + 1)}
    );
  }
  for (int step = 0; step < 5; ++step) {
    const float innerX = 6.0F + (static_cast<float>(step) * 0.8F);
    addBox(
      {innerX, -5.0F, 0.0F},
      {innerX + 0.8F, -2.0F, 0.4F * static_cast<float>(step + 1)}
    );
  }

  // Low central cover preserves Thunderstruck's exposed tracking lanes.
  addBox({-5.0F, -0.9F, 0.0F}, {-3.0F, 0.9F, 1.2F});
  addBox({3.0F, -0.9F, 0.0F}, {5.0F, 0.9F, 1.2F});
  addBox({-1.2F, -1.2F, 0.0F}, {1.2F, 1.2F, 2.6F});

  arena.spawnPositions = {{
    {-8.0F, -9.0F, 2.0F},
    {8.0F, -9.0F, 2.0F},
    {-12.0F, 8.0F, 2.0F},
    {12.0F, 8.0F, 2.0F},
    {-3.0F, -9.0F, 0.0F},
    {3.0F, -9.0F, 0.0F},
  }};
  return arena;
}

CollisionResult resolvePlayerArenaCollision(
  const Arena& arena,
  const PlayerState& player,
  Vec3 requestedPosition,
  Vec3 requestedVelocity
) {
  CollisionResult result{requestedPosition, requestedVelocity, false};

  const float minX = arena.min.x + player.bounds.radius;
  const float maxX = arena.max.x - player.bounds.radius;
  const float minY = arena.min.y + player.bounds.radius;
  const float maxY = arena.max.y - player.bounds.radius;
  const float minZ = arena.min.z + player.bounds.halfHeight;
  const float maxZ = arena.max.z - player.bounds.halfHeight;

  if (result.position.x < minX) {
    result.position.x = minX;
    result.velocity.x = 0.0F;
  } else if (result.position.x > maxX) {
    result.position.x = maxX;
    result.velocity.x = 0.0F;
  }

  if (result.position.y < minY) {
    result.position.y = minY;
    result.velocity.y = 0.0F;
  } else if (result.position.y > maxY) {
    result.position.y = maxY;
    result.velocity.y = 0.0F;
  }

  if (result.position.z < minZ) {
    result.position.z = minZ;
    result.velocity.z = 0.0F;
    result.onGround = true;
  } else if (result.position.z > maxZ) {
    result.position.z = maxZ;
    result.velocity.z = 0.0F;
  }

  for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t index = 0; index < arena.wallCount; ++index) {
      resolveWallCollision(arena.walls[index], player, player.position, result);
    }
  }

  return result;
}

} // namespace lg
