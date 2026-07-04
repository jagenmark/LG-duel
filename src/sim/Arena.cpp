#include "sim/Arena.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

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

void resolveBrushCollision(
  const ArenaBrush& brush,
  const PlayerState& player,
  Vec3 previousPosition,
  CollisionResult& result
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  constexpr float kWalkableNormalZ = 0.35F;
  const float sweptMinX = std::min(previousPosition.x, result.position.x) -
    player.bounds.radius;
  const float sweptMaxX = std::max(previousPosition.x, result.position.x) +
    player.bounds.radius;
  const float sweptMinY = std::min(previousPosition.y, result.position.y) -
    player.bounds.radius;
  const float sweptMaxY = std::max(previousPosition.y, result.position.y) +
    player.bounds.radius;
  const float sweptMinZ = std::min(previousPosition.z, result.position.z) -
    player.bounds.halfHeight;
  const float sweptMaxZ = std::max(previousPosition.z, result.position.z) +
    player.bounds.halfHeight;
  if (
    sweptMaxX < brush.min.x - kCollisionEpsilon ||
    sweptMinX > brush.max.x + kCollisionEpsilon ||
    sweptMaxY < brush.min.y - kCollisionEpsilon ||
    sweptMinY > brush.max.y + kCollisionEpsilon ||
    sweptMaxZ < brush.min.z - kCollisionEpsilon ||
    sweptMinZ > brush.max.z + kCollisionEpsilon
  ) {
    return;
  }

  const float previousBottom = previousPosition.z - player.bounds.halfHeight;
  const float currentBottom = result.position.z - player.bounds.halfHeight;

  const auto planarRadiusForFace =
    [&](const ArenaBrushFace& face) {
      return player.bounds.radius *
        std::sqrt((face.normal.x * face.normal.x) + (face.normal.y * face.normal.y));
    };

  const auto pointInsideBrushPlanarExpansion =
    [&](Vec3 point, std::uint8_t ignoredFace) {
      for (std::uint8_t index = 0; index < brush.faceCount; ++index) {
        if (index == ignoredFace) {
          continue;
        }
        const ArenaBrushFace& face = brush.faces[index];
        const float expandedDistance = face.distance + planarRadiusForFace(face);
        if (dot(face.normal, point) > expandedDistance + kCollisionEpsilon) {
          return false;
        }
      }
      return true;
    };

  for (std::uint8_t index = 0; index < brush.faceCount; ++index) {
    const ArenaBrushFace& face = brush.faces[index];
    if (face.normal.z <= kWalkableNormalZ) {
      continue;
    }
    const float previousSurfaceZ =
      (face.distance - (face.normal.x * previousPosition.x) - (face.normal.y * previousPosition.y)) /
      face.normal.z;
    const float surfaceZ =
      (face.distance - (face.normal.x * result.position.x) - (face.normal.y * result.position.y)) /
      face.normal.z;
    if (!std::isfinite(previousSurfaceZ) || !std::isfinite(surfaceZ)) {
      continue;
    }
    const bool liftingOffFace =
      result.velocity.z > kCollisionEpsilon &&
      (!player.onGround || player.knockbackTicksRemaining > 0) &&
      pointInsideBrushPlanarExpansion(
        {previousPosition.x, previousPosition.y, previousSurfaceZ},
        index
      ) &&
      previousBottom >= previousSurfaceZ - kCollisionEpsilon &&
      previousBottom <= previousSurfaceZ + 0.05F;
    if (liftingOffFace) {
      return;
    }
    if (!pointInsideBrushPlanarExpansion({result.position.x, result.position.y, surfaceZ}, index)) {
      continue;
    }
    const bool landingOnFace =
      previousBottom >= previousSurfaceZ - kCollisionEpsilon &&
      currentBottom <= surfaceZ + kCollisionEpsilon &&
      result.velocity.z <= 0.0F;
    if (landingOnFace) {
      result.position.z = surfaceZ + player.bounds.halfHeight;
      result.velocity.z = std::max(0.0F, result.velocity.z);
      result.onGround = true;
      return;
    }

    const bool movingAwayFromFace =
      previousBottom >= previousSurfaceZ - kCollisionEpsilon &&
      currentBottom >= surfaceZ - kCollisionEpsilon &&
      result.velocity.z > 0.0F;
    if (movingAwayFromFace) {
      if (player.onGround && player.knockbackTicksRemaining == 0) {
        result.onGround = true;
      }
      return;
    }
  }

  float minimumPenetration = std::numeric_limits<float>::max();
  const ArenaBrushFace* separatingFace = nullptr;

  for (std::uint8_t index = 0; index < brush.faceCount; ++index) {
    const ArenaBrushFace& face = brush.faces[index];
    const float planarRadius = planarRadiusForFace(face);
    const float verticalRadius = player.bounds.halfHeight * std::fabs(face.normal.z);
    const float expandedDistance = face.distance + planarRadius + verticalRadius;
    const float penetration = expandedDistance - dot(face.normal, result.position);
    if (penetration < -kCollisionEpsilon) {
      return;
    }
    if (penetration < minimumPenetration) {
      minimumPenetration = penetration;
      separatingFace = &face;
    }
  }

  if (separatingFace == nullptr || minimumPenetration < -kCollisionEpsilon) {
    return;
  }

  result.position += separatingFace->normal * (minimumPenetration + kCollisionEpsilon);
  const float velocityIntoBrush = dot(result.velocity, separatingFace->normal);
  if (velocityIntoBrush < 0.0F) {
    result.velocity -= separatingFace->normal * velocityIntoBrush;
  }
  result.blocked = true;
  if (separatingFace->normal.z > 0.5F) {
    result.onGround = true;
  }
}

