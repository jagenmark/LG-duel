#pragma once

#include "net/NetProtocol.hpp"
#include "net/NetTransport.hpp"
#include "replay/ReplayRecorder.hpp"
#include "replay/ReplayRollingBuffer.hpp"
#include "scenario/ScenarioState.hpp"
#include "sim/Arena.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/Combat.hpp"
#include "sim/MapRegistry.hpp"
#include "sim/Movement.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

namespace lg {

struct BotRosterChange {
  bool ok = false;
  std::size_t changed = 0;
  std::string message;
};

class ServerGame {
public:
  explicit ServerGame(NetTransport& transport, std::string balanceConfigPath = {});

  void tick(float fixedDt);
  void resetMatch();
  void applyBalanceConfig(const BalanceConfig& config);
  void setArena(const Arena& arena);
  void setMapDirectory(std::string mapDirectory);
  [[nodiscard]] bool loadRequestedMap(const std::string& mapName);
  void setConnectedPlayers(
    const std::array<bool, kDuelPlayerCount>& connectedPlayers
  );
  void setConnectedPlayers(
    const std::array<bool, kDuelPlayerCount>& connectedPlayers,
    const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
  );
  void setMatchRules(const MatchRules& rules);
  void setMcGuffinConfig(const McGuffinConfig& config);
  void setRuntimeGameplayTuning(
    const MovementTuning& movementTuning,
    float playerSizeScaleXY,
    float playerSizeScaleZ,
    float lightningKnockback,
    float lightningFireHz,
    float rocketKnockback,
    std::int32_t knockbackTimeMs,
    const WeaponDamageTuning& weaponDamage,
    float vampirism,
    std::uint8_t selfDamagePercent,
    std::int32_t healthAmount,
    bool infiniteAmmo,
    bool botDodgeEnabled,
    int botDodgeMinIntervalMs,
    int botDodgeMaxIntervalMs,
    WeaponSwitchingMode weaponSwitchingMode
  );
  void setWeaponSwitchingMode(WeaponSwitchingMode mode);
  void setBotDodge(bool enabled, int minIntervalMs, int maxIntervalMs);
  void setBotBehavior(
    bool stareEnabled,
    bool standstillEnabled,
    bool dodgeEnabled,
    int dodgeMinIntervalMs,
    int dodgeMaxIntervalMs,
    BotAttackMode attackMode
  );
  void setBotAttackMode(BotAttackMode mode);
  void setBotWeapon(Weapon weapon);
  [[nodiscard]] BotRosterChange addBots(std::optional<std::size_t> count = std::nullopt);
  [[nodiscard]] BotRosterChange kickAllBots();
  [[nodiscard]] BotRosterChange kickBotAtPlayerIndex(std::size_t playerIndex);
  [[nodiscard]] WeaponSwitchingMode weaponSwitchingMode() const;
  [[nodiscard]] bool botStareEnabled() const;
  [[nodiscard]] bool botStandstillEnabled() const;
  [[nodiscard]] bool botDodgeEnabled() const;
  [[nodiscard]] int botDodgeMinIntervalMs() const;
  [[nodiscard]] int botDodgeMaxIntervalMs() const;
  [[nodiscard]] BotAttackMode botAttackMode() const;
  [[nodiscard]] Weapon botWeapon() const;
  [[nodiscard]] bool isBotSlot(std::size_t playerIndex) const;
  [[nodiscard]] bool isHumanPlayer(std::size_t playerIndex) const;
  [[nodiscard]] bool isOccupiedSlot(std::size_t playerIndex) const;
  [[nodiscard]] std::array<bool, kDuelPlayerCount> occupiedPlayers() const;
  [[nodiscard]] bool applyScenarioSetup(
    const ScenarioSetup& setup,
    std::string* error = nullptr
  );
  [[nodiscard]] ScenarioState captureScenarioState() const;

  [[nodiscard]] const ServerSnapshot& snapshot() const;
  [[nodiscard]] const std::array<RocketProjectile, kMaxRocketProjectiles>&
    projectiles() const;
  [[nodiscard]] const Arena& arena() const;
  [[nodiscard]] const std::string& mapDirectory() const;
  [[nodiscard]] const std::string& spawnDebugString() const;
  [[nodiscard]] const MatchRules& matchRules() const;

