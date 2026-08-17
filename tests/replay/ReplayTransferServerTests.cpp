#include "replay/ReplayTransferServer.hpp"
#include "replay/KillcamServerCoordinator.hpp"

#include <algorithm>
#include <array>
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
  lg::replay::ReplayTransferServerConfig config;
  config.maximumSegmentBytes = 4096U;
  config.transfer.retryMilliseconds = 1U;
  config.transfer.timeoutMilliseconds = 20U;
  config.transfer.minimumPacketIntervalMilliseconds = 1U;
  lg::replay::ReplayTransferServer server(config);
  const std::vector<std::uint8_t> bytes(1800U, 0x5aU);

  std::string error;
  failures += expect(
      server.start(0U, 42U, 7U, bytes, 1U, &error, 19U),
      "server transfer should accept a bounded segment");
  failures += expect(server.active(0U) && server.activeCount() == 1U,
                     "server should expose one active transfer");
  failures += expect(
      server.status(0U).has_value() &&
          server.status(0U)->sessionId == 42U &&
          server.status(0U)->generation == 7U &&
          server.status(0U)->lethalSequence == 19U &&
          server.status(0U)->bytes == bytes.size(),
      "server status should retain the authenticated transfer tuple");
  failures += expect(
      !server.start(0U, 42U, 8U, {1U}, 1U, &error),
      "one client should not run two transfers at once");

  const auto begin = server.poll(1U, 1U);
  failures += expect(
      begin.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferBegin>(
              begin.front().message),
      "server poll should enforce one outbound datagram");
  lg::replay::ReplayTransferReceiver receiver;
  if (!begin.empty()) {
    const auto* beginMessage =
        std::get_if<lg::replay::ReplayTransferBegin>(&begin.front().message);
    if (beginMessage != nullptr) {
      const auto acknowledgement = receiver.receiveBegin(*beginMessage, 1U);
      failures += expect(acknowledgement.has_value(),
                         "receiver should acknowledge the server begin");
      if (acknowledgement.has_value()) {
        server.receive(0U, 41U, *acknowledgement);
        failures += expect(
            server.status(0U)->stats.acknowledged == 0U,
            "stale session ACK should not advance the sender");
        server.receive(0U, 42U, *acknowledgement);
      }
    }
  }
  const auto chunk = server.poll(2U, 1U);
  failures += expect(
      chunk.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferChunk>(
              chunk.front().message),
      "server should send a chunk after the begin ACK");
  if (!chunk.empty()) {
    const auto* chunkMessage =
        std::get_if<lg::replay::ReplayTransferChunk>(&chunk.front().message);
    if (chunkMessage != nullptr) {
      const auto acknowledgement = receiver.receiveChunk(*chunkMessage, 2U);
      failures += expect(acknowledgement.has_value(),
                         "receiver should acknowledge the server chunk");
      if (acknowledgement.has_value()) {
        server.receive(0U, 42U, *acknowledgement);
      }
    }
  }
  for (std::uint64_t now = 3U; server.active(0U) && now < 10U; ++now) {
    const auto packets = server.poll(now, 1U);
    if (packets.empty()) continue;
    const auto* nextChunk =
        std::get_if<lg::replay::ReplayTransferChunk>(&packets.front().message);
    if (nextChunk == nullptr) continue;
    const auto acknowledgement = receiver.receiveChunk(*nextChunk, now);
    if (acknowledgement.has_value()) server.receive(0U, 42U, *acknowledgement);
  }
  failures += expect(!server.active(0U),
                     "server should release a completed transfer slot");
  failures += expect(receiver.takeCompleted().has_value(),
                     "receiver should reassemble the server transfer");

  failures += expect(
      !server.start(1U, 43U, 9U, std::vector<std::uint8_t>(4097U), 4U, &error),
      "server should reject a segment over its configured quota");
  failures += expect(server.start(1U, 43U, 9U, {1U}, 4U, &error),
                     "server should accept a later transfer after rejection");
  (void)server.poll(4U, 1U);
  server.cancel(1U, 43U, lg::replay::ReplayTransferCancelReason::Skipped);
  const auto cancel = server.poll(5U, 1U);
  failures += expect(
      cancel.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferCancel>(
              cancel.front().message) &&
      !server.active(1U),
      "server cancel should emit once and release the slot");

  lg::replay::ReplayTransferServer timeoutServer(config);
  failures += expect(timeoutServer.start(0U, 44U, 10U, {1U}, 1U, &error),
                     "timeout fixture should start a server transfer");
  const auto timeoutBegin = timeoutServer.poll(1U, 1U);
  const auto timeoutCancel = timeoutServer.poll(22U, 1U);
  failures += expect(
      timeoutBegin.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferBegin>(
              timeoutBegin.front().message) &&
          timeoutCancel.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferCancel>(
              timeoutCancel.front().message) &&
          std::get<lg::replay::ReplayTransferCancel>(
              timeoutCancel.front().message
          ).reason == lg::replay::ReplayTransferCancelReason::Timeout &&
          !timeoutServer.active(0U),
      "server should send a timeout cancellation at the configured time"
  );

  lg::replay::ReplayTransferServerConfig immediateResetConfig = config;
  immediateResetConfig.transfer.minimumPacketIntervalMilliseconds = 2U;
  lg::replay::ReplayTransferServer immediateResetServer(immediateResetConfig);
  lg::replay::ReplayTransferReceiver immediateResetReceiver;
  failures += expect(
      immediateResetServer.start(0U, 60U, 12U, bytes, 10U, &error),
      "immediate reset fixture should start a transfer");
  const auto immediateBegin = immediateResetServer.poll(10U, 1U);
  failures += expect(
      immediateBegin.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferBegin>(
              immediateBegin.front().message),
      "immediate reset fixture should send begin");
  if (!immediateBegin.empty()) {
    const auto* beginMessage =
        std::get_if<lg::replay::ReplayTransferBegin>(
            &immediateBegin.front().message);
    if (beginMessage != nullptr) {
      const auto acknowledgement =
          immediateResetReceiver.receiveBegin(*beginMessage, 10U);
      failures += expect(acknowledgement.has_value(),
                         "immediate reset receiver should acknowledge begin");
      if (acknowledgement.has_value()) {
        immediateResetServer.receive(0U, 60U, *acknowledgement);
      }
    }
  }
  const auto immediateChunk = immediateResetServer.poll(12U, 1U);
  failures += expect(
      immediateChunk.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferChunk>(
              immediateChunk.front().message),
      "immediate reset fixture should send a chunk");
  immediateResetServer.clear();
  failures += expect(immediateResetServer.active(0U),
                     "reset should retain a slot until cancel delivery");
  const auto immediateCancel = immediateResetServer.poll(12U, 1U);
  failures += expect(
      immediateCancel.size() == 1U &&
          std::holds_alternative<lg::replay::ReplayTransferCancel>(
              immediateCancel.front().message) &&
          !immediateResetServer.active(0U),
      "reset should deliver cancel before releasing a recent transfer");

  lg::replay::ReplayTransferServer fairServer(config);
  failures += expect(fairServer.start(0U, 50U, 10U, {1U}, 0U, &error) &&
                         fairServer.start(1U, 51U, 10U, {2U}, 0U, &error),
                     "server should accept two transfers for fair polling");
  const auto firstFairPacket = fairServer.poll(0U, 1U);
  const auto secondFairPacket = fairServer.poll(1U, 1U);
  failures += expect(
      firstFairPacket.size() == 1U && secondFairPacket.size() == 1U &&
      firstFairPacket.front().clientIndex == 0U &&
      secondFairPacket.front().clientIndex == 1U,
      "packet budget polling should rotate across active clients");

  lg::replay::ReplayTransferServerConfig concurrentConfig;
  concurrentConfig.maximumSegmentBytes =
      lg::replay::kReplayTransferMaxSegmentBytes;
  concurrentConfig.transfer.retryMilliseconds = 100U;
  concurrentConfig.transfer.timeoutMilliseconds = 5000U;
  concurrentConfig.transfer.minimumPacketIntervalMilliseconds = 2U;
  lg::replay::ReplayTransferServer concurrentServer(concurrentConfig);
  const std::vector<std::uint8_t> maximumSegment(
      concurrentConfig.maximumSegmentBytes, 0x6bU);
  std::array<lg::replay::ReplayTransferReceiver, 2U> concurrentReceivers;
  failures += expect(
      concurrentServer.start(0U, 70U, 20U, maximumSegment, 0U, &error) &&
          concurrentServer.start(1U, 71U, 20U, maximumSegment, 0U, &error),
      "default packet budget should start two maximum-size transfers");
  bool concurrentTimeout = false;
  for (std::uint64_t now = 0U;
       now <= concurrentConfig.transfer.timeoutMilliseconds &&
       concurrentServer.activeCount() != 0U;
       now += 8U) {
    const auto packets = concurrentServer.poll(
        now, lg::replay::kDefaultKillcamPacketsPerTick);
    failures += expect(
        packets.size() <= lg::replay::kDefaultKillcamPacketsPerTick,
        "default killcam packet budget should bound each server tick");
    for (const auto& packet : packets) {
      std::optional<lg::replay::ReplayTransferAck> acknowledgement;
      if (const auto* beginMessage = std::get_if<lg::replay::ReplayTransferBegin>(
              &packet.message);
          beginMessage != nullptr) {
        acknowledgement = concurrentReceivers[packet.clientIndex].receiveBegin(
            *beginMessage, now);
      } else if (const auto* chunkMessage =
                     std::get_if<lg::replay::ReplayTransferChunk>(
                         &packet.message);
                 chunkMessage != nullptr) {
        acknowledgement = concurrentReceivers[packet.clientIndex].receiveChunk(
            *chunkMessage, now);
      } else if (const auto* cancelMessage =
                     std::get_if<lg::replay::ReplayTransferCancel>(
                         &packet.message);
                 cancelMessage != nullptr &&
                 cancelMessage->reason ==
                     lg::replay::ReplayTransferCancelReason::Timeout) {
        concurrentTimeout = true;
      }
      if (acknowledgement.has_value()) {
        concurrentServer.receive(
            packet.clientIndex,
            packet.clientIndex == 0U ? 70U : 71U,
            *acknowledgement);
      }
    }
  }
  failures += expect(
      !concurrentTimeout && !concurrentServer.active(0U) &&
          !concurrentServer.active(1U) &&
          concurrentReceivers[0U].takeCompleted().has_value() &&
          concurrentReceivers[1U].takeCompleted().has_value(),
      "default budget should finish two maximum-size killcams before timeout");

  failures += expect(fairServer.start(2U, 52U, 11U, {3U}, 2U, &error),
                     "server should accept a transfer before clear cancellation");
  (void)fairServer.poll(2U, 8U);
  fairServer.clear();
  const auto clearPackets = fairServer.poll(3U, 8U);
  const auto clearCancel = std::find_if(
      clearPackets.begin(), clearPackets.end(),
      [](const lg::replay::ReplayTransferOutbound& packet) {
        const auto* cancelMessage =
            std::get_if<lg::replay::ReplayTransferCancel>(&packet.message);
        return cancelMessage != nullptr &&
               cancelMessage->reason ==
                   lg::replay::ReplayTransferCancelReason::Invalid;
      });
  failures += expect(clearCancel != clearPackets.end(),
                     "clear should deliver invalid cancellation packets");
  failures += expect(fairServer.activeCount() == 0U,
                     "clear cancellation should release all sender slots");

  return failures == 0 ? 0 : 1;
}
