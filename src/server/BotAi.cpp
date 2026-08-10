#include "server/BotAi.hpp"

#include "sim/Arena.hpp"
#include "sim/Movement.hpp"
#include "sim/PlayerState.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <bit>
#include <cmath>
#include <limits>

namespace lg {
namespace {

constexpr float kPi = 3.14159265359F;
constexpr float kHalfPi = kPi * 0.5F;
constexpr float kMaxPitchRadians = kHalfPi - 0.01F;
constexpr float kNavSpacing = 1.25F;
constexpr float kNavReachRadius = 0.55F;
// Neighboring samples may land on opposite sides of a narrow authored seam.
// Every longer candidate still has to complete normal player movement.
constexpr float kNavLinkSpacingFactor = 3.50F;
constexpr float kBotNavDt = 1.0F / 125.0F;
// Ten nearest candidates keep enough exits at crossroads while the fixed
// link cap still bounds map-load movement simulations.
constexpr std::size_t kMaxNavLinkCandidatesPerNode = 10U;
constexpr float kCommonBotTargetFovDegrees = 108.0F;
constexpr float kMaximumObservedSpeed = 60.0F;

struct NavSamplingBounds {
  Vec3 min = {};
  Vec3 max = {};
};

[[nodiscard]] std::uint32_t mixSeed(std::uint32_t value) {
  value ^= value >> 16U;
  value *= 0x7feb352dU;
  value ^= value >> 15U;
  value *= 0x846ca68bU;
  value ^= value >> 16U;
  return value == 0U ? 0xB07D0D6EU : value;
}

[[nodiscard]] std::uint32_t nextRandom(std::uint32_t& state) {
  std::uint32_t value = state;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  state = value == 0U ? 0xB07D0D6EU : value;
  return state;
}

[[nodiscard]] float randomUnit(std::uint32_t& state) {
  return static_cast<float>(nextRandom(state) >> 8U) /
    static_cast<float>(0x00ffffffU);
}

[[nodiscard]] float wrapRadians(float angle) {
  while (angle <= -kPi) angle += 2.0F * kPi;
  while (angle > kPi) angle -= 2.0F * kPi;
  return angle;
}

[[nodiscard]] float angleDeltaRadians(float from, float to) {
  return wrapRadians(to - from);
}

[[nodiscard]] float approach(float current, float target, float maxStep) {
  const float delta = target - current;
  if (std::fabs(delta) <= maxStep) return target;
  return current + std::copysign(maxStep, delta);
}

[[nodiscard]] float horizontalDistance(Vec3 first, Vec3 second) {
  return std::hypot(first.x - second.x, first.y - second.y);
}

[[nodiscard]] float distance3d(Vec3 first, Vec3 second) {
  return length(first - second);
}

// Imported maps may retain a generous world bounds box around a much smaller
// playable layout. Sampling that empty box would consume all fixed nav nodes
// before reaching any authored route. Collision and gameplay anchors define
// the area the player can actually use; plain test arenas keep their bounds.
[[nodiscard]] NavSamplingBounds navSamplingBounds(const Arena& arena) {
  Vec3 minimum = {std::numeric_limits<float>::infinity(),
    std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity()};
  Vec3 maximum = {-std::numeric_limits<float>::infinity(),
    -std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
  bool haveAuthoredExtent = false;
  const auto include = [&](Vec3 position) {
    minimum.x = std::min(minimum.x, position.x);
    minimum.y = std::min(minimum.y, position.y);
    minimum.z = std::min(minimum.z, position.z);
    maximum.x = std::max(maximum.x, position.x);
    maximum.y = std::max(maximum.y, position.y);
    maximum.z = std::max(maximum.z, position.z);
    haveAuthoredExtent = true;
  };
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    include(arena.walls[index].min);
    include(arena.walls[index].max);
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    include(arena.brushes[index].min);
    include(arena.brushes[index].max);
  }
  for (std::size_t index = 0; index < arena.spawnCount; ++index) {
    include(arena.spawnPositions[index]);
  }
  for (std::size_t index = 0; index < arena.teamSpawnCount; ++index) {
    include(arena.teamSpawns[index].position);
  }
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    include(arena.healthPickups[index].position);
  }
  if (arena.mcguffin.hasNeutralSpawn) include(arena.mcguffin.neutralSpawn);
  if (arena.mcguffin.hasRedBase) {
    include(arena.mcguffin.redBase.min);
    include(arena.mcguffin.redBase.max);
  }
  if (arena.mcguffin.hasBlueBase) {
    include(arena.mcguffin.blueBase.min);
    include(arena.mcguffin.blueBase.max);
  }
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    include(arena.jumpPads[index].min);
    include(arena.jumpPads[index].max);
    include(arena.jumpPads[index].targetPosition);
  }
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    include(arena.teleports[index].min);
    include(arena.teleports[index].max);
    include(arena.teleports[index].destination);
  }
  if (!haveAuthoredExtent) return {arena.min, arena.max};
  constexpr float kMargin = 2.0F * kNavSpacing;
  minimum.x = std::max(arena.min.x, minimum.x - kMargin);
  minimum.y = std::max(arena.min.y, minimum.y - kMargin);
  minimum.z = std::max(arena.min.z, minimum.z - kMargin);
  maximum.x = std::min(arena.max.x, maximum.x + kMargin);
  maximum.y = std::min(arena.max.y, maximum.y + kMargin);
  maximum.z = std::min(arena.max.z, maximum.z + kMargin);
  return {minimum, maximum};
}

[[nodiscard]] bool canStandAt(
  const Arena& arena,
  CollisionBounds bounds,
  Vec3 position
) {
  PlayerState player;
  player.position = position;
  player.bounds = bounds;
  return !playerPositionSolid(arena, player, position);
}

// A cheap player-bounds sweep filters links that a straight walking command
// plainly cannot enter. It never accepts a link: every surviving edge still
// runs the full fixed-step movement proof below.
[[nodiscard]] bool linearlyBlockedForPlayer(
  const Arena& arena,
  CollisionBounds bounds,
  Vec3 from,
  Vec3 to
) {
  PlayerState player;
  player.position = from;
  player.bounds = bounds;
  player.health = 100;
  player.onGround = true;
  player.movementMode = MovementMode::Grounded;
  const CollisionResult trace = resolvePlayerArenaCollision(arena, player, to, to - from);
  return distance3d(trace.position, to) > kNavReachRadius;
}