  [[nodiscard]] bool beginReplayRecording(
    replay::ReplayRecordingConfig config = {},
    std::string* error = nullptr
  );
  [[nodiscard]] std::optional<replay::ReplayDemo> finishReplayRecording();
  [[nodiscard]] bool replayRecordingActive() const;
  [[nodiscard]] replay::ReplayRecorderStats replayRecorderStats() const;
  [[nodiscard]] bool beginRollingReplay(
    replay::ReplayRollingBufferConfig config = {},
    std::string* error = nullptr
  );
  void endRollingReplay();
  [[nodiscard]] replay::ReplayRollingBufferStats rollingReplayStats() const;
  [[nodiscard]] std::optional<replay::ReplayDemo> extractRollingReplaySegment(
    const replay::ReplayLethalEvent& event,
    std::uint32_t beforeTicks,
    std::uint32_t afterTicks,
    std::string* error = nullptr
  ) const;
  [[nodiscard]] std::optional<replay::ReplayLethalEvent> latestReplayLethal() const;
  [[nodiscard]] replay::ReplayCheckpoint captureReplayCheckpoint() const;
  [[nodiscard]] bool restoreReplayCheckpoint(
    const replay::ReplayCheckpoint& checkpoint,
    const replay::ReplayMetadata& metadata,
    std::string* error = nullptr
  );
  [[nodiscard]] bool injectReplayInput(
    const replay::ReplayTickInput& input,
    std::string* error = nullptr
  );
  void endReplayPlayback();

private:
  struct HistoryFrame {
    std::uint32_t serverTick = 0;
    std::array<PlayerState, kDuelPlayerCount> players = {};
  };

  struct FootstepState {
    Vec3 previousPosition = {};
    float distanceSinceStep = 0.0F;
    bool wasOnGround = false;
    bool initialized = false;
  };

  struct BotCombatState {
    float reactionSecondsRemaining = 0.0F;
    std::size_t targetPlayerIndex = kDuelPlayerCount;
    float desiredYawRadians = 0.0F;
    float desiredPitchRadians = 0.0F;
    float aimErrorYawRadians = 0.0F;
    float aimErrorPitchRadians = 0.0F;
    float nextAimErrorRefreshSeconds = 0.0F;
    bool initialized = false;
  };

  struct RecentProjectileRemoval {
    ProjectileUpdate update = {};
    std::uint32_t serverTick = 0;
    bool sentOnce = false;
    bool replayedOnce = false;
  };

