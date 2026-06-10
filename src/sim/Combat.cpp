#include "sim/Combat.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lg {
namespace {

constexpr float kTraceEpsilon = 0.00001F;

[[nodiscard]] float arenaExitDistance(const Arena& arena, Vec3 origin, Vec3 direction) {
  float exitDistance = std::numeric_limits<float>::max();

  const auto clipAxis = [&exitDistance](float axisOrigin, float axisDirection, float minValue, float maxValue) {
    if (axisDirection > kTraceEpsilon) {
      exitDistance = std::min(exitDistance, (maxValue - axisOrigin) / axisDirection);
    } else if (axisDirection < -kTraceEpsilon) {
      exitDistance = std::min(exitDistance, (minValue - axisOrigin) / axisDirection);
    }
  };

  clipAxis(origin.x, direction.x, arena.min.x, arena.max.x);
  clipAxis(origin.y, direction.y, arena.min.y, arena.max.y);
  clipAxis(origin.z, direction.z, arena.min.z, arena.max.z);
  return std::max(0.0F, exitDistance);
}

[[nodiscard]] float wallHitDistance(
  const ArenaWall& wall,
  Vec3 origin,
  Vec3 direction
) {
  float entry = 0.0F;
  float exit = std::numeric_limits<float>::max();
  const auto clipAxis = [&entry, &exit](
    float axisOrigin,
    float axisDirection,
    float minValue,
    float maxValue
  ) {
    if (std::fabs(axisDirection) <= kTraceEpsilon) {
      return axisOrigin >= minValue && axisOrigin <= maxValue;
    }
    const float first = (minValue - axisOrigin) / axisDirection;
    const float second = (maxValue - axisOrigin) / axisDirection;
    entry = std::max(entry, std::min(first, second));
    exit = std::min(exit, std::max(first, second));
    return entry <= exit;
  };

  if (
    !clipAxis(origin.x, direction.x, wall.min.x, wall.max.x) ||
    !clipAxis(origin.y, direction.y, wall.min.y, wall.max.y) ||
    !clipAxis(origin.z, direction.z, wall.min.z, wall.max.z) ||
    exit < 0.0F
  ) {
    return std::numeric_limits<float>::max();
  }
  return std::max(0.0F, entry);
}

[[nodiscard]] bool intersectPlayerCylinder(
  Vec3 origin,
  Vec3 direction,
  const PlayerState& target,
  float maxDistance,
  float& hitDistance
) {
  const Vec3 relativeOrigin = origin - target.position;
  const float radius = target.bounds.radius;
  const float halfHeight = target.bounds.halfHeight;

  float radialEntry = -std::numeric_limits<float>::infinity();
  float radialExit = std::numeric_limits<float>::infinity();
  const float radialA = (direction.x * direction.x) + (direction.y * direction.y);
  const float radialC =
    (relativeOrigin.x * relativeOrigin.x) + (relativeOrigin.y * relativeOrigin.y) - (radius * radius);

  if (radialA <= kTraceEpsilon) {
    if (radialC > 0.0F) {
      return false;
    }
  } else {
    const float radialB =
      2.0F * ((relativeOrigin.x * direction.x) + (relativeOrigin.y * direction.y));
    const float discriminant = (radialB * radialB) - (4.0F * radialA * radialC);
    if (discriminant < 0.0F) {
      return false;
    }

    const float root = std::sqrt(discriminant);
    radialEntry = (-radialB - root) / (2.0F * radialA);
    radialExit = (-radialB + root) / (2.0F * radialA);
  }

  float heightEntry = -std::numeric_limits<float>::infinity();
  float heightExit = std::numeric_limits<float>::infinity();
  if (std::fabs(direction.z) <= kTraceEpsilon) {
    if (relativeOrigin.z < -halfHeight || relativeOrigin.z > halfHeight) {
      return false;
    }
  } else {
    const float first = (-halfHeight - relativeOrigin.z) / direction.z;
    const float second = (halfHeight - relativeOrigin.z) / direction.z;
    heightEntry = std::min(first, second);
    heightExit = std::max(first, second);
  }

  const float entry = std::max({0.0F, radialEntry, heightEntry});
  const float exit = std::min(radialExit, heightExit);
  if (entry > exit || entry > maxDistance) {
    return false;
  }

  hitDistance = entry;
  return true;
}

} // namespace

LightningGunResult simulateLightningGun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const LightningGunTuning& tuning,
  LightningGunState& state,
  float fixedDt
) {
  LightningGunResult result;
  result.start = attacker.position + Vec3{0.0F, 0.0F, tuning.eyeHeight};

  const Vec3 direction = cameraForward(command.viewYawRadians, command.viewPitchRadians);
  float traceDistance =
    std::min(tuning.range, arenaExitDistance(arena, result.start, direction));
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    traceDistance = std::min(
      traceDistance,
      wallHitDistance(arena.walls[index], result.start, direction)
    );
  }
  result.end = result.start + (direction * traceDistance);
  result.active = command.attack && attacker.health > 0;

  if (!result.active || target.health <= 0) {
    return result;
  }

  float hitDistance = 0.0F;
  if (!intersectPlayerCylinder(result.start, direction, target, traceDistance, hitDistance)) {
    return result;
  }

  result.hit = true;
  result.end = result.start + (direction * hitDistance);

  state.fractionalDamage +=
    static_cast<double>(tuning.damagePerSecond) * static_cast<double>(fixedDt);
  result.damageApplied = static_cast<int>(std::floor(state.fractionalDamage));
  state.fractionalDamage -= static_cast<double>(result.damageApplied);

  result.damageApplied = std::min(result.damageApplied, target.health);
  target.health -= result.damageApplied;
  result.knockbackImpulse = direction * (tuning.knockbackPerSecond * fixedDt);
  return result;
}

} // namespace lg