// A nav grid node must rest on the same kind of surface a normal player uses.
[[nodiscard]] bool groundedNodePosition(
  const Arena& arena,
  CollisionBounds bounds,
  Vec3 hint,
  Vec3& position
) {
  PlayerState player;
  // Imported maps occasionally put a semantic origin just inside floor
  // detail. Search a short vertical span before rejecting the anchor, then
  // settle it through ordinary player movement.
  Vec3 start = hint;
  player.bounds = bounds;
  bool foundStart = false;
  for (std::size_t raise = 0; raise <= 12U; ++raise) {
    start = hint + Vec3{0.0F, 0.0F, static_cast<float>(raise) * 0.25F};
    if (!playerPositionSolid(arena, player, start)) {
      foundStart = true;
      break;
    }
  }
  if (!foundStart) return false;
  player.position = start;
  // Authored spawns can sit well above a floor. Step downward through normal
  // player collision rather than assuming a grid hint is already within 35cm
  // of a standable surface.
  Vec3 probe = start;
  for (std::size_t step = 0; step < 32U && probe.z >= arena.min.z - bounds.halfHeight;
       ++step) {
    const CollisionResult landing = resolvePlayerArenaCollision(
      arena, player, probe - Vec3{0.0F, 0.0F, 0.75F}, {0.0F, 0.0F, -1.0F}
    );
    if (landing.onGround && !playerPositionSolid(arena, player, landing.position)) {
      position = landing.position;
      return true;
    }
    if (landing.position.z >= probe.z - 0.001F) break;
    probe = landing.position;
  }
  // Some imported maps author spawn origins high above their first landing
  // surface. Use the same fixed player movement as a final anchor check, not
  // a ray or a direct position write.
  UserCommand settle;
  player.position = start;
  player.velocity = {};
  player.onGround = false;
  player.movementMode = MovementMode::Airborne;
  for (std::size_t tick = 0; tick < 250U; ++tick) {
    simulateMovement(player, settle, arena, MovementTuning{}, kBotNavDt);
    if (player.onGround && !playerPositionSolid(arena, player, player.position)) {
      position = player.position;
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool canTraverse(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds,
  Vec3 from,
  Vec3 to,
  bool jump,
  int maximumTicks = 1536
) {
  PlayerState player;
  player.position = from;
  player.bounds = bounds;
  player.health = 100;
  player.onGround = true;
  player.movementMode = MovementMode::Grounded;
  const Vec3 delta = to - from;
  const float distance = std::max(0.01F, distance3d(from, to));
  UserCommand command;
  command.viewYawRadians = std::atan2(delta.y, delta.x);
  command.viewPitchRadians = 0.0F;
  command.forwardMove = 1.0F;
  command.jump = jump;
  const int ticks = std::clamp(
    static_cast<int>(std::ceil(distance / std::max(1.0F, movement.maxGroundSpeed) /
      kBotNavDt * 1.75F)),
    12,
    // Imported maps can be hundreds of normal simulation units wide. A link
    // still has to reach its endpoint through ordinary movement, with this
    // fixed ceiling keeping blocked authored geometry bounded.
    maximumTicks
  );
  for (int tick = 0; tick < ticks; ++tick) {
    simulateMovement(player, command, arena, movement, kBotNavDt);
    if (distance3d(player.position, to) <= kNavReachRadius) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::size_t nearestNodeWithin(
  const BotNavigationMap& map,
  Vec3 position,
  float maximumDistance
) {
  std::size_t best = BotNavigationMap::kMaxNodes;
  float bestDistance = maximumDistance;
  for (std::size_t index = 0; index < map.nodeCount; ++index) {
    const float distance = distance3d(map.nodes[index].position, position);
    if (distance <= bestDistance) {
      best = index;
      bestDistance = distance;
    }
  }
  return best;
}

[[nodiscard]] BotNavLinkKind linkKind(
  const BotNavigationMap& map,
  std::size_t from,
  std::size_t to
) {
  for (std::size_t index = 0; index < map.linkCount; ++index) {
    const BotNavLink& link = map.links[index];
    if (link.from == from && link.to == to) return link.kind;
  }
  return BotNavLinkKind::Walk;
}

[[nodiscard]] Vec3 aimPoint(Vec3 position, float halfHeight) {
  return position + Vec3{0.0F, 0.0F, halfHeight * 0.45F};
}

} // namespace

BotDifficultyProfile botDifficultyProfile(BotAttackMode mode) {
  switch (mode) {
  case BotAttackMode::Easy:
    return {
      .reactionMinSeconds = 0.30F,
      .reactionMaxSeconds = 0.50F,
      .maxTurnRadiansPerSecond = 1.35F,
      .turnAccelerationRadiansPerSecond2 = 8.0F,
      .trackingErrorRadians = 0.11F,
      // Lower skill waits for a closer alignment. Higher skill still fires
      // only when the estimated hit chance is high, never at a wider angle.
      .fireToleranceRadians = 0.065F,
      .predictionSeconds = 0.04F,
      .memorySeconds = 1.20F,
      .planningIntervalSeconds = 0.75F,
      .targetFovDegrees = kCommonBotTargetFovDegrees,
      .preferredRange = 7.0F,
      .strafeStrength = 0.35F,
      .dashChancePerSecond = 0.02F,
    };
  case BotAttackMode::Medium:
    return {
      .reactionMinSeconds = 0.18F,
      .reactionMaxSeconds = 0.30F,
      .maxTurnRadiansPerSecond = 3.25F,
      .turnAccelerationRadiansPerSecond2 = 18.0F,
      .trackingErrorRadians = 0.055F,
      .fireToleranceRadians = 0.050F,
      .predictionSeconds = 0.12F,
      .memorySeconds = 1.80F,
      .planningIntervalSeconds = 0.45F,
      .targetFovDegrees = kCommonBotTargetFovDegrees,
      .preferredRange = 8.0F,
      .strafeStrength = 0.55F,
      .dashChancePerSecond = 0.05F,
    };
  case BotAttackMode::Hard:
    return {
      .reactionMinSeconds = 0.12F,
      .reactionMaxSeconds = 0.20F,
      .maxTurnRadiansPerSecond = 5.40F,
      .turnAccelerationRadiansPerSecond2 = 30.0F,
      .trackingErrorRadians = 0.022F,
      .fireToleranceRadians = 0.035F,
      .predictionSeconds = 0.20F,
      .memorySeconds = 2.40F,
      .planningIntervalSeconds = 0.30F,
      .targetFovDegrees = kCommonBotTargetFovDegrees,
      .preferredRange = 9.0F,
      .strafeStrength = 0.70F,
      .dashChancePerSecond = 0.08F,
    };
  case BotAttackMode::Off:
    break;
  }
  return {};
}

BotWeaponScore scoreBotWeapon(
  const BotWeaponSense& weapon,
  const BotCombatContext& context,
  float preferredRange,
  bool isCurrentWeapon
) {
  BotWeaponScore score;
  if (!weapon.usable || !std::isfinite(context.targetDistance)) return score;

  const float range = std::max(1.0F, weapon.effectiveRange);
  const float desiredRange = std::clamp(preferredRange, 1.0F, range * 0.90F);
  score.rangeFit = std::clamp(1.0F - std::fabs(context.targetDistance - desiredRange) /
    std::max(range, desiredRange), 0.0F, 1.0F);
  const float viewAccuracy = std::clamp(1.0F - context.angularErrorRadians / 0.16F,
    0.0F, 1.0F);
  const float motionPenalty = weapon.projectileSpeed > 0.01F
    ? std::clamp(context.targetLateralSpeed /
        std::max(1.0F, weapon.projectileSpeed) *
        std::max(0.5F, context.targetDistance / range), 0.0F, 0.75F)
    : 0.0F;
  score.projectileDifficulty = motionPenalty;
  score.hitChance = viewAccuracy * (1.0F - motionPenalty) *
    (context.targetGrounded ? 1.0F : 0.88F) *
    std::clamp(1.0F - context.exposureAgeSeconds * 0.08F, 0.75F, 1.0F);
  const float interval = std::max(0.025F, weapon.fireIntervalSeconds);
  score.damageRate = std::clamp((weapon.damagePerShot / interval) / 160.0F,
    0.0F, 1.25F);
  if (weapon.splashRadius > 0.0F && context.nearbySplashSurface) {
    score.splashValue = std::clamp(weapon.splashDamage / 100.0F, 0.0F, 1.0F) *
      std::clamp(1.0F - context.targetDistance / std::max(1.0F, range), 0.0F, 1.0F);
  }
  if (weapon.splashRadius > 0.0F) {
    const float dangerRadius = std::max(0.5F, weapon.splashRadius * 1.35F);
    score.selfRisk = std::clamp(1.0F - context.targetDistance / dangerRadius, 0.0F, 1.0F) *
      std::clamp((125.0F - static_cast<float>(context.selfHealth)) / 100.0F, 0.25F, 1.0F);
  }
  // A cooling current weapon is not ready simply because it avoids a pullout.
  // Scale by its own cadence so a short beam interval and a rail delay remain
  // comparable choices.
  score.cooldownPenalty = std::clamp(weapon.cooldownSeconds /
    std::max(0.05F, weapon.fireIntervalSeconds), 0.0F, 1.0F);
  score.switchCost = isCurrentWeapon ? 0.0F : std::clamp(
    weapon.switchCostSeconds * 1.5F, 0.0F, 1.0F);
  score.total = 0.34F * score.rangeFit + 0.34F * score.hitChance +
    0.24F * score.damageRate + 0.18F * score.splashValue -
    0.40F * score.selfRisk - 0.18F * score.projectileDifficulty -
    0.22F * score.cooldownPenalty -
    0.15F * score.switchCost;
  if (isCurrentWeapon) score.total += 0.035F;
  return score;
}

BotNavigationMap buildBotNavigationMap(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds
) {
  BotNavigationMap map;
  const NavSamplingBounds samplingBounds = navSamplingBounds(arena);
  std::array<std::size_t, BotNavigationMap::kMaxNodes> semanticAnchorNodes = {};
  std::size_t semanticAnchorCount = 0;
  std::array<std::size_t, Arena::kJumpPadCount> jumpPadEntryNodes = {};
  std::array<std::size_t, Arena::kJumpPadCount> jumpPadDestinationNodes = {};
  std::array<std::size_t, Arena::kTeleportCount> teleportEntryNodes = {};
  std::array<std::size_t, Arena::kTeleportCount> teleportDestinationNodes = {};
  jumpPadEntryNodes.fill(BotNavigationMap::kMaxNodes);
  jumpPadDestinationNodes.fill(BotNavigationMap::kMaxNodes);
  teleportEntryNodes.fill(BotNavigationMap::kMaxNodes);
  teleportDestinationNodes.fill(BotNavigationMap::kMaxNodes);
  auto addGroundedNodeIndex = [&](Vec3 hint) {
    Vec3 position = {};
    if (!groundedNodePosition(arena, bounds, hint, position)) {
      return BotNavigationMap::kMaxNodes;
    }
    const std::size_t existing = nearestNodeWithin(map, position, 0.35F);
    if (existing < map.nodeCount) return existing;
    if (map.nodeCount == BotNavigationMap::kMaxNodes) return BotNavigationMap::kMaxNodes;
    map.nodes[map.nodeCount].position = position;
    return map.nodeCount++;
  };
  const auto addGroundedNode = [&](Vec3 hint) {
    return addGroundedNodeIndex(hint) < map.nodeCount;
  };
  const auto addGroundedNearbyNodeIndex = [&](Vec3 hint) {
    std::size_t node = addGroundedNodeIndex(hint);
    if (node < map.nodeCount) return node;
    // Some imported item origins sit in decorative collision. A nearby player
    // origin still represents the same pickup region, but it must settle by
    // ordinary collision and remain close enough for normal pickup contact.
    constexpr std::array<Vec3, 8U> kNearbyOffsets = {{{1.0F, 0.0F, 0.0F},
      {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
      {0.70710678F, 0.70710678F, 0.0F}, {-0.70710678F, 0.70710678F, 0.0F},
      {0.70710678F, -0.70710678F, 0.0F}, {-0.70710678F, -0.70710678F, 0.0F}}};
    for (Vec3 offset : kNearbyOffsets) {
      node = addGroundedNodeIndex(hint + offset * 0.75F);
      if (node < map.nodeCount) return node;
    }
    return BotNavigationMap::kMaxNodes;
  };
  auto addExactNodeIndex = [&](Vec3 position) {
    if (!canStandAt(arena, bounds, position)) return BotNavigationMap::kMaxNodes;
    const std::size_t existing = nearestNodeWithin(map, position, 0.35F);
    if (existing < map.nodeCount) return existing;
    if (map.nodeCount == BotNavigationMap::kMaxNodes) return BotNavigationMap::kMaxNodes;
    map.nodes[map.nodeCount].position = position;
    return map.nodeCount++;
  };
  const auto rememberSemanticAnchor = [&](std::size_t node) {
    if (node >= map.nodeCount) return;
    for (std::size_t index = 0; index < semanticAnchorCount; ++index) {
      if (semanticAnchorNodes[index] == node) return;
    }
    if (semanticAnchorCount < semanticAnchorNodes.size()) {
      semanticAnchorNodes[semanticAnchorCount++] = node;
    }
  };
  const auto requireAnchor = [&](std::size_t node) {
    ++map.requiredAnchorCount;
    if (node >= map.nodeCount) {
      map.requiredAnchorsComplete = false;
      ++map.missingRequiredAnchorCount;
      return;
    }
    rememberSemanticAnchor(node);
  };

  // Add every gameplay anchor before the bulk grid. This ordered reservation
  // makes a 512-node cap explicit instead of silently dropping a spawn, item,
  // base, pad, or teleport because decorative samples used the capacity.
  for (std::size_t index = 0; index < arena.spawnCount; ++index) {
    Vec3 position = arena.spawnPositions[index];
    position.z += bounds.halfHeight;
    requireAnchor(addGroundedNodeIndex(position));
  }
  for (std::size_t index = 0; index < arena.teamSpawnCount; ++index) {
    Vec3 position = arena.teamSpawns[index].position;
    position.z += bounds.halfHeight;
    requireAnchor(addGroundedNodeIndex(position));
  }
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    Vec3 position = arena.healthPickups[index].position;
    position.z = std::max(position.z, arena.min.z) + bounds.halfHeight;
    requireAnchor(addGroundedNearbyNodeIndex(position));
  }
  const auto requireBase = [&](const ArenaMcGuffinBase& base) {
    requireAnchor(addGroundedNodeIndex({(base.min.x + base.max.x) * 0.5F,
      (base.min.y + base.max.y) * 0.5F,
      std::max(base.min.z, arena.min.z) + bounds.halfHeight}));
  };
  if (arena.mcguffin.hasNeutralSpawn) {
    requireAnchor(addGroundedNodeIndex(arena.mcguffin.neutralSpawn +
      Vec3{0.0F, 0.0F, bounds.halfHeight}));
  }
  if (arena.mcguffin.hasRedBase) requireBase(arena.mcguffin.redBase);
  if (arena.mcguffin.hasBlueBase) requireBase(arena.mcguffin.blueBase);
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const ArenaJumpPad& pad = arena.jumpPads[index];
    jumpPadEntryNodes[index] = addGroundedNodeIndex({(pad.min.x + pad.max.x) * 0.5F,
      (pad.min.y + pad.max.y) * 0.5F,
      std::max(pad.min.z, arena.min.z) + bounds.halfHeight});
    requireAnchor(jumpPadEntryNodes[index]);
    const Vec3 target = pad.hasTarget
      ? pad.targetPosition
      : Vec3{pad.max.x, pad.max.y, pad.max.z + bounds.halfHeight};
    // A pad target commonly names an airborne launch point. The route must
    // end at the first normal grounded landing, not reject that valid trigger
    // because the authored destination itself is in the air.
    jumpPadDestinationNodes[index] = addGroundedNodeIndex(target);
    requireAnchor(jumpPadDestinationNodes[index]);
  }
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    const ArenaTeleport& teleport = arena.teleports[index];
    teleportEntryNodes[index] = addGroundedNodeIndex({(teleport.min.x + teleport.max.x) * 0.5F,
      (teleport.min.y + teleport.max.y) * 0.5F,
      std::max(teleport.min.z, arena.min.z) + bounds.halfHeight});
    requireAnchor(teleportEntryNodes[index]);
    // Prefer the exact authored player-origin because Movement::applyTeleports
    // writes it unchanged. If it is airborne, retain the normal grounded
    // landing node that the next movement ticks can reach.
    teleportDestinationNodes[index] = addExactNodeIndex(teleport.destination);
    if (teleportDestinationNodes[index] >= map.nodeCount) {
      teleportDestinationNodes[index] = addGroundedNodeIndex(teleport.destination);
    }
    requireAnchor(teleportDestinationNodes[index]);
  }

  // Thin routes around a partition end can lie between two coarse grid rows.
  // Sample collision-expanded wall corners before broad fill so a normal-size
  // player gets a tested route through authored door and wall-end clearances.
  constexpr std::size_t kBoundaryTravelNodeBudget = 96U;
  const std::size_t boundaryTravelNodeLimit = std::min(BotNavigationMap::kMaxNodes,
    map.nodeCount + kBoundaryTravelNodeBudget);
  constexpr float kBoundaryClearance = 0.02F;
  for (std::size_t index = 0;
       index < arena.wallCount && map.nodeCount < boundaryTravelNodeLimit;
       ++index) {
    const ArenaWall& wall = arena.walls[index];
    const float lowX = wall.min.x - bounds.radius - kBoundaryClearance;
    const float highX = wall.max.x + bounds.radius + kBoundaryClearance;
    const float lowY = wall.min.y - bounds.radius - kBoundaryClearance;
    const float highY = wall.max.y + bounds.radius + kBoundaryClearance;
    const float z = std::max(wall.min.z, arena.min.z) + bounds.halfHeight;
    for (Vec3 corner : std::array<Vec3, 4U>{{
        {lowX, lowY, z}, {lowX, highY, z}, {highX, lowY, z}, {highX, highY, z}}}) {
      if (map.nodeCount == boundaryTravelNodeLimit) break;
      addGroundedNode(corner);
    }
  }

  // Expand from gameplay anchors before sampling the broad world box. This
  // joins narrow routes around real spawns and resources while fixed work and
  // storage limits keep malformed maps bounded.
  constexpr std::array<Vec3, 8U> kAnchorDirections = {{{1.0F, 0.0F, 0.0F},
    {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
    {0.70710678F, 0.70710678F, 0.0F}, {-0.70710678F, 0.70710678F, 0.0F},
    {0.70710678F, -0.70710678F, 0.0F}, {-0.70710678F, -0.70710678F, 0.0F}}};
  constexpr std::size_t kAnchorExpansionRings = 8U;
  // Leave most fixed capacity for broad connector samples. The reservation
  // still visits every semantic anchor before taking a second outward ring.
  constexpr std::size_t kAnchorExpansionNodeBudget = 256U;
  constexpr std::size_t kAnchorExpansionProbeLimit = 1024U;
  std::size_t anchorExpansionProbes = 0;
  for (std::size_t ring = 1U;
       ring <= kAnchorExpansionRings && map.nodeCount < BotNavigationMap::kMaxNodes &&
         map.nodeCount < kAnchorExpansionNodeBudget &&
         anchorExpansionProbes < kAnchorExpansionProbeLimit;
       ++ring) {
    for (std::size_t anchor = 0U;
         anchor < semanticAnchorCount && map.nodeCount < BotNavigationMap::kMaxNodes &&
           map.nodeCount < kAnchorExpansionNodeBudget &&
           anchorExpansionProbes < kAnchorExpansionProbeLimit;
         ++anchor) {
      const Vec3 origin = map.nodes[semanticAnchorNodes[anchor]].position;
      for (Vec3 direction : kAnchorDirections) {
        if (map.nodeCount == BotNavigationMap::kMaxNodes ||
            map.nodeCount == kAnchorExpansionNodeBudget ||
            anchorExpansionProbes++ == kAnchorExpansionProbeLimit) break;
        addGroundedNode(origin + direction * (kNavSpacing * static_cast<float>(ring)));
      }
    }
  }

  struct GroundLevel {
    float height = 0.0F;
    Vec3 representative = {};
  };
  std::array<GroundLevel, BotNavigationMap::kMaxNodes> groundLevels = {};
  std::size_t groundLevelCount = 0;
  const auto addGroundLevel = [&](float level, Vec3 representative) {
    if (groundLevelCount == groundLevels.size() || level < samplingBounds.min.z ||
        level > samplingBounds.max.z - bounds.halfHeight) return;
    for (std::size_t index = 0; index < groundLevelCount; ++index) {
      if (std::fabs(groundLevels[index].height - level) <= 0.05F) return;
    }
    groundLevels[groundLevelCount++] = {level, representative};
  };
  addGroundLevel(samplingBounds.min.z, {
    (samplingBounds.min.x + samplingBounds.max.x) * 0.5F,
    (samplingBounds.min.y + samplingBounds.max.y) * 0.5F,
    samplingBounds.min.z,
  });
  for (std::size_t index = 0; index < arena.wallCount; ++index) {
    const ArenaWall& wall = arena.walls[index];
    addGroundLevel(wall.max.z, {
      (wall.min.x + wall.max.x) * 0.5F,
      (wall.min.y + wall.max.y) * 0.5F,
      wall.max.z,
    });
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    const ArenaBrush& brush = arena.brushes[index];
    addGroundLevel(brush.max.z, {
      (brush.min.x + brush.max.x) * 0.5F,
      (brush.min.y + brush.max.y) * 0.5F,
      brush.max.z,
    });
  }
  std::sort(groundLevels.begin(), groundLevels.begin() + groundLevelCount,
    [](const GroundLevel& first, const GroundLevel& second) {
      return first.height < second.height;
    });
  // Keep samples close to authored floor edges. A player may need the narrow
  // strip beside a partition end; shrinking by its radius can erase the only
  // real route before movement validation gets a chance to test it.
  const float startX = samplingBounds.min.x + 0.15F;
  const float startY = samplingBounds.min.y + 0.15F;
  const float endX = samplingBounds.max.x - 0.15F;
  const float endY = samplingBounds.max.y - 0.15F;
  // Keep at least half the node budget for a connected grid. Every selected
  // level receives an anchored, collision-checked surface sample first.
  const std::size_t levelBudget = std::max<std::size_t>(1U,
    (BotNavigationMap::kMaxNodes - map.nodeCount) / 2U);
  const std::size_t sampledLevelCount = std::min(groundLevelCount, levelBudget);
  const auto sampledLevelIndex = [&](std::size_t sample) {
    if (sampledLevelCount <= 1U) return std::size_t{0};
    return (sample * (groundLevelCount - 1U)) / (sampledLevelCount - 1U);
  };
  for (std::size_t sample = 0; sample < sampledLevelCount; ++sample) {
    const GroundLevel& level = groundLevels[sampledLevelIndex(sample)];
    addGroundedNode(level.representative + Vec3{0.0F, 0.0F, bounds.halfHeight});
  }
  const float mapArea = std::max(1.0F, (endX - startX) * (endY - startY));
  const float remainingNodes = static_cast<float>(
    std::max<std::size_t>(1U, BotNavigationMap::kMaxNodes - map.nodeCount)
  );
  const float gridSpacing = std::max(kNavSpacing, std::sqrt(mapArea / remainingNodes));
  for (std::size_t sample = 0;
       sample < sampledLevelCount && map.nodeCount < BotNavigationMap::kMaxNodes;
       ++sample) {
    const float z = groundLevels[sampledLevelIndex(sample)].height + bounds.halfHeight;
    for (float y = startY; y <= endY && map.nodeCount < BotNavigationMap::kMaxNodes;
         y += gridSpacing) {
      for (float x = startX; x <= endX && map.nodeCount < BotNavigationMap::kMaxNodes;
           x += gridSpacing) {
        addGroundedNode({x, y, z});
      }
    }
  }
  // If broad samples miss narrow corridors, use every remaining fixed node
  // slot at player spacing. The probe cap makes malformed maps fail promptly.
  constexpr std::size_t kFinePassNodeTarget = BotNavigationMap::kMaxNodes;
  constexpr std::size_t kFinePassProbeLimit = 4096U;
  if (map.nodeCount < kFinePassNodeTarget) {
    std::size_t fineProbeCount = 0;
    for (std::size_t sample = 0;
         sample < sampledLevelCount && map.nodeCount < kFinePassNodeTarget &&
           fineProbeCount < kFinePassProbeLimit;
         ++sample) {
      const float z = groundLevels[sampledLevelIndex(sample)].height + bounds.halfHeight;
      for (float y = startY; y <= endY && map.nodeCount < kFinePassNodeTarget &&
           fineProbeCount < kFinePassProbeLimit;
           y += kNavSpacing) {
        for (float x = startX; x <= endX && map.nodeCount < kFinePassNodeTarget &&
             fineProbeCount < kFinePassProbeLimit;
             x += kNavSpacing) {
          ++fineProbeCount;
          addGroundedNode({x, y, z});
        }
      }
    }
  }
  auto addLink = [&](std::size_t from, std::size_t to, BotNavLinkKind kind) {
    if (from >= map.nodeCount || to >= map.nodeCount || from == to ||
        map.linkCount == BotNavigationMap::kMaxLinks) return;
    for (std::size_t index = 0; index < map.linkCount; ++index) {
      if (map.links[index].from == from && map.links[index].to == to) return;
    }
    map.links[map.linkCount++] = BotNavLink{
      static_cast<std::uint16_t>(from), static_cast<std::uint16_t>(to), kind};
  };
  const std::size_t specialLinkReserve = std::min(BotNavigationMap::kMaxLinks,
    arena.jumpPadCount + arena.teleportCount);
  const std::size_t nonSpecialLinkBudget = BotNavigationMap::kMaxLinks - specialLinkReserve;
  constexpr std::size_t kLocalLinkBudget = BotNavigationMap::kMaxLinks * 3U / 5U;
  const std::size_t localLinkBudget = std::min(kLocalLinkBudget, nonSpecialLinkBudget);
  const auto addLocalLink = [&](std::size_t from, std::size_t to, BotNavLinkKind kind) {
    if (map.linkCount < localLinkBudget) addLink(from, to, kind);
  };

  // The reach derives from the samples we actually emitted. It covers a
  // neighboring diagonal cell while every candidate still has to pass normal
  // player movement, so coarse large-map samples cannot form a ray-only path.
  const float linkDistance = gridSpacing * kNavLinkSpacingFactor;
  std::array<std::array<std::uint16_t, kMaxNavLinkCandidatesPerNode>,
    BotNavigationMap::kMaxNodes> candidateNodes = {};
  std::array<std::size_t, BotNavigationMap::kMaxNodes> candidateCounts = {};
  for (std::size_t from = 0; from < map.nodeCount; ++from) {
    std::array<float, kMaxNavLinkCandidatesPerNode> candidateDistances = {};
    std::array<std::uint16_t, 8U> directionalCandidates = {};
    std::array<float, 8U> directionalDistances = {};
    directionalDistances.fill(std::numeric_limits<float>::infinity());
    std::size_t candidateCount = 0;
    for (std::size_t to = 0; to < map.nodeCount; ++to) {
      if (from == to) continue;
      const float distance = distance3d(map.nodes[from].position, map.nodes[to].position);
      if (distance > linkDistance) continue;
      const Vec3 delta = map.nodes[to].position - map.nodes[from].position;
      if (std::hypot(delta.x, delta.y) > 0.01F) {
        const float angle = std::atan2(delta.y, delta.x);
        const std::size_t sector = static_cast<std::size_t>(std::floor(
          (angle + kPi) * 4.0F / kPi)) & 7U;
        if (distance < directionalDistances[sector]) {
          directionalDistances[sector] = distance;
          directionalCandidates[sector] = static_cast<std::uint16_t>(to);
        }
      }
      std::size_t insert = candidateCount;
      while (insert > 0U && candidateDistances[insert - 1U] > distance) --insert;
      if (insert >= candidateNodes[from].size()) continue;
      const std::size_t limit = std::min(candidateCount + 1U, candidateNodes[from].size());
      for (std::size_t move = limit - 1U; move > insert; --move) {
        candidateNodes[from][move] = candidateNodes[from][move - 1U];
        candidateDistances[move] = candidateDistances[move - 1U];
      }
      candidateNodes[from][insert] = static_cast<std::uint16_t>(to);
      candidateDistances[insert] = distance;
      candidateCount = limit;
    }
    const std::array<std::uint16_t, kMaxNavLinkCandidatesPerNode> nearestCandidates =
      candidateNodes[from];
    const std::size_t nearestCount = candidateCount;
    candidateCount = 0;
    const auto appendCandidate = [&](std::uint16_t candidate) {
      for (std::size_t existing = 0; existing < candidateCount; ++existing) {
        if (candidateNodes[from][existing] == candidate) return;
      }
      if (candidateCount < candidateNodes[from].size()) {
        candidateNodes[from][candidateCount++] = candidate;
      }
    };
    for (std::size_t sector = 0; sector < directionalCandidates.size(); ++sector) {
      if (std::isfinite(directionalDistances[sector])) {
        appendCandidate(directionalCandidates[sector]);
      }
    }
    for (std::size_t nearest = 0;
         nearest < nearestCount && candidateCount < candidateNodes[from].size(); ++nearest) {
      appendCandidate(nearestCandidates[nearest]);
    }
    candidateCounts[from] = candidateCount;
  }
  // Trial movement is the expensive part of map validation. Visit one local
  // candidate across the whole graph before trying a second, in a coprime
  // order that covers anchors and broad samples alike. This bounds work while
  // preserving connected coverage instead of spending all trials at index 0.
  constexpr std::size_t kNavigationTraversalTrialLimit = 1024U;
  std::size_t traversalTrials = 0;
  for (std::size_t candidate = 0;
       candidate < kMaxNavLinkCandidatesPerNode &&
         traversalTrials < kNavigationTraversalTrialLimit;
       ++candidate) {
    for (std::size_t sequence = 0;
         sequence < map.nodeCount && traversalTrials < kNavigationTraversalTrialLimit;
         ++sequence) {
      const std::size_t from = (sequence * 173U) % map.nodeCount;
      if (candidate >= candidateCounts[from]) continue;
      const std::size_t to = candidateNodes[from][candidate];
      ++traversalTrials;
      const bool linearlyBlocked = linearlyBlockedForPlayer(arena, bounds,
        map.nodes[from].position, map.nodes[to].position);
      if (linearlyBlocked) ++map.localBroadphaseRejects;
      if (!linearlyBlocked && canTraverse(arena, movement, bounds, map.nodes[from].position,
            map.nodes[to].position, false, 256)) {
        const BotNavLinkKind kind = std::fabs(map.nodes[from].position.z -
          map.nodes[to].position.z) <= 0.05F ? BotNavLinkKind::Walk : BotNavLinkKind::Step;
        addLocalLink(from, to, kind);
        // A walk or step link needs an independently simulated reverse edge.
        // Do not infer this from geometry: slopes and seams may make it fail.
        if (canTraverse(arena, movement, bounds, map.nodes[to].position,
              map.nodes[from].position, false, 256)) {
          addLocalLink(to, from, kind);
        }
      } else if (std::fabs(map.nodes[from].position.z - map.nodes[to].position.z) > 0.05F &&
                 canTraverse(arena, movement, bounds, map.nodes[from].position,
                   map.nodes[to].position, true, 256)) {
        addLocalLink(from, to, BotNavLinkKind::Jump);
      }
    }
  }
  map.localTraversalTrials = traversalTrials;
  map.localLinkCount = map.linkCount;
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const std::size_t from = jumpPadEntryNodes[index];
    const std::size_t to = jumpPadDestinationNodes[index];
    if (from < map.nodeCount && to < map.nodeCount) {
      // A pad link is legal only through the real trigger. It is not a
      // walkable straight line and therefore must not use walk validation.
      addLink(from, to, BotNavLinkKind::JumpPad);
    }
  }
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    const std::size_t from = teleportEntryNodes[index];
    const std::size_t to = teleportDestinationNodes[index];
    if (from < map.nodeCount && to < map.nodeCount) {
      // Movement::applyTeleports performs this directed transition when the
      // player enters the authored trigger; do not invent a direct walk link.
      addLink(from, to, BotNavLinkKind::Teleport);
    }
  }

  // Local links deliberately stop before the fixed link store is full so pad
  // and teleport links always fit. Then expand the component containing the
  // first gameplay anchor through the nearest untried components. Every bridge
  // uses normal player movement in both directions; this is not a geometric
  // shortcut and it leaves unrelated decorative components alone.
  std::array<std::size_t, BotNavigationMap::kMaxNodes> componentParent = {};
  for (std::size_t node = 0; node < map.nodeCount; ++node) componentParent[node] = node;
  const auto findComponent = [&](auto&& self, std::size_t node) -> std::size_t {
    if (componentParent[node] == node) return node;
    componentParent[node] = self(self, componentParent[node]);
    return componentParent[node];
  };
  const auto mergeComponents = [&](std::size_t first, std::size_t second) {
    first = findComponent(findComponent, first);
    second = findComponent(findComponent, second);
    if (first != second) componentParent[second] = first;
  };
  for (std::size_t link = 0; link < map.linkCount; ++link) {
    mergeComponents(map.links[link].from, map.links[link].to);
  }
  constexpr std::size_t kBridgeTraversalTrialLimit = 512U;
  std::array<std::bitset<BotNavigationMap::kMaxNodes>, BotNavigationMap::kMaxNodes>
    testedBridgePairs = {};
  const auto anchorsConnected = [&] {
    if (semanticAnchorCount < 2U) return true;
    const std::size_t root = findComponent(findComponent, semanticAnchorNodes[0]);
    for (std::size_t anchor = 1U; anchor < semanticAnchorCount; ++anchor) {
      if (findComponent(findComponent, semanticAnchorNodes[anchor]) != root) return false;
    }
    return true;
  };
  while (semanticAnchorCount > 1U && !anchorsConnected() &&
         map.bridgeTraversalTrials < kBridgeTraversalTrialLimit &&
         map.linkCount < nonSpecialLinkBudget) {
    // Give every disconnected gameplay-anchor component equal priority. This
    // avoids expanding whichever anchor happened to occupy node zero while a
    // second spawn waits behind a wall.
    std::array<bool, BotNavigationMap::kMaxNodes> componentHasAnchor = {};
    for (std::size_t anchor = 0; anchor < semanticAnchorCount; ++anchor) {
      componentHasAnchor[findComponent(findComponent, semanticAnchorNodes[anchor])] = true;
    }
    std::size_t bestFrom = BotNavigationMap::kMaxNodes;
    std::size_t bestTo = BotNavigationMap::kMaxNodes;
    float bestDistance = std::numeric_limits<float>::infinity();
    for (std::size_t from = 0; from < map.nodeCount; ++from) {
      const std::size_t fromComponent = findComponent(findComponent, from);
      for (std::size_t to = from + 1U; to < map.nodeCount; ++to) {
        const std::size_t toComponent = findComponent(findComponent, to);
        if (fromComponent == toComponent || testedBridgePairs[from].test(to) ||
            (!componentHasAnchor[fromComponent] && !componentHasAnchor[toComponent])) {
          continue;
        }
        const float distance = distance3d(map.nodes[from].position, map.nodes[to].position);
        if (distance < bestDistance) {
          bestDistance = distance;
          bestFrom = from;
          bestTo = to;
        }
      }
    }
    if (bestFrom >= map.nodeCount || bestTo >= map.nodeCount) break;
    testedBridgePairs[bestFrom].set(bestTo);
    testedBridgePairs[bestTo].set(bestFrom);
    ++map.bridgeTraversalTrials;
    const auto bridgeKind = [&](std::size_t from, std::size_t to, BotNavLinkKind& kind) {
      const bool linearlyBlocked = linearlyBlockedForPlayer(arena, bounds,
        map.nodes[from].position, map.nodes[to].position);
      if (linearlyBlocked) ++map.bridgeBroadphaseRejects;
      if (!linearlyBlocked && canTraverse(arena, movement, bounds, map.nodes[from].position,
            map.nodes[to].position, false, 384)) {
        kind = std::fabs(map.nodes[from].position.z - map.nodes[to].position.z) <= 0.05F
          ? BotNavLinkKind::Walk : BotNavLinkKind::Step;
        return true;
      }
      if (std::fabs(map.nodes[from].position.z - map.nodes[to].position.z) > 0.05F &&
          canTraverse(arena, movement, bounds, map.nodes[from].position,
            map.nodes[to].position, true, 384)) {
        kind = BotNavLinkKind::Jump;
        return true;
      }
      return false;
    };
    BotNavLinkKind forwardKind = BotNavLinkKind::Walk;
    BotNavLinkKind reverseKind = BotNavLinkKind::Walk;
    if (!bridgeKind(bestFrom, bestTo, forwardKind)) {
      if (!std::isfinite(map.nearestRejectedBridgeDistance)) {
        map.nearestRejectedBridgeDistance = bestDistance;
      }
      continue;
    }
    if (map.bridgeTraversalTrials == kBridgeTraversalTrialLimit ||
        !bridgeKind(bestTo, bestFrom, reverseKind)) {
      if (!std::isfinite(map.nearestRejectedBridgeDistance)) {
        map.nearestRejectedBridgeDistance = bestDistance;
      }
      continue;
    }
    const std::size_t before = map.linkCount;
    addLink(bestFrom, bestTo, forwardKind);
    addLink(bestTo, bestFrom, reverseKind);
    if (map.linkCount > before) {
      map.bridgeLinkCount += map.linkCount - before;
      if (componentHasAnchor[findComponent(findComponent, bestFrom)] ||
          componentHasAnchor[findComponent(findComponent, bestTo)]) {
        ++map.bridgeAnchorMerges;
      }
      mergeComponents(bestFrom, bestTo);
    }
  }
  if (semanticAnchorCount > 0U) {
    std::array<bool, BotNavigationMap::kMaxNodes> countedComponents = {};
    for (std::size_t anchor = 0; anchor < semanticAnchorCount; ++anchor) {
      countedComponents[findComponent(findComponent, semanticAnchorNodes[anchor])] = true;
    }
    for (std::size_t node = 0; node < map.nodeCount; ++node) {
      map.remainingAnchorComponents += countedComponents[node] ? 1U : 0U;
    }
  }
  return map;
}

std::size_t nearestBotNavNode(const BotNavigationMap& map, Vec3 position) {
  return nearestNodeWithin(map, position, std::numeric_limits<float>::infinity());
}

void BotBrain::reset(std::uint32_t seed) {
  memory_ = {};
  for (Memory& memory : memory_) memory.ageSeconds = std::numeric_limits<float>::infinity();
  resourceMemory_ = {};
  for (ResourceMemory& memory : resourceMemory_) {
    memory.ageSeconds = std::numeric_limits<float>::infinity();
  }
  pathCount_ = 0;
  pathCursor_ = 0;
  lastWaypoint_ = BotNavigationMap::kMaxNodes;
  replanSeconds_ = 0.0F;
  reactionSeconds_ = 0.0F;
  aimVelocityYaw_ = 0.0F;
  aimVelocityPitch_ = 0.0F;
  aimBiasYaw_ = 0.0F;
  aimBiasPitch_ = 0.0F;
  aimBiasRefreshSeconds_ = 0.0F;
  strafeSeconds_ = 0.0F;
  stuckSampleSeconds_ = 0.0F;
  stuckRecoverySeconds_ = 0.0F;
  stuckSamplePosition_ = {};
  strafeDirection_ = 1;
  targetPlayerIndex_ = kNoAssignedPlayer;
  patrolNode_ = BotNavigationMap::kMaxNodes;
  carrierObjectiveDestination_ = {};
  const std::uint32_t identitySeed = seed == 0U ? 0xB07D0D6EU : seed;
  tacticsRandomState_ = mixSeed(identitySeed ^ 0x71C71C5U);
  movementRandomState_ = mixSeed(identitySeed ^ 0x4D4F5645U);
  aimRandomState_ = mixSeed(identitySeed ^ 0xA11CE55DU);
  // Traits are fixed for a bot identity. Their small bounds keep difficulty
  // as the dominant source of reaction, aim, and movement skill.
  std::uint32_t traitState = mixSeed(identitySeed ^ 0x7A17A17U);
  traits_.aggression = 0.90F + randomUnit(traitState) * 0.20F;
  traits_.risk = 0.90F + randomUnit(traitState) * 0.20F;
  traits_.preferredRangeBias = 0.92F + randomUnit(traitState) * 0.16F;
  traits_.movementCadenceBias = 0.90F + randomUnit(traitState) * 0.20F;
  traits_.reactionLatencyOffsetSeconds = -0.025F + randomUnit(traitState) * 0.050F;
  traits_.aimBiasScale = 0.90F + randomUnit(traitState) * 0.20F;
  hasCarrierObjectiveDestination_ = false;
  targetWasVisible_ = false;
  initialized_ = false;
}

const BotTraits& BotBrain::traits() const {
  return traits_;
}

std::uint64_t BotBrain::deterministicHash() const {
  std::uint64_t hash = 1469598103934665603ULL;
  const auto mix = [&](std::uint32_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  const auto mixFloat = [&](float value) { mix(std::bit_cast<std::uint32_t>(value)); };
  for (const Memory& memory : memory_) {
    mixFloat(memory.position.x); mixFloat(memory.position.y); mixFloat(memory.position.z);
    mixFloat(memory.velocity.x); mixFloat(memory.velocity.y); mixFloat(memory.velocity.z);
    mixFloat(memory.ageSeconds); mixFloat(memory.confidence);
    mix(memory.lastObservationServerTick); mix(memory.hasObservation); mix(memory.valid);
  }
  for (const ResourceMemory& memory : resourceMemory_) {
    mixFloat(memory.position.x); mixFloat(memory.position.y); mixFloat(memory.position.z);
    mix(static_cast<std::uint32_t>(memory.value)); mixFloat(memory.ageSeconds);
    mix(memory.available); mix(memory.valid);
  }
  for (const std::uint16_t node : path_) mix(node);
  mix(static_cast<std::uint32_t>(pathCount_)); mix(static_cast<std::uint32_t>(pathCursor_));
  mix(static_cast<std::uint32_t>(lastWaypoint_)); mixFloat(replanSeconds_);
  mixFloat(reactionSeconds_); mixFloat(aimVelocityYaw_); mixFloat(aimVelocityPitch_);
  mixFloat(aimBiasYaw_); mixFloat(aimBiasPitch_); mixFloat(aimBiasRefreshSeconds_);
  mixFloat(strafeSeconds_); mixFloat(stuckSampleSeconds_); mixFloat(stuckRecoverySeconds_);
  mixFloat(stuckSamplePosition_.x); mixFloat(stuckSamplePosition_.y); mixFloat(stuckSamplePosition_.z);
  mix(static_cast<std::uint32_t>(strafeDirection_)); mix(targetPlayerIndex_);
  mix(static_cast<std::uint32_t>(patrolNode_));
  mixFloat(carrierObjectiveDestination_.x); mixFloat(carrierObjectiveDestination_.y);
  mixFloat(carrierObjectiveDestination_.z); mix(hasCarrierObjectiveDestination_);
  mix(targetWasVisible_); mix(initialized_);
  mixFloat(traits_.aggression); mixFloat(traits_.risk); mixFloat(traits_.preferredRangeBias);
  mixFloat(traits_.movementCadenceBias); mixFloat(traits_.reactionLatencyOffsetSeconds);
  mixFloat(traits_.aimBiasScale);
  mix(tacticsRandomState_); mix(movementRandomState_); mix(aimRandomState_);
  return hash;
}

std::uint32_t BotBrain::randomU32(RandomStream stream) {
  switch (stream) {
  case RandomStream::Tactics: return nextRandom(tacticsRandomState_);
  case RandomStream::Movement: return nextRandom(movementRandomState_);
  case RandomStream::Aim: return nextRandom(aimRandomState_);
  }
  return nextRandom(tacticsRandomState_);
}

float BotBrain::randomFloat(
  RandomStream stream,
  float minValue,
  float maxValue
) {
  const float unit = static_cast<float>(randomU32(stream) >> 8U) /
    static_cast<float>(0x00ffffffU);
  return minValue + (maxValue - minValue) * unit;
}

Weapon BotBrain::chooseWeapon(
  const BotSenseFrame& sense,
  const BotCombatContext& context,
  float preferredRange,
  std::array<BotWeaponScore, kWeaponCount>& scores
) const {
  if (sense.forceWeapon) return sense.forcedWeapon;
  Weapon best = sense.selectedWeapon;
  float bestScore = -std::numeric_limits<float>::infinity();
  float currentScore = -std::numeric_limits<float>::infinity();
  for (std::size_t index = 0; index < kWeaponCount; ++index) {
    const BotWeaponSense& weapon = sense.weapons[index];
    const Weapon candidate = static_cast<Weapon>(index);
    scores[index] = scoreBotWeapon(weapon, context, preferredRange,
      candidate == sense.selectedWeapon);
    if (candidate == sense.selectedWeapon) currentScore = scores[index].total;
    if (scores[index].total > bestScore) {
      bestScore = scores[index].total;
      best = static_cast<Weapon>(index);
    }
  }
  // A usable current weapon stays selected unless the improvement clears a
  // real pullout/switch margin. This avoids per-tick weapon churn.
  constexpr float kSwitchHysteresis = 0.12F;
  if (sense.weapons[weaponIndex(sense.selectedWeapon)].usable &&
      currentScore + kSwitchHysteresis >= bestScore) {
    return sense.selectedWeapon;
  }
  return best;
}

bool BotBrain::planPath(
  const BotNavigationMap& navigation,
  Vec3 start,
  Vec3 target
) {
  pathCount_ = 0;
  pathCursor_ = 0;
  const std::size_t startNode = nearestBotNavNode(navigation, start);
  const std::size_t targetNode = nearestBotNavNode(navigation, target);
  if (startNode >= navigation.nodeCount || targetNode >= navigation.nodeCount) return false;

  std::array<float, BotNavigationMap::kMaxNodes> gScore = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> cameFrom = {};
  std::array<bool, BotNavigationMap::kMaxNodes> closed = {};
  gScore.fill(std::numeric_limits<float>::infinity());
  cameFrom.fill(std::numeric_limits<std::uint16_t>::max());
  gScore[startNode] = 0.0F;
  for (std::size_t step = 0; step < navigation.nodeCount; ++step) {
    std::size_t current = BotNavigationMap::kMaxNodes;
    float bestScore = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0; index < navigation.nodeCount; ++index) {
      if (closed[index]) continue;
      const float fScore = gScore[index] + distance3d(
        navigation.nodes[index].position, navigation.nodes[targetNode].position);
      if (fScore < bestScore) {
        bestScore = fScore;
        current = index;
      }
    }
    if (current >= navigation.nodeCount || !std::isfinite(bestScore)) break;
    if (current == targetNode) {
      std::array<std::uint16_t, BotNavigationMap::kMaxNodes> reversed = {};
      std::size_t count = 0;
      bool reachedStart = false;
      for (std::size_t node = current; count < reversed.size(); ) {
        reversed[count++] = static_cast<std::uint16_t>(node);
        if (node == startNode) {
          reachedStart = true;
          break;
        }
        const std::uint16_t previous = cameFrom[node];
        if (previous == std::numeric_limits<std::uint16_t>::max()) {
          count = 0;
          break;
        }
        node = previous;
      }
      // A truncated reconstruction is not a path. Reject it rather than
      // steering from an arbitrary middle node toward a hidden route start.
      if (!reachedStart) return false;
      while (count > 0 && pathCount_ < path_.size()) path_[pathCount_++] = reversed[--count];
      if (pathCount_ > 1 && path_[0] == startNode) pathCursor_ = 1;
      // Do not immediately reverse across the last completed edge when a
      // fresh lower-rate plan reaches the same goal. If the direct heading is
      // blocked, normal stuck recovery clears this shortcut and replans.
      if (pathCount_ > 2 && path_[pathCursor_] == lastWaypoint_) {
        ++pathCursor_;
      }
      return pathCount_ > 0U;
    }
    closed[current] = true;
    for (std::size_t linkIndex = 0; linkIndex < navigation.linkCount; ++linkIndex) {
      const BotNavLink& link = navigation.links[linkIndex];
      if (link.from != current || closed[link.to]) continue;
      const float tentative = gScore[current] + distance3d(
        navigation.nodes[current].position, navigation.nodes[link.to].position);
      if (tentative < gScore[link.to]) {
        gScore[link.to] = tentative;
        cameFrom[link.to] = static_cast<std::uint16_t>(current);
      }
    }
  }
  return false;
}

BotMotor BotBrain::tick(
  const BotSenseFrame& sense,
  const BotDifficultyProfile& profile,
  const BotNavigationMap& navigation
) {
  BotMotor output;
  UserCommand& command = output.command;
  command.viewYawRadians = sense.self.viewYawRadians;
  command.viewPitchRadians = sense.self.viewPitchRadians;
  command.planarAim = false;
  const float dt = std::max(0.0001F, sense.fixedDt);

  for (Memory& memory : memory_) {
    if (!memory.valid) continue;
    memory.ageSeconds += dt;
    memory.confidence = std::max(0.0F, 1.0F - memory.ageSeconds / profile.memorySeconds);
    if (memory.ageSeconds > profile.memorySeconds) memory.valid = false;
  }
  for (ResourceMemory& memory : resourceMemory_) {
    if (!memory.valid) continue;
    memory.ageSeconds += dt;
    if (memory.ageSeconds > std::max(1.50F, profile.memorySeconds)) {
      memory.valid = false;
    }
  }
  output.observedHealthResourceCount = sense.perceptionFresh ? sense.healthResourceCount : 0U;
  if (sense.perceptionFresh) {
    for (std::size_t index = 0; index < sense.healthResourceCount; ++index) {
      const BotHealthResourceSense& resource = sense.healthResources[index];
      if (resource.resourceIndex >= resourceMemory_.size()) continue;
      ResourceMemory& memory = resourceMemory_[resource.resourceIndex];
      memory.position = resource.position;
      memory.value = resource.value;
      memory.available = resource.available;
      memory.ageSeconds = 0.0F;
      memory.valid = true;
    }
  }
  std::size_t visibleTarget = kDuelPlayerCount;
  float visibleDistance = std::numeric_limits<float>::infinity();
  bool visibleTargetGrounded = false;
  bool visibleTargetSplashSurface = false;
  for (std::size_t index = 0; index < sense.visibleEnemyCount; ++index) {
    const BotObservedEnemy& enemy = sense.visibleEnemies[index];
    if (enemy.playerIndex >= kDuelPlayerCount) continue;
    Memory& memory = memory_[enemy.playerIndex];
    if (sense.perceptionFresh) {
      // The input has no target velocity. Derive it only when a later visible
      // sample arrives. Holding an old sighting therefore cannot react to a
      // hidden authoritative velocity change, and the first sighting has no lead.
      if (!memory.hasObservation) {
        memory.velocity = {};
      } else if (enemy.observationServerTick > memory.lastObservationServerTick) {
        const std::uint32_t elapsedTicks = enemy.observationServerTick -
          memory.lastObservationServerTick;
        const float elapsed = std::max(dt, static_cast<float>(elapsedTicks) * dt);
        Vec3 estimate = (enemy.position - memory.position) / elapsed;
        const float speed = length(estimate);
        if (speed > kMaximumObservedSpeed) estimate *= kMaximumObservedSpeed / speed;
        memory.velocity = estimate;
      }
      memory.position = enemy.position;
      memory.lastObservationServerTick = enemy.observationServerTick;
      memory.hasObservation = true;
      memory.ageSeconds = 0.0F;
      memory.confidence = 1.0F;
      memory.valid = true;
    }
    const float distance = horizontalDistance(sense.self.position, enemy.position);
    if (distance < visibleDistance) {
      visibleDistance = distance;
      visibleTarget = enemy.playerIndex;
      visibleTargetGrounded = enemy.onGround;
      visibleTargetSplashSurface = enemy.nearbySplashSurface;
    }
  }

  std::size_t rememberedTarget = kDuelPlayerCount;
  float rememberedDistance = std::numeric_limits<float>::infinity();
  if (visibleTarget == kDuelPlayerCount) {
    for (std::size_t index = 0; index < memory_.size(); ++index) {
      if (!memory_[index].valid) continue;
      const float distance = horizontalDistance(sense.self.position, memory_[index].position);
      if (distance < rememberedDistance) {
        rememberedDistance = distance;
        rememberedTarget = index;
      }
    }
  }
  const std::size_t target = visibleTarget < kDuelPlayerCount ? visibleTarget : rememberedTarget;
  const bool targetVisible = visibleTarget < kDuelPlayerCount && target == visibleTarget;
  if (target < kDuelPlayerCount) {
    output.targetPlayerIndex = static_cast<std::uint8_t>(target);
    output.lastKnownTargetPosition = memory_[target].position;
    output.targetMemoryAgeSeconds = memory_[target].ageSeconds;
  }
  if (!initialized_) {
    initialized_ = true;
    stuckSamplePosition_ = sense.self.position;
    targetPlayerIndex_ = kNoAssignedPlayer;
  }
  bool acquiredTarget = false;
  if (sense.perceptionFresh) {
    acquiredTarget = targetVisible &&
      (!targetWasVisible_ || targetPlayerIndex_ != target);
    if (acquiredTarget) {
      targetPlayerIndex_ = static_cast<std::uint8_t>(target);
      reactionSeconds_ = std::max(0.0F, randomFloat(RandomStream::Tactics,
        profile.reactionMinSeconds, profile.reactionMaxSeconds) +
        traits_.reactionLatencyOffsetSeconds);
    } else if (!targetVisible && target == kDuelPlayerCount) {
      targetPlayerIndex_ = kNoAssignedPlayer;
    }
    targetWasVisible_ = targetVisible;
  }
  // Keep the first acquisition tick in the stated reaction range. Reducing it
  // here would make every sampled delay one fixed tick shorter than its tune.
  if (!acquiredTarget) reactionSeconds_ = std::max(0.0F, reactionSeconds_ - dt);
  // Seeing a target refreshes memory immediately, but it cannot steer the
  // motor or aim until the sampled human reaction delay has elapsed. The
  // motor may keep turning and moving from the last sample at 125 Hz, while
  // firing requires a fresh LOS/FOV sample.
  const bool targetDecisionAllowed = target < kDuelPlayerCount &&
    reactionSeconds_ <= 0.0F;
  // Cached positions may guide the motor, but a held attack remains legal
  // only while the server has just revalidated that same known target through
  // the physical LOS/FOV cone. The Boolean does not refresh its observation.
  const bool targetCurrentlyVisible = sense.perceptionFresh ? targetVisible :
    (sense.attackTargetCurrentlyVisible &&
      sense.attackTargetPlayerIndex == static_cast<std::uint8_t>(target));
  const bool targetFireAllowed = targetCurrentlyVisible && targetDecisionAllowed;

  Vec3 movementGoal = sense.self.position;
  const bool carrierDelivery = sense.objective.carrying &&
    sense.objective.hasScoringPosition;
  if (carrierDelivery && (!hasCarrierObjectiveDestination_ ||
      distance3d(carrierObjectiveDestination_, sense.objective.scoringPosition) > 0.01F)) {
    // A new carrier base supersedes any chase or patrol route immediately.
    // Do not wait for that route's lower-rate replan timer before moving home.
    pathCount_ = 0;
    pathCursor_ = 0;
    replanSeconds_ = 0.0F;
    carrierObjectiveDestination_ = sense.objective.scoringPosition;
    hasCarrierObjectiveDestination_ = true;
  } else if (!carrierDelivery) {
    hasCarrierObjectiveDestination_ = false;
  }
  // Delivery comes before combat and recovery movement. A carrier can still
  // aim and fire after reacting, but it must not abandon its legal base goal.
  if (carrierDelivery) {
    movementGoal = sense.objective.scoringPosition;
    output.goal = BotGoalKind::Objective;
  } else if (sense.self.health < 45) {
    float bestDistance = std::numeric_limits<float>::infinity();
    for (const ResourceMemory& resource : resourceMemory_) {
      if (!resource.valid || !resource.available || resource.value <= 0) continue;
      const float distance = horizontalDistance(sense.self.position, resource.position);
      if (distance < bestDistance) {
        bestDistance = distance;
        movementGoal = resource.position;
        output.goal = BotGoalKind::RecoverHealth;
      }
    }
  }
  Vec3 combatTarget = sense.self.position;
  if (target < kDuelPlayerCount) {
    const Memory& memory = memory_[target];
    combatTarget = memory.position + memory.velocity * std::min(
      profile.predictionSeconds + memory.ageSeconds, 0.40F);
  }
  if (output.goal == BotGoalKind::Safe && targetDecisionAllowed) {
    movementGoal = combatTarget;
    output.goal = BotGoalKind::Chase;
  } else if (output.goal == BotGoalKind::Safe && sense.objective.active) {
    movementGoal = sense.objective.carrying && sense.objective.hasScoringPosition
      ? sense.objective.scoringPosition : sense.objective.position;
    output.goal = BotGoalKind::Objective;
  } else if (output.goal == BotGoalKind::Safe && navigation.nodeCount > 0U) {
    const bool reachedPatrol = patrolNode_ < navigation.nodeCount &&
      horizontalDistance(sense.self.position, navigation.nodes[patrolNode_].position) <=
        kNavReachRadius;
    if (patrolNode_ >= navigation.nodeCount || reachedPatrol) {
      const std::size_t nearest = nearestBotNavNode(navigation, sense.self.position);
      std::size_t candidateCount = 0;
      for (std::size_t index = 0; index < navigation.linkCount; ++index) {
        const BotNavLink& link = navigation.links[index];
        if (link.from != nearest || link.to == lastWaypoint_) continue;
        ++candidateCount;
      }
      if (candidateCount > 0U) {
        std::size_t selected = randomU32(RandomStream::Tactics) % candidateCount;
        for (std::size_t index = 0; index < navigation.linkCount; ++index) {
          const BotNavLink& link = navigation.links[index];
          if (link.from != nearest || link.to == lastWaypoint_) continue;
          if (selected-- == 0U) {
            patrolNode_ = link.to;
            break;
          }
        }
      } else {
        patrolNode_ = BotNavigationMap::kMaxNodes;
      }
    }
    if (patrolNode_ < navigation.nodeCount) {
      movementGoal = navigation.nodes[patrolNode_].position;
      output.goal = BotGoalKind::Explore;
    }
  }

  const float targetDistance = targetDecisionAllowed
    ? horizontalDistance(sense.self.position, combatTarget) : 0.0F;
  BotCombatContext combatContext;
  combatContext.targetDistance = targetDistance;
  combatContext.selfHealth = static_cast<int>(std::clamp(
    static_cast<float>(sense.self.health) / traits_.risk, 1.0F, 200.0F));
  if (targetDecisionAllowed) {
    const Vec3 toTarget = combatTarget - sense.self.position;
    const float horizontal = std::max(0.001F, std::hypot(toTarget.x, toTarget.y));
    const float desiredYaw = std::atan2(toTarget.y, toTarget.x);
    combatContext.angularErrorRadians = std::fabs(angleDeltaRadians(
      sense.self.viewYawRadians, desiredYaw));
    const Memory& memory = memory_[target];
    combatContext.targetLateralSpeed = std::fabs(
      memory.velocity.x * (-toTarget.y / horizontal) +
      memory.velocity.y * (toTarget.x / horizontal));
    combatContext.exposureAgeSeconds = memory.ageSeconds;
    combatContext.targetGrounded = visibleTargetGrounded;
    combatContext.nearbySplashSurface = visibleTargetSplashSurface;
  }
  command.weapon = targetDecisionAllowed || sense.forceWeapon
    ? chooseWeapon(sense, combatContext,
      profile.preferredRange * traits_.preferredRangeBias, output.weaponScores)
    : sense.selectedWeapon;
  output.selectedWeaponScore = output.weaponScores[weaponIndex(command.weapon)].total;
  const BotWeaponSense& activeWeapon = sense.weapons[weaponIndex(command.weapon)];
  command.zoomed = targetDecisionAllowed && command.weapon == Weapon::Railgun;

  Vec3 aimPosition = combatTarget;
  if (targetDecisionAllowed) {
    const Memory& memory = memory_[target];
    const float projectileLead = activeWeapon.projectileSpeed > 0.001F
      ? std::min(0.80F, targetDistance / activeWeapon.projectileSpeed)
      : 0.0F;
    // Both hitscan tracking and projectile lead use only the most recently
    // observed velocity. A remembered enemy can shape movement and aim, but
    // cannot enable fire after LOS has gone.
    aimPosition = memory.position + memory.velocity * std::min(
      profile.predictionSeconds + memory.ageSeconds + projectileLead, 0.80F);
  }
  Vec3 aimTarget = targetDecisionAllowed
    ? aimPoint(aimPosition, sense.self.halfHeight)
    : aimPoint(movementGoal, sense.self.halfHeight);
  const Vec3 aimDelta = aimTarget - (sense.self.position + Vec3{0.0F, 0.0F, 0.65F});
  if (targetDecisionAllowed || output.goal == BotGoalKind::Explore) {
    if (targetDecisionAllowed) {
      aimBiasRefreshSeconds_ -= dt;
    }
    if (targetDecisionAllowed && aimBiasRefreshSeconds_ <= 0.0F) {
      const float bias = profile.trackingErrorRadians * traits_.aimBiasScale;
      aimBiasYaw_ = randomFloat(RandomStream::Aim, -bias, bias);
      aimBiasPitch_ = randomFloat(RandomStream::Aim, -bias * 0.75F, bias * 0.75F);
      aimBiasRefreshSeconds_ = randomFloat(RandomStream::Aim, 0.25F, 0.65F);
    }
    const float desiredYaw = wrapRadians(std::atan2(aimDelta.y, aimDelta.x) +
      (targetDecisionAllowed ? aimBiasYaw_ : 0.0F));
    const float desiredPitch = std::clamp(std::atan2(aimDelta.z,
      std::hypot(aimDelta.x, aimDelta.y)) +
        (targetDecisionAllowed ? aimBiasPitch_ : 0.0F),
      -kMaxPitchRadians, kMaxPitchRadians);
    const float yawTargetVelocity = std::clamp(angleDeltaRadians(command.viewYawRadians, desiredYaw) / dt,
      -profile.maxTurnRadiansPerSecond, profile.maxTurnRadiansPerSecond);
    const float pitchTargetVelocity = std::clamp((desiredPitch - command.viewPitchRadians) / dt,
      -profile.maxTurnRadiansPerSecond, profile.maxTurnRadiansPerSecond);
    aimVelocityYaw_ = approach(aimVelocityYaw_, yawTargetVelocity,
      profile.turnAccelerationRadiansPerSecond2 * dt);
    aimVelocityPitch_ = approach(aimVelocityPitch_, pitchTargetVelocity,
      profile.turnAccelerationRadiansPerSecond2 * dt);
    const float yawStep = std::clamp(aimVelocityYaw_ * dt,
      -profile.maxTurnRadiansPerSecond * dt, profile.maxTurnRadiansPerSecond * dt);
    const float pitchStep = std::clamp(aimVelocityPitch_ * dt,
      -profile.maxTurnRadiansPerSecond * dt, profile.maxTurnRadiansPerSecond * dt);
    command.viewYawRadians = wrapRadians(command.viewYawRadians + yawStep);
    command.viewPitchRadians = std::clamp(command.viewPitchRadians + pitchStep,
      -kMaxPitchRadians, kMaxPitchRadians);
  }

  Vec3 moveTarget = movementGoal;
  // Visible combat permits direct movement. A known health or objective goal
  // still uses the graph, so a visible enemy cannot turn it into a wall run.
  const bool directTarget = output.goal == BotGoalKind::Chase && targetVisible &&
    targetDecisionAllowed;
  replanSeconds_ -= dt;
  if (!directTarget && output.goal != BotGoalKind::Safe && replanSeconds_ <= 0.0F) {
    const bool pathFound = planPath(navigation, sense.self.position, movementGoal);
    if (!pathFound) {
      // Never fall through to direct input when A* cannot bridge components.
      // The next deterministic replan may find a new patrol route; until then
      // ordinary input stays neutral instead of pushing into a wall.
      moveTarget = sense.self.position;
    }
    replanSeconds_ = profile.planningIntervalSeconds * traits_.movementCadenceBias +
      randomFloat(RandomStream::Tactics, 0.0F, 0.12F);
  }
  if (!directTarget && pathCursor_ < pathCount_) {
    while (pathCursor_ < pathCount_ && distance3d(sense.self.position,
      navigation.nodes[path_[pathCursor_]].position) <= kNavReachRadius) {
      lastWaypoint_ = path_[pathCursor_++];
    }
    if (pathCursor_ < pathCount_) {
      moveTarget = navigation.nodes[path_[pathCursor_]].position;
      output.waypointNode = path_[pathCursor_];
    } else {
      moveTarget = sense.self.position;
    }
  } else if (!directTarget) {
    moveTarget = sense.self.position;
  }

  Vec3 moveDelta = moveTarget - sense.self.position;
  moveDelta.z = 0.0F;
  const float moveLength = length(moveDelta);
  const bool wantsMovement = output.goal != BotGoalKind::Safe && moveLength > 0.20F;
  stuckSampleSeconds_ += dt;
  if (wantsMovement && stuckSampleSeconds_ >= 0.50F) {
    if (horizontalDistance(sense.self.position, stuckSamplePosition_) < 0.12F) {
      stuckRecoverySeconds_ = 0.55F;
      strafeDirection_ = -strafeDirection_;
      pathCount_ = 0;
      pathCursor_ = 0;
      replanSeconds_ = 0.0F;
    }
    stuckSamplePosition_ = sense.self.position;
    stuckSampleSeconds_ = 0.0F;
  }
  stuckRecoverySeconds_ = std::max(0.0F, stuckRecoverySeconds_ - dt);
  output.recoveredFromStuck = stuckRecoverySeconds_ > 0.0F;

  if (sense.dodgeOverride && !sense.standstill) {
    // Keep the old bot_dodge training control an explicit motor override:
    // it supplies a stable side-step and does not blend chase steering into
    // the input a user asked to inspect.
    strafeSeconds_ -= dt;
    if (strafeSeconds_ <= 0.0F) {
      strafeDirection_ = (randomU32(RandomStream::Movement) & 1U) == 0U ? -1 : 1;
      strafeSeconds_ = randomFloat(RandomStream::Movement,
        static_cast<float>(sense.dodgeMinIntervalMs) / 1000.0F,
        static_cast<float>(sense.dodgeMaxIntervalMs) / 1000.0F
      );
    }
    command.rightMove = static_cast<float>(strafeDirection_);
  } else if (!sense.standstill && wantsMovement) {
    const Vec3 direction = moveDelta / std::max(moveLength, 0.001F);
    const Vec3 forward = {std::cos(command.viewYawRadians), std::sin(command.viewYawRadians), 0.0F};
    // Match Movement::yawRight exactly. Using the opposite side vector here
    // makes a target-relative command drift away from its chosen waypoint.
    const Vec3 right = {std::sin(command.viewYawRadians),
      -std::cos(command.viewYawRadians), 0.0F};
    command.forwardMove = std::clamp(dot(direction, forward), -1.0F, 1.0F);
    command.rightMove = std::clamp(dot(direction, right), -1.0F, 1.0F);
    if (output.goal == BotGoalKind::Chase && targetVisible && targetDecisionAllowed) {
      strafeSeconds_ -= dt;
      if (strafeSeconds_ <= 0.0F) {
        strafeDirection_ = (randomU32(RandomStream::Movement) & 1U) == 0U ? -1 : 1;
        strafeSeconds_ = randomFloat(RandomStream::Movement, 0.35F, 0.80F);
      }
      const float preferredRange = profile.preferredRange * traits_.preferredRangeBias;
      const float rangeError = targetDistance - std::max(1.0F,
        std::min(preferredRange, activeWeapon.effectiveRange * 0.60F));
      command.forwardMove = std::clamp(command.forwardMove + rangeError * 0.10F, -1.0F, 1.0F);
      command.rightMove = std::clamp(command.rightMove +
        static_cast<float>(strafeDirection_) * profile.strafeStrength, -1.0F, 1.0F);
      if (sense.self.onGround && randomFloat(RandomStream::Movement, 0.0F, 1.0F) <
          0.012F * traits_.aggression) command.jump = true;
      if (sense.self.dashReady && randomFloat(RandomStream::Movement, 0.0F, 1.0F) <
          profile.dashChancePerSecond * dt * traits_.aggression) {
        command.dash = true;
      }
    }
    if (output.waypointNode < navigation.nodeCount && pathCursor_ > 0) {
      const BotNavLinkKind kind = linkKind(navigation, path_[pathCursor_ - 1U],
        output.waypointNode);
      command.jump = command.jump || kind == BotNavLinkKind::Jump;
    }
  }
  if (output.recoveredFromStuck && !sense.standstill) {
    command.rightMove = static_cast<float>(strafeDirection_);
    command.jump = sense.self.onGround;
  }

  const float yawError = target < kDuelPlayerCount
    ? std::fabs(angleDeltaRadians(command.viewYawRadians,
        std::atan2(aimDelta.y, aimDelta.x) + aimBiasYaw_)) : 0.0F;
  const float pitchError = target < kDuelPlayerCount
    ? std::fabs(command.viewPitchRadians - std::atan2(aimDelta.z,
        std::hypot(aimDelta.x, aimDelta.y)) - aimBiasPitch_) : 0.0F;
  const float alignment = std::clamp(1.0F - std::max(yawError, pitchError) / 0.12F,
    0.0F, 1.0F);
  const float estimatedHitChance = alignment *
    (1.0F - output.weaponScores[weaponIndex(command.weapon)].projectileDifficulty);
  const float minimumHitChance = profile.maxTurnRadiansPerSecond < 2.0F ? 0.58F :
    profile.maxTurnRadiansPerSecond < 4.0F ? 0.66F : 0.74F;
  if (!sense.combatEnabled) {
    output.noFireReason = BotNoFireReason::Disabled;
  } else if (!targetFireAllowed && !targetCurrentlyVisible) {
    output.noFireReason = BotNoFireReason::NoVisibleTarget;
  } else if (reactionSeconds_ > 0.0F) {
    output.noFireReason = BotNoFireReason::Reaction;
  } else if (!activeWeapon.usable) {
    output.noFireReason = BotNoFireReason::WeaponUnavailable;
  } else if (yawError > profile.fireToleranceRadians || pitchError > profile.fireToleranceRadians ||
             estimatedHitChance < minimumHitChance) {
    output.noFireReason = BotNoFireReason::Turning;
  } else {
    command.attack = true;
    output.noFireReason = BotNoFireReason::None;
  }
  if (sense.standstill) {
    command.forwardMove = 0.0F;
    command.rightMove = 0.0F;
    command.jump = false;
    command.dash = false;
  }
  return output;
}

} // namespace lg