[[nodiscard]] float planarRadiusForFace(
  const ArenaBrushFace& face,
  const PlayerState& player
) {
  return player.bounds.radius *
    std::sqrt((face.normal.x * face.normal.x) + (face.normal.y * face.normal.y));
}

[[nodiscard]] bool playerOverlapsWallSolid(
  const ArenaWall& wall,
  const PlayerState& player,
  Vec3 position
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  const float minX = wall.min.x - player.bounds.radius;
  const float maxX = wall.max.x + player.bounds.radius;
  const float minY = wall.min.y - player.bounds.radius;
  const float maxY = wall.max.y + player.bounds.radius;
  const float playerMinZ = position.z - player.bounds.halfHeight;
  const float playerMaxZ = position.z + player.bounds.halfHeight;
  const float stepHeight = wall.max.z - playerMinZ;
  if (player.onGround && stepHeight > 0.0F && stepHeight <= kPlayerStepHeight) {
    return false;
  }
  return position.x > minX + kCollisionEpsilon &&
    position.x < maxX - kCollisionEpsilon &&
    position.y > minY + kCollisionEpsilon &&
    position.y < maxY - kCollisionEpsilon &&
    playerMinZ < wall.max.z - kCollisionEpsilon &&
    playerMaxZ > wall.min.z + kCollisionEpsilon;
}

[[nodiscard]] bool playerOverlapsBrushSolid(
  const ArenaBrush& brush,
  const PlayerState& player,
  Vec3 position
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  if (
    position.x + player.bounds.radius <= brush.min.x + kCollisionEpsilon ||
    position.x - player.bounds.radius >= brush.max.x - kCollisionEpsilon ||
    position.y + player.bounds.radius <= brush.min.y + kCollisionEpsilon ||
    position.y - player.bounds.radius >= brush.max.y - kCollisionEpsilon ||
    position.z + player.bounds.halfHeight <= brush.min.z + kCollisionEpsilon ||
    position.z - player.bounds.halfHeight >= brush.max.z - kCollisionEpsilon
  ) {
    return false;
  }

  for (std::uint8_t index = 0; index < brush.faceCount; ++index) {
    const ArenaBrushFace& face = brush.faces[index];
    const float planarRadius = planarRadiusForFace(face, player);
    const float verticalRadius = player.bounds.halfHeight * std::fabs(face.normal.z);
    const float expandedDistance = face.distance + planarRadius + verticalRadius;
    if (dot(face.normal, position) >= expandedDistance - kCollisionEpsilon) {
      return false;
    }
  }

  return true;
}

