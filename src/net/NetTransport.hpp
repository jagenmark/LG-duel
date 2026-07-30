#pragma once

#include "net/NetProtocol.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace lg {

inline constexpr std::size_t kNetworkTelemetryHistorySamples = 100;

struct NetworkTelemetrySample {
  std::uint64_t serial = 0;
  float pingMilliseconds = 0.0F;
  float snapshotJitterMilliseconds = 0.0F;
  float incomingLossPercent = 0.0F;
  float outgoingLossPercent = 0.0F;
  float incomingKilobitsPerSecond = 0.0F;
  float outgoingKilobitsPerSecond = 0.0F;
  float snapshotAgeMilliseconds = 0.0F;
  std::uint16_t snapshotsReceived = 0;
  std::uint16_t snapshotGaps = 0;
  std::uint16_t lateSnapshots = 0;
  bool interpolationUnderrun = false;
  bool interpolationHardCorrection = false;
  float predictionCorrectionDistance = 0.0F;
};

struct NetworkTelemetry {
  bool valid = false;
  float pingMilliseconds = 0.0F;
  float pingVariationMilliseconds = 0.0F;
  float snapshotJitterMilliseconds = 0.0F;
  float incomingLossPercent = 0.0F;
  float outgoingLossPercent = 0.0F;
  float incomingKilobitsPerSecond = 0.0F;
  float outgoingKilobitsPerSecond = 0.0F;
  float snapshotRate = 0.0F;
  float snapshotAgeMilliseconds = 0.0F;
  std::size_t lastSnapshotBytes = 0;
  std::size_t lastCommandBytes = 0;
  std::uint64_t lateSnapshots = 0;
  std::uint64_t reorderedSnapshots = 0;
  std::uint32_t acknowledgedCommandDatagramSequence = 0;
  std::array<NetworkTelemetrySample, kNetworkTelemetryHistorySamples> history = {};
  std::size_t historyCount = 0;
};

struct SnapshotDiagnostics {
  std::uint32_t snapshotPacketsDecoded = 0;
  float snapshotDecodeMilliseconds = 0.0F;
  std::uint32_t snapshotsApplied = 0;
  float snapshotApplyMilliseconds = 0.0F;
  std::size_t snapshotQueueDepth = 0;
  std::uint64_t duplicateSnapshotsIgnored = 0;
  std::uint64_t staleSnapshotsIgnored = 0;
};

class NetTransport {
public:
  virtual ~NetTransport() = default;

  virtual void sendCommand(const CommandPacket& packet) = 0;
  [[nodiscard]] virtual bool receiveCommand(CommandPacket& packet) = 0;

  virtual void sendSnapshot(const ServerSnapshot& snapshot) = 0;
  [[nodiscard]] virtual bool receiveSnapshot(ServerSnapshot& snapshot) = 0;
  virtual void sendProjectileUpdates(const ProjectileUpdatePacket&) {}
  [[nodiscard]] virtual bool receiveProjectileUpdates(ProjectileUpdatePacket&) {
    return false;
  }
  virtual void publishChatHistory(const ChatHistory&) {}
  [[nodiscard]] virtual bool receiveChatHistory(ChatHistoryChunk&) { return false; }
  [[nodiscard]] virtual SnapshotDiagnostics snapshotDiagnostics() const {
    return {};
  }
  [[nodiscard]] virtual NetworkTelemetry networkTelemetry() const {
    return {};
  }
};

} // namespace lg
