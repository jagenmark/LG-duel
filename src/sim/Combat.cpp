#include "sim/Combat.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lg {
namespace {

constexpr float kTraceEpsilon = 0.00001F;
constexpr float kTwoPi = 6.28318530718F;
constexpr float kHeadHitboxBottomRatio = 0.76F;
constexpr float kHeadHitboxTopRatio = 1.0F;
constexpr float kHeadHitboxRadiusScale = 0.96F;
constexpr int kHeadshotDamageMultiplier = 2;

[[nodiscard]] constexpr Vec3 cross(Vec3 lhs, Vec3 rhs) {
  return {
    (lhs.y * rhs.z) - (lhs.z * rhs.y),
    (lhs.z * rhs.x) - (lhs.x * rhs.z),
    (lhs.x * rhs.y) - (lhs.y * rhs.x),
  };
}

[[nodiscard]] Vec3 pelletDirection(
  Vec3 forward,
  Vec3 right,
  Vec3 up,
  float spreadRadians,
  std::uint8_t pelletIndex
) {
  if (pelletIndex == 0 || spreadRadians <= 0.0F) {
    return forward;
  }

  constexpr float kGoldenAngle = 2.39996323F;
  const float normalizedRadius =
    std::sqrt(static_cast<float>(pelletIndex) / static_cast<float>(kShotgunPelletCount - 1U));
  const float angle = static_cast<float>(pelletIndex) * kGoldenAngle;
  const float spread = std::tan(spreadRadians) * normalizedRadius;
  return normalize(
    forward +
    (right * (std::cos(angle) * spread)) +
    (up * (std::sin(angle) * spread))
  );
}

[[nodiscard]] std::uint32_t mixU32(std::uint32_t value) {
  value ^= value >> 16U;
  value *= 0x7feb352dU;
  value ^= value >> 15U;
  value *= 0x846ca68bU;
  value ^= value >> 16U;
  return value;
}

[[nodiscard]] float unitFloat(std::uint32_t value) {
  return static_cast<float>(value & 0x00ffffffU) /
    static_cast<float>(0x00ffffffU);
}

[[nodiscard]] Vec3 spreadDirection(
  Vec3 forward,
  float spreadRadians,
  std::uint32_t seed
) {
  if (spreadRadians <= 0.0F) {
    return forward;
  }

  Vec3 right = normalize(cross(forward, Vec3{0.0F, 0.0F, 1.0F}));
  if (length(right) <= kTraceEpsilon) {
    right = Vec3{1.0F, 0.0F, 0.0F};
  }
  const Vec3 up = normalize(cross(right, forward));
  const float radius = std::sqrt(unitFloat(mixU32(seed)));
  const float angle = unitFloat(mixU32(seed ^ 0x9e3779b9U)) * kTwoPi;
  const float spread = std::tan(spreadRadians) * radius;
  return normalize(
    forward +
    (right * (std::cos(angle) * spread)) +
    (up * (std::sin(angle) * spread))
  );
}

struct TraceHit {
  float distance = std::numeric_limits<float>::max();
  Vec3 normal = {};
  bool hit = false;
};

struct HeadHitbox {
  Vec3 center = {};
  Vec3 halfExtents = {};
};

[[nodiscard]] TraceHit arenaExitHit(const Arena& arena, Vec3 origin, Vec3 direction) {
  float exitDistance = std::numeric_limits<float>::max();
  Vec3 exitNormal = {};

  const auto clipAxis = [&exitDistance, &exitNormal](
    float axisOrigin,
    float axisDirection,
    float minValue,
    float maxValue,
    Vec3 minNormal,
    Vec3 maxNormal
  ) {
    if (axisDirection > kTraceEpsilon) {
      const float distance = (maxValue - axisOrigin) / axisDirection;
      if (distance < exitDistance) {
        exitDistance = distance;
        exitNormal = maxNormal;
      }
    } else if (axisDirection < -kTraceEpsilon) {
      const float distance = (minValue - axisOrigin) / axisDirection;
      if (distance < exitDistance) {
        exitDistance = distance;
        exitNormal = minNormal;
      }
    }
  };

  clipAxis(origin.x, direction.x, arena.min.x, arena.max.x, {1.0F, 0.0F, 0.0F}, {-1.0F, 0.0F, 0.0F});
  clipAxis(origin.y, direction.y, arena.min.y, arena.max.y, {0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F});
  clipAxis(origin.z, direction.z, arena.min.z, arena.max.z, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, -1.0F});
  return {std::max(0.0F, exitDistance), exitNormal, exitDistance < std::numeric_limits<float>::max()};
}

