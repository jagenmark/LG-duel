#pragma once

#include "net/NetProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace lg::replay {

// The replay file stores inputs after server acceptance.  This boundary makes
// bots ordinary recorded actors and keeps their private planning state out of
// both hashes and checkpoints.
inline constexpr std::uint16_t kReplayFormatVersionV1 = 1;
inline constexpr std::uint16_t kReplayFormatVersion = 2;
inline constexpr std::uint16_t kReplayTickRate = 125;
// Saved demos may cover a full high-player match. Killcam transfer has its own
// much smaller cap in ReplayTransfer.hpp.
inline constexpr std::size_t kMaxReplayBytes = 512U * 1024U * 1024U;
// ReplayRecorder retains native, fixed-slot frames before it writes the sparse
// file. This hard cap covers ten minutes at 125 Hz with bounded checkpoints.
inline constexpr std::size_t kMaxReplayResidentBytes = 512U * 1024U * 1024U;
inline constexpr std::size_t kMaxReplayChunkBytes = 8U * 1024U * 1024U;
inline constexpr std::size_t kMaxReplayTicks = 4U * 1024U * 1024U;
inline constexpr std::size_t kMaxReplayCheckpoints = 4096U;
inline constexpr std::size_t kMaxReplayHistoryFrames = 256U;
inline constexpr std::size_t kMaxReplayNameBytes = kMaxPlayerNameBytes;
inline constexpr std::size_t kMaxReplayMapNameBytes = kMaxMapNameBytes;
inline constexpr std::uint8_t kNoReplayPlayer = 255U;

enum class ReplayChunkType : std::uint8_t {
  TickInputs = 1,
  Checkpoint = 2,
  StateHash = 3,
  LethalEvent = 4,
};

enum class LethalKind : std::uint8_t {
  Direct = 0,
  Splash = 1,
  Self = 2,
  World = 3,
};

enum class ReplayVisibility : std::uint8_t {
  // A full demo is for local development, testing, and approved spectators.
  DeveloperFull = 0,
  // Safe unfiltered remote replay is limited to the two-player duel mode.
  DuelOnly = 1,
};

struct ReplayPlayerMetadata {
  std::uint8_t slot = 0;
  bool occupied = false;
  bool bot = false;
  Team team = Team::None;
  std::string name;
};

struct ReplayMetadata {
  std::uint32_t formatFlags = 0;
  std::uint32_t protocolRevision = 0;
  std::uint64_t buildFingerprint = 0;
  std::uint64_t gameplayConfigHash = 0;
  std::uint32_t initialServerTick = 0;
  std::uint32_t mapRevision = 1;
  std::string mapName;
  std::uint32_t mapContentHash = 0;
  GameMode gameMode = GameMode::Duel;
  MatchRules matchRules = {};
  ReplayVisibility visibility = ReplayVisibility::DeveloperFull;
  std::array<ReplayPlayerMetadata, kDuelPlayerCount> players = {};
};

// This contains every input that can change gameplay for a player on a tick.
// command is the final validated command, not a UDP packet or bot decision.
struct ReplaySlotInput {
  bool present = false;
  bool hasCommand = false;
  bool receivedThisTick = false;
  UserCommand command = {};
  std::uint32_t viewedServerTick = 0;
  ActionEdgeState consumedActionEdges = {};
  bool jumpEdgeAccepted = false;
  bool dashEdgeAccepted = false;
  bool attackEdgeAccepted = false;
  UserCommand attackEdgeCommand = {};
  std::uint32_t attackEdgeViewedServerTick = 0;
  bool mcguffinThrowAccepted = false;
  UserCommand mcguffinThrowCommand = {};
};

struct ReplayTickInput {
  std::uint32_t tick = 0;
  std::array<ReplaySlotInput, kDuelPlayerCount> slots = {};
};

