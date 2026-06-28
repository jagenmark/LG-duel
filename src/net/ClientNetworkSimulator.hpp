#pragma once

#include "net/NetCodec.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lg {

enum class ClientNetworkSimDirection {
  Outgoing,
  Incoming,
};

struct ClientNetworkSimulationConfig {
  int latencyMs = 0;
  int jitterMs = 0;
  int lossPercent = 0;
  int reorderPercent = 0;
  std::uint32_t seed = 0;
};

struct ClientNetworkSimulationStats {
  std::size_t queuedOutgoingPackets = 0;
  std::size_t queuedIncomingPackets = 0;
  std::uint64_t droppedOutgoingPackets = 0;
  std::uint64_t droppedIncomingPackets = 0;
  std::uint64_t reorderedOutgoingPackets = 0;
  std::uint64_t reorderedIncomingPackets = 0;
};

enum class ClientNetworkSimAction {
  Immediate,
  Queued,
  Dropped,
};

class ClientNetworkSimulator {
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  void setConfig(const ClientNetworkSimulationConfig& config);
  [[nodiscard]] const ClientNetworkSimulationConfig& config() const;
  [[nodiscard]] ClientNetworkSimulationStats stats() const;
  [[nodiscard]] bool active() const;
  [[nodiscard]] bool hasQueuedPackets() const;

  [[nodiscard]] ClientNetworkSimAction enqueue(
    ClientNetworkSimDirection direction,
    const WirePacket& wire,
    TimePoint now
  );
  [[nodiscard]] bool popDue(
    ClientNetworkSimDirection direction,
    TimePoint now,
    WirePacket& wire
  );
  void clear();

private:
  struct ScheduledDatagram {
    TimePoint deliverAt = {};
    std::uint64_t insertionOrder = 0;
    WirePacket wire = {};
  };

  [[nodiscard]] std::vector<ScheduledDatagram>& queue(
    ClientNetworkSimDirection direction
  );
  [[nodiscard]] const std::vector<ScheduledDatagram>& queue(
    ClientNetworkSimDirection direction
  ) const;
  [[nodiscard]] bool randomChance(int percent);
  [[nodiscard]] int randomJitter(int jitterMs);
  [[nodiscard]] std::uint32_t randomU32();
  [[nodiscard]] int clampedDelayMs();
  [[nodiscard]] int clampedJitterMs();
  [[nodiscard]] int clampedLossPercent();
  [[nodiscard]] int clampedReorderPercent();

  ClientNetworkSimulationConfig config_ = {};
  ClientNetworkSimulationStats counters_ = {};
  std::vector<ScheduledDatagram> outgoing_;
  std::vector<ScheduledDatagram> incoming_;
  std::uint64_t insertionOrder_ = 0;
  std::uint32_t randomState_ = 0x4C474455U;
};

} // namespace lg