[[nodiscard]] TraceHit wallHit(
  const ArenaWall& wall,
  Vec3 origin,
  Vec3 direction
) {
  float entry = 0.0F;
  float exit = std::numeric_limits<float>::max();
  Vec3 normal = {};
  const auto clipAxis = [&entry, &exit, &normal](
    float axisOrigin,
    float axisDirection,
    float minValue,
    float maxValue,
    Vec3 minNormal,
    Vec3 maxNormal
  ) {
    if (std::fabs(axisDirection) <= kTraceEpsilon) {
      return axisOrigin >= minValue && axisOrigin <= maxValue;
    }
    const float first = (minValue - axisOrigin) / axisDirection;
    const float second = (maxValue - axisOrigin) / axisDirection;
    const float candidateEntry = std::min(first, second);
    if (candidateEntry > entry) {
      entry = candidateEntry;
      normal = first < second ? minNormal : maxNormal;
    }
    exit = std::min(exit, std::max(first, second));
    return entry <= exit;
  };

  if (
    !clipAxis(origin.x, direction.x, wall.min.x, wall.max.x, {-1.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}) ||
    !clipAxis(origin.y, direction.y, wall.min.y, wall.max.y, {0.0F, -1.0F, 0.0F}, {0.0F, 1.0F, 0.0F}) ||
    !clipAxis(origin.z, direction.z, wall.min.z, wall.max.z, {0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, 1.0F}) ||
    exit < 0.0F
  ) {
    return {};
  }
  return {std::max(0.0F, entry), normal, true};
}

[[nodiscard]] TraceHit brushHit(
  const ArenaBrush& brush,
  Vec3 origin,
  Vec3 direction
) {
  float entry = 0.0F;
  float exit = std::numeric_limits<float>::max();
  Vec3 normal = {};
  for (std::uint8_t index = 0; index < brush.faceCount; ++index) {
    const ArenaBrushFace& face = brush.faces[index];
    const float numerator = face.distance - dot(face.normal, origin);
    const float denominator = dot(face.normal, direction);
    if (std::fabs(denominator) <= kTraceEpsilon) {
      if (numerator < 0.0F) {
        return {};
      }
      continue;
    }
    const float planeDistance = numerator / denominator;
    if (denominator < 0.0F) {
      if (planeDistance > entry) {
        entry = planeDistance;
        normal = face.normal;
      }
    } else {
      exit = std::min(exit, planeDistance);
    }
    if (entry > exit) {
      return {};
    }
  }
  if (exit < 0.0F) {
    return {};
  }
  return {std::max(0.0F, entry), normal, true};
}

[[nodiscard]] float brushBoundsHitDistance(
  const ArenaBrush& brush,
  Vec3 origin,
  Vec3 direction
) {
  return wallHit(ArenaWall{brush.min, brush.max}, origin, direction).distance;
}

