#pragma once

#include "shared/Constants.hpp"
#include "sim/Combat.hpp"
#include "sim/Arena.hpp"
#include "sim/GameMode.hpp"
#include "sim/IcePool.hpp"
#include "sim/MapRegistry.hpp"
#include "sim/Movement.hpp"
#include "sim/McGuffinRules.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lg {

inline constexpr std::size_t kDuelPlayerCount = kMaxPlayers;
inline constexpr std::size_t kMaxSpectatorClients = 8;
inline constexpr std::size_t kMaxNetworkClients =
  kDuelPlayerCount + kMaxSpectatorClients;
inline constexpr std::uint8_t kNoAssignedPlayer = 255;
inline constexpr std::size_t kMaxBundledCommands = 12;
inline constexpr std::size_t kMaxCommandDatagramBytes = 1200;
inline constexpr std::size_t kLocalHitFeedbackEventWindow = 4;
inline constexpr std::size_t kMaxChatMessageBytes = 240;
inline constexpr std::size_t kMaxPlayerNameBytes = 20;
inline constexpr std::size_t kChatHistoryCapacity = 40;
inline constexpr std::size_t kChatHistoryChunkCapacity = 4;
inline constexpr std::size_t kMaxMapNameBytes = 32;
inline constexpr float kMaxLightningKnockback = 100000.0F;
inline constexpr float kMaxRocketKnockback = 1000.0F;
inline constexpr float kMinLightningFireHz = 1.0F;
inline constexpr float kMaxLightningFireHz = 125.0F;

enum class MatchPhase : std::uint8_t {
  WaitingForPlayers = 0,
  WaitingForReady = 1,
  Countdown = 2,
  Live = 3,
  RoundEnd = 4,
  MatchEnd = 5,
};

enum class WeaponSwitchingMode : std::uint8_t {
  Ql = 0,
  Cpma = 1,
  Crazy = 2,
};

enum class BotAttackMode : std::uint8_t {
  Off = 0,
  Easy = 1,
  Medium = 2,
  Hard = 3,
};

enum class BotCommandType : std::uint8_t {
  None = 0,
  Add = 1,
  KickSlot = 2,
  KickAll = 3,
  AttackMode = 4,
  Stare = 5,
  Standstill = 6,
  Dodge = 7,
};

struct MatchRules {
  std::uint16_t roundLimit = 10;
  std::uint16_t timeLimitMinutes = 0;
  std::uint8_t playerLimit = 2;
  std::uint16_t countdownTicks = 625;
  std::uint16_t roundEndTicks = 625;
  std::uint16_t matchEndTicks = 625;
  // Shared by modes that respawn players during live play. Elimination modes
  // still end their round on death instead of consulting this timer.
  std::uint16_t deathRespawnTicks = 250;
  bool showOpponentHealth = true;
};

enum class McGuffinEventType : std::uint8_t {
  None = 0,
  Pickup = 1,
  Drop = 2,
  Install = 3,
  Steal = 4,
  Return = 5,
  RoundWin = 6,
  Throw = 7,
};

struct McGuffinSnapshot {
  McGuffinState state = McGuffinState::NeutralSpawn;
  Team associatedTeam = Team::None;
  Team carrierTeam = Team::None;
  std::uint8_t carrierIndex = kNoMcGuffinCarrier;
  Vec3 position = {};
  Vec3 velocity = {};
  std::uint32_t stateTicks = 0;
  std::uint32_t scoreSubPoints = 0;
  std::uint32_t carrySubPoints = 0;
  std::uint16_t carriedPoints = 0;
  std::uint32_t interactionTicks = 0;
  std::uint32_t finalHoldTicks = 0;
  std::uint32_t eventSequence = 0;
  McGuffinEventType lastEvent = McGuffinEventType::None;
  std::uint8_t eventPlayerIndex = kNoMcGuffinCarrier;
};

struct ConnectRequest {
  std::uint32_t clientNonce = 0;
};

struct ConnectAccept {
  std::uint32_t clientNonce = 0;
  std::uint8_t clientIndex = 0;
  std::uint8_t playerIndex = kNoAssignedPlayer;
  std::uint32_t serverTick = 0;
};

// Edge counters are cumulative input state. Repeating them in every command
// bundle makes one-shot actions loss-tolerant without executing duplicates.
struct ActionEdgeState {
  std::uint32_t jump = 0;
  std::uint32_t dash = 0;
  std::uint32_t reset = 0;
  std::uint32_t ready = 0;
  std::uint32_t mcguffinThrow = 0;
  float mcguffinThrowYawRadians = 0.0F;
  float mcguffinThrowPitchRadians = 0.0F;
  std::uint32_t attack = 0;
  float attackYawRadians = 0.0F;
  float attackPitchRadians = 0.0F;
  std::uint32_t attackViewedServerTick = 0;
  Weapon attackWeapon = Weapon::LightningGun;
};