struct PlayerArenaTrace {
  Vec3 endPosition = {};
  Vec3 normal = {};
  float fraction = 1.0F;
  bool hit = false;
  bool startedSolid = false;
};

[[nodiscard]] Vec3 axisNormal(int axis, float value) {
  Vec3 normal = {};
  if (axis == 0) {
    normal.x = value;
  } else if (axis == 1) {
    normal.y = value;
  } else {
    normal.z = value;
  }
  return normal;
}

void keepEarliestTrace(const PlayerArenaTrace& candidate, PlayerArenaTrace& trace) {
  if (candidate.startedSolid) {
    trace.startedSolid = true;
    trace.hit = true;
    trace.fraction = 0.0F;
    trace.endPosition = candidate.endPosition;
    trace.normal = candidate.normal;
    return;
  }
  if (candidate.hit && candidate.fraction < trace.fraction) {
    trace = candidate;
  }
}

[[nodiscard]] PlayerArenaTrace traceExpandedAabb(
  Vec3 min,
  Vec3 max,
  Vec3 start,
  Vec3 end
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  const Vec3 delta = end - start;
  const bool startsInside =
    start.x > min.x + kCollisionEpsilon &&
    start.x < max.x - kCollisionEpsilon &&
    start.y > min.y + kCollisionEpsilon &&
    start.y < max.y - kCollisionEpsilon &&
    start.z > min.z + kCollisionEpsilon &&
    start.z < max.z - kCollisionEpsilon;
  if (startsInside) {
    return {end, {}, 1.0F, false, false};
  }

  float entry = 0.0F;
  float exit = 1.0F;
  Vec3 normal = {};
  const float starts[3] = {start.x, start.y, start.z};
  const float deltas[3] = {delta.x, delta.y, delta.z};
  const float mins[3] = {min.x, min.y, min.z};
  const float maxs[3] = {max.x, max.y, max.z};

  for (int axis = 0; axis < 3; ++axis) {
    if (std::fabs(deltas[axis]) <= kCollisionEpsilon) {
      if (starts[axis] <= mins[axis] || starts[axis] >= maxs[axis]) {
        return {end, {}, 1.0F, false, false};
      }
      continue;
    }

    float nearTime = (mins[axis] - starts[axis]) / deltas[axis];
    float farTime = (maxs[axis] - starts[axis]) / deltas[axis];
    Vec3 nearNormal = axisNormal(axis, -1.0F);
    if (nearTime > farTime) {
      std::swap(nearTime, farTime);
      nearNormal = axisNormal(axis, 1.0F);
    }
    if (
      deltas[axis] > kCollisionEpsilon &&
      std::fabs(starts[axis] - mins[axis]) <= kCollisionEpsilon
    ) {
      nearTime = 0.0F;
      nearNormal = axisNormal(axis, -1.0F);
    } else if (
      deltas[axis] < -kCollisionEpsilon &&
      std::fabs(starts[axis] - maxs[axis]) <= kCollisionEpsilon
    ) {
      nearTime = 0.0F;
      nearNormal = axisNormal(axis, 1.0F);
    }
    if (nearTime >= entry) {
      entry = nearTime;
      normal = nearNormal;
    }
    exit = std::min(exit, farTime);
    if (entry > exit) {
      return {end, {}, 1.0F, false, false};
    }
  }

  if (entry < 0.0F || entry > 1.0F) {
    return {end, {}, 1.0F, false, false};
  }
  if (length(normal) <= kCollisionEpsilon) {
    return {end, {}, 1.0F, false, false};
  }
  return {start + (delta * entry), normal, entry, true, false};
}