[[nodiscard]] HeadHitbox playerHeadHitbox(const PlayerState& target) {
  const float halfHeight = std::max(kTraceEpsilon, target.bounds.halfHeight);
  const float radius = std::max(kTraceEpsilon, target.bounds.radius);
  const float centerRatio =
    (kHeadHitboxBottomRatio + kHeadHitboxTopRatio) * 0.5F;
  const float centerOffsetZ = -halfHeight + (2.0F * halfHeight * centerRatio);
  return {
    target.position + Vec3{0.0F, 0.0F, centerOffsetZ},
    {
      radius * kHeadHitboxRadiusScale,
      radius * kHeadHitboxRadiusScale,
      halfHeight * (kHeadHitboxTopRatio - kHeadHitboxBottomRatio),
    },
  };
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

[[nodiscard]] bool intersectPlayerRelativeAabb(
  Vec3 origin,
  Vec3 direction,
  const PlayerState& target,
  float maxDistance,
  Vec3 halfExtents,
  float& hitDistance
) {
  if (
    halfExtents.x <= 0.0F ||
    halfExtents.y <= 0.0F ||
    halfExtents.z <= 0.0F
  ) {
    return false;
  }

  const Vec3 relativeOrigin = origin - target.position;
  float entry = 0.0F;
  float exit = maxDistance;
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
    !clipAxis(relativeOrigin.x, direction.x, -halfExtents.x, halfExtents.x) ||
    !clipAxis(relativeOrigin.y, direction.y, -halfExtents.y, halfExtents.y) ||
    !clipAxis(relativeOrigin.z, direction.z, -halfExtents.z, halfExtents.z)
  ) {
    return false;
  }

  if (entry > maxDistance || exit < 0.0F) {
    return false;
  }

  hitDistance = std::max(0.0F, entry);
  return true;
}

[[nodiscard]] bool intersectPlayerHeadHitbox(
  Vec3 origin,
  Vec3 direction,
  const PlayerState& target,
  float maxDistance,
  float& hitDistance
) {
  const HeadHitbox head = playerHeadHitbox(target);
  PlayerState headTarget = target;
  headTarget.position = head.center;
  return intersectPlayerRelativeAabb(
    origin,
    direction,
    headTarget,
    maxDistance,
    head.halfExtents,
    hitDistance
  );
}

[[nodiscard]] int applyHeadshotDamage(int damage, bool headshot) {
  return headshot ? damage * kHeadshotDamageMultiplier : damage;
}

} // namespace

Vec3 weaponMuzzlePosition(const PlayerState& attacker, float eyeHeight) {
  constexpr CollisionBounds kDefaultPlayerBounds = {};
  const float heightScale =
    attacker.bounds.halfHeight / kDefaultPlayerBounds.halfHeight;
  return attacker.position + Vec3{0.0F, 0.0F, eyeHeight * heightScale};
}

WorldTrace traceWorld(
  const Arena& arena,
  Vec3 origin,
  Vec3 direction,
  float maxDistance
) {
  WorldTrace trace;
  trace.start = origin;
  trace.distance = maxDistance;
  const TraceHit arenaExit = arenaExitHit(arena, origin, direction);
  if (arenaExit.hit && arenaExit.distance <= trace.distance) {
    trace.distance = arenaExit.distance;
    trace.normal = arenaExit.normal;
    trace.hit = true;
  }
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const TraceHit hit = wallHit(arena.walls[index], origin, direction);
    if (hit.hit && hit.distance <= trace.distance) {
      trace.distance = hit.distance;
      trace.normal = hit.normal;
      trace.hit = true;
    }
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    const float boundsDistance =
      brushBoundsHitDistance(arena.brushes[index], origin, direction);
    if (boundsDistance > trace.distance + kTraceEpsilon) {
      continue;
    }
    const TraceHit hit = brushHit(arena.brushes[index], origin, direction);
    if (hit.hit && hit.distance <= trace.distance) {
      trace.distance = hit.distance;
      trace.normal = hit.normal;
      trace.hit = true;
    }
  }
  trace.end = origin + (direction * trace.distance);
  return trace;
}

Vec3 shotgunPelletDirection(
  Vec3 forward,
  Vec3 right,
  Vec3 up,
  float spreadRadians,
  std::uint8_t pelletIndex
) {
  return pelletDirection(forward, right, up, spreadRadians, pelletIndex);
}

bool tracePlayerCylinder(
  Vec3 origin,
  Vec3 direction,
  const PlayerState& target,
  float maxDistance,
  float& hitDistance
) {
  return intersectPlayerCylinder(origin, direction, target, maxDistance, hitDistance);
}

bool tracePlayerHeadHitbox(
  Vec3 origin,
  Vec3 direction,
  const PlayerState& target,
  float maxDistance,
  float& hitDistance
) {
  return intersectPlayerHeadHitbox(
    origin,
    direction,
    target,
    maxDistance,
    hitDistance
  );
}

