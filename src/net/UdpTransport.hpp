#pragma once

#include "net/ClientNetworkSimulator.hpp"
#include "net/NetTransport.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <deque>
#include <optional>
#include <string>

namespace lg {

// A full 512-slot correction sweep needs at most 19 datagrams. Keeping over
// three sweeps absorbs short client stalls without allowing an unbounded queue.
inline constexpr std::size_t kMaxQueuedProjectileUpdatePackets = 64;

// Bound socket work and retained commands independently. Excess datagrams stay
// in the socket until the next update; full command queues rely on client retry.
inline constexpr std::size_t kMaxServerDatagramsPerUpdate = 64;
inline constexpr std::size_t kMaxQueuedServerCommands = 256;

class UdpServerTransport final : public NetTransport {
public:
  using NetTransport::receiveReplayTransferMessage;
  using NetTransport::sendReplayTransferMessage;
  explicit UdpServerTransport(std::uint16_t port);
  ~UdpServerTransport() override;

  UdpServerTransport(const UdpServerTransport&) = delete;
  UdpServerTransport& operator=(const UdpServerTransport&) = delete;

  [[nodiscard]] bool initialize();
  void update();

  void sendCommand(const CommandPacket& packet) override;
  [[nodiscard]] bool receiveCommand(CommandPacket& packet) override;
  void sendSnapshot(const ServerSnapshot& snapshot) override;
  [[nodiscard]] bool receiveSnapshot(ServerSnapshot& snapshot) override;
  void sendProjectileUpdates(const ProjectileUpdatePacket& packet) override;
  [[nodiscard]] bool receiveProjectileUpdates(ProjectileUpdatePacket& packet) override;
  void publishChatHistory(const ChatHistory& history) override;
  [[nodiscard]] bool receiveChatHistory(ChatHistoryChunk& chunk) override;
  bool sendReplayTransferMessage(
      std::uint8_t clientIndex,
      const replay::ReplayTransferMessage& message);
  [[nodiscard]] bool receiveReplayTransferMessage(
      std::uint8_t& clientIndex,
      replay::ReplayTransferMessage& message);
  [[nodiscard]] std::optional<std::uint8_t> clientIndexForPlayer(
      std::uint8_t playerIndex) const;
  [[nodiscard]] std::uint32_t clientSession(
      std::uint8_t clientIndex) const;

  [[nodiscard]] std::uint16_t localPort() const;
  [[nodiscard]] std::size_t connectedClientCount() const;
  [[nodiscard]] std::array<bool, kDuelPlayerCount> connectedPlayers() const;
  [[nodiscard]] std::array<std::uint32_t, kDuelPlayerCount>
    connectedPlayerSessions() const;
  [[nodiscard]] const std::string& lastError() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class UdpClientTransport final : public NetTransport {
public:
  UdpClientTransport(std::string host, std::uint16_t port);
  ~UdpClientTransport() override;

  UdpClientTransport(const UdpClientTransport&) = delete;
  UdpClientTransport& operator=(const UdpClientTransport&) = delete;

  [[nodiscard]] bool initialize();
  void disconnect();
  void update();
  void setNetworkSimulationConfig(const ClientNetworkSimulationConfig& config);

  void sendCommand(const CommandPacket& packet) override;
  [[nodiscard]] bool receiveCommand(CommandPacket& packet) override;
  void sendSnapshot(const ServerSnapshot& snapshot) override;
  [[nodiscard]] bool receiveSnapshot(ServerSnapshot& snapshot) override;
  void sendProjectileUpdates(const ProjectileUpdatePacket& packet) override;
  [[nodiscard]] bool receiveProjectileUpdates(ProjectileUpdatePacket& packet) override;
  void publishChatHistory(const ChatHistory& history) override;
  [[nodiscard]] bool receiveChatHistory(ChatHistoryChunk& chunk) override;
  bool sendReplayTransferMessage(
      const replay::ReplayTransferMessage& message) override;
  [[nodiscard]] bool receiveReplayTransferMessage(
      replay::ReplayTransferMessage& message) override;
  [[nodiscard]] SnapshotDiagnostics snapshotDiagnostics() const override;
  [[nodiscard]] NetworkTelemetry networkTelemetry() const override;

  [[nodiscard]] bool connected() const;
  [[nodiscard]] bool timedOut() const;
  [[nodiscard]] std::uint8_t clientIndex() const;
  [[nodiscard]] std::uint8_t playerIndex() const;
  [[nodiscard]] std::uint32_t sessionId() const;
  [[nodiscard]] bool spectator() const;
  [[nodiscard]] float pingMilliseconds() const;
  [[nodiscard]] ClientNetworkSimulationStats networkSimulationStats() const;
  [[nodiscard]] ClientNetworkSimulationConfig networkSimulationConfig() const;
  [[nodiscard]] const std::deque<ClientNetworkSimulationDecision>&
    networkSimulationDecisions() const;
  [[nodiscard]] const std::string& lastError() const;
  [[nodiscard]] const std::string& host() const;
  [[nodiscard]] std::uint16_t port() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace lg