struct CommandPacket {
  // The body slot is server-owned. UDP clients repeat their last observed value,
  // but transport authentication uses clientIndex and the server overwrites this
  // field before authoritative command processing.
  std::uint8_t playerIndex = 0;
  UserCommand command = {};
  bool requestReset = false;
  bool toggleReady = false;
  std::uint32_t viewedServerTick = 0;
  bool requestMovementTuning = false;
  MovementTuning movementTuning = {};
  float playerSizeScaleXY = 1.0F;
  float playerSizeScaleZ = 1.0F;
  float lightningKnockback = 1000.0F;
  float lightningFireHz = 20.0F;
  float rocketKnockback = 1000.0F;
  WeaponDamageTuning weaponDamage = {
    5,
    5,
    120,
    80,
    100,
    20,
    120,
  };
  WeaponAmmoConfig weaponAmmo = {};
  float vampirism = 0.0F;
  std::string chatMessage;
  std::string playerName;
  std::string mapName;
  std::uint8_t selfDamagePercent = 100;
  std::int32_t healthAmount = 100;
  bool botDodgeEnabled = false;
  std::int32_t botDodgeMinIntervalMs = 250;
  std::int32_t botDodgeMaxIntervalMs = 750;
  bool requestGameMode = false;
  GameMode requestedGameMode = GameMode::Duel;
  bool requestTeam = false;
  Team requestedTeam = Team::None;
  WeaponSwitchingMode weaponSwitchingMode = WeaponSwitchingMode::Crazy;
  std::uint32_t clientNonce = 0;
  std::int32_t knockbackTimeMs = 100;
  BotCommandType botCommand = BotCommandType::None;
  std::int32_t botCommandValue = 0;
  std::int32_t botCommandMinIntervalMs = 250;
  std::int32_t botCommandMaxIntervalMs = 750;
  bool requestMcGuffinThrow = false;
  bool wantsScoreboardStats = false;
  std::uint32_t acknowledgedConfigurationRevision = 0;
  bool requestSpectator = false;
  ActionEdgeState actionEdges = {};
  // A connection identity is distinct from its optional player body. Keeping it
  // separate lets spectator slots use the same loss-tolerant command bundles.
  std::uint8_t clientIndex = 0;
};

struct CommandBundle {
  std::uint32_t datagramSequence = 0;
  ActionEdgeState actionEdges = {};
  std::uint8_t commandCount = 0;
  std::array<CommandPacket, kMaxBundledCommands> commands = {};
};

struct PingPacket {
  std::uint32_t token = 0;
};

struct DisconnectPacket {
  std::uint32_t clientNonce = 0;
};

struct ChatMessage {
  std::uint32_t sequence = 0;
  std::uint8_t playerIndex = 0;
  std::string speakerName;
  std::string message;
};

struct ChatHistory {
  std::uint8_t messageCount = 0;
  std::array<ChatMessage, kChatHistoryCapacity> messages = {};
};

// Chat travels outside the high-frequency gameplay snapshot. Four maximum-size
// entries keep each history datagram comfortably below common UDP MTUs.
struct ChatHistoryChunk {
  std::uint32_t oldestAvailableSequence = 0;
  std::uint32_t latestSequence = 0;
  std::uint8_t messageCount = 0;
  std::array<ChatMessage, kChatHistoryChunkCapacity> messages = {};
};

struct ChatHistoryAck {
  std::uint32_t sequence = 0;
};

struct WeaponCombatStats {
  std::uint32_t damageDealt = 0;
  std::uint16_t attempts = 0;
  std::uint16_t hits = 0;
};

struct RoundCombatStats {
  std::array<WeaponCombatStats, kWeaponCount> weapons = {};
};

struct CombatStatsPacket {
  std::uint32_t serverTick = 0;
  std::array<RoundCombatStats, kDuelPlayerCount> round = {};
  std::array<RoundCombatStats, kDuelPlayerCount> match = {};
};

struct FootstepAudioEvent {
  bool active = false;
  bool jumping = false;
  bool landing = false;
  std::uint32_t sequence = 0;
  Vec3 position = {};
};

struct GrenadeBounceAudioEvent {
  bool active = false;
  std::uint32_t sequence = 0;
  Vec3 position = {};
};

struct FragEvent {
  bool active = false;
  std::uint32_t sequence = 0;
  std::uint8_t targetPlayerIndex = 255;
  Weapon weapon = Weapon::LightningGun;
};

struct LocalHitFeedbackEvent {
  bool active = false;
  std::uint32_t sequence = 0;
  std::uint8_t targetPlayerIndex = 255;
  int damageApplied = 0;
  bool headshot = false;
  Weapon weapon = Weapon::LightningGun;
};

