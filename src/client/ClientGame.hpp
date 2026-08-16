#pragma once

#include "client/Interpolation.hpp"
#include "client/Prediction.hpp"
#include "net/NetProtocol.hpp"
#include "net/NetTransport.hpp"
#include "sim/Arena.hpp"
#include "sim/Movement.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace lg {

class ClientGame {
public:
  ClientGame(
    NetTransport& transport,
    std::size_t localPlayerIndex,
    std::size_t commandClientIndex = kNoAssignedPlayer
  );

  void sendCommand(
    const UserCommand& command,
    bool requestReset,
    bool toggleReady = false,
    bool requestMovementTuning = false,
    const MovementTuning& movementTuning = {},
    float playerSizeScaleXY = 1.0F,
    float playerSizeScaleZ = 1.0F,
    float lightningKnockback = 1000.0F,
    float rocketKnockback = 1000.0F,
    std::int32_t knockbackTimeMs = 100,
    float vampirism = 0.0F,
    std::uint8_t selfDamagePercent = 100,
    std::int32_t healthAmount = 100,
    const WeaponDamageTuning& weaponDamage = {},
    const WeaponAmmoConfig& weaponAmmo = {},
    float lightningFireHz = 20.0F,
    bool botDodgeEnabled = false,
    std::int32_t botDodgeMinIntervalMs = 250,
    std::int32_t botDodgeMaxIntervalMs = 750,
    std::string chatMessage = {},
    std::string playerName = {},
    std::string mapName = {},
    bool usePresentedServerTick = true,
    bool requestGameMode = false,
    GameMode requestedGameMode = GameMode::Duel,
    bool requestTeam = false,
    Team requestedTeam = Team::None,
    WeaponSwitchingMode weaponSwitchingMode = WeaponSwitchingMode::Crazy,
    BotCommandType botCommand = BotCommandType::None,
    std::int32_t botCommandValue = 0,
    std::int32_t botCommandMinIntervalMs = 250,
    std::int32_t botCommandMaxIntervalMs = 750,
    bool requestMcGuffinThrow = false,
    bool wantsScoreboardStats = false,
    bool requestSpectator = false
  );
  // Send a monotonic neutral packet without changing action-edge or
  // prediction state. Replay presentation uses this to keep the live session
  // authenticated while fixed-tick input is paused.
  void sendKeepalive(
    std::uint32_t sequence,
    bool usePresentedServerTick = true
  );
  void receiveSnapshots();
  void advanceInterpolation(
    float elapsedSeconds,
    float interpolationDelaySeconds,
    bool adaptive = false,
    float minimumDelaySeconds = 0.016F,
    float maximumDelaySeconds = 0.064F,
    float maximumExtrapolationSeconds = 0.016F
  );

  [[nodiscard]] bool hasSnapshot() const;
  [[nodiscard]] const ServerSnapshot& snapshot() const;
  [[nodiscard]] bool hasAcknowledgedCommand() const;
  [[nodiscard]] bool hasPendingMovementTuning() const;
  [[nodiscard]] std::uint32_t lastAcknowledgedCommand() const;
  [[nodiscard]] const PlayerState& predictedPlayer() const;
  [[nodiscard]] PlayerState interpolatedPlayer(std::size_t playerIndex) const;
  [[nodiscard]] PlayerState interpolatedPlayer(std::size_t playerIndex, float alpha) const;
  [[nodiscard]] const PredictionDiagnostics& predictionDiagnostics() const;
  [[nodiscard]] const MovementTuning& movementTuning() const;
  [[nodiscard]] const Arena& arena() const;
  [[nodiscard]] SnapshotDiagnostics snapshotDiagnostics() const;
  [[nodiscard]] SnapshotInterpolation::Diagnostics interpolationDiagnostics() const;
  [[nodiscard]] SnapshotInterpolation::PlayerCollisionSample
    interpolationCollisionSample(std::size_t playerIndex) const;
  [[nodiscard]] bool hasConnectionError() const;
  [[nodiscard]] const std::string& connectionError() const;
  [[nodiscard]] const std::deque<ChatMessage>& chatHistory() const;
  [[nodiscard]] const std::array<
    RocketProjectileSnapshot,
    kMaxRocketProjectiles
  >& projectiles() const;
  [[nodiscard]] std::size_t localPlayerIndex() const;
  [[nodiscard]] std::size_t localClientIndex() const;
  [[nodiscard]] bool spectator() const;

private:
  void clearProjectiles();
  void receiveProjectileUpdates();
  void advanceProjectiles(float elapsedSeconds);
  void removeExplodedProjectile(
    std::size_t owner,
    const RocketExplosionResult& explosion
  );

  NetTransport& transport_;
  std::size_t localPlayerIndex_ = 0;
  std::size_t commandClientIndex_ = 0;
  bool spectator_ = false;
  Arena arena_ = {};
  std::uint32_t mapRevision_ = 1;
  MovementTuning movementTuning_ = {};
  IcePoolTuning icePoolTuning_ = {};
  Prediction prediction_ = {};
  SnapshotInterpolation interpolation_ = {};
  ServerSnapshot snapshot_ = {};
  MapDescriptor map_ = {};
  SnapshotDiagnostics snapshotDiagnostics_ = {};
  ActionEdgeState actionEdges_ = {};
  bool previousJumpHeld_ = false;
  bool previousDashHeld_ = false;
  bool previousAttackHeld_ = false;
  std::uint32_t lastSnapshotPacketsDecoded_ = 0;
  std::uint64_t duplicateSnapshotsIgnored_ = 0;
  std::uint64_t staleSnapshotsIgnored_ = 0;
  std::string connectionError_;
  std::uint32_t pendingMovementTuningCommand_ = 0;
  bool hasPendingMovementTuning_ = false;
  bool hasSnapshot_ = false;
  std::deque<ChatMessage> chatHistory_;
  std::array<RocketProjectileSnapshot, kMaxRocketProjectiles> projectiles_ = {};
  std::array<std::uint32_t, kMaxRocketProjectiles> projectileSequences_ = {};
  std::array<std::uint32_t, kMaxRocketProjectiles> projectileUpdateTicks_ = {};
  std::array<float, kMaxRocketProjectiles> projectileAgesSeconds_ = {};
  std::array<bool, kMaxRocketProjectiles> projectileResting_ = {};
  std::array<bool, kMaxRocketProjectiles> projectileSlotsInitialized_ = {};
  std::array<bool, kMaxRocketProjectiles> projectileTerminal_ = {};
  struct ExplodedProjectileKey {
    std::uint8_t owner = 0;
    std::uint32_t sequence = 0;
    bool valid = false;
  };
  std::array<ExplodedProjectileKey, kMaxRocketProjectiles>
    explodedProjectileKeys_ = {};
  std::array<std::uint32_t, kMaxPlayers> processedExplosionSequences_ = {};
  std::size_t nextExplodedProjectileKey_ = 0;
  std::uint32_t projectileRevision_ = 0;
  bool hasProjectileRevision_ = false;
};

} // namespace lg
