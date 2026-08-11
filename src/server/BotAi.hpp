#pragma once

#include "net/NetProtocol.hpp"
#include "shared/Constants.hpp"
#include "shared/Math.hpp"
#include "sim/UserCommand.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace lg {

struct Arena;
struct MovementTuning;
struct CollisionBounds;

// Bot code below this line receives only these filtered values. In particular,
// it cannot read ServerGame, ServerSnapshot, or a non-visible opponent pose.
// The game has no sound-perception API, so this boundary intentionally carries
// no inferred or exact sound cues.
struct BotSelfSense {
  Vec3 position = {};
  Vec3 velocity = {};
  float viewYawRadians = 0.0F;
  float viewPitchRadians = 0.0F;
  float radius = 0.35F;
  float halfHeight = 0.9F;
  int health = 100;
  bool onGround = false;
  bool dashReady = false;
};

struct BotObservedEnemy {
  std::uint8_t playerIndex = kNoAssignedPlayer;
  Vec3 position = {};
  // A bot gets only a visible position sample and the tick when it saw it.
  // BotBrain derives a bounded estimate from two such samples; do not add
  // authoritative player velocity here.
  std::uint32_t observationServerTick = 0;
  bool onGround = false;
  // Static-world trace behind a currently visible target; it is not a hidden
  // entity fact and lets splash weapons value a visible nearby surface.
  bool nearbySplashSurface = false;
};

struct BotHealthResourceSense {
  std::uint8_t resourceIndex = 0;
  Vec3 position = {};
  int value = 0;
  // This is the state seen now, never the authoritative state of a hidden
  // pickup. Memory below may retain it after LOS loss.
  bool available = false;
};

struct BotWeaponSense {
  bool usable = false;
  bool infiniteAmmo = false;
  float effectiveRange = 0.0F;
  float damagePerShot = 0.0F;
  float fireIntervalSeconds = 0.0F;
  float projectileSpeed = 0.0F;
  float splashRadius = 0.0F;
  float splashDamage = 0.0F;
  float cooldownSeconds = 0.0F;
  float switchCostSeconds = 0.0F;
};

// These values describe the current visible target or the bot's own state.
// They contain no enemy health or hidden movement state.
struct BotCombatContext {
  float targetDistance = 0.0F;
  float angularErrorRadians = 0.0F;
  float targetLateralSpeed = 0.0F;
  float exposureAgeSeconds = 0.0F;
  int selfHealth = 100;
  bool targetGrounded = false;
  bool nearbySplashSurface = false;
};

struct BotWeaponScore {
  float rangeFit = 0.0F;
  float hitChance = 0.0F;
  float damageRate = 0.0F;
  float projectileDifficulty = 0.0F;
  float splashValue = 0.0F;
  float selfRisk = 0.0F;
  // Readiness applies to every weapon, including the current one. It is not
  // a weapon-switch cost.
  float cooldownPenalty = 0.0F;
  float switchCost = 0.0F;
  float total = -std::numeric_limits<float>::infinity();
};

[[nodiscard]] BotWeaponScore scoreBotWeapon(
  const BotWeaponSense& weapon,
  const BotCombatContext& context,
  float preferredRange,
  bool isCurrentWeapon
);

struct BotObjectiveSense {
  Vec3 position = {};
  Vec3 scoringPosition = {};
  bool active = false;
  bool carrying = false;
  bool hasScoringPosition = false;
};

struct BotSenseFrame {
  std::uint32_t serverTick = 0;
  float fixedDt = 0.0F;
  // The server refreshes LOS/FOV traces at a deterministic lower cadence.
  // The brain still ticks the motor at 125 Hz, but only a fresh sample may
  // start acquisition or allow an attack.
  bool perceptionFresh = true;
  // During a cached frame the server may pass only this current LOS/FOV
  // result for the motor's already-known target. It carries no target pose or
  // velocity and lets held beam input stay in the ordinary command path.
  std::uint8_t attackTargetPlayerIndex = kNoAssignedPlayer;
  bool attackTargetCurrentlyVisible = false;
  BotSelfSense self = {};
  std::array<BotObservedEnemy, kDuelPlayerCount> visibleEnemies = {};
  std::size_t visibleEnemyCount = 0;
  std::array<BotHealthResourceSense, 32> healthResources = {};
  std::size_t healthResourceCount = 0;
  BotObjectiveSense objective = {};
  std::array<BotWeaponSense, kWeaponCount> weapons = {};
  Weapon selectedWeapon = Weapon::MachineGun;
  bool forceWeapon = false;
  Weapon forcedWeapon = Weapon::MachineGun;
  bool combatEnabled = true;
  bool standstill = false;
  bool dodgeOverride = false;
  int dodgeMinIntervalMs = 250;
  int dodgeMaxIntervalMs = 750;
};

