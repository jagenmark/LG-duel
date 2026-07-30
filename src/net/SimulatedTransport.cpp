#include "net/SimulatedTransport.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace lg {
namespace {

[[nodiscard]] float clampRate(float value) {
  return std::clamp(value, 0.0F, 1.0F);
}

} // namespace

SimulatedTransport::SimulatedTransport(NetworkSimulationConfig config)
  : config_(config), randomState_(config.randomSeed == 0 ? 1U : config.randomSeed) {
  config_.commands.lossRate = clampRate(config_.commands.lossRate);
  config_.commands.duplicationRate = clampRate(config_.commands.duplicationRate);
  config_.commands.reorderRate = clampRate(config_.commands.reorderRate);
  config_.snapshots.lossRate = clampRate(config_.snapshots.lossRate);
  config_.snapshots.duplicationRate = clampRate(config_.snapshots.duplicationRate);
  config_.snapshots.reorderRate = clampRate(config_.snapshots.reorderRate);
}

void SimulatedTransport::sendCommand(const CommandPacket& packet) {
  WirePacket wire;
  if (encodeCommandPacket(packet, wire)) {
    schedule(wire, config_.commands, commands_);
  }
}

bool SimulatedTransport::receiveCommand(CommandPacket& packet) {
  WirePacket wire;
  return receiveWire(commands_, wire) && decodeCommandPacket(wire, packet);
}

void SimulatedTransport::sendSnapshot(const ServerSnapshot& snapshot) {
  WirePacket wire;
  if (encodeBoundedGameplaySnapshot(snapshot, wire)) {
    schedule(wire, config_.snapshots, snapshots_);
  }
}

bool SimulatedTransport::receiveSnapshot(ServerSnapshot& snapshot) {
  WirePacket wire;
  return receiveWire(snapshots_, wire) && decodeServerSnapshot(wire, snapshot);
}

void SimulatedTransport::publishChatHistory(const ChatHistory& history) {
  if (history.messageCount == 0U) {
    return;
  }
  const std::uint32_t latest = history.messages[history.messageCount - 1U].sequence;
  if (latest == publishedChatSequence_) {
    return;
  }
  for (std::size_t first = 0; first < history.messageCount;
       first += kChatHistoryChunkCapacity) {
    ChatHistoryChunk chunk;
    chunk.oldestAvailableSequence = history.messages[0].sequence;
    chunk.latestSequence = latest;
    chunk.messageCount = static_cast<std::uint8_t>(std::min(
      kChatHistoryChunkCapacity,
      static_cast<std::size_t>(history.messageCount) - first
    ));
    for (std::size_t index = 0; index < chunk.messageCount; ++index) {
      chunk.messages[index] = history.messages[first + index];
    }
    WirePacket wire;
    if (encodeChatHistoryChunk(chunk, wire)) {
      schedule(wire, config_.snapshots, chatHistory_);
    }
  }
  publishedChatSequence_ = latest;
}

bool SimulatedTransport::receiveChatHistory(ChatHistoryChunk& chunk) {
  WirePacket wire;
  return receiveWire(chatHistory_, wire) && decodeChatHistoryChunk(wire, chunk);
}

void SimulatedTransport::advanceTicks(std::uint32_t ticks) {
  currentTick_ += ticks;
}

std::uint64_t SimulatedTransport::currentTick() const {
  return currentTick_;
}

const NetworkSimulationStats& SimulatedTransport::stats() const {
  return stats_;
}

void SimulatedTransport::schedule(
  const WirePacket& wire,
  const NetworkSimulationProfile& profile,
  std::vector<ScheduledPacket>& queue
) {
  ++stats_.sentPackets;
  if (randomChance(profile.lossRate)) {
    ++stats_.droppedPackets;
    return;
  }

  const auto enqueue = [&](bool duplicate) {
    const std::int64_t jitter = randomJitter(profile.jitterTicks);
    std::int64_t delay = static_cast<std::int64_t>(profile.latencyTicks) + jitter;
    delay = std::max<std::int64_t>(0, delay);
    if (randomChance(profile.reorderRate)) {
      delay += static_cast<std::int64_t>(profile.reorderExtraDelayTicks);
      if (insertionOrder_ % 2U == 0U) {
        ++delay;
      }
      ++stats_.reorderedPackets;
    }
    if (duplicate) {
      ++delay;
    }

    queue.push_back(ScheduledPacket{
      currentTick_ + static_cast<std::uint64_t>(delay),
      insertionOrder_++,
      wire,
    });
  };

  enqueue(false);
  if (randomChance(profile.duplicationRate)) {
    ++stats_.duplicatedPackets;
    enqueue(true);
  }
}

bool SimulatedTransport::receiveWire(std::vector<ScheduledPacket>& queue, WirePacket& wire) {
  auto selected = queue.end();
  for (auto iterator = queue.begin(); iterator != queue.end(); ++iterator) {
    if (iterator->deliveryTick > currentTick_) {
      continue;
    }
    if (
      selected == queue.end() ||
      iterator->deliveryTick < selected->deliveryTick ||
      (
        iterator->deliveryTick == selected->deliveryTick &&
        iterator->insertionOrder < selected->insertionOrder
      )
    ) {
      selected = iterator;
    }
  }

  if (selected == queue.end()) {
    return false;
  }

  wire = selected->wire;
  queue.erase(selected);
  ++stats_.deliveredPackets;
  return true;
}

std::uint32_t SimulatedTransport::randomU32() {
  std::uint32_t value = randomState_;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  randomState_ = value;
  return value;
}

bool SimulatedTransport::randomChance(float rate) {
  if (rate <= 0.0F) {
    return false;
  }
  if (rate >= 1.0F) {
    return true;
  }
  const double unit = static_cast<double>(randomU32()) /
    static_cast<double>(std::numeric_limits<std::uint32_t>::max());
  return unit < static_cast<double>(rate);
}

std::int64_t SimulatedTransport::randomJitter(std::uint32_t jitterTicks) {
  if (jitterTicks == 0) {
    return 0;
  }
  const std::uint64_t width = (static_cast<std::uint64_t>(jitterTicks) * 2U) + 1U;
  const std::uint64_t sample = static_cast<std::uint64_t>(randomU32()) % width;
  return static_cast<std::int64_t>(sample) - static_cast<std::int64_t>(jitterTicks);
}

} // namespace lg