[[nodiscard]] PlayerArenaTrace traceArenaBounds(
  const Arena& arena,
  const PlayerState& player,
  Vec3 start,
  Vec3 end
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  const Vec3 delta = end - start;
  const Vec3 min = {
    arena.min.x + player.bounds.radius,
    arena.min.y + player.bounds.radius,
    arena.min.z + player.bounds.halfHeight,
  };
  const Vec3 max = {
    arena.max.x - player.bounds.radius,
    arena.max.y - player.bounds.radius,
    arena.max.z - player.bounds.halfHeight,
  };
  if (
    start.x < min.x - kCollisionEpsilon ||
    start.x > max.x + kCollisionEpsilon ||
    start.y < min.y - kCollisionEpsilon ||
    start.y > max.y + kCollisionEpsilon ||
    start.z < min.z - kCollisionEpsilon ||
    start.z > max.z + kCollisionEpsilon
  ) {
    return {start, {}, 0.0F, true, true};
  }

  PlayerArenaTrace trace{end, {}, 1.0F, false, false};
  const auto constrainAxis =
    [&](float startValue, float endValue, float deltaValue, float minValue, float maxValue, int axis) {
      if (deltaValue > kCollisionEpsilon && endValue > maxValue) {
        const float fraction = (maxValue - startValue) / deltaValue;
        keepEarliestTrace(
          {start + (delta * fraction), axisNormal(axis, -1.0F), fraction, true, false},
          trace
        );
      } else if (deltaValue < -kCollisionEpsilon && endValue < minValue) {
        const float fraction = (minValue - startValue) / deltaValue;
        keepEarliestTrace(
          {start + (delta * fraction), axisNormal(axis, 1.0F), fraction, true, false},
          trace
        );
      }
    };

  constrainAxis(start.x, end.x, delta.x, min.x, max.x, 0);
  constrainAxis(start.y, end.y, delta.y, min.y, max.y, 1);
  constrainAxis(start.z, end.z, delta.z, min.z, max.z, 2);
  return trace;
}

[[nodiscard]] PlayerArenaTrace traceWall(
  const ArenaWall& wall,
  const PlayerState& player,
  Vec3 start,
  Vec3 end
) {
  const Vec3 min = {
    wall.min.x - player.bounds.radius,
    wall.min.y - player.bounds.radius,
    wall.min.z - player.bounds.halfHeight,
  };
  const Vec3 max = {
    wall.max.x + player.bounds.radius,
    wall.max.y + player.bounds.radius,
    wall.max.z + player.bounds.halfHeight,
  };
  PlayerArenaTrace trace = traceExpandedAabb(min, max, start, end);
  const float playerFeetZ = player.onGround
    ? player.position.z - player.bounds.halfHeight
    : start.z - player.bounds.halfHeight;
  const float startFeetZ = start.z - player.bounds.halfHeight;
  const float stepHeight = wall.max.z - playerFeetZ;
  const Vec3 delta = end - start;
  const bool verticalProbe =
    std::fabs(delta.x) <= 0.0001F && std::fabs(delta.y) <= 0.0001F;
  if (
    trace.startedSolid &&
    verticalProbe &&
    (
      start.x < wall.min.x - 0.0001F ||
      start.x > wall.max.x + 0.0001F ||
      start.y < wall.min.y - 0.0001F ||
      start.y > wall.max.y + 0.0001F
    )
  ) {
    return {end, {}, 1.0F, false, false};
  }
  if (
    trace.startedSolid &&
    player.onGround &&
    stepHeight > 0.0F &&
    stepHeight <= kPlayerStepHeight
  ) {
    const bool raisedStepMove =
      startFeetZ >= playerFeetZ + kPlayerStepHeight - 0.0001F;
    if (verticalProbe) {
      if (startFeetZ >= wall.max.z - 0.0001F) {
        return {start, {0.0F, 0.0F, 1.0F}, 0.0F, true, false};
      }
      return {end, {}, 1.0F, false, false};
    }
    if (raisedStepMove) {
      return {end, {}, 1.0F, false, false};
    }
  }
  return trace;
}

