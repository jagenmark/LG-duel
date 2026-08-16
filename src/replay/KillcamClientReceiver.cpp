#include "replay/KillcamClientReceiver.hpp"

#include <utility>

namespace lg::replay {

KillcamClientReceiver::KillcamClientReceiver(
    ReplayTransferReceiverConfig config)
    : receiver_(config) {}

std::optional<ReplayTransferMessage> KillcamClientReceiver::cancelMessage(
    ReplayTransferCancelReason reason) const {
  if (reason == ReplayTransferCancelReason::None) {
    return std::nullopt;
  }
  const ReplayTransferBegin& begin = receiver_.beginMessage();
  if (begin.transferId == 0U || begin.generation == 0U ||
      begin.sessionId == 0U) {
    return std::nullopt;
  }
  return ReplayTransferCancel{
      begin.transferId, begin.generation, reason, begin.sessionId};
}

std::optional<ReplayTransferMessage> KillcamClientReceiver::receive(
    const ReplayTransferMessage& message,
    std::uint64_t now) {
  if (const auto* begin = std::get_if<ReplayTransferBegin>(&message)) {
    const auto acknowledgement = receiver_.receiveBegin(*begin, now);
    if (acknowledgement.has_value()) return *acknowledgement;
    if (!receiver_.active() && begin->byteCount > kReplayTransferMaxSegmentBytes) {
      failed_ = true;
      return ReplayTransferCancel{begin->transferId, begin->generation,
                                  ReplayTransferCancelReason::TooLarge,
                                  begin->sessionId};
    }
    if (receiver_.failed()) failed_ = true;
    return std::nullopt;
  }

  if (const auto* chunk = std::get_if<ReplayTransferChunk>(&message)) {
    const auto acknowledgement = receiver_.receiveChunk(*chunk, now);
    if (acknowledgement.has_value()) {
      if (const auto result = receiver_.takeCompleted(); result.has_value()) {
        completed_ = std::move(*result);
      }
      if (receiver_.failed()) {
        failed_ = true;
        return cancelMessage(ReplayTransferCancelReason::Invalid);
      }
      return *acknowledgement;
    }
    if (receiver_.failed()) {
      failed_ = true;
      return cancelMessage(ReplayTransferCancelReason::Invalid);
    }
    return std::nullopt;
  }

  if (const auto* cancel = std::get_if<ReplayTransferCancel>(&message)) {
    receiver_.cancel(*cancel);
    failed_ = true;
  }
  return std::nullopt;
}

std::optional<ReplayTransferMessage> KillcamClientReceiver::update(
    std::uint64_t now) {
  if (!receiver_.active()) return std::nullopt;
  const auto timeoutMessage = cancelMessage(ReplayTransferCancelReason::Timeout);
  if (receiver_.expire(now)) {
    failed_ = true;
    return timeoutMessage;
  }
  return std::nullopt;
}

std::optional<ReplayTransferMessage> KillcamClientReceiver::cancel(
    ReplayTransferCancelReason reason) {
  const auto message = cancelMessage(reason);
  if (!message.has_value()) return std::nullopt;
  const auto* cancelPacket = std::get_if<ReplayTransferCancel>(&*message);
  if (cancelPacket != nullptr) receiver_.cancel(*cancelPacket);
  failed_ = true;
  return message;
}

std::optional<std::vector<std::uint8_t>> KillcamClientReceiver::takeCompleted() {
  if (!completed_.has_value()) return std::nullopt;
  std::optional<std::vector<std::uint8_t>> result = std::move(completed_);
  completed_.reset();
  return result;
}

KillcamClientReceiverStatus KillcamClientReceiver::status() const {
  KillcamClientReceiverStatus result;
  result.active = receiver_.active();
  result.failed = failed();
  if (receiver_.active() || receiver_.failed()) {
    const ReplayTransferBegin& begin = receiver_.beginMessage();
    result.transferId = begin.transferId;
    result.generation = begin.generation;
    result.sessionId = begin.sessionId;
    result.expectedBytes = begin.byteCount;
  }
  result.receivedBytes = receiver_.receivedBytes();
  return result;
}

void KillcamClientReceiver::reset() {
  receiver_ = ReplayTransferReceiver{};
  completed_.reset();
  failed_ = false;
}

} // namespace lg::replay
