#include "replay/KillcamClientReceiver.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

int expect(bool condition, std::string_view message) {
  if (condition) return 0;
  std::cerr << "FAILED: " << message << '\n';
  return 1;
}

} // namespace

int main() {
  int failures = 0;
  lg::replay::ReplayTransferConfig senderConfig;
  senderConfig.sessionId = 77U;
  senderConfig.retryMilliseconds = 1U;
  senderConfig.timeoutMilliseconds = 100U;
  senderConfig.minimumPacketIntervalMilliseconds = 1U;
  const std::vector<std::uint8_t> source(1400U, 0x23U);
  lg::replay::ReplayTransferSender sender;
  failures += expect(sender.begin(4U, 9U, source, 1U, senderConfig),
                     "client receiver fixture should start a transfer");
  lg::replay::KillcamClientReceiver receiver({20U, 100U});

  const auto begin = sender.nextMessage(1U);
  failures += expect(begin.has_value(), "sender should produce a typed begin");
  if (begin.has_value()) {
    const auto acknowledgement = receiver.receive(*begin, 1U);
    failures += expect(
        acknowledgement.has_value() &&
            std::holds_alternative<lg::replay::ReplayTransferAck>(
                *acknowledgement),
        "client receiver should ACK begin");
    if (acknowledgement.has_value()) {
      sender.acknowledge(
          std::get<lg::replay::ReplayTransferAck>(*acknowledgement));
    }
  }

  if (begin.has_value()) {
    const auto* beginMessage =
        std::get_if<lg::replay::ReplayTransferBegin>(&*begin);
    if (beginMessage != nullptr) {
      lg::replay::ReplayTransferChunk stale;
      stale.transferId = beginMessage->transferId;
      stale.generation = beginMessage->generation + 1U;
      stale.sessionId = beginMessage->sessionId;
      stale.index = 0U;
      stale.count = beginMessage->chunkCount;
      stale.payload = {0x23U};
      stale.crc32 = lg::replay::replayTransferCrc32(stale.payload);
      failures += expect(
          !receiver.receive(stale, 2U).has_value() && !receiver.failed(),
          "stale generation chunk should not affect an active receiver");
    }
  }

  for (std::uint64_t now = 2U; !sender.complete() && now < 20U; ++now) {
    const auto message = sender.nextMessage(now);
    if (!message.has_value()) continue;
    const auto acknowledgement = receiver.receive(*message, now);
    failures += expect(
        acknowledgement.has_value() &&
            std::holds_alternative<lg::replay::ReplayTransferAck>(
                *acknowledgement),
        "client receiver should ACK every valid chunk");
    if (acknowledgement.has_value()) {
      sender.acknowledge(
          std::get<lg::replay::ReplayTransferAck>(*acknowledgement));
    }
  }
  const auto completed = receiver.takeCompleted();
  failures += expect(sender.complete() && completed.has_value() &&
                          *completed == source,
                     "client receiver should expose the exact completed bytes");
  failures += expect(!receiver.active() && !receiver.failed(),
                     "successful client receive should not be marked failed");

  lg::replay::KillcamClientReceiver crcReceiver({20U, 100U});
  failures += expect(begin.has_value() && crcReceiver.receive(*begin, 1U).has_value(),
                     "CRC receiver should accept begin");
  lg::replay::ReplayTransferChunk corruptChunk;
  if (begin.has_value()) {
    const auto* beginMessage =
        std::get_if<lg::replay::ReplayTransferBegin>(&*begin);
    if (beginMessage != nullptr) {
      corruptChunk.transferId = beginMessage->transferId;
      corruptChunk.generation = beginMessage->generation;
      corruptChunk.sessionId = beginMessage->sessionId;
      corruptChunk.count = beginMessage->chunkCount;
      corruptChunk.payload = {0x23U};
      corruptChunk.crc32 = lg::replay::replayTransferCrc32(corruptChunk.payload) ^ 1U;
    }
  }
  const auto crcCancel = crcReceiver.receive(corruptChunk, 2U);
  failures += expect(
      crcCancel.has_value() &&
          std::get<lg::replay::ReplayTransferCancel>(*crcCancel).reason ==
              lg::replay::ReplayTransferCancelReason::Invalid,
      "corrupt chunk CRC should produce an invalid cancel");

  lg::replay::ReplayTransferSender hashSender;
  failures += expect(hashSender.begin(6U, 10U, source, 1U, senderConfig),
                     "whole-file hash fixture should start");
  lg::replay::KillcamClientReceiver hashReceiver({20U, 100U});
  auto hashBegin = hashSender.nextMessage(1U);
  failures += expect(hashBegin.has_value(),
                     "whole-file hash fixture should send begin");
  if (hashBegin.has_value()) {
    auto badBegin = std::get<lg::replay::ReplayTransferBegin>(*hashBegin);
    badBegin.sha256[0] ^= 1U;
    const auto hashAck = hashReceiver.receive(badBegin, 1U);
    failures += expect(
        hashAck.has_value() &&
            std::holds_alternative<lg::replay::ReplayTransferAck>(*hashAck),
        "whole-file hash fixture should ACK begin");
    if (hashAck.has_value()) {
      hashSender.acknowledge(
          std::get<lg::replay::ReplayTransferAck>(*hashAck));
    }
  }
  std::optional<lg::replay::ReplayTransferMessage> hashCancel;
  for (std::uint64_t now = 2U; !hashCancel.has_value() && now < 20U; ++now) {
    const auto message = hashSender.nextMessage(now);
    if (!message.has_value()) continue;
    const auto response = hashReceiver.receive(*message, now);
    if (response.has_value() &&
        std::holds_alternative<lg::replay::ReplayTransferCancel>(*response)) {
      hashCancel = response;
    } else if (response.has_value()) {
      hashSender.acknowledge(
          std::get<lg::replay::ReplayTransferAck>(*response));
    }
  }
  failures += expect(
      hashCancel.has_value() &&
          std::get<lg::replay::ReplayTransferCancel>(*hashCancel).reason ==
              lg::replay::ReplayTransferCancelReason::Invalid &&
          !hashReceiver.takeCompleted().has_value(),
      "whole-file SHA mismatch should cancel without exposing bytes");

  lg::replay::KillcamClientReceiver expiring({5U, 20U});
  failures += expect(begin.has_value() && expiring.receive(*begin, 1U).has_value(),
                     "expiry receiver should accept the same begin");
  const auto timeout = expiring.update(7U);
  failures += expect(
      timeout.has_value() &&
          std::get<lg::replay::ReplayTransferCancel>(*timeout).reason ==
              lg::replay::ReplayTransferCancelReason::Timeout &&
          expiring.failed(),
      "idle receiver should send a timeout cancel");

  lg::replay::KillcamClientReceiver skipped;
  failures += expect(begin.has_value() && skipped.receive(*begin, 1U).has_value(),
                     "skip receiver should accept the same begin");
  const auto skip = skipped.cancel(lg::replay::ReplayTransferCancelReason::Skipped);
  failures += expect(
      skip.has_value() &&
          std::get<lg::replay::ReplayTransferCancel>(*skip).reason ==
              lg::replay::ReplayTransferCancelReason::Skipped &&
          skipped.failed(),
      "user skip should cancel and fail closed");

  lg::replay::KillcamClientReceiver oversized;
  lg::replay::ReplayTransferBegin invalidBegin;
  invalidBegin.transferId = 5U;
  invalidBegin.generation = 9U;
  invalidBegin.sessionId = 77U;
  invalidBegin.chunkCount = lg::replay::kReplayTransferMaxChunks;
  invalidBegin.byteCount =
      static_cast<std::uint32_t>(lg::replay::kReplayTransferMaxSegmentBytes + 1U);
  invalidBegin.sha256.fill(1U);
  const auto tooLarge = oversized.receive(invalidBegin, 1U);
  failures += expect(
      tooLarge.has_value() &&
          std::get<lg::replay::ReplayTransferCancel>(*tooLarge).reason ==
              lg::replay::ReplayTransferCancelReason::TooLarge,
      "oversized begin should be rejected with an explicit cancel");

  return failures == 0 ? 0 : 1;
}
