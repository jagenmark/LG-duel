#include "server/BotAi.hpp"

#include "sim/Arena.hpp"
#include "sim/Movement.hpp"
#include "sim/PlayerState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace lg {
namespace {

constexpr float kPi = 3.14159265359F;
constexpr float kHalfPi = kPi * 0.5F;
constexpr float kMaxPitchRadians = kHalfPi - 0.01F;
constexpr float kNavSpacing = 1.25F;
constexpr float kNavReachRadius = 0.55F;
constexpr float kNavLinkSpacingFactor = 1.55F;
constexpr float kBotNavDt = 1.0F / 125.0F;

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

// A nav grid node must rest on the same kind of surface a normal player uses.
// The semantic teleport destination is deliberately exempt: teleport movement
// writes that exact authored position, even when its next normal tick lands.
[[nodiscard]] bool groundedNodePosition(
  const Arena& arena,
  CollisionBounds bounds,
  Vec3 hint,
  Vec3& position
) {
  PlayerState player;
  player.position = hint;
  player.bounds = bounds;
  if (playerPositionSolid(arena, player, hint)) return false;
  const CollisionResult landing = resolvePlayerArenaCollision(
    arena, player, hint - Vec3{0.0F, 0.0F, 0.35F}, {0.0F, 0.0F, -1.0F}
  );
  if (!landing.onGround || playerPositionSolid(arena, player, landing.position)) {
    return false;
  }
  position = landing.position;
  return true;
}

[[nodiscard]] bool canTraverse(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds,
  Vec3 from,
  Vec3 to,
  bool jump
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
      kBotNavDt * 2.5F)),
    12,
    600
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
      .fireToleranceRadians = 0.040F,
      .predictionSeconds = 0.04F,
      .memorySeconds = 1.20F,
      .planningIntervalSeconds = 0.75F,
      .targetFovDegrees = 100.0F,
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
      .fireToleranceRadians = 0.060F,
      .predictionSeconds = 0.12F,
      .memorySeconds = 1.80F,
      .planningIntervalSeconds = 0.45F,
      .targetFovDegrees = 108.0F,
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
      .fireToleranceRadians = 0.075F,
      .predictionSeconds = 0.20F,
      .memorySeconds = 2.40F,
      .planningIntervalSeconds = 0.30F,
      .targetFovDegrees = 116.0F,
      .preferredRange = 9.0F,
      .strafeStrength = 0.70F,
      .dashChancePerSecond = 0.08F,
    };
  case BotAttackMode::Off:
    break;
  }
  return {};
}

