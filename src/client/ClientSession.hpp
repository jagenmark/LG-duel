#pragma once

#include "client/ClientGame.hpp"
#include "net/UdpTransport.hpp"
#include "replay/ReplayTransfer.hpp"

#include <cstdint>
#include <chrono>
#include <deque>
#include <memory>
#include <string>
#include <string_view>

namespace lg {

enum class ClientConnectionState {
  Disconnected,
  Connecting,
  Connected,
  Failed,
};

class ClientSession {
public:
  bool connect(std::string host, std::uint16_t port);
  void disconnect();
  bool reconnect();
  void update();
  void setNetworkSimulationConfig(const ClientNetworkSimulationConfig& config);
  [[nodiscard]] bool sendReplayTransferMessage(
      const replay::ReplayTransferMessage& message);
  [[nodiscard]] bool receiveReplayTransferMessage(
      replay::ReplayTransferMessage& message);

  void sendCommand(
    const UserCommand& command,
    bool requestReset,
    bool toggleReady,
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

  [[nodiscard]] ClientConnectionState state() const;
  [[nodiscard]] bool connected() const;
  [[nodiscard]] bool readyForPlay() const;
  [[nodiscard]] std::size_t playerIndex() const;
  [[nodiscard]] std::size_t clientIndex() const;
  [[nodiscard]] std::uint32_t sessionId() const;
  [[nodiscard]] bool spectator() const;
  [[nodiscard]] float pingMilliseconds() const;
  [[nodiscard]] ClientNetworkSimulationStats networkSimulationStats() const;
  [[nodiscard]] ClientNetworkSimulationConfig networkSimulationConfig() const;
  [[nodiscard]] const std::deque<ClientNetworkSimulationDecision>&
    networkSimulationDecisions() const;
  [[nodiscard]] NetworkTelemetry networkTelemetry() const;
  [[nodiscard]] std::string_view host() const;
  [[nodiscard]] std::uint16_t port() const;
  [[nodiscard]] const std::string& statusMessage() const;
  [[nodiscard]] ClientGame* game();
  [[nodiscard]] const ClientGame* game() const;

private:
  std::unique_ptr<UdpClientTransport> transport_;
  std::unique_ptr<ClientGame> game_;
  std::string lastHost_ = "127.0.0.1";
  std::uint16_t lastPort_ = 27960;
  ClientConnectionState state_ = ClientConnectionState::Disconnected;
  std::chrono::steady_clock::time_point connectStarted_ = {};
  std::string statusMessage_ = "Disconnected";
};

} // namespace lg