bool tracePlayerProjectileDirectAabb(
  Vec3 origin,
  Vec3 direction,
  const PlayerState& target,
  float maxDistance,
  Vec3 halfExtents,
  float& hitDistance
) {
  return intersectPlayerRelativeAabb(
    origin,
    direction,
    target,
    maxDistance,
    halfExtents,
    hitDistance
  );
}

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
  result.start = weaponMuzzlePosition(attacker, tuning.eyeHeight);

  const Vec3 direction = cameraForward(command.viewYawRadians, command.viewPitchRadians);
  const WorldTrace worldTrace = traceWorld(arena, result.start, direction, tuning.range);
  const float traceDistance = worldTrace.distance;
  result.end = worldTrace.end;
  result.active = command.attack && attacker.health > 0;

  if (!result.active || target.health <= 0) {
    state.shotCredit = 1.0;
    return result;
  }

  float hitDistance = 0.0F;
  if (!intersectPlayerCylinder(result.start, direction, target, traceDistance, hitDistance)) {
    state.shotCredit = std::min(
      1.0,
      state.shotCredit +
        static_cast<double>(std::max(0.0F, tuning.fireHz)) *
          static_cast<double>(fixedDt)
    );
    return result;
  }

  result.hit = true;
  result.end = result.start + (direction * hitDistance);
  float headHitDistance = 0.0F;
  result.headshot = intersectPlayerHeadHitbox(
    result.start,
    direction,
    target,
    traceDistance,
    headHitDistance
  );

  const float fireHz = std::max(1.0F, tuning.fireHz);
  state.shotCredit = std::min(
    state.shotCredit,
    static_cast<double>(fireHz)
  );
  const int shotsApplied = static_cast<int>(std::floor(state.shotCredit));
  if (shotsApplied <= 0) {
    state.shotCredit +=
      static_cast<double>(fireHz) * static_cast<double>(fixedDt);
    return result;
  }
  state.shotCredit -= static_cast<double>(shotsApplied);
  state.shotCredit +=
    static_cast<double>(fireHz) * static_cast<double>(fixedDt);
  state.fractionalDamage +=
    static_cast<double>(shotsApplied) *
    static_cast<double>(tuning.damagePerSecond) /
    static_cast<double>(fireHz);
  result.damageApplied = static_cast<int>(std::floor(state.fractionalDamage));
  state.fractionalDamage -= static_cast<double>(result.damageApplied);

  result.damageApplied =
    applyHeadshotDamage(result.damageApplied, result.headshot);
  result.damageApplied = std::min(result.damageApplied, target.health);
  target.health -= result.damageApplied;
  result.knockbackImpulse =
    direction *
    (
      tuning.knockbackPerSecond *
      (static_cast<float>(shotsApplied) / fireHz)
    );
  return result;
}