BotNavigationMap buildBotNavigationMap(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds
) {
  BotNavigationMap map;
  auto addGroundedNode = [&](Vec3 hint) {
    if (map.nodeCount == BotNavigationMap::kMaxNodes) return;
    Vec3 position = {};
    if (!groundedNodePosition(arena, bounds, hint, position) ||
        nearestNodeWithin(map, position, 0.35F) < map.nodeCount) {
      return;
    }
    map.nodes[map.nodeCount++].position = position;
  };
  auto addExactNode = [&](Vec3 position) {
    if (map.nodeCount == BotNavigationMap::kMaxNodes ||
        !canStandAt(arena, bounds, position) ||
        nearestNodeWithin(map, position, 0.35F) < map.nodeCount) {
      return;
    }
    map.nodes[map.nodeCount++].position = position;
  };

  // Semantic points remain part of the same generated graph. They never name
  // a map or bypass collision, and make pads, teleports, and pickups reachable.
  for (std::size_t index = 0; index < arena.spawnCount; ++index) {
    Vec3 position = arena.spawnPositions[index];
    position.z += bounds.halfHeight;
    addGroundedNode(position);
  }
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    Vec3 position = arena.healthPickups[index].position;
    position.z = std::max(position.z, arena.min.z) + bounds.halfHeight;
    addGroundedNode(position);
  }
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const ArenaJumpPad& pad = arena.jumpPads[index];
    addGroundedNode({(pad.min.x + pad.max.x) * 0.5F, (pad.min.y + pad.max.y) * 0.5F,
      std::max(pad.min.z, arena.min.z) + bounds.halfHeight});
    const Vec3 target = pad.hasTarget
      ? pad.targetPosition
      : Vec3{pad.max.x, pad.max.y, pad.max.z + bounds.halfHeight};
    addExactNode(target);
  }
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    const ArenaTeleport& teleport = arena.teleports[index];
    addGroundedNode({(teleport.min.x + teleport.max.x) * 0.5F,
      (teleport.min.y + teleport.max.y) * 0.5F,
      std::max(teleport.min.z, arena.min.z) + bounds.halfHeight});
    // Teleports use the authored player-origin exactly. Do not add the player
    // half height: Movement::applyTeleports writes destination unchanged.
    addExactNode(teleport.destination);
  }

  struct GroundLevel {
    float height = 0.0F;
    Vec3 representative = {};
  };
  std::array<GroundLevel, BotNavigationMap::kMaxNodes> groundLevels = {};
  std::size_t groundLevelCount = 0;
  const auto addGroundLevel = [&](float level, Vec3 representative) {
    if (groundLevelCount == groundLevels.size() || level < arena.min.z ||
        level > arena.max.z - bounds.halfHeight) return;
    for (std::size_t index = 0; index < groundLevelCount; ++index) {
      if (std::fabs(groundLevels[index].height - level) <= 0.05F) return;
    }
    groundLevels[groundLevelCount++] = {level, representative};
  };
  addGroundLevel(arena.min.z, {
    (arena.min.x + arena.max.x) * 0.5F,
    (arena.min.y + arena.max.y) * 0.5F,
    arena.min.z,
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
  const float startX = arena.min.x + bounds.radius + 0.15F;
  const float startY = arena.min.y + bounds.radius + 0.15F;
  const float endX = arena.max.x - bounds.radius - 0.15F;
  const float endY = arena.max.y - bounds.radius - 0.15F;
  // Keep at least half the node budget for a connected grid. Every selected
  // level receives an anchored, collision-checked surface sample first. This
  // retains all normal maps' levels and deterministically spreads coverage
  // over unusually tall maps instead of silently dropping level nine onward.
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
  const float gridSpacing = std::max(kNavSpacing, std::sqrt(
    mapArea * static_cast<float>(std::max<std::size_t>(1U, sampledLevelCount)) /
    remainingNodes
  ));
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

  auto addLink = [&](std::size_t from, std::size_t to, BotNavLinkKind kind) {
    if (from >= map.nodeCount || to >= map.nodeCount || from == to ||
        map.linkCount == BotNavigationMap::kMaxLinks) return;
    for (std::size_t index = 0; index < map.linkCount; ++index) {
      if (map.links[index].from == from && map.links[index].to == to) return;
    }
    map.links[map.linkCount++] = BotNavLink{
      static_cast<std::uint16_t>(from), static_cast<std::uint16_t>(to), kind};
  };

  // The reach derives from the samples we actually emitted. It covers a
  // neighboring diagonal cell while every candidate still has to pass normal
  // player movement, so coarse large-map samples cannot form a ray-only path.
  const float linkDistance = gridSpacing * kNavLinkSpacingFactor;
  for (std::size_t from = 0; from < map.nodeCount; ++from) {
    for (std::size_t to = 0; to < map.nodeCount; ++to) {
      if (from == to || distance3d(map.nodes[from].position, map.nodes[to].position) >
          linkDistance) continue;
      if (canTraverse(arena, movement, bounds, map.nodes[from].position,
            map.nodes[to].position, false)) {
        addLink(from, to, std::fabs(map.nodes[from].position.z - map.nodes[to].position.z) <=
          0.05F ? BotNavLinkKind::Walk : BotNavLinkKind::Step);
      } else if (canTraverse(arena, movement, bounds, map.nodes[from].position,
                   map.nodes[to].position, true)) {
        addLink(from, to, BotNavLinkKind::Jump);
      }
    }
  }
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const ArenaJumpPad& pad = arena.jumpPads[index];
    const Vec3 entry = {(pad.min.x + pad.max.x) * 0.5F, (pad.min.y + pad.max.y) * 0.5F,
      std::max(pad.min.z, arena.min.z) + bounds.halfHeight};
    const Vec3 exit = pad.hasTarget
      ? pad.targetPosition
      : Vec3{pad.max.x, pad.max.y, pad.max.z + bounds.halfHeight};
    const std::size_t from = nearestNodeWithin(map, entry, kNavSpacing);
    const std::size_t to = nearestNodeWithin(map, exit, kNavSpacing * 2.0F);
    if (from < map.nodeCount && to < map.nodeCount && canTraverse(
          arena, movement, bounds, map.nodes[from].position, map.nodes[to].position, false)) {
      addLink(from, to, BotNavLinkKind::JumpPad);
    }
  }
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    const ArenaTeleport& teleport = arena.teleports[index];
    const Vec3 entry = {(teleport.min.x + teleport.max.x) * 0.5F,
      (teleport.min.y + teleport.max.y) * 0.5F,
      std::max(teleport.min.z, arena.min.z) + bounds.halfHeight};
    const Vec3 exit = teleport.destination;
    const std::size_t from = nearestNodeWithin(map, entry, kNavSpacing);
    const std::size_t to = nearestNodeWithin(map, exit, kNavSpacing * 2.0F);
    if (from < map.nodeCount && to < map.nodeCount && canTraverse(
          arena, movement, bounds, map.nodes[from].position, map.nodes[to].position, false)) {
      addLink(from, to, BotNavLinkKind::Teleport);
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
  randomState_ = seed == 0U ? 0xB07D0D6EU : seed;
  hasCarrierObjectiveDestination_ = false;
  targetWasVisible_ = false;
  initialized_ = false;
}

std::uint32_t BotBrain::randomU32() {
  std::uint32_t value = randomState_;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  randomState_ = value == 0U ? 0xB07D0D6EU : value;
  return randomState_;
}

float BotBrain::randomFloat(float minValue, float maxValue) {
  const float unit = static_cast<float>(randomU32() >> 8U) /
    static_cast<float>(0x00ffffffU);
  return minValue + (maxValue - minValue) * unit;
}

Weapon BotBrain::chooseWeapon(const BotSenseFrame& sense, float targetDistance) const {
  if (sense.forceWeapon) return sense.forcedWeapon;
  Weapon best = sense.selectedWeapon;
  float bestScore = -std::numeric_limits<float>::infinity();
  for (std::size_t index = 0; index < kWeaponCount; ++index) {
    const BotWeaponSense& weapon = sense.weapons[index];
    if (!weapon.usable) continue;
    const float range = std::max(1.0F, weapon.effectiveRange);
    float score = 1.0F - std::min(1.0F, std::fabs(targetDistance - range * 0.45F) / range);
    if (weapon.projectileSpeed > 0.0F) score += std::min(0.2F, weapon.projectileSpeed / 250.0F);
    if (weapon.splash && targetDistance < 2.5F) score -= 0.45F;
    if (static_cast<Weapon>(index) == sense.selectedWeapon) score += 0.06F;
    if (score > bestScore) {
      bestScore = score;
      best = static_cast<Weapon>(index);
    }
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
  output.observedHealthResourceCount = sense.healthResourceCount;
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
  std::size_t visibleTarget = kDuelPlayerCount;
  float visibleDistance = std::numeric_limits<float>::infinity();
  for (std::size_t index = 0; index < sense.visibleEnemyCount; ++index) {
    const BotObservedEnemy& enemy = sense.visibleEnemies[index];
    if (enemy.playerIndex >= kDuelPlayerCount) continue;
    Memory& memory = memory_[enemy.playerIndex];
    memory.position = enemy.position;
    memory.velocity = enemy.velocity;
    memory.ageSeconds = 0.0F;
    memory.confidence = 1.0F;
    memory.valid = true;
    const float distance = horizontalDistance(sense.self.position, enemy.position);
    if (distance < visibleDistance) {
      visibleDistance = distance;
      visibleTarget = enemy.playerIndex;
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
  const bool acquiredTarget = targetVisible &&
    (!targetWasVisible_ || targetPlayerIndex_ != target);
  if (acquiredTarget) {
    targetPlayerIndex_ = static_cast<std::uint8_t>(target);
    reactionSeconds_ = randomFloat(profile.reactionMinSeconds, profile.reactionMaxSeconds);
  } else if (!targetVisible && target == kDuelPlayerCount) {
    targetPlayerIndex_ = kNoAssignedPlayer;
  }
  // Keep the first acquisition tick in the stated reaction range. Reducing it
  // here would make every sampled delay one fixed tick shorter than its tune.
  if (!acquiredTarget) reactionSeconds_ = std::max(0.0F, reactionSeconds_ - dt);
  targetWasVisible_ = targetVisible;
  // Seeing a target refreshes memory immediately, but it cannot steer the
  // motor or aim until the sampled human reaction delay has elapsed.
  const bool targetDecisionAllowed = target < kDuelPlayerCount && reactionSeconds_ <= 0.0F;

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
        std::size_t selected = randomU32() % candidateCount;
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
  command.weapon = targetDecisionAllowed || sense.forceWeapon
    ? chooseWeapon(sense, targetDistance) : sense.selectedWeapon;
  const BotWeaponSense& activeWeapon = sense.weapons[weaponIndex(command.weapon)];

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
      aimBiasYaw_ = randomFloat(-profile.trackingErrorRadians, profile.trackingErrorRadians);
      aimBiasPitch_ = randomFloat(-profile.trackingErrorRadians * 0.75F,
        profile.trackingErrorRadians * 0.75F);
      aimBiasRefreshSeconds_ = randomFloat(0.25F, 0.65F);
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
    replanSeconds_ = profile.planningIntervalSeconds + randomFloat(0.0F, 0.12F);
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
      strafeDirection_ = (randomU32() & 1U) == 0U ? -1 : 1;
      strafeSeconds_ = randomFloat(
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
        strafeDirection_ = (randomU32() & 1U) == 0U ? -1 : 1;
        strafeSeconds_ = randomFloat(0.35F, 0.80F);
      }
      const float rangeError = targetDistance - std::max(1.0F,
        std::min(profile.preferredRange, activeWeapon.effectiveRange * 0.60F));
      command.forwardMove = std::clamp(command.forwardMove + rangeError * 0.10F, -1.0F, 1.0F);
      command.rightMove = std::clamp(command.rightMove +
        static_cast<float>(strafeDirection_) * profile.strafeStrength, -1.0F, 1.0F);
      if (sense.self.onGround && randomFloat(0.0F, 1.0F) < 0.012F) command.jump = true;
      if (sense.self.dashReady && randomFloat(0.0F, 1.0F) < profile.dashChancePerSecond * dt) {
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
  if (!sense.combatEnabled) {
    output.noFireReason = BotNoFireReason::Disabled;
  } else if (!targetVisible) {
    output.noFireReason = BotNoFireReason::NoVisibleTarget;
  } else if (reactionSeconds_ > 0.0F) {
    output.noFireReason = BotNoFireReason::Reaction;
  } else if (!activeWeapon.usable) {
    output.noFireReason = BotNoFireReason::WeaponUnavailable;
  } else if (yawError > profile.fireToleranceRadians || pitchError > profile.fireToleranceRadians) {
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