// Values are deliberately bounded. Difficulty changes only perception timing,
// planning, and input quality; it never changes player simulation values.
struct BotDifficultyProfile {
  float reactionMinSeconds = 0.30F;
  float reactionMaxSeconds = 0.50F;
  float maxTurnRadiansPerSecond = 1.35F;
  float turnAccelerationRadiansPerSecond2 = 8.0F;
  float trackingErrorRadians = 0.11F;
  float fireToleranceRadians = 0.040F;
  float predictionSeconds = 0.05F;
  float memorySeconds = 1.25F;
  float planningIntervalSeconds = 0.65F;
  // Physical vision is deliberately the same normal 108 degree yaw+pitch
  // cone for every difficulty. Skill changes timing and motor quality only.
  float targetFovDegrees = 108.0F;
  float preferredRange = 7.0F;
  float strafeStrength = 0.45F;
  float dashChancePerSecond = 0.0F;
};

[[nodiscard]] BotDifficultyProfile botDifficultyProfile(BotAttackMode mode);

enum class BotNavLinkKind : std::uint8_t {
  Walk,
  Step,
  Jump,
  JumpPad,
  Teleport,
};

// A failed special route remains visible to offline validation. These stages
// identify the first normal-movement proof that did not succeed; they never
// turn a failed trigger into a graph edge.
enum class BotNavSpecialFailureStage : std::uint8_t {
  None,
  EntrySearch,
  TriggerActivation,
  Landing,
  NodeCapacity,
};

struct BotNavNode {
  Vec3 position = {};
};

struct BotNavLink {
  std::uint16_t from = 0;
  std::uint16_t to = 0;
  BotNavLinkKind kind = BotNavLinkKind::Walk;
};

// A special route records the two player origins produced by normal movement:
// the grounded trigger entry and the grounded point reached after the trigger.
// It remains server-local so offline validation can prove each directed route.
struct BotNavSpecialRoute {
  std::uint16_t entryNode = UINT16_MAX;
  std::uint16_t exitNode = UINT16_MAX;
  bool verified = false;
  BotNavSpecialFailureStage failureStage = BotNavSpecialFailureStage::None;
};

// These values are generated at map load so a strict validator can report
// real directed reach without asking the 125 Hz bot motor to do extra work.
struct BotNavAnchorReach {
  std::uint16_t node = UINT16_MAX;
  std::uint16_t weakComponent = UINT16_MAX;
  std::uint16_t directedReach = 0;
};

enum class BotNavAnchorKind : std::uint8_t {
  Spawn,
  TeamSpawn,
  Health,
  NeutralObjective,
  RedBase,
  BlueBase,
  JumpPadEntry,
  JumpPadLanding,
  TeleportEntry,
  TeleportLanding,
};

struct BotNavRequiredAnchor {
  BotNavAnchorKind kind = BotNavAnchorKind::Spawn;
  std::uint16_t sourceIndex = 0;
  std::uint16_t node = UINT16_MAX;
};

