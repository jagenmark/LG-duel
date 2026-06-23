#include "sim/Arena.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace lg {
namespace {

constexpr std::string_view kThunderstruckMapText = R"(version 1
bounds min=-15,-11,0 max=15,11,10

# Thunderstruck's lower central court is ringed by raised fighting lanes.
box lane_north -15,6.5,0 15,11,2
box lane_west -15,-7,0 -10,6.5,2
box lane_east 10,-7,0 15,6.5,2
box spawn_deck_west -15,-11,0 -3,-7,2
box spawn_deck_east 3,-11,0 15,-7,2

# A raised cross-lane overlooks the court while leaving an underpass.
box bridge -10,3,2 10,4.5,2.4

# Opposing five-step stairways connect the lower court to the side lanes.
box stair_west_1 -6.8,-5,0 -6,-2,0.4
box stair_west_2 -7.6,-5,0 -6.8,-2,0.8
box stair_west_3 -8.4,-5,0 -7.6,-2,1.2
box stair_west_4 -9.2,-5,0 -8.4,-2,1.6
box stair_west_5 -10,-5,0 -9.2,-2,2
box stair_east_1 6,-5,0 6.8,-2,0.4
box stair_east_2 6.8,-5,0 7.6,-2,0.8
box stair_east_3 7.6,-5,0 8.4,-2,1.2
box stair_east_4 8.4,-5,0 9.2,-2,1.6
box stair_east_5 9.2,-5,0 10,-2,2

# Low central cover preserves Thunderstruck's exposed tracking lanes.
box cover_left -5,-0.9,0 -3,0.9,1.2
box cover_right 3,-0.9,0 5,0.9,1.2
box center_pillar -1.2,-1.2,0 1.2,1.2,2.6

spawn player_1 -8,-9,2 yaw=0
spawn player_2 8,-9,2 yaw=180
spawn player_3 -12,8,2 yaw=0
spawn player_4 12,8,2 yaw=180
spawn player_5 -3,-9,0 yaw=0
spawn player_6 3,-9,0 yaw=180
)";

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
  if (const char* path = std::getenv("LG_DUEL_MAP"); path != nullptr && path[0] != '\0') {
    const ArenaLoadResult fileResult = loadArenaFromFile(path);
    if (fileResult.ok) {
      return fileResult.arena;
    }
  }

  const ArenaLoadResult embeddedResult = loadArenaFromText(kThunderstruckMapText);
  return embeddedResult.ok ? embeddedResult.arena : Arena{};
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
