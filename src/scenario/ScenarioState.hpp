#pragma once

#include "net/NetProtocol.hpp"
#include "sim/Combat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lg {

struct ScenarioPlayerSetup {
  bool connected = false;
  bool bot = false;
  bool ready = false;
  Team team = Team::None;
  Vec3 position = {};
  Vec3 velocity = {};
  float viewYawRadians = 0.0F;
  float viewPitchRadians = 0.0F;
  std::int32_t health = 0;
  bool alive = false;
  bool onGround = false;
  Weapon selectedWeapon = Weapon::LightningGun;
  std::optional<WeaponAmmoArray> ammo;
};

struct ScenarioMatchSetup {
  GameMode gameMode = GameMode::Duel;
  MatchPhase phase = MatchPhase::Live;
  std::uint32_t phaseTicksRemaining = 0;
  std::uint32_t liveTicksElapsed = 0;
  std::array<std::uint16_t, kDuelPlayerCount> scores = {};
  std::array<std::uint16_t, kPlayableTeamCount> teamScores = {};
  std::array<std::uint16_t, kPlayableTeamCount> mcguffinScores = {};
  std::array<std::uint8_t, kPlayableTeamCount> mcguffinRoundsWon = {};
  std::uint8_t mcguffinRound = 0;
  std::uint8_t roundWinner = 255;
  std::uint8_t matchWinner = 255;
  Team roundWinningTeam = Team::None;
  Team matchWinningTeam = Team::None;
};

struct ScenarioSetup {
  std::uint64_t seed = 1;
  std::uint32_t serverTick = 0;
  std::array<ScenarioPlayerSetup, kDuelPlayerCount> players = {};
  ScenarioMatchSetup match = {};
};

struct ScenarioWeaponState {
  Weapon selectedWeapon = Weapon::LightningGun;
  WeaponAmmoArray ammo = {};
  LightningGunState lightningGun = {};
  LightningGunState freezeGun = {};
  double lightningAmmoCredit = 0.0;
  double freezeAmmoCredit = 0.0;
  double fractionalVampirismHealing = 0.0;
  std::uint32_t railgunCooldownTicks = 0;
  std::uint32_t revolverCooldownTicks = 0;
  float sniperAdsFraction = 0.0F;
  float sniperChargeFraction = 0.0F;
  std::uint32_t machineGunCooldownTicks = 0;
  std::uint32_t shotgunCooldownTicks = 0;
  std::uint32_t rocketCooldownTicks = 0;
  std::uint32_t grenadeCooldownTicks = 0;
  std::uint32_t plasmaGunCooldownTicks = 0;
  std::uint32_t weaponPulloutTicks = 0;
};

struct ScenarioBotState {
  int dodgeDirection = 0;
  float dodgeSwitchSeconds = 0.0F;
  float reactionSecondsRemaining = 0.0F;
  std::size_t targetPlayerIndex = kDuelPlayerCount;
  float desiredYawRadians = 0.0F;
  float desiredPitchRadians = 0.0F;
  float aimErrorYawRadians = 0.0F;
  float aimErrorPitchRadians = 0.0F;
  float nextAimErrorRefreshSeconds = 0.0F;
  bool initialized = false;
};

struct ScenarioPlayerState {
  std::uint8_t slot = 0;
  bool connected = false;
  bool bot = false;
  bool participating = false;
  bool ready = false;
  Team team = Team::None;
  bool alive = false;
  PlayerState player = {};
  ScenarioWeaponState weapon = {};
  std::uint32_t respawnTicksRemaining = 0;
  UserCommand command = {};
  ActionEdgeState consumedActionEdges = {};
  std::uint32_t viewedServerTick = 0;
  bool hasCommand = false;
  ScenarioBotState botState = {};
};

struct ScenarioProjectileState {
  std::uint8_t slot = 0;
  bool active = false;
  std::uint8_t owner = 0;
  Weapon weapon = Weapon::RocketLauncher;
  Vec3 position = {};
  Vec3 previousPosition = {};
  Vec3 velocity = {};
  float projectileRadius = 0.0F;
  float projectileHitboxRadius = 0.0F;
  bool ownerCollisionArmed = false;
  bool resting = false;
  std::uint32_t ageTicks = 0;
};

struct ScenarioHistoryState {
  std::uint32_t serverTick = 0;
  std::array<PlayerState, kDuelPlayerCount> players = {};
};

struct ScenarioMatchState {
  GameMode gameMode = GameMode::Duel;
  MatchPhase phase = MatchPhase::WaitingForPlayers;
  std::uint32_t phaseTicksRemaining = 0;
  std::uint32_t liveTicksElapsed = 0;
  std::array<std::uint16_t, kDuelPlayerCount> scores = {};
  std::array<std::uint16_t, kPlayableTeamCount> teamScores = {};
  std::array<std::uint16_t, kPlayableTeamCount> mcguffinScores = {};
  std::array<std::uint8_t, kPlayableTeamCount> mcguffinRoundsWon = {};
  std::uint8_t mcguffinRound = 0;
  std::uint8_t roundWinner = 255;
  std::uint8_t matchWinner = 255;
  Team roundWinningTeam = Team::None;
  Team matchWinningTeam = Team::None;
  std::array<RoundCombatStats, kDuelPlayerCount> roundCombatStats = {};
  std::array<RoundCombatStats, kDuelPlayerCount> matchCombatStats = {};
};

// This is an opt-in copy for scenario checks. The normal server tick does not
// build it or keep a second state tree in sync.
struct ScenarioState {
  std::uint32_t serverTick = 0;
  std::uint32_t mapRevision = 0;
  std::string mapName;
  std::uint32_t mapContentHash = 0;
  std::array<ScenarioPlayerState, kDuelPlayerCount> players = {};
  std::array<ScenarioProjectileState, kMaxRocketProjectiles> projectiles = {};
  ScenarioMatchState match = {};
  std::array<bool, Arena::kHealthPickupCount> healthPickupAvailable = {};
  std::array<std::uint32_t, Arena::kHealthPickupCount> healthPickupCooldownTicks = {};
  IcePoolArray icePools = {};
  McGuffinObjective mcguffin = {};
  Team mcguffinRedBaseOwner = Team::Red;
  Team mcguffinBlueBaseOwner = Team::Blue;
  std::array<std::uint32_t, kDuelPlayerCount> mcguffinStealTicks = {};
  std::uint32_t mcguffinCarrySubPoints = 0;
  std::uint16_t mcguffinCarriedPoints = 0;
  std::uint32_t mcguffinFinalHoldTicks = 0;
  std::uint32_t mcguffinRoundLiveTicks = 0;
  std::uint32_t mcguffinThrowPickupLockoutTicks = 0;
  std::uint32_t botRandomState = 0;
  std::uint32_t spawnRandomState = 0;
  std::array<std::uint32_t, kDuelPlayerCount> rocketExplosionSequences = {};
  std::array<std::uint32_t, kDuelPlayerCount> fragEventSequences = {};
  std::array<std::uint32_t, kDuelPlayerCount> localHitFeedbackSequences = {};
  std::array<std::uint32_t, kDuelPlayerCount> footstepSequences = {};
  std::array<std::uint32_t, kMaxRocketProjectiles> grenadeBounceSequences = {};
  std::array<std::uint32_t, Arena::kTeamSpawnCount> spawnLastUsedTicks = {};
  std::array<bool, Arena::kTeamSpawnCount> spawnWasUsed = {};
  std::size_t nextDeathmatchSpawnIndex = 0;
  bool playersColliding = false;
  std::vector<ScenarioHistoryState> history;
};

} // namespace lg