// The fixed storage makes tactical ticks allocation-free. The builder samples
// at map-load time and drops excess samples deterministically.
struct BotNavigationMap {
  // Imported maps need 2048 bulk nodes before adaptive local refinement. A
  // fixed 512-node reserve then covers a bounded collision-settled target
  // flood without moving work into the bot tick or making map size an
  // unbounded input.
  static constexpr std::size_t kMaxNodes = 2560;
  static constexpr std::size_t kMaxLinks = kMaxNodes * 10U;
  std::array<BotNavNode, kMaxNodes> nodes = {};
  std::array<BotNavLink, kMaxLinks> links = {};
  // Built once at map load. Tactical replans use this fixed adjacency rather
  // than scanning every link for each popped path node.
  std::array<std::uint16_t, kMaxNodes> outgoingLinkHead = {};
  std::array<std::uint16_t, kMaxLinks> outgoingLinkNext = {};
  bool outgoingLinksPrepared = false;
  std::size_t nodeCount = 0;
  std::size_t linkCount = 0;
  // Required semantic anchors are inserted before bulk grid samples. A false
  // value makes a capacity or standability loss visible to map validation.
  bool requiredAnchorsComplete = true;
  std::size_t requiredAnchorCount = 0;
  std::size_t missingRequiredAnchorCount = 0;
  // Build diagnostics stay server-local. A round-robin flood does all work at
  // map load, never in the 125 Hz motor loop.
  std::size_t localLinkCount = 0;
  std::size_t localTraversalTrials = 0;
  std::size_t localBroadphaseRejects = 0;
  std::size_t unreachableAnchorNodes = 0;
  std::size_t regionSeedCount = 0;
  std::size_t regionExpansionWork = 0;
  std::size_t regionNodeCount = 0;
  std::size_t nodeCapacityRejects = 0;
  std::size_t linkCapacityRejects = 0;
  std::size_t localGroundedRejects = 0;
  std::size_t localTraversalRejects = 0;
  std::size_t localBroadphaseRetries = 0;
  std::size_t localTraversalSimulationTicks = 0;
  std::size_t localTraversalStallRejects = 0;
  std::size_t localSimpleWalkProofTrials = 0;
  std::size_t localSimpleWalkProofTicks = 0;
  std::size_t localSimpleWalkRejects = 0;
  std::size_t healthApproachGroundedCandidates = 0;
  std::size_t healthApproachSimulationTrials = 0;
  std::size_t healthGraphApproachSimulationTrials = 0;
  std::size_t surfaceApproachProbeTrials = 0;
  std::size_t surfaceApproachProbeLinks = 0;
  std::size_t surfaceApproachTargetCount = 0;
  std::size_t surfaceApproachBridgeTrials = 0;
  std::size_t surfaceApproachBridgeLinks = 0;
  std::size_t surfaceApproachFloodNodes = 0;
  std::size_t surfaceApproachFloodWork = 0;
  bool surfaceApproachFloodExhausted = false;
  std::size_t surfaceDropProbeTrials = 0;
  std::size_t surfaceDropProbeLinks = 0;
  bool regionWorkExhausted = false;
  bool regionTaskCapacityReached = false;
  // A health node normally rests inside the pickup touch volume. If the item
  // has no legal resting center, an entry/landing pair records the simulated
  // walk or jump that crossed it instead. UINT16_MAX means no proof exists.
  std::array<std::uint16_t, 32> healthAnchorNodes = {};
  std::array<std::uint16_t, 32> healthApproachEntryNodes = {};
  // Only a failed health anchor receives this bounded collision diagnostic.
  // It never changes validation: it makes an authored occlusion explicit.
  std::array<bool, 32> healthTouchVolumeOccluded = {};
  std::array<std::uint32_t, 32> healthTouchVolumeProofs = {};
  std::array<bool, 32> healthTouchVolumeFreeCenterFound = {};
  std::array<Vec3, 32> healthTouchVolumeFirstFreeCenter = {};
  std::array<BotNavRequiredAnchor, kMaxNodes> requiredAnchors = {};
  std::size_t semanticAnchorCount = 0;
  std::array<BotNavAnchorReach, kMaxNodes> anchorReach = {};
  std::size_t jumpPadRouteCount = 0;
  std::size_t teleportRouteCount = 0;
  std::array<BotNavSpecialRoute, 48> jumpPadRoutes = {};
  std::array<BotNavSpecialRoute, 16> teleportRoutes = {};
};

// This is the sole authoritative-to-static-map boundary. It proves walk and
// jump links by running the normal movement simulation with player bounds.
[[nodiscard]] BotNavigationMap buildBotNavigationMap(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds
);

// Manual test maps may call this after populating links. Normal map builds
// call it before returning, so bot replans stay allocation-free and indexed.
void prepareBotNavigationMap(BotNavigationMap& map);

[[nodiscard]] std::size_t nearestBotNavNode(
  const BotNavigationMap& map,
  Vec3 position
);

enum class BotGoalKind : std::uint8_t {
  Safe,
  Chase,
  RecoverHealth,
  Objective,
  Explore,
};

enum class BotNoFireReason : std::uint8_t {
  None,
  Disabled,
  Reaction,
  NoVisibleTarget,
  Turning,
  WeaponUnavailable,
};

