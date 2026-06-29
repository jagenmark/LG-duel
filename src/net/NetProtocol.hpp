#pragma once

#include "shared/Constants.hpp"
#include "sim/Combat.hpp"
#include "sim/Arena.hpp"
#include "sim/GameMode.hpp"
#include "sim/Movement.hpp"
#include "sim/PlayerState.hpp"
#include "sim/UserCommand.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace lg {

inline constexpr std::size_t kDuelPlayerCount = kMaxPlayers;
inline constexpr std::size_t kMaxBundledCommands = 3;
inline constexpr std::size_t kLocalHitFeedbackEventWindow = 4;
inline constexpr std::size_t kMaxChatMessageBytes = 240;
inline constexpr std::size_t kMaxPlayerNameBytes = 20;
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

struct MatchRules {
  std::uint16_t roundLimit = 10;
  std::uint16_t timeLimitMinutes = 0;
  std::uint8_t playerLimit = 2;
  std::uint16_t countdownTicks = 625;
  std::uint16_t roundEndTicks = 625;
  std::uint16_t matchEndTicks = 625;
  bool showOpponentHealth = true;
};

struct ConnectRequest {
  std::uint32_t clientNonce = 0;
};

struct ConnectAccept {
  std::uint32_t clientNonce = 0;
  std::uint8_t playerIndex = 0;
  std::uint32_t serverTick = 0;
};

struct CommandPacket {
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
  WeaponDamageTuning weaponDamage = {};
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
  std::uint32_t clientNonce = 0;
};

struct CommandBundle {
  std::uint8_t commandCount = 0;
  std::array<CommandPacket, kMaxBundledCommands> commands = {};
};

struct PingPacket {
  std::uint32_t token = 0;
};

struct DisconnectPacket {
  std::uint32_t clientNonce = 0;
};

struct RoundCombatStats {
  std::uint32_t lightningActiveTicks = 0;
  std::uint32_t lightningHitTicks = 0;
  std::uint32_t damageDealt = 0;
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
  std::uint8_t targetPlayerIndex = 255;
};

struct LocalHitFeedbackEvent {
  bool active = false;
  std::uint32_t sequence = 0;
  std::uint8_t targetPlayerIndex = 255;
  int damageApplied = 0;
  Weapon weapon = Weapon::LightningGun;
};

struct ServerSnapshot {
  std::uint32_t serverTick = 0;
  std::uint32_t mapRevision = 1;
  Arena arena = thunderstruckArena();
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
  std::array<std::uint32_t, kDuelPlayerCount> respawnTicksRemaining = {};
  std::array<std::uint16_t, kDuelPlayerCount> scores = {};
  GameMode gameMode = GameMode::Duel;
  std::array<Team, kDuelPlayerCount> teams = {};
  std::array<std::uint16_t, kPlayableTeamCount> teamScores = {};
  Team roundWinningTeam = Team::None;
  Team matchWinningTeam = Team::None;
  std::array<bool, kDuelPlayerCount> connectedPlayers = {};
  std::array<bool, kDuelPlayerCount> participatingPlayers = {};
  std::array<bool, kDuelPlayerCount> readyPlayers = {};
  MatchPhase matchPhase = MatchPhase::WaitingForPlayers;
  MatchRules matchRules = {};
  MovementTuning movementTuning = {};
  float playerSizeScaleXY = 1.0F;
  float playerSizeScaleZ = 1.0F;
  float lightningKnockback = 1000.0F;
  float lightningFireHz = 20.0F;
  float rocketKnockback = 1000.0F;
  WeaponDamageTuning weaponDamage = {};
  float vampirism = 0.0F;
  std::uint8_t selfDamagePercent = 100;
  std::int32_t healthAmount = 100;
  bool botDodgeEnabled = false;
  std::int32_t botDodgeMinIntervalMs = 250;
  std::int32_t botDodgeMaxIntervalMs = 750;
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
  std::uint32_t chatSequence = 0;
  std::uint8_t chatPlayerIndex = 0;
  std::string chatMessage;
};

} // namespace lg