  void receiveCommands();
  void setArena(const Arena& arena, MapDescriptor descriptor);
  void resetPlayerInputState(std::size_t playerIndex);
  void respawnPlayer(std::size_t playerIndex);
  [[nodiscard]] ArenaSpawnGroup spawnGroupForTeam(Team team) const;
  [[nodiscard]] std::optional<std::size_t> selectTeamSpawn(
    std::size_t playerIndex,
    const PlayerState& freshPlayer
  );
  [[nodiscard]] std::uint32_t nextSpawnRandomU32();
  void respawnRound();
  void updateMatchState();
  void beginCountdown();
  void beginRoundEnd(std::size_t winnerIndex);
  void beginRoundEnd(Team winnerTeam);
  void beginMatchEnd(std::size_t winnerIndex);
  void beginMatchEnd(Team winnerTeam);
  [[nodiscard]] bool enoughPlayersConnected() const;
  [[nodiscard]] bool allConnectedPlayersReady() const;
  [[nodiscard]] bool warmupPhase() const;
  [[nodiscard]] bool isActiveCombatant(std::size_t playerIndex) const;
  [[nodiscard]] bool isValidEnemyTarget(
    std::size_t attackerIndex,
    std::size_t targetIndex
  ) const;
  [[nodiscard]] std::size_t nearestValidEnemy(
    std::size_t attackerIndex,
    bool requireLineOfSight
  ) const;
  [[nodiscard]] bool hasLineOfSight(
    std::size_t attackerIndex,
    std::size_t targetIndex
  ) const;
  [[nodiscard]] bool damageAllowed(
    std::size_t attackerIndex,
    std::size_t targetIndex
  ) const;
  [[nodiscard]] std::uint32_t weaponCooldownTicks(
    std::size_t playerIndex,
    Weapon weapon
  ) const;
  [[nodiscard]] bool canSwitchWeapon(std::size_t playerIndex) const;
  [[nodiscard]] bool canFireSelectedWeapon(std::size_t playerIndex) const;
  [[nodiscard]] bool hasAmmoForWeapon(std::size_t playerIndex, Weapon weapon) const;
  void refillAmmo(std::size_t playerIndex);
  bool consumeAmmo(std::size_t playerIndex, Weapon weapon);
  void consumeLightningGunAmmo(std::size_t playerIndex, float fixedDt);
  void consumeFreezeGunAmmo(std::size_t playerIndex, float fixedDt);
  void decayIcePools(float fixedDt);
  void growIcePool(Vec3 center, Vec3 normal, float fixedDt);
  void updateSelectedWeapon(std::size_t playerIndex, Weapon requestedWeapon);
  void recordHistory();
  [[nodiscard]] const HistoryFrame& historyFrameForTick(std::uint32_t serverTick) const;
  void simulateRockets(float fixedDt);
  bool spawnProjectile(
    std::size_t attackerIndex,
    const PlayerState& attacker,
    const UserCommand& command,
    Weapon weapon
  );
  void updateFootstepAudioEvents();
  void resetHealthPickups();
  void updateHealthPickups();
  void resetMcGuffinRound();
  void updateMcGuffin();
  void dropMcGuffinCarrier(std::size_t playerIndex);
  void recordMcGuffinEvent(McGuffinEventType event, std::size_t playerIndex);
  void beginMcGuffinRoundEnd(Team winnerTeam);
  void restoreTransientCombatEvents();
  void rememberTransientCombatEvents();
  void updateParticipatingPlayers();
  void updateBotCommands(float fixedDt);
  [[nodiscard]] replay::ReplayTickInput captureResolvedReplayInput() const;
  [[nodiscard]] replay::ReplayMetadata replayMetadata() const;
  [[nodiscard]] std::uint64_t replayGameplayConfigHash() const;
  void resetRollingReplay();
  void recordReplayLethal(std::size_t attackerIndex, std::size_t targetIndex, Weapon weapon);
  void applyReplayInput(const replay::ReplayTickInput& input);
  void handleBotCommandRequest(const CommandPacket& packet);
  void updateClanArenaBotTeams();
  void refreshWarmupRosterState();
  void addBotAtPlayerIndex(std::size_t playerIndex);
  void removeBotAtPlayerIndex(std::size_t playerIndex);
  [[nodiscard]] std::uint32_t randomU32();
  [[nodiscard]] float randomFloat(float minValue, float maxValue);
  void applyDamageAndKnockback(
    std::size_t attackerIndex,
    std::size_t targetIndex,
    int damageApplied,
    Vec3 knockbackImpulse,
    Weapon weapon,
    bool headshot
  );
  void clearProjectiles();
  void publishSnapshot();
  void publishProjectileUpdates();

