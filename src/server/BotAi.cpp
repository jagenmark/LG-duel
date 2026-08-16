#include "server/BotAi.hpp"

#include "sim/Arena.hpp"
#include "sim/Movement.hpp"
#include "sim/PlayerState.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <vector>

namespace lg {
namespace {

constexpr float kPi = 3.14159265359F;
constexpr float kHalfPi = kPi * 0.5F;
constexpr float kMaxPitchRadians = kHalfPi - 0.01F;
constexpr float kNavSpacing = 1.25F;
constexpr float kNavReachRadius = 0.55F;
constexpr float kBotNavDt = 1.0F / 125.0F;
constexpr float kCommonBotTargetFovDegrees = 108.0F;
constexpr float kMaximumObservedSpeed = 60.0F;
// Retain a still-visible opponent unless a challenger is clearly closer.
// The absolute floor protects close-range fights; the relative term scales
// the commitment margin at long range.
constexpr float kTargetSwitchMinimumDistanceAdvantage = 0.75F;
constexpr float kTargetSwitchRelativeDistanceAdvantage = 0.12F;
constexpr int kHealthRecoveryThreshold = 45;
constexpr float kMinimumHealthResourceMemorySeconds = 1.50F;
constexpr float kHealthRouteCostBias = 1.0F;
constexpr float kHealthResourceSwitchUtilityMultiplier = 1.20F;

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

[[nodiscard]] bool playerTouchesKillVolume(
  const Arena& arena,
  CollisionBounds bounds,
  Vec3 position
) {
  for (std::size_t index = 0; index < arena.killVolumeCount; ++index) {
    const ArenaKillVolume& volume = arena.killVolumes[index];
    if (playerTouchesTriggerVolume(bounds, position, volume.min, volume.max)) return true;
  }
  return false;
}

[[nodiscard]] bool canStandAt(
  const Arena& arena,
  CollisionBounds bounds,
  Vec3 position
) {
  PlayerState player;
  player.position = position;
  player.bounds = bounds;
  return !playerPositionSolid(arena, player, position) &&
    !playerTouchesKillVolume(arena, bounds, position);
}

// Keep this geometric precondition in step with Movement.cpp's trigger test.
// It only chooses legal candidate origins; simulateMovement still proves that
// the next ordinary tick activates the trigger and reaches a legal landing.
[[nodiscard]] bool playerWouldOverlapTrigger(
  CollisionBounds bounds,
  Vec3 position,
  Vec3 minimum,
  Vec3 maximum
) {
  const float closestX = std::clamp(position.x, minimum.x, maximum.x);
  const float closestY = std::clamp(position.y, minimum.y, maximum.y);
  const float deltaX = position.x - closestX;
  const float deltaY = position.y - closestY;
  return (deltaX * deltaX) + (deltaY * deltaY) <= bounds.radius * bounds.radius &&
    position.z + bounds.halfHeight >= minimum.z &&
    position.z - bounds.halfHeight <= maximum.z;
}

// A failed health anchor normally means sampling or movement could not find a
// route. This stricter check identifies the different authored-data fault:
// one real collision primitive contains every legal player center that could
// overlap the pickup. It uses the same player-expanded brush planes as
// playerPositionSolid, with the pickup touch cylinder as the tested volume.
[[nodiscard]] bool healthTouchVolumeFullyOccluded(
  const Arena& arena,
  CollisionBounds bounds,
  const ArenaHealthPickup& pickup,
  std::uint32_t& proofCells
) {
  constexpr float kCollisionEpsilon = 0.0001F;
  const float touchRadius = bounds.radius + kHealthPickupTouchRadius;
  const float touchHalfHeight = bounds.halfHeight + kHealthPickupTouchHalfHeight;
  const auto wallCoversCell = [&](const ArenaWall& wall, Vec3 minimum, Vec3 maximum) {
    // This considers the center volume while airborne. A ground-state step
    // exception could only make a low wall less occluding, so it cannot form
    // a proof here.
    return wall.min.x - bounds.radius < minimum.x + kCollisionEpsilon &&
      wall.max.x + bounds.radius > maximum.x - kCollisionEpsilon &&
      wall.min.y - bounds.radius < minimum.y + kCollisionEpsilon &&
      wall.max.y + bounds.radius > maximum.y - kCollisionEpsilon &&
      wall.min.z - bounds.halfHeight < minimum.z + kCollisionEpsilon &&
      wall.max.z + bounds.halfHeight > maximum.z - kCollisionEpsilon;
  };
  const auto brushCoversCell = [&](const ArenaBrush& brush, Vec3 minimum, Vec3 maximum) {
    if (brush.min.x - bounds.radius >= minimum.x + kCollisionEpsilon ||
        brush.max.x + bounds.radius <= maximum.x - kCollisionEpsilon ||
        brush.min.y - bounds.radius >= minimum.y + kCollisionEpsilon ||
        brush.max.y + bounds.radius <= maximum.y - kCollisionEpsilon ||
        brush.min.z - bounds.halfHeight >= minimum.z + kCollisionEpsilon ||
        brush.max.z + bounds.halfHeight <= maximum.z - kCollisionEpsilon) {
      return false;
    }
    const Vec3 center = (minimum + maximum) * 0.5F;
    const Vec3 halfExtent = (maximum - minimum) * 0.5F;
    for (std::uint8_t faceIndex = 0; faceIndex < brush.faceCount; ++faceIndex) {
      const ArenaBrushFace& face = brush.faces[faceIndex];
      const float normalXY = std::sqrt((face.normal.x * face.normal.x) +
        (face.normal.y * face.normal.y));
      const float playerSupport = bounds.radius * normalXY +
        bounds.halfHeight * std::fabs(face.normal.z);
      const float cellSupport = (std::fabs(face.normal.x) * halfExtent.x) +
        (std::fabs(face.normal.y) * halfExtent.y) +
        (std::fabs(face.normal.z) * halfExtent.z);
      if (dot(face.normal, center) + cellSupport >=
          face.distance + playerSupport - kCollisionEpsilon) return false;
    }
    return true;
  };
  constexpr float kCellSize = 0.10F;
  const std::size_t horizontalCells = static_cast<std::size_t>(std::ceil(
    (touchRadius * 2.0F) / kCellSize
  ));
  const std::size_t verticalCells = static_cast<std::size_t>(std::ceil(
    (touchHalfHeight * 2.0F) / kCellSize
  ));
  proofCells = 0;
  for (std::size_t z = 0; z < verticalCells; ++z) {
    const float z0 = pickup.position.z - touchHalfHeight + static_cast<float>(z) * kCellSize;
    const float z1 = std::min(pickup.position.z + touchHalfHeight, z0 + kCellSize);
    for (std::size_t y = 0; y < horizontalCells; ++y) {
      const float y0 = pickup.position.y - touchRadius + static_cast<float>(y) * kCellSize;
      const float y1 = std::min(pickup.position.y + touchRadius, y0 + kCellSize);
      for (std::size_t x = 0; x < horizontalCells; ++x) {
        const float x0 = pickup.position.x - touchRadius + static_cast<float>(x) * kCellSize;
        const float x1 = std::min(pickup.position.x + touchRadius, x0 + kCellSize);
        const float closestX = std::clamp(pickup.position.x, x0, x1);
        const float closestY = std::clamp(pickup.position.y, y0, y1);
        const float dx = closestX - pickup.position.x;
        const float dy = closestY - pickup.position.y;
        if ((dx * dx) + (dy * dy) > touchRadius * touchRadius) continue;
        const Vec3 minimum = {x0, y0, z0};
        const Vec3 maximum = {x1, y1, z1};
        bool covered = false;
        for (std::size_t index = 0; index < arena.wallCount && !covered; ++index) {
          covered = wallCoversCell(arena.walls[index], minimum, maximum);
        }
        for (std::size_t index = 0; index < arena.brushCount && !covered; ++index) {
          covered = brushCoversCell(arena.brushes[index], minimum, maximum);
        }
        // Every legal point lies in one of these cells. Requiring a single
        // real primitive to cover each full cell makes this a conservative
        // collision proof, not a sampled route or an anchor exemption.
        if (!covered) return false;
        ++proofCells;
      }
    }
  }
  return proofCells > 0U;
}

[[nodiscard]] bool findFreeHealthTouchCenter(
  const Arena& arena,
  CollisionBounds bounds,
  const ArenaHealthPickup& pickup,
  std::uint32_t& proofCenters,
  Vec3& firstFreeCenter
) {
  // This diagnostic records only an exact legal touch center that the server
  // collision predicate accepts. Exhausting the raster proves nothing, so it
  // never labels a pickup invalid or changes anchor eligibility.
  constexpr float kSpacing = 0.05F;
  const float touchRadius = bounds.radius + kHealthPickupTouchRadius;
  const float touchHalfHeight = bounds.halfHeight + kHealthPickupTouchHalfHeight;
  const int horizontalSteps = static_cast<int>(std::ceil(touchRadius / kSpacing));
  const int verticalSteps = static_cast<int>(std::ceil(touchHalfHeight / kSpacing));
  PlayerState player;
  player.bounds = bounds;
  proofCenters = 0;
  for (int z = -verticalSteps; z <= verticalSteps; ++z) {
    const float offsetZ = static_cast<float>(z) * kSpacing;
    if (offsetZ < -touchHalfHeight || offsetZ > touchHalfHeight) continue;
    for (int y = -horizontalSteps; y <= horizontalSteps; ++y) {
      const float offsetY = static_cast<float>(y) * kSpacing;
      for (int x = -horizontalSteps; x <= horizontalSteps; ++x) {
        const float offsetX = static_cast<float>(x) * kSpacing;
        const Vec3 center = pickup.position + Vec3{offsetX, offsetY, offsetZ};
        if (playerTouchesHealthPickup(bounds, center, pickup) &&
            !playerPositionSolid(arena, player, center) &&
            !playerTouchesKillVolume(arena, bounds, center)) {
          firstFreeCenter = center;
          return true;
        }
        ++proofCenters;
      }
    }
  }
  return false;
}

// A cheap player-bounds sweep filters links that a straight walking command
// plainly cannot enter. It never accepts a link: every surviving edge still
// runs the full fixed-step movement proof below. Kill volumes stay out of this
// chord test because a jump or drop can safely clear one before the proof ends.
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
  // Long local rays first sample the player capsule along the segment. This
  // rejects an obvious wall before the fixed-step proof spends up to 128
  // movement ticks walking into it. It never accepts an edge by itself.
  const float distance = distance3d(from, to);
  const std::size_t samples = std::clamp<std::size_t>(
    static_cast<std::size_t>(std::ceil(distance / (kNavSpacing * 1.5F))), 1U, 8U
  );
  for (std::size_t sample = 1U; sample < samples; ++sample) {
    const float fraction = static_cast<float>(sample) / static_cast<float>(samples);
    const Vec3 position = from + (to - from) * fraction;
    if (playerPositionSolid(arena, player, position)) return true;
  }
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
    if (!playerPositionSolid(arena, player, start) &&
        !playerTouchesKillVolume(arena, bounds, start)) {
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
    if (landing.onGround && !playerPositionSolid(arena, player, landing.position) &&
        !playerTouchesKillVolume(arena, bounds, landing.position)) {
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
    if (playerTouchesKillVolume(arena, bounds, player.position)) return false;
    if (player.onGround && !playerPositionSolid(arena, player, player.position)) {
      position = player.position;
      return true;
    }
  }
  return false;
}

struct BotNavTraversalProof {
  bool reached = false;
  bool stalled = false;
  std::size_t simulatedTicks = 0;
};

[[nodiscard]] BotNavTraversalProof canTraverse(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds,
  Vec3 from,
  Vec3 to,
  bool jump,
  int maximumTicks = 1536
) {
  BotNavTraversalProof proof;
  PlayerState player;
  player.position = from;
  player.bounds = bounds;
  player.health = 100;
  player.onGround = true;
  player.movementMode = MovementMode::Grounded;
  if (playerTouchesKillVolume(arena, bounds, player.position)) return proof;
  const Vec3 delta = to - from;
  const float distance = std::max(0.01F, distance3d(from, to));
  UserCommand command;
  command.viewYawRadians = std::atan2(delta.y, delta.x);
  command.viewPitchRadians = 0.0F;
  command.forwardMove = 1.0F;
  command.jump = jump;
  // Refined target-surface links are half a normal grid step. They still use
  // the authoritative input simulation, but need less acceleration time than
  // a regular map link. Keeping the longer floor for normal links preserves
  // their former proof horizon while the bounded refinement cannot dominate
  // map-load time with identical 96-tick checks.
  const int minimumTicks = distance <= kNavSpacing * 0.75F ? 72 : 96;
  const int ticks = std::clamp(
    static_cast<int>(std::ceil(distance / std::max(1.0F, movement.maxGroundSpeed) /
      kBotNavDt * 1.75F)),
    // A standing player needs time to build speed. A short neighbor must not
    // fail merely because its max-speed travel estimate omits acceleration.
    minimumTicks,
    // Imported maps can be hundreds of normal simulation units wide. A link
    // still has to reach its endpoint through ordinary movement, with this
    // fixed ceiling keeping blocked authored geometry bounded.
    maximumTicks
  );
  for (int tick = 0; tick < ticks; ++tick) {
    simulateMovement(player, command, arena, movement, kBotNavDt);
    ++proof.simulatedTicks;
    if (playerTouchesKillVolume(arena, bounds, player.position)) return proof;
    if (distance3d(player.position, to) <= kNavReachRadius) {
      proof.reached = true;
      return proof;
    }
    // A walk command has no later input change. Once normal movement has
    // spent a fixed warm-up at rest against collision, more identical ticks
    // cannot turn that rejected edge into a valid one. Keep jump proofs at
    // their full horizon because their airborne arc can still cross a gap.
    if (!jump && tick >= 47 && player.onGround &&
        (player.velocity.x * player.velocity.x) + (player.velocity.y * player.velocity.y) <=
          0.0001F) {
      proof.stalled = true;
      return proof;
    }
  }
  return proof;
}

// Special links use the same trigger order as ordinary movement. Starting at
// the settled trigger node still takes a normal fixed movement tick before the
// trigger may change velocity or position; no nav code writes a player pose.
[[nodiscard]] BotNavSpecialFailureStage simulateJumpPadLanding(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds,
  Vec3 entry,
  Vec3& landing
) {
  PlayerState player;
  player.position = entry;
  player.bounds = bounds;
  player.health = 100;
  player.onGround = true;
  player.movementMode = MovementMode::Grounded;
  UserCommand command;
  bool launched = false;
  for (std::size_t tick = 0; tick < 32U; ++tick) {
    simulateMovement(player, command, arena, movement, kBotNavDt);
    if (playerTouchesKillVolume(arena, bounds, player.position)) {
      return BotNavSpecialFailureStage::Landing;
    }
    if (player.jumpPadCooldownTicksRemaining == kDefaultJumpPadCooldownTicks) {
      launched = true;
      break;
    }
  }
  if (!launched) return BotNavSpecialFailureStage::TriggerActivation;
  for (std::size_t tick = 0; tick < 768U; ++tick) {
    simulateMovement(player, command, arena, movement, kBotNavDt);
    if (playerTouchesKillVolume(arena, bounds, player.position)) {
      return BotNavSpecialFailureStage::Landing;
    }
    if (player.onGround && !playerPositionSolid(arena, player, player.position)) {
      landing = player.position;
      return BotNavSpecialFailureStage::None;
    }
  }
  return BotNavSpecialFailureStage::Landing;
}

[[nodiscard]] BotNavSpecialFailureStage simulateTeleportLanding(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds,
  Vec3 entry,
  Vec3 destination,
  Vec3& landing
) {
  PlayerState player;
  player.position = entry;
  player.bounds = bounds;
  player.health = 100;
  player.onGround = true;
  player.movementMode = MovementMode::Grounded;
  UserCommand command;
  bool teleported = false;
  for (std::size_t tick = 0; tick < 32U; ++tick) {
    simulateMovement(player, command, arena, movement, kBotNavDt);
    if (playerTouchesKillVolume(arena, bounds, player.position)) {
      return BotNavSpecialFailureStage::Landing;
    }
    if (distance3d(player.position, destination) <= 0.01F) {
      teleported = true;
      break;
    }
  }
  if (!teleported) return BotNavSpecialFailureStage::TriggerActivation;
  for (std::size_t tick = 0; tick < 384U; ++tick) {
    simulateMovement(player, command, arena, movement, kBotNavDt);
    if (playerTouchesKillVolume(arena, bounds, player.position)) {
      return BotNavSpecialFailureStage::Landing;
    }
    if (player.onGround && !playerPositionSolid(arena, player, player.position)) {
      landing = player.position;
      return BotNavSpecialFailureStage::None;
    }
  }
  return BotNavSpecialFailureStage::Landing;
}

// A health item may hang over a route where a player can collect it in a
// jump, but no player center can stand inside its volume. This proves the
// only valid fallback: normal input crosses the same server touch test and
// then returns a grounded landing for the navigation graph.
[[nodiscard]] bool simulateHealthApproach(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds,
  Vec3 entry,
  const ArenaHealthPickup& pickup,
  bool jump,
  Vec3& landing
) {
  PlayerState player;
  player.position = entry;
  player.bounds = bounds;
  player.health = 100;
  player.onGround = true;
  player.movementMode = MovementMode::Grounded;
  const Vec3 delta = pickup.position - entry;
  UserCommand command;
  command.viewYawRadians = std::atan2(delta.y, delta.x);
  command.forwardMove = 1.0F;
  command.jump = jump;
  bool touched = playerTouchesHealthPickup(bounds, player.position, pickup);
  for (std::size_t tick = 0; tick < 384U; ++tick) {
    simulateMovement(player, command, arena, movement, kBotNavDt);
    if (playerTouchesKillVolume(arena, bounds, player.position)) return false;
    touched = touched || playerTouchesHealthPickup(bounds, player.position, pickup);
    if (!touched) continue;
    command.forwardMove = 0.0F;
    command.jump = false;
    if (player.onGround && !playerPositionSolid(arena, player, player.position)) {
      landing = player.position;
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
  std::array<std::size_t, Arena::kTeleportCount> teleportEntryNodes = {};
  std::array<Vec3, Arena::kJumpPadCount> jumpPadEntryPositions = {};
  std::array<Vec3, Arena::kJumpPadCount> jumpPadLandingPositions = {};
  std::array<BotNavSpecialFailureStage, Arena::kJumpPadCount> jumpPadStages = {};
  std::array<Vec3, Arena::kTeleportCount> teleportEntryPositions = {};
  std::array<Vec3, Arena::kTeleportCount> teleportLandingPositions = {};
  std::array<BotNavSpecialFailureStage, Arena::kTeleportCount> teleportStages = {};
  std::array<std::size_t, 32> healthNodes = {};
  jumpPadEntryNodes.fill(BotNavigationMap::kMaxNodes);
  teleportEntryNodes.fill(BotNavigationMap::kMaxNodes);
  jumpPadStages.fill(BotNavSpecialFailureStage::EntrySearch);
  teleportStages.fill(BotNavSpecialFailureStage::EntrySearch);
  healthNodes.fill(BotNavigationMap::kMaxNodes);
  map.healthAnchorNodes.fill(UINT16_MAX);
  map.healthApproachEntryNodes.fill(UINT16_MAX);
  // This index exists only while a map is built. It keeps insertion order in
  // map.links while avoiding an O(E^2) scan to reject a duplicate directed
  // edge on large maps. Its fixed size bounds both memory and probe length.
  constexpr std::size_t kLinkIndexSlots = 65536U;
  constexpr std::uint32_t kEmptyLinkIndex = UINT32_MAX;
  std::vector<std::uint32_t> linkIndex(kLinkIndexSlots, kEmptyLinkIndex);
  const auto directedLinkKey = [](std::size_t from, std::size_t to) {
    return static_cast<std::uint32_t>(from * BotNavigationMap::kMaxNodes + to);
  };
  const auto indexedKeyExists = [&](const std::vector<std::uint32_t>& index,
                                    std::uint32_t key) {
    std::size_t slot = (key * 0x9e3779b9U) & (kLinkIndexSlots - 1U);
    for (std::size_t probe = 0; probe < kLinkIndexSlots; ++probe) {
      const std::uint32_t stored = index[slot];
      if (stored == kEmptyLinkIndex) return false;
      if (stored == key) return true;
      slot = (slot + 1U) & (kLinkIndexSlots - 1U);
    }
    return false;
  };
  const auto addIndexedKey = [&](std::vector<std::uint32_t>& index, std::uint32_t key) {
    std::size_t slot = (key * 0x9e3779b9U) & (kLinkIndexSlots - 1U);
    for (std::size_t probe = 0; probe < kLinkIndexSlots; ++probe) {
      if (index[slot] == kEmptyLinkIndex || index[slot] == key) {
        index[slot] = key;
        return;
      }
      slot = (slot + 1U) & (kLinkIndexSlots - 1U);
    }
  };
  auto addGroundedNodeIndex = [&](Vec3 hint, float mergeDistance = 0.35F) {
    Vec3 position = {};
    if (!groundedNodePosition(arena, bounds, hint, position)) {
      ++map.localGroundedRejects;
      return BotNavigationMap::kMaxNodes;
    }
    const std::size_t existing = nearestNodeWithin(map, position, mergeDistance);
    if (existing < map.nodeCount) return existing;
    if (map.nodeCount == BotNavigationMap::kMaxNodes) {
      ++map.nodeCapacityRejects;
      return BotNavigationMap::kMaxNodes;
    }
    map.nodes[map.nodeCount].position = position;
    return map.nodeCount++;
  };
  auto addExactNodeIndex = [&](Vec3 position, bool merge = true) {
    if (!canStandAt(arena, bounds, position)) return BotNavigationMap::kMaxNodes;
    if (merge) {
      const std::size_t existing = nearestNodeWithin(map, position, 0.35F);
      if (existing < map.nodeCount) return existing;
    }
    if (map.nodeCount == BotNavigationMap::kMaxNodes) {
      ++map.nodeCapacityRejects;
      return BotNavigationMap::kMaxNodes;
    }
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
  const auto requireAnchor = [&](std::size_t node, BotNavAnchorKind kind,
                                 std::size_t sourceIndex) {
    const std::size_t anchorIndex = map.requiredAnchorCount;
    ++map.requiredAnchorCount;
    if (anchorIndex < map.requiredAnchors.size()) {
      map.requiredAnchors[anchorIndex] = {kind,
        static_cast<std::uint16_t>(sourceIndex), static_cast<std::uint16_t>(node)};
    }
    if (node >= map.nodeCount) {
      map.requiredAnchorsComplete = false;
      ++map.missingRequiredAnchorCount;
      return;
    }
    rememberSemanticAnchor(node);
  };

  // Trigger centers can be blocked by trim or lie above a narrow ledge. Probe
  // a bounded, center-first lattice over the real player/trigger overlap
  // area. A point earns use only if a normal fixed movement tick activates
  // its trigger and reaches a grounded landing.
  const auto findTriggeredEntry = [&](Vec3 minimum, Vec3 maximum,
                                      const auto& simulate, Vec3& entry,
                                      Vec3& landing) {
    constexpr std::array<float, 5U> kFractions = {{0.50F, 0.25F, 0.75F, 0.05F, 0.95F}};
    const float lowX = minimum.x - bounds.radius * 0.95F;
    const float highX = maximum.x + bounds.radius * 0.95F;
    const float lowY = minimum.y - bounds.radius * 0.95F;
    const float highY = maximum.y + bounds.radius * 0.95F;
    BotNavSpecialFailureStage lastStage = BotNavSpecialFailureStage::EntrySearch;
    for (const float yFraction : kFractions) {
      for (const float xFraction : kFractions) {
        const Vec3 hint = {
          lowX + (highX - lowX) * xFraction,
          lowY + (highY - lowY) * yFraction,
          std::max(minimum.z, arena.min.z) + bounds.halfHeight,
        };
        Vec3 settled = {};
        if (!groundedNodePosition(arena, bounds, hint, settled) ||
            !playerWouldOverlapTrigger(bounds, settled, minimum, maximum)) {
          continue;
        }
        Vec3 candidateLanding = {};
        lastStage = simulate(settled, candidateLanding);
        if (lastStage == BotNavSpecialFailureStage::None) {
          entry = settled;
          landing = candidateLanding;
          return lastStage;
        }
      }
    }
    return lastStage;
  };

  const auto addHealthTouchNode = [&](const ArenaHealthPickup& pickup) {
    // Match ServerGame::updateHealthPickups. A retry may move around a
    // decorative origin only when the resulting player center can really
    // touch this pickup; fixed offsets alone are not evidence of reachability.
    const float maximumRadius = bounds.radius + kHealthPickupTouchRadius;
    // A map origin may sit below a shelf or inside visual detail. Search the
    // entire legal touch slab, but retain a point only after normal movement
    // has settled a player center that can actually collect the pickup.
    constexpr std::array<float, 9U> kVerticalOffsets = {{0.0F, 0.425F, -0.425F,
      0.85F, -0.85F, 1.275F, -1.275F, 1.65F, -1.65F}};
    constexpr std::array<float, 9U> kLattice = {{0.0F, -0.25F, 0.25F, -0.50F,
      0.50F, -0.75F, 0.75F, -1.0F, 1.0F}};
    constexpr std::array<Vec3, 16U> kDirections = {{{1.0F, 0.0F, 0.0F},
      {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
      {0.70710678F, 0.70710678F, 0.0F}, {-0.70710678F, 0.70710678F, 0.0F},
      {0.70710678F, -0.70710678F, 0.0F}, {-0.70710678F, -0.70710678F, 0.0F},
      {0.92387953F, 0.38268343F, 0.0F}, {-0.92387953F, 0.38268343F, 0.0F},
      {0.92387953F, -0.38268343F, 0.0F}, {-0.92387953F, -0.38268343F, 0.0F},
      {0.38268343F, 0.92387953F, 0.0F}, {-0.38268343F, 0.92387953F, 0.0F},
      {0.38268343F, -0.92387953F, 0.0F}, {-0.38268343F, -0.92387953F, 0.0F}}};
    for (const float verticalOffset : kVerticalOffsets) {
      Vec3 settled = {};
      if (groundedNodePosition(arena, bounds,
          pickup.position + Vec3{0.0F, 0.0F, verticalOffset}, settled) &&
          playerTouchesHealthPickup(bounds, settled, pickup)) {
        // A nearby bulk or semantic node may not itself touch the item.
        // Preserve the proven settled center as a dedicated resource anchor.
        return addExactNodeIndex(settled, false);
      }
      // A blocked origin can leave only part of the legal pickup disk open.
      // Cover its interior with a fixed lattice before checking the outer
      // rim; each candidate still has to settle and satisfy server pickup
      // overlap, so sampling never invents a resource anchor.
      for (const float y : kLattice) {
        for (const float x : kLattice) {
          if ((x * x) + (y * y) > 1.0001F || (x == 0.0F && y == 0.0F)) continue;
          settled = {};
          const Vec3 hint = pickup.position +
            Vec3{x * (maximumRadius * 0.98F), y * (maximumRadius * 0.98F), verticalOffset};
          if (groundedNodePosition(arena, bounds, hint, settled) &&
              playerTouchesHealthPickup(bounds, settled, pickup)) {
            return addExactNodeIndex(settled, false);
          }
        }
      }
      for (const Vec3 direction : kDirections) {
        settled = {};
        const Vec3 hint = pickup.position + direction * (maximumRadius * 0.995F) +
          Vec3{0.0F, 0.0F, verticalOffset};
        if (groundedNodePosition(arena, bounds, hint, settled) &&
            playerTouchesHealthPickup(bounds, settled, pickup)) {
          return addExactNodeIndex(settled, false);
        }
      }
    }
    return BotNavigationMap::kMaxNodes;
  };

  // Add every gameplay anchor before the bulk grid. This ordered reservation
  // makes the fixed node cap explicit instead of silently dropping a spawn, item,
  // base, pad, or teleport because decorative samples used the capacity.
  for (std::size_t index = 0; index < arena.spawnCount; ++index) {
    Vec3 position = arena.spawnPositions[index];
    position.z += bounds.halfHeight;
    requireAnchor(addGroundedNodeIndex(position), BotNavAnchorKind::Spawn, index);
  }
  for (std::size_t index = 0; index < arena.teamSpawnCount; ++index) {
    Vec3 position = arena.teamSpawns[index].position;
    position.z += bounds.halfHeight;
    requireAnchor(addGroundedNodeIndex(position), BotNavAnchorKind::TeamSpawn, index);
  }
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    healthNodes[index] = addHealthTouchNode(arena.healthPickups[index]);
    if (healthNodes[index] < map.nodeCount) {
      map.healthAnchorNodes[index] = static_cast<std::uint16_t>(healthNodes[index]);
      rememberSemanticAnchor(healthNodes[index]);
    }
  }
  const auto requireBase = [&](const ArenaMcGuffinBase& base, BotNavAnchorKind kind) {
    requireAnchor(addGroundedNodeIndex({(base.min.x + base.max.x) * 0.5F,
      (base.min.y + base.max.y) * 0.5F,
      std::max(base.min.z, arena.min.z) + bounds.halfHeight}), kind, 0U);
  };
  if (arena.mcguffin.hasNeutralSpawn) {
    requireAnchor(addGroundedNodeIndex(arena.mcguffin.neutralSpawn +
      Vec3{0.0F, 0.0F, bounds.halfHeight}), BotNavAnchorKind::NeutralObjective, 0U);
  }
  if (arena.mcguffin.hasRedBase) requireBase(arena.mcguffin.redBase, BotNavAnchorKind::RedBase);
  if (arena.mcguffin.hasBlueBase) requireBase(arena.mcguffin.blueBase, BotNavAnchorKind::BlueBase);
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const ArenaJumpPad& pad = arena.jumpPads[index];
    jumpPadStages[index] = findTriggeredEntry(pad.min, pad.max,
      [&](Vec3 entry, Vec3& landing) {
        return simulateJumpPadLanding(arena, movement, bounds, entry, landing);
      }, jumpPadEntryPositions[index], jumpPadLandingPositions[index]);
    if (jumpPadStages[index] == BotNavSpecialFailureStage::None) {
      // The route must retain the exact normal-movement origin. Merging a
      // nearby general sample could make a bot stop outside a thin trigger.
      jumpPadEntryNodes[index] = addExactNodeIndex(jumpPadEntryPositions[index], false);
      if (jumpPadEntryNodes[index] >= map.nodeCount) {
        jumpPadStages[index] = BotNavSpecialFailureStage::NodeCapacity;
      }
    }
    requireAnchor(jumpPadEntryNodes[index], BotNavAnchorKind::JumpPadEntry, index);
  }
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    const ArenaTeleport& teleport = arena.teleports[index];
    teleportStages[index] = findTriggeredEntry(teleport.min, teleport.max,
      [&](Vec3 entry, Vec3& landing) {
        return simulateTeleportLanding(arena, movement, bounds, entry,
          teleport.destination, landing);
      }, teleportEntryPositions[index], teleportLandingPositions[index]);
    if (teleportStages[index] == BotNavSpecialFailureStage::None) {
      teleportEntryNodes[index] = addExactNodeIndex(teleportEntryPositions[index], false);
      if (teleportEntryNodes[index] >= map.nodeCount) {
        teleportStages[index] = BotNavSpecialFailureStage::NodeCapacity;
      }
    }
    requireAnchor(teleportEntryNodes[index], BotNavAnchorKind::TeleportEntry, index);
  }
  auto addLink = [&](std::size_t from, std::size_t to, BotNavLinkKind kind) {
    if (from >= map.nodeCount || to >= map.nodeCount || from == to) return;
    if (map.linkCount == BotNavigationMap::kMaxLinks) {
      ++map.linkCapacityRejects;
      return;
    }
    const std::uint32_t key = directedLinkKey(from, to);
    if (indexedKeyExists(linkIndex, key)) return;
    addIndexedKey(linkIndex, key);
    map.links[map.linkCount++] = BotNavLink{
      static_cast<std::uint16_t>(from), static_cast<std::uint16_t>(to), kind};
  };

  // The landing is the outcome of the real trigger, not a grounded version
  // of its authored target. Both ends remain required semantic anchors.
  map.jumpPadRouteCount = std::min(arena.jumpPadCount, map.jumpPadRoutes.size());
  for (std::size_t index = 0; index < arena.jumpPadCount; ++index) {
    const std::size_t entry = jumpPadEntryNodes[index];
    BotNavSpecialFailureStage failureStage = jumpPadStages[index];
    const bool landed = entry < map.nodeCount &&
      failureStage == BotNavSpecialFailureStage::None;
    // A landing may sit close to an unrelated grid node or even match the
    // entry position. Retain it as its own route endpoint so every special
    // edge records the exact pose produced by normal movement.
    const std::size_t exit = landed ?
      addExactNodeIndex(jumpPadLandingPositions[index], false) : BotNavigationMap::kMaxNodes;
    if (landed && exit >= map.nodeCount) failureStage = BotNavSpecialFailureStage::NodeCapacity;
    requireAnchor(exit, BotNavAnchorKind::JumpPadLanding, index);
    if (index < map.jumpPadRoutes.size() && entry < map.nodeCount && exit < map.nodeCount) {
      map.jumpPadRoutes[index] = {static_cast<std::uint16_t>(entry),
        static_cast<std::uint16_t>(exit), true, BotNavSpecialFailureStage::None};
      addLink(entry, exit, BotNavLinkKind::JumpPad);
    } else if (index < map.jumpPadRoutes.size()) {
      map.jumpPadRoutes[index].failureStage = failureStage;
    }
  }
  map.teleportRouteCount = std::min(arena.teleportCount, map.teleportRoutes.size());
  for (std::size_t index = 0; index < arena.teleportCount; ++index) {
    const std::size_t entry = teleportEntryNodes[index];
    BotNavSpecialFailureStage failureStage = teleportStages[index];
    const bool landed = entry < map.nodeCount &&
      failureStage == BotNavSpecialFailureStage::None;
    const std::size_t exit = landed ?
      addExactNodeIndex(teleportLandingPositions[index], false) : BotNavigationMap::kMaxNodes;
    if (landed && exit >= map.nodeCount) failureStage = BotNavSpecialFailureStage::NodeCapacity;
    requireAnchor(exit, BotNavAnchorKind::TeleportLanding, index);
    if (index < map.teleportRoutes.size() && entry < map.nodeCount && exit < map.nodeCount) {
      map.teleportRoutes[index] = {static_cast<std::uint16_t>(entry),
        static_cast<std::uint16_t>(exit), true, BotNavSpecialFailureStage::None};
      addLink(entry, exit, BotNavLinkKind::Teleport);
    } else if (index < map.teleportRoutes.size()) {
      map.teleportRoutes[index].failureStage = failureStage;
    }
  }
  const auto findHealthApproach = [&](const ArenaHealthPickup& pickup,
                                      Vec3& entry, Vec3& landing,
                                      BotNavLinkKind& kind) {
    constexpr std::array<Vec3, 16U> kDirections = {{{1.0F, 0.0F, 0.0F},
      {-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}, {0.0F, -1.0F, 0.0F},
      {0.70710678F, 0.70710678F, 0.0F}, {-0.70710678F, 0.70710678F, 0.0F},
      {0.70710678F, -0.70710678F, 0.0F}, {-0.70710678F, -0.70710678F, 0.0F},
      {0.92387953F, 0.38268343F, 0.0F}, {-0.92387953F, 0.38268343F, 0.0F},
      {0.92387953F, -0.38268343F, 0.0F}, {-0.92387953F, -0.38268343F, 0.0F},
      {0.38268343F, 0.92387953F, 0.0F}, {-0.38268343F, 0.92387953F, 0.0F},
      {0.38268343F, -0.92387953F, 0.0F}, {-0.38268343F, -0.92387953F, 0.0F}}};
    constexpr std::array<float, 3U> kApproachDistances = {{1.35F, 2.40F, 3.45F}};
    constexpr std::array<float, 3U> kVerticalOffsets = {{0.0F, 0.85F, -0.85F}};
    for (const float verticalOffset : kVerticalOffsets) {
      for (const float distance : kApproachDistances) {
        for (const Vec3 direction : kDirections) {
          Vec3 settled = {};
          if (!groundedNodePosition(arena, bounds,
              pickup.position + direction * distance +
                Vec3{0.0F, 0.0F, verticalOffset}, settled)) {
            continue;
          }
          ++map.healthApproachGroundedCandidates;
          Vec3 candidateLanding = {};
          ++map.healthApproachSimulationTrials;
          if (simulateHealthApproach(arena, movement, bounds, settled, pickup,
              false, candidateLanding)) {
            entry = settled;
            landing = candidateLanding;
            kind = BotNavLinkKind::Walk;
            return true;
          }
          ++map.healthApproachSimulationTrials;
          if (simulateHealthApproach(arena, movement, bounds, settled, pickup,
              true, candidateLanding)) {
            entry = settled;
            landing = candidateLanding;
            kind = BotNavLinkKind::Jump;
            return true;
          }
        }
      }
    }
    return false;
  };
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    if (healthNodes[index] < map.nodeCount) continue;
    Vec3 entry = {};
    Vec3 landing = {};
    BotNavLinkKind kind = BotNavLinkKind::Walk;
    if (!findHealthApproach(arena.healthPickups[index], entry, landing, kind)) continue;
    const std::size_t entryNode = addExactNodeIndex(entry, false);
    const std::size_t landingNode = addExactNodeIndex(landing, false);
    if (entryNode >= map.nodeCount || landingNode >= map.nodeCount) continue;
    // This edge has a stronger proof than a local ray: the player crossed
    // ServerGame's health overlap and settled at its recorded landing.
    addLink(entryNode, landingNode, kind);
    healthNodes[index] = landingNode;
    map.healthAnchorNodes[index] = static_cast<std::uint16_t>(landingNode);
    map.healthApproachEntryNodes[index] = static_cast<std::uint16_t>(entryNode);
    rememberSemanticAnchor(entryNode);
    rememberSemanticAnchor(landingNode);
  }
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    if (healthNodes[index] < map.nodeCount) continue;
    map.healthTouchVolumeOccluded[index] = healthTouchVolumeFullyOccluded(
      arena, bounds, arena.healthPickups[index], map.healthTouchVolumeProofs[index]);
    if (!map.healthTouchVolumeOccluded[index]) {
      std::uint32_t testedCenters = 0;
      map.healthTouchVolumeFreeCenterFound[index] = findFreeHealthTouchCenter(
        arena, bounds, arena.healthPickups[index], testedCenters,
        map.healthTouchVolumeFirstFreeCenter[index]);
    }
  }
  for (std::size_t index = 0; index < arena.healthPickupCount; ++index) {
    requireAnchor(healthNodes[index], BotNavAnchorKind::Health, index);
  }
  const std::size_t semanticLinkCount = map.linkCount;

