#pragma once

#include "net/NetCodec.hpp"
#include "net/NetTransport.hpp"

#include <cstdint>
#include <vector>

namespace lg {

struct NetworkSimulationProfile {
  std::uint32_t latencyTicks = 0;
  std::uint32_t jitterTicks = 0;
  float lossRate = 0.0F;
  float duplicationRate = 0.0F;
  float reorderRate = 0.0F;
  std::uint32_t reorderExtraDelayTicks = 1;
};

struct NetworkSimulationConfig {
  NetworkSimulationProfile commands = {};
  NetworkSimulationProfile snapshots = {};
  std::uint32_t randomSeed = 0x4C474455U;
};

struct NetworkSimulationStats {
  std::uint64_t sentPackets = 0;
  std::uint64_t droppedPackets = 0;
  std::uint64_t duplicatedPackets = 0;
  std::uint64_t reorderedPackets = 0;
  std::uint64_t deliveredPackets = 0;
};

class SimulatedTransport final : public NetTransport {
public:
  explicit SimulatedTransport(NetworkSimulationConfig config = {});

  void sendCommand(const CommandPacket& packet) override;
  [[nodiscard]] bool receiveCommand(CommandPacket& packet) override;

  void sendSnapshot(const ServerSnapshot& snapshot) override;
  [[nodiscard]] bool receiveSnapshot(ServerSnapshot& snapshot) override;
  void publishChatHistory(const ChatHistory& history) override;
  [[nodiscard]] bool receiveChatHistory(ChatHistoryChunk& chunk) override;

  void advanceTicks(std::uint32_t ticks = 1);

  [[nodiscard]] std::uint64_t currentTick() const;
  [[nodiscard]] const NetworkSimulationStats& stats() const;

private:
  struct ScheduledPacket {
    std::uint64_t deliveryTick = 0;
    std::uint64_t insertionOrder = 0;
    WirePacket wire = {};
  };

  void schedule(
    const WirePacket& wire,
    const NetworkSimulationProfile& profile,
    std::vector<ScheduledPacket>& queue
  );
  [[nodiscard]] bool receiveWire(std::vector<ScheduledPacket>& queue, WirePacket& wire);
  [[nodiscard]] std::uint32_t randomU32();
  [[nodiscard]] bool randomChance(float rate);
  [[nodiscard]] std::int64_t randomJitter(std::uint32_t jitterTicks);

  NetworkSimulationConfig config_ = {};
  NetworkSimulationStats stats_ = {};
  std::vector<ScheduledPacket> commands_;
  std::vector<ScheduledPacket> snapshots_;
  std::vector<ScheduledPacket> chatHistory_;
  std::uint32_t publishedChatSequence_ = 0;
  std::uint64_t currentTick_ = 0;
  std::uint64_t insertionOrder_ = 0;
  std::uint32_t randomState_ = 0;
};

} // namespace lg