  NetTransport& transport_;
  Arena arena_ = {};
  MapDescriptor mapDescriptor_ = {};
  std::string mapDirectory_ = "maps";
  std::uint32_t mapRevision_ = 1;
  std::uint64_t emergencyPlayerCollisionRepairCount_ = 0;
  std::uint64_t unresolvedPlayerCollisionInvariantCount_ = 0;
  MovementTuning movementTuning_ = {};
  float playerSizeScaleXY_ = 1.0F;
  float playerSizeScaleZ_ = 1.0F;
  float lightningKnockback_ = 1000.0F;
  float lightningFireHz_ = 20.0F;
  float rocketKnockback_ = 1000.0F;
  std::int32_t knockbackTimeMs_ = 100;
  WeaponDamageTuning weaponDamage_ = {};
  LightningGunTuning lightningGunTuning_ = {};
  FreezeGunTuning freezeGunTuning_ = {};
  IcePoolTuning icePoolTuning_ = {};
  HitscanTuning railgunTuning_ = {};
  HitscanTuning revolverTuning_ = {};
  float sniperChargeSeconds_ = 3.3F;
  float sniperMaxDamageMultiplier_ = 3.0F;
  MachineGunTuning machineGunTuning_ = {};
  ShotgunTuning shotgunTuning_ = {};
  RocketLauncherTuning rocketLauncherTuning_ = {};
  GrenadeLauncherTuning grenadeLauncherTuning_ = {};
  PlasmaGunTuning plasmaGunTuning_ = {};
  float vampirism_ = 0.0F;
  std::uint8_t selfDamagePercent_ = 100;
  std::int32_t healthAmount_ = 100;
  WeaponAmmoConfig weaponAmmoConfig_ = {};
  std::array<WeaponAmmoArray, kDuelPlayerCount> playerAmmo_ = {};
  std::array<double, kDuelPlayerCount> lightningAmmoCredit_ = {};
  std::array<double, kDuelPlayerCount> freezeAmmoCredit_ = {};
  std::array<double, kDuelPlayerCount> fractionalVampirismHealing_ = {};
  std::uint32_t railgunCooldownDurationTicks_ = 188;
  std::uint32_t revolverCooldownDurationTicks_ = 188;
  std::uint32_t machineGunCooldownDurationTicks_ = 13;
  std::uint32_t shotgunCooldownDurationTicks_ = 125;
  std::uint32_t rocketLauncherCooldownDurationTicks_ = 100;
  std::uint32_t weaponPulloutDurationTicks_ = 20;
  std::uint32_t jumpPadRetriggerCooldownTicks_ = kDefaultJumpPadCooldownTicks;
  std::int32_t smallHealthPickupAmount_ = 25;
  std::int32_t largeHealthPickupAmount_ = 50;
  std::uint32_t smallHealthPickupCooldownTicks_ = 1250;
  std::uint32_t largeHealthPickupCooldownTicks_ = 4375;
  std::array<std::uint32_t, Arena::kHealthPickupCount> healthPickupCooldownTicks_ = {};
  std::array<LightningGunState, kDuelPlayerCount> lightningGunStates_ = {};
  std::array<LightningGunState, kDuelPlayerCount> freezeGunStates_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> railgunCooldownTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> revolverCooldownTicks_ = {};
  std::array<float, kDuelPlayerCount> sniperAdsFractions_ = {};
  std::array<float, kDuelPlayerCount> sniperChargeFractions_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> machineGunCooldownTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> shotgunCooldownTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> rocketCooldownTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> grenadeCooldownTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> plasmaGunCooldownTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> rocketExplosionSequences_ = {};
  std::array<Weapon, kDuelPlayerCount> selectedWeapons_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> weaponPulloutTicks_ = {};
  std::array<WeaponFireResult, kDuelPlayerCount> recentWeaponFires_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> recentWeaponFireTicks_ = {};
  std::array<RocketExplosionResult, kDuelPlayerCount> recentRocketExplosions_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> recentRocketExplosionTicks_ = {};
  std::array<FootstepAudioEvent, kDuelPlayerCount> recentFootstepAudioEvents_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> recentFootstepAudioEventTicks_ = {};
  std::array<GrenadeBounceAudioEvent, kDuelPlayerCount> recentGrenadeBounceAudioEvents_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> recentGrenadeBounceAudioEventTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> fragEventSequences_ = {};
  std::array<FragEvent, kDuelPlayerCount> recentFragEvents_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> recentFragEventTicks_ = {};
  std::array<
    std::array<LocalHitFeedbackEvent, kLocalHitFeedbackEventWindow>,
    kDuelPlayerCount
  > recentLocalHitFeedbackEvents_ = {};
  std::array<
    std::array<std::uint32_t, kLocalHitFeedbackEventWindow>,
    kDuelPlayerCount
  > recentLocalHitFeedbackEventTicks_ = {};
  std::array<FootstepState, kDuelPlayerCount> footstepStates_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> localHitFeedbackSequences_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> footstepSequences_ = {};
  std::array<RocketProjectile, kMaxRocketProjectiles> rockets_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> projectileSequences_ = {};
  std::array<std::uint32_t, kMaxRocketProjectiles> grenadeBounceSequences_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> grenadeBounceEventSequences_ = {};
  std::array<ProjectileUpdate, kDuelPlayerCount> spawnedProjectileUpdates_ = {};
  std::size_t spawnedProjectileCount_ = 0;
  std::deque<RecentProjectileRemoval> recentProjectileRemovals_ = {};
  std::size_t projectileCorrectionCursor_ = 0;
  std::uint32_t projectileRevision_ = 1;
  std::array<UserCommand, kDuelPlayerCount> commands_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> viewedServerTicks_ = {};
  std::array<bool, kDuelPlayerCount> hasCommand_ = {};
  std::array<bool, kDuelPlayerCount> receivedCommandThisTick_ = {};
  std::array<ActionEdgeState, kDuelPlayerCount> lastActionEdges_ = {};
  std::array<bool, kDuelPlayerCount> jumpEdgeThisTick_ = {};
  std::array<bool, kDuelPlayerCount> dashEdgeThisTick_ = {};
  std::array<bool, kDuelPlayerCount> attackEdgeThisTick_ = {};
  std::array<UserCommand, kDuelPlayerCount> attackEdgeCommands_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> attackEdgeViewedServerTicks_ = {};
  std::array<bool, kDuelPlayerCount> mcguffinThrowRequestedThisTick_ = {};
  std::array<UserCommand, kDuelPlayerCount> mcguffinThrowCommands_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> playerSessions_ = {};
  std::array<bool, kDuelPlayerCount> botPlayers_ = {};
  bool botStareEnabled_ = true;
  bool botStandstillEnabled_ = false;
  bool botDodgeEnabled_ = false;
  int botDodgeMinIntervalMs_ = 250;
  int botDodgeMaxIntervalMs_ = 750;
  std::array<int, kDuelPlayerCount> botDodgeDirections_ = {};
  std::array<float, kDuelPlayerCount> botDodgeSwitchSeconds_ = {};
  BotAttackMode botAttackMode_ = BotAttackMode::Off;
  Weapon botWeapon_ = Weapon::MachineGun;
  std::array<BotCombatState, kDuelPlayerCount> botCombatStates_ = {};
  std::uint32_t botRandomState_ = 0xB07D0D6EU;
  std::deque<HistoryFrame> history_ = {};
  MatchRules matchRules_ = {};
  McGuffinConfig mcguffinConfig_ = {};
  McGuffinObjective mcguffinObjective_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> mcguffinStealTicks_ = {};
  std::uint32_t mcguffinCarrySubPoints_ = 0;
  std::uint16_t mcguffinCarriedPoints_ = 0;
  std::uint32_t mcguffinFinalHoldTicks_ = 0;
  std::uint32_t mcguffinRoundLiveTicks_ = 0;
  std::uint32_t mcguffinThrowPickupLockoutTicks_ = 0;
  std::array<std::uint32_t, Arena::kTeamSpawnCount> spawnLastUsedTicks_ = {};
  std::array<bool, Arena::kTeamSpawnCount> spawnWasUsed_ = {};
  std::uint32_t spawnRandomState_ = 0x51A7E123U;
  std::size_t nextDeathmatchSpawnIndex_ = 0;
  std::string spawnDebugString_ = "no team spawn selected yet";
  WeaponSwitchingMode weaponSwitchingMode_ = WeaponSwitchingMode::Crazy;
  ChatHistory chatHistory_ = {};
  std::array<std::uint32_t, kMaxNetworkClients> chatClientNonces_ = {};
  std::array<std::uint32_t, kMaxNetworkClients> acknowledgedChatCommands_ = {};
  std::array<bool, kMaxNetworkClients> hasAcknowledgedChatCommand_ = {};
  ServerSnapshot snapshot_ = {};
  std::unique_ptr<replay::ReplayRecorder> replayRecorder_ = {};
  std::unique_ptr<replay::ReplayRollingBuffer> rollingReplay_ = {};
  std::optional<replay::ReplayLethalEvent> latestReplayLethal_ = {};
  std::uint32_t replayGeneration_ = 1;
  std::optional<replay::ReplayTickInput> pendingReplayInput_ = {};
  bool replayPlayback_ = false;
};

} // namespace lg
