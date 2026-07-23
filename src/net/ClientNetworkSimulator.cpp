#include "net/ClientNetworkSimulator.hpp"

#include <algorithm>
#include <iostream>
#include <limits>

namespace lg {
namespace {

constexpr int kMaxSimulatedDelayMs = 5000;
constexpr int kReorderHoldMs = 16;
constexpr std::size_t kMaxQueuedDatagrams = 512;
constexpr std::size_t kMaxRecordedDecisions = 1024;
constexpr std::uint32_t kDefaultSeed = 0x4C474455U;

[[nodiscard]] int clampPercent(int value) {
  return std::clamp(value, 0, 100);
}

} // namespace

void ClientNetworkSimulator::setConfig(const ClientNetworkSimulationConfig& config) {
  const std::uint32_t oldSeed = config_.seed;
  config_ = config;
  config_.latencyMs = std::clamp(config_.latencyMs, 0, kMaxSimulatedDelayMs);
  config_.jitterMs = std::clamp(config_.jitterMs, 0, kMaxSimulatedDelayMs);
  config_.lossPercent = clampPercent(config_.lossPercent);
  config_.reorderPercent = clampPercent(config_.reorderPercent);
  if (config_.seed != oldSeed) {
    // Reset randomness only when the seed changes. Live latency/loss adjustments
    // otherwise continue the same deterministic pseudo-random sequence.
    randomState_ = config_.seed == 0 ? kDefaultSeed : config_.seed;
  }
}

const ClientNetworkSimulationConfig& ClientNetworkSimulator::config() const {
  return config_;
}

ClientNetworkSimulationStats ClientNetworkSimulator::stats() const {
  ClientNetworkSimulationStats stats = counters_;
  stats.queuedOutgoingPackets = outgoing_.size();
  stats.queuedIncomingPackets = incoming_.size();
  return stats;
}

const std::deque<ClientNetworkSimulationDecision>&
ClientNetworkSimulator::decisions() const {
  return decisions_;
}

bool ClientNetworkSimulator::active() const {
  return config_.latencyMs > 0 ||
    config_.jitterMs > 0 ||
    config_.lossPercent > 0 ||
    config_.reorderPercent > 0;
}

bool ClientNetworkSimulator::hasQueuedPackets() const {
  return !outgoing_.empty() || !incoming_.empty();
}

ClientNetworkSimAction ClientNetworkSimulator::enqueue(
  ClientNetworkSimDirection direction,
  const WirePacket& wire,
  TimePoint now
) {
  if (!active()) {
    return ClientNetworkSimAction::Immediate;
  }

  if (randomChance(clampedLossPercent())) {
    if (direction == ClientNetworkSimDirection::Outgoing) {
      ++counters_.droppedOutgoingPackets;
    } else {
      ++counters_.droppedIncomingPackets;
    }
    recordDecision({
      ++decisionSequence_, direction, ClientNetworkSimAction::Dropped,
      0, false, false,
    });
    return ClientNetworkSimAction::Dropped;
  }

  std::vector<ScheduledDatagram>& selectedQueue = queue(direction);
  int delayMs = clampedDelayMs() + randomJitter(clampedJitterMs());
  delayMs = std::clamp(delayMs, 0, kMaxSimulatedDelayMs);
  const bool reordered = randomChance(clampedReorderPercent());
  if (reordered) {
    // Holding a selected datagram creates an opportunity for a later packet to
    // overtake it; the empty-queue offset ensures the first packet is held too.
    delayMs = std::clamp(
      delayMs + kReorderHoldMs + (selectedQueue.empty() ? 1 : 0),
      0,
      kMaxSimulatedDelayMs
    );
    if (direction == ClientNetworkSimDirection::Outgoing) {
      ++counters_.reorderedOutgoingPackets;
    } else {
      ++counters_.reorderedIncomingPackets;
    }
  }
  if (delayMs == 0) {
    recordDecision({
      ++decisionSequence_, direction, ClientNetworkSimAction::Immediate,
      0, reordered, false,
    });
    return ClientNetworkSimAction::Immediate;
  }

  if (selectedQueue.size() >= kMaxQueuedDatagrams) {
    // Bound memory during extreme latency or a stalled consumer. Dropping the
    // new datagram preserves delivery times already scheduled in the queue.
    if (direction == ClientNetworkSimDirection::Outgoing) {
      ++counters_.droppedOutgoingPackets;
    } else {
      ++counters_.droppedIncomingPackets;
    }
    std::cerr << "net_sim queue limit reached; dropping "
              << (direction == ClientNetworkSimDirection::Outgoing ? "outgoing" : "incoming")
              << " datagram\n";
    recordDecision({
      ++decisionSequence_, direction, ClientNetworkSimAction::Dropped,
      delayMs, reordered, true,
    });
    return ClientNetworkSimAction::Dropped;
  }

  selectedQueue.push_back(ScheduledDatagram{
    now + std::chrono::milliseconds(delayMs),
    insertionOrder_++,
    wire,
  });
  recordDecision({
    ++decisionSequence_, direction, ClientNetworkSimAction::Queued,
    delayMs, reordered, false,
  });
  return ClientNetworkSimAction::Queued;
}

bool ClientNetworkSimulator::popDue(
  ClientNetworkSimDirection direction,
  TimePoint now,
  WirePacket& wire
) {
  std::vector<ScheduledDatagram>& selectedQueue = queue(direction);
  auto selected = selectedQueue.end();
  for (auto iterator = selectedQueue.begin(); iterator != selectedQueue.end(); ++iterator) {
    if (iterator->deliverAt > now) {
      continue;
    }
    if (
      selected == selectedQueue.end() ||
      iterator->deliverAt < selected->deliverAt ||
      (
        iterator->deliverAt == selected->deliverAt &&
        iterator->insertionOrder < selected->insertionOrder
      )
    ) {
      // Deliver by scheduled time, then insertion order. Equal-delay packets
      // remain stable while intentionally delayed packets may be overtaken.
      selected = iterator;
    }
  }

  if (selected == selectedQueue.end()) {
    return false;
  }

  wire = selected->wire;
  selectedQueue.erase(selected);
  return true;
}

void ClientNetworkSimulator::clear() {
  // Connection loss/session replacement must discard delayed datagrams so they
  // cannot be injected into a later authoritative timeline.
  outgoing_.clear();
  incoming_.clear();
  decisions_.clear();
}

std::vector<ClientNetworkSimulator::ScheduledDatagram>&
ClientNetworkSimulator::queue(ClientNetworkSimDirection direction) {
  return direction == ClientNetworkSimDirection::Outgoing ? outgoing_ : incoming_;
}

const std::vector<ClientNetworkSimulator::ScheduledDatagram>&
ClientNetworkSimulator::queue(ClientNetworkSimDirection direction) const {
  return direction == ClientNetworkSimDirection::Outgoing ? outgoing_ : incoming_;
}

bool ClientNetworkSimulator::randomChance(int percent) {
  if (percent <= 0) {
    return false;
  }
  if (percent >= 100) {
    return true;
  }
  return static_cast<int>(randomU32() % 100U) < percent;
}

int ClientNetworkSimulator::randomJitter(int jitterMs) {
  if (jitterMs <= 0) {
    return 0;
  }
  const std::uint32_t width = static_cast<std::uint32_t>((jitterMs * 2) + 1);
  return static_cast<int>(randomU32() % width) - jitterMs;
}

std::uint32_t ClientNetworkSimulator::randomU32() {
  // A small fixed xorshift generator makes simulation runs reproducible without
  // depending on platform-specific standard-library engine behavior.
  std::uint32_t value = randomState_ == 0 ? kDefaultSeed : randomState_;
  value ^= value << 13U;
  value ^= value >> 17U;
  value ^= value << 5U;
  randomState_ = value;
  return value;
}

int ClientNetworkSimulator::clampedDelayMs() {
  return std::clamp(config_.latencyMs, 0, kMaxSimulatedDelayMs);
}

int ClientNetworkSimulator::clampedJitterMs() {
  return std::clamp(config_.jitterMs, 0, kMaxSimulatedDelayMs);
}

int ClientNetworkSimulator::clampedLossPercent() {
  return clampPercent(config_.lossPercent);
}

int ClientNetworkSimulator::clampedReorderPercent() {
  return clampPercent(config_.reorderPercent);
}

void ClientNetworkSimulator::recordDecision(
  ClientNetworkSimulationDecision decision
) {
  if (decisions_.size() == kMaxRecordedDecisions) {
    decisions_.pop_front();
  }
  decisions_.push_back(std::move(decision));
}

} // namespace lg