struct BotMotor {
  UserCommand command = {};
  BotGoalKind goal = BotGoalKind::Safe;
  BotNoFireReason noFireReason = BotNoFireReason::NoVisibleTarget;
  std::uint8_t targetPlayerIndex = kNoAssignedPlayer;
  Vec3 lastKnownTargetPosition = {};
  float targetMemoryAgeSeconds = std::numeric_limits<float>::infinity();
  std::size_t waypointNode = BotNavigationMap::kMaxNodes;
  std::size_t observedHealthResourceCount = 0;
  bool recoveredFromStuck = false;
  std::array<BotWeaponScore, kWeaponCount> weaponScores = {};
  float selectedWeaponScore = -std::numeric_limits<float>::infinity();
};

struct BotTraits {
  float aggression = 1.0F;
  float risk = 1.0F;
  float preferredRangeBias = 1.0F;
  float movementCadenceBias = 1.0F;
  float reactionLatencyOffsetSeconds = 0.0F;
  float aimBiasScale = 1.0F;
};

class BotBrain {
public:
  void reset(std::uint32_t seed);
  [[nodiscard]] const BotTraits& traits() const;
  // Server-local reproducibility probe. It is intentionally not a snapshot
  // field and covers memory, aim, path, recovery, traits, and RNG streams.
  [[nodiscard]] std::uint64_t deterministicHash() const;
  [[nodiscard]] BotMotor tick(
    const BotSenseFrame& sense,
    const BotDifficultyProfile& profile,
    const BotNavigationMap& navigation
  );

private:
  struct Memory {
    Vec3 position = {};
    Vec3 velocity = {};
    float ageSeconds = std::numeric_limits<float>::infinity();
    float confidence = 0.0F;
    std::uint32_t lastObservationServerTick = 0;
    bool hasObservation = false;
    bool valid = false;
  };

  struct ResourceMemory {
    Vec3 position = {};
    int value = 0;
    float ageSeconds = std::numeric_limits<float>::infinity();
    bool available = false;
    bool valid = false;
  };

  enum class RandomStream : std::uint8_t { Tactics, Movement, Aim };
  [[nodiscard]] std::uint32_t randomU32(RandomStream stream);
  [[nodiscard]] float randomFloat(
    RandomStream stream,
    float minValue,
    float maxValue
  );
  [[nodiscard]] bool planPath(
    const BotNavigationMap& navigation,
    Vec3 start,
    Vec3 target
  );
  [[nodiscard]] Weapon chooseWeapon(
    const BotSenseFrame& sense,
    const BotCombatContext& context,
    float preferredRange,
    std::array<BotWeaponScore, kWeaponCount>& scores
  ) const;

  std::array<Memory, kDuelPlayerCount> memory_ = {};
  std::array<ResourceMemory, 32> resourceMemory_ = {};
  std::array<std::uint16_t, BotNavigationMap::kMaxNodes> path_ = {};
  std::size_t pathCount_ = 0;
  std::size_t pathCursor_ = 0;
  std::size_t lastWaypoint_ = BotNavigationMap::kMaxNodes;
  float replanSeconds_ = 0.0F;
  float reactionSeconds_ = 0.0F;
  float aimVelocityYaw_ = 0.0F;
  float aimVelocityPitch_ = 0.0F;
  float aimBiasYaw_ = 0.0F;
  float aimBiasPitch_ = 0.0F;
  float aimBiasRefreshSeconds_ = 0.0F;
  float strafeSeconds_ = 0.0F;
  float stuckSampleSeconds_ = 0.0F;
  float stuckRecoverySeconds_ = 0.0F;
  Vec3 stuckSamplePosition_ = {};
  int strafeDirection_ = 1;
  std::uint8_t targetPlayerIndex_ = kNoAssignedPlayer;
  std::size_t patrolNode_ = BotNavigationMap::kMaxNodes;
  Vec3 carrierObjectiveDestination_ = {};
  BotTraits traits_ = {};
  std::uint32_t tacticsRandomState_ = 0xB07D0D6EU;
  std::uint32_t movementRandomState_ = 0x51A7E123U;
  std::uint32_t aimRandomState_ = 0xA11CE55DU;
  bool hasCarrierObjectiveDestination_ = false;
  bool targetWasVisible_ = false;
  bool initialized_ = false;
};

} // namespace lg