LightningGunResult simulateFreezeGun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const FreezeGunTuning& tuning,
  LightningGunState& state,
  float fixedDt
) {
  LightningGunResult result;
  result.start = weaponMuzzlePosition(attacker, tuning.eyeHeight);

  const Vec3 direction = cameraForward(command.viewYawRadians, command.viewPitchRadians);
  const WorldTrace worldTrace = traceWorld(arena, result.start, direction, tuning.range);
  const float traceDistance = worldTrace.distance;
  result.end = worldTrace.end;
  result.active = command.attack && attacker.health > 0;

  if (!result.active || target.health <= 0) {
    state.shotCredit = 1.0;
    return result;
  }

  float hitDistance = 0.0F;
  if (!intersectPlayerCylinder(result.start, direction, target, traceDistance, hitDistance)) {
    state.shotCredit = std::min(
      1.0,
      state.shotCredit +
        static_cast<double>(std::max(0.0F, tuning.fireHz)) *
          static_cast<double>(fixedDt)
    );
    return result;
  }

  result.hit = true;
  result.end = result.start + (direction * hitDistance);
  float headHitDistance = 0.0F;
  result.headshot = intersectPlayerHeadHitbox(
    result.start,
    direction,
    target,
    traceDistance,
    headHitDistance
  );

  const float fireHz = std::max(1.0F, tuning.fireHz);
  state.shotCredit = std::min(
    state.shotCredit,
    static_cast<double>(fireHz)
  );
  const int shotsApplied = static_cast<int>(std::floor(state.shotCredit));
  if (shotsApplied <= 0) {
    state.shotCredit +=
      static_cast<double>(fireHz) * static_cast<double>(fixedDt);
    return result;
  }
  state.shotCredit -= static_cast<double>(shotsApplied);
  state.shotCredit +=
    static_cast<double>(fireHz) * static_cast<double>(fixedDt);

  state.fractionalDamage +=
    static_cast<double>(shotsApplied) *
    static_cast<double>(tuning.damagePerSecond) /
    static_cast<double>(fireHz);
  result.damageApplied = static_cast<int>(std::floor(state.fractionalDamage));
  state.fractionalDamage -= static_cast<double>(result.damageApplied);
  result.damageApplied =
    applyHeadshotDamage(result.damageApplied, result.headshot);
  result.damageApplied = std::min(result.damageApplied, target.health);
  target.health -= result.damageApplied;

  result.freezeApplied =
    tuning.freezePerSecond * (static_cast<float>(shotsApplied) / fireHz);
  // Freeze is owned by the target. Separate beams call this independently, so
  // stacked attackers build the same target meter faster without shared state.
  target.freezeLevel = std::clamp(
    target.freezeLevel + result.freezeApplied,
    0.0F,
    std::max(0.0F, tuning.maxLevel)
  );
  return result;
}

void decayPlayerFreezeLevel(
  PlayerState& player,
  const FreezeGunTuning& tuning,
  float fixedDt
) {
  if (player.health <= 0) {
    player.freezeLevel = 0.0F;
    return;
  }
  player.freezeLevel = std::max(
    0.0F,
    player.freezeLevel - std::max(0.0F, tuning.decayPerSecond) * fixedDt
  );
}

float freezeMovementScale(const PlayerState& player, const FreezeGunTuning& tuning) {
  const float maxLevel = std::max(0.0001F, tuning.maxLevel);
  const float freezeFraction =
    std::clamp(player.freezeLevel / maxLevel, 0.0F, 1.0F);
  const float slowFraction =
    freezeFraction * std::clamp(tuning.maxSlowFraction, 0.0F, 0.95F);
  return std::clamp(1.0F - slowFraction, 0.05F, 1.0F);
}

WeaponFireResult simulateRailgun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const HitscanTuning& tuning,
  Weapon weapon
) {
  WeaponFireResult result;
  result.weapon = weapon;
  result.visualSeed = command.sequence;
  result.start = weaponMuzzlePosition(attacker, tuning.eyeHeight);
  const Vec3 direction = cameraForward(command.viewYawRadians, command.viewPitchRadians);
  const WorldTrace worldTrace = traceWorld(arena, result.start, direction, tuning.range);
  result.end = worldTrace.end;
  result.fired = command.attack && attacker.health > 0;
  if (!result.fired || target.health <= 0) {
    return result;
  }

  float hitDistance = 0.0F;
  if (!intersectPlayerCylinder(result.start, direction, target, worldTrace.distance, hitDistance)) {
    return result;
  }

  result.hit = true;
  result.end = result.start + (direction * hitDistance);
  float headHitDistance = 0.0F;
  result.headshot = intersectPlayerHeadHitbox(
    result.start,
    direction,
    target,
    worldTrace.distance,
    headHitDistance
  );
  result.damageApplied =
    std::min(applyHeadshotDamage(tuning.damage, result.headshot), target.health);
  target.health -= result.damageApplied;
  result.knockbackImpulse = direction * tuning.knockback;
  return result;
}

