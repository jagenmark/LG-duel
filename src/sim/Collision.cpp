#include "sim/Collision.hpp"

#include "shared/Math.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lg {
namespace {

[[nodiscard]] float wallTravelDistance(
  const ArenaWall& wall,
  const PlayerState& player,
  Vec3 direction
) {
  const float playerMinZ = player.position.z - player.bounds.halfHeight;
  const float playerMaxZ = player.position.z + player.bounds.halfHeight;
  if (playerMaxZ <= wall.min.z || playerMinZ >= wall.max.z) {
    return std::numeric_limits<float>::max();
  }

  const float minX = wall.min.x - player.bounds.radius;
  const float maxX = wall.max.x + player.bounds.radius;
  const float minY = wall.min.y - player.bounds.radius;
  const float maxY = wall.max.y + player.bounds.radius;
  float entry = 0.0F;
  float exit = std::numeric_limits<float>::max();

  const auto clipAxis = [&entry, &exit](
    float origin,
    float axisDirection,
    float minValue,
    float maxValue
  ) {
    if (std::fabs(axisDirection) <= 0.00001F) {
      return origin >= minValue && origin <= maxValue;
    }
    const float first = (minValue - origin) / axisDirection;
    const float second = (maxValue - origin) / axisDirection;
    entry = std::max(entry, std::min(first, second));
    exit = std::min(exit, std::max(first, second));
    return entry <= exit;
  };

  if (
    !clipAxis(player.position.x, direction.x, minX, maxX) ||
    !clipAxis(player.position.y, direction.y, minY, maxY) ||
    exit < 0.0F
  ) {
    return std::numeric_limits<float>::max();
  }
  return std::max(0.0F, entry);
}

[[nodiscard]] float brushTravelDistance(
  const ArenaBrush& brush,
  const PlayerState& player,
  Vec3 direction
) {
  const float playerMinZ = player.position.z - player.bounds.halfHeight;
  const float playerMaxZ = player.position.z + player.bounds.halfHeight;
  if (playerMaxZ <= brush.min.z || playerMinZ >= brush.max.z) {
    return std::numeric_limits<float>::max();
  }

  const float minX = brush.min.x - player.bounds.radius;
  const float maxX = brush.max.x + player.bounds.radius;
  const float minY = brush.min.y - player.bounds.radius;
  const float maxY = brush.max.y + player.bounds.radius;
  float boundsEntry = 0.0F;
  float boundsExit = std::numeric_limits<float>::max();

  const auto clipBoundsAxis = [&boundsEntry, &boundsExit](
    float origin,
    float axisDirection,
    float minValue,
    float maxValue
  ) {
    if (std::fabs(axisDirection) <= 0.00001F) {
      return origin >= minValue && origin <= maxValue;
    }
    const float first = (minValue - origin) / axisDirection;
    const float second = (maxValue - origin) / axisDirection;
    boundsEntry = std::max(boundsEntry, std::min(first, second));
    boundsExit = std::min(boundsExit, std::max(first, second));
    return boundsEntry <= boundsExit;
  };

  if (
    !clipBoundsAxis(player.position.x, direction.x, minX, maxX) ||
    !clipBoundsAxis(player.position.y, direction.y, minY, maxY) ||
    boundsExit < 0.0F
  ) {
    return std::numeric_limits<float>::max();
  }

  float entry = 0.0F;
  float exit = std::numeric_limits<float>::max();
  for (std::uint8_t index = 0; index < brush.faceCount; ++index) {
    const ArenaBrushFace& face = brush.faces[index];
    const float planarRadius =
      player.bounds.radius * std::sqrt((face.normal.x * face.normal.x) + (face.normal.y * face.normal.y));
    const float verticalRadius = player.bounds.halfHeight * std::fabs(face.normal.z);
    const float expandedDistance = face.distance + planarRadius + verticalRadius;
    const float originDistance = expandedDistance - dot(face.normal, player.position);
    const float directionDistance = dot(face.normal, direction);
    if (std::fabs(directionDistance) <= 0.00001F) {
      if (originDistance < 0.0F) {
        return std::numeric_limits<float>::max();
      }
      continue;
    }
    const float planeTime = originDistance / directionDistance;
    if (directionDistance < 0.0F) {
      entry = std::max(entry, planeTime);
    } else {
      exit = std::min(exit, planeTime);
    }
    if (entry > exit) {
      return std::numeric_limits<float>::max();
    }
  }
  if (exit < 0.0F) {
    return std::numeric_limits<float>::max();
  }
  return std::max(0.0F, entry);
}

[[nodiscard]] float availablePlanarTravel(
  const Arena& arena,
  const PlayerState& player,
  Vec3 direction
) {
  float available = std::numeric_limits<float>::max();

  const auto constrainAxis = [&available](
    float position,
    float axisDirection,
    float minPosition,
    float maxPosition
  ) {
    if (axisDirection > 0.00001F) {
      available = std::min(available, (maxPosition - position) / axisDirection);
    } else if (axisDirection < -0.00001F) {
      available = std::min(available, (minPosition - position) / axisDirection);
    }
  };

  constrainAxis(
    player.position.x,
    direction.x,
    arena.min.x + player.bounds.radius,
    arena.max.x - player.bounds.radius
  );
  constrainAxis(
    player.position.y,
    direction.y,
    arena.min.y + player.bounds.radius,
    arena.max.y - player.bounds.radius
  );
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    available = std::min(
      available,
      wallTravelDistance(arena.walls[index], player, direction)
    );
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    available = std::min(
      available,
      brushTravelDistance(arena.brushes[index], player, direction)
    );
  }
  return std::max(0.0F, available);
}

} // namespace

bool resolvePlayerCollision(const Arena& arena, PlayerState& first, PlayerState& second) {
  const float verticalDistance = std::fabs(first.position.z - second.position.z);
  if (verticalDistance >= first.bounds.halfHeight + second.bounds.halfHeight) {
    return false;
  }

  const Vec3 planarOffset = {
    second.position.x - first.position.x,
    second.position.y - first.position.y,
    0.0F,
  };
  const float distance = length(planarOffset);
  const float minimumDistance = first.bounds.radius + second.bounds.radius;
  if (distance >= minimumDistance) {
    return false;
  }

  const Vec3 normal = distance > 0.00001F ? planarOffset / distance : Vec3{1.0F, 0.0F, 0.0F};
  const float penetration = minimumDistance - distance;
  const float firstAvailable = availablePlanarTravel(arena, first, normal * -1.0F);
  const float secondAvailable = availablePlanarTravel(arena, second, normal);
  float firstCorrection = std::min(penetration * 0.5F, firstAvailable);
  float secondCorrection = std::min(penetration - firstCorrection, secondAvailable);
  firstCorrection = std::min(penetration - secondCorrection, firstAvailable);

  first.position -= normal * firstCorrection;
  second.position += normal * secondCorrection;

  const float inwardRelativeSpeed = dot(second.velocity - first.velocity, normal);
  if (inwardRelativeSpeed < 0.0F) {
    const Vec3 velocityCorrection = normal * (inwardRelativeSpeed * 0.5F);
    first.velocity += velocityCorrection;
    second.velocity -= velocityCorrection;
  }

  return true;
}

} // namespace lg