[[nodiscard]] PlayerArenaTrace traceBrushWalkableDrop(
  const ArenaBrush& brush,
  const PlayerState& player,
  Vec3 start,
  Vec3 end
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  constexpr float kWalkableNormalZ = 0.35F;
  const Vec3 delta = end - start;
  if (
    std::fabs(delta.x) > kCollisionEpsilon ||
    std::fabs(delta.y) > kCollisionEpsilon ||
    delta.z >= -kCollisionEpsilon
  ) {
    return {end, {}, 1.0F, false, false};
  }

  PlayerArenaTrace trace{end, {}, 1.0F, false, false};
  for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
    const ArenaBrushFace& face = brush.faces[faceIndex];
    if (face.normal.z <= kWalkableNormalZ) {
      continue;
    }

    const float planarRadius = planarRadiusForFace(face, player);
    const float verticalRadius = player.bounds.halfHeight * std::fabs(face.normal.z);
    const float expandedDistance = face.distance + planarRadius + verticalRadius;
    const float startDistance = expandedDistance - dot(face.normal, start);
    const float directionDistance = dot(face.normal, delta);
    if (directionDistance >= -kCollisionEpsilon || startDistance >= 0.0F) {
      continue;
    }

    const float fraction = startDistance / directionDistance;
    if (fraction < 0.0F || fraction > trace.fraction) {
      continue;
    }

    const Vec3 hitPosition = start + (delta * fraction);
    bool insideOtherFaces = true;
    for (std::uint8_t otherIndex = 0; otherIndex < brush.faceCount; ++otherIndex) {
      if (otherIndex == faceIndex) {
        continue;
      }
      const ArenaBrushFace& other = brush.faces[otherIndex];
      const float otherExpandedDistance =
        other.distance +
        planarRadiusForFace(other, player) +
        (player.bounds.halfHeight * std::fabs(other.normal.z));
      if (dot(other.normal, hitPosition) > otherExpandedDistance + kCollisionEpsilon) {
        insideOtherFaces = false;
        break;
      }
    }
    if (!insideOtherFaces) {
      continue;
    }

    trace = {hitPosition, face.normal, fraction, true, false};
  }
  return trace;
}

[[nodiscard]] PlayerArenaTrace traceWallsAndBounds(
  const Arena& arena,
  const PlayerState& player,
  Vec3 start,
  Vec3 end
) {
  PlayerArenaTrace trace{end, {}, 1.0F, false, false};
  keepEarliestTrace(traceArenaBounds(arena, player, start, end), trace);
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    keepEarliestTrace(traceWall(arena.walls[index], player, start, end), trace);
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    keepEarliestTrace(traceBrushWalkableDrop(arena.brushes[index], player, start, end), trace);
  }
  trace.endPosition = trace.hit ? start + ((end - start) * trace.fraction) : end;
  return trace;
}

[[nodiscard]] Vec3 clipVelocity(Vec3 velocity, Vec3 normal) {
  constexpr float kOverclip = 1.001F;
  constexpr float kStopEpsilon = 0.0001F;
  const float backoff = dot(velocity, normal) * kOverclip;
  if (backoff >= 0.0F) {
    return velocity;
  }
  Vec3 clipped = velocity - (normal * backoff);
  if (std::fabs(clipped.x) < kStopEpsilon) {
    clipped.x = 0.0F;
  }
  if (std::fabs(clipped.y) < kStopEpsilon) {
    clipped.y = 0.0F;
  }
  if (std::fabs(clipped.z) < kStopEpsilon) {
    clipped.z = 0.0F;
  }
  return clipped;
}

[[nodiscard]] Vec3 cross(Vec3 lhs, Vec3 rhs) {
  return {
    (lhs.y * rhs.z) - (lhs.z * rhs.y),
    (lhs.z * rhs.x) - (lhs.x * rhs.z),
    (lhs.x * rhs.y) - (lhs.y * rhs.x),
  };
}

[[nodiscard]] bool duplicatePlane(Vec3 lhs, Vec3 rhs) {
  return dot(lhs, rhs) > 0.99F;
}

