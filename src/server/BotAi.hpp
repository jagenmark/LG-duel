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
  Vec3 velocity = {};
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
  float effectiveRange = 0.0F;
  float projectileSpeed = 0.0F;
  bool splash = false;
};

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

struct BotNavNode {
  Vec3 position = {};
};

struct BotNavLink {
  std::uint16_t from = 0;
  std::uint16_t to = 0;
  BotNavLinkKind kind = BotNavLinkKind::Walk;
};

// The fixed storage makes tactical ticks allocation-free. The builder samples
// at map-load time and drops excess samples deterministically.
struct BotNavigationMap {
  static constexpr std::size_t kMaxNodes = 512;
  static constexpr std::size_t kMaxLinks = kMaxNodes * 10U;
  std::array<BotNavNode, kMaxNodes> nodes = {};
  std::array<BotNavLink, kMaxLinks> links = {};
  std::size_t nodeCount = 0;
  std::size_t linkCount = 0;
};

// This is the sole authoritative-to-static-map boundary. It proves walk and
// jump links by running the normal movement simulation with player bounds.
[[nodiscard]] BotNavigationMap buildBotNavigationMap(
  const Arena& arena,
  const MovementTuning& movement,
  CollisionBounds bounds
);

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
};

class BotBrain {
public:
  void reset(std::uint32_t seed);
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
    bool valid = false;
  };

  struct ResourceMemory {
    Vec3 position = {};
    int value = 0;
    float ageSeconds = std::numeric_limits<float>::infinity();
    bool available = false;
    bool valid = false;
  };

  [[nodiscard]] std::uint32_t randomU32();
  [[nodiscard]] float randomFloat(float minValue, float maxValue);
  void planPath(
    const BotNavigationMap& navigation,
    Vec3 start,
    Vec3 target
  );
  [[nodiscard]] Weapon chooseWeapon(
    const BotSenseFrame& sense,
    float targetDistance
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
  std::uint32_t randomState_ = 0xB07D0D6EU;
  bool targetWasVisible_ = false;
  bool initialized_ = false;
};

} // namespace lg
