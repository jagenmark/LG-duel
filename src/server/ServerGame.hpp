#pragma once

#include "net/NetProtocol.hpp"
#include "net/NetTransport.hpp"
#include "sim/Arena.hpp"
#include "sim/Combat.hpp"
#include "sim/Movement.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <string>

namespace lg {

class ServerGame {
public:
  explicit ServerGame(NetTransport& transport);

  void tick(float fixedDt);
  void resetMatch();
  void setArena(const Arena& arena);
  void setMapDirectory(std::string mapDirectory);
  void setConnectedPlayers(
    const std::array<bool, kDuelPlayerCount>& connectedPlayers
  );
  void setConnectedPlayers(
    const std::array<bool, kDuelPlayerCount>& connectedPlayers,
    const std::array<std::uint32_t, kDuelPlayerCount>& playerSessions
  );
  void setMatchRules(const MatchRules& rules);
  void setBotDodge(bool enabled, int minIntervalMs, int maxIntervalMs);
  [[nodiscard]] bool botDodgeEnabled() const;
  [[nodiscard]] int botDodgeMinIntervalMs() const;
  [[nodiscard]] int botDodgeMaxIntervalMs() const;

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

  void receiveCommands();
  [[nodiscard]] bool loadRequestedMap(const std::string& mapName);
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
  [[nodiscard]] bool damageAllowed(
    std::size_t attackerIndex,
    std::size_t targetIndex
  ) const;
  void recordHistory();
  [[nodiscard]] const HistoryFrame& historyFrameForTick(std::uint32_t serverTick) const;
  void simulateRockets(float fixedDt);
  void updateFootstepAudioEvents();
  void restoreTransientCombatEvents();
  void rememberTransientCombatEvents();
  void updateParticipatingPlayers();
  void updateBotCommands(float fixedDt);
  [[nodiscard]] std::uint32_t randomU32();
  void applyDamageAndKnockback(
    std::size_t attackerIndex,
    std::size_t targetIndex,
    int damageApplied,
    Vec3 knockbackImpulse
  );
  void publishSnapshot();

  NetTransport& transport_;
  Arena arena_ = thunderstruckArena();
  std::string mapDirectory_ = "maps";
  std::uint32_t mapRevision_ = 1;
  MovementTuning movementTuning_ = {};
  float playerSizeScaleXY_ = 1.0F;
  float playerSizeScaleZ_ = 1.0F;
  float lightningKnockback_ = 1000.0F;
  float rocketKnockback_ = 1000.0F;
  WeaponDamageTuning weaponDamage_ = {};
  LightningGunTuning lightningGunTuning_ = {};
  HitscanTuning railgunTuning_ = {};
  MachineGunTuning machineGunTuning_ = {};
  ShotgunTuning shotgunTuning_ = {};
  RocketLauncherTuning rocketLauncherTuning_ = {};
  float vampirism_ = 0.0F;
  std::uint8_t selfDamagePercent_ = 100;
  std::int32_t healthAmount_ = 100;
  std::array<double, kDuelPlayerCount> fractionalVampirismHealing_ = {};
  std::array<LightningGunState, kDuelPlayerCount> lightningGunStates_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> railgunCooldownTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> machineGunCooldownTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> shotgunCooldownTicks_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> rocketCooldownTicks_ = {};
  std::array<WeaponFireResult, kDuelPlayerCount> recentWeaponFires_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> recentWeaponFireTicks_ = {};
  std::array<RocketExplosionResult, kDuelPlayerCount> recentRocketExplosions_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> recentRocketExplosionTicks_ = {};
  std::array<FootstepAudioEvent, kDuelPlayerCount> recentFootstepAudioEvents_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> recentFootstepAudioEventTicks_ = {};
  std::array<FootstepState, kDuelPlayerCount> footstepStates_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> footstepSequences_ = {};
  std::array<RocketProjectile, kMaxRocketProjectiles> rockets_ = {};
  std::array<UserCommand, kDuelPlayerCount> commands_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> viewedServerTicks_ = {};
  std::array<bool, kDuelPlayerCount> hasCommand_ = {};
  std::array<bool, kDuelPlayerCount> receivedCommandThisTick_ = {};
  std::array<std::uint32_t, kDuelPlayerCount> playerSessions_ = {};
  bool botDodgeEnabled_ = false;
  int botDodgeMinIntervalMs_ = 250;
  int botDodgeMaxIntervalMs_ = 750;
  std::array<int, kDuelPlayerCount> botDodgeDirections_ = {};
  std::array<float, kDuelPlayerCount> botDodgeSwitchSeconds_ = {};
  std::uint32_t botRandomState_ = 0xB07D0D6EU;
  std::deque<HistoryFrame> history_ = {};
  MatchRules matchRules_ = {};
  ServerSnapshot snapshot_ = {};
};

} // namespace lg