bool addCollisionPlane(
  Vec3 normal,
  std::array<Vec3, 5>& planes,
  std::size_t& planeCount
) {
  constexpr std::size_t kMaxPlanes = 5;
  if (length(normal) <= 0.0001F) {
    return true;
  }
  for (std::size_t index = 0; index < planeCount; ++index) {
    if (duplicatePlane(normal, planes[index])) {
      return true;
    }
  }
  if (planeCount >= kMaxPlanes) {
    return false;
  }
  planes[planeCount++] = normal;
  return true;
}

[[nodiscard]] bool withinContactRange(float value, float min, float max) {
  constexpr float kCollisionEpsilon = 0.0001F;
  return value >= min - kCollisionEpsilon && value <= max + kCollisionEpsilon;
}

bool addTouchingSolidBoxPlanes(
  Vec3 min,
  Vec3 max,
  Vec3 position,
  Vec3 velocity,
  std::array<Vec3, 5>& planes,
  std::size_t& planeCount
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  const bool insideY =
    withinContactRange(position.y, min.y, max.y);
  const bool insideZ =
    withinContactRange(position.z, min.z, max.z);
  if (insideY && insideZ) {
    if (
      velocity.x > kCollisionEpsilon &&
      std::fabs(position.x - min.x) <= kCollisionEpsilon &&
      !addCollisionPlane({-1.0F, 0.0F, 0.0F}, planes, planeCount)
    ) {
      return false;
    }
    if (
      velocity.x < -kCollisionEpsilon &&
      std::fabs(position.x - max.x) <= kCollisionEpsilon &&
      !addCollisionPlane({1.0F, 0.0F, 0.0F}, planes, planeCount)
    ) {
      return false;
    }
  }

  const bool insideX =
    withinContactRange(position.x, min.x, max.x);
  if (insideX && insideZ) {
    if (
      velocity.y > kCollisionEpsilon &&
      std::fabs(position.y - min.y) <= kCollisionEpsilon &&
      !addCollisionPlane({0.0F, -1.0F, 0.0F}, planes, planeCount)
    ) {
      return false;
    }
    if (
      velocity.y < -kCollisionEpsilon &&
      std::fabs(position.y - max.y) <= kCollisionEpsilon &&
      !addCollisionPlane({0.0F, 1.0F, 0.0F}, planes, planeCount)
    ) {
      return false;
    }
  }

  if (insideX && insideY) {
    if (
      velocity.z > kCollisionEpsilon &&
      std::fabs(position.z - min.z) <= kCollisionEpsilon &&
      !addCollisionPlane({0.0F, 0.0F, -1.0F}, planes, planeCount)
    ) {
      return false;
    }
    if (
      velocity.z < -kCollisionEpsilon &&
      std::fabs(position.z - max.z) <= kCollisionEpsilon &&
      !addCollisionPlane({0.0F, 0.0F, 1.0F}, planes, planeCount)
    ) {
      return false;
    }
  }

  return true;
}

