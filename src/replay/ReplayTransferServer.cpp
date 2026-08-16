#include "replay/ReplayTransferServer.hpp"

#include <algorithm>

namespace lg::replay {
namespace {

void setError(std::string* error, const char* message) {
  if (error != nullptr) *error = message;
}

} // namespace

ReplayTransferServer::ReplayTransferServer(ReplayTransferServerConfig config)
    : config_(config) {
  configure(config);
}

void ReplayTransferServer::configure(ReplayTransferServerConfig config) {
  if (config.maximumSegmentBytes == 0U ||
      config.maximumSegmentBytes > kReplayTransferMaxSegmentBytes) {
    config.maximumSegmentBytes = kReplayTransferMaxSegmentBytes;
  }
  config_ = config;
  clear();
}

bool ReplayTransferServer::start(
    std::uint8_t clientIndex,
    std::uint32_t sessionId,
    std::uint32_t generation,
    std::vector<std::uint8_t> bytes,
    std::uint64_t now,
    std::string* error,
    std::uint32_t lethalSequence) {
  if (clientIndex >= slots_.size()) {
    setError(error, "replay transfer client index is out of range");
    return false;
  }
  if (sessionId == 0U || generation == 0U) {
    setError(error, "replay transfer session or generation is invalid");
    return false;
  }
  if (bytes.empty() || bytes.size() > config_.maximumSegmentBytes ||
      bytes.size() > kReplayTransferMaxSegmentBytes) {
    setError(error, "replay transfer segment exceeds the configured quota");
    return false;
  }
  Slot& slot = slots_[clientIndex];
  if (slot.active) {
    setError(error, "replay transfer client already has an active transfer");
    return false;
  }

  ReplayTransferConfig transferConfig = config_.transfer;
  transferConfig.sessionId = sessionId;
  const std::uint32_t transferId = nextTransferId_++;
  if (nextTransferId_ == 0U) nextTransferId_ = 1U;
  if (!slot.sender.begin(transferId, generation, std::move(bytes), now,
                         transferConfig, lethalSequence)) {
    setError(error, "replay transfer sender rejected the segment");
    return false;
  }
  slot.active = true;
  slot.sessionId = sessionId;
  slot.generation = generation;
  slot.bytes = slot.sender.beginMessage().byteCount;
  if (error != nullptr) error->clear();
  return true;
}

void ReplayTransferServer::receive(
    std::uint8_t clientIndex,
    std::uint32_t sessionId,
    const ReplayTransferMessage& message) {
  if (clientIndex >= slots_.size()) return;
  Slot& slot = slots_[clientIndex];
  if (!slot.active || sessionId == 0U || sessionId != slot.sessionId) return;

  if (const auto* ack = std::get_if<ReplayTransferAck>(&message)) {
    if (ack->sessionId == sessionId) slot.sender.acknowledge(*ack);
    return;
  }
  if (const auto* cancelMessage =
          std::get_if<ReplayTransferCancel>(&message)) {
    if (cancelMessage->sessionId == sessionId &&
        cancelMessage->transferId == slot.sender.beginMessage().transferId &&
        cancelMessage->generation == slot.generation) {
      slot.active = false;
      slot = {};
    }
  }
}

void ReplayTransferServer::cancel(
    std::uint8_t clientIndex,
    std::uint32_t sessionId,
    ReplayTransferCancelReason reason) {
  if (clientIndex >= slots_.size() || reason == ReplayTransferCancelReason::None)
    return;
  Slot& slot = slots_[clientIndex];
  if (slot.active && (sessionId == 0U || sessionId == slot.sessionId)) {
    slot.sender.cancel(reason);
  }
}

std::vector<ReplayTransferOutbound> ReplayTransferServer::poll(
    std::uint64_t now,
    std::size_t packetBudget) {
  std::vector<ReplayTransferOutbound> result;
  result.reserve(std::min(packetBudget, slots_.size()));
  if (packetBudget == 0U) return result;
  const std::size_t start = pollCursor_ % slots_.size();
  std::size_t considered = 0U;
  while (considered < slots_.size() && result.size() < packetBudget) {
    const std::size_t index = (start + considered) % slots_.size();
    ++considered;
    Slot& slot = slots_[index];
    if (!slot.active) continue;
    const std::optional<ReplayTransferMessage> message = slot.sender.nextMessage(now);
    if (message.has_value()) {
      result.push_back({static_cast<std::uint8_t>(index), *message});
    }
    if (slot.sender.complete() || slot.sender.stats().cancelled ||
        (message.has_value() &&
         std::holds_alternative<ReplayTransferCancel>(*message))) {
      slot = {};
    }
  }
  pollCursor_ = (start + considered) % slots_.size();
  return result;
}

void ReplayTransferServer::clearClient(
    std::uint8_t clientIndex,
    std::uint32_t sessionId) {
  if (clientIndex >= slots_.size()) return;
  if (sessionId == 0U || !slots_[clientIndex].active ||
      slots_[clientIndex].sessionId == sessionId) {
    slots_[clientIndex] = {};
  }
}

void ReplayTransferServer::clear() {
  // Keep the slot until poll() emits the typed cancellation. Dropping it here
  // would leave the client waiting for its timeout and would also make a
  // generation or map reset indistinguishable from packet loss.
  for (Slot& slot : slots_) {
    if (slot.active) slot.sender.cancel(ReplayTransferCancelReason::Invalid);
  }
}

bool ReplayTransferServer::active(std::uint8_t clientIndex) const {
  return clientIndex < slots_.size() && slots_[clientIndex].active;
}

std::optional<ReplayTransferServerStatus> ReplayTransferServer::status(
    std::uint8_t clientIndex) const {
  if (!active(clientIndex)) return std::nullopt;
  const Slot& slot = slots_[clientIndex];
  return ReplayTransferServerStatus{
      true,
      clientIndex,
      slot.sender.beginMessage().transferId,
      slot.generation,
      slot.sessionId,
      slot.sender.beginMessage().lethalSequence,
      slot.bytes,
      slot.sender.stats()};
}

std::size_t ReplayTransferServer::activeCount() const {
  return static_cast<std::size_t>(std::count_if(
      slots_.begin(), slots_.end(), [](const Slot& slot) { return slot.active; }));
}

} // namespace lg::replay
