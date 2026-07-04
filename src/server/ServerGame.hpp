#pragma once

#include "net/NetProtocol.hpp"
#include "net/NetTransport.hpp"
#include "sim/Arena.hpp"
#include "sim/BalanceConfig.hpp"
#include "sim/Combat.hpp"
#include "sim/MapRegistry.hpp"
#include "sim/Movement.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace lg {

enum class BotAttackMode {
  Off,
  Easy,
  Medium,
  Hard,
};

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
  [[nodiscard]] bool isBotSlot(std::size_t playerIndex) const;
  [[nodiscard]] bool isHumanPlayer(std::size_t playerIndex) const;
  [[nodiscard]] bool isOccupiedSlot(std::size_t playerIndex) const;
  [[nodiscard]] std::array<bool, kDuelPlayerCount> occupiedPlayers() const;

  [[nodiscard]] const ServerSnapshot& snapshot() const;
  [[nodiscard]] const Arena& arena() const;
  [[nodiscard]] const std::string& mapDirectory() const;
  [[nodiscard]] const MatchRules& matchRules() const;

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

  void receiveCommands();
  void setArena(const Arena& arena, MapDescriptor descriptor);
  void resetPlayerInputState(std::size_t playerIndex);
  void respawnPlayer(std::size_t playerIndex);
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
  void restoreTransientCombatEvents();
  void rememberTransientCombatEvents();
  void updateParticipatingPlayers();
  void updateBotCommands(float fixedDt);
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
    Weapon weapon
  );
  void publishSnapshot();

  NetTransport& transport_;
  Arena arena_ = thunderstruckArena();
  MapDescriptor mapDescriptor_ = {};
  std::string mapDirectory_ = "maps";
  std::uint32_t mapRevision_ = 1;
  MovementTuning movementTuning_ = {};
  float playerSizeScaleXY_ = 1.0F;
  float playerSizeScaleZ_ = 1.0F;
  float lightningKnockback_ = 1000.0F;
  float lightningFireHz_ = 20.0F;
  float rocketKnockback_ = 1000.0F;
  std::int32_t knockbackTimeMs_ = 100;
  WeaponDamageTuning weaponDamage_ = {};
  LightningGunTuning lightningGunTuning_ = {};
  HitscanTuning railgunTuning_ = {};
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
  std::array<double, kDuelPlayerCount> fractionalVampirismHealing_ = {};
  std::uint32_t railgunCooldownDurationTicks_ = 188;
  std::uint32_t machineGunCooldownDurationTicks_ = 13;
  std::uint32_t shotgunCooldownDurationTicks_ = 125;
  std::uint32_t rocketLauncherCooldownDurationTicks_ = 100;
  std::uint32_t weaponPulloutDurationTicks_ = 20;
  std::uint32_t jumpPadRetriggerCooldownTicks_ = kDefaultJumpPadCooldownTicks;
  std::array<LightningGunState, kDuelPlayerCount> lightningGunStates_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> railgunCooldownTicks_ = {};
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
  std::array<GrenadeBounceAudioEvent, kMaxRocketProjectiles> recentGrenadeBounceAudioEvents_ = {};
  std::array<std::uint32_t, kMaxRocketProjectiles> recentGrenadeBounceAudioEventTicks_ = {};
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
  std::array<std::uint32_t, kMaxRocketProjectiles> grenadeBounceSequences_ = {};
  std::array<UserCommand, kDuelPlayerCount> commands_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> viewedServerTicks_ = {};
  std::array<bool, kDuelPlayerCount> hasCommand_ = {};
  std::array<bool, kDuelPlayerCount> receivedCommandThisTick_ = {};
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
  std::array<BotCombatState, kDuelPlayerCount> botCombatStates_ = {};
  std::uint32_t botRandomState_ = 0xB07D0D6EU;
  std::deque<HistoryFrame> history_ = {};
  MatchRules matchRules_ = {};
  WeaponSwitchingMode weaponSwitchingMode_ = WeaponSwitchingMode::Crazy;
  ServerSnapshot snapshot_ = {};
};

} // namespace lg
