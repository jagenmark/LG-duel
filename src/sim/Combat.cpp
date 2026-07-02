#include "sim/Combat.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>

namespace lg {
namespace {

constexpr float kTraceEpsilon = 0.00001F;
constexpr float kTwoPi = 6.28318530718F;

struct TraceWorldDiagnosticsCounters {
  std::atomic<std::uint64_t> calls = 0;
  std::atomic<std::uint64_t> wallChecks = 0;
  std::atomic<std::uint64_t> brushCandidates = 0;
  std::atomic<std::uint64_t> brushExactTests = 0;
  std::atomic<std::uint64_t> brushFaceChecks = 0;
  std::atomic<std::uint64_t> brushBoxSkips = 0;
  std::atomic<std::uint64_t> totalNanoseconds = 0;
};

std::atomic_bool traceWorldDiagnosticsEnabled = false;
TraceWorldDiagnosticsCounters traceWorldDiagnosticsCounters;

void addTraceWorldCount(
  std::atomic<std::uint64_t>& counter,
  std::uint64_t amount
) {
  counter.fetch_add(amount, std::memory_order_relaxed);
}

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

[[nodiscard]] float brushHitDistance(
  const ArenaBrush& brush,
  Vec3 origin,
  Vec3 direction,
  bool diagnosticsEnabled
) {
  if (diagnosticsEnabled) {
    addTraceWorldCount(traceWorldDiagnosticsCounters.brushExactTests, 1U);
  }
  float entry = 0.0F;
  float exit = std::numeric_limits<float>::max();
  for (std::uint8_t index = 0; index < brush.faceCount; ++index) {
    if (diagnosticsEnabled) {
      addTraceWorldCount(traceWorldDiagnosticsCounters.brushFaceChecks, 1U);
    }
    const ArenaBrushFace& face = brush.faces[index];
    const float numerator = face.distance - dot(face.normal, origin);
    const float denominator = dot(face.normal, direction);
    if (std::fabs(denominator) <= kTraceEpsilon) {
      if (numerator < 0.0F) {
        return std::numeric_limits<float>::max();
      }
      continue;
    }
    const float planeDistance = numerator / denominator;
    if (denominator < 0.0F) {
      entry = std::max(entry, planeDistance);
    } else {
      exit = std::min(exit, planeDistance);
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

[[nodiscard]] float brushBoundsHitDistance(
  const ArenaBrush& brush,
  Vec3 origin,
  Vec3 direction
) {
  return wallHitDistance(ArenaWall{brush.min, brush.max}, origin, direction);
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

Vec3 weaponMuzzlePosition(const PlayerState& attacker, float eyeHeight) {
  constexpr CollisionBounds kDefaultPlayerBounds = {};
  const float heightScale =
    attacker.bounds.halfHeight / kDefaultPlayerBounds.halfHeight;
  return attacker.position + Vec3{0.0F, 0.0F, eyeHeight * heightScale};
}

void setTraceWorldDiagnosticsEnabled(bool enabled) {
  traceWorldDiagnosticsEnabled.store(enabled, std::memory_order_relaxed);
}

void resetTraceWorldDiagnostics() {
  traceWorldDiagnosticsCounters.calls.store(0, std::memory_order_relaxed);
  traceWorldDiagnosticsCounters.wallChecks.store(0, std::memory_order_relaxed);
  traceWorldDiagnosticsCounters.brushCandidates.store(0, std::memory_order_relaxed);
  traceWorldDiagnosticsCounters.brushExactTests.store(0, std::memory_order_relaxed);
  traceWorldDiagnosticsCounters.brushFaceChecks.store(0, std::memory_order_relaxed);
  traceWorldDiagnosticsCounters.brushBoxSkips.store(0, std::memory_order_relaxed);
  traceWorldDiagnosticsCounters.totalNanoseconds.store(0, std::memory_order_relaxed);
}

TraceWorldDiagnostics traceWorldDiagnostics() {
  return {
    traceWorldDiagnosticsCounters.calls.load(std::memory_order_relaxed),
    traceWorldDiagnosticsCounters.wallChecks.load(std::memory_order_relaxed),
    traceWorldDiagnosticsCounters.brushCandidates.load(std::memory_order_relaxed),
    traceWorldDiagnosticsCounters.brushExactTests.load(std::memory_order_relaxed),
    traceWorldDiagnosticsCounters.brushFaceChecks.load(std::memory_order_relaxed),
    traceWorldDiagnosticsCounters.brushBoxSkips.load(std::memory_order_relaxed),
    traceWorldDiagnosticsCounters.totalNanoseconds.load(std::memory_order_relaxed),
  };
}

WorldTrace traceWorld(
  const Arena& arena,
  Vec3 origin,
  Vec3 direction,
  float maxDistance
) {
  const bool diagnosticsEnabled =
    traceWorldDiagnosticsEnabled.load(std::memory_order_relaxed);
  const auto traceStart = diagnosticsEnabled
    ? std::chrono::steady_clock::now()
    : std::chrono::steady_clock::time_point{};
  if (diagnosticsEnabled) {
    addTraceWorldCount(traceWorldDiagnosticsCounters.calls, 1U);
  }
  WorldTrace trace;
  trace.start = origin;
  trace.distance = std::min(maxDistance, arenaExitDistance(arena, origin, direction));
  if (diagnosticsEnabled) {
    addTraceWorldCount(
      traceWorldDiagnosticsCounters.wallChecks,
      static_cast<std::uint64_t>(arena.wallCount)
    );
    addTraceWorldCount(
      traceWorldDiagnosticsCounters.brushCandidates,
      static_cast<std::uint64_t>(arena.brushCount)
    );
  }
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    trace.distance = std::min(
      trace.distance,
      wallHitDistance(arena.walls[index], origin, direction)
    );
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    const float boundsDistance =
      brushBoundsHitDistance(arena.brushes[index], origin, direction);
    if (boundsDistance > trace.distance + kTraceEpsilon) {
      if (diagnosticsEnabled) {
        addTraceWorldCount(traceWorldDiagnosticsCounters.brushBoxSkips, 1U);
      }
      continue;
    }
    trace.distance = std::min(
      trace.distance,
      brushHitDistance(
        arena.brushes[index],
        origin,
        direction,
        diagnosticsEnabled
      )
    );
  }
  trace.end = origin + (direction * trace.distance);
  if (diagnosticsEnabled) {
    const auto traceEnd = std::chrono::steady_clock::now();
    addTraceWorldCount(
      traceWorldDiagnosticsCounters.totalNanoseconds,
      static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
          traceEnd - traceStart
        ).count()
      )
    );
  }
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

WeaponFireResult simulateRailgun(
  const PlayerState& attacker,
  PlayerState& target,
  const UserCommand& command,
  const Arena& arena,
  const HitscanTuning& tuning
) {
  WeaponFireResult result;
  result.weapon = Weapon::Railgun;
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
  result.damageApplied = std::min(tuning.damage, target.health);
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
  result.damageApplied = std::min(tuning.damage, target.health);
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
    nearestHitDistance = std::min(nearestHitDistance, hitDistance);
    accumulatedKnockbackDirection += direction;
  }

  if (result.pelletHitCount == 0) {
    return result;
  }

  result.hit = true;
  result.end = result.start + (forward * nearestHitDistance);
  result.damageApplied =
    std::min(static_cast<int>(result.pelletHitCount) * tuning.damagePerPellet, target.health);
  target.health -= result.damageApplied;
  const float hitFraction =
    static_cast<float>(result.pelletHitCount) /
    static_cast<float>(std::max<std::uint8_t>(1, tuning.pelletCount));
  result.knockbackImpulse =
    normalize(accumulatedKnockbackDirection) * tuning.knockback * hitFraction;
  return result;
}

} // namespace lg
