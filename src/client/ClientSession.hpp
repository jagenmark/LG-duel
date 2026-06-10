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
    const MovementTuning& movementTuning = {}
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
