#pragma once

#include "sim/Arena.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/Movement.hpp"
#include "sim/UserCommand.hpp"
#include "sim/WeaponRuntime.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lg {

enum class AimTrainerPhase : std::uint8_t { Idle, Armed, Running, Results };
enum class AimTargetVisual : std::uint8_t { Orb, Worker };
enum class AimTargetLife : std::uint8_t { Invincible, OneHit, Health };
enum class AimSpawnMode : std::uint8_t { FixedList, BoundedRandom };
enum class AimTargetMotion : std::uint8_t { Stationary, Strafe, RandomWaypoint };
enum class AimScoreMode : std::uint8_t { Hit, Damage, Clear };
enum class AimPlayerMovement : std::uint8_t { Locked, Normal };
enum class AimWeaponPolicy : std::uint8_t { All, Forced };

struct AimColor {
  std::uint8_t red = 80;
  std::uint8_t green = 220;
  std::uint8_t blue = 255;
};

struct AimTargetGroup {
  std::string name = "target";
  AimTargetVisual visual = AimTargetVisual::Orb;
  AimTargetLife life = AimTargetLife::OneHit;
  AimSpawnMode spawnMode = AimSpawnMode::BoundedRandom;
  AimTargetMotion motion = AimTargetMotion::Stationary;
  AimColor color = {};
  float radius = 0.35F;
  std::uint32_t count = 1;
  std::int32_t health = 100;
  std::uint32_t respawnDelayTicks = 0;
  std::vector<Vec3> fixedSpawns;
  Vec3 randomMinimum = {4.0F, -3.0F, 1.0F};
  Vec3 randomMaximum = {10.0F, 3.0F, 4.0F};
  Vec3 strafeDirection = {0.0F, 1.0F, 0.0F};
  float strafeSpeed = 0.0F;
  std::uint32_t waypointTicks = 125;
};

struct AimScenario {
  static constexpr std::uint32_t kVersion = 1;
  static constexpr std::size_t kMaxGroups = 64;
  static constexpr std::uint32_t kMaxTargetsPerGroup = 64;
  static constexpr std::size_t kMaxTargets =
    kMaxGroups * static_cast<std::size_t>(kMaxTargetsPerGroup);
  std::uint32_t version = kVersion;
  std::string name = "60s Orb";
  std::uint32_t durationTicks = 7500;
  AimPlayerMovement playerMovement = AimPlayerMovement::Locked;
  AimWeaponPolicy weaponPolicy = AimWeaponPolicy::All;
  Weapon forcedWeapon = Weapon::LightningGun;
  std::array<bool, kWeaponCount> allowedWeapons = {
    {true, true, true, true, true, true, true, true, true}
  };
  bool infiniteAmmo = true;
  AimScoreMode scoreMode = AimScoreMode::Hit;
  std::uint32_t hitScore = 1;
  std::uint32_t damageScorePerPoint = 1;
  std::uint32_t clearScore = 1;
  std::uint64_t seed = 1;
  std::string mapName = "aim_trainer";
  std::uint64_t mapIdentity = 0;
  std::uint64_t balanceIdentity = 0;
  std::vector<AimTargetGroup> groups = {AimTargetGroup{}};
};

struct AimTrainerStats {
  std::uint64_t score = 0;
  std::uint64_t damage = 0;
  std::uint32_t clears = 0;
  std::uint32_t attempts = 0;
  std::uint32_t hits = 0;
  std::uint32_t pelletAttempts = 0;
  std::uint32_t pelletHits = 0;
  std::uint32_t beamAttempts = 0;
  std::uint32_t beamHits = 0;
  std::uint32_t projectileAttempts = 0;
  std::uint32_t projectileHits = 0;
  [[nodiscard]] float accuracyPercent() const;
  [[nodiscard]] float scorePerMinute(std::uint32_t elapsedTicks) const;
};