struct ReplayWeaponState {
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

struct ReplayCheckpointPlayer {
  bool connected = false;
  bool participating = false;
  bool ready = false;
  Team team = Team::None;
  PlayerState player = {};
  ReplayWeaponState weapon = {};
  std::uint32_t respawnTicksRemaining = 0;
  UserCommand command = {};
  ActionEdgeState consumedActionEdges = {};
  std::uint32_t viewedServerTick = 0;
  bool hasCommand = false;
};

struct ReplayProjectile {
  bool active = false;
  std::uint8_t owner = 0;
  std::uint32_t sequence = 0;
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

struct ReplayHistoryFrame {
  std::uint32_t serverTick = 0;
  std::array<PlayerState, kDuelPlayerCount> players = {};
};

// Footstep state is gameplay-adjacent sequence state. Persist it so a seek
// resumes cadence and audio sequence numbering exactly at the checkpoint.
struct ReplayFootstepState {
  Vec3 previousPosition = {};
  float distanceSinceStep = 0.0F;
  bool wasOnGround = false;
  bool initialized = false;
};

struct ReplayMatchState {
  GameMode gameMode = GameMode::Duel;
  MatchPhase phase = MatchPhase::WaitingForPlayers;
  std::uint32_t phaseTicksRemaining = 0;
  std::uint32_t liveTicksElapsed = 0;
  bool overtime = false;
  std::array<std::uint16_t, kDuelPlayerCount> scores = {};
  std::array<std::uint16_t, kPlayableTeamCount> teamScores = {};
  std::array<std::uint16_t, kPlayableTeamCount> mcguffinScores = {};
  std::array<std::uint8_t, kPlayableTeamCount> mcguffinRoundsWon = {};
  std::uint8_t mcguffinRound = 0;
  std::uint8_t roundWinner = kNoReplayPlayer;
  std::uint8_t matchWinner = kNoReplayPlayer;
  Team roundWinningTeam = Team::None;
  Team matchWinningTeam = Team::None;
  std::array<RoundCombatStats, kDuelPlayerCount> roundCombatStats = {};
  std::array<RoundCombatStats, kDuelPlayerCount> matchCombatStats = {};
};

// This deliberately does not contain ScenarioState, bot RNG, bot combat
// state, transport state, renderer state, or wall clock values.
struct ReplayCheckpoint {
  std::uint32_t serverTick = 0;
  std::uint32_t mapRevision = 1;
  std::uint32_t projectileRevision = 1;
  std::uint64_t gameplayConfigHash = 0;
  std::array<ReplayCheckpointPlayer, kDuelPlayerCount> players = {};
  std::array<ReplayProjectile, kMaxRocketProjectiles> projectiles = {};
  ReplayMatchState match = {};
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
  std::uint32_t spawnRandomState = 1;
  std::array<std::uint32_t, kDuelPlayerCount> projectileSequences = {};
  std::array<std::uint32_t, kDuelPlayerCount> rocketExplosionSequences = {};
  std::array<std::uint32_t, kDuelPlayerCount> fragEventSequences = {};
  std::array<std::uint32_t, kDuelPlayerCount> localHitFeedbackSequences = {};
  std::array<std::uint32_t, kDuelPlayerCount> footstepSequences = {};
  std::array<ReplayFootstepState, kDuelPlayerCount> footstepStates = {};
  std::array<std::uint32_t, kDuelPlayerCount> grenadeBounceEventSequences = {};
  std::array<std::uint32_t, kMaxRocketProjectiles> grenadeBounceSequences = {};
  std::array<std::uint32_t, Arena::kTeamSpawnCount> spawnLastUsedTicks = {};
  std::array<bool, Arena::kTeamSpawnCount> spawnWasUsed = {};
  std::uint32_t nextDeathmatchSpawnIndex = 0;
  bool playersColliding = false;
  std::vector<ReplayHistoryFrame> history;
};

struct ReplayStateHash {
  std::uint32_t tick = 0;
  std::uint64_t value = 0;
};

struct ReplayLethalEvent {
  std::uint32_t tick = 0;
  std::uint32_t replayGeneration = 0;
  std::uint8_t victim = kNoReplayPlayer;
  std::uint8_t killer = kNoReplayPlayer;
  Weapon weapon = Weapon::LightningGun;
  std::uint32_t projectileSequence = 0;
  LethalKind kind = LethalKind::Direct;
};

struct ReplayDemo {
  ReplayMetadata metadata = {};
  std::vector<ReplayTickInput> ticks;
  std::vector<ReplayCheckpoint> checkpoints;
  std::vector<ReplayStateHash> hashes;
  std::vector<ReplayLethalEvent> lethalEvents;
};

} // namespace lg::replay