struct ServerSnapshot {
  std::uint32_t serverTick = 0;
  // This recipient-only state describes the connection's current body
  // assignment without making observers simulation entities or expanding
  // gameplay arrays. Other transports leave the tag false.
  bool hasLocalClientState = false;
  std::uint8_t localPlayerIndex = kNoAssignedPlayer;
  bool localSpectator = false;
  std::uint8_t spectatorCount = 0;
  std::uint32_t acknowledgedCommandDatagramSequence = 0;
  std::uint32_t commandDatagramAckBits = 0;
  std::uint32_t mapRevision = 1;
  MapDescriptor map = {};
  std::array<std::uint32_t, kDuelPlayerCount> acknowledgedCommand = {};
  std::array<bool, kDuelPlayerCount> hasAcknowledgedCommand = {};
  std::array<PlayerState, kDuelPlayerCount> players = {};
  std::array<Weapon, kDuelPlayerCount> selectedWeapons = {};
  std::array<LightningGunResult, kDuelPlayerCount> lightningGuns = {};
  std::array<WeaponFireResult, kDuelPlayerCount> weaponFires = {};
  std::array<RocketExplosionResult, kDuelPlayerCount> rocketExplosions = {};
  std::array<FootstepAudioEvent, kDuelPlayerCount> footstepAudioEvents = {};
  std::array<GrenadeBounceAudioEvent, kMaxRocketProjectiles> grenadeBounceAudioEvents = {};
  std::array<FragEvent, kDuelPlayerCount> fragEvents = {};
  std::array<
    std::array<LocalHitFeedbackEvent, kLocalHitFeedbackEventWindow>,
    kDuelPlayerCount
  > localHitFeedbackEvents = {};
  std::array<RocketProjectileSnapshot, kMaxRocketProjectiles> rockets = {};
  IcePoolArray icePools = {};
  std::array<bool, Arena::kHealthPickupCount> healthPickupAvailable = {};
  std::array<std::uint32_t, kDuelPlayerCount> respawnTicksRemaining = {};
  std::array<std::uint16_t, kDuelPlayerCount> scores = {};
  GameMode gameMode = GameMode::Duel;
  std::array<Team, kDuelPlayerCount> teams = {};
  std::array<std::uint16_t, kPlayableTeamCount> teamScores = {};
  std::array<std::uint16_t, kPlayableTeamCount> mcguffinScores = {};
  std::array<std::uint8_t, kPlayableTeamCount> mcguffinRoundsWon = {};
  std::uint8_t mcguffinRound = 0;
  Team mcguffinRedBaseOwner = Team::Red;
  Team mcguffinBlueBaseOwner = Team::Blue;
  McGuffinSnapshot mcguffin = {};
  McGuffinConfig mcguffinConfig = {};
  Team roundWinningTeam = Team::None;
  Team matchWinningTeam = Team::None;
  std::array<bool, kDuelPlayerCount> connectedPlayers = {};
  std::array<bool, kDuelPlayerCount> botPlayers = {};
  std::array<bool, kDuelPlayerCount> participatingPlayers = {};
  std::array<bool, kDuelPlayerCount> readyPlayers = {};
  bool hasCombatStats = true;
  std::uint32_t configurationRevision = 1;
  bool hasConfiguration = true;
  MatchPhase matchPhase = MatchPhase::WaitingForPlayers;
  MatchRules matchRules = {};
  MovementTuning movementTuning = {};
  float playerSizeScaleXY = 1.0F;
  float playerSizeScaleZ = 1.0F;
  float lightningKnockback = 1000.0F;
  float lightningFireHz = 20.0F;
  float rocketKnockback = 1000.0F;
  std::int32_t knockbackTimeMs = 100;
  WeaponDamageTuning weaponDamage = {};
  IcePoolTuning icePoolTuning = {};
  float vampirism = 0.0F;
  std::uint8_t selfDamagePercent = 100;
  std::int32_t healthAmount = 100;
  bool botDodgeEnabled = false;
  std::int32_t botDodgeMinIntervalMs = 250;
  std::int32_t botDodgeMaxIntervalMs = 750;
  bool botStareEnabled = true;
  bool botStandstillEnabled = false;
  BotAttackMode botAttackMode = BotAttackMode::Off;
  WeaponSwitchingMode weaponSwitchingMode = WeaponSwitchingMode::Crazy;
  WeaponAmmoConfig weaponAmmo = {};
  std::array<WeaponAmmoArray, kDuelPlayerCount> playerAmmo = {};
  std::array<RoundCombatStats, kDuelPlayerCount> roundCombatStats = {};
  std::array<RoundCombatStats, kDuelPlayerCount> matchCombatStats = {};
  std::array<std::string, kDuelPlayerCount> playerNames = {
    "PLAYER 1",
    "PLAYER 2",
    "PLAYER 3",
    "PLAYER 4",
    "PLAYER 5",
    "PLAYER 6",
  };
  std::uint32_t phaseTicksRemaining = 0;
  std::uint32_t liveTicksElapsed = 0;
  std::uint8_t roundWinner = 255;
  std::uint8_t matchWinner = 255;
  bool playersColliding = false;
};

// Snapshots are decoded, queued, copied, assigned, and interpolated constantly.
// Keep static map geometry owned by map/server/client state, not snapshots.
// MSVC's STL/layout is bulkier than Clang's here, so this budget is intentionally
// about the native in-memory snapshot, not the encoded packet size.
static_assert(
  sizeof(ServerSnapshot) < 8192,
  "ServerSnapshot must remain compact; static Arena data belongs outside snapshots."
);

} // namespace lg
