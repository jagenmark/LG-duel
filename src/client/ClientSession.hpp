#pragma once

#include "client/ClientGame.hpp"
#include "net/UdpTransport.hpp"

#include <cstdint>
#include <chrono>
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
    float vampirism = 0.0F,
    std::uint8_t selfDamagePercent = 100,
    std::int32_t healthAmount = 100,
    bool botDodgeEnabled = false,
    std::int32_t botDodgeMinIntervalMs = 250,
    std::int32_t botDodgeMaxIntervalMs = 750,
    std::string chatMessage = {},
    std::string playerName = {},
    bool usePresentedServerTick = true,
    bool requestGameMode = false,
    GameMode requestedGameMode = GameMode::Duel,
    bool requestTeam = false,
    Team requestedTeam = Team::None
  );

  [[nodiscard]] ClientConnectionState state() const;
  [[nodiscard]] bool connected() const;
  [[nodiscard]] bool readyForPlay() const;
  [[nodiscard]] std::size_t playerIndex() const;
  [[nodiscard]] float pingMilliseconds() const;
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
