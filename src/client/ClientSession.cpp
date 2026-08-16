#include "client/ClientSession.hpp"

#include "benchmark/BenchmarkTiming.hpp"

#include <utility>

namespace lg {

bool ClientSession::connect(std::string host, std::uint16_t port) {
  // Connecting replaces the entire transport/game timeline; no prediction or
  // delayed packet state may survive into the new endpoint.
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
    // Send the best-effort disconnect before destroying transport state. UDP
    // delivery is not guaranteed, so the server timeout remains the fallback.
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
  benchmark::ScopedTiming timing(
    benchmark::TimingSubsystem::NetworkProcessing
  );
  if (!transport_) {
    return;
  }

  transport_->update();
  if (
    state_ == ClientConnectionState::Connecting &&
    std::chrono::steady_clock::now() - connectStarted_ > std::chrono::seconds(5)
  ) {
    // The transport retries handshakes internally; this outer deadline turns a
    // silent/full server into a terminal UI state instead of retrying forever.
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
    // The connection slot remains valid for spectators; only the separately
    // assigned player-body slot may index prediction and collision state.
    game_ = std::make_unique<ClientGame>(
      *transport_, transport_->playerIndex(), transport_->clientIndex()
    );
    state_ = ClientConnectionState::Connected;
    statusMessage_ = "Connected to " + lastHost_ + ':' + std::to_string(lastPort_);
  }
  if (game_) {
    game_->receiveSnapshots();
    if (game_->hasConnectionError()) {
      // Map verification and other snapshot-application errors invalidate the
      // whole session; continuing would predict against different authority data.
      statusMessage_ = game_->connectionError();
      game_.reset();
      transport_.reset();
      state_ = ClientConnectionState::Failed;
      return;
    }
  }
}

void ClientSession::setNetworkSimulationConfig(
  const ClientNetworkSimulationConfig& config
) {
  if (transport_) {
    transport_->setNetworkSimulationConfig(config);
  }
}

bool ClientSession::sendReplayTransferMessage(
  const replay::ReplayTransferMessage& message
) {
  return transport_ != nullptr && transport_->sendReplayTransferMessage(message);
}

bool ClientSession::receiveReplayTransferMessage(
  replay::ReplayTransferMessage& message
) {
  return transport_ != nullptr && transport_->receiveReplayTransferMessage(message);
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
  std::int32_t knockbackTimeMs,
  float vampirism,
  std::uint8_t selfDamagePercent,
  std::int32_t healthAmount,
  const WeaponDamageTuning& weaponDamage,
  const WeaponAmmoConfig& weaponAmmo,
  float lightningFireHz,
  bool botDodgeEnabled,
  std::int32_t botDodgeMinIntervalMs,
    std::int32_t botDodgeMaxIntervalMs,
    std::string chatMessage,
    std::string playerName,
    std::string mapName,
    bool usePresentedServerTick,
    bool requestGameMode,
    GameMode requestedGameMode,
    bool requestTeam,
    Team requestedTeam,
    WeaponSwitchingMode weaponSwitchingMode,
    BotCommandType botCommand,
    std::int32_t botCommandValue,
    std::int32_t botCommandMinIntervalMs,
    std::int32_t botCommandMaxIntervalMs,
    bool requestMcGuffinThrow,
    bool wantsScoreboardStats,
    bool requestSpectator
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
      knockbackTimeMs,
      vampirism,
      selfDamagePercent,
      healthAmount,
      weaponDamage,
      weaponAmmo,
      lightningFireHz,
      botDodgeEnabled,
      botDodgeMinIntervalMs,
        botDodgeMaxIntervalMs,
        std::move(chatMessage),
        std::move(playerName),
        std::move(mapName),
        usePresentedServerTick,
        requestGameMode,
        requestedGameMode,
        requestTeam,
        requestedTeam,
        weaponSwitchingMode,
        botCommand,
        botCommandValue,
        botCommandMinIntervalMs,
        botCommandMaxIntervalMs,
        requestMcGuffinThrow,
        wantsScoreboardStats,
        requestSpectator
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
  return game_ ? game_->localPlayerIndex() : 0U;
}

std::size_t ClientSession::clientIndex() const {
  return game_
    ? game_->localClientIndex()
    : transport_ ? transport_->clientIndex() : kNoAssignedPlayer;
}

bool ClientSession::spectator() const {
  return game_ && game_->spectator();
}

float ClientSession::pingMilliseconds() const {
  return transport_ ? transport_->pingMilliseconds() : 0.0F;
}

ClientNetworkSimulationStats ClientSession::networkSimulationStats() const {
  return transport_ ? transport_->networkSimulationStats() : ClientNetworkSimulationStats{};
}

ClientNetworkSimulationConfig ClientSession::networkSimulationConfig() const {
  return transport_ ? transport_->networkSimulationConfig() : ClientNetworkSimulationConfig{};
}

const std::deque<ClientNetworkSimulationDecision>&
ClientSession::networkSimulationDecisions() const {
  static const std::deque<ClientNetworkSimulationDecision> empty;
  return transport_ ? transport_->networkSimulationDecisions() : empty;
}

NetworkTelemetry ClientSession::networkTelemetry() const {
  return transport_ ? transport_->networkTelemetry() : NetworkTelemetry{};
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