  // Extra seeds place a small, fixed set of player-tested wall corners and
  // upper surfaces into the same local flood. They are not bridge endpoints:
  // only a simulated edge can connect them to a gameplay region.
  std::array<std::size_t, BotNavigationMap::kMaxNodes> seedNodes = {};
  std::size_t seedCount = 0;
  const auto addSeed = [&](std::size_t node) {
    if (node >= map.nodeCount) return;
    for (std::size_t index = 0; index < seedCount; ++index) {
      if (seedNodes[index] == node) return;
    }
    if (seedCount < seedNodes.size()) seedNodes[seedCount++] = node;
  };
  for (std::size_t index = 0; index < semanticAnchorCount; ++index) {
    addSeed(semanticAnchorNodes[index]);
  }
  const std::size_t semanticSeedCount = seedCount;
  constexpr std::size_t kWallCornerSeedBudget = 96U;
  constexpr float kWallCornerClearance = 0.02F;
  std::size_t wallCornerSeeds = 0;
  const auto addWallCornerSeeds = [&](Vec3 minimum, Vec3 maximum) {
    if (wallCornerSeeds >= kWallCornerSeedBudget) return;
    const float lowX = minimum.x - bounds.radius - kWallCornerClearance;
    const float highX = maximum.x + bounds.radius + kWallCornerClearance;
    const float lowY = minimum.y - bounds.radius - kWallCornerClearance;
    const float highY = maximum.y + bounds.radius + kWallCornerClearance;
    const float z = std::max(minimum.z, arena.min.z) + bounds.halfHeight;
    for (const Vec3 corner : std::array<Vec3, 4U>{{
        {lowX, lowY, z}, {lowX, highY, z}, {highX, lowY, z}, {highX, highY, z}}}) {
      if (wallCornerSeeds++ >= kWallCornerSeedBudget) break;
      const std::size_t node = addGroundedNodeIndex(corner);
      addSeed(node);
    }
  };
  for (std::size_t index = 0; index < arena.wallCount && wallCornerSeeds < kWallCornerSeedBudget;
       ++index) {
    addWallCornerSeeds(arena.walls[index].min, arena.walls[index].max);
  }
  for (std::size_t index = 0; index < arena.brushCount && wallCornerSeeds < kWallCornerSeedBudget;
       ++index) {
    addWallCornerSeeds(arena.brushes[index].min, arena.brushes[index].max);
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
    addGroundLevel(wall.max.z, {(wall.min.x + wall.max.x) * 0.5F,
      (wall.min.y + wall.max.y) * 0.5F, wall.max.z});
  }
  for (std::size_t index = 0; index < arena.brushCount; ++index) {
    const ArenaBrush& brush = arena.brushes[index];
    addGroundLevel(brush.max.z, {(brush.min.x + brush.max.x) * 0.5F,
      (brush.min.y + brush.max.y) * 0.5F, brush.max.z});
  }
  std::sort(groundLevels.begin(), groundLevels.begin() + groundLevelCount,
    [](const GroundLevel& first, const GroundLevel& second) {
      return first.height < second.height;
    });
  constexpr std::size_t kGroundLevelSeedBudget = 64U;
  const std::size_t sampledLevels = std::min(groundLevelCount, kGroundLevelSeedBudget);
  for (std::size_t sample = 0; sample < sampledLevels; ++sample) {
    const std::size_t index = sampledLevels <= 1U ? 0U :
      (sample * (groundLevelCount - 1U)) / (sampledLevels - 1U);
    const std::size_t node = addGroundedNodeIndex(
      groundLevels[index].representative + Vec3{0.0F, 0.0F, bounds.halfHeight}
    );
    addSeed(node);
  }

