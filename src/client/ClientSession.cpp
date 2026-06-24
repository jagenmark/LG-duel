#include "client/ClientSession.hpp"

#include <utility>

namespace lg {

bool ClientSession::connect(std::string host, std::uint16_t port) {
  disconnect();
  lastHost_ = std::move(host);
  lastPort_ = port;
  transport_ = std::make_unique<UdpClientTransport>(lastHost_, lastPort_);
  if (!transport_->initialize()) {
    statusMessage_ = transport_->lastError();
    transport_.reset();
    state_ = ClientConnectionState::Failed;
    return false;
  }
  state_ = ClientConnectionState::Connecting;
  connectStarted_ = std::chrono::steady_clock::now();
  statusMessage_ = "Connecting to " + lastHost_ + ':' + std::to_string(lastPort_);
  return true;
}

void ClientSession::disconnect() {
  if (transport_) {
    transport_->disconnect();
  }
  game_.reset();
  transport_.reset();
  state_ = ClientConnectionState::Disconnected;
  statusMessage_ = "Disconnected";
}

bool ClientSession::reconnect() {
  return connect(lastHost_, lastPort_);
}

void ClientSession::update() {
  if (!transport_) {
    return;
  }

  transport_->update();
  if (
    state_ == ClientConnectionState::Connecting &&
    std::chrono::steady_clock::now() - connectStarted_ > std::chrono::seconds(5)
  ) {
    transport_.reset();
    state_ = ClientConnectionState::Failed;
    statusMessage_ = "Connection failed or server is full";
    return;
  }
  if (transport_->timedOut()) {
    game_.reset();
    transport_.reset();
    state_ = ClientConnectionState::Failed;
    statusMessage_ = "Connection timed out";
    return;
  }

  if (transport_->connected() && !game_) {
    game_ = std::make_unique<ClientGame>(*transport_, transport_->playerIndex());
    state_ = ClientConnectionState::Connected;
    statusMessage_ = "Connected to " + lastHost_ + ':' + std::to_string(lastPort_);
  }
  if (game_) {
    game_->receiveSnapshots();
  }
}

void ClientSession::sendCommand(
  const UserCommand& command,
  bool requestReset,
  bool toggleReady,
  bool requestMovementTuning,
  const MovementTuning& movementTuning,
  float playerSizeScaleXY,
  float playerSizeScaleZ,
  float lightningKnockback,
  float rocketKnockback,
  float vampirism,
  std::uint8_t selfDamagePercent,
  std::int32_t healthAmount,
  bool botDodgeEnabled,
  std::int32_t botDodgeMinIntervalMs,
  std::int32_t botDodgeMaxIntervalMs,
  std::string chatMessage,
  std::string playerName,
  std::string mapName,
  bool usePresentedServerTick
) {
  if (game_) {
    game_->sendCommand(
      command,
      requestReset,
      toggleReady,
      requestMovementTuning,
      movementTuning,
      playerSizeScaleXY,
      playerSizeScaleZ,
      lightningKnockback,
      rocketKnockback,
      vampirism,
      selfDamagePercent,
      healthAmount,
      botDodgeEnabled,
      botDodgeMinIntervalMs,
      botDodgeMaxIntervalMs,
      std::move(chatMessage),
      std::move(playerName),
      std::move(mapName),
      usePresentedServerTick
    );
  }
}

ClientConnectionState ClientSession::state() const {
  return state_;
}

bool ClientSession::connected() const {
  return transport_ && transport_->connected();
}

bool ClientSession::readyForPlay() const {
  return game_ && game_->hasSnapshot();
}

std::size_t ClientSession::playerIndex() const {
  return transport_ ? transport_->playerIndex() : 0U;
}

float ClientSession::pingMilliseconds() const {
  return transport_ ? transport_->pingMilliseconds() : 0.0F;
}

std::string_view ClientSession::host() const {
  return lastHost_;
}

std::uint16_t ClientSession::port() const {
  return lastPort_;
}

const std::string& ClientSession::statusMessage() const {
  return statusMessage_;
}

ClientGame* ClientSession::game() {
  return game_.get();
}

const ClientGame* ClientSession::game() const {
  return game_.get();
}

} // namespace lg
