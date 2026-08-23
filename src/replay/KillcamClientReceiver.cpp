#include "replay/KillcamClientReceiver.hpp"

#include <variant>
#include <utility>

namespace lg::replay {

namespace {

std::uint32_t messageSession(const ReplayTransferMessage& message) {
  return std::visit([](const auto& value) { return value.sessionId; }, message);
}

} // namespace

KillcamClientReceiver::KillcamClientReceiver(
    ReplayTransferReceiverConfig config)
    : config_(config), receiver_(config) {}

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
  if (boundSessionId_ == 0U || messageSession(message) != boundSessionId_) {
    return std::nullopt;
  }
  if (const auto* begin = std::get_if<ReplayTransferBegin>(&message)) {
    const auto acknowledgement = receiver_.receiveBegin(*begin, now);
    if (acknowledgement.has_value()) {
      failed_ = false;
      return *acknowledgement;
    }
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
        const ReplayTransferBegin& completedBegin = receiver_.beginMessage();
        completedStatus_ = status();
        completedStatus_->active = false;
        completedStatus_->failed = false;
        completedStatus_->transferId = completedBegin.transferId;
        completedStatus_->generation = completedBegin.generation;
        completedStatus_->sessionId = completedBegin.sessionId;
        completedStatus_->lethalSequence = completedBegin.lethalSequence;
        completedStatus_->expectedBytes = completedBegin.byteCount;
        completedStatus_->receivedBytes = result->size();
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
    if (!receiver_.active()) return std::nullopt;
    const ReplayTransferBegin& activeBegin = receiver_.beginMessage();
    if (cancel->transferId != activeBegin.transferId ||
        cancel->generation != activeBegin.generation ||
        cancel->sessionId != activeBegin.sessionId ||
        cancel->sessionId != boundSessionId_) {
      return std::nullopt;
    }
    receiver_.cancel(*cancel);
    if (receiver_.failed()) failed_ = true;
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
    result.lethalSequence = begin.lethalSequence;
    result.expectedBytes = begin.byteCount;
  } else if (completedStatus_.has_value()) {
    result = *completedStatus_;
  }
  result.receivedBytes = receiver_.receivedBytes();
  return result;
}

void KillcamClientReceiver::bindSession(std::uint32_t sessionId) {
  if (sessionId == boundSessionId_) return;
  reset();
  boundSessionId_ = sessionId;
}

void KillcamClientReceiver::reset() {
  receiver_ = ReplayTransferReceiver(config_);
  completed_.reset();
  completedStatus_.reset();
  boundSessionId_ = 0U;
  failed_ = false;
}

} // namespace lg::replay