  // Fixed-storage, fair region flood. It never compares every pair of nodes.
  // Each gameplay anchor receives its own round-robin queue of local rays;
  // rays continue forward and branch only when real movement rejects a wall.
  // This spends fixed work on progress across a map instead of filling a tiny
  // disk around the first few anchors.
  constexpr std::array<Vec3, 8U> kRegionDirections = {{{1.0F, 0.0F, 0.0F},
    {0.70710678F, 0.70710678F, 0.0F}, {0.0F, 1.0F, 0.0F},
    {-0.70710678F, 0.70710678F, 0.0F}, {-1.0F, 0.0F, 0.0F},
    {-0.70710678F, -0.70710678F, 0.0F}, {0.0F, -1.0F, 0.0F},
    {0.70710678F, -0.70710678F, 0.0F}}};
  // Seed every local grid direction. A diagonal corridor can leave an anchor
  // before any cardinal ray reaches its first wall, so cardinal-only starts
  // can strand an otherwise walkable spawn region.
  constexpr std::array<std::size_t, 8U> kInitialDirections = {{0U, 1U, 2U, 3U,
    4U, 5U, 6U, 7U}};
  constexpr std::size_t kRegionExpansionWorkLimit = 8192U;
  constexpr std::size_t kAdaptiveRefinementNodeReserve = 512U;
  constexpr std::size_t kBulkNodeLimit =
    BotNavigationMap::kMaxNodes - kAdaptiveRefinementNodeReserve;
  constexpr std::uint16_t kNoRegion = UINT16_MAX;
  // A node-direction expansion has one result for every region. Queue it
  // once, so the fixed task store covers the full node-by-8 local frontier
  // without duplicate work consuming the bounded queue.
  constexpr std::size_t kMaxRegionTasks =
    BotNavigationMap::kMaxNodes * kRegionDirections.size();
  constexpr std::uint8_t kNoResumeDirection = UINT8_MAX;
  std::array<std::uint8_t, BotNavigationMap::kMaxNodes> queuedDirections = {};
  std::array<std::uint16_t, kMaxRegionTasks> taskNode = {};
  std::array<std::uint16_t, kMaxRegionTasks> taskNext = {};
  std::array<std::uint8_t, kMaxRegionTasks> taskDirection = {};
  std::array<std::uint8_t, kMaxRegionTasks> taskResumeDirection = {};
  std::array<bool, kMaxRegionTasks> taskIsResumeProbe = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> regionHead = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> regionTail = {};
  regionHead.fill(kNoRegion);
  regionTail.fill(kNoRegion);
  taskNext.fill(kNoRegion);
  std::size_t taskCount = 0;
  const std::size_t totalSeedCount = seedCount;
  std::size_t activeSeedCount = semanticSeedCount;
  const auto enqueue = [&](std::size_t region, std::size_t node, std::size_t direction,
                           std::uint8_t resumeDirection = UINT8_MAX,
                           bool resumeProbe = false) {
    if (region >= totalSeedCount || node >= map.nodeCount ||
        direction >= kRegionDirections.size()) return;
    if (taskCount == taskNode.size()) {
      map.regionTaskCapacityReached = true;
      return;
    }
    const std::uint8_t directionBit = static_cast<std::uint8_t>(1U << direction);
    if ((queuedDirections[node] & directionBit) != 0U) return;
    queuedDirections[node] |= directionBit;
    const std::size_t task = taskCount++;
    taskNode[task] = static_cast<std::uint16_t>(node);
    taskDirection[task] = static_cast<std::uint8_t>(direction);
    taskResumeDirection[task] = resumeDirection;
    taskIsResumeProbe[task] = resumeProbe;
    if (regionTail[region] == kNoRegion) {
      regionHead[region] = static_cast<std::uint16_t>(task);
    } else {
      taskNext[regionTail[region]] = static_cast<std::uint16_t>(task);
    }
    regionTail[region] = static_cast<std::uint16_t>(task);
  };
  for (std::size_t seed = 0; seed < semanticSeedCount; ++seed) {
    for (const std::size_t direction : kInitialDirections) {
      enqueue(seed, seedNodes[seed], direction);
    }
  }
  map.regionSeedCount = semanticSeedCount;
  // Semantic anchors grow first but leave deterministic capacity for corner
  // and upper-surface frontiers. This prevents a large empty region from
  // consuming every node before a narrow authored route gets a turn.
  const std::size_t semanticNodeLimit = std::max(map.nodeCount,
    kBulkNodeLimit - 96U);
  std::size_t floodNodeLimit = semanticNodeLimit;
  // Compact multi-room maps retain the proven 512-node topology spacing so
  // a connector crosses in one ray. Larger maps switch to the enlarged bulk
  // budget before that coarse ray exceeds 3 units; this spends the measured
  // extra capacity on real surface detail instead of jumping over it.
  constexpr std::size_t kPrimarySamplingBudget = 512U - 96U;
  const float sampledArea = std::max(1.0F,
    (samplingBounds.max.x - samplingBounds.min.x) *
    (samplingBounds.max.y - samplingBounds.min.y));
  // Scale the first local ray step to the authored map, while retaining a
  // one-step fallback below. A fixed 1.25-unit-only flood cannot join the
  // meaningful regions of large imported maps before its fixed nodes fill.
  const float coarseExpansionDistance = std::sqrt(
    sampledArea / static_cast<float>(kPrimarySamplingBudget)
  );
  const float denseExpansionDistance = std::sqrt(
    sampledArea / static_cast<float>(kBulkNodeLimit - 96U)
  );
  const float primaryExpansionDistance = std::clamp(
    coarseExpansionDistance <= kNavSpacing * 2.4F
      ? coarseExpansionDistance
      : denseExpansionDistance,
    kNavSpacing, kNavSpacing * 4.0F);
  const auto enqueueDetour = [&](std::size_t region, std::size_t from,
                                 std::size_t direction, bool resumeProbe,
                                 std::uint8_t resumeDirection) {
    if (resumeProbe || resumeDirection != kNoResumeDirection) return;
    // A blocked horizontal ray tries both perpendicular local directions
    // from its last proven node. Each detour keeps probing the original
    // direction, so it turns back only after it has cleared the wall.
    const std::size_t left = (direction + kRegionDirections.size() - 1U) %
      kRegionDirections.size();
    const std::size_t right = (direction + 1U) % kRegionDirections.size();
    enqueue(region, from, left, static_cast<std::uint8_t>(direction));
    enqueue(region, from, right, static_cast<std::uint8_t>(direction));
  };
  const auto tryDirectedLink = [&](std::size_t from, std::size_t to, bool retryBroadphase) {
    if (from >= map.nodeCount || to >= map.nodeCount || from == to) return false;
    const std::uint32_t forwardKey = directedLinkKey(from, to);
    if (indexedKeyExists(linkIndex, forwardKey)) return true;
    ++map.localTraversalTrials;
    const bool forwardBroadphaseBlocked = linearlyBlockedForPlayer(
      arena, bounds, map.nodes[from].position, map.nodes[to].position
    );
    if (forwardBroadphaseBlocked && !retryBroadphase) {
      ++map.localBroadphaseRejects;
      return false;
    }
    if (forwardBroadphaseBlocked) ++map.localBroadphaseRetries;
    const bool sameLevelSimpleWalk = !forwardBroadphaseBlocked &&
      std::fabs(map.nodes[from].position.z - map.nodes[to].position.z) <= 0.05F;
    const auto runProof = [&](std::size_t start, std::size_t goal, bool jump,
                              bool simpleWalk) {
      const BotNavTraversalProof proof = canTraverse(arena, movement, bounds,
        map.nodes[start].position, map.nodes[goal].position, jump, 384);
      map.localTraversalSimulationTicks += proof.simulatedTicks;
      map.localTraversalStallRejects += proof.stalled ? 1U : 0U;
      if (simpleWalk && !jump) {
        ++map.localSimpleWalkProofTrials;
        map.localSimpleWalkProofTicks += proof.simulatedTicks;
        map.localSimpleWalkRejects += proof.reached ? 0U : 1U;
      }
      return proof.reached;
    };
    BotNavLinkKind kind = std::fabs(map.nodes[from].position.z - map.nodes[to].position.z) <= 0.05F
      ? BotNavLinkKind::Walk : BotNavLinkKind::Step;
    // A local edge can descend a tall authored ledge. Keep the proof bounded,
    // but allow its fixed simulation window to cover a normal fall instead of
    // rejecting it at the former one-second cap. Kill volumes are checked by
    // the traversal proof, so survivable drops remain legal while lethal ones
    // never become graph edges.
    bool reached = runProof(from, to, false, sameLevelSimpleWalk);
    // A gap can have equal floor heights.  Prove a normal jump in that case
    // too; z alone does not describe the movement rule.
    if (!reached) {
      reached = runProof(from, to, true, false);
      if (reached) kind = BotNavLinkKind::Jump;
    }
    if (!reached) {
      ++map.localTraversalRejects;
      return false;
    }
    addLink(from, to, kind);
    // Direction stays explicit. Flat or symmetric ground earns a reverse
    // link only after its own normal movement proof; pads and teleports never
    // pass through this helper and remain one-way trigger transitions.
    const std::uint32_t reverseKey = directedLinkKey(to, from);
    if (indexedKeyExists(linkIndex, reverseKey)) return true;
    ++map.localTraversalTrials;
    const bool reverseBroadphaseBlocked = linearlyBlockedForPlayer(
      arena, bounds, map.nodes[to].position, map.nodes[from].position
    );
    if (!reverseBroadphaseBlocked || retryBroadphase) {
      if (reverseBroadphaseBlocked) ++map.localBroadphaseRetries;
      const bool reverseSameLevelSimpleWalk = !reverseBroadphaseBlocked &&
        std::fabs(map.nodes[from].position.z - map.nodes[to].position.z) <= 0.05F;
      bool reverseReached = runProof(to, from, false, reverseSameLevelSimpleWalk);
      if (!reverseReached && kind == BotNavLinkKind::Jump) {
        reverseReached = runProof(to, from, true, false);
      }
      if (reverseReached) {
        addLink(to, from, kind);
      } else {
        ++map.localBroadphaseRejects;
      }
    } else {
      ++map.localBroadphaseRejects;
    }
    return true;
  };
  // A broad upper platform can need several regular grid steps before a ray
  // reaches its edge. Probe a small fixed set of lower surfaces from every
  // gameplay anchor so a normal fall can join that platform to the level
  // below. Each retained endpoint and edge still passes the same grounded
  // and fixed-step movement proof as all other navigation nodes.
  constexpr std::array<float, 2U> kSurfaceDropProbeMultipliers = {{2.0F, 4.0F}};
  for (std::size_t anchor = 0; anchor < semanticSeedCount; ++anchor) {
    const std::size_t from = seedNodes[anchor];
    if (from >= map.nodeCount) continue;
    for (const Vec3 direction : kRegionDirections) {
      for (const float multiplier : kSurfaceDropProbeMultipliers) {
        ++map.surfaceDropProbeTrials;
        const std::size_t oldNodeCount = map.nodeCount;
        const std::size_t to = addGroundedNodeIndex(
          map.nodes[from].position + direction * (primaryExpansionDistance * multiplier),
          kNavSpacing * 0.90F
        );
        const bool created = to >= oldNodeCount && to < map.nodeCount;
        if (to >= map.nodeCount ||
            map.nodes[to].position.z >= map.nodes[from].position.z - 0.50F ||
            !tryDirectedLink(from, to, false)) {
          if (created) --map.nodeCount;
          continue;
        }
        ++map.surfaceDropProbeLinks;
        for (const std::size_t directionIndex : kInitialDirections) {
          enqueue(anchor, to, directionIndex);
        }
      }
    }
  }
  // Open maps with no one-way movement can exhaust the full bulk budget long
  // after every gameplay anchor already has a proved route. Keep a fair base
  // flood, then stop only when one required anchor reaches every other one in
  // both directions. Maps with pads or teleports retain the full directed
  // build because their useful coverage cannot be reduced to one strong
  // component.
  constexpr std::size_t kMinimumConnectedRegionWork = 512U;
  const bool mayStopAtConnectedAnchors = arena.jumpPadCount == 0U &&
    arena.teleportCount == 0U;
  const auto requiredAnchorsStronglyConnected = [&] {
    const std::size_t anchorCount = std::min(map.requiredAnchorCount,
      map.requiredAnchors.size());
    std::size_t root = BotNavigationMap::kMaxNodes;
    for (std::size_t index = 0; index < anchorCount; ++index) {
      const std::size_t node = map.requiredAnchors[index].node;
      if (node >= map.nodeCount) return false;
      if (root == BotNavigationMap::kMaxNodes) root = node;
    }
    if (root == BotNavigationMap::kMaxNodes) return false;
    const auto reachesAllAnchors = [&](bool reverse) {
      std::array<bool, BotNavigationMap::kMaxNodes> reached = {};
      std::array<std::size_t, BotNavigationMap::kMaxNodes> queue = {};
      std::size_t head = 0;
      std::size_t tail = 0;
      reached[root] = true;
      queue[tail++] = root;
      while (head < tail) {
        const std::size_t current = queue[head++];
        for (std::size_t link = 0; link < map.linkCount; ++link) {
          const BotNavLink& edge = map.links[link];
          const std::size_t from = reverse ? edge.to : edge.from;
          const std::size_t to = reverse ? edge.from : edge.to;
          if (from == current && !reached[to]) {
            reached[to] = true;
            queue[tail++] = to;
          }
        }
      }
      for (std::size_t index = 0; index < anchorCount; ++index) {
        if (!reached[map.requiredAnchors[index].node]) return false;
      }
      return true;
    };
    return reachesAllAnchors(false) && reachesAllAnchors(true);
  };
  bool stoppedAtConnectedAnchors = false;
  const auto runFlood = [&] {
    while (map.regionExpansionWork < kRegionExpansionWorkLimit && map.linkCount < map.links.size()) {
      bool progressed = false;
      for (std::size_t region = 0;
           region < activeSeedCount && map.regionExpansionWork < kRegionExpansionWorkLimit;
           ++region) {
      if (regionHead[region] == kNoRegion) continue;
      progressed = true;
      const std::size_t task = regionHead[region];
      const std::size_t from = taskNode[task];
      const std::size_t direction = taskDirection[task];
      const std::uint8_t resumeDirection = taskResumeDirection[task];
      const bool resumeProbe = taskIsResumeProbe[task];
      regionHead[region] = taskNext[task];
      if (regionHead[region] == kNoRegion) regionTail[region] = kNoRegion;
      ++map.regionExpansionWork;
      bool createdNode = false;
      std::size_t to = BotNavigationMap::kMaxNodes;
      const auto connectAtDistance = [&](float distance) {
        const std::size_t oldNodeCount = map.nodeCount;
        to = addGroundedNodeIndex(map.nodes[from].position +
          kRegionDirections[direction] * distance, kNavSpacing * 0.90F);
        if (map.nodeCount > floodNodeLimit && to + 1U == map.nodeCount) {
          --map.nodeCount;
          to = BotNavigationMap::kMaxNodes;
        }
        if (to >= map.nodeCount || to == from || !tryDirectedLink(from, to, false)) {
          // This candidate was not connected. Do not turn it into an unproved
          // remote seed just because its position happened to be standable.
          if (map.nodeCount > oldNodeCount && to + 1U == map.nodeCount) --map.nodeCount;
          return false;
        }
        createdNode = to >= oldNodeCount;
        return true;
      };
      if (!connectAtDistance(primaryExpansionDistance) &&
          (primaryExpansionDistance <= kNavSpacing + 0.001F ||
            !connectAtDistance(kNavSpacing))) {
        enqueueDetour(region, from, direction, resumeProbe, resumeDirection);
        continue;
      }
      if (resumeDirection != kNoResumeDirection) {
        enqueue(region, to, direction, resumeDirection);
        enqueue(region, to, resumeDirection, kNoResumeDirection, true);
      } else {
        enqueue(region, to, direction);
      }
      if (createdNode) ++map.regionNodeCount;
      }
      if (!progressed) break;
      if (mayStopAtConnectedAnchors &&
          map.regionExpansionWork >= kMinimumConnectedRegionWork &&
          requiredAnchorsStronglyConnected()) {
        stoppedAtConnectedAnchors = true;
        break;
      }
    }
  };
  runFlood();
  if (!stoppedAtConnectedAnchors && map.regionExpansionWork < kRegionExpansionWorkLimit &&
      activeSeedCount < totalSeedCount) {
    activeSeedCount = totalSeedCount;
    floodNodeLimit = kBulkNodeLimit;
    for (std::size_t seed = semanticSeedCount; seed < totalSeedCount; ++seed) {
      for (const std::size_t direction : kInitialDirections) {
        enqueue(seed, seedNodes[seed], direction);
      }
    }
    runFlood();
  }
  // Refine only semantic targets that the normal flood cannot reach from a
  // real spawn.  Required-anchor kinds keep this decision inside nav build
  // data instead of relying on validator rules or map names.
  std::array<std::size_t, BotNavigationMap::kMaxNodes> spawnNodes = {};
  std::size_t spawnNodeCount = 0;
  const std::size_t storedRequiredAnchorCount = std::min(
    map.requiredAnchorCount, map.requiredAnchors.size()
  );
  for (std::size_t index = 0; index < storedRequiredAnchorCount; ++index) {
    const BotNavRequiredAnchor& anchor = map.requiredAnchors[index];
    if ((anchor.kind != BotNavAnchorKind::Spawn && anchor.kind != BotNavAnchorKind::TeamSpawn) ||
        anchor.node >= map.nodeCount) {
      continue;
    }
    bool duplicate = false;
    for (std::size_t existing = 0; existing < spawnNodeCount; ++existing) {
      duplicate = spawnNodes[existing] == anchor.node;
      if (duplicate) break;
    }
    if (!duplicate) spawnNodes[spawnNodeCount++] = anchor.node;
  }
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> incomingHead = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxLinks> incomingNext = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> outgoingHead = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxLinks> outgoingNext = {};
  std::array<bool, BotNavigationMap::kMaxNodes> reachableFromEverySpawn = {};
  std::array<std::size_t, BotNavigationMap::kMaxNodes> spawnReachCounts = {};
  std::size_t smallestSpawnReach = BotNavigationMap::kMaxNodes;
  const auto rebuildRefinementReach = [&] {
    incomingHead.fill(UINT16_MAX);
    outgoingHead.fill(UINT16_MAX);
    for (std::size_t link = 0; link < map.linkCount; ++link) {
      const BotNavLink& edge = map.links[link];
      incomingNext[link] = incomingHead[edge.to];
      incomingHead[edge.to] = static_cast<std::uint16_t>(link);
      outgoingNext[link] = outgoingHead[edge.from];
      outgoingHead[edge.from] = static_cast<std::uint16_t>(link);
    }
    reachableFromEverySpawn.fill(spawnNodeCount > 0U);
    spawnReachCounts.fill(0U);
    smallestSpawnReach = BotNavigationMap::kMaxNodes;
    for (std::size_t source = 0; source < spawnNodeCount; ++source) {
      std::array<bool, BotNavigationMap::kMaxNodes> seen = {};
      std::array<std::size_t, BotNavigationMap::kMaxNodes> queue = {};
      std::size_t head = 0;
      std::size_t tail = 0;
      seen[spawnNodes[source]] = true;
      queue[tail++] = spawnNodes[source];
      while (head < tail) {
        const std::size_t current = queue[head++];
        for (std::uint16_t link = outgoingHead[current]; link != UINT16_MAX;
             link = outgoingNext[link]) {
          const std::size_t next = map.links[link].to;
          if (!seen[next]) {
            seen[next] = true;
            queue[tail++] = next;
          }
        }
      }
      spawnReachCounts[source] = tail;
      smallestSpawnReach = std::min(smallestSpawnReach, tail);
      for (std::size_t node = 0; node < map.nodeCount; ++node) {
        reachableFromEverySpawn[node] = reachableFromEverySpawn[node] && seen[node];
      }
    }
  };
  const auto allSpawnsCanReach = [&](std::size_t target) {
    if (spawnNodeCount == 0U || target >= map.nodeCount) return true;
    std::array<bool, BotNavigationMap::kMaxNodes> seen = {};
    std::array<std::size_t, BotNavigationMap::kMaxNodes> queue = {};
    std::size_t head = 0;
    std::size_t tail = 0;
    seen[target] = true;
    queue[tail++] = target;
    while (head < tail) {
      const std::size_t current = queue[head++];
      for (std::uint16_t link = incomingHead[current]; link != UINT16_MAX;
           link = incomingNext[link]) {
        const std::size_t predecessor = map.links[link].from;
        if (!seen[predecessor]) {
          seen[predecessor] = true;
          queue[tail++] = predecessor;
        }
      }
    }
    for (std::size_t source = 0; source < spawnNodeCount; ++source) {
      if (!seen[spawnNodes[source]]) return false;
    }
    return true;
  };
  const auto isGameplayDestination = [&](std::size_t node) {
    const std::size_t storedAnchorCount = std::min(map.requiredAnchorCount,
      map.requiredAnchors.size());
    for (std::size_t index = 0; index < storedAnchorCount; ++index) {
      const BotNavRequiredAnchor& anchor = map.requiredAnchors[index];
      if (anchor.node != node) continue;
      if (anchor.kind == BotNavAnchorKind::Health ||
          anchor.kind == BotNavAnchorKind::NeutralObjective ||
          anchor.kind == BotNavAnchorKind::RedBase ||
          anchor.kind == BotNavAnchorKind::BlueBase) {
        return true;
      }
    }
    return false;
  };
  const auto isSmallestSpawnRegion = [&](std::size_t node) {
    for (std::size_t source = 0; source < spawnNodeCount; ++source) {
      if (spawnNodes[source] == node && spawnReachCounts[source] == smallestSpawnReach) {
        return true;
      }
    }
    return false;
  };
  // A target-only, height-preserving flood follows an upper walk until a
  // normal simulated inbound edge joins a node every spawn already reaches.
  // Its fixed storage bounds map-load work while retaining enough points to
  // pass a bent or remote ramp that a local grid cannot cover.
  constexpr float kSurfaceApproachStep = 0.625F;
  constexpr float kSurfaceApproachMaximumDrop = 0.60F;
  constexpr std::size_t kSurfaceApproachNodeBudget = 512U;
  constexpr std::size_t kSurfaceApproachWorkBudget = 4096U;
  // A refinement joins only through a normal movement proof. Its search
  // radius follows the bulk ray scale so a coarse broad-map sample can meet
  // a fine upper-surface walk without adding an unproved bridge.
  const float kSurfaceApproachJoinDistance = primaryExpansionDistance * 2.0F;
  constexpr std::size_t kSurfaceApproachJoinTrialBudget = 64U;
  constexpr std::array<float, 3U> kSurfaceApproachHeightOffsets = {{0.0F, 0.85F, -0.85F}};
  constexpr std::size_t kSurfaceApproachTargetBudget = 16U;
  rebuildRefinementReach();
  // A pickup can have a free airborne touch center without any standable
  // center inside its touch volume. Retry a missing resource only from nodes
  // every current spawn can reach, then record the real UserCommand landing.
  // A local but isolated approach cannot satisfy gameplay coverage.
  constexpr std::size_t kHealthGraphApproachTrialBudget = 64U;
  const auto attachMissingHealthFromGraph = [&](std::size_t healthIndex) {
    if (healthIndex >= arena.healthPickupCount || healthNodes[healthIndex] < map.nodeCount) {
      return false;
    }
    std::array<std::size_t, kHealthGraphApproachTrialBudget> candidates = {};
    std::array<float, kHealthGraphApproachTrialBudget> candidateDistances = {};
    candidates.fill(BotNavigationMap::kMaxNodes);
    candidateDistances.fill(std::numeric_limits<float>::infinity());
    for (std::size_t node = 0; node < map.nodeCount; ++node) {
      if (!reachableFromEverySpawn[node]) continue;
      const float distance = distance3d(map.nodes[node].position,
        arena.healthPickups[healthIndex].position);
      for (std::size_t slot = 0; slot < candidates.size(); ++slot) {
        if (distance >= candidateDistances[slot]) continue;
        for (std::size_t shift = candidates.size() - 1U; shift > slot; --shift) {
          candidates[shift] = candidates[shift - 1U];
          candidateDistances[shift] = candidateDistances[shift - 1U];
        }
        candidates[slot] = node;
        candidateDistances[slot] = distance;
        break;
      }
    }
    for (std::size_t slot = 0; slot < candidates.size(); ++slot) {
      const std::size_t entryNode = candidates[slot];
      if (entryNode >= map.nodeCount) break;
      Vec3 landing = {};
      ++map.healthGraphApproachSimulationTrials;
      BotNavLinkKind kind = BotNavLinkKind::Walk;
      if (!simulateHealthApproach(arena, movement, bounds, map.nodes[entryNode].position,
          arena.healthPickups[healthIndex], false, landing)) {
        ++map.healthGraphApproachSimulationTrials;
        kind = BotNavLinkKind::Jump;
        if (!simulateHealthApproach(arena, movement, bounds, map.nodes[entryNode].position,
            arena.healthPickups[healthIndex], true, landing)) {
          continue;
        }
      }
      const std::size_t landingNode = addExactNodeIndex(landing, false);
      if (landingNode >= map.nodeCount) continue;
      addLink(entryNode, landingNode, kind);
      healthNodes[healthIndex] = landingNode;
      map.healthAnchorNodes[healthIndex] = static_cast<std::uint16_t>(landingNode);
      map.healthApproachEntryNodes[healthIndex] = static_cast<std::uint16_t>(entryNode);
      rememberSemanticAnchor(entryNode);
      rememberSemanticAnchor(landingNode);
      const std::size_t anchorCount = std::min(map.requiredAnchorCount,
        map.requiredAnchors.size());
      for (std::size_t anchor = 0; anchor < anchorCount; ++anchor) {
        BotNavRequiredAnchor& required = map.requiredAnchors[anchor];
        if (required.kind != BotNavAnchorKind::Health || required.sourceIndex != healthIndex ||
            required.node < map.nodeCount) {
          continue;
        }
        required.node = static_cast<std::uint16_t>(landingNode);
        if (map.missingRequiredAnchorCount > 0U) --map.missingRequiredAnchorCount;
        map.requiredAnchorsComplete = map.missingRequiredAnchorCount == 0U;
        break;
      }
      return true;
    }
    return false;
  };
  bool attachedHealthFromReachableGraph = false;
  for (std::size_t healthIndex = 0; healthIndex < arena.healthPickupCount; ++healthIndex) {
    attachedHealthFromReachableGraph = attachMissingHealthFromGraph(healthIndex) ||
      attachedHealthFromReachableGraph;
  }
  if (attachedHealthFromReachableGraph) rebuildRefinementReach();
  for (std::size_t anchor = 0;
       anchor < semanticSeedCount && map.surfaceApproachTargetCount < kSurfaceApproachTargetBudget;
       ++anchor) {
    const std::size_t anchorNode = seedNodes[anchor];
    if ((!isGameplayDestination(anchorNode) && !isSmallestSpawnRegion(anchorNode)) ||
        allSpawnsCanReach(anchorNode)) continue;
    ++map.surfaceApproachTargetCount;
    const std::size_t bulkNodeCount = map.nodeCount;
    std::array<bool, BotNavigationMap::kMaxNodes> visited = {};
    std::array<std::size_t, kSurfaceApproachNodeBudget> highQueue = {};
    std::array<std::size_t, kSurfaceApproachNodeBudget> lowQueue = {};
    std::size_t highHead = 0;
    std::size_t highTail = 0;
    std::size_t lowHead = 0;
    std::size_t lowTail = 0;
    std::size_t targetFloodNodes = 0;
    std::size_t targetFloodWork = 0;
    visited[anchorNode] = true;
    highQueue[highTail++] = anchorNode;
    bool joined = false;
    while ((highHead < highTail || lowHead < lowTail) &&
           targetFloodWork < kSurfaceApproachWorkBudget) {
      const std::size_t current = highHead < highTail ? highQueue[highHead++] : lowQueue[lowHead++];
      for (const Vec3 direction : kRegionDirections) {
        if (targetFloodWork == kSurfaceApproachWorkBudget) break;
        ++targetFloodWork;
        ++map.surfaceApproachFloodWork;
        std::size_t candidate = BotNavigationMap::kMaxNodes;
        std::size_t oldNodeCount = map.nodeCount;
        for (const float heightOffset : kSurfaceApproachHeightOffsets) {
          ++map.surfaceApproachProbeTrials;
          oldNodeCount = map.nodeCount;
          candidate = addGroundedNodeIndex(map.nodes[current].position +
            direction * kSurfaceApproachStep + Vec3{0.0F, 0.0F, heightOffset},
            kSurfaceApproachStep * 0.45F);
          if (candidate < map.nodeCount) break;
        }
        const bool created = candidate >= oldNodeCount && candidate < map.nodeCount;
        if (candidate >= map.nodeCount || candidate == current) {
          if (created) --map.nodeCount;
          continue;
        }
        const bool lowerPriority = map.nodes[candidate].position.z < map.nodes[current].position.z -
          kSurfaceApproachMaximumDrop;
        const std::size_t oldLinkCount = map.linkCount;
        if (!tryDirectedLink(candidate, current, true)) {
          if (created) --map.nodeCount;
          continue;
        }
        map.surfaceApproachProbeLinks += map.linkCount - oldLinkCount;
        if (candidate < bulkNodeCount && reachableFromEverySpawn[candidate]) {
          joined = true;
          break;
        }
        if (!visited[candidate] && targetFloodNodes + 1U < kSurfaceApproachNodeBudget) {
          std::size_t nearestBulk = BotNavigationMap::kMaxNodes;
          float nearestDistanceSquared = kSurfaceApproachJoinDistance * kSurfaceApproachJoinDistance;
          for (std::size_t node = 0; node < bulkNodeCount; ++node) {
            if (!reachableFromEverySpawn[node]) continue;
            const Vec3 delta = map.nodes[node].position - map.nodes[candidate].position;
            const float distanceSquared = (delta.x * delta.x) + (delta.y * delta.y) +
              (delta.z * delta.z);
            if (distanceSquared < nearestDistanceSquared) {
              nearestDistanceSquared = distanceSquared;
              nearestBulk = node;
            }
          }
          if (nearestBulk < bulkNodeCount &&
              map.surfaceApproachBridgeTrials < kSurfaceApproachJoinTrialBudget) {
            ++map.surfaceApproachBridgeTrials;
            const std::size_t oldBridgeLinkCount = map.linkCount;
            if (tryDirectedLink(nearestBulk, candidate, true)) {
              map.surfaceApproachBridgeLinks += map.linkCount - oldBridgeLinkCount;
              joined = true;
              break;
            }
          }
          visited[candidate] = true;
          if (lowerPriority) {
            lowQueue[lowTail++] = candidate;
          } else {
            highQueue[highTail++] = candidate;
          }
          ++targetFloodNodes;
          ++map.surfaceApproachFloodNodes;
        }
      }
      if (joined) break;
    }
    map.surfaceApproachFloodExhausted = map.surfaceApproachFloodExhausted || (!joined &&
      (targetFloodWork == kSurfaceApproachWorkBudget ||
        targetFloodNodes + 1U >= kSurfaceApproachNodeBudget));
    // A join changes the directed graph. Recompute before the next target so
    // it sees the new spawn-reachable surface instead of spending reserve on
    // a destination that the previous refinement already connected.
    if (joined) rebuildRefinementReach();
  }
  if (map.surfaceApproachTargetCount > 0U && map.regionExpansionWork < kRegionExpansionWorkLimit) {
    runFlood();
  }
  const auto hasQueuedRegionWork = [&] {
    for (std::size_t region = 0; region < activeSeedCount; ++region) {
      if (regionHead[region] != kNoRegion) return true;
    }
    return false;
  };
  map.regionWorkExhausted = map.regionExpansionWork == kRegionExpansionWorkLimit &&
    hasQueuedRegionWork();
  map.localLinkCount = map.linkCount - semanticLinkCount;