WeaponFireResult simulateMachineGun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const MachineGunTuning& tuning
) {
  WeaponFireResult result;
  result.weapon = Weapon::MachineGun;
  result.visualSeed = command.sequence;
  result.start = weaponMuzzlePosition(attacker, tuning.eyeHeight);
  const Vec3 forward = cameraForward(command.viewYawRadians, command.viewPitchRadians);
  const Vec3 direction = spreadDirection(forward, tuning.spreadRadians, command.sequence);
  const WorldTrace worldTrace = traceWorld(arena, result.start, direction, tuning.range);
  result.end = worldTrace.end;
  result.fired = command.attack && attacker.health > 0;
  if (!result.fired || target.health <= 0) {
    return result;
  }

  float hitDistance = 0.0F;
  if (!intersectPlayerCylinder(result.start, direction, target, worldTrace.distance, hitDistance)) {
    return result;
  }

  result.hit = true;
  result.end = result.start + (direction * hitDistance);
  float headHitDistance = 0.0F;
  result.headshot = intersectPlayerHeadHitbox(
    result.start,
    direction,
    target,
    worldTrace.distance,
    headHitDistance
  );
  result.damageApplied =
    std::min(applyHeadshotDamage(tuning.damage, result.headshot), target.health);
  target.health -= result.damageApplied;
  result.knockbackImpulse = direction * tuning.knockback;
  return result;
}

WeaponFireResult simulateShotgun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const ShotgunTuning& tuning
) {
  WeaponFireResult result;
  result.weapon = Weapon::Shotgun;
  result.visualSeed = command.sequence;
  result.pelletCount = tuning.pelletCount;
  result.start = weaponMuzzlePosition(attacker, tuning.eyeHeight);

  const Vec3 forward = cameraForward(command.viewYawRadians, command.viewPitchRadians);
  Vec3 right = normalize(cross(forward, Vec3{0.0F, 0.0F, 1.0F}));
  if (length(right) <= kTraceEpsilon) {
    right = Vec3{1.0F, 0.0F, 0.0F};
  }
  const Vec3 up = normalize(cross(right, forward));
  const WorldTrace centerTrace = traceWorld(arena, result.start, forward, tuning.range);
  result.end = centerTrace.end;
  result.fired = command.attack && attacker.health > 0;
  if (!result.fired || target.health <= 0) {
    return result;
  }

  Vec3 accumulatedKnockbackDirection = {};
  float nearestHitDistance = centerTrace.distance;
  int totalDamage = 0;
  float centerHeadHitDistance = 0.0F;
  const bool centerHeadshot = intersectPlayerHeadHitbox(
    result.start,
    forward,
    target,
    centerTrace.distance,
    centerHeadHitDistance
  );
  for (std::uint8_t pelletIndex = 0; pelletIndex < tuning.pelletCount; ++pelletIndex) {
    const Vec3 direction = pelletDirection(
      forward,
      right,
      up,
      tuning.spreadRadians,
      pelletIndex
    );
    const WorldTrace pelletTrace = traceWorld(arena, result.start, direction, tuning.range);
    float hitDistance = 0.0F;
    if (!intersectPlayerCylinder(
      result.start,
      direction,
      target,
      pelletTrace.distance,
      hitDistance
    )) {
      continue;
    }

    ++result.pelletHitCount;
    float headHitDistance = 0.0F;
    const bool pelletHeadshot =
      centerHeadshot &&
      intersectPlayerHeadHitbox(
        result.start,
        direction,
        target,
        pelletTrace.distance,
        headHitDistance
      );
    if (pelletHeadshot) {
      ++result.pelletHeadshotCount;
    }
    totalDamage += applyHeadshotDamage(
      tuning.damagePerPellet,
      pelletHeadshot
    );
    nearestHitDistance = std::min(nearestHitDistance, hitDistance);
    accumulatedKnockbackDirection += direction;
  }

  if (result.pelletHitCount == 0) {
    return result;
  }

  result.hit = true;
  result.headshot = result.pelletHeadshotCount > 0;
  result.end = result.start + (forward * nearestHitDistance);
  result.damageApplied = std::min(totalDamage, target.health);
  target.health -= result.damageApplied;
  const float hitFraction =
    static_cast<float>(result.pelletHitCount) /
    static_cast<float>(std::max<std::uint8_t>(1, tuning.pelletCount));
  result.knockbackImpulse =
    normalize(accumulatedKnockbackDirection) * tuning.knockback * hitFraction;
  return result;
}

} // namespace lg