bool addTouchingArenaPlanes(
  const Arena& arena,
  const PlayerState& player,
  Vec3 position,
  Vec3 velocity,
  std::array<Vec3, 5>& planes,
  std::size_t& planeCount
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  const Vec3 boundsMin = {
    arena.min.x + player.bounds.radius,
    arena.min.y + player.bounds.radius,
    arena.min.z + player.bounds.halfHeight,
  };
  const Vec3 boundsMax = {
    arena.max.x - player.bounds.radius,
    arena.max.y - player.bounds.radius,
    arena.max.z - player.bounds.halfHeight,
  };

  if (
    velocity.x < -kCollisionEpsilon &&
    std::fabs(position.x - boundsMin.x) <= kCollisionEpsilon &&
    !addCollisionPlane({1.0F, 0.0F, 0.0F}, planes, planeCount)
  ) {
    return false;
  }
  if (
    velocity.x > kCollisionEpsilon &&
    std::fabs(position.x - boundsMax.x) <= kCollisionEpsilon &&
    !addCollisionPlane({-1.0F, 0.0F, 0.0F}, planes, planeCount)
  ) {
    return false;
  }
  if (
    velocity.y < -kCollisionEpsilon &&
    std::fabs(position.y - boundsMin.y) <= kCollisionEpsilon &&
    !addCollisionPlane({0.0F, 1.0F, 0.0F}, planes, planeCount)
  ) {
    return false;
  }
  if (
    velocity.y > kCollisionEpsilon &&
    std::fabs(position.y - boundsMax.y) <= kCollisionEpsilon &&
    !addCollisionPlane({0.0F, -1.0F, 0.0F}, planes, planeCount)
  ) {
    return false;
  }
  if (
    velocity.z < -kCollisionEpsilon &&
    std::fabs(position.z - boundsMin.z) <= kCollisionEpsilon &&
    !addCollisionPlane({0.0F, 0.0F, 1.0F}, planes, planeCount)
  ) {
    return false;
  }
  if (
    velocity.z > kCollisionEpsilon &&
    std::fabs(position.z - boundsMax.z) <= kCollisionEpsilon &&
    !addCollisionPlane({0.0F, 0.0F, -1.0F}, planes, planeCount)
  ) {
    return false;
  }

  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    const Vec3 min = {
      wall.min.x - player.bounds.radius,
      wall.min.y - player.bounds.radius,
      wall.min.z - player.bounds.halfHeight,
    };
    const Vec3 max = {
      wall.max.x + player.bounds.radius,
      wall.max.y + player.bounds.radius,
      wall.max.z + player.bounds.halfHeight,
    };
    if (!addTouchingSolidBoxPlanes(min, max, position, velocity, planes, planeCount)) {
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool clipVelocityToPlanes(
  Vec3 velocity,
  const std::array<Vec3, 5>& planes,
  std::size_t planeCount,
  Vec3& clippedVelocity
) {
  constexpr float kIntoPlaneEpsilon = 0.1F;

  for (std::size_t firstPlane = 0; firstPlane < planeCount; ++firstPlane) {
    if (dot(velocity, planes[firstPlane]) >= kIntoPlaneEpsilon) {
      continue;
    }

    Vec3 candidate = clipVelocity(velocity, planes[firstPlane]);
    bool candidateOk = true;
    for (std::size_t secondPlane = 0; secondPlane < planeCount; ++secondPlane) {
      if (secondPlane == firstPlane) {
        continue;
      }
      if (dot(candidate, planes[secondPlane]) >= kIntoPlaneEpsilon) {
        continue;
      }

      candidate = clipVelocity(candidate, planes[secondPlane]);
      if (dot(candidate, planes[firstPlane]) >= 0.0F) {
        continue;
      }

      const Vec3 creaseDirection = normalize(cross(planes[firstPlane], planes[secondPlane]));
      if (length(creaseDirection) <= 0.0001F) {
        candidateOk = false;
        break;
      }
      candidate = creaseDirection * dot(creaseDirection, velocity);

      for (std::size_t thirdPlane = 0; thirdPlane < planeCount; ++thirdPlane) {
        if (thirdPlane == firstPlane || thirdPlane == secondPlane) {
          continue;
        }
        if (dot(candidate, planes[thirdPlane]) < kIntoPlaneEpsilon) {
          clippedVelocity = {};
          return false;
        }
      }
    }

    if (candidateOk) {
      clippedVelocity = candidate;
      return true;
    }
  }

  clippedVelocity = velocity;
  return true;
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

CollisionResult slidePlayerArenaMove(
  const Arena& arena,
  const PlayerState& player,
  Vec3 start,
  Vec3 velocity,
  float fixedDt
) {
  constexpr int kMaxBumps = 4;
  constexpr std::size_t kMaxPlanes = 5;
  constexpr float kCollisionEpsilon = 0.0001F;
  constexpr float kMinimumSpeed = 0.0001F;

  CollisionResult result{start, velocity, false};
  if (fixedDt <= 0.0F || length(velocity) <= kMinimumSpeed) {
    result.onGround = player.onGround;
    return result;
  }

  std::array<Vec3, kMaxPlanes> planes = {};
  std::size_t planeCount = 0;
  float timeLeft = fixedDt;
  Vec3 position = start;
  const Vec3 originalVelocity = velocity;

  for (int bump = 0; bump < kMaxBumps; ++bump) {
    const Vec3 target = position + (result.velocity * timeLeft);
    const PlayerArenaTrace trace = traceWallsAndBounds(arena, player, position, target);
    if (trace.startedSolid) {
      result.position = position;
      result.velocity.z = 0.0F;
      result.onGround = false;
      result.blocked = true;
      return result;
    }
    if (trace.fraction > kCollisionEpsilon) {
      position = trace.endPosition;
      result.position = position;
      planeCount = 0;
    }
    if (!trace.hit) {
      result.position = position;
      break;
    }
    result.blocked = true;
    if (trace.normal.z > 0.7F) {
      result.onGround = true;
    }

    const std::size_t previousPlaneCount = planeCount;
    if (!addCollisionPlane(trace.normal, planes, planeCount)) {
      result.velocity = {};
      result.position = position;
      result.blocked = true;
      return result;
    }
    if (planeCount == previousPlaneCount && length(trace.normal) > kCollisionEpsilon) {
      result.velocity += trace.normal * kCollisionEpsilon;
    }
    if (!addTouchingArenaPlanes(arena, player, position, result.velocity, planes, planeCount)) {
      result.velocity = {};
      result.position = position;
      result.blocked = true;
      return result;
    }

    Vec3 newVelocity = {};
    if (!clipVelocityToPlanes(result.velocity, planes, planeCount, newVelocity)) {
      result.velocity = {};
      result.position = position;
      result.blocked = true;
      return result;
    }
    result.velocity = newVelocity;
    if (planeCount > 1 && dot(result.velocity, originalVelocity) <= 0.0F) {
      result.velocity = {};
      result.position = position;
      result.blocked = true;
      return result;
    }
    if (trace.normal.z > 0.7F && result.velocity.z > 0.0F) {
      result.velocity.z = 0.0F;
    } else if (trace.normal.z < -0.7F && result.velocity.z < 0.0F) {
      result.velocity.z = 0.0F;
    }
    if (length(result.velocity) <= kMinimumSpeed) {
      result.velocity = {};
      result.position = position;
      return result;
    }

    timeLeft -= timeLeft * trace.fraction;
    if (timeLeft <= kCollisionEpsilon) {
      result.position = position;
      return result;
    }
  }

  for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t index = 0; index < arena.brushCount; ++index) {
      resolveBrushCollision(arena.brushes[index], player, start, result);
    }
  }
  return result;
}

CollisionResult resolvePlayerArenaCollision(
  const Arena& arena,
  const PlayerState& player,
  Vec3 requestedPosition,
  Vec3 requestedVelocity
) {
  return resolvePlayerArenaCollisionFrom(
    arena,
    player,
    player.position,
    requestedPosition,
    requestedVelocity
  );
}

CollisionResult resolvePlayerArenaCollisionFrom(
  const Arena& arena,
  const PlayerState& player,
  Vec3 previousPosition,
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
      resolveWallCollision(arena.walls[index], player, previousPosition, result);
    }
    for (std::size_t index = 0; index < arena.brushCount; ++index) {
      resolveBrushCollision(arena.brushes[index], player, previousPosition, result);
    }
  }

  return result;
}

bool playerPositionSolid(
  const Arena& arena,
  const PlayerState& player,
  Vec3 position
) {
  if (
    position.x < arena.min.x + player.bounds.radius ||
    position.x > arena.max.x - player.bounds.radius ||
    position.y < arena.min.y + player.bounds.radius ||
    position.y > arena.max.y - player.bounds.radius ||
    position.z < arena.min.z + player.bounds.halfHeight ||
    position.z > arena.max.z - player.bounds.halfHeight
  ) {
    return true;
  }

  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    if (playerOverlapsWallSolid(arena.walls[index], player, position)) {
      return true;
    }
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    if (playerOverlapsBrushSolid(arena.brushes[index], player, position)) {
      return true;
    }
  }

  return false;
}

} // namespace lg