struct AimTargetView {
  std::uint32_t id = 0;
  std::uint32_t groupIndex = 0;
  AimTargetVisual visual = AimTargetVisual::Orb;
  AimColor color = {};
  Vec3 position = {};
  float radius = 0.35F;
  PlayerState worker = {};
  std::int32_t health = 0;
  bool active = false;
};

struct AimTrainerResult {
  std::uint64_t scenarioFingerprint = 0;
  std::uint64_t score = 0;
  std::uint64_t damage = 0;
  std::uint32_t clears = 0;
  std::uint32_t attempts = 0;
  std::uint32_t hits = 0;
  std::uint32_t durationTicks = 0;
  std::uint64_t seed = 0;
  bool ranked = true;
};

struct AimTrainerProjectileView {
  bool active = false;
  Weapon weapon = Weapon::RocketLauncher;
  Vec3 position = {};
  Vec3 velocity = {};
  float radius = 0.0F;
};

struct AimTrainerFrame {
  AimTrainerPhase phase = AimTrainerPhase::Idle;
  PlayerState player = {};
  Weapon selectedWeapon = Weapon::LightningGun;
  std::uint32_t elapsedTicks = 0;
  std::uint32_t remainingTicks = 0;
  AimTrainerStats stats = {};
  std::vector<AimTargetView> targets;
  std::vector<AimTrainerProjectileView> projectiles;
  std::vector<WeaponFireResult> pendingFires;
  IcePoolArray icePools = {};
  WeaponFireResult latestFire = {};
  LightningGunResult latestBeam = {};
  WeaponAmmoArray ammo = {};
  bool fireEventPending = false;
  bool naturalCompletion = false;
  bool storageWarning = false;
  std::string message;
  AimTrainerResult result = {};
};

struct AimTrainerArmResult {
  bool ok = false;
  std::string error;
};

// A transport-free local gameplay simulation. The public header deliberately
// contains no network protocol, socket, or client-session types.
class AimTrainer {
public:
  AimTrainer(Arena arena, BalanceConfig balance, MovementTuning movement = {});

  [[nodiscard]] AimTrainerArmResult arm(const AimScenario& scenario);
  [[nodiscard]] bool start();
  [[nodiscard]] const AimTrainerFrame& tick(const UserCommand& command);
  [[nodiscard]] const AimTrainerFrame& view() const;
  void consumePresentationEvents();
  void markStorageWarning(std::string message);
  void abort();

  [[nodiscard]] static std::uint64_t scenarioFingerprint(const AimScenario& scenario);
  [[nodiscard]] static std::uint64_t balanceFingerprint(
    const BalanceConfig& balance,
    const MovementTuning& movement = {}
  );

private:
  struct TargetRuntime {
    AimTargetView view = {};
    std::uint32_t respawnTicks = 0;
    std::uint32_t spawnOrdinal = 0;
    Vec3 waypoint = {};
    std::uint32_t nextWaypointTick = 0;
    float strafeDirectionSign = 1.0F;
  };

  [[nodiscard]] bool validateScenario(const AimScenario& scenario, std::string& error) const;
  void resetRun();
  void respawnTarget(TargetRuntime& target);
  void updateTargetMotion(TargetRuntime& target);
  [[nodiscard]] std::uint32_t randomU32();
  [[nodiscard]] float randomFloat(float minimum, float maximum);
  [[nodiscard]] WeaponRuntimeTarget runtimeTarget(const TargetRuntime& target) const;
  void refreshTargetViews();
  void applyHitBatch(const WeaponRuntimeTick& tick);
  void makeResult(bool ranked);
  void finishNaturally();

  Arena arena_;
  BalanceConfig balance_;
  MovementTuning movement_;
  AimScenario scenario_;
  AimTrainerFrame frame_;
  PlayerState startPlayer_;
  WeaponRuntimeState weapons_;
  WeaponRuntimeConfig weaponConfig_;
  std::vector<TargetRuntime> targets_;
  std::uint32_t randomState_ = 1;
  bool resultRecorded_ = false;
};

} // namespace lg