  // This directed reach check is diagnostic only. The validator below makes
  // the required spawn-to-resource and special-trigger route decisions.
  map.semanticAnchorCount = semanticAnchorCount;
  if (semanticAnchorCount > 0U) {
    std::array<bool, BotNavigationMap::kMaxNodes> reached = {};
    std::array<std::size_t, BotNavigationMap::kMaxNodes> queue = {};
    std::size_t head = 0;
    std::size_t tail = 0;
    const std::size_t root = semanticAnchorNodes[0];
    reached[root] = true;
    queue[tail++] = root;
    while (head < tail) {
      const std::size_t current = queue[head++];
      for (std::size_t link = 0; link < map.linkCount; ++link) {
        const BotNavLink& edge = map.links[link];
        if (edge.from == current && !reached[edge.to]) {
          reached[edge.to] = true;
          queue[tail++] = edge.to;
        }
      }
    }
    for (std::size_t anchor = 0; anchor < semanticAnchorCount; ++anchor) {
      map.unreachableAnchorNodes += !reached[semanticAnchorNodes[anchor]];
    }
  }
  // Report both weak geometry components and directed reach. A pad or
  // teleport may join a weak component one way only, so validation keeps the
  // latter value separate from normal route checks.
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> weakComponents = {};
  weakComponents.fill(UINT16_MAX);
  std::uint16_t weakComponentCount = 0;
  for (std::size_t root = 0; root < map.nodeCount; ++root) {
    if (weakComponents[root] != UINT16_MAX) continue;
    std::array<std::size_t, BotNavigationMap::kMaxNodes> queue = {};
    std::size_t head = 0;
    std::size_t tail = 0;
    weakComponents[root] = weakComponentCount;
    queue[tail++] = root;
    while (head < tail) {
      const std::size_t current = queue[head++];
      for (std::size_t link = 0; link < map.linkCount; ++link) {
        const BotNavLink& edge = map.links[link];
        const std::size_t adjacent = edge.from == current ? edge.to :
          (edge.to == current ? edge.from : BotNavigationMap::kMaxNodes);
        if (adjacent < map.nodeCount && weakComponents[adjacent] == UINT16_MAX) {
          weakComponents[adjacent] = weakComponentCount;
          queue[tail++] = adjacent;
        }
      }
    }
    ++weakComponentCount;
  }
  map.weakComponentCount = weakComponentCount;
  for (std::size_t anchor = 0; anchor < semanticAnchorCount; ++anchor) {
    const std::size_t node = semanticAnchorNodes[anchor];
    BotNavAnchorReach& diagnostic = map.anchorReach[anchor];
    diagnostic.node = static_cast<std::uint16_t>(node);
    diagnostic.weakComponent = weakComponents[node];
    std::array<bool, BotNavigationMap::kMaxNodes> seen = {};
    std::array<std::size_t, BotNavigationMap::kMaxNodes> queue = {};
    std::size_t head = 0;
    std::size_t tail = 0;
    seen[node] = true;
    queue[tail++] = node;
    while (head < tail) {
      const std::size_t current = queue[head++];
      for (std::size_t link = 0; link < map.linkCount; ++link) {
        const BotNavLink& edge = map.links[link];
        if (edge.from == current && !seen[edge.to]) {
          seen[edge.to] = true;
          queue[tail++] = edge.to;
        }
      }
    }
    diagnostic.directedReach = static_cast<std::uint16_t>(tail);
  }
  prepareBotNavigationMap(map);
  return map;
}

void prepareBotNavigationMap(BotNavigationMap& map) {
  map.outgoingLinkHead.fill(UINT16_MAX);
  map.outgoingLinkNext.fill(UINT16_MAX);
  // Prepending from the end preserves link insertion order for each source.
  // That keeps A* tie behavior stable across rebuilds and platforms.
  for (std::size_t index = map.linkCount; index > 0U; --index) {
    const std::size_t linkIndex = index - 1U;
    const BotNavLink& link = map.links[linkIndex];
    if (link.from >= map.nodeCount || link.to >= map.nodeCount) continue;
    map.outgoingLinkNext[linkIndex] = map.outgoingLinkHead[link.from];
    map.outgoingLinkHead[link.from] = static_cast<std::uint16_t>(linkIndex);
  }
  map.outgoingLinksPrepared = true;
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
  healthResourceIndex_ = std::numeric_limits<std::size_t>::max();
  healthResourceUtility_ = -std::numeric_limits<float>::infinity();
  healthRouteCost_ = std::numeric_limits<float>::infinity();
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
  mix(static_cast<std::uint32_t>(healthResourceIndex_));
  mixFloat(healthResourceUtility_); mixFloat(healthRouteCost_);
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

bool BotBrain::planHealthRecovery(
  const BotSenseFrame& sense,
  const BotDifficultyProfile& profile,
  const BotNavigationMap& navigation
) {
  pathCount_ = 0;
  pathCursor_ = 0;
  const std::size_t noResource = std::numeric_limits<std::size_t>::max();
  const auto clearPlan = [&] {
    healthResourceIndex_ = noResource;
    healthResourceUtility_ = -std::numeric_limits<float>::infinity();
    healthRouteCost_ = std::numeric_limits<float>::infinity();
  };
  const int missingHealth = std::max(0, sense.self.maxHealth - sense.self.health);
  const std::size_t startNode = nearestBotNavNode(navigation, sense.self.position);
  if (missingHealth <= 0 || startNode >= navigation.nodeCount) {
    clearPlan();
    return false;
  }

  // One deterministic single-source pass gives comparable directed
  // route costs to every remembered health anchor at replan cadence.
  std::array<float, BotNavigationMap::kMaxNodes> routeCosts = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> cameFrom = {};
  std::array<bool, BotNavigationMap::kMaxNodes> closed = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> heap = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> heapPosition = {};
  routeCosts.fill(std::numeric_limits<float>::infinity());
  cameFrom.fill(UINT16_MAX);
  heapPosition.fill(UINT16_MAX);

  const auto comesBefore = [&](std::size_t first, std::size_t second) {
    return routeCosts[first] < routeCosts[second] ||
      (routeCosts[first] == routeCosts[second] && first < second);
  };
  std::size_t heapCount = 0;
  const auto siftUp = [&](std::size_t index) {
    while (index > 0U) {
      const std::size_t parent = (index - 1U) / 2U;
      if (!comesBefore(heap[index], heap[parent])) break;
      std::swap(heap[index], heap[parent]);
      heapPosition[heap[index]] = static_cast<std::uint16_t>(index);
      heapPosition[heap[parent]] = static_cast<std::uint16_t>(parent);
      index = parent;
    }
  };
  const auto siftDown = [&](std::size_t index) {
    while (true) {
      const std::size_t left = index * 2U + 1U;
      if (left >= heapCount) break;
      const std::size_t right = left + 1U;
      std::size_t child = left;
      if (right < heapCount && comesBefore(heap[right], heap[left])) child = right;
      if (!comesBefore(heap[child], heap[index])) break;
      std::swap(heap[index], heap[child]);
      heapPosition[heap[index]] = static_cast<std::uint16_t>(index);
      heapPosition[heap[child]] = static_cast<std::uint16_t>(child);
      index = child;
    }
  };
  const auto pushOrDecrease = [&](std::size_t node) {
    if (heapPosition[node] == UINT16_MAX) {
      heap[heapCount] = static_cast<std::uint16_t>(node);
      heapPosition[node] = static_cast<std::uint16_t>(heapCount++);
    }
    siftUp(heapPosition[node]);
  };

  routeCosts[startNode] = 0.0F;
  pushOrDecrease(startNode);
  while (heapCount > 0U) {
    const std::size_t current = heap[0];
    --heapCount;
    if (heapCount > 0U) {
      heap[0] = heap[heapCount];
      heapPosition[heap[0]] = 0U;
      siftDown(0U);
    }
    heapPosition[current] = UINT16_MAX;
    if (closed[current]) continue;
    closed[current] = true;
    const auto relax = [&](const BotNavLink& link) {
      if (closed[link.to]) return;
      const float tentative = routeCosts[current] + distance3d(
        navigation.nodes[current].position, navigation.nodes[link.to].position
      );
      if (tentative < routeCosts[link.to]) {
        routeCosts[link.to] = tentative;
        cameFrom[link.to] = static_cast<std::uint16_t>(current);
        pushOrDecrease(link.to);
      }
    };
    if (navigation.outgoingLinksPrepared) {
      for (std::uint16_t linkIndex = navigation.outgoingLinkHead[current];
           linkIndex != UINT16_MAX;
           linkIndex = navigation.outgoingLinkNext[linkIndex]) {
        relax(navigation.links[linkIndex]);
      }
    } else {
      for (std::size_t linkIndex = 0; linkIndex < navigation.linkCount; ++linkIndex) {
        const BotNavLink& link = navigation.links[linkIndex];
        if (link.from == current) relax(link);
      }
    }
  }

  const float memorySeconds = std::max(
    kMinimumHealthResourceMemorySeconds, profile.memorySeconds
  );
  std::size_t bestResource = noResource;
  float bestUtility = -std::numeric_limits<float>::infinity();
  float bestRouteCost = std::numeric_limits<float>::infinity();
  float currentUtility = -std::numeric_limits<float>::infinity();
  float currentRouteCost = std::numeric_limits<float>::infinity();
  for (std::size_t index = 0; index < resourceMemory_.size(); ++index) {
    const ResourceMemory& resource = resourceMemory_[index];
    if (!resource.valid || !resource.available || resource.value <= 0) continue;
    const std::size_t anchor = navigation.healthAnchorNodes[index];
    if (anchor >= navigation.nodeCount || !std::isfinite(routeCosts[anchor])) continue;
    const int recoverableHealth = std::min(resource.value, missingHealth);
    if (recoverableHealth <= 0) continue;
    const float confidence = std::clamp(
      1.0F - resource.ageSeconds / memorySeconds, 0.0F, 1.0F
    );
    if (confidence <= 0.0F) continue;
    // Score only health the bot can actually receive. Sightings become
    // less trustworthy with age, and graph cost replaces geometric proximity.
    const float utility = static_cast<float>(recoverableHealth) * confidence /
      (kHealthRouteCostBias + routeCosts[anchor]);
    if (index == healthResourceIndex_) {
      currentUtility = utility;
      currentRouteCost = routeCosts[anchor];
    }
    if (utility > bestUtility ||
        (utility == bestUtility && index < bestResource)) {
      bestResource = index;
      bestUtility = utility;
      bestRouteCost = routeCosts[anchor];
    }
  }
  if (bestResource == noResource) {
    clearPlan();
    return false;
  }

  std::size_t selectedResource = bestResource;
  float selectedUtility = bestUtility;
  float selectedRouteCost = bestRouteCost;
  // Retain the current pickup unless a challenger is at least 20%
  // better, preventing minor memory or route changes from churning paths.
  if (healthResourceIndex_ < resourceMemory_.size() &&
      bestResource != healthResourceIndex_ &&
      std::isfinite(currentUtility) &&
      bestUtility < currentUtility *
        kHealthResourceSwitchUtilityMultiplier) {
    selectedResource = healthResourceIndex_;
    selectedUtility = currentUtility;
    selectedRouteCost = currentRouteCost;
  }

  const std::size_t targetNode =
    navigation.healthAnchorNodes[selectedResource];
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> reversed = {};
  std::size_t count = 0;
  bool reachedStart = false;
  for (std::size_t node = targetNode; count < reversed.size(); ) {
    reversed[count++] = static_cast<std::uint16_t>(node);
    if (node == startNode) {
      reachedStart = true;
      break;
    }
    const std::uint16_t previous = cameFrom[node];
    if (previous == UINT16_MAX) {
      count = 0;
      break;
    }
    node = previous;
  }
  if (!reachedStart) {
    clearPlan();
    return false;
  }
  while (count > 0U && pathCount_ < path_.size()) {
    path_[pathCount_++] = reversed[--count];
  }
  if (pathCount_ > 1U && path_[0] == startNode) pathCursor_ = 1U;
  if (pathCount_ > 2U && path_[pathCursor_] == lastWaypoint_) {
    ++pathCursor_;
  }
  healthResourceIndex_ = selectedResource;
  healthResourceUtility_ = selectedUtility;
  healthRouteCost_ = selectedRouteCost;
  return pathCount_ > 0U;
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
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> heap = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> heapPosition = {};
  gScore.fill(std::numeric_limits<float>::infinity());
  cameFrom.fill(std::numeric_limits<std::uint16_t>::max());
  heapPosition.fill(UINT16_MAX);
  const auto fScore = [&](std::size_t node) {
    return gScore[node] + distance3d(
      navigation.nodes[node].position, navigation.nodes[targetNode].position
    );
  };
  const auto comesBefore = [&](std::size_t first, std::size_t second) {
    const float firstScore = fScore(first);
    const float secondScore = fScore(second);
    return firstScore < secondScore || (firstScore == secondScore && first < second);
  };
  std::size_t heapCount = 0;
  const auto siftUp = [&](std::size_t index) {
    while (index > 0U) {
      const std::size_t parent = (index - 1U) / 2U;
      if (!comesBefore(heap[index], heap[parent])) break;
      std::swap(heap[index], heap[parent]);
      heapPosition[heap[index]] = static_cast<std::uint16_t>(index);
      heapPosition[heap[parent]] = static_cast<std::uint16_t>(parent);
      index = parent;
    }
  };
  const auto siftDown = [&](std::size_t index) {
    while (true) {
      const std::size_t left = index * 2U + 1U;
      if (left >= heapCount) break;
      const std::size_t right = left + 1U;
      std::size_t child = left;
      if (right < heapCount && comesBefore(heap[right], heap[left])) child = right;
      if (!comesBefore(heap[child], heap[index])) break;
      std::swap(heap[index], heap[child]);
      heapPosition[heap[index]] = static_cast<std::uint16_t>(index);
      heapPosition[heap[child]] = static_cast<std::uint16_t>(child);
      index = child;
    }
  };
  const auto pushOrDecrease = [&](std::size_t node) {
    if (heapPosition[node] == UINT16_MAX) {
      heap[heapCount] = static_cast<std::uint16_t>(node);
      heapPosition[node] = static_cast<std::uint16_t>(heapCount++);
    }
    siftUp(heapPosition[node]);
  };
  gScore[startNode] = 0.0F;
  pushOrDecrease(startNode);
  while (heapCount > 0U) {
    const std::size_t current = heap[0];
    --heapCount;
    if (heapCount > 0U) {
      heap[0] = heap[heapCount];
      heapPosition[heap[0]] = 0U;
      siftDown(0U);
    }
    heapPosition[current] = UINT16_MAX;
    if (closed[current]) continue;
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
    const auto relax = [&](const BotNavLink& link) {
      if (closed[link.to]) return;
      const float tentative = gScore[current] + distance3d(
        navigation.nodes[current].position, navigation.nodes[link.to].position);
      if (tentative < gScore[link.to]) {
        gScore[link.to] = tentative;
        cameFrom[link.to] = static_cast<std::uint16_t>(current);
        pushOrDecrease(link.to);
      }
    };
    if (navigation.outgoingLinksPrepared) {
      for (std::uint16_t linkIndex = navigation.outgoingLinkHead[current];
           linkIndex != UINT16_MAX; linkIndex = navigation.outgoingLinkNext[linkIndex]) {
        relax(navigation.links[linkIndex]);
      }
    } else {
      // Hand-assembled test maps can omit the map-load preparation. Runtime
      // maps always use the indexed branch above.
      for (std::size_t linkIndex = 0; linkIndex < navigation.linkCount; ++linkIndex) {
        const BotNavLink& link = navigation.links[linkIndex];
        if (link.from == current) relax(link);
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
    if (memory.ageSeconds > std::max(
        kMinimumHealthResourceMemorySeconds, profile.memorySeconds)) {
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
  std::size_t currentVisibleTarget = kDuelPlayerCount;
  float currentVisibleDistance = std::numeric_limits<float>::infinity();
  bool currentVisibleTargetGrounded = false;
  bool currentVisibleTargetSplashSurface = false;
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
    const bool betterCandidate = distance < visibleDistance ||
      (distance == visibleDistance && enemy.playerIndex < visibleTarget);
    if (betterCandidate) {
      visibleDistance = distance;
      visibleTarget = enemy.playerIndex;
      visibleTargetGrounded = enemy.onGround;
      visibleTargetSplashSurface = enemy.nearbySplashSurface;
    }
    if (enemy.playerIndex == targetPlayerIndex_) {
      currentVisibleTarget = enemy.playerIndex;
      currentVisibleDistance = distance;
      currentVisibleTargetGrounded = enemy.onGround;
      currentVisibleTargetSplashSurface = enemy.nearbySplashSurface;
    }
  }
  if (currentVisibleTarget < kDuelPlayerCount &&
      visibleTarget != currentVisibleTarget) {
    const float switchAdvantage = std::max(
      kTargetSwitchMinimumDistanceAdvantage,
      currentVisibleDistance * kTargetSwitchRelativeDistanceAdvantage
    );
    if (visibleDistance + switchAdvantage >= currentVisibleDistance) {
      visibleTarget = currentVisibleTarget;
      visibleDistance = currentVisibleDistance;
      visibleTargetGrounded = currentVisibleTargetGrounded;
      visibleTargetSplashSurface = currentVisibleTargetSplashSurface;
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

  const auto clearHealthRecoveryPlan = [&] {
    healthResourceIndex_ = std::numeric_limits<std::size_t>::max();
    healthResourceUtility_ = -std::numeric_limits<float>::infinity();
    healthRouteCost_ = std::numeric_limits<float>::infinity();
    pathCount_ = 0;
    pathCursor_ = 0;
    replanSeconds_ = 0.0F;
  };
  const bool committedHealthResourceValid =
    healthResourceIndex_ < resourceMemory_.size() &&
    resourceMemory_[healthResourceIndex_].valid &&
    resourceMemory_[healthResourceIndex_].available &&
    resourceMemory_[healthResourceIndex_].value > 0 &&
    navigation.healthAnchorNodes[healthResourceIndex_] < navigation.nodeCount;
  if (healthResourceIndex_ < resourceMemory_.size() &&
      (carrierDelivery ||
       sense.self.health >= kHealthRecoveryThreshold ||
       sense.self.health >= sense.self.maxHealth ||
       !committedHealthResourceValid)) {
    clearHealthRecoveryPlan();
  }
  if (!carrierDelivery &&
      sense.self.health < kHealthRecoveryThreshold &&
      sense.self.health < sense.self.maxHealth &&
      replanSeconds_ <= dt &&
      planHealthRecovery(sense, profile, navigation)) {
    // This runs before the common replan timer decrement below. Add this
    // tick so the resulting interval matches normal A* replans.
    replanSeconds_ = dt +
      profile.planningIntervalSeconds * traits_.movementCadenceBias +
      randomFloat(RandomStream::Tactics, 0.0F, 0.12F);
  }
  if (healthResourceIndex_ < resourceMemory_.size()) {
    output.healthResourceIndex = healthResourceIndex_;
    output.healthResourceUtility = healthResourceUtility_;
    output.healthRouteCost = healthRouteCost_;
  }

  // Delivery comes before combat and recovery movement. A carrier can still
  // aim and fire after reacting, but it must not abandon its legal base goal.
  if (carrierDelivery) {
    movementGoal = sense.objective.scoringPosition;
    output.goal = BotGoalKind::Objective;
  } else if (healthResourceIndex_ < resourceMemory_.size()) {
    const std::size_t healthAnchor =
      navigation.healthAnchorNodes[healthResourceIndex_];
    movementGoal = navigation.nodes[healthAnchor].position;
    output.goal = BotGoalKind::RecoverHealth;
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
  // Combat movement uses the same movement-proven graph as every other goal.
  // Direct visible-target steering used to bypass it completely, which let a
  // target on the other side of an exposed platform pull the bot straight
  // over the edge.
  replanSeconds_ -= dt;
  if (output.goal != BotGoalKind::Safe && replanSeconds_ <= 0.0F) {
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
  if (pathCursor_ < pathCount_) {
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
  } else {
    moveTarget = sense.self.position;
  }

  Vec3 moveDelta = moveTarget - sense.self.position;
  moveDelta.z = 0.0F;
  const float moveLength = length(moveDelta);
  const bool wantsMovement = output.goal != BotGoalKind::Safe && moveLength > 0.20F;
  // Recovery is a one-tick stop. The stuck sample below can trigger another
  // recovery only after a fresh 0.50 second observation window, so a bot gets
  // a full tick to use the route that this recovery invalidated.
  stuckRecoverySeconds_ = std::max(0.0F, stuckRecoverySeconds_ - dt);
  stuckSampleSeconds_ += dt;
  if (wantsMovement && stuckSampleSeconds_ >= 0.50F) {
    if (horizontalDistance(sense.self.position, stuckSamplePosition_) < 0.12F) {
      stuckRecoverySeconds_ = dt;
      strafeDirection_ = -strafeDirection_;
      pathCount_ = 0;
      pathCursor_ = 0;
      replanSeconds_ = 0.0F;
    }
    stuckSamplePosition_ = sense.self.position;
    stuckSampleSeconds_ = 0.0F;
  }
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
    // Do not blend free-form combat dodging into a movement-proven edge.
    // Strafing, random hops, and dashes can invalidate the exact input that
    // made that edge safe near a drop. Aim and firing remain independent.
    if (output.goal == BotGoalKind::Chase && targetVisible && targetDecisionAllowed &&
        output.waypointNode >= navigation.nodeCount) {
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
    // Recovery already clears the stale route and forces an immediate replan.
    // The former blind sideways jump was useful against a wall but dangerous
    // at a ledge, so do not add unproved movement while the new path is chosen.
    command.forwardMove = 0.0F;
    command.rightMove = 0.0F;
    command.jump = false;
    command.dash = false;
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
